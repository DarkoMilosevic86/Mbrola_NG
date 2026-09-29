// MBROLA NG - installed voices on Linux (ANALYSIS 10.3, 10.9, 13.4)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <map>
#include <string>
#include <vector>

namespace mbng {

struct VoiceInfo {
  std::string id;            // cr1
  std::string language;      // hr
  std::string database;      // full path of the MBROLA database file
  std::string gender;        // male / female / ""
  int base_pitch = 0;
  std::string phoneme_map;   // "A=A: E=e ..."
  std::map<std::string, std::string> names;  // ui language -> name
};

// "key = value" lines (# comments); missing file -> empty map.
std::map<std::string, std::string> read_key_values(const std::string& path);

struct VoiceSearch {
  std::vector<std::string> roots;  // <root>/<id>/<id> (+ <id>.voice)
  std::vector<std::string> distro_roots;  // /usr/share/mbrola: <id>/<id> or <id>
  std::string known_dir;           // <id>.voice descriptions of catalog voices
  std::string language_dir;        // <code>.dat
};

// Default search path: $XDG_DATA_HOME/mbrola-ng/voices (per user),
// /usr/local/share/mbrola-ng/voices, <datadir>/voices, /usr/share/mbrola.
VoiceSearch default_voice_search(const std::string& datadir);

// Usable voices (database present, language known and compiled); the first
// root wins when a voice id is found twice.
std::vector<VoiceInfo> find_voices(const VoiceSearch& s);

}  // namespace mbng
