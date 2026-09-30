// MBROLA NG - C API implementation
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "mbrola_ng.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <deque>
#include <memory>
#include <new>
#include <string>

#include "engine/loader.h"
#include "engine/pipeline.h"
#include "engine/synthproc.h"

using namespace mbng;

struct mbng_engine {
  std::shared_ptr<const Language> lang;
  std::unique_ptr<Pipeline> pipe;
  int offset_unit = MBNG_OFFSET_UTF16;
  int sample_rate = 16000;
  int volume = 100;
  bool phoneme_events = false;
  std::string error;
  // mbng_read_pho state
  bool active = false;
  bool has_chunk = false;
  bool ended = false;
  Chunk chunk;
  double t0_ms = 0;
  // mbng_read state (synthesis process)
  std::string voice_path, synth_path;
  SynthProcess synth;
  std::vector<int16_t> pcm;
  size_t pcm_pos = 0;
  bool chunk_open = false;      // waiting for the end frame of a chunk
  bool utterance_done = false;  // no more chunks
  bool end_sent = false;
  int64_t received = 0;         // samples received from the synthesizer
  int64_t delivered = 0;        // samples handed to the host
  std::deque<mbng_event> pending;
};

static thread_local std::string g_create_error;

namespace {

int fail(mbng_engine* e, int code, const std::string& msg) {
  if (e) e->error = msg;
  else g_create_error = msg;
  return code;
}

// Offsets of every code point in the host's unit.
void offsets_for(const u32str& t, int unit, uint32_t base, std::vector<uint32_t>& offs) {
  offs.resize(t.size() + 1);
  uint32_t pos = base;
  for (size_t i = 0; i < t.size(); ++i) {
    offs[i] = pos;
    pos += unit == MBNG_OFFSET_UTF8 ? utf8_units(t[i]) : unit == MBNG_OFFSET_UTF16 ? utf16_units(t[i]) : 1;
  }
  offs[t.size()] = pos;
}

u32str segment_text(const mbng_segment& s) {
  if (s.text) {
    size_t n = s.length < 0 ? std::strlen(s.text) : static_cast<size_t>(s.length);
    return utf8_to_u32(s.text, n);
  }
  if (s.text16) {
    size_t n = 0;
    if (s.length < 0) while (s.text16[n]) ++n;
    else n = static_cast<size_t>(s.length);
    return utf16_to_u32(s.text16, n);
  }
  return u32str();
}

void fill_event(mbng_engine* e, const Event& ev, double t0, mbng_event& out) {
  std::memset(&out, 0, sizeof out);
  switch (ev.type) {
    case EventType::Word: out.type = MBNG_EVENT_WORD; break;
    case EventType::Sentence: out.type = MBNG_EVENT_SENTENCE; break;
    case EventType::Mark: out.type = MBNG_EVENT_MARK; break;
    case EventType::Phoneme: out.type = MBNG_EVENT_PHONEME; break;
    case EventType::End: out.type = MBNG_EVENT_END; break;
  }
  out.sample = static_cast<int64_t>((t0 + ev.ms) * e->sample_rate / 1000.0);
  out.text_offset = static_cast<int32_t>(ev.src_b);
  out.text_length = static_cast<int32_t>(ev.src_e >= ev.src_b ? ev.src_e - ev.src_b : 0);
  if (ev.type == EventType::Mark) {
    out.value = ev.mark;
    std::strncpy(out.name, ev.name.c_str(), sizeof out.name - 1);
  } else if (ev.type == EventType::Phoneme) {
    out.value = ev.dur_ms;
    if (ev.phoneme >= 0)
      std::strncpy(out.name, e->lang->d().phonemes[ev.phoneme].sym.c_str(), sizeof out.name - 1);
  }
}

}  // namespace

