#!/usr/bin/env python3
# MBROLA NG - voice manager for Linux (ANALYSIS 13.4)
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   mbrola-ng-voices list                    installed voices
#   mbrola-ng-voices available               voices in the catalog
#   mbrola-ng-voices install hr|cr1 ...      download, verify and install
#   mbrola-ng-voices install-file FILE       offline: database file or zip
#   mbrola-ng-voices remove cr1
#   mbrola-ng-voices update                  reinstall voices changed in the catalog
# Options: --system (with sudo: /usr/local/share/mbrola-ng/voices for all
# users; default: ~/.local/share/mbrola-ng/voices), --yes (accept licenses).
#
# A plain terminal program on purpose: fully accessible with Orca and
# braille, and scriptable. Same catalog, .voice files and verification as
# the NVDA Voice Manager (nvda/addon/synthDrivers/mbrola_ng/_voices.py).

import argparse
import datetime
import hashlib
import json
import locale
import os
import shutil
import struct
import sys
import tempfile
import urllib.request
import zipfile

VERSION = "@MBNG_VERSION@"
DATADIR = "@MBNG_DATADIR@"  # set by the build (CMake configure_file)
CATALOG_URL = None          # online catalog, decided at release time (10.5)
CATALOG_SCHEMA = 1
ENGINE_VERSION = "0.1"


def dataDir():
	env = os.environ.get("MBROLA_NG_DATADIR")
	if env:
		return env
	if not DATADIR.startswith("@"):
		return DATADIR
	# not installed: running from the source tree
	return os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "linux", "share", "mbrola-ng")


def userVoicesRoot():
	xdg = os.environ.get("XDG_DATA_HOME")
	base = xdg if xdg and xdg.startswith("/") else os.path.join(os.path.expanduser("~"), ".local", "share")
	return os.path.join(base, "mbrola-ng", "voices")


SYSTEM_VOICES = "/usr/local/share/mbrola-ng/voices"
DISTRO_VOICES = ("/usr/share/mbrola", "/usr/local/share/mbrola")


# ------------------------------------------------------------ translations
_HR = {
	"Installed voices:": "Instalirani glasovi:",
	"No voice is installed. Install one with: mbrola-ng-voices install hr":
		"Nijedan glas nije instaliran. Instalirajte ga naredbom: mbrola-ng-voices install hr",
	"user": "korisnik",
	"all users": "svi korisnici",
	"distribution package": "paket distribucije",
	"Voices in the catalog:": "Glasovi u katalogu:",
	"installed": "instaliran",
	"Unknown voice or language: {0}": "Nepoznat glas ili jezik: {0}",
	"Installing {0}...": "Instaliram {0}...",
	"Downloading {0} ({1})...": "Preuzimam {0} ({1})...",
	"download failed: {0}": "preuzimanje nije uspjelo: {0}",
	"{0} has the wrong size": "{0} ima pogrešnu veličinu",
	"{0} is damaged (checksum mismatch)": "{0} je oštećen (kontrolni zbroj se ne slaže)",
	"License of the voice {0}:": "Licenca glasa {0}:",
	"Do you accept the license? [y/N] ": "Prihvaćate li licencu? [d/N] ",
	"License not accepted, {0} was not installed.": "Licenca nije prihvaćena, {0} nije instaliran.",
	"The voice {0} is installed in {1}.": "Glas {0} instaliran je u {1}.",
	"The voice {0} could not be installed: {1}": "Glas {0} nije moguće instalirati: {1}",
	"The voice {0} is removed.": "Glas {0} je uklonjen.",
	"The voice {0} is not installed here.": "Glas {0} ovdje nije instaliran.",
	"The voice {0} belongs to a distribution package; remove it with the package manager.":
		"Glas {0} pripada paketu distribucije; uklonite ga upraviteljem paketa.",
	"--system needs root rights: run it with sudo.": "--system traži administratorska prava: pokrenite s sudo.",
	"All voices are up to date.": "Svi glasovi su ažurni.",
	"This file is not a voice known to this version of MBROLA NG ({0}).":
		"Ova datoteka nije glas poznat ovoj verziji MBROLA NG-a ({0}).",
	"not an MBROLA voice database": "nije baza MBROLA glasa",
	"the voice database is damaged": "baza glasa je oštećena",
	"the voice lacks phonemes: {0}": "glasu nedostaju fonemi: {0}",
	"unexpected sample rate {0}": "neočekivana frekvencija uzorkovanja {0}",
	"Restart Speech Dispatcher so that it sees the change:":
		"Ponovno pokrenite Speech Dispatcher da vidi promjenu:",
	"  killall speech-dispatcher   (it starts again by itself; then choose MBROLA NG in Orca: Speech, Speech synthesizer)":
		"  killall speech-dispatcher   (sam se ponovno pokreće; zatim u Orci odaberite MBROLA NG: Govor, Sintetizator govora)",
	"no languages found in {0} - is MBROLA NG installed?": "u {0} nema jezika - je li MBROLA NG instaliran?",
	"The voice {0} is already installed (use update to reinstall a changed voice).":
		"Glas {0} je već instaliran (za ponovnu instalaciju promijenjenog glasa koristite update).",
}


