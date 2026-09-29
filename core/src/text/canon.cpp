// MBROLA NG - script mapping to the canonical form
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "text/canon.h"

#include <algorithm>

namespace mbng {

Canonicalizer::Canonicalizer(const Scripts& s) : s_(s) {
  for (auto& e : s_.chars) sorted_.push_back(&e);
  std::sort(sorted_.begin(), sorted_.end(), [](const CharEntry* a, const CharEntry* b) { return a->ch < b->ch; });
  for (auto& h : s_.homoglyphs) homo_[(static_cast<uint64_t>(h.script) << 32) | h.from] = h.to;
  for (auto& lk : s_.lexkey) lexkey_[lk.first] = lk.second;
  for (size_t i = 0; i < s_.normalize.size(); ++i)
    if (!s_.normalize[i].first.empty()) clean_[s_.normalize[i].first[0]].push_back(i);
  for (auto& kv : clean_)
    std::stable_sort(kv.second.begin(), kv.second.end(), [&](size_t a, size_t b) {
      return s_.normalize[a].first.size() > s_.normalize[b].first.size();
    });
}

const CharEntry* Canonicalizer::entry(char32_t c) const {
  auto it = std::lower_bound(sorted_.begin(), sorted_.end(), c,
                             [](const CharEntry* e, char32_t x) { return e->ch < x; });
  return it != sorted_.end() && (*it)->ch == c ? *it : nullptr;
}

char32_t Canonicalizer::lower(char32_t c) const {
  const CharEntry* e = entry(c);
  if (e) return e->lower ? e->lower : c;
  if (c >= 'A' && c <= 'Z') return c + 32;
  return c;
}

u32str Canonicalizer::word(const char32_t* s, size_t n, uint8_t* caps, int* script) const {
  // majority script
  int counts[16] = {};
  for (size_t i = 0; i < n; ++i) {
    const CharEntry* e = entry(s[i]);
    if (e && e->script < 16) counts[e->script]++;
  }
  int major = 0;
  for (int k = 1; k < 16; ++k)
    if (counts[k] > counts[major]) major = k;
  if (script) *script = major;

  u32str out;
  int upper = 0, lowerc = 0, letters = 0;
  bool first_upper = false, first_seen = false;
  for (size_t i = 0; i < n; ++i) {
    const CharEntry* e = entry(s[i]);
    if (!e) continue;  // unknown letter or combining mark: dropped
    if (e->script != major) {
      char32_t lc = e->lower ? e->lower : e->ch;
      auto h = homo_.find((static_cast<uint64_t>(major) << 32) | lc);
      if (h != homo_.end()) {
        const CharEntry* t = entry(h->second);
        if (t) {
          // keep the case of the original letter
          if (e->upper) {
            for (auto* x : sorted_)
              if (x->lower == t->ch && x->upper) { t = x; break; }
          }
          e = t;
        }
      }
    }
    ++letters;
    if (e->upper) ++upper; else ++lowerc;
    if (!first_seen) { first_seen = true; first_upper = e->upper; }
    out += e->canon;
  }
  if (caps) {
    if (letters >= 2 && lowerc == 0) *caps = kCapsUpper;
    else if (first_upper) *caps = kCapsCapitalized;
    else if (upper > 0) *caps = kCapsMixed;
    else *caps = kCapsLower;
  }
  return out;
}

u32str Canonicalizer::text(const u32str& s) const {
  u32str out;
  size_t i = 0;
  while (i < s.size()) {
    if (is_space(s[i])) {
      while (i < s.size() && is_space(s[i])) ++i;
      if (!out.empty()) out.push_back(' ');
      continue;
    }
    if (is_word_char(s[i]) && !is_combining(s[i])) {
      size_t j = i;
      while (j < s.size() && is_word_char(s[j])) ++j;
      out += word(s.data() + i, j - i);
      i = j;
      continue;
    }
    out.push_back(s[i++]);
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

u32str Canonicalizer::lexkey(const u32str& canon) const {
  if (lexkey_.empty()) return canon;
  u32str out;
  for (char32_t c : canon) {
    auto it = lexkey_.find(c);
    if (it == lexkey_.end()) out.push_back(c);
    else out += it->second;
  }
  return out;
}

u32str Canonicalizer::cleanup(const u32str& s, std::vector<uint32_t>& map) const {
  u32str out;
  map.clear();
  out.reserve(s.size());
  map.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    auto it = clean_.find(s[i]);
    bool done = false;
    if (it != clean_.end()) {
      for (size_t idx : it->second) {
        const auto& p = s_.normalize[idx];
        if (s.compare(i, p.first.size(), p.first) == 0) {
          for (char32_t c : p.second) { out.push_back(c); map.push_back(static_cast<uint32_t>(i)); }
          i += p.first.size();
          done = true;
          break;
        }
      }
    }
    if (!done) { out.push_back(s[i]); map.push_back(static_cast<uint32_t>(i)); ++i; }
  }
  return out;
}

}  // namespace mbng
