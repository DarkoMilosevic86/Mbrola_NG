// MBROLA NG - Speech Dispatcher output module sd_mbrola_ng (ANALYSIS 13)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Speech Dispatcher starts this program as `sd_mbrola_ng <config file>` and
// talks to it over stdin/stdout with the output module protocol (the one
// implemented by speech-dispatcher's module_main.c / module_process.c; it is
// implemented here directly so the module needs no speech-dispatcher
// development files):
//
//   server -> module                       module -> server
//   INIT                                   299-msg / 299 OK LOADED SUCCESSFULLY
//   AUDIO, key=value..., "."               207 OK RECEIVING AUDIO SETTINGS,
//                                          203 OK AUDIO INITIALIZED
//   SET, key=value..., "."                 203 OK RECEIVING SETTINGS,
//                                          203 OK SETTINGS RECEIVED
//   SPEAK|CHAR|KEY|SOUND_ICON, text, "."   202 OK RECEIVING MESSAGE,
//                                          200 OK SPEAKING, then events:
//                                          701 BEGIN, 705 audio blocks,
//                                          700 index marks, 702 END|703 STOP
//   STOP / PAUSE                           (703 STOP / 704 PAUSE from speech)
//   LIST VOICES [lang [variant]]           200-name\tlang\tvariant ...
//   LOGLEVEL / DEBUG / QUIT
//
// Audio always goes to the server ("audio_output_method=server",
// speech-dispatcher 0.11+), which plays it through PipeWire/PulseAudio/ALSA
// and reports the index marks when the audio before them has been played.
// MBROLA itself runs in the separate mbrola_ng_synth process (ANALYSIS 4.2).
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mbrola_ng.h"
#include "ssml.h"
#include "voices.h"

#ifndef MBNG_DATADIR
#define MBNG_DATADIR "/usr/share/mbrola-ng"
#endif
#ifndef MBNG_VERSION_STRING
#define MBNG_VERSION_STRING "0.1.0"
#endif
#ifndef MBNG_SYNTH_PATH
#define MBNG_SYNTH_PATH "/usr/libexec/mbrola-ng/mbrola_ng_synth"
#endif

using namespace mbng;

