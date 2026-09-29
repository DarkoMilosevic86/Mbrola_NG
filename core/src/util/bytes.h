// MBROLA NG - little-endian binary writer/reader for the .dat format
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "util/text.h"

namespace mbng {

// Thrown for every malformed/corrupt input. Always caught at the API border.
struct FormatError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

class ByteWriter {
 public:
  void u8(uint8_t v) { buf_.push_back(v); }
  void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
  void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
  void u64(uint64_t v) { u32(static_cast<uint32_t>(v)); u32(static_cast<uint32_t>(v >> 32)); }
  void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
  void i64(int64_t v) { u64(static_cast<uint64_t>(v)); }
  void f64(double v);
  void str(const std::string& s);     // u32 length + UTF-8 bytes
  void ustr(const u32str& s) { str(u32_to_utf8(s)); }
  void bytes(const void* p, size_t n);
  void count(size_t n) { u32(static_cast<uint32_t>(n)); }
  size_t size() const { return buf_.size(); }
  std::vector<uint8_t>& data() { return buf_; }
  void patch_u32(size_t pos, uint32_t v);

 private:
  std::vector<uint8_t> buf_;
};

class ByteReader {
 public:
  ByteReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
  uint8_t u8() { need(1); return p_[pos_++]; }
  uint16_t u16() { uint16_t a = u8(); return static_cast<uint16_t>(a | (u8() << 8)); }
  uint32_t u32() { uint32_t a = u16(); return a | (static_cast<uint32_t>(u16()) << 16); }
  uint64_t u64() { uint64_t a = u32(); return a | (static_cast<uint64_t>(u32()) << 32); }
  int16_t i16() { return static_cast<int16_t>(u16()); }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  int64_t i64() { return static_cast<int64_t>(u64()); }
  double f64();
  std::string str();
  u32str ustr() { return utf8_to_u32(str()); }
  // Element count; `min_elem_size` guards against absurd counts in corrupt files.
  size_t count(size_t min_elem_size = 1);
  bool at_end() const { return pos_ == n_; }
  size_t pos() const { return pos_; }

 private:
  void need(size_t k) {
    if (k > n_ - pos_) throw FormatError("unexpected end of data");
  }
  const uint8_t* p_;
  size_t n_;
  size_t pos_ = 0;
};

}  // namespace mbng
