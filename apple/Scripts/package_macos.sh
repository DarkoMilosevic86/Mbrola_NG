#!/bin/sh
# MBROLA NG - builds the macOS release and packs it for distribution
# Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   package_macos.sh [--notarize <keychain profile>]
#
# Builds the Release configuration from scratch for Apple silicon and Intel
# (apple/build/package-mac), checks the result and writes
#
#   dist/MBROLA_NG-<version>-macos.zip
#
# which holds "MBROLA NG.app": unpack, move to Applications, done.
#
# Signing:
# - With a "Developer ID Application" certificate of the team
#   (Config/Signing.xcconfig) in the keychain the app is signed with it, as
#   Gatekeeper wants it for an app from outside the App Store.
#   --notarize <profile> then also sends it to Apple's notary service and
#   staples the ticket, so that it opens on every Mac without a warning.
#   The profile is made once with
#       xcrun notarytool store-credentials <profile> --apple-id <id> --team-id <team>
# - Without that certificate the build is signed for development (Xcode's
#   automatic signing). It runs on this Mac; on another Mac Gatekeeper refuses
#   it until the user allows it in System Settings > Privacy & Security.
set -eu

APPLE="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$APPLE/.." && pwd)"
DERIVED="$APPLE/build/package-mac"
DIST="$ROOT/dist"

usage() {
  sed -n '5,25p' "$0"
  exit 2
}

PROFILE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --notarize)
      [ $# -ge 2 ] || usage
      PROFILE="$2"
      shift 2
      ;;
    *) usage ;;
  esac
done

setting() {  # <name>: a build setting of the app target
  xcodebuild -project "$APPLE/MbrolaNG.xcodeproj" -target MbrolaNG -configuration Release \
    -showBuildSettings 2>/dev/null | sed -n "s/^ *$1 = //p" | head -n 1
}

TEAM="$(setting DEVELOPMENT_TEAM)"
if security find-identity -v -p codesigning | grep -q "\"Developer ID Application: .*($TEAM)\""; then
  DEVELOPER_ID=1
  echo "Signing with the Developer ID certificate of team $TEAM"
else
  DEVELOPER_ID=0
  echo "No Developer ID Application certificate of team $TEAM: signing for development"
  if [ -n "$PROFILE" ]; then
    echo "error: --notarize needs a build signed with a Developer ID certificate" >&2
    exit 1
  fi
fi

# ------------------------------------------------------------------- build
rm -rf "$DERIVED"
mkdir -p "$DERIVED" "$DIST"
LOG="$DERIVED/xcodebuild.log"
echo "Building Release (arm64 + x86_64), log: $LOG"
set -- -project "$APPLE/MbrolaNG.xcodeproj" -scheme "MBROLA NG" -configuration Release \
  -destination "generic/platform=macOS" -derivedDataPath "$DERIVED" \
  ARCHS="arm64 x86_64" ONLY_ACTIVE_ARCH=NO
if [ $DEVELOPER_ID -eq 1 ]; then
  # the notary service wants a secure timestamp and no get-task-allow
  set -- "$@" CODE_SIGN_STYLE=Manual CODE_SIGN_IDENTITY="Developer ID Application" \
    PROVISIONING_PROFILE_SPECIFIER= OTHER_CODE_SIGN_FLAGS=--timestamp \
    CODE_SIGN_INJECT_BASE_ENTITLEMENTS=NO
else
  set -- "$@" -allowProvisioningUpdates
fi
if ! xcodebuild "$@" build > "$LOG" 2>&1; then
  grep -E "error:|BUILD FAILED" "$LOG" >&2 || tail -n 30 "$LOG" >&2
  exit 1
fi

# ------------------------------------------------------------------ checks
APP="$DERIVED/Build/Products/Release/MBROLA NG.app"
APPEX="$APP/Contents/PlugIns/MbrolaNGSpeech.appex"
fail() {
  echo "error: $1" >&2
  exit 1
}
[ -d "$APP" ] || fail "the build did not produce $APP"
[ -d "$APPEX" ] || fail "the speech extension is not inside the app"
[ -f "$APPEX/Contents/Resources/catalog.json" ] || fail "catalog.json is missing in the speech extension"
for dir in "$ROOT"/languages/*/; do
  code="$(basename "$dir")"
  [ -f "$APPEX/Contents/Resources/languages/$code.dat" ] || fail "language $code is missing in the speech extension"
done
for binary in "$APP/Contents/MacOS/MBROLA NG" "$APPEX/Contents/MacOS/MbrolaNGSpeech"; do
  for arch in arm64 x86_64; do
    lipo "$binary" -verify_arch $arch || fail "$(basename "$binary") has no $arch code"
  done
done
codesign --verify --deep --strict "$APP" || fail "the signature of the app is not valid"
# the app and the extension meet in the App Group <team id>.<bundle id>
# (Shared/SharedContainer.swift): without it an installed voice never speaks
for bundle in "$APP" "$APPEX"; do
  codesign -d --entitlements - --xml "$bundle" 2>/dev/null | grep -q "<string>$TEAM\\.$(setting MBNG_BUNDLE_ID)</string>" \
    || fail "$(basename "$bundle") is signed without the App Group"
done

# --------------------------------------------------------------------- zip
VERSION="$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$APP/Contents/Info.plist")"
ZIP="$DIST/MBROLA_NG-$VERSION-macos.zip"
pack() {
  rm -f "$ZIP"
  ditto -c -k --keepParent "$APP" "$ZIP"
}
pack

if [ -n "$PROFILE" ]; then
  echo "Notarizing (this takes a few minutes)"
  xcrun notarytool submit "$ZIP" --keychain-profile "$PROFILE" --wait
  xcrun stapler staple "$APP"
  pack  # again, now with the ticket inside
  spctl --assess --type execute "$APP" || fail "Gatekeeper does not accept the notarized app"
fi

echo "-> ${ZIP#"$ROOT"/} ($(du -h "$ZIP" | cut -f1 | tr -d ' '), version $VERSION)"
if [ $DEVELOPER_ID -eq 0 ]; then
  echo "Signed for development: on another Mac Gatekeeper has to be told to open it"
  echo "(System Settings > Privacy & Security > Open Anyway)."
elif [ -z "$PROFILE" ]; then
  echo "Signed with Developer ID but not notarized: run again with --notarize <keychain profile>."
fi
