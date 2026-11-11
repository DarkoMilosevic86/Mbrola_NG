# MBROLA NG - makes the short voice samples played by "Try" in the apps,
# for voices that are not installed yet
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   python tools\make_voice_samples.py        (Windows, after build.cmd x64)
#
# For every voice of Resources/catalog/catalog.json: downloads the database
# into build\voices-cache (checksum verified, not committed), speaks one sentence
# through MBROLA_NG.dll with the voice's catalog settings, and writes
# Resources/assets/samples/<id>.wav (16-bit mono).
#
# The samples are not part of any build: the apps download them from the
# Resources repository when "Try" is pressed on a voice that is not
# installed. After regenerating them, run the script with --catalog-entries
# and paste the printed blocks into the "sample" entry of each voice in
# Resources/catalog/catalog.json, so the size and SHA-256 the apps verify
# match the files. Push both repositories together.
#
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
OUT = os.path.join(ROOT, "Resources", "assets", "samples")

# where the apps download a sample from (the "sample" entry of the catalog)
SAMPLE_URLS = (
	"https://raw.githubusercontent.com/Mbrola-NG/Resources/main/assets/samples/%s",
	"https://cdn.jsdelivr.net/gh/Mbrola-NG/Resources@main/assets/samples/%s",
)

TEXT = {
	"hr": "Dobar dan! Ovako zvuči ovaj glas. Instalirajte ga i čitat će vam sve na telefonu.",
	"en": "Hello! This is how this voice sounds. Install it, and it will read everything on your phone.",
	"es": "¡Buenos días! Así suena esta voz. Instálala y te leerá todo en el teléfono.",
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


def catalogEntries(catalog):
	"""Prints the "sample" entry of every voice, as it belongs in the catalog:
	the apps refuse a sample whose size or SHA-256 does not match."""
	for entry in catalog["voices"]:
		name = entry["id"] + ".wav"
		path = os.path.join(OUT, name)
		if not os.path.isfile(path):
			print("# %s: no sample" % entry["id"])
			continue
		data = open(path, "rb").read()
		sep = "," + chr(10) + "          "
		urls = sep.join('"%s"' % (u % name) for u in SAMPLE_URLS)
		print('      "sample": { "name": "%s",' % name)
		print('        "urls": [ %s ],' % urls)
		print('        "size": %d,' % len(data))
		print('        "sha256": "%s" },' % hashlib.sha256(data).hexdigest())


def main():
	with open(os.path.join(ROOT, "Resources", "catalog", "catalog.json"), encoding="utf-8") as fh:
		catalog = json.load(fh)
	if "--catalog-entries" in sys.argv:
		catalogEntries(catalog)
		return
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
	print("")
	print("Now put the new sizes and checksums into Resources/catalog/catalog.json:")
	print("  python tools/make_voice_samples.py --catalog-entries")


if __name__ == "__main__":
	main()
