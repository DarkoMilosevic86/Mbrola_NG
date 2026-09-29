// MBROLA NG - loading languages (shared, read-only, reference counted)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lang/language.h"

namespace mbng {

// Loads <code>.dat from a file (cached per process: every engine instance
// using the same file shares one Language). A directory is accepted only
// when a source loader is registered (developer builds / tools).
std::shared_ptr<const Language> load_language(const std::string& path, std::string& error);
std::shared_ptr<const Language> load_language(const void* data, size_t size, std::string& error);

// Developer source mode (ANALYSIS 9.2): compiles a language SOURCE folder in
// memory. Registered by tools / developer builds, never in end-user builds.
using SourceLoader = std::shared_ptr<const Language> (*)(const std::string& dir, std::string& error);
void set_source_loader(SourceLoader loader);

bool read_file(const std::string& utf8_path, std::vector<uint8_t>& out);
bool is_directory(const std::string& utf8_path);

}  // namespace mbng
