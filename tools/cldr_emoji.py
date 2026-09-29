# MBROLA NG - private helper (not shipped): Unicode CLDR annotations +
# emoji-test.txt -> languages/<code>/emoji.txt
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   python tools/cldr_emoji.py <code> <language name> emoji-test.txt \
#          annotations/<code>.xml annotationsDerived/<code>.xml out/emoji.txt
#
# Uses only the type="tts" short names. emoji.txt is ordinary language data
# afterwards (hand-editable); langc does not know where names come from.
import sys
import xml.etree.ElementTree as ET

FE0F = "️"


def load_names(*paths):
	names = {}
	for p in paths:
		for a in ET.parse(p).getroot().iter("annotation"):
			if a.get("type") == "tts" and a.text:
				names[a.get("cp")] = a.text.strip()
	return names


def main():
	code, langname, test, ann, annd, out = sys.argv[1:7]
	names = load_names(ann, annd)
	lines = []
	group = sub = None
	named = missing = 0
	for line in open(test, encoding="utf-8"):
		line = line.rstrip("\n")
		if line.startswith("# group:"):
			group = line.split(":", 1)[1].strip()
			lines.append("")
			lines.append("# ==== %s ====" % group)
			continue
		if line.startswith("# subgroup:"):
			sub = line.split(":", 1)[1].strip()
			lines.append("# --- %s" % sub)
			continue
		if not line or line.startswith("#") or "; fully-qualified" not in line:
			continue
		seq = "".join(chr(int(h, 16)) for h in line.split(";")[0].split())
		name = names.get(seq) or names.get(seq.replace(FE0F, ""))
		if name:
			lines.append("%s\t%s" % (seq, name))
			named += 1
		else:
			lines.append("# MISSING %s" % seq)
			missing += 1
	header = [
		"# " + "=" * 77,
		"# MBROLA NG - %s (%s) - emoji.txt" % (langname, code),
		"# Copyright (c) 2026 Darko Milošević (file layout and selection)",
		"#",
		'# Emoji names: Unicode CLDR release 48.2, locale "%s"' % code,
		"#   common/annotations/%s.xml + common/annotationsDerived/%s.xml" % (code, code),
		'#   (type="tts" short names). Emoji list and order: Unicode Emoji 17.0,',
		"#   emoji-test.txt (fully-qualified sequences only).",
		"# CLDR data: Copyright (c) 1991-2025 Unicode, Inc. Licensed under the",
		"#   Unicode License v3 (https://www.unicode.org/license.txt). The CLDR",
		"#   attribution must be kept when this data is redistributed.",
		"#",
		"# FORMAT (read by langc):",
		"#   <emoji sequence><TAB><text to speak>",
		"#   - One emoji or emoji sequence per line; the separator is ONE tab.",
		"#   - The engine ignores U+FE0F when matching; the longest sequence wins.",
		'#   - "# MISSING" lines have no CLDR name yet; to enable one, replace',
		'#     the line with "<emoji><TAB><name>".',
		"#   - The text is ordinary text of the language (normal reading rules).",
		"#",
		"# Entries: %d named, %d missing." % (named, missing),
		"# " + "=" * 77,
	]
	with open(out, "w", encoding="utf-8", newline="\n") as f:
		f.write("\n".join(header + lines) + "\n")
	print("wrote %s: %d named, %d missing" % (out, named, missing))


if __name__ == "__main__":
	main()
