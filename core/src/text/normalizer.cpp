// MBROLA NG - text normalization
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "text/normalizer.h"

#include <algorithm>
#include <cstdlib>

#include "phon/phonology.h"

namespace mbng {

namespace {
constexpr int kMaxDepth = 4;

int64_t roman_value(const u32str& s) {
  if (s.empty() || s.size() > 15) return -1;
  auto v = [](char32_t c) -> int {
    switch (c) {
      case 'i': return 1; case 'v': return 5; case 'x': return 10; case 'l': return 50;
      case 'c': return 100; case 'd': return 500; case 'm': return 1000; default: return 0;
    }
  };
  int64_t total = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    int a = v(s[i]);
    if (!a) return -1;
    int b = i + 1 < s.size() ? v(s[i + 1]) : 0;
    total += a < b ? -a : a;
  }
  if (total <= 0 || total >= 4000) return -1;
  // canonical form check
  static const char* const r[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
  static const int n[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
  std::u32string canon;
  int64_t t = total;
  for (int k = 0; k < 13; ++k)
    while (t >= n[k]) { for (const char* p = r[k]; *p; ++p) canon.push_back(static_cast<char32_t>(*p)); t -= n[k]; }
  return canon == s ? total : -1;
}
}  // namespace

int break_priority(BreakType b) {
  switch (b) {
    case BreakType::None: return 0;
    case BreakType::Comma: case BreakType::Dash: return 1;
    case BreakType::Colon: case BreakType::Semicolon: return 2;
    case BreakType::Period: return 3;
    case BreakType::Paragraph: return 4;
    case BreakType::Exclaim: return 5;
    case BreakType::Question: return 6;
  }
  return 0;
}

enum class TK : uint8_t { Word, Num, Space, Sym, Emoji };

struct Normalizer::Tok {
  TK k = TK::Sym;
  uint32_t b = 0, e = 0;  // range in the cleaned text
  u32str canon;
  uint8_t caps = 0;
  int script = 0;
  int64_t val = -1;
  int nd = 0;
  bool zlead = false;
  int emoji = -1;
  int nl = 0;
};

struct Normalizer::Ctx {
  u32str t;
  std::vector<uint32_t> hb;  // host offsets, t.size()+1
  std::vector<Tok> tk;
  NormOptions opt;
  std::vector<Item>* out = nullptr;
  int depth = 0;
  bool pinned = false;
  uint32_t pin_b = 0, pin_e = 0;
  uint32_t host_b(uint32_t tpos) const { return pinned ? pin_b : hb[tpos]; }
  uint32_t host_e(uint32_t tpos) const { return pinned ? pin_e : hb[tpos]; }
};

Normalizer::Normalizer(const Language& lang, const Phonology& ph) : L(lang), P(ph) {
  const LanguageData& d = L.d();
  u32str ds = utf8_to_u32(d.meta_value("decimal_separator", ","));
  if (!ds.empty()) decimal_sep_ = ds[0];
  decimal_word_ = d.meta_value("decimal_word");
  decimal_max_ = std::atoi(d.meta_value("decimal_max_digits", "2").c_str());
  max_digits_ = std::atoi(d.meta_value("max_number_digits", "15").c_str());
  if (max_digits_ < 1 || max_digits_ > 18) max_digits_ = 15;
  zero_lead_digits_ = d.meta_value("leading_zero_digits", "1") != "0";
  for (auto& s : split_ws(d.meta_value("thousands_separators"))) {
    if (s == "space") sep_space_ = true;
    else sep_chars_ += utf8_to_u32(s).substr(0, 1);
  }
  emoji_repeat_min_ = std::atoi(d.meta_value("emoji_repeat_min", "3").c_str());
  emoji_repeat_ = d.meta_value("emoji_repeat");
}

void Normalizer::run(const u32str& text, const std::vector<uint32_t>& offs, const NormOptions& opt,
                     std::vector<Item>& out) const {
  Ctx c;
  std::vector<uint32_t> map;
  c.t = L.canon().cleanup(text, map);
  c.hb.resize(c.t.size() + 1);
  for (size_t i = 0; i < c.t.size(); ++i) c.hb[i] = offs[map[i]];
  c.hb[c.t.size()] = offs[text.size()];
  c.opt = opt;
  c.out = &out;
  // character echo: a lone character is spelled
  if (opt.auto_spell_single) {
    size_t n = 0;
    for (char32_t ch : c.t)
      if (!is_space(ch) && ch != 0xFE0F) ++n;
    if (n == 1) {
      bool emoji_char = false;
      for (size_t k = 0; k < c.t.size(); ++k) {
        size_t len;
        if (L.match_emoji(c.t, k, len) >= 0) emoji_char = true;
      }
      if (!emoji_char) c.opt.spell = true;
    }
  }
  run_ctx(c);
}

void Normalizer::run_ctx(Ctx& c) const {
  tokenize(c);
  merge_thousands(c);
  size_t i = 0;
  const size_t n = c.tk.size();
  while (i < n) {
    const Tok& t = c.tk[i];
    if (c.opt.spell) {
      if (t.k == TK::Space) {
        if (t.nl) emit_break(c, BreakType::Period, t.b, t.e);
      } else if (t.k == TK::Emoji && c.opt.emoji) {
        emit_text(c, L.d().emoji[t.emoji].name, t.b, t.e);
      } else {
        spell_range(c, t.b, t.e, true);
      }
      ++i;
      continue;
    }
    if (t.k != TK::Space) {
      size_t used = try_rules(c, i);
      if (used) { i += used; continue; }
    }
    switch (t.k) {
      case TK::Space:
        if (t.nl >= 2) emit_break(c, BreakType::Paragraph, t.b, t.e);
        else if (t.nl == 1) emit_break(c, BreakType::Period, t.b, t.e);
        ++i;
        break;
      case TK::Num:
        i += number(c, i, false, i);
        break;
      case TK::Word: {
        size_t used = try_abbrev(c, i);
        if (!used) used = try_prefix_unit(c, i);
        if (used) { i += used; break; }
        word(c, i);
        ++i;
        break;
      }
      case TK::Sym: {
        char32_t ch = c.t[t.b];
        if (ch == '-' && i + 1 < n && c.tk[i + 1].k == TK::Num &&
            (i == 0 || c.tk[i - 1].k == TK::Space || (c.tk[i - 1].k == TK::Sym && c.t[c.tk[i - 1].b] == '('))) {
          i += 1 + number(c, i + 1, true, i);
          break;
        }
        size_t used = try_prefix_unit(c, i);
        if (used) { i += used; break; }
        symbol(c, i);
        ++i;
        break;
      }
      case TK::Emoji:
        i += emoji(c, i);
        break;
    }
  }
}

// ------------------------------------------------------------ tokenizer
void Normalizer::tokenize(Ctx& c) const {
  const u32str& t = c.t;
  const Canonicalizer& cz = L.canon();
  size_t i = 0;
  while (i < t.size()) {
    char32_t ch = t[i];
    Tok k;
    k.b = static_cast<uint32_t>(i);
    size_t len = 0;
    int em = L.may_start_emoji(ch) ? L.match_emoji(t, i, len) : -1;
    if (em >= 0) {
      k.k = TK::Emoji;
      k.emoji = em;
      i += len;
    } else if (is_space(ch)) {
      k.k = TK::Space;
      while (i < t.size() && is_space(t[i])) {
        if (is_newline(t[i]) || (t[i] == '\r' && (i + 1 >= t.size() || t[i + 1] != '\n'))) ++k.nl;
        ++i;
      }
    } else if (is_digit(ch)) {
      k.k = TK::Num;
      while (i < t.size() && is_digit(t[i])) k.canon.push_back(t[i++]);
      k.nd = static_cast<int>(k.canon.size());
      k.zlead = k.canon[0] == '0';
      if (k.nd <= 18) {
        k.val = 0;
        for (char32_t d : k.canon) k.val = k.val * 10 + (d - '0');
      }
    } else if (cz.is_word_char(ch) && !is_combining(ch)) {
      k.k = TK::Word;
      size_t j = i;
      while (j < t.size() && cz.is_word_char(t[j]) && !(L.may_start_emoji(t[j]) && [&] {
               size_t l2;
               return L.match_emoji(t, j, l2) >= 0;
             }()))
        ++j;
      k.canon = cz.word(t.data() + i, j - i, &k.caps, &k.script);
      i = j;
    } else if (is_combining(ch) || ch == 0xFE0F || ch == 0x200D) {
      ++i;  // stray combining mark / variation selector: ignored
      continue;
    } else {
      k.k = TK::Sym;
      ++i;
    }
    k.e = static_cast<uint32_t>(i);
    c.tk.push_back(std::move(k));
  }
}

void Normalizer::merge_thousands(Ctx& c) const {
  if (sep_chars_.empty() && !sep_space_) return;
  std::vector<Tok>& tk = c.tk;
  std::vector<Tok> out;
  for (size_t i = 0; i < tk.size(); ++i) {
    Tok t = tk[i];
    if (t.k == TK::Num && t.nd <= 3 && !(t.zlead && t.nd > 1)) {
      size_t j = i;
      char32_t sep = 0;
      while (j + 2 < tk.size() && tk[j + 2].k == TK::Num && tk[j + 2].nd == 3) {
        const Tok& s = tk[j + 1];
        char32_t sc = 0;
        if (s.k == TK::Sym && sep_chars_.find(c.t[s.b]) != u32str::npos) sc = c.t[s.b];
        if (sep_space_ && s.k == TK::Space && s.e - s.b == 1 && !s.nl) sc = ' ';
        if (!sc || (sep && sc != sep)) break;
        sep = sc;
        j += 2;
      }
      // "1.000." at a sentence end is fine; "1.000.5" stops before ".5"
      if (j > i && t.nd + (j - i) / 2 * 3 <= 18) {
        for (size_t k = i + 2; k <= j; k += 2) t.canon += tk[k].canon;
        t.nd = static_cast<int>(t.canon.size());
        t.val = 0;
        for (char32_t d : t.canon) t.val = t.val * 10 + (d - '0');
        t.e = tk[j].e;
        out.push_back(t);
        i = j;
        continue;
      }
    }
    out.push_back(t);
  }
  tk.swap(out);
}

// --------------------------------------------------------------- patterns
bool Normalizer::elem_one(const Ctx& c, const NElem& e, const Tok& t) const {
  switch (e.kind) {
    case NKind::Digits:
      if (t.k != TK::Num) return false;
      if (t.nd < e.min_len || t.nd > e.max_len) return false;
      if (e.zero_lead && !t.zlead) return false;
      if (e.no_zero_lead && t.zlead && t.nd > 1) return false;
      if (e.min_val != INT64_MIN || e.max_val != INT64_MAX) {
        if (t.val < 0 || t.val < e.min_val || t.val > e.max_val) return false;
      }
      return true;
    case NKind::Lit: return t.k == TK::Sym && c.t[t.b] == e.ch;
    case NKind::Sym: return t.k == TK::Sym;
    case NKind::Space: return t.k == TK::Space;
    case NKind::List: return t.k == TK::Word && L.list_contains(e.list, L.lex_key(t.canon));
    case NKind::Word: return t.k == TK::Word;
    case NKind::Lower: return t.k == TK::Word && (t.caps == kCapsLower || t.caps == kCapsMixed);
    case NKind::Upper: return t.k == TK::Word && t.caps == kCapsUpper;
    case NKind::Cap: return t.k == TK::Word && t.caps == kCapsCapitalized;
    case NKind::Roman:
      return t.k == TK::Word && (t.caps == kCapsUpper || (t.caps == kCapsCapitalized && t.canon.size() == 1)) &&
             roman_value(t.canon) > 0;
    default: return false;
  }
}

static bool is_capture(NKind k) {
  return k == NKind::Digits || k == NKind::List || k == NKind::Word || k == NKind::Lower ||
         k == NKind::Upper || k == NKind::Cap || k == NKind::Roman;
}

bool Normalizer::match_fwd(const Ctx& c, const std::vector<NElem>& el, size_t k, size_t pos,
                           std::vector<size_t>* caps, size_t& end) const {
  if (k == el.size()) { end = pos; return true; }
  const NElem& e = el[k];
  if (e.kind == NKind::Start) return pos == 0 && match_fwd(c, el, k + 1, pos, caps, end);
  if (e.kind == NKind::End) return pos == c.tk.size() && match_fwd(c, el, k + 1, pos, caps, end);
  if (pos < c.tk.size() && elem_one(c, e, c.tk[pos])) {
    bool cap = caps && is_capture(e.kind);
    if (cap) caps->push_back(pos);
    if (match_fwd(c, el, k + 1, pos + 1, caps, end)) return true;
    if (cap) caps->pop_back();
  }
  if (e.optional) {
    bool cap = caps && is_capture(e.kind);
    if (cap) caps->push_back(SIZE_MAX);  // keep capture numbering stable
    if (match_fwd(c, el, k + 1, pos, caps, end)) return true;
    if (cap) caps->pop_back();
  }
  return false;
}

// Matches el[0..k) backwards; pos = index of the token just before.
bool Normalizer::match_bwd(const Ctx& c, const std::vector<NElem>& el, size_t k, long pos) const {
  if (k == 0) return true;
  const NElem& e = el[k - 1];
  if (e.kind == NKind::Start) return pos < 0 && match_bwd(c, el, k - 1, pos);
  if (e.kind == NKind::End) return false;
  if (pos >= 0 && elem_one(c, e, c.tk[pos]) && match_bwd(c, el, k - 1, pos - 1)) return true;
  if (e.optional) return match_bwd(c, el, k - 1, pos);
  return false;
}

std::string Normalizer::number_text(int64_t v, int ruleset) const {
  if (ruleset < 0) ruleset = L.default_cardinal();
  return L.rbnf().format(v, ruleset);
}

std::string Normalizer::digits_text(const u32str& digits) const {
  std::string s;
  for (char32_t d : digits) {
    if (!is_digit(d)) continue;
    if (!s.empty()) s.push_back(' ');
    s += L.rbnf().format(d - '0', L.digit_ruleset());
  }
  return s;
}

// "0911234567" -> 09 11 23 45 67: pairs from the left (a pair with a leading
// zero is two digits), an odd last digit alone - the way phone numbers are said.
std::string Normalizer::pairs_text(const u32str& digits) const {
  std::string s;
  u32str d;
  for (char32_t ch : digits)
    if (is_digit(ch)) d.push_back(ch);
  for (size_t i = 0; i < d.size(); i += 2) {
    if (!s.empty()) s.push_back(' ');
    if (i + 1 >= d.size() || d[i] == '0')
      s += digits_text(d.substr(i, 2));
    else
      s += number_text((d[i] - '0') * 10 + (d[i + 1] - '0'), L.default_cardinal());
  }
  return s;
}

bool Normalizer::next_starts_sentence(const Ctx& c, size_t i) const {
  while (i < c.tk.size() && c.tk[i].k == TK::Space) {
    if (c.tk[i].nl) return true;
    ++i;
  }
  if (i >= c.tk.size()) return true;
  const Tok& t = c.tk[i];
  return t.k == TK::Word && (t.caps == kCapsCapitalized || t.caps == kCapsUpper);
}

size_t Normalizer::try_rules(Ctx& c, size_t i) const {
  const LanguageData& d = L.d();
  std::vector<size_t> caps;
  for (const NRule& r : d.nrules) {
    caps.clear();
    size_t end = 0;
    if (!match_fwd(c, r.core, 0, i, &caps, end) || end == i) continue;
    size_t rend;
    if (!r.right.empty() && !match_fwd(c, r.right, 0, end, nullptr, rend)) continue;
    if (!r.left.empty() && !match_bwd(c, r.left, r.left.size(), static_cast<long>(i) - 1)) continue;

    std::string s;
    for (const NOut& o : r.out) {
      if (!s.empty()) s.push_back(' ');
      if (o.fmt == NFmt::Text) { s += o.text; continue; }
      if (o.cap == 0 || o.cap > caps.size() || caps[o.cap - 1] == SIZE_MAX) continue;
      const Tok& t = c.tk[caps[o.cap - 1]];
      int64_t v = t.k == TK::Num ? t.val : (t.k == TK::Word ? roman_value(t.canon) : -1);
      bool too_long = t.k == TK::Num && (t.val < 0 || t.nd > max_digits_);
      switch (o.fmt) {
        case NFmt::Default:
          if (t.k == TK::Num) s += too_long ? digits_text(t.canon) : number_text(v, -1);
          else s += u32_to_utf8(c.t.substr(t.b, t.e - t.b));
          break;
        case NFmt::Ruleset:
          s += (v < 0 || too_long) ? digits_text(t.canon) : number_text(v, o.idx);
          break;
        case NFmt::Map: {
          const std::string* m = v >= 0 ? L.map_value(o.idx, v) : nullptr;
          s += m ? *m : (v >= 0 ? number_text(v, -1) : std::string());
          break;
        }
        case NFmt::Digits:
          s += t.k == TK::Num ? digits_text(t.canon) : u32_to_utf8(c.t.substr(t.b, t.e - t.b));
          break;
        case NFmt::Plural: {
          PluralCat pc = L.plurals().select(v < 0 ? 0 : v);
          s += o.forms[pc].empty() ? o.forms[kOther] : o.forms[pc];
          break;
        }
        default: break;
      }
    }
    uint32_t b = c.tk[i].b, e = c.tk[end - 1].e;
    emit_text(c, s, b, e);
    const Tok& last = c.tk[end - 1];
    if (r.sentence_end_check && last.k == TK::Sym && c.t[last.b] == '.' && next_starts_sentence(c, end))
      emit_break(c, BreakType::Period, last.b, last.e);
    return end - i;
  }
  return 0;
}

// --------------------------------------------------- abbreviations, units
size_t Normalizer::try_abbrev(Ctx& c, size_t i) const {
  if (L.d().abbrevs.empty()) return 0;
  u32str canon, raw;
  int best = -1;
  size_t best_n = 0;
  for (size_t k = i; k < c.tk.size() && k < i + 10; ++k) {
    const Tok& t = c.tk[k];
    if (t.k == TK::Emoji || (t.k == TK::Space && (t.nl || k == i))) break;
    if (t.k == TK::Space) { canon.push_back(' '); raw.push_back(' '); }
    else {
      canon += t.k == TK::Word || t.k == TK::Num ? t.canon : c.t.substr(t.b, t.e - t.b);
      raw += c.t.substr(t.b, t.e - t.b);
    }
    if (canon.size() > L.max_abbrev_len()) break;
    if (t.k == TK::Space) continue;
    int a = L.abbrev(raw, true);
    if (a < 0) a = L.abbrev(L.lex_key(canon), false);
    if (a >= 0) { best = a; best_n = k - i + 1; }
  }
  if (best < 0) return 0;
  const Abbrev& ab = L.d().abbrevs[best];
  const Tok& last = c.tk[i + best_n - 1];
  emit_text(c, ab.expansion, c.tk[i].b, last.e);
  if (ab.may_end && last.k == TK::Sym && c.t[last.b] == '.' && next_starts_sentence(c, i + best_n))
    emit_break(c, BreakType::Period, last.b, last.e);
  return best_n;
}

int Normalizer::match_unit(const Ctx& c, size_t i, bool prefix_only, size_t& ntok) const {
  if (L.d().units.empty()) return -1;
  u32str canon, raw;
  int best = -1;
  for (size_t k = i; k < c.tk.size() && k < i + 4; ++k) {
    const Tok& t = c.tk[k];
    if (t.k == TK::Space || t.k == TK::Emoji) break;
    if (k > i && t.k == TK::Num) break;
    canon += t.k == TK::Word ? t.canon : c.t.substr(t.b, t.e - t.b);
    raw += c.t.substr(t.b, t.e - t.b);
    if (canon.size() > L.max_unit_len()) break;
    int u = L.unit(raw, true);
    if (u < 0) {
      u = L.unit(L.lex_key(canon), false);
      if (u >= 0 && L.d().units[u].case_sensitive) u = -1;
    }
    if (u >= 0 && (!prefix_only || L.d().units[u].prefix)) { best = u; ntok = k - i + 1; }
  }
  return best;
}

size_t Normalizer::try_prefix_unit(Ctx& c, size_t i) const {
  size_t ul = 0;
  int u = match_unit(c, i, true, ul);
  if (u < 0) return 0;
  size_t k = i + ul;
  if (k < c.tk.size() && c.tk[k].k == TK::Space && !c.tk[k].nl) ++k;
  if (k >= c.tk.size() || c.tk[k].k != TK::Num) return 0;
  // read "<number> <unit>" with the unit's forms
  size_t used = number(c, k, false, i);
  (void)used;
  return 0 + (k - i) + used;
}

size_t Normalizer::number(Ctx& c, size_t i, bool negative, size_t first) const {
  const Tok& t = c.tk[i];
  size_t j = i;
  const Tok* frac = nullptr;
  if (i + 2 < c.tk.size() && c.tk[i + 1].k == TK::Sym && c.t[c.tk[i + 1].b] == decimal_sep_ &&
      c.tk[i + 2].k == TK::Num && !decimal_word_.empty()) {
    frac = &c.tk[i + 2];
    j = i + 2;
  }
  // unit after the number (skipped if the number came after a prefix unit)
  int u = -1;
  size_t k = j + 1, ul = 0;
  const Unit* unit = nullptr;
  if (first < i && !(negative && first + 1 == i)) {
    size_t pl = 0;
    int pu = match_unit(c, first, true, pl);
    if (pu >= 0) unit = &L.d().units[pu];
  } else {
    if (k < c.tk.size() && c.tk[k].k == TK::Space && !c.tk[k].nl) ++k;
    u = match_unit(c, k, false, ul);
    if (u >= 0) unit = &L.d().units[u];
  }
  int ruleset = unit && unit->ruleset >= 0 ? unit->ruleset : L.default_cardinal();

  std::string s;
  const bool as_pairs = c.opt.digits == 2 && t.nd > 2;
  bool as_digits = c.opt.digits == 1 || as_pairs || t.val < 0 || t.nd > max_digits_ || (zero_lead_digits_ && t.zlead && t.nd > 1);
  if (as_digits) {
    s = as_pairs ? pairs_text(t.canon) : digits_text(t.canon);
    const Symbol* m = negative ? L.symbol('-') : nullptr;
    if (m && !m->name.empty()) s = m->name + " " + s;
  } else {
    s = number_text(negative ? -t.val : t.val, ruleset);
  }
  std::string opstr = u32_to_utf8(t.canon);
  if (frac) {
    bool fd = frac->nd > decimal_max_ || frac->zlead || frac->val < 0;
    s += " " + decimal_word_ + " " + (fd ? digits_text(frac->canon) : number_text(frac->val, L.default_cardinal()));
    opstr += "," + u32_to_utf8(frac->canon);
  }
  if (unit) {
    PluralCat pc = L.plurals().select(PluralOperands::from_string(opstr));
    s += " " + (unit->forms[pc].empty() ? unit->forms[kOther] : unit->forms[pc]);
  }
  size_t last = (u >= 0) ? k + ul - 1 : j;
  emit_text(c, s, c.tk[first].b, c.tk[last].e);
  return last - i + 1;
}

// ---------------------------------------------------------------- words
void Normalizer::word(Ctx& c, size_t i) const {
  const Tok& t = c.tk[i];
  if (t.canon.empty()) return;
  const Acronyms& A = L.d().acronyms;
  u32str key = L.lex_key(t.canon);
  bool spell = false;
  if (t.caps == kCapsUpper) {
    if (std::binary_search(A.words.begin(), A.words.end(), key)) spell = false;
    else if (std::binary_search(A.spell.begin(), A.spell.end(), key)) spell = true;
    else if (static_cast<int>(t.canon.size()) <= A.spell_max_len) spell = true;
    else spell = !P.has_vowel(t.canon);
  } else if (!L.lexicon(key) && !P.is_clitic(t.canon)) {
    spell = !P.has_nucleus(t.canon);
  }
  if (spell) spell_range(c, t.b, t.e, false);
  else emit_word(c, t.canon, t.b, t.e);
}

// caps_prefix: say the capital prefix (character mode only, not acronyms)
void Normalizer::spell_range(Ctx& c, uint32_t b, uint32_t e, bool caps_prefix) const {
  const Canonicalizer& cz = L.canon();
  for (uint32_t k = b; k < e; ++k) {
    char32_t ch = c.t[k];
    if (is_space(ch) || is_combining(ch) || ch == 0xFE0F) continue;
    const CharEntry* ce = cz.entry(ch);
    char32_t lc = cz.lower(ch);
    const SpellEntry* se = L.spelling(lc);
    if (!se && ce && !ce->canon.empty()) se = L.spelling(ce->canon[0]);
    std::string name;
    if (se) name = se->name;
    else if (const Symbol* sy = L.symbol(ch)) name = sy->name;
    else if (is_digit(ch)) name = number_text(ch - '0', L.digit_ruleset());
    else if (const SpellEntry* unk = L.spelling(0)) name = unk->name;  // "unknown character"
    if (name.empty()) continue;
    if (caps_prefix && ce && ce->upper && c.opt.capitals && !L.d().spell_capital.empty())
      emit_text(c, L.d().spell_capital, k, k + 1);
    emit_text(c, name, k, k + 1, kItemLetter);
  }
}

void Normalizer::symbol(Ctx& c, size_t i) const {
  const Tok& t = c.tk[i];
  const Symbol* s = L.symbol(c.t[t.b]);
  if (!s) return;
  // Punctuation glued to words on both sides ("COVID-19", "a,b") is not a
  // pause; a sentence mark followed directly by a word ("www.x.hr") is not
  // a sentence end.
  bool next_adj = i + 1 < c.tk.size() && (c.tk[i + 1].k == TK::Word || c.tk[i + 1].k == TK::Num);
  bool prev_adj = i > 0 && (c.tk[i - 1].k == TK::Word || c.tk[i - 1].k == TK::Num);
  bool sentence = s->brk == BreakType::Period || s->brk == BreakType::Question || s->brk == BreakType::Exclaim;
  if (s->level <= c.opt.punct_level && !s->name.empty()) emit_text(c, s->name, t.b, t.e);
  bool suppress = sentence ? next_adj : (next_adj && prev_adj);
  if (s->brk != BreakType::None && !suppress) emit_break(c, s->brk, t.b, t.e);
}

size_t Normalizer::emoji(Ctx& c, size_t i) const {
  const Tok& t = c.tk[i];
  size_t n = 1;
  while (i + n < c.tk.size() && c.tk[i + n].k == TK::Emoji && c.tk[i + n].emoji == t.emoji) ++n;
  if (!c.opt.emoji) return n;
  const std::string& name = L.d().emoji[t.emoji].name;
  if (n >= static_cast<size_t>(std::max(2, emoji_repeat_min_)) && !emoji_repeat_.empty()) {
    std::string s = emoji_repeat_;
    size_t p;
    if ((p = s.find("$n")) != std::string::npos) s.replace(p, 2, std::to_string(n));
    if ((p = s.find("$name")) != std::string::npos) s.replace(p, 5, name);
    emit_text(c, s, t.b, c.tk[i + n - 1].e);
  } else {
    for (size_t k = 0; k < n; ++k) {
      emit_text(c, name, c.tk[i + k].b, c.tk[i + k].e);
      emit_break(c, BreakType::Comma, c.tk[i + k].b, c.tk[i + k].e);
    }
  }
  return n;
}

// --------------------------------------------------------------- emitting
void Normalizer::emit_word(Ctx& c, const u32str& canon, uint32_t b, uint32_t e, uint8_t flags) const {
  Item it;
  it.kind = ItemKind::Word;
  it.text = canon;
  it.src_b = c.host_b(b);
  it.src_e = c.host_e(e);
  it.flags = flags;
  c.out->push_back(std::move(it));
}

void Normalizer::emit_break(Ctx& c, BreakType brk, uint32_t b, uint32_t e) const {
  auto& out = *c.out;
  if (!out.empty() && out.back().kind == ItemKind::Break) {
    if (break_priority(brk) > break_priority(out.back().brk)) out.back().brk = brk;
    return;
  }
  Item it;
  it.kind = ItemKind::Break;
  it.brk = brk;
  it.src_b = c.host_b(b);
  it.src_e = c.host_e(e);
  out.push_back(std::move(it));
}

void Normalizer::emit_text(Ctx& c, const std::string& text, uint32_t b, uint32_t e, uint8_t flags) const {
  if (text.empty()) return;
  if (c.depth >= kMaxDepth) return;
  Ctx sub;
  sub.t = utf8_to_u32(text);
  sub.hb.assign(sub.t.size() + 1, 0);
  sub.opt = c.opt;
  sub.opt.spell = false;
  sub.out = c.out;
  sub.depth = c.depth + 1;
  sub.pinned = true;
  sub.pin_b = c.host_b(b);
  sub.pin_e = c.host_e(e);
  size_t before = c.out->size();
  run_ctx(sub);
  if (flags)
    for (size_t k = before; k < c.out->size(); ++k) (*c.out)[k].flags |= flags;
}

}  // namespace mbng
