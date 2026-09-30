#!/usr/bin/env python3
# MBROLA NG - one build command for every target
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   python build.py [targets] [options]
#
# targets (default: all):
#   all       every target that can be built on this system
#   core      engine, synthesizer, tools, compiled + tested languages
#   nvda      NVDA add-on           (Windows)  -> dist/mbrolaNG-<ver>.nvda-addon
#   sapi      SAPI 5 installer      (Windows, Inno Setup 6) -> dist/MBROLA_NG-<ver>-setup.exe
#   orca      Speech Dispatcher module + mbrola-ng-voices (Linux)
#   android   Android app (needs the Android SDK + NDK and JDK 17)
#             -> dist/MBROLA_NG-<ver>.apk and .aab (signed when
#             android/keystore.properties exists, see android/create_keystore.py),
#             otherwise dist/MBROLA_NG-<ver>-debug.apk
#   clean     removes every build output
# options:
#   --arch x64|x86|both   Windows architectures for core (default: x64;
#                         nvda and sapi always build both)
#   --debug               debug build of core
#   --install user|system Linux: install for this user (~/.local, no root)
#                         or for all users (sudo)
#   --deb                 Linux: also make a .deb package (-> dist/)
#   --prepare             android: only put the language data, catalog into
#                         android/generated (then build in Android Studio)
#
# The platform scripts stay usable on their own: build.cmd (Windows core),
# nvda/build_addon.py, installer/build_installer.py, linux/build.sh.
import argparse
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.join(ROOT, "dist")
WINDOWS = sys.platform == "win32"
LINUX = sys.platform.startswith("linux")

TARGETS = ("all", "core", "nvda", "sapi", "orca", "android", "clean")


def run(cmd, **kw):
	print("+", " ".join(cmd), flush=True)
	subprocess.run(cmd, check=True, cwd=ROOT, **kw)


def to_dist(pattern):
	os.makedirs(DIST, exist_ok=True)
	for f in glob.glob(os.path.join(ROOT, pattern)):
		shutil.copy2(f, DIST)
		print("->", os.path.relpath(os.path.join(DIST, os.path.basename(f)), ROOT))


def need(platform_ok, target, where):
	if not platform_ok:
		raise SystemExit("%s can only be built on %s." % (target, where))


# ------------------------------------------------------------------ targets
def build_core(a):
	if WINDOWS:
		archs = ("x64", "x86") if a.arch == "both" else (a.arch,)
		for arch in archs:
			run(["cmd", "/c", os.path.join(ROOT, "build.cmd"), arch, "Debug" if a.debug else "Release"])
	elif LINUX:
		run(["sh", os.path.join(ROOT, "linux", "build.sh")])
	else:
		build_type = "Debug" if a.debug else "Release"
		out = os.path.join(ROOT, "build", "native")
		run(["cmake", "-S", ROOT, "-B", out, "-DCMAKE_BUILD_TYPE=" + build_type])
		run(["cmake", "--build", out])


def build_windows_both(a, done):
	"""nvda and sapi ship x64 and x86: build both once."""
	if "win-both" not in done:
		for arch in ("x64", "x86"):
			run(["cmd", "/c", os.path.join(ROOT, "build.cmd"), arch])
		done.add("win-both")


def build_nvda(a, done):
	need(WINDOWS, "The NVDA add-on", "Windows")
	build_windows_both(a, done)
	run([sys.executable, os.path.join(ROOT, "nvda", "build_addon.py"), "--no-build"])
	to_dist("nvda/*.nvda-addon")


def build_sapi(a, done):
	need(WINDOWS, "The SAPI 5 installer", "Windows")
	build_windows_both(a, done)
	run([sys.executable, os.path.join(ROOT, "installer", "build_installer.py"), "--no-build"])
	to_dist("installer/*-setup.exe")


def build_orca(a):
	need(LINUX, "The Speech Dispatcher module (Orca)", "Linux")
	script = os.path.join(ROOT, "linux", "build.sh")
	if a.install:
		run(["sh", script, "--" + a.install])
	else:
		run(["sh", script])
	if a.deb:
		run(["sh", script, "--deb"])
		to_dist("build/linux-deb/*.deb")


