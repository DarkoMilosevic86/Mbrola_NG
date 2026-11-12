# MBROLA NG - checks Resources/catalog/catalog.json
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   python tools/check_catalog.py
#
# The apps read the catalog from the Resources repository at run time and
# silently ignore one they cannot use, so a mistake there is not noticed
# until someone wonders why a voice is missing. This checks what the apps
# rely on: the schema, the structure of every language and voice, the
# https addresses, the SHA-256 form, that the sample listed for a voice is the
# file in Resources/assets/samples (size and checksum), and that every
# language folder has a catalog entry. No network is needed.
# Exit status 1 when there is an error; warnings do not fail.
import hashlib
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CATALOG = os.path.join(ROOT, "Resources", "catalog", "catalog.json")
SAMPLES = os.path.join(ROOT, "Resources", "assets", "samples")
LANGUAGES = os.path.join(ROOT, "languages")
SCHEMA = 1
HEX64 = re.compile(r"^[0-9a-f]{64}$")

errors = []
warnings = []


def error(where, text):
	errors.append("%s: %s" % (where, text))


def checkFile(where, f, needSize=True):
	"""One {"name", "urls", "size", "sha256"} object."""
	if not isinstance(f, dict) or not f.get("name"):
		error(where, "no name")
		return
	where = "%s/%s" % (where, f["name"])
	urls = f.get("urls")
	if not isinstance(urls, list) or not urls:
		error(where, "no urls")
	else:
		for u in urls:
			if not isinstance(u, str) or not u.lower().startswith("https://"):
				error(where, "address is not https (the apps drop it): %r" % (u,))
	if needSize and not (isinstance(f.get("size"), int) and f["size"] > 0):
		error(where, "size is missing or not a positive integer")
	if not HEX64.match(str(f.get("sha256", ""))):
		error(where, "sha256 is not 64 lowercase hex digits")


def checkSample(where, voice):
	sample = voice.get("sample")
	if sample is None:
		warnings.append("%s: no sample (the app shows no Try button before installation)" % where)
		return
	checkFile(where + "/sample", sample)
	name = sample.get("name", "")
	if name in [f.get("name") for f in voice.get("files", [])]:
		error(where, "the sample must not be one of the files (it would be installed with the voice)")
	path = os.path.join(SAMPLES, name)
	if not os.path.isfile(path):
		error(where, "sample %s is not in Resources/assets/samples" % name)
		return
	with open(path, "rb") as fh:
		data = fh.read()
	if sample.get("size") != len(data):
		error(where, "sample size %s is not the size of %s (%d)" % (sample.get("size"), name, len(data)))
	if sample.get("sha256") != hashlib.sha256(data).hexdigest():
		error(where, "sample sha256 is not the checksum of %s" % name)


def main():
	try:
		with open(CATALOG, encoding="utf-8") as fh:
			catalog = json.load(fh)
	except (OSError, ValueError) as e:
		raise SystemExit("cannot read %s: %s\nIs the Resources submodule checked out? "
			"git submodule update --init" % (CATALOG, e))

	if catalog.get("schema") != SCHEMA:
		error("catalog", "schema is %r, the apps understand %d" % (catalog.get("schema"), SCHEMA))

	languages = {}
	for l in catalog.get("languages", []):
		code = l.get("code")
		if not code:
			error("language", "no code")
			continue
		if code in languages:
			error("language " + code, "listed twice")
		languages[code] = l
		for key in ("sapi_lcid", "android_locale", "apple_locale"):
			if not l.get(key):
				error("language " + code, key + " is missing")
		if not l.get("names", {}).get("en"):
			error("language " + code, 'names has no "en" (the fallback of every app)')

	voices = {}
	for v in catalog.get("voices", []):
		vid = v.get("id")
		if not vid:
			error("voice", "no id")
			continue
		if vid in voices:
			error("voice " + vid, "listed twice")
		voices[vid] = v
		where = "voice " + vid
		if v.get("language") not in languages:
			error(where, "language %r is not in the language list" % v.get("language"))
		if not v.get("names", {}).get("en"):
			error(where, 'names has no "en"')
		files = v.get("files")
		if not isinstance(files, list) or not files:
			error(where, "no files")
			files = []
		for f in files:
			checkFile(where, f)
		license = v.get("license") or {}
		if license.get("file") not in [f.get("name") for f in files]:
			error(where, "license.file is not one of the files (the license is shown before the download)")
		if not isinstance(v.get("voice_config"), dict):
			error(where, "no voice_config")
		checkSample(where, v)

	for code, l in languages.items():
		listed = set(l.get("voices", []))
		actual = {vid for vid, v in voices.items() if v.get("language") == code}
		if listed != actual:
			error("language " + code, "voices %s do not match the voices of this language %s"
				% (sorted(listed), sorted(actual)))

	if os.path.isdir(LANGUAGES):
		for code in sorted(os.listdir(LANGUAGES)):
			if os.path.isfile(os.path.join(LANGUAGES, code, "language.txt")) and code not in languages:
				warnings.append("language %s has no catalog entry: it compiles, but no app shows it" % code)

	for w in warnings:
		print("warning:", w)
	for e in errors:
		print("error:", e)
	print("%d languages, %d voices, %d errors, %d warnings"
		% (len(languages), len(voices), len(errors), len(warnings)))
	return 1 if errors else 0


if __name__ == "__main__":
	sys.exit(main())
