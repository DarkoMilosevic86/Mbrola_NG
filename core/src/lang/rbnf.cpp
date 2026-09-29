// MBROLA NG - RBNF and plural rules
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lang/rbnf.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "util/bytes.h"

namespace mbng {

static const char* const kPluralNames[kPluralCount] = {"zero", "one", "two", "few", "many", "other"};

std::string plural_name(PluralCat c) { return c < kPluralCount ? kPluralNames[c] : "other"; }

int plural_from_name(const std::string& s) {
  for (int i = 0; i < kPluralCount; ++i)
    if (s == kPluralNames[i]) return i;
  return -1;
}

PluralOperands PluralOperands::from_int(int64_t n) {
  PluralOperands o;
  o.i = n < 0 ? -n : n;
  return o;
}

PluralOperands PluralOperands::from_string(const std::string& s) {
  PluralOperands o;
  size_t k = 0;
  while (k < s.size() && (s[k] == '-' || s[k] == '+')) ++k;
  for (; k < s.size() && isdigit(static_cast<unsigned char>(s[k])); ++k)
    if (o.i < 100000000000000000LL) o.i = o.i * 10 + (s[k] - '0');
  if (k < s.size() && (s[k] == ',' || s[k] == '.')) {
    ++k;
    std::string frac;
    for (; k < s.size() && isdigit(static_cast<unsigned char>(s[k])); ++k) frac.push_back(s[k]);
    if (frac.size() > 17) frac.resize(17);
    o.v = static_cast<int>(frac.size());
    for (char c : frac) o.f = o.f * 10 + (c - '0');
    std::string tt = frac;
    while (!tt.empty() && tt.back() == '0') tt.pop_back();
    for (char c : tt) o.t = o.t * 10 + (c - '0');
  }
  return o;
}

// ------------------------------------------------------------ plural parsing
namespace {

struct Lexer {
  const std::string& s;
  size_t p = 0;
  explicit Lexer(const std::string& str) : s(str) {}
  void ws() { while (p < s.size() && isspace(static_cast<unsigned char>(s[p]))) ++p; }
  bool eat(const char* t) {
    ws();
    size_t n = strlen(t);
    if (s.compare(p, n, t) == 0) {
      // keywords must not be followed by letters
      if (isalpha(static_cast<unsigned char>(t[0])) && p + n < s.size() && isalpha(static_cast<unsigned char>(s[p + n])))
        return false;
      p += n;
      return true;
    }
    return false;
  }
  bool end() { ws(); return p >= s.size(); }
  int64_t number() {
    ws();
    if (p >= s.size() || !isdigit(static_cast<unsigned char>(s[p]))) throw FormatError("plural rule: number expected");
    int64_t v = 0;
    while (p < s.size() && isdigit(static_cast<unsigned char>(s[p]))) v = v * 10 + (s[p++] - '0');
    return v;
  }
};

}  // namespace

void PluralRules::compile(const std::vector<PluralRule>& rules) {
  rules_.clear();
  for (const PluralRule& pr : rules) {
    std::string expr = pr.expr;
    size_t at = expr.find('@');  // strip CLDR samples
    if (at != std::string::npos) expr.resize(at);
    Rule rule;
    rule.cat = pr.cat;
    Lexer lx(expr);
    if (!lx.end()) {
      for (;;) {
        And conj;
        for (;;) {
          Relation rel;
          lx.ws();
          if (lx.p >= expr.size()) throw FormatError("plural rule: operand expected");
          char op = expr[lx.p++];
          if (std::string("nivfte").find(op) == std::string::npos) throw FormatError("plural rule: unknown operand");
          rel.operand = op;
          if (lx.eat("%") || lx.eat("mod")) rel.mod = lx.number();
          if (lx.eat("!=")) rel.negate = true;
          else if (lx.eat("=")) rel.negate = false;
          else if (lx.eat("is not") || lx.eat("not in")) rel.negate = true;
          else if (lx.eat("is") || lx.eat("in")) rel.negate = false;
          else throw FormatError("plural rule: '=' or '!=' expected");
          do {
            int64_t a = lx.number(), b = a;
            if (lx.eat("..")) b = lx.number();
            rel.ranges.push_back({a, b});
          } while (lx.eat(","));
          conj.push_back(rel);
          if (!lx.eat("and")) break;
        }
        rule.ors.push_back(conj);
        if (!lx.eat("or")) break;
      }
      if (!lx.end()) throw FormatError("plural rule: unexpected text '" + expr.substr(lx.p) + "'");
    }
    rules_.push_back(rule);
  }
}

PluralCat PluralRules::select(const PluralOperands& o) const {
  for (const Rule& r : rules_) {
    if (r.ors.empty()) return r.cat;
    for (const And& conj : r.ors) {
      bool all = true;
      for (const Relation& rel : conj) {
        int64_t v = 0;
        bool integral = true;
        switch (rel.operand) {
          case 'n': v = o.i; integral = (o.v == 0 || o.f == 0); break;
          case 'i': v = o.i; break;
          case 'v': v = o.v; break;
          case 'f': v = o.f; break;
          case 't': v = o.t; break;
          default: v = 0; break;
        }
        if (rel.mod) v %= rel.mod;
        bool in = false;
        if (integral)
          for (auto& rg : rel.ranges)
            if (v >= rg.first && v <= rg.second) { in = true; break; }
        if (in == rel.negate) { all = false; break; }
      }
      if (all) return r.cat;
    }
  }
  return kOther;
}

// ------------------------------------------------------------------ RBNF
std::string Rbnf::format(int64_t n, int ruleset) const {
  std::string out;
  apply(n, ruleset, 0, out);
  for (auto& rp : num_.replace) {
    // "from$" matches only at the end of a word
    bool at_end = rp.first.size() > 1 && rp.first.back() == '$';
    std::string from = at_end ? rp.first.substr(0, rp.first.size() - 1) : rp.first;
    size_t pos = 0;
    while (!from.empty() && (pos = out.find(from, pos)) != std::string::npos) {
      size_t after = pos + from.size();
      if (at_end && after < out.size() && out[after] != ' ') { pos = after; continue; }
      out.replace(pos, from.size(), rp.second);
      pos += rp.second.size();
    }
  }
  // collapse spaces
  std::string clean;
  for (char c : out) {
    if (c == ' ' && (clean.empty() || clean.back() == ' ')) continue;
    clean.push_back(c);
  }
  while (!clean.empty() && clean.back() == ' ') clean.pop_back();
  return clean;
}

void Rbnf::apply(int64_t n, int ruleset, int depth, std::string& out) const {
  if (depth > 32) throw FormatError("number rules recurse too deeply");
  if (ruleset == -2 || ruleset < 0 || ruleset >= static_cast<int>(num_.rulesets.size())) {
    out += std::to_string(n);
    return;
  }
  const RbnfRuleset& rs = num_.rulesets[ruleset];
  const RbnfRule* rule = nullptr;
  if (n < 0) {
    for (const RbnfRule& r : rs.rules)
      if (r.negative) { rule = &r; break; }
    if (!rule) {
      out += "-";
      apply(-n, ruleset, depth + 1, out);
      return;
    }
  } else {
    for (const RbnfRule& r : rs.rules) {
      if (r.negative) continue;
      if (r.base <= n) rule = &r;
      else break;
    }
  }
  if (!rule) {
    out += std::to_string(n);
    return;
  }
  const int64_t absn = n < 0 ? -n : n;
  const int64_t q = rule->negative ? absn : absn / rule->divisor;
  const int64_t r = rule->negative ? absn : absn % rule->divisor;
  bool skipping = false;
  for (const RbnfPart& p : rule->parts) {
    switch (p.kind) {
      case RbnfPartKind::OptStart:
        skipping = !rule->negative && r == 0;
        break;
      case RbnfPartKind::OptEnd:
        skipping = false;
        break;
      default:
        if (skipping) break;
        switch (p.kind) {
          case RbnfPartKind::Text: out += p.text; break;
          case RbnfPartKind::Less: apply(q, p.ruleset == -1 ? ruleset : p.ruleset, depth + 1, out); break;
          case RbnfPartKind::Greater: apply(r, p.ruleset == -1 ? ruleset : p.ruleset, depth + 1, out); break;
          case RbnfPartKind::Equal:
            if (p.ruleset == -1 || p.ruleset == ruleset) throw FormatError("'==' must name another ruleset");
            apply(absn, p.ruleset, depth + 1, out);
            break;
          case RbnfPartKind::Plural: {
            PluralCat c = plural_.select(q);
            const std::string& f = p.forms[c].empty() ? p.forms[kOther] : p.forms[c];
            out += f;
            break;
          }
          default: break;
        }
    }
  }
}

}  // namespace mbng
