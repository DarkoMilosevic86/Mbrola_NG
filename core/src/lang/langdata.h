// MBROLA NG - compiled language data model
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
// This is everything the core knows about a language. It is produced by the
// language compiler (langc) from a source folder and stored in <code>.dat
// (datfile.cpp). The core contains NO language knowledge: every table below
// comes from data (ANALYSIS 5.2, D7).
#pragma once

#include <bitset>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "util/text.h"

namespace mbng {

constexpr int kMaxPhonemes = 250;
constexpr uint8_t kNoPhoneme = 255;

// ---------------------------------------------------------------- phonemes
enum class PhType : uint8_t { Pause = 0, Vowel = 1, Consonant = 2 };

struct Phoneme {
  std::string sym;                    // SAMPA symbol of the target voice
  PhType type = PhType::Consonant;
  std::vector<std::string> features;  // free-form (documentation, classes)
  uint16_t dur = 70;                  // default duration, ms
  uint16_t min_dur = 30;              // minimum duration at high rates, ms
  uint8_t partner = kNoPhoneme;       // voicing partner (p <-> b ...)
  uint8_t viseme = 0;
};

struct PhClass {
  std::string name;
  std::vector<uint8_t> members;
};

struct LetterClass {
  std::string name;
  u32str members;  // sorted canonical letters
};

// ----------------------------------------------------------------- scripts
struct CharEntry {
  char32_t ch = 0;
  uint8_t script = 0;
  bool upper = false;
  char32_t lower = 0;  // lowercase form of the character in the same script
  u32str canon;        // canonical (script-independent) letters
};

struct Homoglyph {
  uint8_t script = 0;  // letters are converted INTO this script
  char32_t from = 0;
  char32_t to = 0;
};

struct Scripts {
  std::vector<std::string> names;
  std::vector<CharEntry> chars;                   // sorted by ch
  std::vector<Homoglyph> homoglyphs;
  std::vector<std::pair<u32str, u32str>> normalize;  // text clean-up, longest match
  std::vector<std::pair<char32_t, u32str>> lexkey;   // canonical -> lexicon key folding
};

// --------------------------------------------------------------------- G2P
enum class CtxKind : uint8_t { Char = 0, Class = 1, Boundary = 2 };

struct CtxElem {
  CtxKind kind = CtxKind::Char;
  char32_t ch = 0;
  uint16_t cls = 0;
};

struct G2PRule {
  u32str match;                 // canonical letters consumed
  std::vector<CtxElem> left;    // written left-to-right
  std::vector<CtxElem> right;
  std::vector<uint8_t> out;     // phoneme ids (may be empty)
  uint32_t line = 0;
};

// ---------------------------------------------- phoneme-level rule contexts
enum class PKind : uint8_t {
  Phoneme = 0,
  Class = 1,
  Boundary = 2,     // "#"  word boundary required here
  OptBoundary = 3,  // "#?" word boundary allowed here
};

struct PElem {
  PKind kind = PKind::Phoneme;
  uint16_t id = 0;
};

enum class PostOp : uint8_t { Replace = 0, Delete = 1, Partner = 2 };

struct PostlexRule {
  PElem focus;
  std::vector<PElem> left, right;
  PostOp op = PostOp::Replace;
  std::vector<uint8_t> repl;
  uint32_t line = 0;
};

// Two identical adjacent phonemes (the voice has no X-X diphone).
enum class IdentOp : uint8_t { Merge = 0, Pause = 1, Keep = 2 };

struct IdentRule {
  uint16_t cls = 0;
  IdentOp within = IdentOp::Merge;
  uint16_t within_val = 150;  // merge: duration percent; pause: ms
  IdentOp across = IdentOp::Merge;
  uint16_t across_val = 150;
};

struct Postlex {
  std::vector<PostlexRule> rules;
  std::vector<IdentRule> identical;
};

// ------------------------------------------------------------ syllables
struct SyllRule {
  uint8_t ph = 0;  // phoneme that becomes a syllable nucleus in this context
  std::vector<PElem> left, right;  // Boundary = word edge
};

struct Syllables {
  uint16_t nucleus_class = 0xFFFF;  // 0xFFFF = all vowels
  std::vector<SyllRule> syllabic;
};

// ------------------------------------------------------ lexicon / accents
enum PhFlag : uint8_t {
  kLong = 1,
  kAccFalling = 2,
  kAccRising = 4,
};

struct PhTok {
  uint8_t ph = 0;
  uint8_t flags = 0;
};

struct LexEntry {
  u32str key;               // lexicon key (canonical + lexkey folding)
  std::vector<PhTok> phones;
};

enum class AccentType : uint8_t { None = 0, Falling = 1, Rising = 2 };

struct AccEntry {
  u32str key;
  int8_t syll = 1;  // 1-based; negative counts from the end
  AccentType type = AccentType::Falling;
  std::vector<uint8_t> long_sylls;  // 1-based
};

struct Accents {
  int8_t default_syll = 1;
  AccentType default_type = AccentType::Falling;
  bool never_last = false;
  std::vector<AccEntry> entries;  // sorted by key
};

enum class CliticType : uint8_t { Pro = 1, En = 2 };

struct Clitic {
  u32str key;
  CliticType type = CliticType::Pro;
};

// -------------------------------------------------------------- numbers
enum class RbnfPartKind : uint8_t {
  Text = 0,
  Less = 1,     // << quotient
  Greater = 2,  // >> remainder
  Equal = 3,    // == whole number
  OptStart = 4,
  OptEnd = 5,
  Plural = 6,   // $(cardinal,one{..}few{..}other{..})$
};

// Plural categories (CLDR order).
enum PluralCat : uint8_t { kZero = 0, kOne, kTwo, kFew, kMany, kOther, kPluralCount };

struct RbnfPart {
  RbnfPartKind kind = RbnfPartKind::Text;
  std::string text;
  int16_t ruleset = -1;  // -1 = same ruleset, -2 = plain digits
  std::vector<std::string> forms;  // kPluralCount entries for Plural
};

struct RbnfRule {
  int64_t base = 0;
  int64_t divisor = 1;
  bool negative = false;  // "-x:" rule
  std::vector<RbnfPart> parts;
};

struct RbnfRuleset {
  std::string name;  // without the leading %
  std::vector<RbnfRule> rules;  // sorted by base, negative rule separate
};

struct PluralRule {
  PluralCat cat = kOther;
  std::string expr;  // CLDR syntax; parsed at load time
};

struct Numbers {
  std::vector<RbnfRuleset> rulesets;
  std::vector<PluralRule> plurals;
  std::vector<std::pair<std::string, std::string>> replace;  // output fix-ups
};

// ------------------------------------------------------- normalization
enum class NKind : uint8_t {
  Digits = 0,  // #  (length range, value range, leading-zero flag)
  Lit = 1,     // one literal non-letter character
  Space = 2,   // _
  List = 3,    // @list   (word in list)
  Word = 4,    // word
  Lower = 5,   // lower   (word starting lowercase)
  Upper = 6,   // upper   (all capitals)
  Cap = 7,     // cap     (capitalised word)
  Roman = 8,   // roman   (roman numeral in capitals)
  Start = 9,   // ^
  End = 10,    // $
  Sym = 11,    // any single symbol character
};

struct NElem {
  NKind kind = NKind::Digits;
  bool optional = false;
  bool zero_lead = false;  // digits must start with 0
  bool no_zero_lead = false;
  uint8_t min_len = 1, max_len = 255;
  int64_t min_val = INT64_MIN, max_val = INT64_MAX;
  char32_t ch = 0;
  uint16_t list = 0;
};

enum class NFmt : uint8_t {
  Default = 0,  // numbers: default cardinal; words: as written
  Ruleset = 1,  // $1:%ruleset
  Map = 2,      // $1:@map
  Digits = 3,   // $1:digits
  Plural = 4,   // $1:plural(one|few|other)
  Text = 5,     // literal text
};

struct NOut {
  NFmt fmt = NFmt::Text;
  std::string text;
  uint8_t cap = 0;  // 1-based capture index
  uint16_t idx = 0; // ruleset or map index
  std::vector<std::string> forms;  // plural forms, kPluralCount
};

struct NRule {
  std::vector<NElem> left, core, right;
  std::vector<NOut> out;
  bool sentence_end_check = true;  // a consumed final '.' may still end the sentence
  std::string where;               // file:line
};

struct WordList {
  std::string name;
  std::vector<u32str> words;  // sorted canonical
};

struct NumMap {
  std::string name;
  std::vector<std::pair<int64_t, std::string>> items;  // sorted by number
};

struct Unit {
  std::vector<u32str> keys;  // canonical spellings
  std::vector<u32str> raw;   // as written (for case-sensitive keys)
  bool case_sensitive = false;
  bool prefix = false;       // may stand before the number (currencies)
  int16_t ruleset = -1;      // number ruleset (gender)
  std::vector<std::string> forms;  // kPluralCount
};

struct Abbrev {
  u32str key;  // canonical (or raw if case-sensitive)
  std::string expansion;
  bool case_sensitive = false;
  bool may_end = false;  // abbreviation's final '.' may also end a sentence
};

enum class BreakType : uint8_t {
  None = 0, Comma, Semicolon, Colon, Dash, Period, Question, Exclaim, Paragraph
};

struct Symbol {
  char32_t ch = 0;
  uint8_t level = 3;  // 0 none .. 3 all: spoken when user level >= this
  BreakType brk = BreakType::None;
  std::string name;
};

struct EmojiEntry {
  u32str seq;  // without U+FE0F
  std::string name;
};

struct SpellEntry {
  char32_t ch = 0;  // lowercase character
  std::string name;
};

struct Acronyms {
  std::vector<u32str> words;  // read as words
  std::vector<u32str> spell;  // always spelled
  int spell_max_len = 2;      // all-caps words this short are spelled
};

struct LanguageData {
  std::map<std::string, std::string> meta;
  std::vector<Phoneme> phonemes;
  std::vector<PhClass> phclasses;
  std::vector<LetterClass> letterclasses;
  Scripts scripts;
  std::vector<G2PRule> g2p;
  Postlex postlex;
  Syllables syllables;
  std::vector<LexEntry> lexicon;  // sorted by key
  Accents accents;
  std::vector<Clitic> clitics;    // sorted by key
  Numbers numbers;
  std::vector<WordList> lists;
  std::vector<NumMap> maps;
  std::vector<NRule> nrules;
  std::vector<Unit> units;
  std::vector<Abbrev> abbrevs;
  std::vector<Symbol> symbols;    // sorted by ch
  std::vector<EmojiEntry> emoji;  // sorted by seq
  std::vector<SpellEntry> spelling;  // sorted by ch
  std::string spell_capital;      // e.g. "veliko"
  Acronyms acronyms;
  std::map<std::string, double> prosody;

  std::string meta_value(const std::string& key, const std::string& def = "") const;
  int find_phoneme(const std::string& sym) const;
  int find_ruleset(const std::string& name) const;
  int find_list(const std::string& name) const;
  int find_map(const std::string& name) const;
};

}  // namespace mbng
