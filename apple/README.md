# MBROLA NG for iOS, iPadOS and macOS

<!-- Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
     SPDX-License-Identifier: GPL-2.0-or-later -->

A system voice for Apple devices: VoiceOver, Spoken Content (Speak Screen,
Speak Selection), `say` and every app that speaks through `AVSpeechSynthesizer`
can use the MBROLA NG voices. iOS / iPadOS 16 or newer, macOS 13 (Ventura) or
newer - the first versions with the speech synthesis provider API.

- **The app** (SwiftUI, one target for iPhone, iPad and Mac) is the voice
  manager: installed voices, voices to install (license shown first, download
  verified by size and SHA-256), "Try" (an installed voice speaks your text,
  others play a recorded sample), per-voice settings (speed, pitch,
  modulation, volume, read emoji, how numbers are read), remove.
  English and Croatian.
- **The speech extension** (`MbrolaNGSpeech.appex`, an Audio Unit extension of
  type `ausp`) is what the system talks to. It offers exactly the installed
  voices and turns the system's SSML requests into audio with the same core,
  language data and voices as every other MBROLA NG platform.

## Building and running

Open `apple/MbrolaNG.xcodeproj` in Xcode, choose the scheme **MBROLA NG** and
the destination (**My Mac**, an iPhone or iPad), and press Run. Nothing else is
needed: the build compiles and tests the languages itself
(`Scripts/build_languages.sh` builds `langc` for the Mac and writes
`languages/<code>.dat` into the extension).

Signing is in `Config/Signing.xcconfig`: the development team and the bundle
identifier (`io.github.darkomilosevic86.mbrolang`, the same as the Android
application id). To build with another Apple developer account, change
`DEVELOPMENT_TEAM` there, or put it into `Config/Local.xcconfig` (ignored by
git). The app and the extension share an App Group, which Xcode's automatic
signing registers by itself.

From the command line:

    xcodebuild -project apple/MbrolaNG.xcodeproj -scheme "MBROLA NG" \
               -destination 'platform=macOS' -allowProvisioningUpdates build

The project file is generated from `project.yml` with
[XcodeGen](https://github.com/yonaskolb/XcodeGen) and committed. After
changing `project.yml` or adding files: `cd apple && xcodegen generate`.

## Packaging for distribution

**macOS**: one script builds the Release configuration from scratch for Apple
silicon and Intel, checks the result (speech extension, catalog, languages,
both architectures, signature, App Group) and packs the app:

    apple/Scripts/package_macos.sh

It writes `dist/MBROLA_NG-<version>-macos.zip`, which holds `MBROLA NG.app`:
unpack it and move the app to Applications.

- With a **Developer ID Application** certificate of the team in the keychain
  the app is signed with it. `--notarize <keychain profile>` then also sends
  it to Apple's notary service and staples the ticket, so that it opens on
  every Mac without a warning. The profile is made once with
  `xcrun notarytool store-credentials <profile> --apple-id <id> --team-id <team>`.
- Without that certificate the build is signed for development. It runs on
  the Mac that built it; on another Mac Gatekeeper refuses it until the user
  allows it in System Settings › Privacy & Security (Open Anyway).

**iOS / iPadOS**: in Xcode, Product › Archive with the destination
*Any iOS Device*, then distribute the archive from the Organizer.

## Using the voices

1. Start the app and install a voice.
2. The system lists it within a minute:
   - **iOS / iPadOS**: Settings › Accessibility › VoiceOver › Speech (VoiceOver),
     Settings › Accessibility › Spoken Content › Voices (Speak Screen / Selection).
   - **macOS**: VoiceOver Utility › Speech (VoiceOver), System Settings ›
     Accessibility › Spoken Content › System voice › Manage Voices, or
     `say -v "MBROLA NG Croatian male (cr1)" "Dobar dan"`.

The Croatian voice is registered for `hr-HR` and also announces that it can
read Serbian, Bosnian and Montenegrin text (`apple_also_for` in the catalog;
the Croatian data reads Cyrillic too).

## How it works

    VoiceOver / Spoken Content / AVSpeechSynthesizer
                      |   SSML request, pulls audio
                      v
    MbrolaNGSpeech.appex  (own process, sandboxed)
      MbrolaSpeechAudioUnit   AVSpeechSynthesisProviderAudioUnit
      Shared/SSML.swift       SSML -> segments, keeps text positions
      Shared/SpeechEngine     Swift face of core/include/mbrola_ng.h
      core/src                the portable core (unchanged)
      Shared/Native           MBROLA inside the process
                      ^
                      |  App Group container: voices/<id>/<id>, voice-settings.json
    MBROLA NG.app  (voice manager; installs voices, writes settings)

- **MBROLA runs inside the extension**, not as the separate
  `mbrola_ng_synth` program of the other platforms: iOS does not let an app
  start a process. `Shared/Native/synthproc_inproc.cpp` implements the core's
  `SynthProcess` class on top of MBROLA's multi-channel library
  (`Shared/Native/mbrola_inproc.c`) in place of `core/src/engine/synthproc.cpp`;
  nothing in `core/` is changed. The extension is a process of its own, so a
  damaged voice still cannot take VoiceOver down.
- **License consequence**: MBROLA is AGPL v3, and here it is linked in. The
  Apple app and extension are therefore distributed under the AGPL v3 as a
  whole (the GPL-2.0-or-later parts allow that). Both licenses also mean the
  app cannot simply be put on the App Store under Apple's standard terms;
  building it yourself and TestFlight-style distribution of your own builds
  is a question for the copyright holders.
- **Latency**: a speech synthesizer unit is rendered offline, so the render
  block pulls from the core exactly the frames the system asks for. The first
  audio of an utterance is there in about a millisecond; synthesis runs
  several thousand times faster than real time.
- **Pauses**: VoiceOver sends what it says about an element as sentences in
  one request (`<s>Settings</s><s>Heading</s>`). `Shared/SSML.swift` joins
  them with the pause of a comma, as the system's voices do, instead of the
  paragraph break a new line would be to the core. A line break inside the
  text (the lines of a label) is likewise the pause of a comma, an empty
  line that of a full stop. VoiceOver on the Mac separates the parts with
  `<break time="60ms"/>` and `250ms` instead: a break shorter than 100 ms is
  that comma and nothing more, a longer one follows the speed of the request.
  Next to the system's voices the core's pauses are still long, and it has
  no parameter for them, so `PauseShortener` (`Shared/SpeechEngine.swift`)
  takes silence out of the audio: a pause of up to 200 ms is halved, of what
  is beyond that four fifths stay; the word positions move accordingly. Silence at the end of a
  request is heard before the next one, so the extension gives the last
  block the length of the audio in it instead of padding it with silence,
  and shortens the core's pause after the last sentence (450 ms after a full
  stop) to 50 ms.