def _uiLang():
	for var in ("LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"):
		v = os.environ.get(var)
		if v:
			return v.split(":")[0].split(".")[0].split("_")[0].lower()
	return "en"


UI_LANG = _uiLang()


def _(text):
	return _HR.get(text, text) if UI_LANG == "hr" else text


def say(text=""):
	print(text, flush=True)


# ------------------------------------------------------------------ catalog
class VoiceError(Exception):
	pass


def _readJson(path):
	with open(path, "r", encoding="utf-8") as f:
		return json.load(f)


def _validCatalog(cat):
	return isinstance(cat, dict) and cat.get("schema") == CATALOG_SCHEMA and isinstance(cat.get("voices"), list)


def cacheDir():
	xdg = os.environ.get("XDG_CACHE_HOME")
	base = xdg if xdg and xdg.startswith("/") else os.path.join(os.path.expanduser("~"), ".cache")
	return os.path.join(base, "mbrola-ng")


def loadCatalog(path=None):
	"""Developer override (~/.local/share/mbrola-ng/catalog.json), online
	catalog (cached), cache, then the catalog installed with MBROLA NG."""
	if path:
		return _readJson(path)
	override = os.path.join(os.path.dirname(userVoicesRoot()), "catalog.json")
	candidates = [override]
	cache = os.path.join(cacheDir(), "catalog.json")
	if CATALOG_URL:
		try:
			with urllib.request.urlopen(CATALOG_URL, timeout=15) as r:
				cat = json.loads(r.read().decode("utf-8"))
			if _validCatalog(cat):
				os.makedirs(cacheDir(), exist_ok=True)
				with open(cache, "w", encoding="utf-8") as f:
					json.dump(cat, f, ensure_ascii=False, indent=1)
				return cat
		except Exception:
			pass
	candidates += [cache, os.path.join(dataDir(), "catalog.json")]
	for p in candidates:
		try:
			cat = _readJson(p)
			if _validCatalog(cat):
				return cat
		except Exception:
			pass
	return {"schema": CATALOG_SCHEMA, "languages": [], "voices": []}


def supportedLanguages():
	try:
		return {f[:-4] for f in os.listdir(os.path.join(dataDir(), "languages")) if f.endswith(".dat")}
	except OSError:
		return set()


def _versionTuple(v):
	out = []
	for part in str(v).split("."):
		try:
			out.append(int(part))
		except ValueError:
			out.append(0)
	return tuple(out)


def catalogVoices(cat):
	langs = supportedLanguages()
	return [v for v in cat.get("voices", []) if isinstance(v, dict) and v.get("language") in langs
		and _versionTuple(v.get("min_engine", "0")) <= _versionTuple(ENGINE_VERSION)]


def localized(names):
	if not isinstance(names, dict):
		return str(names or "")
	for key in (UI_LANG, "en"):
		if names.get(key):
			return names[key]
	return next(iter(names.values()), "")


