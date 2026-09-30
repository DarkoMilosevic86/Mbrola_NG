# MBROLA NG for Android

<!-- Copyright (c) 2026 Darko Milošević
     SPDX-License-Identifier: GPL-2.0-or-later -->

A text-to-speech engine for Android (TalkBack and every app that speaks) with
a built-in voice manager. Android 10 or newer; arm64-v8a, armeabi-v7a, x86_64.

- **Home screen**: installed voices (Voice settings, Try, Remove) and the
  languages to install voices from. A language opens the list of its voices
  (Try plays a short recorded sample, Install shows the license, downloads
  and verifies the voice).
- **Voice settings** - also the screen the system opens from
  *Settings → Text-to-speech output → engine settings*: speed, pitch,
  modulation, volume, "Read emoji", and how numbers are read (as whole
  numbers, digit by digit, in pairs). Speed and volume follow the system
  (TalkBack) unless "Use this speed / volume instead of the system ..." is
  checked.
- **Languages of the phone**: the Croatian voice is also offered for
  Serbian, Bosnian and Montenegrin (`android_also_for` in the catalog; the
  Croatian data reads Cyrillic too), so a phone set to one of them speaks at
  once. For any other language, "Always use this voice, for every language"
  in the voice settings makes one voice answer every request - without it
  Android reports "language not supported" and TalkBack stays silent.
- **Direct boot**: the engine speaks before the first unlock after a reboot
  (TalkBack on the lock screen). Voices, language data and voice settings
  are kept in device-protected storage for that.
- UI in English and Croatian. The voice catalog is bundled in the app.

## Building

    python build.py android

Needs the Android SDK (platform 36, NDK 27.2, CMake 3.22.1 - Gradle installs
missing parts when the licenses are accepted) and JDK 17. The script finds
them through `ANDROID_HOME` / `JAVA_HOME` or in the usual places, compiles the
languages with the desktop build, copies them and the catalog into
`android/generated/assets`, and runs Gradle.

| Without `android/keystore.properties` | `dist/MBROLA_NG-<version>-debug.apk` (for testing) |
|---|---|
| With it | `dist/MBROLA_NG-<version>.apk` (signed, direct installation) and `dist/MBROLA_NG-<version>.aab` (for Google Play) |

Android Studio: run `python build.py android --prepare` once, then open the
`android` folder.

## Signing key (once)

    python android/create_keystore.py

It asks for your name, city, country and a password, and writes
`android/mbrola-ng-upload.jks` and `android/keystore.properties`. Both are
ignored by git. **Back them up and never publish them**: updates of a
published app must be signed with the same key.

## Native code

`app/src/main/cpp` builds two files per ABI from the same sources as the
desktop build (`cmake/sources.cmake`):

- `libmbrola_ng_jni.so` - the portable core and the JNI bridge;
- `libmbrola_ng_synth.so` - despite its name an executable: `mbrola_ng_synth`
  (MBROLA, AGPL v3), started as a separate process. It has a library name so
  that Android installs it into the app's native library folder, the only
  place an app may run a program from (`useLegacyPackaging = true`).

Both are linked for 16 KB memory pages.

## Voice samples

`app/src/main/assets/samples/<voice>.wav` are the recordings played by "Try"
for voices that are not installed. Recreate them with
`python tools/make_voice_samples.py` (Windows, after `build.cmd x64`).
A voice without a sample simply has no Try button until it is installed.

## Store graphics (`images/`)

    python android/images/make_images.py      (pip install pillow)

| File | Google Play field |
|------|-------------------|
| `icon-512.png` | App icon (512 x 512) |
| `feature-graphic-en.png`, `feature-graphic-hr.png` | Feature graphic (1024 x 500) |
| `screenshots/en/*.png`, `screenshots/hr/*.png` | Phone screenshots (1080 x 2160) |

The screenshots are taken from the running app.

## Before publishing on Google Play

- The application id `io.github.darkomilosevic86.mbrolang`
  (`app/build.gradle.kts`) can never be changed after the first upload -
  decide on it first.
- Upload the `.aab`; Play App Signing keeps the final signing key, the key
  from `create_keystore.py` is the upload key.
- The listing needs a privacy policy URL. The app collects no data; it uses
  the internet only to download voices from GitHub.
- Voices are free of charge and may not be sold (their licenses): the app
  must stay free, without ads or paid features.
- Raise `versionCode` in `app/build.gradle.kts` for every upload.