namespace {

// ------------------------------------------------------------------ output
std::mutex g_out_mutex;
FILE* g_log = nullptr;

void log_msg(const char* fmt, ...) {
  if (!g_log) return;
  va_list ap;
  va_start(ap, fmt);
  std::fprintf(g_log, "sd_mbrola_ng: ");
  std::vfprintf(g_log, fmt, ap);
  std::fputc('\n', g_log);
  std::fflush(g_log);
  va_end(ap);
}

bool write_all(const char* p, size_t n) {
  while (n > 0) {
    ssize_t w = ::write(STDOUT_FILENO, p, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    p += w;
    n -= static_cast<size_t>(w);
  }
  return true;
}

// One whole reply or event (may be several lines), never interleaved.
void send(const std::string& text) {
  std::lock_guard<std::mutex> lock(g_out_mutex);
  std::string s = text + "\n";
  write_all(s.data(), s.size());
}

bool little_endian() {
  const uint16_t one = 1;
  return *reinterpret_cast<const unsigned char*>(&one) == 1;
}

// 705 audio block: parameters, then the samples with HDLC-like escaping
// (a newline or 0x7d is sent as 0x7d followed by the byte with bit 5 flipped).
void send_audio(const int16_t* pcm, int samples, int rate) {
  const int max_samples = 4000;  // blocks of at most 8000 bytes
  while (samples > 0) {
    int n = samples > max_samples ? max_samples : samples;
    std::string s;
    s.reserve(static_cast<size_t>(n) * 2 * 102 / 100 + 160);
    char head[200];
    std::snprintf(head, sizeof head,
                  "705-bits=16\n705-num_channels=1\n705-sample_rate=%d\n705-num_samples=%d\n"
                  "705-big_endian=%d\n705-AUDIO",
                  rate, n, little_endian() ? 0 : 1);
    s += head;
    s += '\0';
    const char* b = reinterpret_cast<const char*>(pcm);
    for (int i = 0; i < n * 2; ++i) {
      char c = b[i];
      if (c == '\n' || c == 0x7d) {
        s += static_cast<char>(0x7d);
        s += static_cast<char>(c ^ 0x20);
      } else {
        s += c;
      }
    }
    s += "\n705 AUDIO";
    send(s);
    pcm += n;
    samples -= n;
  }
}

// ------------------------------------------------------------------- input
class LineReader {
 public:
  // Next line without the trailing '\n'; false at end of input.
  bool next(std::string& line) {
    for (;;) {
      size_t nl = buf_.find('\n', pos_);
      if (nl != std::string::npos) {
        line.assign(buf_, pos_, nl - pos_);
        pos_ = nl + 1;
        if (pos_ > 65536) {
          buf_.erase(0, pos_);
          pos_ = 0;
        }
        return true;
      }
      char tmp[4096];
      ssize_t r = ::read(STDIN_FILENO, tmp, sizeof tmp);
      if (r < 0 && errno == EINTR) continue;
      if (r <= 0) return false;
      buf_.append(tmp, static_cast<size_t>(r));
    }
  }

 private:
  std::string buf_;
  size_t pos_ = 0;
};

LineReader g_in;

// ------------------------------------------------------------------ config
struct Config {
  std::string datadir = MBNG_DATADIR;
  std::string synth = MBNG_SYNTH_PATH;
  std::vector<std::string> voice_paths;  // extra voice roots, searched first
  std::string default_voice;
  bool module_punctuation = false;  // false: the server's symbol preprocessing
  bool read_emoji = true;
  bool digits = false;
  int max_rate = 400;               // percent of the normal rate at rate=+100
  bool debug = false;
};

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return std::string();
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

bool truthy(const std::string& v) {
  std::string l = lower(v);
  return l == "1" || l == "yes" || l == "on" || l == "true";
}

// dotconf style: `Name value` or `Name "value"`, # comments
void read_config(const char* path, Config& c) {
  if (!path || !*path) return;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    size_t sp = line.find_first_of(" \t");
    if (sp == std::string::npos) continue;
    std::string key = lower(line.substr(0, sp));
    std::string val = trim(line.substr(sp + 1));
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
    if (key == "mbrolangdatadir") c.datadir = val;
    else if (key == "mbrolangsynth") c.synth = val;
    else if (key == "mbrolangvoicepath") {
      size_t start = 0;
      while (start <= val.size()) {
        size_t colon = val.find(':', start);
        std::string p = val.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!p.empty()) c.voice_paths.push_back(p);
        if (colon == std::string::npos) break;
        start = colon + 1;
      }
    } else if (key == "mbrolangdefaultvoice") c.default_voice = val;
    else if (key == "mbrolangpunctuation") c.module_punctuation = lower(val) == "module";
    else if (key == "mbrolangreademoji") c.read_emoji = truthy(val);
    else if (key == "mbrolangdigitbydigit") c.digits = truthy(val);
    else if (key == "mbrolangmaxrate") c.max_rate = std::atoi(val.c_str());
    else if (key == "debug") c.debug = truthy(val);
  }
  if (c.max_rate < 100) c.max_rate = 100;
  if (c.max_rate > 800) c.max_rate = 800;
}

// ---------------------------------------------------------------- settings
struct Settings {
  int rate = 0, pitch = 0, pitch_range = 0, volume = 0;  // -100 .. +100
  std::string punctuation = "none";  // none / some / most / all
  std::string spelling = "off";
  std::string cap = "none";          // none / spell / icon
  std::string voice_type = "male1";  // male1..3, female1..3, child_male, child_female
  std::string language;              // "hr", "en-US", "" = none
  std::string synthesis_voice;       // voice id from LIST VOICES, "" = none
};

// speechd -100..+100 (0 = normal) -> percent, like the NVDA driver
// (below normal down to 50 %, above normal up to max_rate)
int rate_percent(int r, int max_rate) {
  if (r < -100) r = -100;
  if (r > 100) r = 100;
  double p = r < 0 ? 100.0 * std::pow(2.0, r / 100.0) : 100.0 * std::pow(max_rate / 100.0, r / 100.0);
  return static_cast<int>(p + 0.5);
}

int pitch_percent(int p) {
  if (p < -100) p = -100;
  if (p > 100) p = 100;
  return static_cast<int>(100.0 * std::pow(2.0, p / 100.0) + 0.5);
}

// "en-US" / "hr_HR" / "NULL" -> "en" / "hr" / ""
std::string base_language(const std::string& l) {
  if (l.empty() || l == "NULL" || l == "none") return std::string();
  return lower(l.substr(0, l.find_first_of("-_")));
}

// ------------------------------------------------------------------ module
class Module {
 public:
  bool init(const char* config_path, std::string& msg);
  void run();