- **Spelling**: such a comma is only ever text next to other text. Between
  spelled characters (`<say-as interpret-as="characters">`, VoiceOver's
  typing echo and spelling) it becomes a 1 ms break, which the core follows
  with the same pause; alone in a text segment the core would spell it
  ("a comma b comma"), as it spells every text that is one character.
- **What the system sends** (observed, covered by `Tests/SSMLTests.swift`):
  speed as `<prosody rate="160%">` (12.5 % ... 400 %), pitch as a relative
  `pitch="+30%"`, volume as `volume="-6.02dB"` or `silent`. They are applied
  on top of the voice's own settings. A speed around the whole request
  becomes the speed of the voice, so that the pauses follow it as well.
- **Word positions** are reported back to the system (highlighting in Speak
  Screen, `willSpeakRangeOfSpeechString` in apps).
- Output is 16 kHz mono float, the rate of every voice in the catalog; a
  voice with another rate would be resampled (`Shared/Resampler.swift`).
- Voices, their licenses and the settings file are stored without file
  protection and excluded from backups, so they are readable before the
  first unlock after a restart.

## Tests

    xcodebuild -project apple/MbrolaNG.xcodeproj -scheme "MBROLA NG" \
               -destination 'platform=macOS' test

Unit tests (`Tests/`): the SSML parser, the catalog, settings, the resampler,
and - when a voice is installed - the engine itself.

Debug builds also understand launch arguments that check the whole chain
without touching the screen (`App/DebugSelfTest.swift`); the results are
printed as lines starting with `MBNG selftest`:

    "MBROLA NG.app/Contents/MacOS/MBROLA NG" -MBNGInstallVoice cr1 -MBNGSelfTest cr1

    xcrun devicectl device process launch --console --device <id> -- \
        io.github.darkomilosevic86.mbrolang -MBNGInstallVoice cr1 -MBNGSelfTest cr1

`-MBNGSelfTest` speaks through the *system* (AVSpeechSynthesizer -> speech
extension) into memory and reports the audio, the timing and the word
positions, including stopping in the middle and speaking again.

Note: the iOS **Simulator** runs the app and the unit tests, but its
accessibility service cannot store third-party voices, so the system never
lists them there. The system side can only be tested on a device or a Mac.

## App icon

`swift apple/Scripts/make_icons.swift` redraws the icons (the drawing of the
Android launcher icon) into `App/Assets.xcassets`.
