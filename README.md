# MBROLA NG

**Open-source text-to-speech built on the MBROLA diphone synthesizer. It
supports Croatian first and English as well. Languages are open, readable
data files, so anyone can add a new one.**

MBROLA NG turns text into speech with the classic
[MBROLA](https://github.com/numediart/MBROLA) diphone voices. It adds what
MBROLA never had: a complete text front end (numbers, dates, abbreviations,
symbols, emoji, Latin and Cyrillic script), prosody, and native integration
with the screen readers and speech systems people actually use.

## Platforms

| Platform | What it is | Status |
|----------|------------|--------|
| **NVDA** (Windows) | Native NVDA synthesizer add-on with a Voice Manager | Working |
| **Windows SAPI 5** | SAPI 5 engine for JAWS, Balabolka and all other SAPI 5 applications, with an installer (English / Croatian) that downloads the chosen voices | Working |
| **Linux** (Orca) | Speech Dispatcher output module `sd_mbrola_ng` + terminal voice manager `mbrola-ng-voices` | Implemented, **not yet tested on Linux**. Testers welcome. |
| **Android** | TextToSpeech engine for TalkBack and all apps, with a voice manager and per-voice settings; works before the first unlock (direct boot) | Working on the emulator, **not yet tested on real devices**. Testers welcome. |

The same core, language data and voices are used on every platform, so a
sentence sounds the same everywhere.

## Features

- **Croatian**: Latin and Cyrillic spelling (both give the same speech),
  numbers with the correct case and gender, ordinals, dates, times, units,
  currencies, abbreviations and acronyms, voicing assimilation across word
  boundaries, clitics, and spelling mode.
- **English**: CMU Pronouncing Dictionary (about 125,000 words) plus
  letter-to-sound rules for unknown words. Numbers, dates and units are
  supported. The American voices us1, us2 and us3 and the British voice en1
  can all be used.
- **Emoji** are read by their Unicode CLDR names, in Croatian and in English.
- **Screen reader behaviour**: fast, immediate stop; index marks for
  say-all; character and spelling mode; rate, pitch, inflection and volume.
- **Robust**: MBROLA runs as a separate small process. A damaged voice file
  can never crash the screen reader.
- **Open language format**: a language is a folder of commented text files
  (`languages/hr`, `languages/en`). They cover phonemes, grapheme-to-phoneme
  rules, lexicon, number grammar, dates, symbols, emoji, prosody and built-in
  tests. `langc` compiles the folder into one `.dat` file.

## Voices

Voices are **not** part of this repository or of any package. Each MBROLA
voice has its own license: it may be used only with MBROLA and only for
non-commercial purposes. The voice managers download the voices from
[numediart/MBROLA-voices](https://github.com/numediart/MBROLA-voices). They
show the license, verify the download and install it:

| Voice | Language | |
|-------|----------|-|
| cr1 | Croatian | male (University of Zagreb, Department of Phonetics) |
| us1 | American English | female |
| us2, us3 | American English | male |
| en1 | British English | male |

- NVDA: *NVDA menu → Tools → MBROLA NG Voice Manager*
- Windows: choose the voices in the installer
- Android: choose a language on the app's home screen, then a voice
- Linux: `mbrola-ng-voices install hr` (the distribution's `mbrola-*` voice
  packages are also used)

## Building

Everything is built with one command:

    python build.py [targets] [options]

| Target | Builds | Output |
|--------|--------|--------|
| `all` (default) | everything this system can build | |
| `core` | engine, `mbrola_ng_synth`, tools, compiled and tested languages | `build/` |
| `nvda` | NVDA add-on (x64 + x86) | `dist/mbrolaNG-<version>.nvda-addon` |
| `sapi` | SAPI 5 engine + installer | `dist/MBROLA_NG-<version>-setup.exe` |
| `orca` | Speech Dispatcher module + `mbrola-ng-voices` | `build/linux/` |
| `android` | Android app (Android SDK + NDK, JDK 17) | `dist/MBROLA_NG-<version>.apk` and `.aab` (signed), or `-debug.apk` |
| `clean` | removes all build output | |

Options: `--arch x64|x86|both` and `--debug` (Windows core); on Linux,
`--install user` (into `~/.local`, no root needed), `--install system` (with
sudo) and `--deb` (a `.deb` package in `dist/`); for Android, `--prepare`
(only copy the language data for Android Studio).

Examples:

    python build.py                       # everything for this system
    python build.py nvda                  # only the NVDA add-on
    python build.py core --arch both      # Windows core, x64 and x86
    python3 build.py orca --install user  # Linux: build, test, install
    python build.py android               # Android apk (and aab when a signing key exists)
    python build.py clean

Requirements:

- **Windows**: Visual Studio 2022 or newer with C++ (CMake and Ninja
  included), Python 3.8+, and [Inno Setup 6](https://jrsoftware.org/isinfo.php)
  for `sapi`.
- **Linux**: CMake 3.20+, g++ (C++17), Python 3. Speech Dispatcher 0.11 or
  newer is needed at run time. See [linux/README-linux.md](linux/README-linux.md).
- **Android**: Android SDK with NDK 27 and JDK 17, on Windows or Linux. See
  [android/README.md](android/README.md).

The platform scripts can also be used on their own: `build.cmd` (Windows
core), `nvda/build_addon.py`, `installer/build_installer.py` and
`linux/build.sh`; the Android project in `android/` opens in Android Studio.

## Repository layout

    core/            portable C++17 engine with a plain C API (core/include/mbrola_ng.h)
    core/src/sapi/   SAPI 5 engine (inside MBROLA_NG.dll)
    synth/           mbrola_ng_synth: MBROLA + a small pipe front end (separate process, AGPL v3)
    external/mbrola/ MBROLA synthesizer 3.4-dev (unmodified)
    languages/       language sources: hr (Croatian), en (English)
    tools/           langc (language compiler), mbtts (developer CLI), data import scripts
    nvda/            NVDA add-on (driver + Voice Manager, English / Croatian)
    installer/       Windows installer (Inno Setup, English / Croatian)
    linux/           Speech Dispatcher module, mbrola-ng-voices, build.sh
    android/         Android app: TTS engine, voice manager, store graphics
    cmake/           source lists shared by the desktop and Android builds
    catalog/         voice catalog (download locations, checksums, voice settings)
    ANALYSIS.txt     design document: architecture, formats, decisions

## Adding a language

Copy a language folder, edit its text files, then compile and test it:

    build\x64\langc languages\xx -o xx.dat              # compiles and runs tests.txt
    build\x64\mbtts -l languages\xx --phonemes "text"   # check the pronunciation
    build\x64\mbtts -l xx.dat --wav out.wav -v path\to\voice "text"

The engine contains no knowledge of any language. As long as a language fits
the existing rule mechanisms, adding it needs no change to the code. See
`ANALYSIS.txt` (sections 8 and 9) for the file formats.

## License

Copyright (c) 2026 Darko Milošević.

MBROLA NG is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version. See [`LICENSE`](LICENSE).

Parts with their own licenses:

| Part | License |
|------|---------|
| `external/mbrola` (MBROLA, Faculté Polytechnique de Mons / UMONS) and `synth/` (`mbrola_ng_synth`, a separate program) | GNU AGPL v3: `external/mbrola/LICENSE`, `synth/LICENSE` |
| `languages/en/lexicon.txt` (CMU Pronouncing Dictionary) | BSD-style (notice in the file header) |
| `languages/*/emoji.txt` (Unicode CLDR emoji names) | Unicode License v3 |

MBROLA voices (cr1, us1, us2, us3, en1, ...) are not part of this repository
or of any package. Each has its own license (use only with MBROLA, no
commercial use).

## Acknowledgements

- The [MBROLA project](https://github.com/numediart/MBROLA) (Thierry Dutoit
  and the TCTS Lab, Faculté Polytechnique de Mons / UMONS), and everyone who
  recorded MBROLA voices, including the Department of Phonetics, University
  of Zagreb, for cr1.
- The [CMU Pronouncing Dictionary](http://www.speech.cs.cmu.edu/cgi-bin/cmudict)
  (Carnegie Mellon University).
- [Unicode CLDR](https://cldr.unicode.org/) for the emoji names.
- [NVDA](https://www.nvaccess.org/), [Orca](https://orca.gnome.org/) and
  [Speech Dispatcher](https://github.com/brailcom/speechd), whose APIs make
  this possible.
