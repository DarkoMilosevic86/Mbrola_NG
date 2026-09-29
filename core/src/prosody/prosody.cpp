// MBROLA NG - prosody and .pho generation
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
#include "prosody/prosody.h"

#include <algorithm>
#include <cmath>

namespace mbng {

namespace {

struct PhraseR {
  std::vector<Seg> segs;
  std::vector<int> dur;
  std::vector<std::vector<std::pair<int, double>>> f0;  // (percent, relative pitch)
  std::vector<int> word_first;  // first seg index per word (-1 = none)
  int pause_after = 0;
};

void add_line(std::string& pho, const std::string& sym, int dur, const std::vector<std::pair<int, int>>& pts) {
  pho += sym;
  pho += ' ';
  pho += std::to_string(dur);
  for (auto& p : pts) {
    pho += ' ';
    pho += std::to_string(p.first);
    pho += ' ';
    pho += std::to_string(p.second);
  }
  pho += '\n';
}

void add_pause(std::string& pho, const std::string& sym, int ms) {
  while (ms > 0) {
    int d = std::min(ms, 1000);
    add_line(pho, sym, d, {});
    ms -= d;
  }
}

}  // namespace

double Prosody::pause_for(BreakType b, bool last) const {
  switch (b) {
    case BreakType::Comma: return k("pause_comma", 180);
    case BreakType::Dash: return k("pause_dash", 180);
    case BreakType::Colon: return k("pause_colon", 250);
    case BreakType::Semicolon: return k("pause_semicolon", 300);
    case BreakType::Period: return k("pause_period", 450);
    case BreakType::Question: return k("pause_question", 450);
    case BreakType::Exclaim: return k("pause_exclaim", 450);
    case BreakType::Paragraph: return k("pause_paragraph", 650);
    case BreakType::None: return last ? k("pause_end", 60) : k("pause_comma", 180);
  }
  return 0;
}

void Prosody::silence(int ms, Chunk& out, const std::string& pause_sym) {
  add_pause(out.pho, pause_sym, std::max(ms, 1));
  out.ms += std::max(ms, 1);
}

void Prosody::render(const Sentence& s, const ProsodyParams& p, Chunk& out) const {
  const LanguageData& d = L.d();
  const std::string& pause_sym = d.phonemes[L.pause_phoneme()].sym;
  const double rate = std::max(0.2, p.rate);

  // ---- 1. phonemes per phrase (lexicon, G2P, post-lexical rules)
  std::vector<PhraseR> ph(s.phrases.size());
  std::vector<std::vector<int>> clitic(s.phrases.size());
  for (size_t pi = 0; pi < s.phrases.size(); ++pi) {
    const SPhrase& sp = s.phrases[pi];
    PhraseR& r = ph[pi];
    clitic[pi].resize(sp.words.size(), 0);
    for (size_t wi = 0; wi < sp.words.size(); ++wi) {
      const SWord& w = sp.words[wi];
      if (w.raw) {
        for (uint8_t id : w.phones) {
          Seg g;
          g.ph = id;
          g.word = static_cast<uint16_t>(wi);
          if (L.is_vowel(id)) g.flags |= kSegNucleus;
          r.segs.push_back(g);
        }
        continue;
      }
      PWord pw = P.analyse(w.text);
      clitic[pi][wi] = pw.clitic;
      for (Seg g : pw.segs) {
        g.word = static_cast<uint16_t>(wi);
        r.segs.push_back(g);
      }
    }
    P.postlex(r.segs);
    r.word_first.assign(sp.words.size(), -1);
    for (size_t i = 0; i < r.segs.size(); ++i)
      if (r.word_first[r.segs[i].word] < 0) r.word_first[r.segs[i].word] = static_cast<int>(i);
  }

  // ---- 2. durations
  const double f_stressed = k("stressed_vowel", 1.2), f_unstressed = k("unstressed_vowel", 0.9);
  const double f_long = k("long_vowel", 1.5), f_final = k("final_lengthening", 1.3);
  const double f_clitic = k("clitic", 0.9);
  double total = 0;
  for (size_t pi = 0; pi < ph.size(); ++pi) {
    PhraseR& r = ph[pi];
    int last_nuc = -1;
    for (size_t i = 0; i < r.segs.size(); ++i)
      if (r.segs[i].flags & kSegNucleus) last_nuc = static_cast<int>(i);
    r.dur.resize(r.segs.size());
    for (size_t i = 0; i < r.segs.size(); ++i) {
      const Seg& g = r.segs[i];
      const Phoneme& P0 = d.phonemes[g.ph];
      const SWord& w = s.phrases[pi].words[g.word];
      double wr = rate * std::max(0.2f, w.rate);
      if (g.flags & kSegInserted) {
        r.dur[i] = std::max(5, static_cast<int>(g.pause_ms / wr));
        continue;
      }
      double du = P0.dur * g.dur_factor;
      if (g.flags & kSegNucleus) {
        du *= (g.flags & kSegStressed) ? f_stressed : f_unstressed;
        if (g.flags & kSegLong) du *= f_long;
      }
      if (last_nuc >= 0 && g.word == r.segs[last_nuc].word && g.syll == r.segs[last_nuc].syll &&
          static_cast<int>(i) >= last_nuc - 2)
        du *= f_final;
      if (clitic[pi].size() > g.word && clitic[pi][g.word]) du *= f_clitic;
      du /= wr;
      r.dur[i] = std::max(static_cast<int>(P0.min_dur), static_cast<int>(std::lround(du)));
      if (r.dur[i] < 5) r.dur[i] = 5;
      total += r.dur[i];
    }
    bool last = pi + 1 == ph.size();
    BreakType b = last ? s.final_brk : s.phrases[pi].brk;
    double pz = pause_for(b, last && s.last_in_input) / rate;
    r.pause_after = static_cast<int>(std::lround(pz));
    if (!last) total += r.pause_after;
  }
  if (total <= 0) total = 1;

  // ---- 3. intonation (relative pitch targets on syllable nuclei)
  double f_start = k("f0_start", 1.12), f_end = k("f0_end", 0.9);
  if (s.final_brk == BreakType::Exclaim) f_start *= k("exclaim_boost", 1.12);
  const double acc_fall = k("accent_falling", 1.22), acc_rise = k("accent_rising", 1.18);
  const double fin_stmt = k("statement_final", 0.78), fin_q = k("question_final", 1.45);
  const double fin_cont = k("continuation_final", 1.08);
  double t = 0;
  for (size_t pi = 0; pi < ph.size(); ++pi) {
    PhraseR& r = ph[pi];
    r.f0.assign(r.segs.size(), {});
    int last_nuc = -1;
    for (size_t i = 0; i < r.segs.size(); ++i)
      if (r.segs[i].flags & kSegNucleus) last_nuc = static_cast<int>(i);
    bool rising_carry = false;
    uint16_t carry_word = 0;
    for (size_t i = 0; i < r.segs.size(); ++i) {
      const Seg& g = r.segs[i];
      double mid = t + r.dur[i] / 2.0;
      t += r.dur[i];
      if (!(g.flags & kSegNucleus)) continue;
      double base = f_start + (f_end - f_start) * std::min(1.0, mid / total);
      auto& pts = r.f0[i];
      if (rising_carry && g.word == carry_word) {
        pts.push_back({30, base * acc_rise});
        pts.push_back({90, base * (1 + (acc_rise - 1) * 0.5)});
        rising_carry = false;
      } else if (g.flags & kSegStressed) {
        if (g.flags & kSegRising) {
          pts.push_back({20, base * 0.97});
          pts.push_back({100, base * (1 + (acc_rise - 1) * 0.7)});
          rising_carry = true;
          carry_word = g.word;
        } else {
          pts.push_back({15, base * acc_fall});
          pts.push_back({90, base * 0.98});
        }
      } else {
        pts.push_back({50, base});
        rising_carry = false;
      }
      if (static_cast<int>(i) == last_nuc) {
        bool last = pi + 1 == ph.size();
        double first = pts.empty() ? base : pts.front().second;
        pts.clear();
        if (last) {
          if (s.final_brk == BreakType::Question) {
            pts.push_back({10, base * 0.95});
            pts.push_back({100, fin_q});
          } else {
            pts.push_back({10, first});
            pts.push_back({100, base * fin_stmt});
          }
        } else {
          pts.push_back({10, first});
          pts.push_back({100, std::max(base, 1.0) * fin_cont});
        }
      }
    }
    t += r.pause_after;
  }

  // ---- 4. .pho text and events
  Chunk& o = out;
  const int lead = static_cast<int>(k("lead_pause", 20));
  add_pause(o.pho, pause_sym, std::max(lead, 1));
  double now = std::max(lead, 1);
  {
    Event ev;
    ev.type = EventType::Sentence;
    ev.ms = 0;
    bool first = true;
    for (auto& sp : s.phrases)
      for (auto& w : sp.words) {
        if (first) { ev.src_b = w.src_b; first = false; }
        ev.src_e = w.src_e;
      }
    o.events.push_back(ev);
  }
  for (size_t pi = 0; pi < ph.size(); ++pi) {
    const SPhrase& sp = s.phrases[pi];
    PhraseR& r = ph[pi];
    size_t si = 0;
    for (size_t wi = 0; wi < sp.words.size(); ++wi) {
      const SWord& w = sp.words[wi];
      for (auto& m : w.marks_before) {
        Event ev;
        ev.type = EventType::Mark;
        ev.ms = now;
        ev.mark = m.first;
        ev.name = m.second;
        o.events.push_back(ev);
      }
      Event ev;
      ev.type = EventType::Word;
      ev.ms = now;
      ev.src_b = w.src_b;
      ev.src_e = w.src_e;
      o.events.push_back(ev);
      if (!o.debug_words.empty()) o.debug_words.push_back(' ');
      if (w.raw) {
        std::vector<PhTok> toks;
        for (uint8_t id : w.phones) toks.push_back({id, 0});
        o.debug_words += "[" + P.to_string(toks) + "]";
      } else {
        o.debug_words += u32_to_utf8(w.text);
      }
      for (; si < r.segs.size() && r.segs[si].word == wi; ++si) {
        const Seg& g = r.segs[si];
        std::vector<std::pair<int, int>> pts;
        for (auto& f : r.f0[si]) {
          double rel = 1 + (f.second - 1) * p.range * w.range;
          int hz = static_cast<int>(std::lround(p.base_pitch * p.pitch * w.pitch * rel));
          pts.push_back({f.first, std::max(30, std::min(hz, 600))});
        }
        // the voice may use other symbols than the language (phoneme_map, 10.3)
        const std::string& sym = g.ph < p.voice_syms.size() && !p.voice_syms[g.ph].empty()
                                     ? p.voice_syms[g.ph]
                                     : d.phonemes[g.ph].sym;
        add_line(o.pho, sym, r.dur[si], pts);
        if (p.phoneme_events) {
          Event pe;
          pe.type = EventType::Phoneme;
          pe.ms = now;
          pe.phoneme = g.ph;
          pe.dur_ms = r.dur[si];
          o.events.push_back(pe);
        }
        now += r.dur[si];
      }
    }
    for (auto& m : sp.marks_after) {
      Event ev;
      ev.type = EventType::Mark;
      ev.ms = now;
      ev.mark = m.first;
      ev.name = m.second;
      o.events.push_back(ev);
    }
    int pause = std::max(r.pause_after, pi + 1 == ph.size() ? 10 : 1);
    add_pause(o.pho, pause_sym, pause);
    now += pause;
    if (!o.debug_phones.empty()) o.debug_phones += " _ ";
    o.debug_phones += P.to_string(r.segs);
  }
  o.ms = now;
}

}  // namespace mbng
