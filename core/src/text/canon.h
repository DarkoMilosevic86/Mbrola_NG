// MBROLA NG - script mapping to the canonical (script-independent) form
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Decision D1: a script is only a spelling. Every letter of every script the
// language accepts is mapped by scripts.txt to canonical letters; all later
// stages (lexicon, G2P, lists, abbreviations ...) only see canonical text.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "lang/langdata.h"

namespace mbng {

enum Caps : uint8_t { kCapsLower = 0, kCapsCapitalized = 1, kCapsUpper = 2, kCapsMixed = 3 };

class Canonicalizer {
 public:
  explicit Canonicalizer(const Scripts& s);

  const CharEntry* entry(char32_t c) const;
  // Letter for word building: known to the language or a letter of any
  // alphabet (unknown letters are kept in the word and dropped silently).
  bool is_word_char(char32_t c) const { return entry(c) != nullptr || is_letterish(c) || is_combining(c); }
  char32_t lower(char32_t c) const;

  // Canonical lowercase form of one word. Homoglyphs are resolved towards
  // the majority script of the word. Unknown letters are dropped.
  u32str word(const char32_t* s, size_t n, uint8_t* caps = nullptr, int* script = nullptr) const;
  // Canonical form of a key phrase: letter runs -> word(), whitespace runs ->
  // one space, all other characters unchanged.
  u32str text(const u32str& s) const;
  // Lexicon key folding (e.g. single-letter digraphs back to two letters).
  u32str lexkey(const u32str& canon) const;
  // Clean-up table (typographic characters, combining sequences ...).
  // map[i] = index in `s` of the character that produced out[i].
  u32str cleanup(const u32str& s, std::vector<uint32_t>& map) const;

 private:
  const Scripts& s_;
  std::vector<const CharEntry*> sorted_;
  std::unordered_map<uint64_t, char32_t> homo_;  // (script<<32 | from) -> to
  std::unordered_map<char32_t, u32str> lexkey_;
  std::unordered_map<char32_t, std::vector<size_t>> clean_;  // first char -> entries, longest first
};

}  // namespace mbng
