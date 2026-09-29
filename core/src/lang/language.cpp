// MBROLA NG - a loaded language
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lang/language.h"

#include <algorithm>

#include "lang/datfile.h"
#include "util/bytes.h"

namespace mbng {

static const std::vector<uint32_t> kNoRules;

Language::Language(LanguageData data) : d_(std::move(data)) {
  plurals_.compile(d_.numbers.plurals);
  rbnf_ = std::make_unique<Rbnf>(d_.numbers, plurals_);

  phcls_.resize(d_.phclasses.size());
  for (size_t i = 0; i < d_.phclasses.size(); ++i)
    for (uint8_t m : d_.phclasses[i].members) phcls_[i].set(m);

  for (size_t i = 0; i < d_.phonemes.size(); ++i)
    if (d_.phonemes[i].type == PhType::Pause) { pause_ = static_cast<uint8_t>(i); break; }
  if (pause_ == kNoPhoneme) throw FormatError("the language defines no pause phoneme");

  std::sort(d_.scripts.chars.begin(), d_.scripts.chars.end(),
            [](const CharEntry& a, const CharEntry& b) { return a.ch < b.ch; });
  canon_ = std::make_unique<Canonicalizer>(d_.scripts);

  // G2P: per first letter, longest match first, then file order.
  for (uint32_t i = 0; i < d_.g2p.size(); ++i)
    if (!d_.g2p[i].match.empty()) g2p_index_[d_.g2p[i].match[0]].push_back(i);
  for (auto& kv : g2p_index_)
    std::stable_sort(kv.second.begin(), kv.second.end(), [&](uint32_t a, uint32_t b) {
      return d_.g2p[a].match.size() > d_.g2p[b].match.size();
    });

  for (size_t i = 0; i < d_.emoji.size(); ++i) {
    const u32str& s = d_.emoji[i].seq;
    if (s.empty()) continue;
    emoji_.emplace(s, static_cast<int>(i));
    emoji_start_.insert(s[0]);
    emoji_max_ = std::max(emoji_max_, s.size());
  }
  for (size_t i = 0; i < d_.abbrevs.size(); ++i) {
    auto& a = d_.abbrevs[i];
    (a.case_sensitive ? abbr_raw_ : abbr_canon_).emplace(a.key, static_cast<int>(i));
    max_abbrev_len_ = std::max(max_abbrev_len_, a.key.size());
  }
  for (size_t i = 0; i < d_.units.size(); ++i) {
    auto& u = d_.units[i];
    for (auto& k : u.keys) { unit_canon_.emplace(k, static_cast<int>(i)); max_unit_len_ = std::max(max_unit_len_, k.size()); }
    for (auto& k : u.raw) { unit_raw_.emplace(k, static_cast<int>(i)); max_unit_len_ = std::max(max_unit_len_, k.size()); }
  }
  default_cardinal_ = d_.find_ruleset(d_.meta_value("default_cardinal"));
  if (default_cardinal_ < 0) throw FormatError("default_cardinal ruleset missing");
  digit_ruleset_ = d_.find_ruleset(d_.meta_value("digit_ruleset", d_.meta_value("default_cardinal")));
  if (digit_ruleset_ < 0) digit_ruleset_ = default_cardinal_;
}

std::shared_ptr<const Language> Language::from_dat(const uint8_t* p, size_t n) {
  LanguageData d;
  read_dat(p, n, d);
  return std::make_shared<Language>(std::move(d));
}

bool Language::in_letterclass(uint16_t cls, char32_t c) const {
  if (cls >= d_.letterclasses.size()) return false;
  const u32str& m = d_.letterclasses[cls].members;
  return std::binary_search(m.begin(), m.end(), c);
}

template <class T>
static const T* find_key(const std::vector<T>& v, const u32str& key) {
  auto it = std::lower_bound(v.begin(), v.end(), key, [](const T& e, const u32str& k) { return e.key < k; });
  return it != v.end() && it->key == key ? &*it : nullptr;
}

const LexEntry* Language::lexicon(const u32str& key) const { return find_key(d_.lexicon, key); }
const AccEntry* Language::accent(const u32str& key) const { return find_key(d_.accents.entries, key); }
const Clitic* Language::clitic(const u32str& key) const { return find_key(d_.clitics, key); }

const std::vector<uint32_t>& Language::g2p_rules_for(char32_t first) const {
  auto it = g2p_index_.find(first);
  return it == g2p_index_.end() ? kNoRules : it->second;
}

int Language::match_emoji(const u32str& s, size_t pos, size_t& len) const {
  if (pos >= s.size() || !may_start_emoji(s[pos])) return -1;
  // Build the candidate without U+FE0F (entries are stored without it).
  u32str cand;
  std::vector<size_t> ends;  // source length for each candidate length
  size_t i = pos;
  while (i < s.size() && cand.size() < emoji_max_) {
    if (s[i] != 0xFE0F) {
      cand.push_back(s[i]);
      ends.push_back(i + 1 - pos);
    } else if (!ends.empty()) {
      ends.back() = i + 1 - pos;
    }
    ++i;
  }
  // include a trailing FE0F after the final code point
  if (!ends.empty() && pos + ends.back() < s.size() && s[pos + ends.back()] == 0xFE0F) ends.back()++;
  for (size_t k = cand.size(); k > 0; --k) {
    auto it = emoji_.find(cand.substr(0, k));
    if (it != emoji_.end()) {
      len = ends[k - 1];
      if (pos + len < s.size() && s[pos + len] == 0xFE0F) ++len;
      return it->second;
    }
  }
  return -1;
}

const Symbol* Language::symbol(char32_t c) const {
  auto& v = d_.symbols;
  auto it = std::lower_bound(v.begin(), v.end(), c, [](const Symbol& e, char32_t x) { return e.ch < x; });
  return it != v.end() && it->ch == c ? &*it : nullptr;
}

const SpellEntry* Language::spelling(char32_t c) const {
  auto& v = d_.spelling;
  auto it = std::lower_bound(v.begin(), v.end(), c, [](const SpellEntry& e, char32_t x) { return e.ch < x; });
  return it != v.end() && it->ch == c ? &*it : nullptr;
}

bool Language::list_contains(uint16_t list, const u32str& w) const {
  if (list >= d_.lists.size()) return false;
  auto& v = d_.lists[list].words;
  return std::binary_search(v.begin(), v.end(), w);
}

const std::string* Language::map_value(uint16_t map, int64_t n) const {
  if (map >= d_.maps.size()) return nullptr;
  for (auto& it : d_.maps[map].items)
    if (it.first == n) return &it.second;
  return nullptr;
}

int Language::abbrev(const u32str& key, bool raw) const {
  auto& m = raw ? abbr_raw_ : abbr_canon_;
  auto it = m.find(key);
  return it == m.end() ? -1 : it->second;
}

int Language::unit(const u32str& key, bool raw) const {
  auto& m = raw ? unit_raw_ : unit_canon_;
  auto it = m.find(key);
  return it == m.end() ? -1 : it->second;
}

double Language::prosody(const std::string& key, double def) const {
  auto it = d_.prosody.find(key);
  return it == d_.prosody.end() ? def : it->second;
}

}  // namespace mbng