 private:
  // commands
  void cmd_speak(int kind);
  void cmd_stop(bool pause);
  void cmd_set();
  void cmd_audio();
  void cmd_loglevel();
  void cmd_debug(const std::string& line);
  void cmd_list_voices(const std::string& line);
  bool read_params(std::vector<std::pair<std::string, std::string>>& out);

  // speech
  void rescan();
  const VoiceInfo* find_voice(const std::string& id) const;
  std::string choose_voice() const;
  bool prepare_engine(std::string& err);
  void apply_params();
  void wait_speech();
  void speech_thread(SsmlResult r);
  void cleanup();

  Config cfg_;
  VoiceSearch search_;
  std::vector<VoiceInfo> voices_;
  Settings set_;                    // received with SET
  std::string voice_;               // chosen voice id
  mbng_engine* engine_ = nullptr;
  std::string engine_voice_;
  int sample_rate_ = 16000;
  std::thread worker_;
  std::atomic<bool> speaking_{false};
  std::atomic<bool> stop_{false};
  std::atomic<bool> pause_{false};
};

enum { KIND_TEXT, KIND_CHAR, KIND_KEY, KIND_ICON };

bool Module::init(const char* config_path, std::string& msg) {
  read_config(config_path, cfg_);
  if (cfg_.debug) g_log = stderr;
  search_ = default_voice_search(cfg_.datadir);
  search_.roots.insert(search_.roots.begin(), cfg_.voice_paths.begin(), cfg_.voice_paths.end());
  if (::access(cfg_.synth.c_str(), X_OK) != 0) {
    msg = "MBROLA NG: synthesizer not found: " + cfg_.synth;
    return false;
  }
  rescan();
  if (voices_.empty()) {
    msg = "MBROLA NG: no voice installed (install one with: mbrola-ng-voices install hr)";
    return false;
  }
  voice_ = find_voice(cfg_.default_voice) ? cfg_.default_voice : voices_.front().id;
  std::string names;
  for (const VoiceInfo& v : voices_) names += " " + v.id;
  msg = "MBROLA NG " MBNG_VERSION_STRING " initialized, voices:" + names;
  return true;
}

void Module::rescan() {
  voices_ = find_voices(search_);
  for (const VoiceInfo& v : voices_) log_msg("voice %s (%s): %s", v.id.c_str(), v.language.c_str(), v.database.c_str());
}

const VoiceInfo* Module::find_voice(const std::string& id) const {
  for (const VoiceInfo& v : voices_)
    if (v.id == id) return &v;
  return nullptr;
}

// A synthesis voice (a name from LIST VOICES) of the requested language
// wins; otherwise the best voice of the language (voice type = gender,
// configured default voice, current voice); otherwise the synthesis voice or
// the current voice.
std::string Module::choose_voice() const {
  if (voices_.empty()) return std::string();  // all removed meanwhile
  const std::string lang = base_language(set_.language);
  const VoiceInfo* sv = set_.synthesis_voice.empty() ? nullptr : find_voice(set_.synthesis_voice);
  if (sv && (lang.empty() || sv->language == lang)) return sv->id;
  const std::string current = find_voice(voice_) ? voice_ : voices_.front().id;
  if (!lang.empty()) {
    const std::string want_gender = set_.voice_type.find("female") != std::string::npos ? "female"
                                    : set_.voice_type.find("male") != std::string::npos ? "male"
                                                                                         : "";
    const VoiceInfo* best = nullptr;
    int best_score = -1;
    for (const VoiceInfo& v : voices_) {
      if (v.language != lang) continue;
      int score = 0;
      if (!want_gender.empty() && v.gender == want_gender) score += 4;
      if (v.id == cfg_.default_voice) score += 2;
      if (v.id == current) score += 1;
      if (score > best_score) {
        best = &v;
        best_score = score;
      }
    }
    if (best) return best->id;
  }
  return sv ? sv->id : current;
}

