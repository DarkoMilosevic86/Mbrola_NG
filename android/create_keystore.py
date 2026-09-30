#!/usr/bin/env python3
# MBROLA NG - creates the key that signs the Android app (run it ONCE)
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   python android/create_keystore.py
#
# Asks for your name, city, country and a password, then writes
#   android/mbrola-ng-upload.jks   the keystore (your private signing key)
#   android/keystore.properties    its path, alias and passwords for Gradle
# Both are ignored by git and must NEVER be published. KEEP A BACKUP of both
# (and remember the password): an app on Google Play can only be updated with
# the same key. With Play App Signing this is your "upload key"; Google keeps
# the final app signing key, and a lost upload key can be reset by Google
# support - but only with effort, so keep the backup.
#
# After this, `python build.py android` makes a signed .apk and .aab.
import getpass
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KEYSTORE = os.path.join(HERE, "mbrola-ng-upload.jks")
PROPERTIES = os.path.join(HERE, "keystore.properties")
ALIAS = "mbrola-ng"
# Google Play requires the key to be valid beyond 22 October 2033; 30 years.
VALIDITY_DAYS = 10950


def find_keytool():
	exe = "keytool.exe" if sys.platform == "win32" else "keytool"
	candidates = []
	if os.environ.get("JAVA_HOME"):
		candidates.append(os.path.join(os.environ["JAVA_HOME"], "bin", exe))
	local = os.environ.get("LOCALAPPDATA", "")
	for pattern in (os.path.join(local, "Android", "jdk-*", "bin", exe),
			"C:/Program Files/Android/Android Studio/jbr/bin/" + exe,
			"C:/Program Files/Eclipse Adoptium/jdk-*/bin/" + exe,
			"C:/Program Files/Java/jdk-*/bin/" + exe,
			"/usr/lib/jvm/*/bin/" + exe, "/usr/bin/" + exe):
		candidates += sorted(glob.glob(pattern), reverse=True)
	for c in candidates:
		if os.path.isfile(c):
			return c
	raise SystemExit("keytool not found: install JDK 17 or set JAVA_HOME.")


def ask(prompt, default=None, required=True):
	while True:
		text = input("%s%s: " % (prompt, " [%s]" % default if default else "")).strip() or (default or "")
		if text or not required:
			return text


def dname_escape(value):
	# characters with a meaning in an X.500 name
	for ch in '\\,+"<>;=':
		value = value.replace(ch, "\\" + ch)
	return value


def main():
	if os.path.exists(KEYSTORE) or os.path.exists(PROPERTIES):
		raise SystemExit(
			"A keystore already exists (%s).\n"
			"Do NOT create a new one for an app that is already published: updates must be\n"
			"signed with the same key. Delete both files yourself only if you are sure."
			% os.path.basename(KEYSTORE))
	keytool = find_keytool()
	print("This creates the signing key of the MBROLA NG Android app.\n")
	name = ask("Your full name (or the publisher name)", "Darko Milošević")
	organization = ask("Organization (empty for none)", required=False)
	city = ask("City")
	country = ""
	while len(country) != 2 or not country.isalpha():
		country = ask("Country code, two letters (HR, RS, BA, ...)").upper()
	while True:
		password = getpass.getpass("Password for the key (at least 8 characters; it is not shown): ")
		if len(password) < 8:
			print("Too short.")
			continue
		if getpass.getpass("The same password again: ") == password:
			break
		print("The passwords differ, try again.")

	parts = ["CN=" + dname_escape(name)]
	if organization:
		parts.append("O=" + dname_escape(organization))
	parts += ["L=" + dname_escape(city), "C=" + country]
	env = dict(os.environ, MBNG_KEY_PASSWORD=password)
	subprocess.run([
		keytool, "-genkeypair", "-v",
		"-keystore", KEYSTORE, "-storetype", "PKCS12",
		"-alias", ALIAS, "-keyalg", "RSA", "-keysize", "4096",
		"-validity", str(VALIDITY_DAYS),
		"-dname", ", ".join(parts),
		"-storepass:env", "MBNG_KEY_PASSWORD", "-keypass:env", "MBNG_KEY_PASSWORD",
	], check=True, env=env)

	def prop(value):
		return value.replace("\\", "\\\\")

	with open(PROPERTIES, "w", encoding="utf-8", newline="\n") as f:
		f.write("# MBROLA NG release signing - NEVER commit or publish this file\n")
		f.write("storeFile=%s\n" % os.path.basename(KEYSTORE))
		f.write("storePassword=%s\n" % prop(password))
		f.write("keyAlias=%s\n" % ALIAS)
		f.write("keyPassword=%s\n" % prop(password))

	print("\nCreated:\n  %s\n  %s" % (KEYSTORE, PROPERTIES))
	print("Make a BACKUP of both files now (USB stick, password manager) and never publish them.")
	print("Fingerprint of the certificate (Google Play Console shows the same one):")
	subprocess.run([keytool, "-list", "-v", "-keystore", KEYSTORE, "-alias", ALIAS,
		"-storepass:env", "MBNG_KEY_PASSWORD"], env=env)
	print("\nNow run: python build.py android")


if __name__ == "__main__":
	main()
