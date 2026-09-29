# MBROLA NG on Linux (Speech Dispatcher, Orca)

<!-- Copyright (c) 2026 Darko Milošević
     SPDX-License-Identifier: GPL-2.0-or-later -->

> **Status: implemented but not yet tested on Linux.** It is written against
> the Speech Dispatcher 0.11+ module protocol and builds from the same
> portable core as the tested Windows versions, but it has not yet run on a
> real Linux system with Orca. Reports are very welcome: output of
> `linux/build.sh`, of `python3 linux/test_module.py`, and how it sounds
> with Orca.

MBROLA NG is a Speech Dispatcher output module (`sd_mbrola_ng`), so Orca and
every other program that speaks through Speech Dispatcher (`spd-say`,
Speech Note, browsers, ...) can use it. Requirements: speech-dispatcher 0.11
or newer (Debian 13, Ubuntu 24.04 / 26.04, current Fedora and Arch).

## Building and installing

    sudo apt install build-essential cmake python3 pkg-config    # Debian/Ubuntu
    sudo dnf install gcc-c++ cmake python3 pkgconf               # Fedora
    sudo pacman -S base-devel cmake python                       # Arch

    linux/build.sh --user      # for this user only, into ~/.local (no sudo)
    linux/build.sh --system    # for all users (/usr/local + module folder)
    linux/build.sh --deb       # a .deb package in build/linux-deb

`--user` puts the module into `~/.local/libexec/speech-dispatcher-modules`,
where Speech Dispatcher finds it by itself. `mbrola-ng-voices` goes to
`~/.local/bin`.

## Voices

Voices are not part of the package (each has its own license). Install them
with the voice manager, a normal terminal program:

    mbrola-ng-voices available            # voices of the catalog
    mbrola-ng-voices install hr           # Croatian: cr1 (shows the license)
    mbrola-ng-voices install us2 en1      # English voices
    mbrola-ng-voices list                 # installed voices
    mbrola-ng-voices remove us2
    mbrola-ng-voices update
    mbrola-ng-voices install-file cr1.zip # offline: database file or zip
    sudo mbrola-ng-voices install hr --system    # for all users

Voices of the distribution's own MBROLA packages (`/usr/share/mbrola/cr1`,
for example `sudo apt install mbrola-us2`) are used without a download.
Messages are in Croatian when the system language is Croatian.

## Using it

After installing the first voice restart Speech Dispatcher (it starts again
by itself when a program needs it):

    killall speech-dispatcher
    spd-say -O                                  # output modules: mbrola_ng
    spd-say -o mbrola_ng -L                     # its voices
    spd-say -o mbrola_ng -l hr "Dobar dan, ovo je MBROLA NG."

Orca: Preferences (Orca+Space), Speech: Speech system "Speech Dispatcher",
Speech synthesizer "MBROLA NG" (listed as mbrola_ng), Voice: cr1. Rate,
pitch and volume of Orca are used; Orca's language/voice settings choose the
voice (Croatian text: cr1, English: us1/us2/us3/en1 when installed).

Settings of the module (all optional) are in
`/etc/speech-dispatcher/modules/mbrola_ng.conf`, or a copy in
`~/.config/speech-dispatcher/modules/mbrola_ng.conf`: default voice, who reads
punctuation (Speech Dispatcher or MBROLA NG), emoji, numbers digit by digit,
highest speed.

If `spd-say -O` does not list mbrola_ng: your `speechd.conf` has active
`AddModule` lines (then automatic detection is off). Add one line to it:

    AddModule "mbrola_ng" "sd_mbrola_ng" "mbrola_ng.conf"

## Testing without Speech Dispatcher

    python3 linux/test_module.py                 # INIT, SPEAK, marks, STOP ...
    python3 linux/test_module.py "Neki tekst."   # writes mbrola_ng_test.wav
    aplay mbrola_ng_test.wav

Module log: set `Debug 1` in the module configuration, then see
`~/.cache/speech-dispatcher/log/mbrola_ng.log`.

## Files

    <prefix>/bin/mbrola-ng-voices
    <prefix>/libexec/mbrola-ng/mbrola_ng_synth       (MBROLA, AGPL v3, separate process)
    <module folder>/sd_mbrola_ng
    <prefix>/share/mbrola-ng/languages/*.dat, catalog.json, voices.d/*.voice
    /etc/speech-dispatcher/modules/mbrola_ng.conf    (not with --user)
    ~/.local/share/mbrola-ng/voices/<id>/            (voices of the user)
    /usr/local/share/mbrola-ng/voices/<id>/          (voices for all users)
