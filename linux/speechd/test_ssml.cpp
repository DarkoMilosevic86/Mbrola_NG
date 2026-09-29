// MBROLA NG - tests of the SSML parser of the Speech Dispatcher module
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include <string>

#include "mbrola_ng.h"
#include "ssml.h"

using namespace mbng;

static int failures = 0;

// Compact form: T"text" M<name> B500 R150 P120 S+ S-
static std::string dump(const SsmlResult& r) {
  std::string out;
  for (const SsmlSegment& s : r.segments) {
    if (!out.empty()) out += ' ';
    switch (s.type) {
      case MBNG_SEG_TEXT: out += "T\"" + s.text + "\""; break;
      case MBNG_SEG_MARK: out += "M<" + r.marks[s.value] + ">"; break;
      case MBNG_SEG_BREAK: out += "B" + std::to_string(s.value); break;
      case MBNG_SEG_RATE: out += "R" + std::to_string(s.value); break;
      case MBNG_SEG_PITCH: out += "P" + std::to_string(s.value); break;
      case MBNG_SEG_SPELL_ON: out += "S+"; break;
      case MBNG_SEG_SPELL_OFF: out += "S-"; break;
      default: out += "?"; break;
    }
  }
  return out;
}

static void check(const char* ssml, const char* expected) {
  std::string got = dump(parse_ssml(ssml));
  if (got != expected) {
    std::printf("FAIL: %s\n  expected: %s\n  got:      %s\n", ssml, expected, got.c_str());
    ++failures;
  }
}

int main() {
  // what speech-dispatcher sends for a plain text message
  check("<speak>Dobar dan.<mark name=\"__spd_0\"/> Kako ste?</speak>",
        "T\"Dobar dan.\" M<__spd_0> T\" Kako ste?\"");
  check("<speak>a &lt; b &amp;&amp; c &gt; d &#269;&#x161; &quot;x&apos;</speak>",
        "T\"a < b && c > d \xC4\x8D\xC5\xA1 \"x'\"");
  check("<speak>jedan<break time=\"500ms\"/>dva<break time='1.5s'/>tri<break strength=\"strong\"/></speak>",
        "T\"jedan\" B500 T\"dva\" B1500 T\"tri\" B700");
  check("<speak><prosody rate=\"150%\">brzo <prosody pitch=\"+20%\">visoko</prosody></prosody> normalno</speak>",
        "R150 T\"brzo \" P120 T\"visoko\" P100 R100 T\" normalno\"");
  check("<speak><say-as interpret-as=\"characters\">HNK</say-as> Rijeka</speak>",
        "S+ T\"HNK\" S- T\" Rijeka\"");
  check("<speak><sub alias=\"World Wide Web\">WWW</sub>.</speak>", "T\"World Wide Web.\"");
  check("<speak><!-- comment -->x<?pi?> <unknown a=\"b\">y</unknown></speak>", "T\"x y\"");
  check("<speak>unclosed <say-as interpret-as=\"characters\">ab</speak>", "T\"unclosed \" S+ T\"ab\" S-");
  check("<speak>a < b</speak>", "T\"a < b\"");
  check("<speak><mark name=\"0:5\"/><mark name='x y'/></speak>", "M<0:5> M<x y>");
  check("<speak><p>one</p><p>two</p></speak>", "T\"\none\n\ntwo\n\"");
  if (dump(parse_ssml("<b>raw</b>", false)) != "T\"<b>raw</b>\"") {
    std::printf("FAIL: plain text mode\n");
    ++failures;
  }
  if (failures) return 1;
  std::printf("ssml: all tests passed\n");
  return 0;
}
