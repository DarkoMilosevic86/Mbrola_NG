#!/bin/sh
# MBROLA NG - build and install on Linux (Speech Dispatcher module for Orca)
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   linux/build.sh            build + tests into build/linux (nothing installed)
#   linux/build.sh --user     build + install for this user into ~/.local
#                             (no root rights; module in
#                             ~/.local/libexec/speech-dispatcher-modules)
#   linux/build.sh --system   build + install for all users (asks for sudo):
#                             /usr/local + the system module folder + /etc
#   linux/build.sh --deb      .deb package (prefix /usr) in build/linux-deb
#   linux/build.sh --uninstall-user   removes the --user installation
#
# Needs: cmake (3.20+), g++ (C++17), make or ninja, python3; for --system and
# --deb also pkg-config and the speech-dispatcher development files help to
# find the module folder (libspeechd-dev / speech-dispatcher-devel).
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODE=${1:-dev}
GEN=""
if command -v ninja >/dev/null 2>&1; then GEN="-G Ninja"; fi
JOBS=$(nproc 2>/dev/null || echo 2)

configure_build() {  # <build dir> <prefix>
  cmake -S "$ROOT" -B "$1" $GEN -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$2"
  cmake --build "$1" -j "$JOBS"
  (cd "$1" && ctest --output-on-failure)
}

restart_hint() {
  echo
  echo "Next steps:"
  echo "  mbrola-ng-voices install hr        (downloads the Croatian voice cr1)"
  echo "  killall speech-dispatcher          (it starts again by itself)"
  echo "  spd-say -o mbrola_ng -l hr \"Dobar dan\""
  echo "  Orca: Preferences (Insert+Space), Speech, Speech synthesizer: MBROLA NG"
}

case "$MODE" in
  dev|--dev)
    configure_build "$ROOT/build/linux" /usr/local
    echo "Built in build/linux. Install with --user or --system."
    ;;
  --user)
    configure_build "$ROOT/build/linux-user" "$HOME/.local"
    cmake --install "$ROOT/build/linux-user"
    case ":$PATH:" in
      *":$HOME/.local/bin:"*) ;;
      *) echo "Note: $HOME/.local/bin is not in PATH; mbrola-ng-voices is there." ;;
    esac
    restart_hint
    ;;
  --system)
    configure_build "$ROOT/build/linux-system" /usr/local
    sudo cmake --install "$ROOT/build/linux-system"
    restart_hint
    ;;
  --deb)
    configure_build "$ROOT/build/linux-deb" /usr
    (cd "$ROOT/build/linux-deb" && cpack -G DEB)
    ls -1 "$ROOT"/build/linux-deb/*.deb
    echo "Install with: sudo apt install ./build/linux-deb/<file>.deb"
    ;;
  --uninstall-user)
    M="$ROOT/build/linux-user/install_manifest.txt"
    if [ ! -f "$M" ]; then echo "no --user installation found ($M)"; exit 1; fi
    xargs -d '\n' rm -f < "$M"
    rmdir "$HOME/.local/libexec/mbrola-ng" "$HOME/.local/share/mbrola-ng/languages" \
          "$HOME/.local/share/mbrola-ng/voices.d" 2>/dev/null || true
    echo "Removed. Voices in ~/.local/share/mbrola-ng/voices are kept."
    ;;
  *)
    sed -n '4,17p' "$0"
    exit 2
    ;;
esac
