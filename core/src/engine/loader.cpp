// MBROLA NG - loading languages
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "engine/loader.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

#include "util/bytes.h"

namespace mbng {

namespace fs = std::filesystem;

static std::atomic<SourceLoader> g_source_loader{nullptr};

void set_source_loader(SourceLoader loader) { g_source_loader = loader; }

bool read_file(const std::string& utf8_path, std::vector<uint8_t>& out) {
  std::ifstream f(fs::u8path(utf8_path), std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  std::streamoff n = f.tellg();
  if (n < 0 || n > (1 << 30)) return false;
  f.seekg(0);
  out.resize(static_cast<size_t>(n));
  if (n > 0) f.read(reinterpret_cast<char*>(out.data()), n);
  return static_cast<bool>(f);
}

bool is_directory(const std::string& utf8_path) {
  std::error_code ec;
  return fs::is_directory(fs::u8path(utf8_path), ec);
}

std::shared_ptr<const Language> load_language(const void* data, size_t size, std::string& error) {
  try {
    return Language::from_dat(static_cast<const uint8_t*>(data), size);
  } catch (const std::exception& e) {
    error = e.what();
  } catch (...) {
    error = "unknown error while loading the language";
  }
  return nullptr;
}

std::shared_ptr<const Language> load_language(const std::string& path, std::string& error) {
  if (is_directory(path)) {
    SourceLoader sl = g_source_loader.load();
    if (!sl) {
      error = "language source folders are only supported in developer builds: " + path;
      return nullptr;
    }
    return sl(path, error);
  }
  static std::mutex mu;
  static std::map<std::string, std::weak_ptr<const Language>> cache;
  std::error_code ec;
  std::string key = fs::absolute(fs::u8path(path), ec).u8string();
  auto mtime = fs::last_write_time(fs::u8path(path), ec);
  key += "|" + std::to_string(mtime.time_since_epoch().count());
  {
    std::lock_guard<std::mutex> lock(mu);
    auto it = cache.find(key);
    if (it != cache.end())
      if (auto sp = it->second.lock()) return sp;
  }
  std::vector<uint8_t> buf;
  if (!read_file(path, buf)) {
    error = "cannot read language file: " + path;
    return nullptr;
  }
  auto lang = load_language(buf.data(), buf.size(), error);
  if (lang) {
    std::lock_guard<std::mutex> lock(mu);
    cache[key] = lang;
  } else {
    error = path + ": " + error;
  }
  return lang;
}

}  // namespace mbng
