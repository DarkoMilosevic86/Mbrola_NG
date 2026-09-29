// MBROLA NG - text normalization (ANALYSIS 6.1 - 6.4)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Text -> list of items (canonical words, breaks ...), every item carrying
// the position of the ORIGINAL text it came from (host offset units).
// Driven entirely by language data: patterns (dates, times, ordinals ...),
// number rules, units, abbreviations, symbols, emoji, spelling, acronyms.
#pragma once

#include <string>
#include <vector>

#include "lang/language.h"

namespace mbng {

class Phonology;

struct NormOptions {
  int punct_level = 1;          // 0 none, 1 some, 2 most, 3 all
  bool emoji = true;            // read emoji
  bool spell = false;           // character mode (SPELL_ON)
  bool auto_spell_single = true;  // a lone character is spelled (char echo)
  bool capitals = true;         // say the capital prefix when spelling
  bool digits = false;          // read every number digit by digit
};

enum class ItemKind : uint8_t { Word, Break, Mark, Silence, Rate, Pitch, Range, Phonemes };

enum ItemFlag : uint8_t {
  kItemLetter = 1,  // spelled letter name
};

struct Item {
  ItemKind kind = ItemKind::Word;
  u32str text;                  // canonical word
  uint32_t src_b = 0, src_e = 0;  // position in the host text (host units)
  BreakType brk = BreakType::None;
  int ms = 0;                   // Silence
  int32_t mark = 0;             // Mark
  std::string name;             // Mark name
  double value = 0;             // Rate / Pitch / Range (multiplier)
  std::vector<uint8_t> phones;  // Phonemes
  uint8_t flags = 0;
};

int break_priority(BreakType b);

class Normalizer {
 public:
  Normalizer(const Language& lang, const Phonology& ph);
  // `offs` has text.size()+1 entries: host offset of every code point and
  // the end offset.
  void run(const u32str& text, const std::vector<uint32_t>& offs, const NormOptions& opt,
           std::vector<Item>& out) const;

 private:
  struct Tok;
  struct Ctx;

  void run_ctx(Ctx& c) const;
  void tokenize(Ctx& c) const;
  void merge_thousands(Ctx& c) const;
  size_t try_rules(Ctx& c, size_t i) const;
  bool elem_one(const Ctx& c, const NElem& e, const Tok& t) const;
  bool match_fwd(const Ctx& c, const std::vector<NElem>& el, size_t k, size_t pos,
                 std::vector<size_t>* caps, size_t& end) const;
  bool match_bwd(const Ctx& c, const std::vector<NElem>& el, size_t k, long pos) const;
  size_t try_abbrev(Ctx& c, size_t i) const;
  size_t try_prefix_unit(Ctx& c, size_t i) const;
  int match_unit(const Ctx& c, size_t i, bool prefix_only, size_t& ntok) const;
  size_t number(Ctx& c, size_t i, bool negative, size_t first) const;
  void word(Ctx& c, size_t i) const;
  void symbol(Ctx& c, size_t i) const;
  size_t emoji(Ctx& c, size_t i) const;
  void spell_range(Ctx& c, uint32_t b, uint32_t e, bool caps_prefix) const;

  std::string digits_text(const u32str& digits) const;
  std::string number_text(int64_t v, int ruleset) const;
  void emit_word(Ctx& c, const u32str& canon, uint32_t b, uint32_t e, uint8_t flags = 0) const;
  void emit_break(Ctx& c, BreakType brk, uint32_t b, uint32_t e) const;
  void emit_text(Ctx& c, const std::string& text, uint32_t b, uint32_t e, uint8_t flags = 0) const;
  bool next_starts_sentence(const Ctx& c, size_t i) const;

  const Language& L;
  const Phonology& P;
  char32_t decimal_sep_ = ',';
  std::string decimal_word_;
  int decimal_max_ = 2;
  int max_digits_ = 15;
  bool zero_lead_digits_ = true;
  u32str sep_chars_;          // thousands separator symbols ("." or "," ...)
  bool sep_space_ = false;    // "1 000 000"
  int emoji_repeat_min_ = 3;
  std::string emoji_repeat_;
};

}  // namespace mbng
