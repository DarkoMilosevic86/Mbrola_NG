// MBROLA NG - the text-to-speech pipeline of one engine instance
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
// segments -> normalization -> sentences -> phonology -> prosody -> .pho
// chunks (one per sentence) with events on the chunk's time line.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "lang/language.h"
#include "phon/phonology.h"
#include "prosody/prosody.h"
#include "text/normalizer.h"

namespace mbng {

class Pipeline {
 public:
  explicit Pipeline(std::shared_ptr<const Language> lang);

  const Language& language() const { return *lang_; }
  const Phonology& phonology() const { return phon_; }
  NormOptions& norm() { return norm_; }
  ProsodyParams& params() { return params_; }

  // Build the utterance (host segments in order).
  void begin();
  void add_text(const u32str& text, const std::vector<uint32_t>& offs);
  void add_text_utf8(const std::string& text, uint32_t offset_base = 0);  // code point offsets
  void add_mark(int32_t index, const std::string& name);
  void add_silence(int ms);
  void set_rate(double mult);
  void set_pitch(double mult);
  void set_range(double mult);
  // Raw SAMPA phoneme string ("d o b a r _ d a n"); false on unknown symbols.
  bool add_phonemes(const std::string& sampa, uint32_t src_b, uint32_t src_e, std::string* error);

  // Next chunk of speech; false when the utterance is finished or cancelled.
  bool next_chunk(Chunk& out);
  void cancel() { cancelled_ = true; }
  bool cancelled() const { return cancelled_; }

  const std::vector<Item>& items() const { return items_; }

 private:
  std::shared_ptr<const Language> lang_;
  Phonology phon_;
  Normalizer normalizer_;
  Prosody prosody_;
  NormOptions norm_;
  ProsodyParams params_;
  std::vector<Item> items_;
  size_t cursor_ = 0;
  std::atomic<bool> cancelled_{false};
  float cur_rate_ = 1.0f, cur_pitch_ = 1.0f, cur_range_ = 1.0f;
};

}  // namespace mbng
