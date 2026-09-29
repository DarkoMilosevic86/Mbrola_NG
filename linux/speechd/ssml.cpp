// MBROLA NG - SSML -> segments for the Speech Dispatcher module
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ssml.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "mbrola_ng.h"

namespace mbng {

static void append_utf8(std::string& out, unsigned long cp) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

std::string decode_entities(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') {
      out += s[i];
      continue;
    }
    size_t semi = s.find(';', i + 1);
    if (semi == std::string::npos || semi - i > 12) {
      out += '&';
      continue;
    }
    std::string name = s.substr(i + 1, semi - i - 1);
    if (name == "lt") out += '<';
    else if (name == "gt") out += '>';
    else if (name == "amp") out += '&';
    else if (name == "quot") out += '"';
    else if (name == "apos") out += '\'';
    else if (name == "nbsp") append_utf8(out, 0xA0);
    else if (name.size() > 1 && name[0] == '#') {
      char* end = nullptr;
      unsigned long cp = (name[1] == 'x' || name[1] == 'X') ? std::strtoul(name.c_str() + 2, &end, 16)
                                                            : std::strtoul(name.c_str() + 1, &end, 10);
      if (!end || *end) {
        out += '&';
        continue;
      }
      append_utf8(out, cp);
    } else {
      out += '&';
      continue;
    }
    i = semi;
  }
  return out;
}

namespace {

struct Tag {
  std::string name;  // lower case, without namespace prefix
  bool closing = false;
  bool empty = false;  // <x/>
  std::vector<std::pair<std::string, std::string>> attrs;

  std::string attr(const char* key) const {
    for (const auto& a : attrs)
      if (a.first == key) return a.second;
    return std::string();
  }
};

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Parses the inside of <...> (without the brackets).
Tag parse_tag(const std::string& body) {
  Tag t;
  size_t i = 0, n = body.size();
  auto skip_ws = [&] {
    while (i < n && std::isspace(static_cast<unsigned char>(body[i]))) ++i;
  };
  skip_ws();
  if (i < n && body[i] == '/') {
    t.closing = true;
    ++i;
  }
  size_t start = i;
  while (i < n && !std::isspace(static_cast<unsigned char>(body[i])) && body[i] != '/') ++i;
  t.name = lower(body.substr(start, i - start));
  size_t colon = t.name.rfind(':');
  if (colon != std::string::npos) t.name = t.name.substr(colon + 1);
  while (i < n) {
    skip_ws();
    if (i >= n) break;
    if (body[i] == '/') {
      t.empty = true;
      ++i;
      continue;
    }
    start = i;
    while (i < n && body[i] != '=' && !std::isspace(static_cast<unsigned char>(body[i])) && body[i] != '/') ++i;
    std::string key = lower(body.substr(start, i - start));
    skip_ws();
    std::string value;
    if (i < n && body[i] == '=') {
      ++i;
      skip_ws();
      if (i < n && (body[i] == '"' || body[i] == '\'')) {
        char q = body[i++];
        start = i;
        while (i < n && body[i] != q) ++i;
        value = body.substr(start, i - start);
        if (i < n) ++i;
      } else {
        start = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(body[i])) && body[i] != '/') ++i;
        value = body.substr(start, i - start);
      }
    }
    if (!key.empty()) t.attrs.emplace_back(key, decode_entities(value));
  }
  return t;
}

// "500ms", "1.5s", "2" (seconds in SSML 1.0? speechd clients send ms/s)
int parse_time_ms(const std::string& v) {
  const char* p = v.c_str();
  char* end = nullptr;
  double x = std::strtod(p, &end);
  if (end == p) return -1;
  std::string unit = lower(std::string(end));
  while (!unit.empty() && std::isspace(static_cast<unsigned char>(unit.back()))) unit.pop_back();
  if (unit == "s") x *= 1000.0;
  else if (!unit.empty() && unit != "ms") return -1;
  if (x < 0) return -1;
  return x > 60000 ? 60000 : static_cast<int>(x + 0.5);
}

int break_strength_ms(const std::string& s) {
  if (s == "none") return 0;
  if (s == "x-weak") return 100;
  if (s == "weak") return 200;
  if (s == "medium") return 400;
  if (s == "strong") return 700;
  if (s == "x-strong") return 1200;
  return 400;
}

// prosody rate/pitch value -> factor (1.0 = unchanged), relative to `cur`
double prosody_factor(const std::string& raw, bool is_rate, double cur) {
  std::string v = lower(raw);
  if (v.empty()) return cur;
  if (v == "default" || v == "medium") return 1.0;
  if (is_rate) {
    if (v == "x-slow") return 0.5;
    if (v == "slow") return 0.75;
    if (v == "fast") return 1.5;
    if (v == "x-fast") return 2.0;
  } else {
    if (v == "x-low") return 0.7;
    if (v == "low") return 0.85;
    if (v == "high") return 1.2;
    if (v == "x-high") return 1.4;
  }
  const char* p = v.c_str();
  char* end = nullptr;
  double x = std::strtod(p, &end);
  if (end == p) return cur;
  bool relative = v[0] == '+' || v[0] == '-';
  std::string unit(end);
  double f;
  if (unit == "%") f = relative ? cur * (1.0 + x / 100.0) : x / 100.0;
  else if (unit == "st") f = cur * std::pow(2.0, x / 12.0);
  else if (unit.empty() && is_rate) f = x;  // SSML: rate as a multiplier
  else return cur;                          // Hz and other units: unsupported
  if (f < 0.1) f = 0.1;
  if (f > 8.0) f = 8.0;
  return f;
}

