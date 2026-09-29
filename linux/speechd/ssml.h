// MBROLA NG - SSML -> segments for the Speech Dispatcher module (ANALYSIS 13.2)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Speech Dispatcher hands every TEXT message to the module as SSML
// (<speak> with <mark name=".."/> index marks inserted by the server). This
// small, forgiving parser turns it into the neutral segments of the core C
// API: speak, mark, break, prosody (rate/pitch), say-as characters, sub and
// p/s are understood, every other tag is dropped and its content spoken.
#pragma once

#include <string>
#include <vector>

namespace mbng {

struct SsmlSegment {
  int type = 0;        // MBNG_SEG_*
  std::string text;    // TEXT: UTF-8 text; MARK: mark name
  int value = 0;       // MARK: index into `marks`; BREAK: ms; RATE/PITCH: percent
};

struct SsmlResult {
  std::vector<SsmlSegment> segments;
  std::vector<std::string> marks;  // mark names, MARK segment value = index
};

// `ssml` = true: parse markup; false: the whole input is plain text.
SsmlResult parse_ssml(const std::string& input, bool ssml = true);

// Decodes &lt; &gt; &amp; &quot; &apos; &#N; &#xH; (unknown entities are kept).
std::string decode_entities(const std::string& s);

}  // namespace mbng