def formatSize(n):
	return "%.1f MB" % (n / 1048576.0) if n >= 1048576 else "%d kB" % max(1, n // 1024)


# ------------------------------------------------------------- .voice files
def readVoiceFile(path):
	info = {}
	try:
		with open(path, "r", encoding="utf-8") as f:
			for line in f:
				line = line.strip()
				if line and not line.startswith("#") and "=" in line:
					k, v = line.split("=", 1)
					info[k.strip()] = v.strip()
	except OSError:
		pass
	return info


def _mapText(m):
	if not isinstance(m, dict):
		return ""
	return " ".join("%s=%s" % (k, v) for k, v in m.items() if k and v and " " not in k + v)


def writeVoiceFile(folder, entry):
	cfg = entry.get("voice_config", {})
	lines = [
		"# MBROLA NG voice description - written by mbrola-ng-voices",
		"id = %s" % entry["id"],
		"database = %s" % entry["id"],
		"language = %s" % entry.get("language", ""),
		"version = %s" % entry.get("version", ""),
		"gender = %s" % entry.get("gender", ""),
		"age = %s" % entry.get("age", ""),
		"base_pitch = %s" % cfg.get("base_pitch", 0),
		"pitch_range = %s" % cfg.get("pitch_range", 1.0),
		"rate_factor = %s" % cfg.get("rate_factor", 1.0),
		"volume = %s" % cfg.get("volume", 1.0),
		"phoneme_map = %s" % _mapText(cfg.get("phoneme_map")),
	]
	for k, v in entry.get("names", {}).items():
		lines.append("name_%s = %s" % (k, v))
	with open(os.path.join(folder, entry["id"] + ".voice"), "w", encoding="utf-8", newline="\n") as f:
		f.write("\n".join(lines) + "\n")


# --------------------------------------------------------- installed voices
def installedVoices(cat):
	"""[(id, database path, where, language)] in the module's search order."""
	catById = {v.get("id"): v for v in cat.get("voices", []) if isinstance(v, dict)}
	langs = supportedLanguages()
	result, seen = [], set()
	roots = [(userVoicesRoot(), "user"), (SYSTEM_VOICES, "system"), (os.path.join(dataDir(), "voices"), "system")]
	for root, where in roots:
		if not os.path.isdir(root):
			continue
		for vid in sorted(os.listdir(root)):
			db = os.path.join(root, vid, vid)
			if vid in seen or vid.startswith(".") or not os.path.isfile(db):
				continue
			lang = readVoiceFile(os.path.join(root, vid, vid + ".voice")).get("language") \
				or catById.get(vid, {}).get("language")
			if lang in langs:
				seen.add(vid)
				result.append((vid, db, where, lang))
	for root in DISTRO_VOICES:
		if not os.path.isdir(root):
			continue
		for vid in sorted(os.listdir(root)):
			p = os.path.join(root, vid)
			db = os.path.join(p, vid) if os.path.isfile(os.path.join(p, vid)) else p
			lang = catById.get(vid, {}).get("language")
			if vid not in seen and os.path.isfile(db) and lang in langs:
				seen.add(vid)
				result.append((vid, db, "distro", lang))
	return result


# ------------------------------------------------------------- verification
def sha256File(path):
	h = hashlib.sha256()
	with open(path, "rb") as f:
		for b in iter(lambda: f.read(1 << 16), b""):
			h.update(b)
	return h.hexdigest()


def readDatabaseInfo(path):
	"""MBROLA database header and diphone index: version, freq, phonemes."""
	with open(path, "rb") as f:
		d = f.read(4 << 20)
	if len(d) < 30 or d[:6] != b"MBROLA":
		raise VoiceError(_("not an MBROLA voice database"))
	try:
		p = 11
		nb, = struct.unpack_from("<h", d, p); p += 2
		old, = struct.unpack_from("<H", d, p); p += 2
		if old == 0:
			sizeMrk, = struct.unpack_from("<i", d, p); p += 4
		else:
			sizeMrk = old
		_sizeRaw, freq = struct.unpack_from("<ih", d, p); p += 6
		p += 2

		def zstr():
			nonlocal p
			e = d.index(b"\0", p)
			s = d[p:e].decode("latin-1")
			p = e + 1
			return s

		phonemes = set()
		pm = i = 0
		while pm != sizeMrk and i < nb:
			phonemes.update((zstr(), zstr()))
			p += 2
			pm += d[p]
			p += 2
			i += 1
	except (struct.error, ValueError, IndexError):
		raise VoiceError(_("the voice database is damaged"))
	return {"freq": freq, "phonemes": phonemes}


def verifyDatabase(path, entry):
	info = readDatabaseInfo(path)
	missing = sorted(set(str(entry.get("phonemes", "")).split()) - info["phonemes"])
	if missing:
		raise VoiceError(_("the voice lacks phonemes: {0}").format(" ".join(missing)))
	if entry.get("sample_rate") and int(entry["sample_rate"]) != info["freq"]:
		raise VoiceError(_("unexpected sample rate {0}").format(info["freq"]))


# ----------------------------------------------------------- install/remove
def targetRoot(system):
	if system:
		if os.geteuid() != 0:
			raise SystemExit(_("--system needs root rights: run it with sudo."))
		return SYSTEM_VOICES
	return userVoicesRoot()


def _registryPath(root):
	return os.path.join(root, "installed.json")


def _updateRegistry(root, vid, record=None):
	path = _registryPath(root)
	try:
		reg = _readJson(path)
		if not isinstance(reg.get("voices"), list):
			raise ValueError
	except Exception:
		reg = {"voices": []}
	reg["voices"] = [v for v in reg["voices"] if isinstance(v, dict) and v.get("id") != vid]
	if record:
		reg["voices"].append(record)
	tmp = path + ".tmp"
	with open(tmp, "w", encoding="utf-8") as f:
		json.dump(reg, f, ensure_ascii=False, indent=1)
	os.replace(tmp, path)


def placeVoice(root, entry, staged):
	"""Moves a verified staging folder into place atomically."""
	vid = entry["id"]
	writeVoiceFile(staged, entry)
	target = os.path.join(root, vid)
	if os.path.isdir(target):
		old = target + ".old"
		shutil.rmtree(old, ignore_errors=True)
		os.replace(target, old)
		os.replace(staged, target)
		shutil.rmtree(old, ignore_errors=True)
	else:
		os.replace(staged, target)
	if root == SYSTEM_VOICES:
		for dirpath, dirnames, filenames in os.walk(target):
			os.chmod(dirpath, 0o755)
			for fn in filenames:
				os.chmod(os.path.join(dirpath, fn), 0o644)
	_updateRegistry(root, vid, {
		"id": vid,
		"language": entry.get("language"),
		"version": entry.get("version"),
		"path": target,
		"sha256": sha256File(os.path.join(target, vid)),
		"installed": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
	})
	return target


def stagingFolder(root, vid):
	os.makedirs(root, exist_ok=True)
	return tempfile.mkdtemp(prefix=".download-%s-" % vid, dir=root)


def download(f, dest):
	lastError = None
	for url in f.get("urls", []):
		if not url.lower().startswith("https://"):
			continue
		try:
			with urllib.request.urlopen(url, timeout=30) as r, open(dest, "wb") as out:
				shutil.copyfileobj(r, out, 1 << 16)
			lastError = None
			break
		except Exception as e:
			lastError = e
	if lastError is not None or not os.path.isfile(dest):
		raise VoiceError(_("download failed: {0}").format(lastError))
	size = int(f.get("size", 0))
	if size and os.path.getsize(dest) != size:
		raise VoiceError(_("{0} has the wrong size").format(f["name"]))
	sha = str(f.get("sha256", ""))
	if len(sha) == 64 and sha256File(dest).lower() != sha.lower():
		raise VoiceError(_("{0} is damaged (checksum mismatch)").format(f["name"]))


def acceptLicense(entry, text, assumeYes):
	name = localized(entry.get("names", {}))
	say()
	say(_("License of the voice {0}:").format(name))
	summary = localized(entry.get("license", {}).get("summary", {}))
	if summary:
		say(summary)
		say()
	if text:
		say(text.strip())
		say()
	if assumeYes:
		return True
	try:
		answer = input(_("Do you accept the license? [y/N] ")).strip().lower()
	except EOFError:
		answer = ""
	return answer in ("y", "yes", "d", "da")


def installEntry(entry, root, assumeYes):
	vid = entry["id"]
	name = localized(entry.get("names", {}))
	say(_("Installing {0}...").format(name))
	staged = stagingFolder(root, vid)
	try:
		licName = entry.get("license", {}).get("file")
		files = entry.get("files", [])
		lic = [f for f in files if f.get("name") == licName]
		for f in lic:
			download(f, os.path.join(staged, f["name"]))
		text = ""
		if lic:
			with open(os.path.join(staged, licName), "r", encoding="utf-8", errors="replace") as fh:
				text = fh.read()
		if not acceptLicense(entry, text, assumeYes):
			say(_("License not accepted, {0} was not installed.").format(name))
			return False
		for f in files:
			if f.get("name") != licName:
				say(_("Downloading {0} ({1})...").format(f["name"], formatSize(int(f.get("size", 0)))))
				download(f, os.path.join(staged, f["name"]))
		verifyDatabase(os.path.join(staged, vid), entry)
		target = placeVoice(root, entry, staged)
		staged = None
		say(_("The voice {0} is installed in {1}.").format(name, target))
		return True
	except (VoiceError, OSError) as e:
		say(_("The voice {0} could not be installed: {1}").format(name, e))
		return False
	finally:
		if staged:
			shutil.rmtree(staged, ignore_errors=True)


def restartHint():
	say()
	say(_("Restart Speech Dispatcher so that it sees the change:"))
	say(_("  killall speech-dispatcher   (it starts again by itself; then choose MBROLA NG in Orca: Speech, Speech synthesizer)"))


# ----------------------------------------------------------------- commands
def cmdList(args, cat):
	voices = installedVoices(cat)
	if not voices:
		say(_("No voice is installed. Install one with: mbrola-ng-voices install hr"))
		return 0
	catById = {v.get("id"): v for v in cat.get("voices", [])}
	where = {"user": _("user"), "system": _("all users"), "distro": _("distribution package")}
	say(_("Installed voices:"))
	for vid, db, w, lang in voices:
		name = localized(catById.get(vid, {}).get("names", {})) or vid
		say("  %-5s %-4s %s - %s (%s)" % (vid, lang, name, where[w], db))
	return 0


def cmdAvailable(args, cat):
	installed = {v[0] for v in installedVoices(cat)}
	say(_("Voices in the catalog:"))
	for v in catalogVoices(cat):
		size = sum(int(f.get("size", 0)) for f in v.get("files", []))
		mark = " [%s]" % _("installed") if v["id"] in installed else ""
		say("  %-5s %-4s %s, %s%s" % (v["id"], v.get("language", ""), localized(v.get("names", {})), formatSize(size), mark))
	return 0


def _resolve(cat, names):
	voices = catalogVoices(cat)
	result = []
	for n in names:
		found = [v for v in voices if v["id"] == n] or [v for v in voices if v.get("language") == n][:1]
		if not found:
			raise SystemExit(_("Unknown voice or language: {0}").format(n))
		result += [v for v in found if v not in result]
	return result


def cmdInstall(args, cat):
	if not supportedLanguages():
		raise SystemExit(_("no languages found in {0} - is MBROLA NG installed?").format(os.path.join(dataDir(), "languages")))
	root = targetRoot(args.system)
	installed = {v[0] for v in installedVoices(cat) if v[2] != "distro"}
	ok = True
	changed = False
	for entry in _resolve(cat, args.voices):
		if entry["id"] in installed and os.path.isdir(os.path.join(root, entry["id"])):
			say(_("The voice {0} is already installed (use update to reinstall a changed voice).").format(entry["id"]))
			continue
		if installEntry(entry, root, args.yes):
			changed = True
		else:
			ok = False
	if changed:
		restartHint()
	return 0 if ok else 1


def cmdInstallFile(args, cat):
	root = targetRoot(args.system)
	known = {v["id"]: v for v in catalogVoices(cat)}
	path = args.file
	tmp = tempfile.mkdtemp(prefix="mbrola_ng_")
	try:
		db = None
		if zipfile.is_zipfile(path):
			with zipfile.ZipFile(path) as z:
				for info in z.infolist():
					base = os.path.basename(info.filename)
					if base in known or base.lower() in ("license.txt", "readme.txt"):
						with z.open(info) as src, open(os.path.join(tmp, base), "wb") as dst:
							shutil.copyfileobj(src, dst)
						if base in known:
							db = os.path.join(tmp, base)
		elif os.path.basename(path) in known:
			db = path
			for extra in ("license.txt", "README.txt"):
				p = os.path.join(os.path.dirname(os.path.abspath(path)), extra)
				if os.path.isfile(p):
					shutil.copy2(p, os.path.join(tmp, extra))
		if not db:
			raise SystemExit(_("This file is not a voice known to this version of MBROLA NG ({0}).").format(
				", ".join(sorted(known)) or "-"))
		entry = known[os.path.basename(db)]
		name = localized(entry.get("names", {}))
		verifyDatabase(db, entry)
		licName = entry.get("license", {}).get("file") or "license.txt"
		text = ""
		if os.path.isfile(os.path.join(tmp, licName)):
			with open(os.path.join(tmp, licName), "r", encoding="utf-8", errors="replace") as fh:
				text = fh.read()
		if not acceptLicense(entry, text, args.yes):
			say(_("License not accepted, {0} was not installed.").format(name))
			return 1
		staged = stagingFolder(root, entry["id"])
		try:
			shutil.copy2(db, os.path.join(staged, entry["id"]))
			for extra in os.listdir(tmp):
				if extra != entry["id"]:
					shutil.copy2(os.path.join(tmp, extra), os.path.join(staged, extra))
			target = placeVoice(root, entry, staged)
		except Exception:
			shutil.rmtree(staged, ignore_errors=True)
			raise
		say(_("The voice {0} is installed in {1}.").format(name, target))
		restartHint()
		return 0
	except (VoiceError, OSError) as e:
		say(_("The voice {0} could not be installed: {1}").format(path, e))
		return 1
	finally:
		shutil.rmtree(tmp, ignore_errors=True)


def cmdRemove(args, cat):
	root = targetRoot(args.system)
	ok = True
	for vid in args.voices:
		folder = os.path.join(root, vid)
		if os.path.isdir(folder) and os.path.isfile(os.path.join(folder, vid)):
			shutil.rmtree(folder)
			_updateRegistry(root, vid)
			say(_("The voice {0} is removed.").format(vid))
		elif any(v[0] == vid and v[2] == "distro" for v in installedVoices(cat)):
			say(_("The voice {0} belongs to a distribution package; remove it with the package manager.").format(vid))
			ok = False
		else:
			say(_("The voice {0} is not installed here.").format(vid))
			ok = False
	if ok:
		restartHint()
	return 0 if ok else 1


def cmdUpdate(args, cat):
	root = targetRoot(args.system)
	known = {v["id"]: v for v in catalogVoices(cat)}
	changed = False
	ok = True
	for vid, db, where, lang in installedVoices(cat):
		entry = known.get(vid)
		if not entry or os.path.dirname(os.path.dirname(db)) != root:
			continue
		info = readVoiceFile(os.path.join(os.path.dirname(db), vid + ".voice"))
		files = [f for f in entry.get("files", []) if f.get("name") == vid]
		sha = str(files[0].get("sha256", "")).lower() if files else ""
		if info.get("version") == str(entry.get("version")) and (not sha or sha256File(db) == sha):
			continue
		if installEntry(entry, root, args.yes):
			changed = True
		else:
			ok = False
	if changed:
		restartHint()
	else:
		say(_("All voices are up to date."))
	return 0 if ok else 1


def cmdWriteVoiceFiles(args, cat):
	"""Build helper: <id>.voice of every catalog voice (the module uses them
	for voices of distribution packages, which come without one)."""
	os.makedirs(args.dir, exist_ok=True)
	for v in cat.get("voices", []):
		if isinstance(v, dict) and v.get("id"):
			writeVoiceFile(args.dir, v)
	return 0


def main(argv=None):
	try:
		locale.setlocale(locale.LC_ALL, "")
	except locale.Error:
		pass
	p = argparse.ArgumentParser(prog="mbrola-ng-voices", description="MBROLA NG voice manager " + VERSION)
	p.add_argument("--catalog", help=argparse.SUPPRESS)
	sub = p.add_subparsers(dest="cmd", required=True)
	sub.add_parser("list", help="installed voices")
	sub.add_parser("available", help="voices in the catalog")
	for name, helptext in (("install", "install voices (voice id or language code)"),
			("remove", "remove voices"), ("update", "reinstall voices changed in the catalog"),
			("install-file", "install from a database file or a zip")):
		sp = sub.add_parser(name, help=helptext)
		if name in ("install", "remove"):
			sp.add_argument("voices", nargs="+")
		if name == "install-file":
			sp.add_argument("file")
		sp.add_argument("--system", action="store_true", help="for all users (sudo)")
		if name != "remove":
			sp.add_argument("--yes", "-y", action="store_true", help="accept the voice licenses")
	w = sub.add_parser("write-voice-files", help=argparse.SUPPRESS)
	w.add_argument("dir")
	args = p.parse_args(argv)
	cat = loadCatalog(args.catalog)
	return {
		"list": cmdList, "available": cmdAvailable, "install": cmdInstall, "install-file": cmdInstallFile,
		"remove": cmdRemove, "update": cmdUpdate, "write-voice-files": cmdWriteVoiceFiles,
	}[args.cmd](args, cat)


if __name__ == "__main__":
	sys.exit(main())
