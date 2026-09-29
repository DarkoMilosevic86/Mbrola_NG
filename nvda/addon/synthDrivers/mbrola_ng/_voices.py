# MBROLA NG - voices: catalog, installed voices, verification (ANALYSIS 10.4-10.9)
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Shared by the synthesizer driver and the Voice Manager. No NVDA GUI code.

import datetime
import hashlib
import json
import os
import shutil
import struct

_HERE = os.path.dirname(os.path.abspath(__file__))
LANG_DIR = os.path.join(_HERE, "languages")
BUNDLED_CATALOG = os.path.join(_HERE, "catalog.json")

# Online catalog (HTTPS). The final location is decided at release time
# (10.5); until then the catalog bundled with the add-on is used. A file
# <NVDA config>\mbrola_ng\catalog.json overrides both (developer testing).
CATALOG_URL = None
CATALOG_SCHEMA = 1
ENGINE_VERSION = "0.1"


def _configPath():
	import globalVars
	return globalVars.appArgs.configPath


def dataRoot():
	return os.path.join(_configPath(), "mbrola_ng")


def userVoicesRoot():
	"""Voices installed by this NVDA user (never inside the add-on folder,
	which NVDA replaces on every add-on update)."""
	return os.path.join(dataRoot(), "voices")


def systemVoicesRoot():
	"""Voices of a system-wide MBROLA NG installation (read-only here)."""
	pd = os.environ.get("PROGRAMDATA")
	return os.path.join(pd, "MBROLA NG", "voices") if pd else None


def supportedLanguages():
	"""Language codes whose compiled .dat is part of this add-on."""
	try:
		return {f[:-4] for f in os.listdir(LANG_DIR) if f.endswith(".dat")}
	except OSError:
		return set()


# ------------------------------------------------------------------ catalog
def _validCatalog(cat):
	return isinstance(cat, dict) and cat.get("schema") == CATALOG_SCHEMA and isinstance(cat.get("voices"), list)


def _readJson(path):
	with open(path, "r", encoding="utf-8") as f:
		return json.load(f)


def loadCatalog(online=True, timeout=15):
	"""Returns (catalog, source). Online catalog when available (cached for
	offline use), else the developer override, the cache or the bundled one."""
	override = os.path.join(dataRoot(), "catalog.json")
	if os.path.isfile(override):
		try:
			cat = _readJson(override)
			if _validCatalog(cat):
				return cat, "override"
		except Exception:
			pass
	cache = os.path.join(dataRoot(), "catalog-cache.json")
	if online and CATALOG_URL:
		try:
			import urllib.request
			with urllib.request.urlopen(CATALOG_URL, timeout=timeout) as r:
				cat = json.loads(r.read().decode("utf-8"))
			if _validCatalog(cat):
				os.makedirs(dataRoot(), exist_ok=True)
				with open(cache, "w", encoding="utf-8") as f:
					json.dump(cat, f, ensure_ascii=False, indent=1)
				return cat, "online"
		except Exception:
			pass
	for path, source in ((cache, "cache"), (BUNDLED_CATALOG, "bundled")):
		try:
			cat = _readJson(path)
			if _validCatalog(cat):
				return cat, source
		except Exception:
			pass
	return {"schema": CATALOG_SCHEMA, "languages": [], "voices": []}, "none"


def _versionTuple(v):
	out = []
	for part in str(v).split("."):
		try:
			out.append(int(part))
		except ValueError:
			out.append(0)
	return tuple(out)


def catalogVoices(cat):
	"""Catalog voices usable by this add-on (language present, engine new enough)."""
	langs = supportedLanguages()
	result = []
	for v in cat.get("voices", []):
		if not isinstance(v, dict) or v.get("language") not in langs:
			continue
		if _versionTuple(v.get("min_engine", "0")) > _versionTuple(ENGINE_VERSION):
			continue
		result.append(v)
	return result


def localized(names, uiLang):
	"""Picks a name from {"en": ..., "hr": ...} for the UI language."""
	if not isinstance(names, dict):
		return str(names or "")
	lang = (uiLang or "en").replace("-", "_")
	for key in (lang, lang.split("_")[0], "en"):
		if names.get(key):
			return names[key]
	return next(iter(names.values()), "")