def find_dir(env_names, candidates):
	for n in env_names:
		v = os.environ.get(n)
		if v and os.path.isdir(v):
			return v
	for pattern in candidates:
		found = sorted(glob.glob(os.path.expanduser(os.path.expandvars(pattern))))
		if found:
			return found[-1]
	return None


def android_environment():
	"""JAVA_HOME / ANDROID_HOME for Gradle, or None when something is missing."""
	local = os.environ.get("LOCALAPPDATA", "")
	sdk = find_dir(("ANDROID_HOME", "ANDROID_SDK_ROOT"),
		(os.path.join(local, "Android", "Sdk"), "~/Android/Sdk", "~/Library/Android/sdk"))
	jdk = find_dir(("JAVA_HOME",),
		(os.path.join(local, "Android", "jdk-17*"), "C:/Program Files/Android/Android Studio/jbr",
		"C:/Program Files/Eclipse Adoptium/jdk-17*", "C:/Program Files/Java/jdk-17*",
		"/usr/lib/jvm/java-17-openjdk*", "/usr/lib/jvm/jdk-17*"))
	if not sdk or not jdk:
		return None
	env = dict(os.environ)
	env["ANDROID_HOME"] = sdk
	env["JAVA_HOME"] = jdk
	return env


def prepare_android_assets(a):
	"""Language .dat files (compiled by the desktop build) and the voice
	catalog -> android/generated/assets (an assets folder of the app)."""
	if WINDOWS:
		out = os.path.join(ROOT, "build", "x64")
		if not glob.glob(os.path.join(out, "*.dat")):
			run(["cmd", "/c", os.path.join(ROOT, "build.cmd"), "x64"])
	else:
		out = os.path.join(ROOT, "build", "linux", "share", "mbrola-ng", "languages")
		if not glob.glob(os.path.join(out, "*.dat")):
			run(["sh", os.path.join(ROOT, "linux", "build.sh")])
	assets = os.path.join(ROOT, "android", "generated", "assets")
	langs = os.path.join(assets, "languages")
	if os.path.isdir(assets):
		shutil.rmtree(assets)
	os.makedirs(langs)
	for code in sorted(os.listdir(os.path.join(ROOT, "languages"))):
		dat = os.path.join(out, code + ".dat")
		if not os.path.isfile(dat):
			raise SystemExit("missing %s - did the language compile?" % dat)
		shutil.copy2(dat, langs)
	shutil.copy2(os.path.join(ROOT, "catalog", "catalog.json"), assets)
	print("->", os.path.relpath(assets, ROOT))


def build_android(a, explicit):
	env = android_environment()
	if env is None:
		msg = ("Android: the Android SDK (ANDROID_HOME) or JDK 17 (JAVA_HOME) was not found - skipped.")
		if explicit:
			raise SystemExit(msg)
		print(msg)
		return
	prepare_android_assets(a)
	if a.prepare:
		return
	project = os.path.join(ROOT, "android")
	with open(os.path.join(project, "local.properties"), "w", encoding="utf-8") as f:
		f.write("sdk.dir=%s\n" % env["ANDROID_HOME"].replace("\\", "/"))
	gradlew = os.path.join(project, "gradlew.bat" if WINDOWS else "gradlew")
	signed = os.path.isfile(os.path.join(project, "keystore.properties"))
	tasks = [":app:assembleRelease", ":app:bundleRelease"] if signed else [":app:assembleDebug"]
	cmd = [gradlew] if WINDOWS else ["sh", gradlew]
	print("+", " ".join(cmd + tasks), flush=True)
	subprocess.run(cmd + tasks, check=True, cwd=project, env=env)
	os.makedirs(DIST, exist_ok=True)
	version = android_version()
	outputs = os.path.join(project, "app", "build", "outputs")
	if signed:
		pairs = [(os.path.join(outputs, "apk", "release", "app-release.apk"), "MBROLA_NG-%s.apk" % version),
			(os.path.join(outputs, "bundle", "release", "app-release.aab"), "MBROLA_NG-%s.aab" % version)]
	else:
		pairs = [(os.path.join(outputs, "apk", "debug", "app-debug.apk"), "MBROLA_NG-%s-debug.apk" % version)]
		print("No android/keystore.properties: built a DEBUG apk only (for testing).\n"
			"For Google Play run `python android/create_keystore.py` once, then build again.")
	for src, name in pairs:
		shutil.copy2(src, os.path.join(DIST, name))
		print("->", os.path.join("dist", name))


