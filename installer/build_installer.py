# MBROLA NG - builds the Windows (SAPI 5) installer
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   python installer\build_installer.py [--no-build]
#
# 1. builds x64 and x86 (build.cmd)
# 2. stages installer\build\stage: binaries, languages, licenses, catalog
# 3. compiles installer\mbrola_ng.iss with Inno Setup 6 (ISCC.exe) into
#    installer\MBROLA_NG-<version>-setup.exe
import argparse
import os
import shutil
import subprocess
import sys

VERSION = "0.1.0"

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STAGE = os.path.join(HERE, "build", "stage")

sys.path.insert(0, os.path.join(ROOT, "nvda"))
import build_addon  # noqa: E402  (shared build + license helpers)

README = (
	"MBROLA NG - Copyright (c) 2026 Darko Milosevic.\n"
	"MBROLA NG is free software under the GNU General Public License,\n"
	"version 2 or (at your option) any later version: see LICENSE.txt.\n\n"
	"mbrola_ng_synth.exe is a separate program: the MBROLA synthesizer\n"
	"(Copyright (c) Faculte Polytechnique de Mons - TCTS lab) with a small\n"
	"pipe front end, licensed under the GNU AGPL v3\n"
	"(AGPL-3.0-mbrola_ng_synth.txt). MBROLA_NG.dll starts it as its own\n"
	"process and talks to it through a pipe.\n\n"
	"Emoji names (Croatian, English): Unicode CLDR, Copyright (c) 1991-2025\n"
	"Unicode, Inc., Unicode License v3 (https://www.unicode.org/license.txt).\n\n"
	"English pronunciations: CMU Pronouncing Dictionary, Copyright (C) 1993-2015\n"
	"Carnegie Mellon University. All rights reserved. BSD-style license:\n"
	"see CMUDICT-LICENSE.txt.\n\n"
	"Voices are not part of this installer: they are downloaded from the\n"
	"official MBROLA voice repository (https://github.com/numediart/MBROLA-voices)\n"
	"when you select them. Each voice has its own license, saved next to\n"
	"the voice in %ProgramData%\\MBROLA NG\\voices\\<voice>. The MBROLA voices\n"
	"may be used only with the MBROLA program and not commercially.\n")


def find_iscc():
	for base in (os.environ.get("LOCALAPPDATA"), os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles")):
		if not base:
			continue
		for sub in (os.path.join("Programs", "Inno Setup 6"), "Inno Setup 6"):
			exe = os.path.join(base, sub, "ISCC.exe")
			if os.path.isfile(exe):
				return exe
	exe = shutil.which("ISCC")
	if exe:
		return exe
	raise SystemExit("Inno Setup 6 (ISCC.exe) not found - winget install JRSoftware.InnoSetup")


def stage():
	if os.path.isdir(STAGE):
		shutil.rmtree(STAGE)
	for arch in ("x64", "x86"):
		out = os.path.join(STAGE, arch)
		os.makedirs(out)
		for name in ("MBROLA_NG.dll", "mbrola_ng_synth.exe"):
			shutil.copy2(os.path.join(ROOT, "build", arch, name), out)
	langs = os.path.join(STAGE, "languages")
	os.makedirs(langs)
	codes = sorted(os.listdir(os.path.join(ROOT, "languages")))
	for code in codes:
		dat = os.path.join(ROOT, "build", "x64", code + ".dat")
		if not os.path.isfile(dat):
			raise SystemExit("missing %s - did the language compile?" % dat)
		shutil.copy2(dat, langs)
	shutil.copy2(os.path.join(ROOT, "catalog", "catalog.json"), STAGE)
	lic = os.path.join(STAGE, "licenses")
	build_addon.write_licenses(lic, README)
	return codes


def compile_setup(codes):
	iscc = find_iscc()
	cmd = [iscc, "/Q", "/DAppVersion=" + VERSION, "/DStage=" + STAGE, "/DLanguages=," + ",".join(codes) + ",",
		os.path.join(HERE, "mbrola_ng.iss")]
	subprocess.run(cmd, check=True, cwd=HERE)
	target = os.path.join(HERE, "MBROLA_NG-%s-setup.exe" % VERSION)
	print("wrote", target)
	return target


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--no-build", action="store_true")
	a = ap.parse_args()
	if not a.no_build:
		build_addon.run_build("x64")
		build_addon.run_build("x86")
	compile_setup(stage())


if __name__ == "__main__":
	sys.exit(main())
