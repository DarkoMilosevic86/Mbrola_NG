// MBROLA NG - mbtts, developer command-line tool (Language SDK)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
//   mbtts -l <hr.dat | source-folder> [mode] [options] "text"
//   mbtts -l ... [mode] -f input.txt
// modes:
//   --words      normalized words (default)
//   --phonemes   phonemes after post-lexical rules (with --marks: accents)
//   --pho        MBROLA .pho through the C API (as the synthesizer gets it)
//   --events     events of the C API with sample positions
//   --wav FILE   speech through mbrola_ng_synth into a WAV file
//                (-v voice database, -s synthesizer; default: next to mbtts)
// options:
//   -o FILE      write the output to FILE
//   -r PERCENT   rate (100 = normal)     -p PERCENT  pitch
//   --punct N    punctuation level 0..3  --spell     character mode
//   --digits N   numbers: 0 as numbers, 1 digit by digit, 2 in pairs
//   --marks      show accents/length in --phonemes output
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "engine/loader.h"
#include "engine/pipeline.h"
#include "langsrc.h"
#include "mbrola_ng.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace mbng;

static std::string exe_dir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return std::filesystem::path(buf).parent_path().u8string();
#else
  std::error_code ec;
  auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::string(".") : self.parent_path().string();
#endif
}

static int usage() {
  std::fprintf(stderr,
               "usage: mbtts -l <lang.dat|source-folder> [--words|--phonemes|--pho|--events]\n"
               "             [-o file] [-r rate%%] [-p pitch%%] [--punct 0-3] [--spell] [--marks]\n"
               "             (\"text\" | -f file)\n");
  return 2;
}

