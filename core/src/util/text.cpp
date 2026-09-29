// MBROLA NG - text utilities
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "util/text.h"

#include <cstdarg>
#include <cstdio>

namespace mbng {

u32str utf8_to_u32(const std::string& s) { return utf8_to_u32(s.data(), s.size()); }

u32str utf8_to_u32(const char* s, size_t len) {
  u32str out;
  out.reserve(len);
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  size_t i = 0;
  while (i < len) {
    unsigned c = p[i];
    char32_t cp;
    int n;
    if (c < 0x80) { cp = c; n = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
    else { out.push_back(0xFFFD); ++i; continue; }
    bool ok = i + n < len;  // all continuation bytes present
    for (int k = 1; ok && k <= n; ++k) {
      unsigned cc = p[i + k];
      if ((cc & 0xC0) != 0x80) ok = false;
      else cp = (cp << 6) | (cc & 0x3F);
    }
    if (!ok) { out.push_back(0xFFFD); ++i; continue; }
    static const char32_t minv[4] = {0, 0x80, 0x800, 0x10000};
    if (cp < minv[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    out.push_back(cp);
    i += n + 1;
  }
  return out;
}

u32str utf16_to_u32(const uint16_t* s, size_t len) {
  u32str out;
  out.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    char32_t c = s[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < len && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
      ++i;
    } else if (c >= 0xD800 && c <= 0xDFFF) {
      c = 0xFFFD;
    }
    out.push_back(c);
  }
  return out;
}

void append_utf8(std::string& out, char32_t c) {
  if (c < 0x80) out.push_back(static_cast<char>(c));
  else if (c < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (c >> 6)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
  } else if (c < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (c >> 12)));
    out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (c >> 18)));
    out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
  }
}

std::string u32_to_utf8(const u32str& s) {
  std::string out;
  out.reserve(s.size());
  for (char32_t c : s) append_utf8(out, c);
  return out;
}

std::string u32_to_utf8(char32_t c) {
  std::string out;
  append_utf8(out, c);
  return out;
}

bool is_space(char32_t c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0B || c == 0x0C ||
         c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
         c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

bool is_newline(char32_t c) {
  return c == '\n' || c == 0x85 || c == 0x2028 || c == 0x2029;
}

bool is_digit(char32_t c) { return c >= '0' && c <= '9'; }

bool is_letterish(char32_t c) {
  if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true;
  if (c == 0xAA || c == 0xB5 || c == 0xBA) return true;
  if (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) return true;  // Latin-1 .. Latin Ext-B
  if (c >= 0x250 && c <= 0x2AF) return true;                           // IPA
  if (c >= 0x370 && c <= 0x3FF && c != 0x37E && c != 0x387) return true;  // Greek
  if (c >= 0x400 && c <= 0x52F) return true;                           // Cyrillic (+suppl.)
  if (c >= 0x531 && c <= 0x587) return true;                           // Armenian
  if (c >= 0x5D0 && c <= 0x5EA) return true;                           // Hebrew letters
  if (c >= 0x620 && c <= 0x64A) return true;                           // Arabic letters
  if (c >= 0x10A0 && c <= 0x10FF) return true;                         // Georgian
  if (c >= 0x1E00 && c <= 0x1FFF) return true;                         // Latin Ext Add., Greek Ext
  if (c >= 0x2C60 && c <= 0x2C7F) return true;                         // Latin Ext-C
  if (c >= 0xA640 && c <= 0xA69F) return true;                         // Cyrillic Ext-B
  if (c >= 0xA720 && c <= 0xA7FF) return true;                         // Latin Ext-D
  if (c >= 0xFB00 && c <= 0xFB06) return true;                         // Latin ligatures
  return false;
}

bool is_combining(char32_t c) {
  return (c >= 0x300 && c <= 0x36F) || (c >= 0x483 && c <= 0x489) ||
         (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
         (c >= 0x20D0 && c <= 0x20FF && c != 0x20E3) || (c >= 0xFE20 && c <= 0xFE2F);
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
  return s.substr(a, b - a);
}

std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    size_t j = i;
    while (j < s.size() && !(s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) ++j;
    if (j > i) out.push_back(s.substr(i, j - i));
    i = j;
  }
  return out;
}

bool starts_with(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string to_lower_ascii(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::string format(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  return buf;
}

uint32_t crc32(const void* data, size_t len, uint32_t crc) {
  struct Table {
    uint32_t t[256];
    Table() {
      for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[i] = c;
      }
    }
  };
  static const Table tab;  // thread-safe initialisation (C++11)
  const uint32_t* table = tab.t;
  const unsigned char* p = static_cast<const unsigned char*>(data);
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

}  // namespace mbng
