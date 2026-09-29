// MBROLA NG - phonology
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "phon/phonology.h"

#include <algorithm>

namespace mbng {

std::vector<PhTok> Phonology::transcribe(const u32str& canon, u32str* unknown) const {
  if (const LexEntry* e = L.lexicon(L.lex_key(canon))) return e->phones;
  std::vector<PhTok> out;
  const auto& rules = L.d().g2p;
  size_t pos = 0;
  while (pos < canon.size()) {
    bool done = false;
    for (uint32_t idx : L.g2p_rules_for(canon[pos])) {
      const G2PRule& r = rules[idx];
      if (canon.compare(pos, r.match.size(), r.match) != 0) continue;
      if (!ctx_match_letters(canon, pos, r.match.size(), r)) continue;
      for (uint8_t ph : r.out) out.push_back({ph, 0});
      pos += r.match.size();
      done = true;
      break;
    }
    if (!done) {
      if (unknown) unknown->push_back(canon[pos]);
      ++pos;
    }
  }
  return out;
}

bool Phonology::ctx_match_letters(const u32str& w, size_t pos, size_t len, const G2PRule& r) const {
  auto one = [&](const CtxElem& e, char32_t c) {
    return e.kind == CtxKind::Char ? c == e.ch : L.in_letterclass(e.cls, c);
  };
  size_t j = pos + len;
  for (const CtxElem& e : r.right) {
    if (e.kind == CtxKind::Boundary) {
      if (j != w.size()) return false;
      continue;
    }
    if (j >= w.size() || !one(e, w[j])) return false;
    ++j;
  }
  size_t k = pos;
  for (auto it = r.left.rbegin(); it != r.left.rend(); ++it) {
    if (it->kind == CtxKind::Boundary) {
      if (k != 0) return false;
      continue;
    }
    if (k == 0 || !one(*it, w[k - 1])) return false;
    --k;
  }
  return true;
}

bool Phonology::elem_match(const PElem& e, uint8_t ph) const {
  if (e.kind == PKind::Phoneme) return e.id == ph;
  if (e.kind == PKind::Class) return L.in_phclass(e.id, ph);
  return false;
}

bool Phonology::ctx_match_phones(const std::vector<Seg>& s, size_t focus, const std::vector<PElem>& left,
                                 const std::vector<PElem>& right) const {
  const size_t n = s.size();
  // right context
  size_t cur = focus;
  bool allow = false, require = false;
  for (const PElem& e : right) {
    if (e.kind == PKind::Boundary) { allow = require = true; continue; }
    if (e.kind == PKind::OptBoundary) { allow = true; continue; }
    size_t j = cur + 1;
    if (j >= n) return false;
    bool gap = s[j].word != s[cur].word;
    if ((gap && !allow) || (!gap && require)) return false;
    if (!elem_match(e, s[j].ph)) return false;
    cur = j;
    allow = require = false;
  }
  if (require && cur + 1 < n && s[cur + 1].word == s[cur].word) return false;
  // left context (mirrored)
  cur = focus;
  allow = require = false;
  for (auto it = left.rbegin(); it != left.rend(); ++it) {
    const PElem& e = *it;
    if (e.kind == PKind::Boundary) { allow = require = true; continue; }
    if (e.kind == PKind::OptBoundary) { allow = true; continue; }
    if (cur == 0) return false;
    size_t j = cur - 1;
    bool gap = s[j].word != s[cur].word;
    if ((gap && !allow) || (!gap && require)) return false;
    if (!elem_match(e, s[j].ph)) return false;
    cur = j;
    allow = require = false;
  }
  if (require && cur > 0 && s[cur - 1].word == s[cur].word) return false;
  return true;
}

void Phonology::syllabify(PWord& w) const {
  auto& s = w.segs;
  const Syllables& sy = L.d().syllables;
  for (auto& g : s) {
    bool nuc = sy.nucleus_class == 0xFFFF ? L.is_vowel(g.ph) : L.in_phclass(sy.nucleus_class, g.ph);
    if (nuc) g.flags |= kSegNucleus;
  }
  // syllabic consonants (evaluated on the original, non-syllabic string)
  std::vector<size_t> extra;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i].flags & kSegNucleus) continue;
    for (const SyllRule& r : sy.syllabic)
      if (r.ph == s[i].ph && ctx_match_phones(s, i, r.left, r.right)) { extra.push_back(i); break; }
  }
  for (size_t i : extra) s[i].flags |= kSegNucleus;

  std::vector<size_t> nuclei;
  for (size_t i = 0; i < s.size(); ++i)
    if (s[i].flags & kSegNucleus) nuclei.push_back(i);
  w.nsyll = static_cast<int>(nuclei.size());
  if (nuclei.empty()) {
    for (auto& g : s) g.syll = 0;
    return;
  }
  for (size_t k = 0; k < nuclei.size(); ++k) s[nuclei[k]].syll = static_cast<int16_t>(k);
  for (size_t i = 0; i < nuclei[0]; ++i) s[i].syll = 0;
  for (size_t i = nuclei.back() + 1; i < s.size(); ++i) s[i].syll = static_cast<int16_t>(nuclei.size() - 1);
  for (size_t k = 0; k + 1 < nuclei.size(); ++k) {
    size_t a = nuclei[k], b = nuclei[k + 1];
    size_t m = b - a - 1;
    for (size_t i = a + 1; i < b; ++i) {
      bool to_prev = m >= 2 && i == a + 1;
      s[i].syll = static_cast<int16_t>(to_prev ? k : k + 1);
    }
  }
}

