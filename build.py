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
#   android   Android app (in development, ANALYSIS section 14)
#   clean     removes every build output
# options:
#   --arch x64|x86|both   Windows architectures for core (default: x64;
#                         nvda and sapi always build both)
#   --debug               debug build of core
#   --install user|system Linux: install for this user (~/.local, no root)
#                         or for all users (sudo)
#   --deb                 Linux: also make a .deb package (-> dist/)
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


def build_android(a, explicit):
	msg = ("Android: in development (ANALYSIS section 14) - there is nothing to build yet.")
	if explicit:
		raise SystemExit(msg)
	print(msg)


def clean():
	for rel in ("build", "dist", os.path.join("nvda", "build"), os.path.join("installer", "build")):
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
