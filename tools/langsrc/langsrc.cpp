// MBROLA NG - language source compiler
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Source format: see languages/hr/*.txt (every file documents its syntax in
// its header) and docs in ANALYSIS section 8.
#include "langsrc.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

#include "engine/loader.h"
#include "lang/datfile.h"
#include "lang/rbnf.h"
#include "text/canon.h"
#include "util/bytes.h"

namespace mbng {

namespace {

struct Line {
  int no = 0;
  std::string text;
};

bool is_comment(const std::string& t) {
  if (t.empty() || t[0] != '#') return false;
  if (t.size() == 1) return true;
  char c = t[1];
  return c == ' ' || c == '\t' || c == '#' || c == '=' || c == '-' || c == '!';
}

std::string strip_inline_comment(const std::string& s) {
  for (size_t i = 1; i < s.size(); ++i) {
    if (s[i] == '#' && (s[i - 1] == ' ' || s[i - 1] == '\t') &&
        (i + 1 == s.size() || s[i + 1] == ' ' || s[i + 1] == '\t'))
      return trim(s.substr(0, i));
  }
  return s;
}

bool is_upper_ident(const std::string& s) {
  if (s.size() < 2 || !(s[0] >= 'A' && s[0] <= 'Z')) return false;
  for (char c : s)
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
  return true;
}

// "U+0023" -> '#', otherwise the UTF-8 text itself.
u32str parse_chars(const std::string& tok) {
  if (tok.size() >= 3 && (tok[0] == 'U' || tok[0] == 'u') && tok[1] == '+') {
    char* end = nullptr;
    unsigned long v = std::strtoul(tok.c_str() + 2, &end, 16);
    if (end && *end == 0 && v > 0 && v <= 0x10FFFF) return u32str(1, static_cast<char32_t>(v));
  }
  if (tok == "\"\"") return u32str();
  if (tok.size() >= 2 && tok.front() == '"' && tok.back() == '"') return utf8_to_u32(tok.substr(1, tok.size() - 2));
  return utf8_to_u32(tok);
}

std::vector<std::string> split_on(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t a = 0;
  for (;;) {
    size_t b = s.find(sep, a);
    out.push_back(trim(s.substr(a, b == std::string::npos ? std::string::npos : b - a)));
    if (b == std::string::npos) break;
    a = b + 1;
  }
  return out;
}

class Compiler {
 public:
  Compiler(const std::string& dir, CompileResult& r) : dir_(dir), r_(r), d_(r.data) {}
  void run();

 private:
  // --- helpers
  bool read(const std::string& name, std::vector<Line>& lines, bool required, bool inline_comments = true);
  void err(int line, const std::string& msg) { r_.errors.push_back(file_ + ":" + std::to_string(line) + ": " + msg); }
  void warn(int line, const std::string& msg) { r_.warnings.push_back(file_ + ":" + std::to_string(line) + ": " + msg); }
  int phoneme(const std::string& sym, int line);
  int phclass(const std::string& name) const;
  int letterclass(const std::string& name) const;
  bool pelem(const std::string& tok, int line, std::vector<PElem>& out);
  bool pcontext(const std::string& s, int line, std::vector<PElem>& left, std::vector<PElem>& right);
  int anon_class(const std::vector<uint8_t>& members);
  u32str key(const std::string& s) const { return canon_->lexkey(canon_->text(utf8_to_u32(s))); }

  // --- files
  void language();
  void phonemes();
  void classes();
  void scripts();
  void g2p();
  void postlex();
  void syllables();
  void lexicon();
  void accents();
  void clitics();
  void numbers();
  void normalize_file(const std::string& name);
  void units();
  void abbreviations();
  void symbols();
  void emoji();
  void spelling();
  void acronyms();
  void prosody();
  void tests();

  bool parse_rbnf_text(const std::string& text, int line, RbnfRule& rule, std::vector<std::string>& refs);
  bool parse_pattern_elem(const std::string& tok, int line, NElem& e);

  std::string dir_;
  std::string file_;
  CompileResult& r_;
  LanguageData& d_;
  std::unique_ptr<Canonicalizer> canon_;
  std::vector<std::string> plural_order_;  // category order used by units.txt
  int anon_ = 0;
};

bool Compiler::read(const std::string& name, std::vector<Line>& lines, bool required, bool inline_comments) {
  file_ = name;
  lines.clear();
  std::vector<uint8_t> buf;
  std::string path = dir_ + "/" + name;
  if (!read_file(path, buf)) {
    if (required) r_.errors.push_back(name + ": file missing (required)");
    return false;
  }
  r_.files.push_back(name);
  std::string all(buf.begin(), buf.end());
  if (all.size() >= 3 && static_cast<unsigned char>(all[0]) == 0xEF && static_cast<unsigned char>(all[1]) == 0xBB &&
      static_cast<unsigned char>(all[2]) == 0xBF)
    all.erase(0, 3);
  size_t a = 0;
  int no = 0;
  while (a <= all.size()) {
    size_t b = all.find('\n', a);
    std::string l = all.substr(a, b == std::string::npos ? std::string::npos : b - a);
    ++no;
    if (!l.empty() && l.back() == '\r') l.pop_back();
    std::string t = trim(l);
    if (!t.empty() && !is_comment(t)) {
      if (inline_comments) t = strip_inline_comment(t);
      if (!t.empty()) lines.push_back({no, t});
    }
    if (b == std::string::npos) break;
    a = b + 1;
  }
  return true;
}

int Compiler::phoneme(const std::string& sym, int line) {
  int id = d_.find_phoneme(sym);
  if (id < 0) err(line, "unknown phoneme '" + sym + "'");
  return id;
}

int Compiler::phclass(const std::string& name) const {
  for (size_t i = 0; i < d_.phclasses.size(); ++i)
    if (d_.phclasses[i].name == name) return static_cast<int>(i);
  return -1;
}

int Compiler::letterclass(const std::string& name) const {
  for (size_t i = 0; i < d_.letterclasses.size(); ++i)
    if (d_.letterclasses[i].name == name) return static_cast<int>(i);
  return -1;
}

int Compiler::anon_class(const std::vector<uint8_t>& members) {
  PhClass c;
  c.name = "(anonymous " + std::to_string(++anon_) + ")";
  c.members = members;
  std::sort(c.members.begin(), c.members.end());
  c.members.erase(std::unique(c.members.begin(), c.members.end()), c.members.end());
  d_.phclasses.push_back(c);
  return static_cast<int>(d_.phclasses.size() - 1);
}

// phoneme, CLASS, (a b c), |, |?
bool Compiler::pelem(const std::string& tok, int line, std::vector<PElem>& out) {
  if (tok == "|") { out.push_back({PKind::Boundary, 0}); return true; }
  if (tok == "|?") { out.push_back({PKind::OptBoundary, 0}); return true; }
  int id = d_.find_phoneme(tok);
  if (id >= 0) { out.push_back({PKind::Phoneme, static_cast<uint16_t>(id)}); return true; }
  int c = phclass(tok);
  if (c >= 0) { out.push_back({PKind::Class, static_cast<uint16_t>(c)}); return true; }
  err(line, "unknown phoneme or class '" + tok + "'");
  return false;
}

// "L1 L2 _ R1 R2" with (alternatives)
bool Compiler::pcontext(const std::string& s, int line, std::vector<PElem>& left, std::vector<PElem>& right) {
  // expand (a b c) groups into anonymous classes first
  std::vector<std::string> toks;
  std::string cur;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '(') {
      size_t j = s.find(')', i);
      if (j == std::string::npos) { err(line, "missing ')'"); return false; }
      std::vector<uint8_t> mem;
      for (auto& t : split_ws(s.substr(i + 1, j - i - 1))) {
        int id = d_.find_phoneme(t);
        int c = id < 0 ? phclass(t) : -1;
        if (id >= 0) mem.push_back(static_cast<uint8_t>(id));
        else if (c >= 0) mem.insert(mem.end(), d_.phclasses[c].members.begin(), d_.phclasses[c].members.end());
        else { err(line, "unknown phoneme or class '" + t + "'"); return false; }
      }
      if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
      toks.push_back(d_.phclasses[anon_class(mem)].name);
      i = j;
    } else if (s[i] == ' ' || s[i] == '\t') {
      if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
    } else {
      cur.push_back(s[i]);
    }
  }
  if (!cur.empty()) toks.push_back(cur);
  bool seen = false;
  for (auto& t : toks) {
    if (t == "_") {
      if (seen) { err(line, "two '_' in context"); return false; }
      seen = true;
      continue;
    }
    std::vector<PElem>& dst = seen ? right : left;
    if (!t.empty() && t[0] == '(') {
      int c = phclass(t);
      dst.push_back({PKind::Class, static_cast<uint16_t>(c)});
      continue;
    }
    if (!pelem(t, line, dst)) return false;
  }
  if (!seen) { err(line, "context needs '_'"); return false; }
  return true;
}

// ------------------------------------------------------------ language.txt
void Compiler::language() {
  std::vector<Line> lines;
  if (!read("language.txt", lines, true)) return;
  for (auto& l : lines) {
    size_t eq = l.text.find('=');
    if (eq == std::string::npos) { err(l.no, "expected 'key = value'"); continue; }
    d_.meta[trim(l.text.substr(0, eq))] = trim(l.text.substr(eq + 1));
  }
  for (const char* k : {"code", "name", "default_cardinal"})
    if (!d_.meta.count(k)) err(0, std::string("missing key '") + k + "'");
}

// ------------------------------------------------------------ phonemes.txt
void Compiler::phonemes() {
  std::vector<Line> lines;
  if (!read("phonemes.txt", lines, true)) return;
  std::vector<std::pair<int, std::string>> partners;
  for (auto& l : lines) {
    auto t = split_ws(l.text);
    if (t.size() < 5) { err(l.no, "expected: symbol type duration min-duration partner [features]"); continue; }
    Phoneme p;
    p.sym = t[0];
    if (d_.find_phoneme(p.sym) >= 0) { err(l.no, "duplicate phoneme '" + p.sym + "'"); continue; }
    if (t[1] == "vowel") p.type = PhType::Vowel;
    else if (t[1] == "consonant") p.type = PhType::Consonant;
    else if (t[1] == "pause") p.type = PhType::Pause;
    else { err(l.no, "type must be vowel, consonant or pause"); continue; }
    p.dur = static_cast<uint16_t>(std::atoi(t[2].c_str()));
    p.min_dur = static_cast<uint16_t>(std::atoi(t[3].c_str()));
    if (p.type != PhType::Pause && (p.dur < 10 || p.min_dur < 5 || p.min_dur > p.dur))
      err(l.no, "invalid durations");
    for (size_t k = 5; k < t.size(); ++k) {
      if (starts_with(t[k], "viseme=")) p.viseme = static_cast<uint8_t>(std::atoi(t[k].c_str() + 7));
      else p.features.push_back(t[k]);
    }
    if (t[4] != "-") partners.push_back({l.no, t[4]});
    else partners.push_back({l.no, ""});
    d_.phonemes.push_back(p);
    if (d_.phonemes.size() > kMaxPhonemes) { err(l.no, "too many phonemes"); return; }
  }
  for (size_t i = 0; i < d_.phonemes.size(); ++i)
    if (!partners[i].second.empty()) {
      int id = phoneme(partners[i].second, partners[i].first);
      if (id >= 0) d_.phonemes[i].partner = static_cast<uint8_t>(id);
    }
  int pauses = 0;
  for (auto& p : d_.phonemes) pauses += p.type == PhType::Pause;
  if (pauses != 1) err(0, "exactly one pause phoneme is required");
}

// ------------------------------------------------------------ classes.txt
void Compiler::classes() {
  std::vector<Line> lines;
  if (!read("classes.txt", lines, false)) return;
  int section = 0;  // 1 phonemes, 2 letters
  for (auto& l : lines) {
    if (l.text == "[phonemes]") { section = 1; continue; }
    if (l.text == "[letters]") { section = 2; continue; }
    size_t eq = l.text.find('=');
    if (eq == std::string::npos || !section) { err(l.no, "expected [section] or 'NAME = items'"); continue; }
    std::string name = trim(l.text.substr(0, eq));
    if (!is_upper_ident(name)) { err(l.no, "class names are CAPITALS (2+ characters): " + name); continue; }
    if (phclass(name) >= 0 || letterclass(name) >= 0) { err(l.no, "duplicate class " + name); continue; }
    auto items = split_ws(l.text.substr(eq + 1));
    if (section == 1) {
      if (d_.find_phoneme(name) >= 0) { err(l.no, "class name equals a phoneme symbol"); continue; }
      PhClass c;
      c.name = name;
      for (auto& it : items) {
        int id = d_.find_phoneme(it);
        int k = id < 0 ? phclass(it) : -1;
        if (id >= 0) c.members.push_back(static_cast<uint8_t>(id));
        else if (k >= 0) c.members.insert(c.members.end(), d_.phclasses[k].members.begin(), d_.phclasses[k].members.end());
        else err(l.no, "unknown phoneme or class '" + it + "'");
      }
      std::sort(c.members.begin(), c.members.end());
      c.members.erase(std::unique(c.members.begin(), c.members.end()), c.members.end());
      d_.phclasses.push_back(c);
    } else {
      LetterClass c;
      c.name = name;
      for (auto& it : items) {
        int k = letterclass(it);
        if (k >= 0) c.members += d_.letterclasses[k].members;
        else c.members += parse_chars(it);
      }
      std::sort(c.members.begin(), c.members.end());
      c.members.erase(std::unique(c.members.begin(), c.members.end()), c.members.end());
      d_.letterclasses.push_back(c);
    }
  }
}

// ------------------------------------------------------------ scripts.txt
void Compiler::scripts() {
  std::vector<Line> lines;
  Scripts& S = d_.scripts;
  if (read("scripts.txt", lines, true)) {
    enum { None, Script, Homo, Norm, LexKey } sec = None;
    int script = -1;
    std::map<char32_t, int> seen;
    for (auto& l : lines) {
      if (l.text.front() == '[' && l.text.back() == ']') {
        auto t = split_ws(l.text.substr(1, l.text.size() - 2));
        if (t.size() == 2 && (t[0] == "script" || t[0] == "homoglyphs")) {
          auto it = std::find(S.names.begin(), S.names.end(), t[1]);
          if (it == S.names.end()) {
            if (t[0] == "homoglyphs") { err(l.no, "homoglyphs for an undefined script"); sec = None; continue; }
            S.names.push_back(t[1]);
            it = S.names.end() - 1;
          }
          script = static_cast<int>(it - S.names.begin());
          sec = t[0] == "script" ? Script : Homo;
        } else if (t.size() == 1 && t[0] == "normalize") sec = Norm;
        else if (t.size() == 1 && t[0] == "lexkey") sec = LexKey;
        else { err(l.no, "unknown section"); sec = None; }
        continue;
      }
      size_t arrow = l.text.find("->");
      if (arrow == std::string::npos || sec == None) { err(l.no, "expected 'from -> to' inside a section"); continue; }
      auto lhs = split_ws(l.text.substr(0, arrow));
      auto rhs = split_ws(l.text.substr(arrow + 2));
      u32str to;
      for (auto& t : rhs) to += parse_chars(t);
      switch (sec) {
        case Script: {
          if (lhs.empty()) { err(l.no, "missing letter"); break; }
          if (to.empty()) { err(l.no, "missing canonical letters"); break; }
          u32str lower = parse_chars(lhs[0]);
          if (lower.size() != 1) { err(l.no, "one character expected: " + lhs[0]); break; }
          for (size_t k = 0; k < lhs.size(); ++k) {
            u32str ch = parse_chars(lhs[k]);
            if (ch.size() != 1) { err(l.no, "one character expected: " + lhs[k]); continue; }
            if (seen.count(ch[0])) { err(l.no, "character listed twice: " + lhs[k]); continue; }
            seen[ch[0]] = 1;
            CharEntry e;
            e.ch = ch[0];
            e.script = static_cast<uint8_t>(script);
            e.upper = k > 0;
            e.lower = lower[0];
            e.canon = to;
            S.chars.push_back(e);
          }
          break;
        }
        case Homo: {
          if (lhs.size() != 1 || to.size() != 1) { err(l.no, "homoglyph: 'x -> y' with single characters"); break; }
          u32str f = parse_chars(lhs[0]);
          if (f.size() != 1) { err(l.no, "one character expected"); break; }
          S.homoglyphs.push_back({static_cast<uint8_t>(script), f[0], to[0]});
          break;
        }
        case Norm: {
          u32str from;
          for (auto& t : lhs) from += parse_chars(t);
          if (from.empty()) { err(l.no, "empty source"); break; }
          S.normalize.push_back({from, to});
          break;
        }
        case LexKey: {
          u32str f = lhs.size() == 1 ? parse_chars(lhs[0]) : u32str();
          if (f.size() != 1) { err(l.no, "one character expected"); break; }
          S.lexkey.push_back({f[0], to});
          break;
        }
        default: break;
      }
    }
    for (auto& h : S.homoglyphs) {
      bool found = false;
      for (auto& c : S.chars) if (c.ch == h.to && c.script == h.script) found = true;
      if (!found) warn(0, "homoglyph target is not a letter of its script: U+" + format("%04X", h.to));
    }
  }
  canon_ = std::make_unique<Canonicalizer>(S);
}

// ------------------------------------------------------------ g2p.txt
void Compiler::g2p() {
  std::vector<Line> lines;
  if (!read("g2p.txt", lines, true)) return;
  std::set<char32_t> canon_letters;
  for (auto& c : d_.scripts.chars) for (char32_t x : c.canon) canon_letters.insert(x);
  auto parse_ctx = [&](const std::string& s, int line, std::vector<CtxElem>& out) {
    u32str u = utf8_to_u32(s);
    for (size_t i = 0; i < u.size(); ++i) {
      char32_t c = u[i];
      if (c == ' ' || c == '\t') continue;
      if (c == '^' || c == '$') { out.push_back({CtxKind::Boundary, 0, 0}); continue; }
      if (c == '{') {
        size_t j = u.find('}', i);
        if (j == u32str::npos) { err(line, "missing '}'"); return false; }
        std::string name = u32_to_utf8(u.substr(i + 1, j - i - 1));
        int k = letterclass(name);
        if (k < 0) { err(line, "unknown letter class " + name); return false; }
        out.push_back({CtxKind::Class, 0, static_cast<uint16_t>(k)});
        i = j;
        continue;
      }
      out.push_back({CtxKind::Char, c, 0});
    }
    return true;
  };
  for (auto& l : lines) {
    size_t arrow = l.text.find("->");
    if (arrow == std::string::npos) { err(l.no, "expected 'letters -> phonemes [/ left _ right]'"); continue; }
    std::string lhs = trim(l.text.substr(0, arrow));
    std::string rhs = l.text.substr(arrow + 2);
    std::string ctx;
    size_t slash = rhs.find('/');
    if (slash != std::string::npos) { ctx = trim(rhs.substr(slash + 1)); rhs = rhs.substr(0, slash); }
    G2PRule r;
    r.line = static_cast<uint32_t>(l.no);
    u32str m = utf8_to_u32(lhs);
    if (!m.empty() && m[0] == '^') { r.left.push_back({CtxKind::Boundary, 0, 0}); m.erase(0, 1); }
    if (!m.empty() && m.back() == '$') { r.right.push_back({CtxKind::Boundary, 0, 0}); m.pop_back(); }
    if (m.empty()) { err(l.no, "empty letters"); continue; }
    for (char32_t c : m)
      if (!canon_letters.count(c)) warn(l.no, "'" + u32_to_utf8(c) + "' is not a canonical letter of scripts.txt");
    r.match = m;
    for (auto& t : split_ws(rhs)) {
      if (t == "0") continue;
      int id = phoneme(t, l.no);
      if (id >= 0) r.out.push_back(static_cast<uint8_t>(id));
    }
    if (!ctx.empty()) {
      // the focus marker '_' - not an underscore inside a {CLASS_NAME}
      size_t us = std::string::npos;
      int depth = 0;
      for (size_t k = 0; k < ctx.size() && us == std::string::npos; ++k) {
        if (ctx[k] == '{') ++depth;
        else if (ctx[k] == '}') --depth;
        else if (ctx[k] == '_' && depth == 0) us = k;
      }
      if (us == std::string::npos) { err(l.no, "context needs '_'"); continue; }
      std::vector<CtxElem> left, right;
      if (!parse_ctx(ctx.substr(0, us), l.no, left) || !parse_ctx(ctx.substr(us + 1), l.no, right)) continue;
      r.left.insert(r.left.begin(), left.begin(), left.end());
      r.right.insert(r.right.end(), right.begin(), right.end());
    }
    d_.g2p.push_back(r);
  }
  // every canonical letter should have a rule
  for (char32_t c : canon_letters) {
    bool found = false;
    for (auto& r : d_.g2p) if (!r.match.empty() && r.match[0] == c && r.left.empty() && r.right.empty()) found = true;
    if (!found) warn(0, "no context-free rule starts with canonical letter '" + u32_to_utf8(c) + "'");
  }
}

// ------------------------------------------------------------ postlex.txt
void Compiler::postlex() {
  std::vector<Line> lines;
  if (!read("postlex.txt", lines, false)) return;
  for (auto& l : lines) {
    if (starts_with(l.text, "@identical")) {
      auto t = split_ws(l.text);
      if (t.size() < 2) { err(l.no, "@identical CLASS within=op:value across=op:value"); continue; }
      IdentRule ir;
      std::vector<PElem> e;
      if (!pelem(t[1], l.no, e)) continue;
      if (e[0].kind == PKind::Class) ir.cls = e[0].id;
      else ir.cls = static_cast<uint16_t>(anon_class({static_cast<uint8_t>(e[0].id)}));
      for (size_t k = 2; k < t.size(); ++k) {
        size_t eq = t[k].find('='), co = t[k].find(':');
        if (eq == std::string::npos) { err(l.no, "bad option " + t[k]); continue; }
        std::string what = t[k].substr(0, eq);
        std::string op = t[k].substr(eq + 1, co == std::string::npos ? std::string::npos : co - eq - 1);
        int val = co == std::string::npos ? 100 : std::atoi(t[k].c_str() + co + 1);
        IdentOp o;
        if (op == "merge") o = IdentOp::Merge;
        else if (op == "pause") o = IdentOp::Pause;
        else if (op == "keep") o = IdentOp::Keep;
        else { err(l.no, "op must be merge, pause or keep"); continue; }
        if (what == "within") { ir.within = o; ir.within_val = static_cast<uint16_t>(val); }
        else if (what == "across") { ir.across = o; ir.across_val = static_cast<uint16_t>(val); }
        else err(l.no, "expected within= or across=");
      }
      d_.postlex.identical.push_back(ir);
      continue;
    }
    size_t arrow = l.text.find("->");
    size_t slash = l.text.find('/', arrow == std::string::npos ? 0 : arrow);
    if (arrow == std::string::npos || slash == std::string::npos) {
      err(l.no, "expected 'focus -> replacement / left _ right'");
      continue;
    }
    PostlexRule r;
    r.line = static_cast<uint32_t>(l.no);
    std::vector<PElem> f, dummy;
    std::string focus = trim(l.text.substr(0, arrow));
    if (!focus.empty() && focus[0] == '(') {
      std::vector<PElem> left, right;
      if (!pcontext(focus + " _", l.no, left, right) || left.size() != 1) continue;
      f = left;
    } else if (!pelem(focus, l.no, f)) {
      continue;
    }
    if (f[0].kind != PKind::Phoneme && f[0].kind != PKind::Class) { err(l.no, "focus must be a phoneme or class"); continue; }
    r.focus = f[0];
    std::string repl = trim(l.text.substr(arrow + 2, slash - arrow - 2));
    if (repl == "0") r.op = PostOp::Delete;
    else if (repl == "@partner") r.op = PostOp::Partner;
    else {
      r.op = PostOp::Replace;
      for (auto& t : split_ws(repl)) {
        int id = phoneme(t, l.no);
        if (id >= 0) r.repl.push_back(static_cast<uint8_t>(id));
      }
      if (r.repl.empty()) { err(l.no, "empty replacement (use 0 to delete)"); continue; }
    }
    if (!pcontext(l.text.substr(slash + 1), l.no, r.left, r.right)) continue;
    d_.postlex.rules.push_back(r);
  }
}

// ------------------------------------------------------------ syllables.txt
void Compiler::syllables() {
  std::vector<Line> lines;
  if (!read("syllables.txt", lines, false)) return;
  for (auto& l : lines) {
    if (starts_with(l.text, "@nucleus")) {
      auto t = split_ws(l.text);
      int c = t.size() == 2 ? phclass(t[1]) : -1;
      if (c < 0) { err(l.no, "@nucleus CLASS"); continue; }
      d_.syllables.nucleus_class = static_cast<uint16_t>(c);
      continue;
    }
    size_t slash = l.text.find('/');
    if (slash == std::string::npos) { err(l.no, "expected 'phoneme / left _ right'"); continue; }
    SyllRule r;
    int id = phoneme(trim(l.text.substr(0, slash)), l.no);
    if (id < 0) continue;
    r.ph = static_cast<uint8_t>(id);
    if (!pcontext(l.text.substr(slash + 1), l.no, r.left, r.right)) continue;
    d_.syllables.syllabic.push_back(r);
  }
}

// ------------------------------------------------------------ lexicon.txt
void Compiler::lexicon() {
  std::vector<Line> lines;
  if (!read("lexicon.txt", lines, false)) return;
  std::map<u32str, int> seen;
  for (auto& l : lines) {
    auto t = split_ws(l.text);
    if (t.size() < 2) { err(l.no, "expected 'word phonemes...'"); continue; }
    LexEntry e;
    e.key = key(t[0]);
    if (e.key.empty()) { err(l.no, "word has no letters"); continue; }
    if (seen.count(e.key)) { err(l.no, "duplicate entry (first on line " + std::to_string(seen[e.key]) + ")"); continue; }
    seen[e.key] = l.no;
    bool ok = true;
    for (size_t k = 1; k < t.size(); ++k) {
      std::string s = t[k];
      PhTok tok;
      if (!s.empty() && (s.back() == '\\' || s.back() == '/')) {
        tok.flags |= s.back() == '/' ? kAccRising : kAccFalling;
        s.pop_back();
      }
      if (!s.empty() && s.back() == ':' ) { tok.flags |= kLong; s.pop_back(); }
      int id = phoneme(s, l.no);
      if (id < 0) { ok = false; continue; }
      tok.ph = static_cast<uint8_t>(id);
      e.phones.push_back(tok);
    }
    if (ok) d_.lexicon.push_back(e);
  }
  std::sort(d_.lexicon.begin(), d_.lexicon.end(), [](const LexEntry& a, const LexEntry& b) { return a.key < b.key; });
}

// ------------------------------------------------------------ accents.txt
static bool accent_type(const std::string& s, AccentType& t) {
  if (s == "falling") t = AccentType::Falling;
  else if (s == "rising") t = AccentType::Rising;
  else if (s == "none") t = AccentType::None;
  else return false;
  return true;
}

void Compiler::accents() {
  std::vector<Line> lines;
  if (!read("accents.txt", lines, false)) return;
  Accents& A = d_.accents;
  std::set<u32str> seen;
  for (auto& l : lines) {
    auto t = split_ws(l.text);
    if (t[0] == "@default") {
      if (t.size() != 3 || !accent_type(t[2], A.default_type)) { err(l.no, "@default SYLLABLE falling|rising"); continue; }
      A.default_syll = static_cast<int8_t>(std::atoi(t[1].c_str()));
      continue;
    }
    if (t[0] == "@never_last") { A.never_last = true; continue; }
    if (t.size() < 3) { err(l.no, "expected 'word syllable type [long=n,m]'"); continue; }
    AccEntry e;
    e.key = key(t[0]);
    e.syll = static_cast<int8_t>(std::atoi(t[1].c_str()));
    if (e.syll == 0) { err(l.no, "syllable number must be 1.. or -1.."); continue; }
    if (!accent_type(t[2], e.type)) { err(l.no, "type must be falling, rising or none"); continue; }
    for (size_t k = 3; k < t.size(); ++k) {
      if (!starts_with(t[k], "long=")) { err(l.no, "unknown option " + t[k]); continue; }
      for (auto& n : split_on(t[k].substr(5), ','))
        e.long_sylls.push_back(static_cast<uint8_t>(std::atoi(n.c_str())));
    }
    if (!seen.insert(e.key).second) { err(l.no, "duplicate word"); continue; }
    A.entries.push_back(e);
  }
  std::sort(A.entries.begin(), A.entries.end(), [](const AccEntry& a, const AccEntry& b) { return a.key < b.key; });
}

// ------------------------------------------------------------ clitics.txt
void Compiler::clitics() {
  std::vector<Line> lines;
  if (!read("clitics.txt", lines, false)) return;
  int type = 0;
  std::set<u32str> seen;
  for (auto& l : lines) {
    if (l.text == "@proclitic") { type = 1; continue; }
    if (l.text == "@enclitic") { type = 2; continue; }
    if (!type) { err(l.no, "@proclitic or @enclitic expected first"); continue; }
    for (auto& w : split_ws(l.text)) {
      Clitic c;
      c.key = key(w);
      c.type = static_cast<CliticType>(type);
      if (seen.insert(c.key).second) d_.clitics.push_back(c);
    }
  }
  std::sort(d_.clitics.begin(), d_.clitics.end(), [](const Clitic& a, const Clitic& b) { return a.key < b.key; });
}

// ------------------------------------------------------------ numbers.txt
bool Compiler::parse_rbnf_text(const std::string& text, int line, RbnfRule& rule, std::vector<std::string>& refs) {
  std::string buf;
  auto flush = [&] {
    if (!buf.empty()) {
      RbnfPart p;
      p.kind = RbnfPartKind::Text;
      p.text = buf;
      rule.parts.push_back(p);
      buf.clear();
    }
  };
  size_t i = 0;
  if (!text.empty() && text[0] == '\'') i = 1;
  while (i < text.size()) {
    char c = text[i];
    if (c == '<' || c == '>' || c == '=') {
      size_t j = text.find(c, i + 1);
      if (j == std::string::npos) { err(line, std::string("unterminated '") + c + "' substitution"); return false; }
      std::string inner = text.substr(i + 1, j - i - 1);
      flush();
      RbnfPart p;
      p.kind = c == '<' ? RbnfPartKind::Less : c == '>' ? RbnfPartKind::Greater : RbnfPartKind::Equal;
      if (inner.empty()) p.ruleset = -1;
      else if (inner[0] == '%') { p.ruleset = -3 - static_cast<int16_t>(refs.size()); refs.push_back(inner); }
      else if (inner.find_first_not_of("#0,.") == std::string::npos) p.ruleset = -2;
      else { err(line, "bad substitution '" + inner + "'"); return false; }
      if (c == '=' && p.ruleset == -1) { err(line, "'==' needs a ruleset name"); return false; }
      rule.parts.push_back(p);
      i = j + 1;
      if (c == '>' && i < text.size() && text[i] == '>') { warn(line, "'>>>' treated as '>>'"); ++i; }
      continue;
    }
    if (c == '[' || c == ']') {
      flush();
      RbnfPart p;
      p.kind = c == '[' ? RbnfPartKind::OptStart : RbnfPartKind::OptEnd;
      rule.parts.push_back(p);
      ++i;
      continue;
    }
    if (c == '$' && i + 1 < text.size() && text[i + 1] == '(') {
      size_t j = text.find(")$", i);
      if (j == std::string::npos) { err(line, "unterminated $( ... )$"); return false; }
      std::string inner = text.substr(i + 2, j - i - 2);
      size_t comma = inner.find(',');
      if (comma == std::string::npos || trim(inner.substr(0, comma)) != "cardinal") {
        err(line, "only $(cardinal, ...)$ is supported");
        return false;
      }
      flush();
      RbnfPart p;
      p.kind = RbnfPartKind::Plural;
      p.forms.assign(kPluralCount, "");
      std::string rest = inner.substr(comma + 1);
      size_t k = 0;
      while (k < rest.size()) {
        size_t ob = rest.find('{', k);
        if (ob == std::string::npos) break;
        size_t cb = rest.find('}', ob);
        if (cb == std::string::npos) { err(line, "missing '}'"); return false; }
        std::string cat = trim(rest.substr(k, ob - k));
        int ci = plural_from_name(cat);
        if (ci < 0) { err(line, "unknown plural category " + cat); return false; }
        p.forms[ci] = rest.substr(ob + 1, cb - ob - 1);
        k = cb + 1;
      }
      if (p.forms[kOther].empty()) { err(line, "plural needs other{...}"); return false; }
      rule.parts.push_back(p);
      i = j + 2;
      continue;
    }
    buf.push_back(c);
    ++i;
  }
  flush();
  return true;
}

void Compiler::numbers() {
  std::vector<Line> lines;
  if (!read("numbers.txt", lines, true)) return;
  Numbers& N = d_.numbers;
  struct Pending { int line; std::string text; };
  std::vector<std::pair<std::string, std::vector<Pending>>> sets;
  for (auto& l : lines) {
    if (starts_with(l.text, "@plural")) {
      std::string rest = trim(l.text.substr(7));
      size_t colon = rest.find(':');
      int c = colon == std::string::npos ? -1 : plural_from_name(trim(rest.substr(0, colon)));
      if (c < 0) { err(l.no, "@plural CATEGORY: condition"); continue; }
      N.plurals.push_back({static_cast<PluralCat>(c), trim(rest.substr(colon + 1))});
      continue;
    }
    if (starts_with(l.text, "@replace")) {
      auto t = split_ws(l.text);
      if (t.size() != 3) { err(l.no, "@replace FROM TO"); continue; }
      N.replace.push_back({t[1], t[2]});
      continue;
    }
    if (l.text[0] == '%' && l.text.back() == ':' && l.text.find(' ') == std::string::npos) {
      std::string name = l.text.substr(0, l.text.size() - 1);
      while (!name.empty() && name[0] == '%') name.erase(0, 1);
      sets.push_back({name, {}});
      continue;
    }
    if (sets.empty()) { err(l.no, "rule outside of a %ruleset:"); continue; }
    sets.back().second.push_back({l.no, l.text});
  }
  for (auto& s : sets) {
    RbnfRuleset rs;
    rs.name = s.first;
    N.rulesets.push_back(rs);
  }
  PluralRules pr;
  try {
    pr.compile(N.plurals);
  } catch (const std::exception& e) {
    err(0, e.what());
  }
  // rules: text split at ';'
  for (size_t si = 0; si < sets.size(); ++si) {
    RbnfRuleset& rs = N.rulesets[si];
    std::string acc;
    int acc_line = 0;
    auto finish = [&](const std::string& piece, int line) {
      std::string p = trim(piece);
      if (p.empty()) return;
      size_t colon = p.find(':');
      if (colon == std::string::npos) { err(line, "expected 'base: text;'"); return; }
      std::string base = trim(p.substr(0, colon));
      std::string text = p.substr(colon + 1);
      size_t ns = text.find_first_not_of(' ');
      text = ns == std::string::npos ? "" : text.substr(ns);
      RbnfRule r;
      // "base/divisor" gives an explicit divisor (ICU radix syntax), e.g.
      // "2010/100: << >%%2d-year>;" reads 2026 as "20" + "26"
      int64_t explicit_div = 0;
      size_t slash = base.find('/');
      if (slash != std::string::npos) {
        std::string dv = trim(base.substr(slash + 1));
        explicit_div = dv.empty() || dv.find_first_not_of("0123456789") != std::string::npos ? -1 : std::atoll(dv.c_str());
        base = trim(base.substr(0, slash));
        if (explicit_div <= 0) { err(line, "bad divisor in '" + p.substr(0, colon) + "'"); return; }
      }
      if (base == "-x") r.negative = true;
      else {
        std::string digits;
        for (char c : base) if (c != '.' && c != ',' && c != '\'') digits.push_back(c);
        if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos) {
          warn(line, "rule base '" + base + "' not supported, ignored");
          return;
        }
        r.base = std::atoll(digits.c_str());
        r.divisor = 1;
        if (explicit_div > 0) {
          // ICU: divisor = radix^floor(log_radix(base))
          while (r.divisor <= r.base / explicit_div) r.divisor *= explicit_div;
        } else {
          while (r.divisor <= r.base / 10) r.divisor *= 10;
        }
      }
      std::vector<std::string> refs;
      if (!parse_rbnf_text(text, line, r, refs)) return;
      for (auto& part : r.parts) {
        if (part.ruleset <= -3) {
          const std::string& name = refs[-3 - part.ruleset];
          int idx = d_.find_ruleset(name);
          if (idx < 0) { err(line, "unknown ruleset " + name); part.ruleset = -2; }
          else part.ruleset = static_cast<int16_t>(idx);
        }
      }
      rs.rules.push_back(r);
    };
    for (auto& pl : sets[si].second) {
      std::string t = pl.text;
      size_t a = 0;
      if (acc.empty()) acc_line = pl.line;
      for (;;) {
        size_t b = t.find(';', a);
        if (b == std::string::npos) {
          acc += (acc.empty() ? "" : " ") + t.substr(a);
          break;
        }
        acc += (acc.empty() ? "" : " ") + t.substr(a, b - a);
        finish(acc, acc_line);
        acc.clear();
        acc_line = pl.line;
        a = b + 1;
      }
    }
    if (!trim(acc).empty()) err(acc_line, "rule without ';'");
    std::stable_sort(rs.rules.begin(), rs.rules.end(), [](const RbnfRule& a, const RbnfRule& b) {
      if (a.negative != b.negative) return b.negative;  // negative rule last
      return a.base < b.base;
    });
    for (size_t k = 1; k < rs.rules.size(); ++k)
      if (!rs.rules[k].negative && rs.rules[k].base == rs.rules[k - 1].base)
        err(0, "ruleset %" + rs.name + ": two rules for " + std::to_string(rs.rules[k].base));
  }
  // self-reference check for '=' substitutions
  for (size_t si = 0; si < N.rulesets.size(); ++si)
    for (auto& r : N.rulesets[si].rules)
      for (auto& p : r.parts)
        if (p.kind == RbnfPartKind::Equal && p.ruleset == static_cast<int>(si))
          err(0, "ruleset %" + N.rulesets[si].name + ": '=' refers to itself");
  for (const char* k : {"default_cardinal", "digit_ruleset"}) {
    std::string v = d_.meta_value(k);
    if (!v.empty() && d_.find_ruleset(v) < 0) r_.errors.push_back(std::string("language.txt: ") + k + " names an unknown ruleset " + v);
  }
}

// ---------------------------------------------- normalization pattern files
bool Compiler::parse_pattern_elem(const std::string& tok0, int line, NElem& e) {
  std::string tok = tok0;
  if (tok.size() > 1 && tok.back() == '?' ) { e.optional = true; tok.pop_back(); }
  if (tok == "_") { e.kind = NKind::Space; return true; }
  if (tok == "^") { e.kind = NKind::Start; return true; }
  if (tok == "$") { e.kind = NKind::End; return true; }
  if (tok == "word") { e.kind = NKind::Word; return true; }
  if (tok == "lower") { e.kind = NKind::Lower; return true; }
  if (tok == "upper") { e.kind = NKind::Upper; return true; }
  if (tok == "cap") { e.kind = NKind::Cap; return true; }
  if (tok == "roman") { e.kind = NKind::Roman; return true; }
  if (tok == "sym") { e.kind = NKind::Sym; return true; }
  if (tok[0] == '@' && tok.size() > 1) {
    int k = d_.find_list(tok.substr(1));
    if (k < 0) { err(line, "unknown list " + tok); return false; }
    e.kind = NKind::List;
    e.list = static_cast<uint16_t>(k);
    return true;
  }
  std::string t = tok;
  bool zero = false;
  if (starts_with(t, "0num")) { zero = true; t = t.substr(1); }
  if (starts_with(t, "num")) {
    e.kind = NKind::Digits;
    e.zero_lead = zero;
    std::string rest = t.substr(3);
    size_t br = rest.find('[');
    std::string len = br == std::string::npos ? rest : rest.substr(0, br);
    if (!len.empty()) {
      if (len.back() == '+') { e.min_len = static_cast<uint8_t>(std::atoi(len.c_str())); e.max_len = 255; }
      else if (len.find('-') != std::string::npos) {
        e.min_len = static_cast<uint8_t>(std::atoi(len.c_str()));
        e.max_len = static_cast<uint8_t>(std::atoi(len.c_str() + len.find('-') + 1));
      } else e.min_len = e.max_len = static_cast<uint8_t>(std::atoi(len.c_str()));
      if (e.min_len == 0 || e.max_len < e.min_len) { err(line, "bad digit count in " + tok); return false; }
    }
    if (br != std::string::npos) {
      size_t cb = rest.find(']', br);
      if (cb == std::string::npos) { err(line, "missing ']'"); return false; }
      std::string rg = rest.substr(br + 1, cb - br - 1);
      size_t dash = rg.find('-');
      e.min_val = std::atoll(rg.c_str());
      e.max_val = dash == std::string::npos ? e.min_val : std::atoll(rg.c_str() + dash + 1);
      e.no_zero_lead = false;
    }
    return true;
  }
  u32str c = parse_chars(tok);
  if (c.size() == 1 && !is_digit(c[0]) && !canon_->is_word_char(c[0]) && !is_space(c[0])) {
    e.kind = NKind::Lit;
    e.ch = c[0];
    return true;
  }
  err(line, "unknown pattern element '" + tok0 + "'");
  return false;
}

void Compiler::normalize_file(const std::string& name) {
  std::vector<Line> lines;
  if (!read(name, lines, false)) return;
  for (auto& l : lines) {
    if (starts_with(l.text, "@list")) {
      size_t eq = l.text.find('=');
      auto head = split_ws(l.text.substr(0, eq == std::string::npos ? l.text.size() : eq));
      if (eq == std::string::npos || head.size() != 2) { err(l.no, "@list name = words..."); continue; }
      if (d_.find_list(head[1]) >= 0) { err(l.no, "duplicate list"); continue; }
      WordList w;
      w.name = head[1];
      for (auto& x : split_ws(l.text.substr(eq + 1))) w.words.push_back(key(x));
      std::sort(w.words.begin(), w.words.end());
      w.words.erase(std::unique(w.words.begin(), w.words.end()), w.words.end());
      d_.lists.push_back(w);
      continue;
    }
    if (starts_with(l.text, "@map")) {
      size_t eq = l.text.find('=');
      auto head = split_ws(l.text.substr(0, eq == std::string::npos ? l.text.size() : eq));
      if (eq == std::string::npos || head.size() != 2) { err(l.no, "@map name = n:text ..."); continue; }
      if (d_.find_map(head[1]) >= 0) { err(l.no, "duplicate map"); continue; }
      NumMap m;
      m.name = head[1];
      for (auto& x : split_ws(l.text.substr(eq + 1))) {
        size_t c = x.find(':');
        if (c == std::string::npos) { err(l.no, "map items are n:text"); continue; }
        std::string txt = x.substr(c + 1);
        std::replace(txt.begin(), txt.end(), '_', ' ');
        m.items.push_back({std::atoll(x.c_str()), txt});
      }
      std::sort(m.items.begin(), m.items.end());
      d_.maps.push_back(m);
      continue;
    }
    size_t arrow = l.text.find("=>");
    if (arrow == std::string::npos) { err(l.no, "expected 'pattern => output'"); continue; }
    NRule r;
    r.where = name + ":" + std::to_string(l.no);
    std::string pat = l.text.substr(0, arrow);
    std::string out = trim(l.text.substr(arrow + 2));
    // pattern: left ( core ) right
    std::vector<std::string> toks = split_ws(pat);
    int part = 0;  // 0 left/core (until '('), 1 core, 2 right
    bool has_paren = std::find(toks.begin(), toks.end(), "(") != toks.end();
    if (!has_paren) part = 1;
    bool ok = true;
    for (auto& t : toks) {
      if (t == "(") { if (part != 0) ok = false; part = 1; continue; }
      if (t == ")") { if (part != 1) ok = false; part = 2; continue; }
      NElem e;
      if (!parse_pattern_elem(t, l.no, e)) { ok = false; break; }
      (part == 0 ? r.left : part == 1 ? r.core : r.right).push_back(e);
    }
    if (!ok) { if (r.core.empty()) err(l.no, "bad pattern"); continue; }
    if (r.core.empty()) { err(l.no, "empty pattern"); continue; }
    int ncaps = 0;
    for (auto& e : r.core)
      if (e.kind == NKind::Digits || e.kind == NKind::List || e.kind == NKind::Word || e.kind == NKind::Lower ||
          e.kind == NKind::Upper || e.kind == NKind::Cap || e.kind == NKind::Roman)
        ++ncaps;
    if (starts_with(out, "!nobreak")) { r.sentence_end_check = false; out = trim(out.substr(8)); }
    for (auto& t : split_ws(out)) {
      NOut o;
      if (t[0] != '$') {
        o.fmt = NFmt::Text;
        o.text = t;
        r.out.push_back(o);
        continue;
      }
      std::string rest = t.substr(1);
      size_t colon = rest.find(':');
      o.cap = static_cast<uint8_t>(std::atoi(rest.substr(0, colon).c_str()));
      if (o.cap == 0 || o.cap > ncaps) { err(l.no, "no capture " + t); ok = false; continue; }
      if (colon == std::string::npos) { o.fmt = NFmt::Default; r.out.push_back(o); continue; }
      std::string f = rest.substr(colon + 1);
      if (f[0] == '%') {
        int k = d_.find_ruleset(f);
        if (k < 0) { err(l.no, "unknown ruleset " + f); ok = false; continue; }
        o.fmt = NFmt::Ruleset;
        o.idx = static_cast<uint16_t>(k);
      } else if (f[0] == '@') {
        int k = d_.find_map(f.substr(1));
        if (k < 0) { err(l.no, "unknown map " + f); ok = false; continue; }
        o.fmt = NFmt::Map;
        o.idx = static_cast<uint16_t>(k);
      } else if (f == "digits") {
        o.fmt = NFmt::Digits;
      } else if (starts_with(f, "plural(") && f.back() == ')') {
        o.fmt = NFmt::Plural;
        o.forms.assign(kPluralCount, "");
        for (auto& kv : split_on(f.substr(7, f.size() - 8), ',')) {
          size_t eq = kv.find('=');
          int ci = eq == std::string::npos ? -1 : plural_from_name(kv.substr(0, eq));
          if (ci < 0) { err(l.no, "plural(one=..,few=..,other=..)"); ok = false; continue; }
          std::string txt = kv.substr(eq + 1);
          std::replace(txt.begin(), txt.end(), '_', ' ');
          o.forms[ci] = txt;
        }
        if (o.forms[kOther].empty()) { err(l.no, "plural needs other="); ok = false; }
      } else {
        err(l.no, "unknown format " + f);
        ok = false;
        continue;
      }
      r.out.push_back(o);
    }
    if (ok) d_.nrules.push_back(r);
  }
}

// ------------------------------------------------------------ units.txt
void Compiler::units() {
  std::vector<Line> lines;
  if (!read("units.txt", lines, false)) return;
  std::map<std::string, int> genders;
  std::vector<int> order = {kOne, kFew, kOther};
  for (auto& l : lines) {
    if (starts_with(l.text, "@gender")) {
      auto t = split_ws(l.text);
      int k = t.size() == 3 ? d_.find_ruleset(t[2]) : -1;
      if (k < 0) { err(l.no, "@gender NAME %ruleset"); continue; }
      genders[t[1]] = k;
      continue;
    }
    if (starts_with(l.text, "@forms")) {
      order.clear();
      auto t = split_ws(l.text);
      for (size_t k = 1; k < t.size(); ++k) {
        int c = plural_from_name(t[k]);
        if (c < 0) err(l.no, "unknown plural category " + t[k]);
        else order.push_back(c);
      }
      continue;
    }
    auto f = split_on(l.text, '|');
    if (f.size() < 2 + order.size()) { err(l.no, "expected 'keys | gender | forms... [| flags]'"); continue; }
    Unit u;
    auto flags = f.size() > 2 + order.size() ? split_ws(f[2 + order.size()]) : std::vector<std::string>();
    for (auto& fl : flags) {
      if (fl == "prefix") u.prefix = true;
      else if (fl == "cs") u.case_sensitive = true;
      else err(l.no, "unknown flag " + fl);
    }
    for (auto& k : split_ws(f[0])) {
      if (u.case_sensitive) u.raw.push_back(parse_chars(k));
      else u.keys.push_back(key(u32_to_utf8(parse_chars(k))));
    }
    if (f[1] == "-") u.ruleset = -1;
    else if (genders.count(f[1])) u.ruleset = static_cast<int16_t>(genders[f[1]]);
    else { err(l.no, "unknown gender " + f[1]); continue; }
    u.forms.assign(kPluralCount, "");
    for (size_t k = 0; k < order.size(); ++k) u.forms[order[k]] = f[2 + k];
    if (u.forms[kOther].empty()) { err(l.no, "missing 'other' form"); continue; }
    d_.units.push_back(u);
  }
}

// ------------------------------------------------------------ abbreviations.txt
void Compiler::abbreviations() {
  std::vector<Line> lines;
  if (!read("abbreviations.txt", lines, false)) return;
  std::set<u32str> seen;
  for (auto& l : lines) {
    auto f = split_on(l.text, '|');
    if (f.size() < 2 || f[0].empty() || f[1].empty()) { err(l.no, "expected 'abbreviation | expansion [| flags]'"); continue; }
    Abbrev a;
    a.expansion = f[1];
    if (f.size() > 2)
      for (auto& fl : split_ws(f[2])) {
        if (fl == "cs") a.case_sensitive = true;
        else if (fl == "end") a.may_end = true;
        else err(l.no, "unknown flag " + fl);
      }
    u32str raw = utf8_to_u32(f[0]);
    a.key = a.case_sensitive ? raw : canon_->lexkey(canon_->text(raw));
    if (!seen.insert(a.key + (a.case_sensitive ? U"\x01" : U"")).second) { err(l.no, "duplicate abbreviation"); continue; }
    d_.abbrevs.push_back(a);
  }
}

// ------------------------------------------------------------ symbols.txt
static bool break_type(const std::string& s, BreakType& b) {
  static const std::pair<const char*, BreakType> m[] = {
      {"comma", BreakType::Comma},   {"semicolon", BreakType::Semicolon}, {"colon", BreakType::Colon},
      {"dash", BreakType::Dash},     {"period", BreakType::Period},       {"question", BreakType::Question},
      {"exclaim", BreakType::Exclaim}, {"paragraph", BreakType::Paragraph}, {"none", BreakType::None}};
  for (auto& p : m)
    if (s == p.first) { b = p.second; return true; }
  return false;
}

void Compiler::symbols() {
  std::vector<Line> lines;
  if (!read("symbols.txt", lines, false)) return;
  std::set<char32_t> seen;
  for (auto& l : lines) {
    auto f = split_on(l.text, '|');
    if (f.size() < 3) { err(l.no, "expected 'char | level | name [| break]'"); continue; }
    u32str c = parse_chars(f[0]);
    if (c.size() != 1) { err(l.no, "one character expected"); continue; }
    Symbol s;
    s.ch = c[0];
    static const char* const lv[] = {"none", "some", "most", "all", "never"};
    int level = -1;
    for (int k = 0; k < 5; ++k) if (f[1] == lv[k]) level = k;
    if (level < 0) { err(l.no, "level must be none, some, most, all or never"); continue; }
    s.level = static_cast<uint8_t>(level);
    s.name = f[2];
    if (f.size() > 3 && !break_type(f[3], s.brk)) { err(l.no, "unknown break " + f[3]); continue; }
    if (!seen.insert(s.ch).second) { err(l.no, "duplicate symbol"); continue; }
    d_.symbols.push_back(s);
  }
  std::sort(d_.symbols.begin(), d_.symbols.end(), [](const Symbol& a, const Symbol& b) { return a.ch < b.ch; });
}

// ------------------------------------------------------------ emoji.txt
void Compiler::emoji() {
  std::vector<Line> lines;
  if (!read("emoji.txt", lines, false, false)) return;
  std::map<u32str, int> seen;
  for (auto& l : lines) {
    size_t tab = l.text.find('\t');
    if (tab == std::string::npos) { err(l.no, "expected '<emoji><TAB><name>'"); continue; }
    u32str seq;
    for (char32_t c : utf8_to_u32(trim(l.text.substr(0, tab))))
      if (c != 0xFE0F) seq.push_back(c);
    std::string name = trim(l.text.substr(tab + 1));
    if (seq.empty() || name.empty()) { err(l.no, "empty emoji or name"); continue; }
    if (seen.count(seq)) { warn(l.no, "emoji repeated (first on line " + std::to_string(seen[seq]) + "), later entry ignored"); continue; }
    seen[seq] = l.no;
    d_.emoji.push_back({seq, name});
  }
  std::sort(d_.emoji.begin(), d_.emoji.end(), [](const EmojiEntry& a, const EmojiEntry& b) { return a.seq < b.seq; });
}

// ------------------------------------------------------------ spelling.txt
void Compiler::spelling() {
  std::vector<Line> lines;
  if (!read("spelling.txt", lines, false)) return;
  std::set<char32_t> seen;
  for (auto& l : lines) {
    if (starts_with(l.text, "@capital")) { d_.spell_capital = trim(l.text.substr(8)); continue; }
    if (starts_with(l.text, "@unknown")) {
      d_.spelling.push_back({0, trim(l.text.substr(8))});
      continue;
    }
    auto f = split_on(l.text, '|');
    if (f.size() != 2 || f[1].empty()) { err(l.no, "expected 'characters | name'"); continue; }
    for (auto& tok : split_ws(f[0])) {
      u32str c = parse_chars(tok);
      if (c.size() != 1) { err(l.no, "one character expected: " + tok); continue; }
      char32_t lc = canon_->lower(c[0]);
      if (!seen.insert(lc).second) { err(l.no, "character listed twice: " + tok); continue; }
      d_.spelling.push_back({lc, f[1]});
    }
  }
  std::sort(d_.spelling.begin(), d_.spelling.end(), [](const SpellEntry& a, const SpellEntry& b) { return a.ch < b.ch; });
}

// ------------------------------------------------------------ acronyms.txt
void Compiler::acronyms() {
  std::vector<Line> lines;
  if (!read("acronyms.txt", lines, false)) return;
  Acronyms& A = d_.acronyms;
  for (auto& l : lines) {
    auto t = split_ws(l.text);
    if (t[0] == "@words") for (size_t k = 1; k < t.size(); ++k) A.words.push_back(key(t[k]));
    else if (t[0] == "@spell") for (size_t k = 1; k < t.size(); ++k) A.spell.push_back(key(t[k]));
    else if (t[0] == "@spell_max_len" && t.size() == 2) A.spell_max_len = std::atoi(t[1].c_str());
    else err(l.no, "expected @words, @spell or @spell_max_len");
  }
  std::sort(A.words.begin(), A.words.end());
  std::sort(A.spell.begin(), A.spell.end());
}

// ------------------------------------------------------------ prosody.txt
void Compiler::prosody() {
  std::vector<Line> lines;
  if (!read("prosody.txt", lines, false)) return;
  for (auto& l : lines) {
    size_t eq = l.text.find('=');
    if (eq == std::string::npos) { err(l.no, "expected 'key = number'"); continue; }
    std::string v = trim(l.text.substr(eq + 1));
    char* end = nullptr;
    double x = std::strtod(v.c_str(), &end);
    if (!end || *end) { err(l.no, "not a number: " + v); continue; }
    d_.prosody[trim(l.text.substr(0, eq))] = x;
  }
}

// ------------------------------------------------------------ tests.txt
void Compiler::tests() {
  std::vector<Line> lines;
  if (!read("tests.txt", lines, false)) return;
  for (auto& l : lines) {
    size_t arrow = l.text.rfind("=>");
    if (arrow == std::string::npos) { err(l.no, "expected '\"input\" [| \"input\"] => expected'"); continue; }
    TestCase tc;
    tc.where = "tests.txt:" + std::to_string(l.no);
    std::string in = l.text.substr(0, arrow);
    size_t a = 0;
    while ((a = in.find('"', a)) != std::string::npos) {
      size_t b = in.find('"', a + 1);
      if (b == std::string::npos) { err(l.no, "unterminated quote"); break; }
      tc.inputs.push_back(in.substr(a + 1, b - a - 1));
      a = b + 1;
    }
    std::string ex = trim(l.text.substr(arrow + 2));
    if (ex.size() >= 2 && ex.front() == '/' && ex.back() == '/') {
      tc.phonemes = true;
      ex = trim(ex.substr(1, ex.size() - 2));
    }
    tc.expected = ex;
    if (tc.inputs.empty()) { err(l.no, "no input"); continue; }
    r_.tests.push_back(tc);
  }
}

void Compiler::run() {
  language();
  phonemes();
  classes();
  scripts();
  if (!canon_) return;
  g2p();
  postlex();
  syllables();
  lexicon();
  accents();
  clitics();
  numbers();
  std::string files = d_.meta_value("normalize_files", "dates.txt numcontext.txt");
  for (auto& f : split_ws(files)) normalize_file(f);
  units();
  abbreviations();
  symbols();
  emoji();
  spelling();
  acronyms();
  prosody();
  tests();
}

}  // namespace

bool compile_language(const std::string& dir, CompileResult& r) {
  r = CompileResult();
  try {
    Compiler c(dir, r);
    c.run();
  } catch (const std::exception& e) {
    r.errors.push_back(std::string("internal compiler error: ") + e.what());
  }
  return r.errors.empty();
}

std::shared_ptr<const Language> load_source_language(const std::string& dir, std::string& error) {
  CompileResult r;
  if (!compile_language(dir, r)) {
    error = r.errors.front();
    return nullptr;
  }
  try {
    return std::make_shared<Language>(std::move(r.data));
  } catch (const std::exception& e) {
    error = e.what();
    return nullptr;
  }
}

}  // namespace mbng
