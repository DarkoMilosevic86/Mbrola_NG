// MBROLA NG - phonology: lexicon, G2P rules, syllables, accent, post-lexical
// (phrase-level) rules. Generic interpreter of the language data.
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <vector>

#include "lang/language.h"

namespace mbng {

enum SegFlag : uint8_t {
  kSegLong = 1,       // long vowel (lexicon / accents)
  kSegNucleus = 2,    // syllable nucleus
  kSegStressed = 4,   // accented nucleus
  kSegRising = 8,     // accent type rising (else falling)
  kSegInserted = 16,  // inserted pause
};

struct Seg {
  uint8_t ph = 0;
  uint8_t flags = 0;
  uint16_t word = 0;   // phrase-local word index
  int16_t syll = -1;   // syllable index within the word
  float dur_factor = 1.0f;
  int pause_ms = 0;    // for inserted pauses
};

struct PWord {
  std::vector<Seg> segs;
  int nsyll = 0;
  int clitic = 0;  // 0, 1 = proclitic, 2 = enclitic
};

class Phonology {
 public:
  explicit Phonology(const Language& lang) : L(lang) {}

  // Canonical word -> phonemes (lexicon first, then G2P rules). `unknown`
  // receives letters no rule could convert.
  std::vector<PhTok> transcribe(const u32str& canon, u32str* unknown = nullptr) const;
  // Full word analysis: transcription, syllables, accent, clitic status.
  PWord analyse(const u32str& canon) const;
  // Word contains a true vowel / any syllable nucleus.
  bool has_vowel(const u32str& canon) const;
  bool has_nucleus(const u32str& canon) const;
  bool is_clitic(const u32str& canon) const;

  // Phrase-level rules on the concatenated segments of a phrase.
  void postlex(std::vector<Seg>& segs) const;

  // Phoneme string for debugging ("p r e t s j e d n i k").
  std::string to_string(const std::vector<Seg>& segs, bool marks = false) const;
  std::string to_string(const std::vector<PhTok>& toks) const;

 private:
  bool ctx_match_letters(const u32str& w, size_t pos, size_t len, const G2PRule& r) const;
  bool elem_match(const PElem& e, uint8_t ph) const;
  bool ctx_match_phones(const std::vector<Seg>& s, size_t focus, const std::vector<PElem>& left,
                        const std::vector<PElem>& right) const;
  void syllabify(PWord& w) const;
  void accent(PWord& w, const u32str& key, bool from_lexicon) const;

  const Language& L;
};

}  // namespace mbng
