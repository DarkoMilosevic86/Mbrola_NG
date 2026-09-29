# MBROLA NG - builds the NVDA add-on
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   python nvda\build_addon.py [--no-build] [--voice PATH_TO_cr1_FOLDER]
#
# 1. builds x64 and x86 (build.cmd) - both are shipped (ANALYSIS 12.2)
# 2. stages nvda\build\addon: add-on sources + binaries + languages + licenses
# 3. zips it to nvda\mbrolaNG-<version>.nvda-addon
# --voice: also installs a voice folder (cr1 + license.txt) into the NVDA
#          user configuration (what the Voice Manager will do later).
import argparse
import os
import shutil
import subprocess
import sys
import zipfile

VERSION = "0.1.0"

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STAGE = os.path.join(HERE, "build", "addon")
DRIVER = os.path.join(STAGE, "synthDrivers", "mbrola_ng")


def run_build(arch):
	print("building", arch, "...")
	subprocess.run(["cmd", "/c", os.path.join(ROOT, "build.cmd"), arch], check=True, cwd=ROOT)


def stage():
	if os.path.isdir(STAGE):
		shutil.rmtree(STAGE)
	shutil.copytree(os.path.join(HERE, "addon"), STAGE,
		ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
	manifest = os.path.join(STAGE, "manifest.ini")
	with open(manifest, encoding="utf-8") as f:
		text = f.read().replace("@VERSION@", VERSION)
	with open(manifest, "w", encoding="utf-8", newline="\n") as f:
		f.write(text)
	for arch in ("x64", "x86"):
		out = os.path.join(DRIVER, arch)
		os.makedirs(out)
		for name in ("MBROLA_NG.dll", "mbrola_ng_synth.exe"):
			shutil.copy2(os.path.join(ROOT, "build", arch, name), out)
	langs = os.path.join(DRIVER, "languages")
	os.makedirs(langs)
	# all compiled languages (build.cmd compiles every folder of languages\)
	for code in sorted(os.listdir(os.path.join(ROOT, "languages"))):
		dat = os.path.join(ROOT, "build", "x64", code + ".dat")
		if not os.path.isfile(dat):
			raise SystemExit("missing %s - did the language compile?" % dat)
		shutil.copy2(dat, langs)
	# voice catalog known to this version (used when the online one is unavailable)
	shutil.copy2(os.path.join(ROOT, "catalog", "catalog.json"), DRIVER)
	compile_translations()


def _po_unquote(s):
	s = s.strip()
	assert s.startswith('"') and s.endswith('"'), s
	return bytes(s[1:-1], "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")


def read_po(path):
	"""Minimal .po reader: msgid/msgstr pairs (no plurals, no contexts)."""
	entries = {}
	msgid = msgstr = None
	target = None
	with open(path, encoding="utf-8") as f:
		for line in f:
			line = line.strip()
			if line.startswith("msgid "):
				if msgid is not None:
					entries[msgid] = msgstr
				msgid, msgstr, target = _po_unquote(line[6:]), "", "id"
			elif line.startswith("msgstr "):
				msgstr, target = _po_unquote(line[7:]), "str"
			elif line.startswith('"'):
				if target == "id":
					msgid += _po_unquote(line)
				elif target == "str":
					msgstr += _po_unquote(line)
	if msgid is not None:
		entries[msgid] = msgstr
	return entries


def write_mo(entries, path):
	"""GNU gettext .mo writer (what msgfmt produces, without a hash table)."""
	import struct
	keys = sorted(k for k, v in entries.items() if v or k == "")
	ids = [k.encode("utf-8") for k in keys]
	strs = [entries[k].encode("utf-8") for k in keys]
	n = len(keys)
	orig_tab = 28
	trans_tab = orig_tab + n * 8
	data = trans_tab + n * 8
	offsets = []
	blob = b""
	for s in ids + strs:
		offsets.append((len(s), data + len(blob)))
		blob += s + b"\0"
	out = struct.pack("<7I", 0x950412DE, 0, n, orig_tab, trans_tab, 0, data)
	for length, off in offsets:
		out += struct.pack("<2I", length, off)
	with open(path, "wb") as f:
		f.write(out + blob)


def code_msgids():
	import ast
	ids = set()
	for dirpath, _dirs, files in os.walk(os.path.join(HERE, "addon")):
		for name in files:
			if not name.endswith(".py"):
				continue
			with open(os.path.join(dirpath, name), encoding="utf-8") as f:
				tree = ast.parse(f.read())
			for node in ast.walk(tree):
				if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "_"
						and node.args and isinstance(node.args[0], ast.Constant)):
					ids.add(node.args[0].value)
	return ids


