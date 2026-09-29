// MBROLA NG - language source compiler (Language SDK)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Parses a language source folder (languages/<code>/*.txt) into the
// compiled LanguageData model. Used by langc, mbtts and the developer
// source mode of the core. Never part of end-user packages.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lang/langdata.h"
#include "lang/language.h"

namespace mbng {

struct TestCase {
  std::string where;                // file:line
  std::vector<std::string> inputs;  // every input must give `expected`
  std::string expected;
  bool phonemes = false;            // compare phonemes instead of words
};

struct CompileResult {
  LanguageData data;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  std::vector<TestCase> tests;
  std::vector<std::string> files;   // source files read
};

// Returns true when there are no errors.
bool compile_language(const std::string& dir, CompileResult& r);

struct TestReport {
  int passed = 0;
  std::vector<std::string> failures;
};
TestReport run_tests(std::shared_ptr<const Language> lang, const std::vector<TestCase>& tests);

// Developer source mode: compile in memory (registered with set_source_loader).
std::shared_ptr<const Language> load_source_language(const std::string& dir, std::string& error);

}  // namespace mbng
