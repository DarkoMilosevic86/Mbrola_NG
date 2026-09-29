// MBROLA NG - installed voices on Linux
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "voices.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace mbng {

static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return std::string();
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::map<std::string, std::string> read_key_values(const std::string& path) {
  std::map<std::string, std::string> kv;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
  }
  return kv;
}

VoiceSearch default_voice_search(const std::string& datadir) {
  VoiceSearch s;
  const char* xdg = std::getenv("XDG_DATA_HOME");
  const char* home = std::getenv("HOME");
  if (xdg && *xdg && xdg[0] == '/')
    s.roots.push_back(std::string(xdg) + "/mbrola-ng/voices");
  else if (home && *home)
    s.roots.push_back(std::string(home) + "/.local/share/mbrola-ng/voices");
  s.roots.push_back("/usr/local/share/mbrola-ng/voices");
  s.roots.push_back(datadir + "/voices");
  s.distro_roots.push_back("/usr/share/mbrola");
  s.distro_roots.push_back("/usr/local/share/mbrola");
  s.known_dir = datadir + "/voices.d";
  s.language_dir = datadir + "/languages";
  return s;
}

static bool is_file(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

static bool looks_like_mbrola(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  char magic[6] = {};
  return f.read(magic, 6) && std::equal(magic, magic + 6, "MBROLA");
}

static bool make_voice(const std::string& id, const fs::path& db, const fs::path& desc, const VoiceSearch& s,
                       VoiceInfo& v) {
  auto kv = read_key_values(desc.string());
  if (kv.empty()) kv = read_key_values((fs::path(s.known_dir) / (id + ".voice")).string());
  std::string lang = kv["language"];
  if (lang.empty() || lang.find('/') != std::string::npos) return false;
  if (!is_file(fs::path(s.language_dir) / (lang + ".dat"))) return false;
  if (!looks_like_mbrola(db)) return false;
  v.id = id;
  v.language = lang;
  v.database = db.string();
  v.gender = kv["gender"];
  v.base_pitch = std::atoi(kv["base_pitch"].c_str());
  v.phoneme_map = kv["phoneme_map"];
  for (const auto& p : kv)
    if (p.first.compare(0, 5, "name_") == 0 && !p.second.empty()) v.names[p.first.substr(5)] = p.second;
  return true;
}

static std::vector<std::string> sorted_entries(const std::string& dir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::string name = it->path().filename().string();
    if (!name.empty() && name[0] != '.') out.push_back(name);
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<VoiceInfo> find_voices(const VoiceSearch& s) {
  std::vector<VoiceInfo> out;
  std::set<std::string> seen;
  for (const std::string& root : s.roots) {
    for (const std::string& id : sorted_entries(root)) {
      fs::path folder = fs::path(root) / id;
      VoiceInfo v;
      if (seen.count(id) || !is_file(folder / id)) continue;
      if (make_voice(id, folder / id, folder / (id + ".voice"), s, v)) {
        seen.insert(id);
        out.push_back(v);
      }
    }
  }
  // distribution packages (e.g. Debian mbrola-cr1: /usr/share/mbrola/cr1/cr1)
  for (const std::string& root : s.distro_roots) {
    for (const std::string& id : sorted_entries(root)) {
      if (seen.count(id)) continue;
      fs::path p = fs::path(root) / id;
      fs::path db = is_file(p / id) ? p / id : p;
      if (!is_file(db)) continue;
      VoiceInfo v;
      if (make_voice(id, db, fs::path(), s, v)) {
        seen.insert(id);
        out.push_back(v);
      }
    }
  }
  return out;
}

}  // namespace mbng
