// MBROLA NG - runs tests.txt through the real core pipeline
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/pipeline.h"
#include "langsrc.h"

namespace mbng {

namespace {

std::string collapse(const std::string& s) {
  std::string out;
  for (char c : s) {
    if ((c == ' ' || c == '\t') && (out.empty() || out.back() == ' ')) continue;
    out.push_back(c == '\t' ? ' ' : c);
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

}  // namespace

TestReport run_tests(std::shared_ptr<const Language> lang, const std::vector<TestCase>& tests) {
  TestReport rep;
  Pipeline p(lang);
  p.norm().auto_spell_single = false;
  for (const TestCase& tc : tests) {
    for (const std::string& in : tc.inputs) {
      std::string got;
      try {
        p.begin();
        p.add_text_utf8(in);
        Chunk ch;
        while (p.next_chunk(ch)) {
          const std::string& part = tc.phonemes ? ch.debug_phones : ch.debug_words;
          if (part.empty()) continue;
          if (!got.empty()) got += tc.phonemes ? " _ " : " ";
          got += part;
        }
      } catch (const std::exception& e) {
        got = std::string("<exception: ") + e.what() + ">";
      }
      if (collapse(got) == collapse(tc.expected)) {
        ++rep.passed;
      } else {
        rep.failures.push_back(tc.where + ": \"" + in + "\"\n    expected: " + collapse(tc.expected) +
                               "\n    got:      " + collapse(got));
      }
    }
  }
  return rep;
}

}  // namespace mbng
