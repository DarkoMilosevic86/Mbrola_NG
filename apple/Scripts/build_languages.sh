#!/bin/sh
# MBROLA NG - compiles the language data (languages/<code> -> <code>.dat) for
# the Apple build.
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   build_languages.sh <output folder> [<work folder>]
#
# Runs as an Xcode build phase (and by hand). langc is a tool of the Mac that
# builds, never of the device, so it is compiled here for the host with the
# macOS SDK, whatever platform Xcode is building for. It compiles every
# folder of languages/ and runs the language's own tests (tests.txt).
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:?usage: build_languages.sh <output folder> [<work folder>]}"
WORK="${2:-$ROOT/apple/build/langc}"
mkdir -p "$OUT" "$WORK"

# Xcode exports the settings of the target being built (SDKROOT, deployment
# targets, ... of iOS); none of them may reach the host compiler.
host() {
  env -i PATH="/usr/bin:/bin:/usr/sbin:/sbin" HOME="$HOME" \
    DEVELOPER_DIR="${DEVELOPER_DIR:-$(xcode-select -p)}" "$@"
}

SOURCES="
core/src/util/text.cpp core/src/util/bytes.cpp
core/src/lang/langdata.cpp core/src/lang/datfile.cpp core/src/lang/rbnf.cpp core/src/lang/language.cpp
core/src/text/canon.cpp core/src/text/normalizer.cpp
core/src/phon/phonology.cpp core/src/prosody/prosody.cpp
core/src/engine/pipeline.cpp core/src/engine/loader.cpp
tools/langsrc/langsrc.cpp tools/langsrc/testrunner.cpp tools/langc/main.cpp
"

LANGC="$WORK/langc"
rebuild=0
[ -x "$LANGC" ] || rebuild=1
if [ $rebuild -eq 0 ]; then
  for f in $(cd "$ROOT" && find core/src core/include tools/langsrc tools/langc -type f); do
    if [ "$ROOT/$f" -nt "$LANGC" ]; then rebuild=1; break; fi
  done
fi
if [ $rebuild -eq 1 ]; then
  echo "Building langc for the host"
  SDK="$(host xcrun --sdk macosx --show-sdk-path)"
  # shellcheck disable=SC2086
  (cd "$ROOT" && host xcrun --sdk macosx clang++ -std=c++17 -O2 -w -isysroot "$SDK" \
    -Icore/include -Icore/src -Itools/langsrc $SOURCES -o "$LANGC.tmp")
  mv "$LANGC.tmp" "$LANGC"
fi

for dir in "$ROOT"/languages/*/; do
  code="$(basename "$dir")"
  dat="$OUT/$code.dat"
  fresh=1
  [ -f "$dat" ] || fresh=0
  if [ $fresh -eq 1 ]; then
    [ "$LANGC" -nt "$dat" ] && fresh=0
    for f in "$dir"*; do
      if [ "$f" -nt "$dat" ]; then fresh=0; break; fi
    done
  fi
  if [ $fresh -eq 0 ]; then
    echo "Compiling and testing language $code"
    host "$LANGC" "${dir%/}" -o "$dat.tmp" --quiet
    mv "$dat.tmp" "$dat"
  fi
done