extern "C" {

static void drain_chunk(mbng_engine* e);

int MBNG_CALL mbng_api_version(void) { return MBNG_API_VERSION; }

mbng_engine* MBNG_CALL mbng_create(const mbng_config* cfg, int* error) {
  int dummy;
  if (!error) error = &dummy;
  *error = MBNG_OK;
  if (!cfg || cfg->struct_size < offsetof(mbng_config, phoneme_map)) {
    *error = fail(nullptr, MBNG_ERR_ARGUMENT, "invalid configuration");
    return nullptr;
  }
  try {
    std::unique_ptr<mbng_engine> e(new mbng_engine);
    std::string err;
    if (cfg->language_data && cfg->language_size)
      e->lang = load_language(cfg->language_data, cfg->language_size, err);
    else if (cfg->language_path)
      e->lang = load_language(std::string(cfg->language_path), err);
    else
      err = "no language given";
    if (!e->lang) {
      *error = fail(nullptr, MBNG_ERR_LANGUAGE, err);
      return nullptr;
    }
    e->pipe = std::make_unique<Pipeline>(e->lang);
    if (cfg->voice_path) e->voice_path = cfg->voice_path;
    if (cfg->synth_path) e->synth_path = cfg->synth_path;
    if (!e->voice_path.empty() && !e->synth_path.empty()) {
      std::string serr;
      if (!e->synth.start(e->synth_path, e->voice_path, serr)) {
        *error = fail(nullptr, MBNG_ERR_VOICE, serr);
        return nullptr;
      }
      e->sample_rate = e->synth.sample_rate();
    }
    if (cfg->base_pitch > 0) e->pipe->params().base_pitch = cfg->base_pitch;
    if (cfg->struct_size >= offsetof(mbng_config, phoneme_map) + sizeof(cfg->phoneme_map) && cfg->phoneme_map) {
      // "lang=voice lang=voice ..."
      const LanguageData& d = e->lang->d();
      std::vector<std::string>& syms = e->pipe->params().voice_syms;
      syms.assign(d.phonemes.size(), std::string());
      for (const std::string& pair : split_ws(cfg->phoneme_map)) {
        // symbols may contain '=' themselves ("r==3:" maps r= to 3:): use the
        // right-most '=' whose left part is a phoneme of the language
        size_t eq = std::string::npos;
        int id = -1;
        for (size_t k = pair.size(); k-- > 1;) {
          if (pair[k] != '=' || k + 1 >= pair.size()) continue;
          id = d.find_phoneme(pair.substr(0, k));
          if (id >= 0) { eq = k; break; }
        }
        if (id < 0 || eq == std::string::npos) {
          *error = fail(nullptr, MBNG_ERR_VOICE, "invalid phoneme_map entry: " + pair);
          return nullptr;
        }
        syms[id] = pair.substr(eq + 1);
      }
    }
    if (cfg->offset_unit >= MBNG_OFFSET_UTF8 && cfg->offset_unit <= MBNG_OFFSET_CODEPOINT)
      e->offset_unit = cfg->offset_unit;
    return e.release();
  } catch (const std::bad_alloc&) {
    *error = fail(nullptr, MBNG_ERR_MEMORY, "out of memory");
  } catch (const std::exception& ex) {
    *error = fail(nullptr, MBNG_ERR_INTERNAL, ex.what());
  } catch (...) {
    *error = fail(nullptr, MBNG_ERR_INTERNAL, "internal error");
  }
  return nullptr;
}

void MBNG_CALL mbng_destroy(mbng_engine* e) { delete e; }

int MBNG_CALL mbng_get_audio_format(mbng_engine* e, int* rate, int* bits, int* channels) {
  if (!e) return MBNG_ERR_ARGUMENT;
  if (rate) *rate = e->sample_rate;
  if (bits) *bits = 16;
  if (channels) *channels = 1;
  return MBNG_OK;
}

int MBNG_CALL mbng_set_param(mbng_engine* e, int param, int v) {
  if (!e) return MBNG_ERR_ARGUMENT;
  NormOptions& n = e->pipe->norm();
  ProsodyParams& p = e->pipe->params();
  switch (param) {
    case MBNG_PARAM_RATE: p.rate = std::max(20, std::min(v, 800)) / 100.0; break;
    case MBNG_PARAM_PITCH: p.pitch = std::max(25, std::min(v, 400)) / 100.0; break;
    case MBNG_PARAM_RANGE: p.range = std::max(0, std::min(v, 300)) / 100.0; break;
    case MBNG_PARAM_VOLUME: e->volume = std::max(0, std::min(v, 400)); break;
    case MBNG_PARAM_PUNCTUATION: n.punct_level = std::max(0, std::min(v, 3)); break;
    case MBNG_PARAM_EMOJI: n.emoji = v != 0; break;
    case MBNG_PARAM_CAPITALS: n.capitals = v != 0; break;
    case MBNG_PARAM_DIGITS: n.digits = std::max(0, std::min(v, 2)); break;
    case MBNG_PARAM_AUTO_SPELL: n.auto_spell_single = v != 0; break;
    case MBNG_PARAM_PHONEME_EVENTS: p.phoneme_events = v != 0; break;
    default: return fail(e, MBNG_ERR_ARGUMENT, "unknown parameter");
  }
  return MBNG_OK;
}

int MBNG_CALL mbng_get_param(mbng_engine* e, int param, int* v) {
  if (!e || !v) return MBNG_ERR_ARGUMENT;
  NormOptions& n = e->pipe->norm();
  ProsodyParams& p = e->pipe->params();
  switch (param) {
    case MBNG_PARAM_RATE: *v = static_cast<int>(p.rate * 100 + 0.5); break;
    case MBNG_PARAM_PITCH: *v = static_cast<int>(p.pitch * 100 + 0.5); break;
    case MBNG_PARAM_RANGE: *v = static_cast<int>(p.range * 100 + 0.5); break;
    case MBNG_PARAM_VOLUME: *v = e->volume; break;
    case MBNG_PARAM_PUNCTUATION: *v = n.punct_level; break;
    case MBNG_PARAM_EMOJI: *v = n.emoji; break;
    case MBNG_PARAM_CAPITALS: *v = n.capitals; break;
    case MBNG_PARAM_DIGITS: *v = n.digits; break;
    case MBNG_PARAM_AUTO_SPELL: *v = n.auto_spell_single; break;
    case MBNG_PARAM_PHONEME_EVENTS: *v = p.phoneme_events; break;
    default: return fail(e, MBNG_ERR_ARGUMENT, "unknown parameter");
  }
  return MBNG_OK;
}

int MBNG_CALL mbng_begin(mbng_engine* e, const mbng_segment* segs, int count) {
  if (!e || (count > 0 && !segs) || count < 0) return MBNG_ERR_ARGUMENT;
  try {
    Pipeline& p = *e->pipe;
    drain_chunk(e);  // audio of a cancelled utterance still in the pipe
    e->pcm.clear();
    e->pcm_pos = 0;
    e->pending.clear();
    e->utterance_done = false;
    e->end_sent = false;
    e->received = 0;
    e->delivered = 0;
    p.begin();
    e->active = true;
    e->has_chunk = false;
    e->ended = false;
    e->t0_ms = 0;
    const bool spell_saved = p.norm().spell;
    for (int i = 0; i < count; ++i) {
      const mbng_segment& s = segs[i];
      switch (s.type) {
        case MBNG_SEG_TEXT: {
          u32str t = segment_text(s);
          std::vector<uint32_t> offs;
          offsets_for(t, e->offset_unit, static_cast<uint32_t>(std::max(0, s.offset_base)), offs);
          p.add_text(t, offs);
          break;
        }
        case MBNG_SEG_MARK: p.add_mark(s.value, u32_to_utf8(segment_text(s))); break;
        case MBNG_SEG_BREAK: p.add_silence(std::max(0, std::min(s.value, 60000))); break;
        case MBNG_SEG_RATE: p.set_rate(std::max(10, s.value) / 100.0); break;
        case MBNG_SEG_PITCH: p.set_pitch(std::max(10, s.value) / 100.0); break;
        case MBNG_SEG_RANGE: p.set_range(std::max(0, s.value) / 100.0); break;
        case MBNG_SEG_SPELL_ON: p.norm().spell = true; break;
        case MBNG_SEG_SPELL_OFF: p.norm().spell = false; break;
        case MBNG_SEG_PHONEMES: {
          std::string err;
          uint32_t b = static_cast<uint32_t>(std::max(0, s.offset_base));
          if (!p.add_phonemes(u32_to_utf8(segment_text(s)), b, b, &err)) {
            p.norm().spell = spell_saved;
            return fail(e, MBNG_ERR_ARGUMENT, err);
          }
          break;
        }
        default:
          p.norm().spell = spell_saved;
          return fail(e, MBNG_ERR_ARGUMENT, "unknown segment type");
      }
    }
    p.norm().spell = spell_saved;
    return MBNG_OK;
  } catch (const std::bad_alloc&) {
    return fail(e, MBNG_ERR_MEMORY, "out of memory");
  } catch (const std::exception& ex) {
    return fail(e, MBNG_ERR_INTERNAL, ex.what());
  } catch (...) {
    return fail(e, MBNG_ERR_INTERNAL, "internal error");
  }
}

static bool ensure_synth(mbng_engine* e) {
  if (e->synth.running()) return true;
  std::string err;
  if (e->voice_path.empty() || e->synth_path.empty()) {
    fail(e, MBNG_ERR_SYNTH, "no voice / synthesizer configured");
    return false;
  }
  if (!e->synth.start(e->synth_path, e->voice_path, err)) {
    fail(e, MBNG_ERR_SYNTH, err);
    return false;
  }
  e->sample_rate = e->synth.sample_rate();
  return true;
}

// Reads and discards the rest of a chunk still in the pipe.
static void drain_chunk(mbng_engine* e) {
  std::string err;
  std::vector<int16_t> junk;
  while (e->chunk_open) {
    junk.clear();
    int r = e->synth.read_frame(junk, err);
    if (r <= 0) {
      if (r < 0) e->synth.stop();
      e->chunk_open = false;
    }
  }
}

int MBNG_CALL mbng_read(mbng_engine* e, int16_t* buf, int max_samples, mbng_event* events, int max_events,
                        int* n_events) {
  if (n_events) *n_events = 0;
  if (!e || max_samples < 0 || (max_samples > 0 && !buf)) return MBNG_ERR_ARGUMENT;
  if (!e->active) return 0;
  try {
    if (e->pipe->cancelled()) {
      drain_chunk(e);
      e->pending.clear();
      e->active = false;
      return 0;
    }
    int out = 0;
    std::string err;
    const double vol = e->volume / 100.0;
    while (out < max_samples) {
      if (e->pipe->cancelled()) break;
      if (e->pcm_pos < e->pcm.size()) {
        size_t n = std::min(e->pcm.size() - e->pcm_pos, static_cast<size_t>(max_samples - out));
        for (size_t k = 0; k < n; ++k) {
          double v = e->pcm[e->pcm_pos + k] * vol;
          buf[out + k] = static_cast<int16_t>(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        e->pcm_pos += n;
        out += static_cast<int>(n);
        continue;
      }
      e->pcm.clear();
      e->pcm_pos = 0;
      if (e->chunk_open) {
        int r = e->synth.read_frame(e->pcm, err);
        if (r < 0) {  // the synthesizer died: restart it with the next chunk
          e->synth.stop();
          e->chunk_open = false;
          fail(e, MBNG_ERR_SYNTH, "the synthesizer stopped unexpectedly");
        } else if (r == 0) {
          e->chunk_open = false;
          for (auto& ev : e->pending)  // events rounded past the real audio end
            if (ev.sample > e->received) ev.sample = e->received;
        } else {
          e->received += r;
        }
        continue;
      }
      if (e->utterance_done) break;
      Chunk c;
      if (!e->pipe->next_chunk(c)) {
        e->utterance_done = true;
        break;
      }
      for (const Event& ev : c.events) {
        mbng_event me;
        fill_event(e, ev, 0, me);
        me.sample += e->received;
        e->pending.push_back(me);
      }
      if (c.pho.empty()) continue;
      if (!ensure_synth(e)) {
        e->active = false;
        return MBNG_ERR_SYNTH;
      }
      if (!e->synth.send(c.pho + "#\n", err)) {
        e->synth.stop();
        if (!ensure_synth(e) || !e->synth.send(c.pho + "#\n", err)) {
          e->active = false;
          return fail(e, MBNG_ERR_SYNTH, err);
        }
      }
      e->chunk_open = true;
    }
    e->delivered += out;
    if (e->utterance_done && !e->chunk_open && e->pcm_pos >= e->pcm.size() && !e->end_sent) {
      mbng_event end;
      std::memset(&end, 0, sizeof end);
      end.type = MBNG_EVENT_END;
      end.sample = e->delivered;
      e->pending.push_back(end);
      e->end_sent = true;
    }
    int ne = 0;
    while (!e->pending.empty() && ne < max_events && events && e->pending.front().sample <= e->delivered) {
      events[ne++] = e->pending.front();
      e->pending.pop_front();
    }
    if (n_events) *n_events = ne;
    if (out == 0 && e->end_sent && e->pending.empty()) e->active = false;
    return out;
  } catch (const std::exception& ex) {
    return fail(e, MBNG_ERR_INTERNAL, ex.what());
  } catch (...) {
    return fail(e, MBNG_ERR_INTERNAL, "internal error");
  }
}

int MBNG_CALL mbng_read_pho(mbng_engine* e, char* buf, int size, mbng_event* events, int max_events,
                            int* n_events) {
  if (n_events) *n_events = 0;
  if (!e || (size > 0 && !buf) || size < 0) return MBNG_ERR_ARGUMENT;
  if (!e->active) return fail(e, MBNG_ERR_STATE, "no utterance (call mbng_begin first)");
  try {
    if (!e->has_chunk) {
      if (e->ended || !e->pipe->next_chunk(e->chunk)) {
        if (!e->ended && events && max_events > 0) {
          Event end;
          end.type = EventType::End;
          fill_event(e, end, e->t0_ms, events[0]);
          if (n_events) *n_events = 1;
        }
        e->ended = true;
        return 0;
      }
      e->chunk.pho += "#\n";
      e->has_chunk = true;
    }
    int need = static_cast<int>(e->chunk.pho.size()) + 1;
    if (need > size) return -(1000 + need);
    std::memcpy(buf, e->chunk.pho.c_str(), need);
    int ne = 0;
    for (const Event& ev : e->chunk.events) {
      if (!events || ne >= max_events) break;
      fill_event(e, ev, e->t0_ms, events[ne++]);
    }
    if (n_events) *n_events = ne;
    e->t0_ms += e->chunk.ms;
    e->has_chunk = false;
    return need - 1;
  } catch (const std::exception& ex) {
    return fail(e, MBNG_ERR_INTERNAL, ex.what());
  } catch (...) {
    return fail(e, MBNG_ERR_INTERNAL, "internal error");
  }
}

void MBNG_CALL mbng_cancel(mbng_engine* e) {
  if (e && e->pipe) e->pipe->cancel();
}

const char* MBNG_CALL mbng_last_error(mbng_engine* e) {
  return e ? e->error.c_str() : g_create_error.c_str();
}

}  // extern "C"