class Builder {
 public:
  SsmlResult r;

  void text(const std::string& t) {
    if (t.empty()) return;
    if (!r.segments.empty() && r.segments.back().type == MBNG_SEG_TEXT) {
      r.segments.back().text += t;
      return;
    }
    SsmlSegment s;
    s.type = MBNG_SEG_TEXT;
    s.text = t;
    r.segments.push_back(s);
  }
  void seg(int type, int value, const std::string& text = std::string()) {
    SsmlSegment s;
    s.type = type;
    s.value = value;
    s.text = text;
    r.segments.push_back(s);
  }
  void mark(const std::string& name) {
    seg(MBNG_SEG_MARK, static_cast<int>(r.marks.size()), name);
    r.marks.push_back(name);
  }
};

}  // namespace

SsmlResult parse_ssml(const std::string& in, bool ssml) {
  Builder b;
  if (!ssml) {
    b.text(in);
    return b.r;
  }
  struct Frame {
    std::string name;
    double rate, pitch;
    bool spell, skip;
  };
  std::vector<Frame> stack;
  double rate = 1.0, pitch = 1.0;
  int spell = 0;   // nesting depth of say-as characters
  int skip = 0;    // nesting depth of ignored content (<sub> body, <desc>)
  size_t i = 0, n = in.size();
  std::string pending;  // text not yet decoded

  auto flush = [&] {
    if (!pending.empty() && skip == 0) b.text(decode_entities(pending));
    pending.clear();
  };

  while (i < n) {
    char c = in[i];
    if (c != '<') {
      pending += c;
      ++i;
      continue;
    }
    // comments, CDATA, processing instructions, doctype
    if (in.compare(i, 4, "<!--") == 0) {
      size_t e = in.find("-->", i + 4);
      i = e == std::string::npos ? n : e + 3;
      continue;
    }
    if (in.compare(i, 9, "<![CDATA[") == 0) {
      size_t e = in.find("]]>", i + 9);
      flush();
      if (skip == 0) b.text(in.substr(i + 9, (e == std::string::npos ? n : e) - i - 9));
      i = e == std::string::npos ? n : e + 3;
      continue;
    }
    unsigned char next = i + 1 < n ? static_cast<unsigned char>(in[i + 1]) : 0;
    size_t close = in.find('>', i + 1);
    if (close == std::string::npos ||  // a lone '<': plain text
        !(std::isalpha(next) || next == '/' || next == '?' || next == '!' || next == '_')) {
      pending += c;
      ++i;
      continue;
    }
    std::string body = in.substr(i + 1, close - i - 1);
    i = close + 1;
    if (!body.empty() && (body[0] == '?' || body[0] == '!')) continue;
    flush();
    Tag t = parse_tag(body);
    if (t.name.empty()) continue;

    if (t.closing) {
      // pop up to the matching element (tolerates missing end tags)
      size_t k = stack.size();
      while (k > 0 && stack[k - 1].name != t.name) --k;
      if (k == 0) continue;
      while (stack.size() >= k) {
        Frame f = stack.back();
        stack.pop_back();
        if (f.spell && --spell == 0) b.seg(MBNG_SEG_SPELL_OFF, 0);
        if (f.skip) --skip;
        if (f.rate != rate) {
          rate = f.rate;
          b.seg(MBNG_SEG_RATE, static_cast<int>(rate * 100 + 0.5));
        }
        if (f.pitch != pitch) {
          pitch = f.pitch;
          b.seg(MBNG_SEG_PITCH, static_cast<int>(pitch * 100 + 0.5));
        }
        if (f.name == "p" || f.name == "s") b.text("\n");
      }
      continue;
    }

    if (t.name == "mark") {
      if (skip == 0) b.mark(t.attr("name"));
    } else if (t.name == "break") {
      if (skip == 0) {
        int ms = parse_time_ms(t.attr("time"));
        if (ms < 0) ms = break_strength_ms(lower(t.attr("strength")));
        if (ms > 0) b.seg(MBNG_SEG_BREAK, ms);
      }
    } else if (t.name == "audio" && t.empty) {
      // no fallback text: nothing to say
    } else if (!t.empty) {
      Frame f{t.name, rate, pitch, false, false};
      if (t.name == "prosody") {
        double nr = prosody_factor(t.attr("rate"), true, rate);
        double np = prosody_factor(t.attr("pitch"), false, pitch);
        if (nr != rate) {
          rate = nr;
          b.seg(MBNG_SEG_RATE, static_cast<int>(rate * 100 + 0.5));
        }
        if (np != pitch) {
          pitch = np;
          b.seg(MBNG_SEG_PITCH, static_cast<int>(pitch * 100 + 0.5));
        }
      } else if (t.name == "say-as") {
        std::string how = lower(t.attr("interpret-as"));
        if (how == "characters" || how == "spell-out" || how == "tts:char" || how == "char") {
          f.spell = true;
          if (spell++ == 0) b.seg(MBNG_SEG_SPELL_ON, 0);
        }
      } else if (t.name == "sub") {
        if (skip == 0) b.text(t.attr("alias"));
        f.skip = true;
        ++skip;
      } else if (t.name == "desc") {
        f.skip = true;
        ++skip;
      } else if (t.name == "p" || t.name == "s") {
        b.text("\n");
      }
      stack.push_back(f);
    }
  }
  flush();
  // unclosed elements
  if (spell > 0) b.seg(MBNG_SEG_SPELL_OFF, 0);
  return b.r;
}

}  // namespace mbng