def voiceSize(entry):
	return sum(int(f.get("size", 0)) for f in entry.get("files", []))


# --------------------------------------------------------- installed voices
def _registryPath():
	return os.path.join(userVoicesRoot(), "installed.json")


def _readRegistry():
	try:
		reg = _readJson(_registryPath())
		if isinstance(reg, dict) and isinstance(reg.get("voices"), list):
			return reg
	except Exception:
		pass
	return {"voices": []}


def _writeRegistry(reg):
	os.makedirs(userVoicesRoot(), exist_ok=True)
	tmp = _registryPath() + ".tmp"
	with open(tmp, "w", encoding="utf-8") as f:
		json.dump(reg, f, ensure_ascii=False, indent=1)
	os.replace(tmp, _registryPath())


def readVoiceFile(path):
	"""<id>.voice: 'key = value' lines (10.3)."""
	info = {}
	try:
		with open(path, "r", encoding="utf-8") as f:
			for line in f:
				line = line.strip()
				if not line or line.startswith("#") or "=" not in line:
					continue
				k, v = line.split("=", 1)
				info[k.strip()] = v.strip()
	except OSError:
		pass
	return info


def _mapText(m):
	"""{"A": "A:", ...} -> "A=A: ..." (the form the core C API takes)."""
	if not isinstance(m, dict):
		return ""
	return " ".join("%s=%s" % (k, v) for k, v in m.items() if k and v and " " not in k + v)