void Phonology::accent(PWord& w, const u32str& key, bool from_lexicon) const {
  if (w.clitic || w.nsyll == 0) return;
  auto nucleus_of = [&](int syll) -> Seg* {
    for (auto& g : w.segs)
      if ((g.flags & kSegNucleus) && g.syll == syll) return &g;
    return nullptr;
  };
  if (from_lexicon) {
    for (auto& g : w.segs)
      if (g.flags & kSegStressed) return;  // accent given by the lexicon
  }
  const Accents& A = L.d().accents;
  int syll;
  AccentType type;
  const AccEntry* e = L.accent(key);
  if (e) {
    syll = e->syll > 0 ? e->syll - 1 : w.nsyll + e->syll;
    type = e->type;
    for (uint8_t ls : e->long_sylls)
      if (Seg* g = nucleus_of(ls - 1)) g->flags |= kSegLong;
  } else {
    syll = A.default_syll > 0 ? A.default_syll - 1 : w.nsyll + A.default_syll;
    type = A.default_type;
    if (A.never_last && w.nsyll >= 2 && syll >= w.nsyll - 1) syll = w.nsyll - 2;
  }
  syll = std::max(0, std::min(syll, w.nsyll - 1));
  if (type == AccentType::None) return;
  if (Seg* g = nucleus_of(syll)) {
    g->flags |= kSegStressed;
    if (type == AccentType::Rising) g->flags |= kSegRising;
  }
}

PWord Phonology::analyse(const u32str& canon) const {
  PWord w;
  u32str key = L.lex_key(canon);
  const LexEntry* lex = L.lexicon(key);
  std::vector<PhTok> toks = lex ? lex->phones : transcribe(canon);
  for (const PhTok& t : toks) {
    Seg g;
    g.ph = t.ph;
    if (t.flags & kLong) g.flags |= kSegLong;
    if (t.flags & (kAccFalling | kAccRising)) g.flags |= kSegStressed;
    if (t.flags & kAccRising) g.flags |= kSegRising;
    w.segs.push_back(g);
  }
  if (const Clitic* c = L.clitic(key)) w.clitic = static_cast<int>(c->type);
  syllabify(w);
  if (w.clitic) {
    for (auto& g : w.segs) g.flags &= static_cast<uint8_t>(~(kSegStressed | kSegRising));
  }
  accent(w, key, lex != nullptr);
  return w;
}

