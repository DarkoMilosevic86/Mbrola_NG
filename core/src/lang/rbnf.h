// MBROLA NG - rule-based number formatting (ICU/CLDR RBNF subset) and
// CLDR plural rules. Generic: all number words come from language data.
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lang/langdata.h"

namespace mbng {

// CLDR plural operands.
struct PluralOperands {
  int64_t i = 0;  // integer digits
  int v = 0;      // number of visible fraction digits
  int64_t f = 0;  // visible fraction digits as integer
  int64_t t = 0;  // f without trailing zeros
  static PluralOperands from_int(int64_t n);
  // "3,14" / "3.14" / "5" (either separator)
  static PluralOperands from_string(const std::string& s);
};

class PluralRules {
 public:
  // Throws FormatError with a message on syntax errors.
  void compile(const std::vector<PluralRule>& rules);
  PluralCat select(const PluralOperands& o) const;
  PluralCat select(int64_t n) const { return select(PluralOperands::from_int(n)); }

 private:
  struct Relation {
    char operand = 'n';
    int64_t mod = 0;
    bool negate = false;
    std::vector<std::pair<int64_t, int64_t>> ranges;
  };
  using And = std::vector<Relation>;
  struct Rule {
    PluralCat cat;
    std::vector<And> ors;
  };
  std::vector<Rule> rules_;
};

class Rbnf {
 public:
  Rbnf(const Numbers& numbers, const PluralRules& plurals) : num_(numbers), plural_(plurals) {}
  // Formats n with the given ruleset index. Throws FormatError on recursion
  // problems in the data (caught by the compiler's tests).
  std::string format(int64_t n, int ruleset) const;

 private:
  void apply(int64_t n, int ruleset, int depth, std::string& out) const;
  const Numbers& num_;
  const PluralRules& plural_;
};

std::string plural_name(PluralCat c);
int plural_from_name(const std::string& s);  // -1 if unknown

}  // namespace mbng
