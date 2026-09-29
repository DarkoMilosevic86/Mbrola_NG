// MBROLA NG - langc, the language compiler (Language SDK)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
//   langc <source-folder> -o <code>.dat [--no-tests] [--quiet]
//
// Parses the source folder, validates everything, round-trips the binary
// format and runs tests.txt through the real core pipeline.
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "lang/datfile.h"
#include "langsrc.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace mbng;

static int run(const std::vector<std::string>& args) {
  std::string src, out;
  bool tests = true, quiet = false;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "-o" && i + 1 < args.size()) out = args[++i];
    else if (a == "--no-tests") tests = false;
    else if (a == "--quiet" || a == "-q") quiet = true;
    else if (!a.empty() && a[0] != '-' && src.empty()) src = a;
    else {
      std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
      return 2;
    }
  }
  if (src.empty()) {
    std::fprintf(stderr,
                 "MBROLA NG language compiler %d.%d.%d\n"
                 "usage: langc <source-folder> -o <code>.dat [--no-tests] [--quiet]\n",
                 kCompilerVersion >> 16, (kCompilerVersion >> 8) & 0xFF, kCompilerVersion & 0xFF);
    return 2;
  }
  CompileResult r;
  bool ok = compile_language(src, r);
  if (!quiet)
    for (auto& w : r.warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
  for (auto& e : r.errors) std::fprintf(stderr, "error: %s\n", e.c_str());
  if (!ok) {
    std::fprintf(stderr, "%zu error(s)\n", r.errors.size());
    return 1;
  }
  std::vector<uint8_t> dat = write_dat(r.data, static_cast<uint64_t>(std::time(nullptr)));
  std::shared_ptr<const Language> lang;
  try {
    lang = Language::from_dat(dat.data(), dat.size());  // round trip through the real loader
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: compiled data does not load: %s\n", e.what());
    return 1;
  }
  if (!quiet) {
    const LanguageData& d = lang->d();
    std::printf("%s (%s): %zu phonemes, %zu G2P rules, %zu post-lexical rules, %zu lexicon, %zu accents,\n"
                "  %zu number rulesets, %zu patterns, %zu units, %zu abbreviations, %zu symbols, %zu emoji\n",
                d.meta_value("name").c_str(), d.meta_value("code").c_str(), d.phonemes.size(), d.g2p.size(),
                d.postlex.rules.size(), d.lexicon.size(), d.accents.entries.size(), d.numbers.rulesets.size(),
                d.nrules.size(), d.units.size(), d.abbrevs.size(), d.symbols.size(), d.emoji.size());
  }
  int rc = 0;
  if (tests && !r.tests.empty()) {
    TestReport rep = run_tests(lang, r.tests);
    for (auto& f : rep.failures) std::fprintf(stderr, "FAIL %s\n", f.c_str());
    std::printf("tests: %d passed, %zu failed\n", rep.passed, rep.failures.size());
    if (!rep.failures.empty()) rc = 1;
  }
  if (!out.empty()) {
    std::ofstream f(std::filesystem::u8path(out), std::ios::binary);
    f.write(reinterpret_cast<const char*>(dat.data()), static_cast<std::streamsize>(dat.size()));
    if (!f) {
      std::fprintf(stderr, "error: cannot write %s\n", out.c_str());
      return 1;
    }
    if (!quiet) std::printf("wrote %s (%zu bytes)\n", out.c_str(), dat.size());
  }
  return rc;
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
