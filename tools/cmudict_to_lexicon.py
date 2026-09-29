# MBROLA NG - private helper (not shipped): CMU Pronouncing Dictionary ->
# languages/en/lexicon.txt (US English phonemes of the us1/us2 voices).
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   python tools/cmudict_to_lexicon.py cmudict.dict cmudict.LICENSE languages/en/lexicon.txt
#
# cmudict: https://github.com/cmusphinx/cmudict (BSD-style license; the
# license text is copied into the header of the generated file, as the
# license requires). Only the first pronunciation of each word is used;
# primary stress becomes the accent mark "\".
import re
import sys

ARPA = {
	"AA": "A", "AE": "{", "AO": "O", "AW": "aU", "AY": "AI", "EH": "E", "ER": "r=", "EY": "EI",
	"IH": "I", "IY": "i", "OW": "@U", "OY": "OI", "UH": "U", "UW": "u",
	"B": "b", "CH": "tS", "D": "d", "DH": "D", "F": "f", "G": "g", "HH": "h", "JH": "dZ", "K": "k",
	"L": "l", "M": "m", "N": "n", "NG": "N", "P": "p", "R": "r", "S": "s", "SH": "S", "T": "t",
	"TH": "T", "V": "v", "W": "w", "Y": "j", "Z": "z", "ZH": "Z",
}

# Words the dictionary lacks or has in an unwanted first variant:
# letter names for spelling.txt, and a few everyday words.
MANUAL = [
	("ay", "EI\\"),
	("ee", "i\\"),
	("ef", "E\\ f"),
	("el", "E\\ l"),
	("ess", "E\\ s"),
	("aitch", "EI\\ tS"),
	("zee", "z i\\"),
	("cue", "k j u\\"),
	("ar", "A\\ r"),
	("the", "D @"),
	("a", "@"),
	("to", "t @"),
	("of", "@ v"),
	("and", "@ n d"),
	("for", "f r="),
	("or", "O\\ r"),
	("email", "i\\ m EI l"),
	("emoji", "I m @U\\ dZ i"),
	("emojis", "I m @U\\ dZ i z"),
	("wifi", "w AI\\ f AI"),
	("online", "A n l AI\\ n"),
	("nvda", "E\\ n v i d i EI"),
	# words used by abbreviations.txt, symbols.txt, units.txt, acronyms.txt
	("miz", "m I\\ z"),
	("paren", "p @ r E\\ n"),
	("caret", "k {\\ r @ t"),
	("pilcrow", "p I\\ l k r @U"),
	("kilohertz", "k I\\ l @ h r= t s"),
	("milliamp", "m I\\ l i { m p"),
	("covid", "k @U\\ v I d"),
]


def convert(pron):
	out = []
	for p in pron.split():
		m = re.fullmatch(r"([A-Z]+)([012]?)", p)
		if not m or m.group(1) not in ARPA:
			return None
		base, stress = m.group(1), m.group(2)
		sym = ARPA[base]
		if base == "AH":
			sym = "@" if stress == "0" else "V"
		out.append(sym + ("\\" if stress == "1" else ""))
	return " ".join(out)


ARPA["AH"] = "@"


def main():
	src, lic, dst = sys.argv[1:4]
	manual = {w for w, _ in MANUAL}
	entries = []
	seen = set()
	for line in open(src, encoding="utf-8"):
		line = line.split("#", 1)[0].strip()
		if not line:
			continue
		word, pron = line.split(" ", 1)
		if "(" in word:  # alternative pronunciations: keep the first only
			continue
		if not re.fullmatch(r"[a-z']*[a-z][a-z']*", word) or word in manual or word in seen:
			continue
		ph = convert(pron)
		if ph:
			seen.add(word)
			entries.append((word, ph))
	license_text = open(lic, encoding="utf-8").read().strip().splitlines()
	with open(dst, "w", encoding="utf-8", newline="\n") as f:
		f.write("# " + "=" * 77 + "\n")
		f.write("# MBROLA NG - English (en) - lexicon.txt\n")
		f.write("# Copyright (c) 2026 Darko Milošević (conversion, manual entries)\n#\n")
		f.write("# Pronunciations: CMU Pronouncing Dictionary (github.com/cmusphinx/cmudict),\n")
		f.write("# converted from ARPAbet to the US English SAMPA symbols of phonemes.txt\n")
		f.write("# by tools/cmudict_to_lexicon.py (first pronunciation of each word;\n")
		f.write("# primary stress -> accent mark \\). Hand-edit freely.\n#\n")
		f.write("# FORMAT:  word  phonemes...   (a\\ = accented vowel, a: = long)\n#\n")
		f.write("# CMUdict license (must be kept with this data):\n#\n")
		for l in license_text:
			f.write(("#   " + l).rstrip() + "\n")
		f.write("# " + "=" * 77 + "\n\n")
		f.write("# --- manual entries (letter names for spelling, corrections)\n")
		for w, p in MANUAL:
			f.write("%-24s %s\n" % (w, p))
		f.write("\n# --- CMU Pronouncing Dictionary (%d words)\n" % len(entries))
		for w, p in entries:
			f.write("%-24s %s\n" % (w, p))
	print("wrote %s: %d + %d manual entries" % (dst, len(entries), len(MANUAL)))


if __name__ == "__main__":
	main()
