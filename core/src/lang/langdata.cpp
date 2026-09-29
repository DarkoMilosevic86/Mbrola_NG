// MBROLA NG - compiled language data model
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lang/langdata.h"

namespace mbng {

std::string LanguageData::meta_value(const std::string& key, const std::string& def) const {
  auto it = meta.find(key);
  return it == meta.end() ? def : it->second;
}

int LanguageData::find_phoneme(const std::string& sym) const {
  for (size_t i = 0; i < phonemes.size(); ++i)
    if (phonemes[i].sym == sym) return static_cast<int>(i);
  return -1;
}

int LanguageData::find_ruleset(const std::string& name) const {
  std::string n = name;
  while (!n.empty() && n[0] == '%') n.erase(0, 1);
  for (size_t i = 0; i < numbers.rulesets.size(); ++i)
    if (numbers.rulesets[i].name == n) return static_cast<int>(i);
  return -1;
}

int LanguageData::find_list(const std::string& name) const {
  for (size_t i = 0; i < lists.size(); ++i)
    if (lists[i].name == name) return static_cast<int>(i);
  return -1;
}

int LanguageData::find_map(const std::string& name) const {
  for (size_t i = 0; i < maps.size(); ++i)
    if (maps[i].name == name) return static_cast<int>(i);
  return -1;
}

}  // namespace mbng
