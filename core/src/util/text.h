// MBROLA NG - text utilities (UTF-8 / UTF-16 / UTF-32, small string helpers)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mbng {

using u32str = std::u32string;

// Decoding never fails: invalid sequences become U+FFFD.
u32str utf8_to_u32(const std::string& s);
u32str utf8_to_u32(const char* s, size_t len);
u32str utf16_to_u32(const uint16_t* s, size_t len);
std::string u32_to_utf8(const u32str& s);
std::string u32_to_utf8(char32_t c);
void append_utf8(std::string& out, char32_t c);

// Number of UTF-16 code units / UTF-8 bytes needed for one code point.
inline int utf16_units(char32_t c) { return c >= 0x10000 ? 2 : 1; }
inline int utf8_units(char32_t c) {
  return c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
}

// Generic Unicode knowledge (no language knowledge).
bool is_space(char32_t c);
bool is_newline(char32_t c);
bool is_digit(char32_t c);  // ASCII 0-9 only
// True for code points in blocks that contain letters of alphabetic scripts
// (used only to recognise letters the language does not know, so they are
// kept inside words instead of being read as symbols).
bool is_letterish(char32_t c);
// Combining marks (U+0300..U+036F and a few other ranges).
bool is_combining(char32_t c);

std::string trim(const std::string& s);
std::vector<std::string> split_ws(const std::string& s);
bool starts_with(const std::string& s, const std::string& prefix);
std::string to_lower_ascii(std::string s);
std::string format(const char* fmt, ...);

uint32_t crc32(const void* data, size_t len, uint32_t crc = 0);

}  // namespace mbng
