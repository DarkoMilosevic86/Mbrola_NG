// MBROLA NG - binary language file (<code>.dat)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lang/datfile.h"

#include <algorithm>
#include <cstring>
#include <functional>

#include "util/bytes.h"

namespace mbng {
namespace {

constexpr size_t kHeaderSize = 64;

constexpr uint32_t fourcc(const char (&s)[5]) {
  return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) |
         (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24);
}

// ------------------------------------------------------------ writers
void w_strs(ByteWriter& w, const std::vector<std::string>& v) {
  w.count(v.size());
  for (auto& s : v) w.str(s);
}
void w_ustrs(ByteWriter& w, const std::vector<u32str>& v) {
  w.count(v.size());
  for (auto& s : v) w.ustr(s);
}
void w_bytes(ByteWriter& w, const std::vector<uint8_t>& v) {
  w.count(v.size());
  w.bytes(v.data(), v.size());
}
void w_pelems(ByteWriter& w, const std::vector<PElem>& v) {
  w.count(v.size());
  for (auto& e : v) { w.u8(static_cast<uint8_t>(e.kind)); w.u16(e.id); }
}
void w_ctx(ByteWriter& w, const std::vector<CtxElem>& v) {
  w.count(v.size());
  for (auto& e : v) { w.u8(static_cast<uint8_t>(e.kind)); w.u32(e.ch); w.u16(e.cls); }
}
void w_nelems(ByteWriter& w, const std::vector<NElem>& v) {
  w.count(v.size());
  for (auto& e : v) {
    w.u8(static_cast<uint8_t>(e.kind));
    w.u8(static_cast<uint8_t>((e.optional ? 1 : 0) | (e.zero_lead ? 2 : 0) | (e.no_zero_lead ? 4 : 0)));
    w.u8(e.min_len); w.u8(e.max_len);
    w.i64(e.min_val); w.i64(e.max_val);
    w.u32(e.ch); w.u16(e.list);
  }
}

// ------------------------------------------------------------ readers
std::vector<std::string> r_strs(ByteReader& r) {
  std::vector<std::string> v(r.count(4));
  for (auto& s : v) s = r.str();
  return v;
}
std::vector<u32str> r_ustrs(ByteReader& r) {
  std::vector<u32str> v(r.count(4));
  for (auto& s : v) s = r.ustr();
  return v;
}
std::vector<uint8_t> r_bytes(ByteReader& r) {
  std::vector<uint8_t> v(r.count(1));
  for (auto& b : v) b = r.u8();
  return v;
}
std::vector<PElem> r_pelems(ByteReader& r) {
  std::vector<PElem> v(r.count(3));
  for (auto& e : v) {
    uint8_t k = r.u8();
    if (k > 3) throw FormatError("bad phoneme context kind");
    e.kind = static_cast<PKind>(k);
    e.id = r.u16();
  }
  return v;
}
std::vector<CtxElem> r_ctx(ByteReader& r) {
  std::vector<CtxElem> v(r.count(7));
  for (auto& e : v) {
    uint8_t k = r.u8();
    if (k > 2) throw FormatError("bad letter context kind");
    e.kind = static_cast<CtxKind>(k);
    e.ch = r.u32();
    e.cls = r.u16();
  }
  return v;
}
std::vector<NElem> r_nelems(ByteReader& r) {
  std::vector<NElem> v(r.count(26));
  for (auto& e : v) {
    uint8_t k = r.u8();
    if (k > static_cast<uint8_t>(NKind::Sym)) throw FormatError("bad pattern element");
    e.kind = static_cast<NKind>(k);
    uint8_t f = r.u8();
    e.optional = f & 1; e.zero_lead = (f & 2) != 0; e.no_zero_lead = (f & 4) != 0;
    e.min_len = r.u8(); e.max_len = r.u8();
    e.min_val = r.i64(); e.max_val = r.i64();
    e.ch = r.u32(); e.list = r.u16();
  }
  return v;
}

// ------------------------------------------------------------ sections
void w_meta(ByteWriter& w, const LanguageData& d) {
  w.count(d.meta.size());
  for (auto& kv : d.meta) { w.str(kv.first); w.str(kv.second); }
}
void r_meta(ByteReader& r, LanguageData& d) {
  size_t n = r.count(8);
  for (size_t i = 0; i < n; ++i) { std::string k = r.str(); d.meta[k] = r.str(); }
}

void w_phon(ByteWriter& w, const LanguageData& d) {
  w.count(d.phonemes.size());
  for (auto& p : d.phonemes) {
    w.str(p.sym); w.u8(static_cast<uint8_t>(p.type)); w_strs(w, p.features);
    w.u16(p.dur); w.u16(p.min_dur); w.u8(p.partner); w.u8(p.viseme);
  }
}
void r_phon(ByteReader& r, LanguageData& d) {
  d.phonemes.resize(r.count(14));
  if (d.phonemes.size() > kMaxPhonemes) throw FormatError("too many phonemes");
  for (auto& p : d.phonemes) {
    p.sym = r.str();
    uint8_t t = r.u8();
    if (t > 2) throw FormatError("bad phoneme type");
    p.type = static_cast<PhType>(t);
    p.features = r_strs(r);
    p.dur = r.u16(); p.min_dur = r.u16(); p.partner = r.u8(); p.viseme = r.u8();
  }
}

void w_clas(ByteWriter& w, const LanguageData& d) {
  w.count(d.phclasses.size());
  for (auto& c : d.phclasses) { w.str(c.name); w_bytes(w, c.members); }
  w.count(d.letterclasses.size());
  for (auto& c : d.letterclasses) { w.str(c.name); w.ustr(c.members); }
}
void r_clas(ByteReader& r, LanguageData& d) {
  d.phclasses.resize(r.count(8));
  for (auto& c : d.phclasses) { c.name = r.str(); c.members = r_bytes(r); }
  d.letterclasses.resize(r.count(8));
  for (auto& c : d.letterclasses) { c.name = r.str(); c.members = r.ustr(); }
}

void w_scrp(ByteWriter& w, const LanguageData& d) {
  const Scripts& s = d.scripts;
  w_strs(w, s.names);
  w.count(s.chars.size());
  for (auto& c : s.chars) { w.u32(c.ch); w.u8(c.script); w.u8(c.upper); w.u32(c.lower); w.ustr(c.canon); }
  w.count(s.homoglyphs.size());
  for (auto& h : s.homoglyphs) { w.u8(h.script); w.u32(h.from); w.u32(h.to); }
  w.count(s.normalize.size());
  for (auto& n : s.normalize) { w.ustr(n.first); w.ustr(n.second); }
  w.count(s.lexkey.size());
  for (auto& n : s.lexkey) { w.u32(n.first); w.ustr(n.second); }
}
void r_scrp(ByteReader& r, LanguageData& d) {
  Scripts& s = d.scripts;
  s.names = r_strs(r);
  s.chars.resize(r.count(14));
  for (auto& c : s.chars) { c.ch = r.u32(); c.script = r.u8(); c.upper = r.u8() != 0; c.lower = r.u32(); c.canon = r.ustr(); }
  s.homoglyphs.resize(r.count(9));
  for (auto& h : s.homoglyphs) { h.script = r.u8(); h.from = r.u32(); h.to = r.u32(); }
  s.normalize.resize(r.count(8));
  for (auto& n : s.normalize) { n.first = r.ustr(); n.second = r.ustr(); }
  s.lexkey.resize(r.count(8));
  for (auto& n : s.lexkey) { n.first = r.u32(); n.second = r.ustr(); }
}

void w_g2pr(ByteWriter& w, const LanguageData& d) {
  w.count(d.g2p.size());
  for (auto& g : d.g2p) { w.ustr(g.match); w_ctx(w, g.left); w_ctx(w, g.right); w_bytes(w, g.out); w.u32(g.line); }
}
void r_g2pr(ByteReader& r, LanguageData& d) {
  d.g2p.resize(r.count(20));
  for (auto& g : d.g2p) { g.match = r.ustr(); g.left = r_ctx(r); g.right = r_ctx(r); g.out = r_bytes(r); g.line = r.u32(); }
}

void w_plex(ByteWriter& w, const LanguageData& d) {
  w.count(d.postlex.rules.size());
  for (auto& p : d.postlex.rules) {
    w.u8(static_cast<uint8_t>(p.focus.kind)); w.u16(p.focus.id);
    w_pelems(w, p.left); w_pelems(w, p.right);
    w.u8(static_cast<uint8_t>(p.op)); w_bytes(w, p.repl); w.u32(p.line);
  }
  w.count(d.postlex.identical.size());
  for (auto& i : d.postlex.identical) {
    w.u16(i.cls); w.u8(static_cast<uint8_t>(i.within)); w.u16(i.within_val);
    w.u8(static_cast<uint8_t>(i.across)); w.u16(i.across_val);
  }
}
void r_plex(ByteReader& r, LanguageData& d) {
  d.postlex.rules.resize(r.count(20));
  for (auto& p : d.postlex.rules) {
    uint8_t k = r.u8();
    if (k > 1) throw FormatError("bad postlex focus");
    p.focus.kind = static_cast<PKind>(k); p.focus.id = r.u16();
    p.left = r_pelems(r); p.right = r_pelems(r);
    uint8_t op = r.u8();
    if (op > 2) throw FormatError("bad postlex op");
    p.op = static_cast<PostOp>(op); p.repl = r_bytes(r); p.line = r.u32();
  }
  d.postlex.identical.resize(r.count(8));
  for (auto& i : d.postlex.identical) {
    i.cls = r.u16();
    uint8_t a = r.u8(); i.within_val = r.u16();
    uint8_t b = r.u8(); i.across_val = r.u16();
    if (a > 2 || b > 2) throw FormatError("bad identical-pair op");
    i.within = static_cast<IdentOp>(a); i.across = static_cast<IdentOp>(b);
  }
}

void w_syll(ByteWriter& w, const LanguageData& d) {
  w.u16(d.syllables.nucleus_class);
  w.count(d.syllables.syllabic.size());
  for (auto& s : d.syllables.syllabic) { w.u8(s.ph); w_pelems(w, s.left); w_pelems(w, s.right); }
}
void r_syll(ByteReader& r, LanguageData& d) {
  d.syllables.nucleus_class = r.u16();
  d.syllables.syllabic.resize(r.count(9));
  for (auto& s : d.syllables.syllabic) { s.ph = r.u8(); s.left = r_pelems(r); s.right = r_pelems(r); }
}

void w_phtoks(ByteWriter& w, const std::vector<PhTok>& v) {
  w.count(v.size());
  for (auto& t : v) { w.u8(t.ph); w.u8(t.flags); }
}
std::vector<PhTok> r_phtoks(ByteReader& r) {
  std::vector<PhTok> v(r.count(2));
  for (auto& t : v) { t.ph = r.u8(); t.flags = r.u8(); }
  return v;
}

void w_lexi(ByteWriter& w, const LanguageData& d) {
  w.count(d.lexicon.size());
  for (auto& e : d.lexicon) { w.ustr(e.key); w_phtoks(w, e.phones); }
}
void r_lexi(ByteReader& r, LanguageData& d) {
  d.lexicon.resize(r.count(8));
  for (auto& e : d.lexicon) { e.key = r.ustr(); e.phones = r_phtoks(r); }
}

void w_accn(ByteWriter& w, const LanguageData& d) {
  const Accents& a = d.accents;
  w.u8(static_cast<uint8_t>(a.default_syll)); w.u8(static_cast<uint8_t>(a.default_type)); w.u8(a.never_last);
  w.count(a.entries.size());
  for (auto& e : a.entries) {
    w.ustr(e.key); w.u8(static_cast<uint8_t>(e.syll)); w.u8(static_cast<uint8_t>(e.type)); w_bytes(w, e.long_sylls);
  }
}
void r_accn(ByteReader& r, LanguageData& d) {
  Accents& a = d.accents;
  a.default_syll = static_cast<int8_t>(r.u8());
  uint8_t t = r.u8();
  if (t > 2) throw FormatError("bad accent type");
  a.default_type = static_cast<AccentType>(t);
  a.never_last = r.u8() != 0;
  a.entries.resize(r.count(10));
  for (auto& e : a.entries) {
    e.key = r.ustr(); e.syll = static_cast<int8_t>(r.u8());
    uint8_t tt = r.u8();
    if (tt > 2) throw FormatError("bad accent type");
    e.type = static_cast<AccentType>(tt); e.long_sylls = r_bytes(r);
  }
}

void w_clit(ByteWriter& w, const LanguageData& d) {
  w.count(d.clitics.size());
  for (auto& c : d.clitics) { w.ustr(c.key); w.u8(static_cast<uint8_t>(c.type)); }
}
void r_clit(ByteReader& r, LanguageData& d) {
  d.clitics.resize(r.count(5));
  for (auto& c : d.clitics) {
    c.key = r.ustr();
    uint8_t t = r.u8();
    if (t < 1 || t > 2) throw FormatError("bad clitic type");
    c.type = static_cast<CliticType>(t);
  }
}

void w_numb(ByteWriter& w, const LanguageData& d) {
  const Numbers& n = d.numbers;
  w.count(n.rulesets.size());
  for (auto& rs : n.rulesets) {
    w.str(rs.name);
    w.count(rs.rules.size());
    for (auto& rule : rs.rules) {
      w.i64(rule.base); w.i64(rule.divisor); w.u8(rule.negative);
      w.count(rule.parts.size());
      for (auto& p : rule.parts) {
        w.u8(static_cast<uint8_t>(p.kind)); w.str(p.text); w.i16(p.ruleset); w_strs(w, p.forms);
      }
    }
  }
  w.count(n.plurals.size());
  for (auto& p : n.plurals) { w.u8(p.cat); w.str(p.expr); }
  w.count(n.replace.size());
  for (auto& p : n.replace) { w.str(p.first); w.str(p.second); }
}
void r_numb(ByteReader& r, LanguageData& d) {
  Numbers& n = d.numbers;
  n.rulesets.resize(r.count(8));
  for (auto& rs : n.rulesets) {
    rs.name = r.str();
    rs.rules.resize(r.count(21));
    for (auto& rule : rs.rules) {
      rule.base = r.i64(); rule.divisor = r.i64(); rule.negative = r.u8() != 0;
      if (rule.divisor <= 0) throw FormatError("bad RBNF divisor");
      rule.parts.resize(r.count(11));
      for (auto& p : rule.parts) {
        uint8_t k = r.u8();
        if (k > 6) throw FormatError("bad RBNF part");
        p.kind = static_cast<RbnfPartKind>(k); p.text = r.str(); p.ruleset = r.i16(); p.forms = r_strs(r);
        if (p.ruleset >= static_cast<int>(n.rulesets.size()) || p.ruleset < -2)
          throw FormatError("bad RBNF ruleset reference");
        if (p.kind == RbnfPartKind::Plural && p.forms.size() != kPluralCount)
          throw FormatError("bad RBNF plural");
      }
    }
  }
  n.plurals.resize(r.count(5));
  for (auto& p : n.plurals) {
    uint8_t c = r.u8();
    if (c >= kPluralCount) throw FormatError("bad plural category");
    p.cat = static_cast<PluralCat>(c); p.expr = r.str();
  }
  n.replace.resize(r.count(8));
  for (auto& p : n.replace) { p.first = r.str(); p.second = r.str(); }
}

void w_norm(ByteWriter& w, const LanguageData& d) {
  w.count(d.lists.size());
  for (auto& l : d.lists) { w.str(l.name); w_ustrs(w, l.words); }
  w.count(d.maps.size());
  for (auto& m : d.maps) {
    w.str(m.name); w.count(m.items.size());
    for (auto& it : m.items) { w.i64(it.first); w.str(it.second); }
  }
  w.count(d.nrules.size());
  for (auto& n : d.nrules) {
    w_nelems(w, n.left); w_nelems(w, n.core); w_nelems(w, n.right);
    w.count(n.out.size());
    for (auto& o : n.out) {
      w.u8(static_cast<uint8_t>(o.fmt)); w.str(o.text); w.u8(o.cap); w.u16(o.idx); w_strs(w, o.forms);
    }
    w.u8(n.sentence_end_check); w.str(n.where);
  }
}
void r_norm(ByteReader& r, LanguageData& d) {
  d.lists.resize(r.count(8));
  for (auto& l : d.lists) { l.name = r.str(); l.words = r_ustrs(r); }
  d.maps.resize(r.count(8));
  for (auto& m : d.maps) {
    m.name = r.str(); m.items.resize(r.count(12));
    for (auto& it : m.items) { it.first = r.i64(); it.second = r.str(); }
  }
  d.nrules.resize(r.count(18));
  for (auto& n : d.nrules) {
    n.left = r_nelems(r); n.core = r_nelems(r); n.right = r_nelems(r);
    n.out.resize(r.count(12));
    for (auto& o : n.out) {
      uint8_t f = r.u8();
      if (f > 5) throw FormatError("bad output format");
      o.fmt = static_cast<NFmt>(f); o.text = r.str(); o.cap = r.u8(); o.idx = r.u16(); o.forms = r_strs(r);
      if (o.fmt == NFmt::Ruleset && o.idx >= d.numbers.rulesets.size()) throw FormatError("bad ruleset index");
      if (o.fmt == NFmt::Map && o.idx >= d.maps.size()) throw FormatError("bad map index");
      if (o.fmt == NFmt::Plural && o.forms.size() != kPluralCount) throw FormatError("bad plural forms");
    }
    for (auto* v : {&n.left, &n.core, &n.right})
      for (auto& e : *v)
        if (e.kind == NKind::List && e.list >= d.lists.size()) throw FormatError("bad list index");
    n.sentence_end_check = r.u8() != 0; n.where = r.str();
  }
}

void w_unit(ByteWriter& w, const LanguageData& d) {
  w.count(d.units.size());
  for (auto& u : d.units) {
    w_ustrs(w, u.keys); w_ustrs(w, u.raw); w.u8(u.case_sensitive); w.u8(u.prefix); w.i16(u.ruleset); w_strs(w, u.forms);
  }
}
void r_unit(ByteReader& r, LanguageData& d) {
  d.units.resize(r.count(18));
  for (auto& u : d.units) {
    u.keys = r_ustrs(r); u.raw = r_ustrs(r); u.case_sensitive = r.u8() != 0; u.prefix = r.u8() != 0;
    u.ruleset = r.i16(); u.forms = r_strs(r);
    if (u.forms.size() != kPluralCount) throw FormatError("bad unit forms");
    if (u.ruleset < -1 || u.ruleset >= static_cast<int>(d.numbers.rulesets.size())) throw FormatError("bad unit ruleset");
  }
}

void w_abbr(ByteWriter& w, const LanguageData& d) {
  w.count(d.abbrevs.size());
  for (auto& a : d.abbrevs) { w.ustr(a.key); w.str(a.expansion); w.u8(a.case_sensitive); w.u8(a.may_end); }
}
void r_abbr(ByteReader& r, LanguageData& d) {
  d.abbrevs.resize(r.count(10));
  for (auto& a : d.abbrevs) { a.key = r.ustr(); a.expansion = r.str(); a.case_sensitive = r.u8() != 0; a.may_end = r.u8() != 0; }
}

void w_symb(ByteWriter& w, const LanguageData& d) {
  w.count(d.symbols.size());
  for (auto& s : d.symbols) { w.u32(s.ch); w.u8(s.level); w.u8(static_cast<uint8_t>(s.brk)); w.str(s.name); }
}
void r_symb(ByteReader& r, LanguageData& d) {
  d.symbols.resize(r.count(10));
  for (auto& s : d.symbols) {
    s.ch = r.u32(); s.level = r.u8();
    uint8_t b = r.u8();
    if (b > static_cast<uint8_t>(BreakType::Paragraph)) throw FormatError("bad break type");
    s.brk = static_cast<BreakType>(b); s.name = r.str();
  }
}

void w_emoj(ByteWriter& w, const LanguageData& d) {
  w.count(d.emoji.size());
  for (auto& e : d.emoji) { w.ustr(e.seq); w.str(e.name); }
}
void r_emoj(ByteReader& r, LanguageData& d) {
  d.emoji.resize(r.count(8));
  for (auto& e : d.emoji) { e.seq = r.ustr(); e.name = r.str(); }
}

void w_spel(ByteWriter& w, const LanguageData& d) {
  w.str(d.spell_capital);
  w.count(d.spelling.size());
  for (auto& s : d.spelling) { w.u32(s.ch); w.str(s.name); }
}
void r_spel(ByteReader& r, LanguageData& d) {
  d.spell_capital = r.str();
  d.spelling.resize(r.count(8));
  for (auto& s : d.spelling) { s.ch = r.u32(); s.name = r.str(); }
}

void w_acro(ByteWriter& w, const LanguageData& d) {
  w_ustrs(w, d.acronyms.words); w_ustrs(w, d.acronyms.spell); w.i32(d.acronyms.spell_max_len);
}
void r_acro(ByteReader& r, LanguageData& d) {
  d.acronyms.words = r_ustrs(r); d.acronyms.spell = r_ustrs(r); d.acronyms.spell_max_len = r.i32();
}

void w_pros(ByteWriter& w, const LanguageData& d) {
  w.count(d.prosody.size());
  for (auto& kv : d.prosody) { w.str(kv.first); w.f64(kv.second); }
}
void r_pros(ByteReader& r, LanguageData& d) {
  size_t n = r.count(12);
  for (size_t i = 0; i < n; ++i) { std::string k = r.str(); d.prosody[k] = r.f64(); }
}

struct SectionDef {
  uint32_t id;
  void (*write)(ByteWriter&, const LanguageData&);
  void (*read)(ByteReader&, LanguageData&);
  bool required;
};

// Order matters for reading: sections that others refer to come first.
const SectionDef kSections[] = {
    {fourcc("META"), w_meta, r_meta, true},  {fourcc("PHON"), w_phon, r_phon, true},
    {fourcc("CLAS"), w_clas, r_clas, true},  {fourcc("SCRP"), w_scrp, r_scrp, true},
    {fourcc("G2PR"), w_g2pr, r_g2pr, true},  {fourcc("PLEX"), w_plex, r_plex, false},
    {fourcc("SYLL"), w_syll, r_syll, false}, {fourcc("LEXI"), w_lexi, r_lexi, false},
    {fourcc("ACCN"), w_accn, r_accn, false}, {fourcc("CLIT"), w_clit, r_clit, false},
    {fourcc("NUMB"), w_numb, r_numb, true},  {fourcc("NORM"), w_norm, r_norm, false},
    {fourcc("UNIT"), w_unit, r_unit, false}, {fourcc("ABBR"), w_abbr, r_abbr, false},
    {fourcc("SYMB"), w_symb, r_symb, false}, {fourcc("EMOJ"), w_emoj, r_emoj, false},
    {fourcc("SPEL"), w_spel, r_spel, false}, {fourcc("ACRO"), w_acro, r_acro, false},
    {fourcc("PROS"), w_pros, r_pros, false},
};

uint32_t rd32(const uint8_t* p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

std::vector<uint8_t> write_dat(const LanguageData& d, uint64_t timestamp) {
  const size_t nsec = sizeof kSections / sizeof kSections[0];
  ByteWriter w;
  // header
  w.bytes("MBNGLANG", 8);
  w.u16(kDatMajor);
  w.u16(kDatMinor);
  char code[16] = {};
  std::string c = d.meta_value("code");
  std::memcpy(code, c.data(), std::min<size_t>(c.size(), 15));
  w.bytes(code, 16);
  w.u32(kCompilerVersion);
  w.u64(timestamp);
  w.u32(0);                              // flags
  w.u32(static_cast<uint32_t>(nsec));    // section count
  w.u32(static_cast<uint32_t>(kHeaderSize));  // directory offset
  w.u32(0);                              // file size (patched)
  w.u32(0);                              // crc (patched)
  w.u32(0);                              // reserved
  // directory placeholder
  size_t dir = w.size();
  for (size_t i = 0; i < nsec * 4; ++i) w.u32(0);
  for (size_t i = 0; i < nsec; ++i) {
    ByteWriter s;
    kSections[i].write(s, d);
    size_t off = w.size();
    w.bytes(s.data().data(), s.size());
    w.patch_u32(dir + i * 16 + 0, kSections[i].id);
    w.patch_u32(dir + i * 16 + 4, static_cast<uint32_t>(off));
    w.patch_u32(dir + i * 16 + 8, static_cast<uint32_t>(s.size()));
    w.patch_u32(dir + i * 16 + 12, crc32(s.data().data(), s.size()));
  }
  w.patch_u32(52, static_cast<uint32_t>(w.size()));
  w.patch_u32(56, crc32(w.data().data(), w.size()));
  return w.data();
}

void read_dat(const uint8_t* p, size_t n, LanguageData& d) {
  if (n < kHeaderSize || std::memcmp(p, "MBNGLANG", 8) != 0) throw FormatError("not an MBROLA NG language file");
  uint16_t major = static_cast<uint16_t>(p[8] | (p[9] << 8));
  if (major != kDatMajor)
    throw FormatError(format("language file format %u is not supported (this engine reads %u)", major, kDatMajor));
  uint32_t nsec = rd32(p + 44), dir = rd32(p + 48), size = rd32(p + 52), crc = rd32(p + 56);
  if (size != n) throw FormatError("language file has wrong size (truncated?)");
  std::vector<uint8_t> tmp(p, p + n);
  std::memset(tmp.data() + 56, 0, 4);
  if (crc32(tmp.data(), n) != crc) throw FormatError("language file is corrupt (CRC mismatch)");
  if (nsec > 1000 || dir > n || static_cast<uint64_t>(nsec) * 16 > n - dir) throw FormatError("bad section directory");

  for (const SectionDef& def : kSections) {
    bool found = false;
    for (uint32_t i = 0; i < nsec; ++i) {
      const uint8_t* e = p + dir + i * 16;
      if (rd32(e) != def.id) continue;
      uint32_t off = rd32(e + 4), len = rd32(e + 8), scrc = rd32(e + 12);
      if (off > n || len > n - off) throw FormatError("section out of bounds");
      if (crc32(p + off, len) != scrc) throw FormatError("section CRC mismatch");
      ByteReader r(p + off, len);
      def.read(r, d);
      found = true;
      break;
    }
    if (!found && def.required) throw FormatError("required section missing");
  }
  // cross-checks of phoneme ids
  const size_t np = d.phonemes.size();
  auto chk = [&](uint8_t id) { if (id >= np) throw FormatError("phoneme id out of range"); };
  for (auto& ph : d.phonemes) if (ph.partner != kNoPhoneme) chk(ph.partner);
  for (auto& c : d.phclasses) for (auto m : c.members) chk(m);
  for (auto& g : d.g2p) {
    for (auto o : g.out) chk(o);
    for (auto* v : {&g.left, &g.right})
      for (auto& e : *v) if (e.kind == CtxKind::Class && e.cls >= d.letterclasses.size()) throw FormatError("bad class");
  }
  auto chkp = [&](const std::vector<PElem>& v) {
    for (auto& e : v) {
      if (e.kind == PKind::Phoneme) chk(static_cast<uint8_t>(e.id));
      if (e.kind == PKind::Class && e.id >= d.phclasses.size()) throw FormatError("bad class");
    }
  };
  for (auto& r : d.postlex.rules) {
    chkp({r.focus}); chkp(r.left); chkp(r.right);
    for (auto o : r.repl) chk(o);
  }
  for (auto& i : d.postlex.identical) if (i.cls >= d.phclasses.size()) throw FormatError("bad class");
  for (auto& s : d.syllables.syllabic) { chk(s.ph); chkp(s.left); chkp(s.right); }
  if (d.syllables.nucleus_class != 0xFFFF && d.syllables.nucleus_class >= d.phclasses.size()) throw FormatError("bad class");
  for (auto& l : d.lexicon) for (auto& t : l.phones) chk(t.ph);
  for (auto& s : d.scripts.chars) if (s.script >= d.scripts.names.size()) throw FormatError("bad script");
  for (auto& h : d.scripts.homoglyphs) if (h.script >= d.scripts.names.size()) throw FormatError("bad script");
}

}  // namespace mbng
