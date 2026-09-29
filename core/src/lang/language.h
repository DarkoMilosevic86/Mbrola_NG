// MBROLA NG - a loaded language: data + runtime lookup indexes
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <bitset>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lang/langdata.h"
#include "lang/rbnf.h"
#include "text/canon.h"

namespace mbng {

class Language {
 public:
  // Takes ownership of the data and builds the indexes. Throws FormatError.
  explicit Language(LanguageData data);
  Language(const Language&) = delete;
  Language& operator=(const Language&) = delete;
  static std::shared_ptr<const Language> from_dat(const uint8_t* p, size_t n);

  const LanguageData& d() const { return d_; }
  const PluralRules& plurals() const { return plurals_; }
  const Rbnf& rbnf() const { return *rbnf_; }

  const Canonicalizer& canon() const { return *canon_; }
  bool in_phclass(uint16_t cls, uint8_t ph) const { return cls < phcls_.size() && phcls_[cls][ph]; }
  bool in_letterclass(uint16_t cls, char32_t c) const;
  bool is_vowel(uint8_t ph) const { return ph < d_.phonemes.size() && d_.phonemes[ph].type == PhType::Vowel; }
  uint8_t pause_phoneme() const { return pause_; }

  // Lexicon key of a canonical word (lexkey folding applied).
  u32str lex_key(const u32str& canon) const { return canon_->lexkey(canon); }
  const LexEntry* lexicon(const u32str& key) const;
  const AccEntry* accent(const u32str& key) const;
  const Clitic* clitic(const u32str& key) const;
  const std::vector<uint32_t>& g2p_rules_for(char32_t first) const;

  // Emoji: longest match at s[pos]; returns entry index or -1 and length.
  int match_emoji(const u32str& s, size_t pos, size_t& len) const;
  bool may_start_emoji(char32_t c) const { return emoji_start_.count(c) != 0; }

  const Symbol* symbol(char32_t c) const;
  const SpellEntry* spelling(char32_t c) const;
  bool list_contains(uint16_t list, const u32str& w) const;
  const std::string* map_value(uint16_t map, int64_t n) const;
  int abbrev(const u32str& key, bool raw) const;
  int unit(const u32str& key, bool raw) const;
  size_t max_abbrev_len() const { return max_abbrev_len_; }
  size_t max_unit_len() const { return max_unit_len_; }
  double prosody(const std::string& key, double def) const;

  int default_cardinal() const { return default_cardinal_; }
  int digit_ruleset() const { return digit_ruleset_; }

 private:
  LanguageData d_;
  PluralRules plurals_;
  std::unique_ptr<Rbnf> rbnf_;
  std::vector<std::bitset<256>> phcls_;
  std::unique_ptr<Canonicalizer> canon_;
  std::unordered_map<char32_t, std::vector<uint32_t>> g2p_index_;
  std::unordered_map<u32str, int> emoji_;
  std::unordered_set<char32_t> emoji_start_;
  size_t emoji_max_ = 0;
  std::unordered_map<u32str, int> abbr_canon_, abbr_raw_, unit_canon_, unit_raw_;
  size_t max_abbrev_len_ = 0, max_unit_len_ = 0;
  uint8_t pause_ = kNoPhoneme;
  int default_cardinal_ = -1, digit_ruleset_ = -1;
};

}  // namespace mbng