def compile_translations():
	needed = code_msgids()
	locale = os.path.join(STAGE, "locale")
	for lang in sorted(os.listdir(locale)):
		po = os.path.join(locale, lang, "LC_MESSAGES", "nvda.po")
		if not os.path.isfile(po):
			continue
		entries = read_po(po)
		missing = sorted(i for i in needed if not entries.get(i))
		for m in missing:
			print("warning: %s: no translation for %r" % (lang, m))
		write_mo(entries, po[:-3] + ".mo")
		print("translation %s: %d strings, %d missing" % (lang, len(entries) - 1, len(missing)))
	write_licenses(os.path.join(STAGE, "licenses"))


def cmudict_license():
	"""The CMUdict license text (must be reproduced with the compiled en.dat)."""
	with open(os.path.join(ROOT, "languages", "en", "lexicon.txt"), encoding="utf-8") as f:
		cmu = []
		inside = False
		for line in f:
			if line.startswith("# CMUdict license"):
				inside = True
				continue
			if inside:
				if line.startswith("# ===="):
					break
				cmu.append(line[4:].rstrip() if line.startswith("#   ") else "")
	return "\n".join(cmu).strip() + "\n"


ADDON_README = (
	"MBROLA NG - Copyright (c) 2026 Darko Milosevic.\n"
	"MBROLA NG is free software under the GNU General Public License,\n"
	"version 2 or (at your option) any later version: see LICENSE.txt.\n\n"
	"mbrola_ng_synth.exe is a separate program: the MBROLA synthesizer\n"
	"(Copyright (c) Faculte Polytechnique de Mons - TCTS lab) with a small\n"
	"pipe front end, licensed under the GNU AGPL v3\n"
	"(AGPL-3.0-mbrola_ng_synth.txt).\n\n"
	"Emoji names (Croatian, English): Unicode CLDR, Copyright (c) 1991-2025\n"
	"Unicode, Inc., Unicode License v3 (https://www.unicode.org/license.txt).\n\n"
	"English pronunciations: CMU Pronouncing Dictionary, Copyright (C) 1993-2015\n"
	"Carnegie Mellon University. All rights reserved. BSD-style license:\n"
	"see CMUDICT-LICENSE.txt.\n\n"
	"Voices are not part of this add-on; each voice has its own license\n"
	"(cr1: University of Zagreb, Department of Phonetics - free for\n"
	"non-commercial use, see the license.txt installed with the voice).\n")


def write_licenses(lic, readme=ADDON_README):
	"""Third-party licenses (shared with installer\\build_installer.py)."""
	os.makedirs(lic)
	shutil.copy2(os.path.join(ROOT, "LICENSE"), os.path.join(lic, "LICENSE.txt"))
	shutil.copy2(os.path.join(ROOT, "synth", "LICENSE"), os.path.join(lic, "AGPL-3.0-mbrola_ng_synth.txt"))
	with open(os.path.join(lic, "CMUDICT-LICENSE.txt"), "w", encoding="utf-8", newline="\r\n") as f:
		f.write(cmudict_license())
	with open(os.path.join(lic, "README.txt"), "w", encoding="utf-8", newline="\r\n") as f:
		f.write(readme)


def package():
	target = os.path.join(HERE, "mbrolaNG-%s.nvda-addon" % VERSION)
	if os.path.exists(target):
		os.remove(target)
	with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as z:
		for dirpath, _dirs, files in os.walk(STAGE):
			for name in files:
				full = os.path.join(dirpath, name)
				z.write(full, os.path.relpath(full, STAGE))
	print("wrote", target)
	return target


def install_voice(src):
	appdata = os.environ["APPDATA"]
	dst = os.path.join(appdata, "nvda", "mbrola_ng", "voices", "cr1")
	os.makedirs(dst, exist_ok=True)
	for name in ("cr1", "license.txt"):
		shutil.copy2(os.path.join(src, name), dst)
	print("voice installed in", dst)


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--no-build", action="store_true")
	ap.add_argument("--voice", help="folder containing cr1 and license.txt")
	a = ap.parse_args()
	if not a.no_build:
		run_build("x64")
		run_build("x86")
	stage()
	package()
	if a.voice:
		install_voice(a.voice)


if __name__ == "__main__":
	sys.exit(main())