static int run(const std::vector<std::string>& args) {
  std::string lang_path, text, out_path, mode = "words", voice, synth, wav;
  int rate = 100, pitch = 100, punct = 1, digits = 0;
  bool spell = false, marks = false, have_text = false;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
    if (a == "-l") lang_path = next();
    else if (a == "--words") mode = "words";
    else if (a == "--phonemes") mode = "phonemes";
    else if (a == "--pho") mode = "pho";
    else if (a == "--events") mode = "events";
    else if (a == "--wav") { mode = "wav"; wav = next(); }
    else if (a == "-v") voice = next();
    else if (a == "-s") synth = next();
    else if (a == "-o") out_path = next();
    else if (a == "-r") rate = std::atoi(next().c_str());
    else if (a == "-p") pitch = std::atoi(next().c_str());
    else if (a == "--punct") punct = std::atoi(next().c_str());
    else if (a == "--digits") digits = std::atoi(next().c_str());
    else if (a == "--spell") spell = true;
    else if (a == "--marks") marks = true;
    else if (a == "-f") {
      std::vector<uint8_t> buf;
      if (!read_file(next(), buf)) { std::fprintf(stderr, "cannot read input file\n"); return 1; }
      text.assign(buf.begin(), buf.end());
      have_text = true;
    } else if (!a.empty() && a[0] != '-' && !have_text) {
      text = a;
      have_text = true;
    } else return usage();
  }
  if (lang_path.empty() || !have_text) return usage();
  set_source_loader(&load_source_language);  // developer tool: source folders allowed

  std::string output;
  if (mode == "wav") {
    if (synth.empty()) {
      synth = (std::filesystem::path(exe_dir()) / "mbrola_ng_synth").u8string();
#ifdef _WIN32
      synth += ".exe";
#endif
    }
    mbng_config cfg = {};
    cfg.struct_size = sizeof cfg;
    cfg.language_path = lang_path.c_str();
    cfg.voice_path = voice.c_str();
    cfg.synth_path = synth.c_str();
    int err = 0;
    mbng_engine* e = mbng_create(&cfg, &err);
    if (!e) { std::fprintf(stderr, "error %d: %s\n", err, mbng_last_error(nullptr)); return 1; }
    mbng_set_param(e, MBNG_PARAM_RATE, rate);
    mbng_set_param(e, MBNG_PARAM_PITCH, pitch);
    mbng_set_param(e, MBNG_PARAM_PUNCTUATION, punct);
    mbng_set_param(e, MBNG_PARAM_DIGITS, digits);
    std::vector<mbng_segment> segs;
    if (spell) segs.push_back({MBNG_SEG_SPELL_ON, nullptr, nullptr, 0, 0, 0});
    segs.push_back({MBNG_SEG_TEXT, text.c_str(), nullptr, -1, 0, 0});
    mbng_begin(e, segs.data(), static_cast<int>(segs.size()));
    std::vector<int16_t> pcm;
    int16_t block[800];
    mbng_event ev[64];
    int nev = 0, words = 0, ends = 0;
    for (;;) {
      int n = mbng_read(e, block, 800, ev, 64, &nev);
      if (n < 0) { std::fprintf(stderr, "error: %s\n", mbng_last_error(e)); mbng_destroy(e); return 1; }
      pcm.insert(pcm.end(), block, block + n);
      for (int k = 0; k < nev; ++k) words += ev[k].type == MBNG_EVENT_WORD, ends += ev[k].type == MBNG_EVENT_END;
      if (n == 0 && nev == 0) break;
    }
    int rate_hz = 16000, bits, ch;
    mbng_get_audio_format(e, &rate_hz, &bits, &ch);
    mbng_destroy(e);
    std::ofstream f(std::filesystem::u8path(wav), std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    uint32_t bytes = static_cast<uint32_t>(pcm.size() * 2);
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(1);
    u32(rate_hz); u32(rate_hz * 2); u16(2); u16(16); f.write("data", 4); u32(bytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), bytes);
    std::printf("%s: %.2f s, %d Hz, %d word events, end event: %s\n", wav.c_str(),
                pcm.size() / double(rate_hz), rate_hz, words, ends ? "yes" : "no");
    return 0;
  }
  if (mode == "pho" || mode == "events") {
    mbng_config cfg = {};
    cfg.struct_size = sizeof cfg;
    cfg.language_path = lang_path.c_str();
    cfg.offset_unit = MBNG_OFFSET_CODEPOINT;
    int err = 0;
    mbng_engine* e = mbng_create(&cfg, &err);
    if (!e) { std::fprintf(stderr, "error %d: %s\n", err, mbng_last_error(nullptr)); return 1; }
    mbng_set_param(e, MBNG_PARAM_RATE, rate);
    mbng_set_param(e, MBNG_PARAM_PITCH, pitch);
    mbng_set_param(e, MBNG_PARAM_PUNCTUATION, punct);
    mbng_set_param(e, MBNG_PARAM_DIGITS, digits);
    std::vector<mbng_segment> segs;
    if (spell) segs.push_back({MBNG_SEG_SPELL_ON, nullptr, nullptr, 0, 0, 0});
    segs.push_back({MBNG_SEG_TEXT, text.c_str(), nullptr, -1, 0, 0});
    if (mbng_begin(e, segs.data(), static_cast<int>(segs.size())) != MBNG_OK) {
      std::fprintf(stderr, "error: %s\n", mbng_last_error(e));
      mbng_destroy(e);
      return 1;
    }
    std::vector<char> buf(4096);
    mbng_event ev[256];
    for (;;) {
      int ne = 0;
      int n = mbng_read_pho(e, buf.data(), static_cast<int>(buf.size()), ev, 256, &ne);
      if (n <= -1000) { buf.resize(static_cast<size_t>(-n - 1000)); continue; }
      if (n < 0) { std::fprintf(stderr, "error: %s\n", mbng_last_error(e)); break; }
      if (mode == "pho" && n > 0) output += std::string(buf.data(), n);
      if (mode == "events") {
        static const char* const names[] = {"?", "WORD", "SENTENCE", "MARK", "PHONEME", "END"};
        for (int k = 0; k < ne; ++k) {
          const mbng_event& x = ev[k];
          u32str t = utf8_to_u32(text);
          std::string frag = x.type == MBNG_EVENT_WORD || x.type == MBNG_EVENT_SENTENCE
                                 ? u32_to_utf8(t.substr(std::min<size_t>(x.text_offset, t.size()), x.text_length))
                                 : std::string(x.name);
          char line[512];
          std::snprintf(line, sizeof line, "%8lld  %-8s %4d+%-3d %s\n", static_cast<long long>(x.sample),
                        names[x.type >= 1 && x.type <= 5 ? x.type : 0], x.text_offset, x.text_length, frag.c_str());
          output += line;
        }
      }
      if (n == 0) break;
    }
    mbng_destroy(e);
  } else {
    std::string err;
    auto lang = load_language(lang_path, err);
    if (!lang) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    Pipeline p(lang);
    p.norm().punct_level = punct;
    p.norm().digits = digits;
    p.norm().spell = spell;
    p.params().rate = rate / 100.0;
    p.begin();
    p.add_text_utf8(text);
    Chunk ch;
    while (p.next_chunk(ch)) {
      if (mode == "words") {
        if (!ch.debug_words.empty()) output += ch.debug_words + "\n";
      } else {
        // re-run phonology for the accent marks when requested
        if (!marks) {
          if (!ch.debug_phones.empty()) output += ch.debug_phones + "\n";
        } else {
          std::string line;
          for (auto& w : split_ws(ch.debug_words)) {
            PWord pw = p.phonology().analyse(utf8_to_u32(w));
            if (!line.empty()) line += " | ";
            line += p.phonology().to_string(pw.segs, true);
          }
          output += line + "\n";
        }
      }
    }
  }
  if (out_path.empty()) {
    std::fwrite(output.data(), 1, output.size(), stdout);
  } else {
    std::ofstream f(std::filesystem::u8path(out_path), std::ios::binary);
    f << output;
  }
  return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, &s[0], n, nullptr, nullptr);
    args.push_back(s);
  }
  return run(args);
}
#else
int main(int argc, char** argv) { return run(std::vector<std::string>(argv + 1, argv + argc)); }
#endif