bool Module::prepare_engine(std::string& err) {
  voice_ = choose_voice();
  if (engine_ && engine_voice_ == voice_) return true;
  if (engine_) {
    mbng_destroy(engine_);
    engine_ = nullptr;
  }
  const VoiceInfo* v = find_voice(voice_);
  if (!v) {
    err = "no voice";
    return false;
  }
  std::string lang = search_.language_dir + "/" + v->language + ".dat";
  mbng_config c;
  std::memset(&c, 0, sizeof c);
  c.struct_size = sizeof c;
  c.language_path = lang.c_str();
  c.voice_path = v->database.c_str();
  c.synth_path = cfg_.synth.c_str();
  c.offset_unit = MBNG_OFFSET_UTF8;
  c.base_pitch = v->base_pitch;
  c.phoneme_map = v->phoneme_map.c_str();
  int code = 0;
  engine_ = mbng_create(&c, &code);
  if (!engine_) {
    err = mbng_last_error(nullptr);
    return false;
  }
  engine_voice_ = voice_;
  int bits = 16, channels = 1;
  mbng_get_audio_format(engine_, &sample_rate_, &bits, &channels);
  log_msg("voice %s loaded, %d Hz", voice_.c_str(), sample_rate_);
  return true;
}

void Module::apply_params() {
  mbng_set_param(engine_, MBNG_PARAM_RATE, rate_percent(set_.rate, cfg_.max_rate));
  mbng_set_param(engine_, MBNG_PARAM_PITCH, pitch_percent(set_.pitch));
  int range = set_.pitch_range + 100;  // 0 .. 200 %
  mbng_set_param(engine_, MBNG_PARAM_RANGE, range < 0 ? 0 : range > 200 ? 200 : range);
  int vol = (set_.volume + 100) / 2;   // speech-dispatcher's default 100 = full volume
  mbng_set_param(engine_, MBNG_PARAM_VOLUME, vol < 0 ? 0 : vol > 100 ? 100 : vol);
  int punct = 0;
  if (cfg_.module_punctuation) {
    const std::string& p = set_.punctuation;
    punct = p == "all" ? 3 : p == "most" ? 2 : p == "some" ? 1 : 0;
  }
  mbng_set_param(engine_, MBNG_PARAM_PUNCTUATION, punct);
  mbng_set_param(engine_, MBNG_PARAM_CAPITALS, set_.cap == "spell" ? 1 : 0);
  mbng_set_param(engine_, MBNG_PARAM_EMOJI, cfg_.read_emoji ? 1 : 0);
  mbng_set_param(engine_, MBNG_PARAM_DIGITS, cfg_.digits ? 1 : 0);
}

void Module::wait_speech() {
  if (worker_.joinable()) {
    if (speaking_) {
      stop_ = true;
      if (engine_) mbng_cancel(engine_);
    }
    worker_.join();
  }
}

void Module::speech_thread(SsmlResult r) {
  send("701 BEGIN");
  std::vector<mbng_segment> segs(r.segments.size());
  for (size_t i = 0; i < r.segments.size(); ++i) {
    const SsmlSegment& s = r.segments[i];
    mbng_segment& m = segs[i];
    std::memset(&m, 0, sizeof m);
    m.type = s.type;
    m.text = s.text.c_str();
    m.length = static_cast<int32_t>(s.text.size());
    m.value = s.value;
  }
  if (mbng_begin(engine_, segs.data(), static_cast<int>(segs.size())) != MBNG_OK) {
    log_msg("begin failed: %s", mbng_last_error(engine_));
  } else {
    const int kBlock = 4000;
    std::vector<int16_t> pcm(kBlock);
    mbng_event ev[64];
    int64_t fed = 0;
    while (!stop_) {
      int n = 0;
      int got = mbng_read(engine_, pcm.data(), kBlock, ev, 64, &n);
      if (got < 0) {
        log_msg("synthesis failed: %s", mbng_last_error(engine_));
        break;
      }
      if (stop_) break;
      int pos = 0;
      for (int k = 0; k < n; ++k) {
        if (ev[k].type != MBNG_EVENT_MARK) continue;
        int64_t cut = ev[k].sample - fed;
        if (cut > got) cut = got;
        if (cut > pos) {
          send_audio(pcm.data() + pos, static_cast<int>(cut) - pos, sample_rate_);
          pos = static_cast<int>(cut);
        }
        if (ev[k].value >= 0 && static_cast<size_t>(ev[k].value) < r.marks.size())
          send("700-" + r.marks[ev[k].value] + "\n700 INDEX MARK");
      }
      if (got > pos) send_audio(pcm.data() + pos, got - pos, sample_rate_);
      fed += got;
      if (got == 0 && n == 0) break;
    }
  }
  if (stop_) send(pause_ ? "704 PAUSE" : "703 STOP");
  else send("702 END");
  speaking_ = false;
}