def writeVoiceFile(folder, entry):
	cfg = entry.get("voice_config", {})
	names = entry.get("names", {})
	lines = [
		"# MBROLA NG voice description - written by the Voice Manager",
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
	for k, v in names.items():
		lines.append("name_%s = %s" % (k, v))
	with open(os.path.join(folder, entry["id"] + ".voice"), "w", encoding="utf-8", newline="\r\n") as f:
		f.write("\n".join(lines) + "\n")


def installedVoices(cat=None):
	"""All usable installed voices: user voices first, then system voices.
	Each: dict(id, language, version, folder, db, size, system, info)."""
	langs = supportedLanguages()
	catById = {v.get("id"): v for v in (cat or {}).get("voices", []) if isinstance(v, dict)}
	regById = {v.get("id"): v for v in _readRegistry()["voices"] if isinstance(v, dict)}
	result = []
	seen = set()
	for root, system in ((userVoicesRoot(), False), (systemVoicesRoot(), True)):
		if not root or not os.path.isdir(root):
			continue
		for vid in sorted(os.listdir(root)):
			folder = os.path.join(root, vid)
			db = os.path.join(folder, vid)
			if vid in seen or not os.path.isfile(db):
				continue
			info = readVoiceFile(os.path.join(folder, vid + ".voice"))
			ce = catById.get(vid, {})
			lang = info.get("language") or ce.get("language")
			if lang not in langs:
				continue
			reg = regById.get(vid, {}) if not system else {}
			seen.add(vid)
			result.append({
				"id": vid,
				"language": lang,
				"version": info.get("version") or reg.get("version") or "",
				"folder": folder,
				"db": db,
				"size": os.path.getsize(db),
				"system": system,
				"info": info,
				"catalog": ce,
			})
	return result


def voiceNames(voice):
	"""{'en': ..., 'hr': ...} for an installed voice."""
	names = {k[5:]: v for k, v in voice["info"].items() if k.startswith("name_")}
	if not names:
		names = voice["catalog"].get("names", {}) or {"en": voice["id"]}
	return names


def basePitch(voice):
	for src in (voice["info"].get("base_pitch"), voice["catalog"].get("voice_config", {}).get("base_pitch")):
		try:
			v = int(float(src))
			if v > 0:
				return v
		except (TypeError, ValueError):
			pass
	return 0


def phonemeMap(voice):
	"""Language->voice symbol map of an installed voice ("A=A: E=e ...")."""
	if "phoneme_map" in voice["info"]:
		return voice["info"]["phoneme_map"]
	return _mapText(voice["catalog"].get("voice_config", {}).get("phoneme_map"))


# ------------------------------------------------------------- verification
class VoiceError(Exception):
	pass


def sha256File(path, progress=None):
	h = hashlib.sha256()
	with open(path, "rb") as f:
		while True:
			b = f.read(1 << 16)
			if not b:
				break
			h.update(b)
	return h.hexdigest()


def readDatabaseInfo(path):
	"""Parses an MBROLA database header and diphone index. Returns
	dict(version, freq, diphones, real, phonemes)."""
	with open(path, "rb") as f:
		d = f.read(4 << 20 if os.path.getsize(path) > (4 << 20) else -1)
	if len(d) < 30 or d[:6] != b"MBROLA":
		raise VoiceError("not an MBROLA voice database")
	try:
		version = d[6:11].decode("ascii", "replace")
		p = 11
		nb, = struct.unpack_from("<h", d, p); p += 2
		old, = struct.unpack_from("<H", d, p); p += 2
		if old == 0:
			sizeMrk, = struct.unpack_from("<i", d, p); p += 4
		else:
			sizeMrk = old
		_sizeRaw, freq = struct.unpack_from("<ih", d, p); p += 6
		p += 2  # MBRPeriod, Coding

		def zstr():
			nonlocal p
			e = d.index(b"\0", p)
			s = d[p:e].decode("latin-1")
			p = e + 1
			return s

		phonemes = set()
		pm = 0
		i = 0
		while pm != sizeMrk and i < nb:
			left, right = zstr(), zstr()
			p += 2
			pm += d[p]
			p += 2
			phonemes.update((left, right))
			i += 1
		real = i
	except (struct.error, ValueError, IndexError):
		raise VoiceError("the voice database is damaged")
	return {"version": version, "freq": freq, "diphones": nb, "real": real, "phonemes": phonemes}


def verifyDatabase(path, entry):
	"""Checks header and that every phoneme the language needs exists (10.7)."""
	info = readDatabaseInfo(path)
	needed = set(str(entry.get("phonemes", "")).split())
	missing = sorted(needed - info["phonemes"])
	if missing:
		raise VoiceError("the voice lacks phonemes: " + " ".join(missing))
	if entry.get("sample_rate") and int(entry["sample_rate"]) != info["freq"]:
		raise VoiceError("unexpected sample rate %d" % info["freq"])
	return info


# ------------------------------------------------------------ install/remove
def installFolder(entry, sourceFolder, sha):
	"""Moves a downloaded & verified voice folder into place atomically
	and records it in installed.json."""
	root = userVoicesRoot()
	os.makedirs(root, exist_ok=True)
	vid = entry["id"]
	writeVoiceFile(sourceFolder, entry)
	target = os.path.join(root, vid)
	if os.path.isdir(target):
		old = target + ".old"
		if os.path.isdir(old):
			shutil.rmtree(old, ignore_errors=True)
		os.replace(target, old)
		os.replace(sourceFolder, target)
		shutil.rmtree(old, ignore_errors=True)
	else:
		os.replace(sourceFolder, target)
	reg = _readRegistry()
	reg["voices"] = [v for v in reg["voices"] if v.get("id") != vid]
	reg["voices"].append({
		"id": vid,
		"language": entry.get("language"),
		"version": entry.get("version"),
		"path": target,
		"sha256": sha,
		"installed": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
	})
	_writeRegistry(reg)
	return target


def removeVoice(vid):
	folder = os.path.join(userVoicesRoot(), vid)
	if os.path.isdir(folder):
		shutil.rmtree(folder)
	reg = _readRegistry()
	reg["voices"] = [v for v in reg["voices"] if v.get("id") != vid]
	_writeRegistry(reg)


def tempFolder(vid):
	root = userVoicesRoot()
	os.makedirs(root, exist_ok=True)
	folder = os.path.join(root, ".download-" + vid)
	if os.path.isdir(folder):
		shutil.rmtree(folder, ignore_errors=True)
	os.makedirs(folder)
	return folder
