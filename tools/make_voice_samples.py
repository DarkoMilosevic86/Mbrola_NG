# MBROLA NG - makes the short voice samples played by "Try" in the Android
# app for voices that are not installed yet
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   python tools\make_voice_samples.py        (Windows, after build.cmd x64)
#
# For every voice of catalog/catalog.json: downloads the voice database into
# build\voices-cache (checksum verified, not committed), speaks one sentence
# through MBROLA_NG.dll with the voice's catalog settings, and writes
# android/app/src/main/assets/samples/<id>.wav (16-bit mono).
# The samples are speech synthesized with MBROLA - the use the voice licenses
# permit; the voice databases themselves are never added to the repository.
import hashlib
import json
import os
import sys
import urllib.request
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "nvda", "addon", "synthDrivers", "mbrola_ng"))
import _mbng  # noqa: E402  (ctypes binding of the core C API)

BIN = os.path.join(ROOT, "build", "x64")
CACHE = os.path.join(ROOT, "build", "voices-cache")
OUT = os.path.join(ROOT, "android", "app", "src", "main", "assets", "samples")

TEXT = {
	"hr": "Dobar dan! Ovako zvuči ovaj glas. Instalirajte ga i čitat će vam sve na telefonu.",
	"en": "Hello! This is how this voice sounds. Install it, and it will read everything on your phone.",
}


def fetch(entry):
	folder = os.path.join(CACHE, entry["id"])
	os.makedirs(folder, exist_ok=True)
	db = os.path.join(folder, entry["id"])
	f = next(x for x in entry["files"] if x["name"] == entry["id"])
	if not os.path.isfile(db) or hashlib.sha256(open(db, "rb").read()).hexdigest() != f["sha256"]:
		print("downloading", entry["id"], "...")
		urllib.request.urlretrieve(f["urls"][0], db)
		if hashlib.sha256(open(db, "rb").read()).hexdigest() != f["sha256"]:
			raise SystemExit("checksum mismatch: " + db)
	return db


def main():
	with open(os.path.join(ROOT, "catalog", "catalog.json"), encoding="utf-8") as fh:
		catalog = json.load(fh)
	lib = _mbng.Library(BIN)
	os.makedirs(OUT, exist_ok=True)
	for entry in catalog["voices"]:
		lang = entry["language"]
		if lang not in TEXT:
			continue
		cfg = entry.get("voice_config", {})
		pmap = " ".join("%s=%s" % kv for kv in cfg.get("phoneme_map", {}).items())
		engine = _mbng.Engine(lib, os.path.join(BIN, lang + ".dat"), fetch(entry), cfg.get("base_pitch", 0), pmap)
		engine.begin([(_mbng.SEG_TEXT, TEXT[lang], 0)])
		pcm = b""
		while True:
			data, events = engine.read()
			pcm += data
			if not data and not events:
				break
		rate = engine.sampleRate
		engine.destroy()
		path = os.path.join(OUT, entry["id"] + ".wav")
		with wave.open(path, "wb") as w:
			w.setnchannels(1)
			w.setsampwidth(2)
			w.setframerate(rate)
			w.writeframes(pcm)
		print("%s: %.1f s, %d kB" % (os.path.relpath(path, ROOT), len(pcm) / 2.0 / rate, os.path.getsize(path) // 1024))


if __name__ == "__main__":
	main()