bool Phonology::has_vowel(const u32str& canon) const {
  for (const PhTok& t : transcribe(canon))
    if (L.is_vowel(t.ph)) return true;
  return false;
}

bool Phonology::has_nucleus(const u32str& canon) const {
  PWord w = analyse(canon);
  return w.nsyll > 0;
}

bool Phonology::is_clitic(const u32str& canon) const { return L.clitic(L.lex_key(canon)) != nullptr; }

void Phonology::postlex(std::vector<Seg>& s) const {
  const Postlex& P = L.d().postlex;
  for (const PostlexRule& r : P.rules) {
    for (size_t k = s.size(); k-- > 0;) {
      if (!elem_match(r.focus, s[k].ph)) continue;
      if (!ctx_match_phones(s, k, r.left, r.right)) continue;
      switch (r.op) {
        case PostOp::Delete:
          s.erase(s.begin() + k);
          break;
        case PostOp::Partner: {
          uint8_t p = L.d().phonemes[s[k].ph].partner;
          if (p != kNoPhoneme) s[k].ph = p;
          break;
        }
        case PostOp::Replace: {
          if (r.repl.empty()) { s.erase(s.begin() + k); break; }
          s[k].ph = r.repl[0];
          for (size_t m = 1; m < r.repl.size(); ++m) {
            Seg g = s[k];
            g.ph = r.repl[m];
            g.flags &= static_cast<uint8_t>(~(kSegNucleus | kSegStressed | kSegRising | kSegLong));
            s.insert(s.begin() + k + m, g);
          }
          break;
        }
      }
    }
  }
  // identical neighbours: the voice has no X-X diphone
  for (size_t k = s.size(); k-- > 1;) {
    Seg& a = s[k - 1];
    Seg& b = s[k];
    if (a.ph != b.ph || a.ph == L.pause_phoneme()) continue;
    const IdentRule* rule = nullptr;
    for (const IdentRule& ir : P.identical)
      if (L.in_phclass(ir.cls, a.ph)) { rule = &ir; break; }
    if (!rule) continue;
    bool across = a.word != b.word;
    IdentOp op = across ? rule->across : rule->within;
    uint16_t val = across ? rule->across_val : rule->within_val;
    if (op == IdentOp::Merge) {
      a.dur_factor = std::max(a.dur_factor, b.dur_factor) * val / 100.0f;
      a.flags |= b.flags & (kSegNucleus | kSegStressed | kSegRising | kSegLong);
      s.erase(s.begin() + k);
    } else if (op == IdentOp::Pause) {
      Seg g;
      g.ph = L.pause_phoneme();
      g.word = b.word;
      g.syll = b.syll;
      g.flags = kSegInserted;
      g.pause_ms = val;
      s.insert(s.begin() + k, g);
    }
  }
}

std::string Phonology::to_string(const std::vector<Seg>& segs, bool marks) const {
  std::string out;
  for (size_t i = 0; i < segs.size(); ++i) {
    if (i && marks && segs[i].word != segs[i - 1].word) out += " |";
    if (!out.empty()) out.push_back(' ');
    out += L.d().phonemes[segs[i].ph].sym;
    if (marks) {
      if (segs[i].flags & kSegLong) out += ":";
      if (segs[i].flags & kSegStressed) out += (segs[i].flags & kSegRising) ? "/" : "\\";
      else if ((segs[i].flags & kSegNucleus) && !L.is_vowel(segs[i].ph)) out += "=";
    }
  }
  return out;
}

std::string Phonology::to_string(const std::vector<PhTok>& toks) const {
  std::string out;
  for (auto& t : toks) {
    if (!out.empty()) out.push_back(' ');
    out += L.d().phonemes[t.ph].sym;
  }
  return out;
}

}  // namespace mbng
