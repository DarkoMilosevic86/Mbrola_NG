// MBROLA NG - binary language file (<code>.dat), ANALYSIS 9.3
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Layout (little-endian, offsets from file start):
//   Header, 64 bytes:
//     0  char[8]  "MBNGLANG"
//     8  u16      format major      10 u16 format minor
//     12 char[16] language code (zero padded)
//     28 u32      compiler version (0xMMmmpp)
//     32 u64      build timestamp (Unix seconds)
//     40 u32      flags (0)
//     44 u32      section count
//     48 u32      section directory offset
//     52 u32      file size
//     56 u32      CRC32 of the whole file with this field set to 0
//     60 u32      reserved (0)
//   Section directory: count x { u32 fourcc, u32 offset, u32 size, u32 crc32 }
//   Sections: fourcc 'META','PHON','CLAS','SCRP','G2PR','PLEX','SYLL','LEXI',
//     'ACCN','CLIT','NUMB','NORM','UNIT','ABBR','SYMB','EMOJ','SPEL','ACRO',
//     'PROS'. Unknown sections are ignored; a higher MAJOR version is refused.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lang/langdata.h"

namespace mbng {

constexpr uint16_t kDatMajor = 1;
constexpr uint16_t kDatMinor = 0;
constexpr uint32_t kCompilerVersion = 0x000100;  // 0.1.0

std::vector<uint8_t> write_dat(const LanguageData& d, uint64_t timestamp);
// Throws FormatError on any problem (bad magic, version, CRC, bounds ...).
void read_dat(const uint8_t* p, size_t n, LanguageData& d);

}  // namespace mbng
