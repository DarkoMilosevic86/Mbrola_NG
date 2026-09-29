// MBROLA NG - prosody (durations, F0, pauses) and .pho generation
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <vector>

#include "lang/language.h"
#include "phon/phonology.h"

namespace mbng {

enum class EventType : int { Word = 1, Sentence = 2, Mark = 3, Phoneme = 4, End = 5 };

struct Event {
  EventType type = EventType::Word;
  double ms = 0;              // time from the start of the chunk
  uint32_t src_b = 0, src_e = 0;
  int32_t mark = 0;
  std::string name;
  int phoneme = -1;           // Phoneme events
  int dur_ms = 0;
};

struct Chunk {
  std::string pho;            // MBROLA .pho text (without the final '#')
  std::vector<Event> events;
  double ms = 0;              // total duration
  std::string debug_words;    // canonical words (tests / mbtts)
  std::string debug_phones;   // phonemes after post-lexical rules
};

struct ProsodyParams {
  double rate = 1.0;          // speed multiplier (2.0 = twice as fast)
  double pitch = 1.0;         // pitch multiplier
  double range = 1.0;         // intonation range multiplier (0 = monotone)
  double base_pitch = 105.0;  // Hz, from the voice
  bool phoneme_events = false;
  // Symbol the VOICE uses for each language phoneme id (empty = the same
  // symbol as the language). Filled from the voice's phoneme_map.
  std::vector<std::string> voice_syms;
};

struct SWord {
  u32str text;
  uint32_t src_b = 0, src_e = 0;
  float rate = 1.0f, pitch = 1.0f, range = 1.0f;
  std::vector<std::pair<int32_t, std::string>> marks_before;
  bool raw = false;             // `phones` given directly (PHONEMES segment)
  std::vector<uint8_t> phones;
};

struct SPhrase {
  std::vector<SWord> words;
  BreakType brk = BreakType::None;
  std::vector<std::pair<int32_t, std::string>> marks_after;
};

struct Sentence {
  std::vector<SPhrase> phrases;
  BreakType final_brk = BreakType::None;
  bool last_in_input = false;
};

class Prosody {
 public:
  Prosody(const Language& lang, const Phonology& ph) : L(lang), P(ph) {}
  void render(const Sentence& s, const ProsodyParams& p, Chunk& out) const;
  static void silence(int ms, Chunk& out, const std::string& pause_sym);

 private:
  double k(const char* key, double def) const { return L.prosody(key, def); }
  double pause_for(BreakType b, bool last) const;
  const Language& L;
  const Phonology& P;
};

}  // namespace mbng