bool Module::read_params(std::vector<std::pair<std::string, std::string>>& out) {
  std::string line;
  while (g_in.next(line)) {
    if (line == ".") return true;
    size_t eq = line.find('=');
    if (eq != std::string::npos) out.emplace_back(line.substr(0, eq), line.substr(eq + 1));
  }
  return false;
}

void Module::cmd_set() {
  send("203 OK RECEIVING SETTINGS");
  std::vector<std::pair<std::string, std::string>> kv;
  if (!read_params(kv)) return;
  for (const auto& p : kv) {
    const std::string& k = p.first;
    const std::string& v = p.second;
    if (k == "rate") set_.rate = std::atoi(v.c_str());
    else if (k == "pitch") set_.pitch = std::atoi(v.c_str());
    else if (k == "pitch_range") set_.pitch_range = std::atoi(v.c_str());
    else if (k == "volume") set_.volume = std::atoi(v.c_str());
    else if (k == "punctuation_mode") set_.punctuation = v;
    else if (k == "spelling_mode") set_.spelling = v;
    else if (k == "cap_let_recogn") set_.cap = v;
    else if (k == "voice") set_.voice_type = lower(v);
    else if (k == "language") set_.language = v == "NULL" ? std::string() : v;
    else if (k == "synthesis_voice") set_.synthesis_voice = v == "NULL" ? std::string() : v;
  }
  send("203 OK SETTINGS RECEIVED");
}

void Module::cmd_audio() {
  send("207 OK RECEIVING AUDIO SETTINGS");
  std::vector<std::pair<std::string, std::string>> kv;
  if (!read_params(kv)) return;
  bool server = false;
  for (const auto& p : kv)
    if (p.first == "audio_output_method" && p.second == "server") server = true;
  if (server)
    send("203 OK AUDIO INITIALIZED");
  else
    send("300-MBROLA NG sends its audio through the server only (speech-dispatcher 0.11 or newer)\n"
         "300 MODULE ERROR");
}

void Module::cmd_loglevel() {
  send("207 OK RECEIVING LOGLEVEL SETTINGS");
  std::vector<std::pair<std::string, std::string>> kv;
  if (!read_params(kv)) return;
  send("203 OK LOGLEVEL SET");
}

void Module::cmd_debug(const std::string& line) {
  // DEBUG ON <file> | DEBUG OFF
  if (line.compare(0, 9, "DEBUG ON ") == 0) {
    FILE* f = std::fopen(line.substr(9).c_str(), "a");
    if (!f) {
      send("303 CANT OPEN CUSTOM DEBUG FILE");
      return;
    }
    if (g_log && g_log != stderr) std::fclose(g_log);
    g_log = f;
    send("200 OK DEBUGGING ON");
  } else if (line == "DEBUG OFF") {
    if (g_log && g_log != stderr) std::fclose(g_log);
    g_log = cfg_.debug ? stderr : nullptr;
    send("200 OK DEBUGGING OFF");
  } else {
    send("302 ERROR BAD SYNTAX");
  }
}

void Module::cmd_list_voices(const std::string& line) {
  // LIST VOICES [language [variant]]
  std::vector<std::string> words;
  size_t i = 0;
  while (i < line.size()) {
    size_t j = line.find(' ', i);
    if (j == std::string::npos) j = line.size();
    if (j > i) words.push_back(line.substr(i, j - i));
    i = j + 1;
  }
  std::string want = words.size() > 2 ? base_language(words[2]) : std::string();
  std::string want_variant = words.size() > 3 ? lower(words[3]) : std::string();
  if (!speaking_) rescan();  // voices installed meanwhile by mbrola-ng-voices
  std::string out;
  for (const VoiceInfo& v : voices_) {
    if (!want.empty() && v.language != want) continue;
    if (!want_variant.empty() && want_variant != "none") continue;
    out += "200-" + v.id + "\t" + v.language + "\tnone\n";
  }
  if (out.empty()) {
    send("304 CANT LIST VOICES");
    return;
  }
  send(out + "200 OK VOICE LIST SENT");
}

// Number of UTF-8 characters (up to 2).
int utf8_chars(const std::string& s) {
  int n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80 && ++n > 1) break;
  return n;
}

