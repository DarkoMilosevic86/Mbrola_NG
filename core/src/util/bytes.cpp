// MBROLA NG - binary writer/reader
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "util/bytes.h"

#include <cstring>

namespace mbng {

void ByteWriter::f64(double v) {
  uint64_t u;
  std::memcpy(&u, &v, sizeof u);
  u64(u);
}

void ByteWriter::str(const std::string& s) {
  count(s.size());
  bytes(s.data(), s.size());
}

void ByteWriter::bytes(const void* p, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  buf_.insert(buf_.end(), b, b + n);
}

void ByteWriter::patch_u32(size_t pos, uint32_t v) {
  for (int k = 0; k < 4; ++k) buf_[pos + k] = static_cast<uint8_t>(v >> (8 * k));
}

double ByteReader::f64() {
  uint64_t u = u64();
  double v;
  std::memcpy(&v, &u, sizeof v);
  return v;
}

std::string ByteReader::str() {
  size_t n = count(1);
  std::string s(reinterpret_cast<const char*>(p_ + pos_), n);
  pos_ += n;
  return s;
}

size_t ByteReader::count(size_t min_elem_size) {
  uint32_t n = u32();
  if (min_elem_size == 0) min_elem_size = 1;
  if (static_cast<uint64_t>(n) * min_elem_size > n_ - pos_) throw FormatError("invalid element count");
  return n;
}

}  // namespace mbng
