// MBROLA NG - the text-to-speech pipeline
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/pipeline.h"

namespace mbng {

Pipeline::Pipeline(std::shared_ptr<const Language> lang)
    : lang_(std::move(lang)), phon_(*lang_), normalizer_(*lang_, phon_), prosody_(*lang_, phon_) {
  params_.base_pitch = lang_->prosody("base_pitch", 105);
}

void Pipeline::begin() {
  items_.clear();
  cursor_ = 0;
  cancelled_ = false;
  cur_rate_ = cur_pitch_ = cur_range_ = 1.0f;
}

void Pipeline::add_text(const u32str& text, const std::vector<uint32_t>& offs) {
  normalizer_.run(text, offs, norm_, items_);
}

void Pipeline::add_text_utf8(const std::string& text, uint32_t offset_base) {
  u32str t = utf8_to_u32(text);
  std::vector<uint32_t> offs(t.size() + 1);
  for (size_t i = 0; i <= t.size(); ++i) offs[i] = offset_base + static_cast<uint32_t>(i);
  add_text(t, offs);
}

void Pipeline::add_mark(int32_t index, const std::string& name) {
  Item it;
  it.kind = ItemKind::Mark;
  it.mark = index;
  it.name = name;
  items_.push_back(std::move(it));
}

void Pipeline::add_silence(int ms) {
  Item it;
  it.kind = ItemKind::Silence;
  it.ms = ms;
  items_.push_back(std::move(it));
}

void Pipeline::set_rate(double mult) {
  Item it;
  it.kind = ItemKind::Rate;
  it.value = mult;
  items_.push_back(std::move(it));
}

void Pipeline::set_pitch(double mult) {
  Item it;
  it.kind = ItemKind::Pitch;
  it.value = mult;
  items_.push_back(std::move(it));
}

void Pipeline::set_range(double mult) {
  Item it;
  it.kind = ItemKind::Range;
  it.value = mult;
  items_.push_back(std::move(it));
}

bool Pipeline::add_phonemes(const std::string& sampa, uint32_t src_b, uint32_t src_e, std::string* error) {
  Item it;
  it.kind = ItemKind::Phonemes;
  it.src_b = src_b;
  it.src_e = src_e;
  for (const std::string& sym : split_ws(sampa)) {
    int id = lang_->d().find_phoneme(sym);
    if (id < 0) {
      if (error) *error = "unknown phoneme '" + sym + "'";
      return false;
    }
    it.phones.push_back(static_cast<uint8_t>(id));
  }
  items_.push_back(std::move(it));
  return true;
}

static bool ends_sentence(BreakType b) {
  return b == BreakType::Period || b == BreakType::Question || b == BreakType::Exclaim ||
         b == BreakType::Paragraph;
}

bool Pipeline::next_chunk(Chunk& out) {
  out = Chunk();
  if (cancelled_) return false;
  // skip leading breaks, apply state items
  while (cursor_ < items_.size()) {
    const Item& it = items_[cursor_];
    if (it.kind == ItemKind::Break) { ++cursor_; continue; }
    if (it.kind == ItemKind::Rate) { cur_rate_ = static_cast<float>(it.value); ++cursor_; continue; }
    if (it.kind == ItemKind::Pitch) { cur_pitch_ = static_cast<float>(it.value); ++cursor_; continue; }
    if (it.kind == ItemKind::Range) { cur_range_ = static_cast<float>(it.value); ++cursor_; continue; }
    break;
  }
  if (cursor_ >= items_.size()) return false;

  if (items_[cursor_].kind == ItemKind::Silence) {
    // host-requested silence is absolute (not scaled by the rate)
    Prosody::silence(items_[cursor_].ms, out, lang_->d().phonemes[lang_->pause_phoneme()].sym);
    ++cursor_;
    return true;
  }

  Sentence s;
  s.phrases.emplace_back();
  std::vector<std::pair<int32_t, std::string>> pending_marks;
  bool any_word = false;
  while (cursor_ < items_.size()) {
    const Item& it = items_[cursor_];
    if (it.kind == ItemKind::Silence) break;  // becomes its own chunk
    ++cursor_;
    switch (it.kind) {
      case ItemKind::Word:
      case ItemKind::Phonemes: {
        SWord w;
        w.text = it.text;
        w.src_b = it.src_b;
        w.src_e = it.src_e;
        w.rate = cur_rate_;
        w.pitch = cur_pitch_;
        w.range = cur_range_;
        w.marks_before.swap(pending_marks);
        if (it.kind == ItemKind::Phonemes) {
          w.raw = true;
          w.phones = it.phones;
        }
        s.phrases.back().words.push_back(std::move(w));
        any_word = true;
        break;
      }
      case ItemKind::Mark:
        pending_marks.push_back({it.mark, it.name});
        break;
      case ItemKind::Rate: cur_rate_ = static_cast<float>(it.value); break;
      case ItemKind::Pitch: cur_pitch_ = static_cast<float>(it.value); break;
      case ItemKind::Range: cur_range_ = static_cast<float>(it.value); break;
      case ItemKind::Break:
        if (s.phrases.back().words.empty()) break;  // nothing to separate
        if (ends_sentence(it.brk)) {
          s.final_brk = it.brk;
          goto done;
        }
        s.phrases.back().brk = it.brk;
        s.phrases.emplace_back();
        break;
      default: break;
    }
  }
done:
  // drop an empty trailing phrase
  if (s.phrases.size() > 1 && s.phrases.back().words.empty()) {
    s.final_brk = s.final_brk == BreakType::None ? s.phrases[s.phrases.size() - 2].brk : s.final_brk;
    s.phrases.pop_back();
  }
  s.phrases.back().marks_after.swap(pending_marks);
  // marks that follow the sentence directly belong to its end
  while (cursor_ < items_.size() && items_[cursor_].kind == ItemKind::Mark) {
    s.phrases.back().marks_after.push_back({items_[cursor_].mark, items_[cursor_].name});
    ++cursor_;
  }
  size_t k = cursor_;
  while (k < items_.size() && items_[k].kind == ItemKind::Break) ++k;
  s.last_in_input = k >= items_.size();

  if (!any_word) {
    // only marks: an empty chunk that carries the events
    for (auto& m : s.phrases.back().marks_after) {
      Event ev;
      ev.type = EventType::Mark;
      ev.mark = m.first;
      ev.name = m.second;
      out.events.push_back(ev);
    }
    return !out.events.empty() || next_chunk(out);
  }
  prosody_.render(s, params_, out);
  return true;
}

}  // namespace mbng