void Module::cmd_speak(int kind) {
  send("202 OK RECEIVING MESSAGE");
  std::string text, line;
  int lines = 0;
  for (;;) {
    if (!g_in.next(line)) return;
    if (line == ".") break;
    if (!line.empty() && line[0] == '.') line.erase(0, 1);  // dot stuffing
    if (lines++) text += '\n';
    text += line;
  }
  if (text.empty()) {
    send("301 ERROR CANT SPEAK");
    return;
  }
  if (kind != KIND_TEXT && lines > 1) {
    send("305 DATA MORE THAN ONE LINE");
    return;
  }
  wait_speech();

  if (kind == KIND_ICON) {  // the server plays sound icons itself
    send("200 OK SPEAKING");
    send("701 BEGIN");
    send("706-" + text + "\n706 ICON");
    send("702 END");
    return;
  }

  SsmlResult r;
  if (kind == KIND_TEXT) {
    r = parse_ssml(text, true);
  } else {
    if (text == "space") text = " ";
    SsmlSegment on, off;
    on.type = MBNG_SEG_SPELL_ON;
    off.type = MBNG_SEG_SPELL_OFF;
    if (kind == KIND_CHAR || utf8_chars(text) == 1) {
      r.segments.push_back(on);
      SsmlSegment t;
      t.type = MBNG_SEG_TEXT;
      t.text = text;
      r.segments.push_back(t);
      r.segments.push_back(off);
    } else {
      // key names: "shift_a", "control_alt_delete", "kp-enter", "F1"
      size_t start = 0;
      while (start <= text.size()) {
        size_t us = text.find('_', start);
        std::string part = text.substr(start, us == std::string::npos ? std::string::npos : us - start);
        if (!part.empty()) {
          SsmlSegment t;
          t.type = MBNG_SEG_TEXT;
          if (utf8_chars(part) == 1) {
            r.segments.push_back(on);
            t.text = part;
            r.segments.push_back(t);
            r.segments.push_back(off);
          } else {
            for (char& c : part)
              if (c == '-') c = ' ';
            t.text = part + " ";
            r.segments.push_back(t);
          }
        }
        if (us == std::string::npos) break;
        start = us + 1;
      }
    }
  }
  if (set_.spelling == "on" && kind == KIND_TEXT) {
    SsmlSegment on;
    on.type = MBNG_SEG_SPELL_ON;
    r.segments.insert(r.segments.begin(), on);
  }

  std::string err;
  if (!prepare_engine(err)) {
    log_msg("cannot speak: %s", err.c_str());
    send("301 ERROR CANT SPEAK");
    return;
  }
  apply_params();
  stop_ = false;
  pause_ = false;
  speaking_ = true;
  send("200 OK SPEAKING");
  worker_ = std::thread(&Module::speech_thread, this, std::move(r));
}

void Module::cmd_stop(bool pause) {
  if (!speaking_) return;
  pause_ = pause;
  stop_ = true;
  if (engine_) mbng_cancel(engine_);
}

void Module::cleanup() {
  wait_speech();
  if (engine_) {
    mbng_destroy(engine_);
    engine_ = nullptr;
  }
}

void Module::run() {
  std::string line;
  while (g_in.next(line)) {
    if (line == "SPEAK") cmd_speak(KIND_TEXT);
    else if (line == "CHAR") cmd_speak(KIND_CHAR);
    else if (line == "KEY") cmd_speak(KIND_KEY);
    else if (line == "SOUND_ICON") cmd_speak(KIND_ICON);
    else if (line == "STOP") cmd_stop(false);
    else if (line == "PAUSE") cmd_stop(true);
    else if (line.compare(0, 11, "LIST VOICES") == 0) cmd_list_voices(line);
    else if (line == "SET") cmd_set();
    else if (line == "AUDIO") cmd_audio();
    else if (line == "LOGLEVEL") cmd_loglevel();
    else if (line.compare(0, 5, "DEBUG") == 0) cmd_debug(line);
    else if (line == "QUIT") {
      cleanup();
      send("210 OK QUIT");
      return;
    } else {
      send("300 ERR UNKNOWN COMMAND");
    }
  }
  cleanup();  // the server closed the pipe
}

}  // namespace

int main(int argc, char** argv) {
  signal(SIGPIPE, SIG_IGN);
  Module m;
  std::string line;
  if (!g_in.next(line) || line != "INIT") {
    std::fprintf(stderr, "sd_mbrola_ng: the server did not start with INIT\n");
    return 3;
  }
  std::string msg;
  if (!m.init(argc > 1 ? argv[1] : nullptr, msg)) {
    send("399-" + msg + "\n399 ERR CANT INIT MODULE");
    return 1;
  }
  send("299-" + msg + "\n299 OK LOADED SUCCESSFULLY");
  m.run();
  return 0;
}