def android_version():
	with open(os.path.join(ROOT, "android", "app", "build.gradle.kts"), encoding="utf-8") as f:
		for line in f:
			if "versionName" in line:
				return line.split('"')[1]
	return "0"


def clean():
	# a running Gradle daemon keeps files of android/app/build open
	project = os.path.join(ROOT, "android")
	env = android_environment()
	if env and os.path.isdir(os.path.join(project, ".gradle")):
		gradlew = os.path.join(project, "gradlew.bat" if WINDOWS else "gradlew")
		subprocess.run(([gradlew] if WINDOWS else ["sh", gradlew]) + ["--stop"], cwd=project, env=env,
			stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	for rel in ("build", "dist", os.path.join("nvda", "build"), os.path.join("installer", "build"),
			os.path.join("android", "generated"), os.path.join("android", "build"),
			os.path.join("android", "app", "build"), os.path.join("android", "app", ".cxx"),
			os.path.join("android", ".gradle"), os.path.join("android", ".kotlin")):
		p = os.path.join(ROOT, rel)
		if os.path.isdir(p):
			print("removing", rel)
			shutil.rmtree(p)
	for pattern in ("nvda/*.nvda-addon", "installer/*-setup.exe", "*.wav"):
		for f in glob.glob(os.path.join(ROOT, pattern)):
			print("removing", os.path.relpath(f, ROOT))
			os.remove(f)
	for dirpath, dirnames, _files in os.walk(ROOT):
		if ".git" in dirnames:
			dirnames.remove(".git")
		if "__pycache__" in dirnames:
			shutil.rmtree(os.path.join(dirpath, "__pycache__"))
			dirnames.remove("__pycache__")


def main():
	ap = argparse.ArgumentParser(description="MBROLA NG build",
		formatter_class=argparse.RawDescriptionHelpFormatter,
		epilog="targets: " + ", ".join(TARGETS))
	ap.add_argument("targets", nargs="*", default=["all"], metavar="target")
	ap.add_argument("--arch", choices=("x64", "x86", "both"), default="x64")
	ap.add_argument("--debug", action="store_true")
	ap.add_argument("--install", choices=("user", "system"))
	ap.add_argument("--deb", action="store_true")
	ap.add_argument("--prepare", action="store_true")
	a = ap.parse_args()
	for t in a.targets:
		if t not in TARGETS:
			ap.error("unknown target %r (choose from %s)" % (t, ", ".join(TARGETS)))

	targets = list(a.targets)
	if "clean" in targets:
		clean()
		targets = [t for t in targets if t != "clean"]
		if not targets:
			return 0
	if "all" in targets:
		targets = ["core"] + (["nvda", "sapi"] if WINDOWS else []) + (["orca"] if LINUX else []) + ["android"]

	done = set()
	for t in targets:
		print("=== %s ===" % t, flush=True)
		if t == "core":
			if LINUX and "orca" in targets:
				continue  # orca builds and tests the core itself
			build_core(a)
		elif t == "nvda":
			build_nvda(a, done)
		elif t == "sapi":
			build_sapi(a, done)
		elif t == "orca":
			build_orca(a)
		elif t == "android":
			build_android(a, explicit="all" not in a.targets)
	print("done.")
	return 0


if __name__ == "__main__":
	try:
		sys.exit(main())
	except subprocess.CalledProcessError as e:
		sys.exit("build failed: %s" % e)
