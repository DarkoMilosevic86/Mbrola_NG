// MBROLA NG - self-test of debug builds, driven by launch arguments
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Not part of release builds. Lets a script check the whole chain on a real
// device or a Mac without touching the screen:
//
//   -MBNGInstallVoice cr1     download and install the voice (license skipped)
//   -MBNGShow cr1 | about    open that screen at launch
//   -MBNGSelfTest cr1         speak through the SYSTEM (AVSpeechSynthesizer ->
//                             speech extension) into memory and report
//
// Results are printed as lines starting with "MBNG selftest".
#if DEBUG
import AVFoundation

@MainActor
enum DebugSelfTest {
    /// `-MBNGShow cr1` / `-MBNGShow about`: the screen to open at launch (screenshots).
    static var initialPath: [Route] {
        switch UserDefaults.standard.string(forKey: "MBNGShow") {
        case nil: return []
        case "about"?: return [.about]
        case let id?: return [.voice(id)]
        }
    }

    static func runIfRequested(onChange: @escaping @MainActor () -> Void) {
        let defaults = UserDefaults.standard
        let install = defaults.string(forKey: "MBNGInstallVoice")
        let test = defaults.string(forKey: "MBNGSelfTest")
        guard install != nil || test != nil else { return }
        Task {
            if let id = install, let voice = Catalog.shared.voice(id) {
                if VoiceStore.isInstalled(id) {
                    report("install \(id): already installed")
                } else {
                    do {
                        let staging = try VoiceInstaller.newStagingDirectory(voice)
                        _ = try await VoiceInstaller.fetchLicense(voice, into: staging)
                        try await VoiceInstaller.install(voice, from: staging) { _, _ in }
                        report("install \(id): done")
                    } catch {
                        report("install \(id): FAILED \(error.localizedDescription)")
                    }
                    onChange()
                }
            }
            if let id = test { await speakThroughSystem(id) }
            report("finished")
        }
    }

    private static func report(_ line: String) {
        print("MBNG selftest \(line)")
        NSLog("MBNG selftest %@", line)
    }

    private final class Listener: NSObject, AVSpeechSynthesizerDelegate, @unchecked Sendable {
        var ranges = 0
        func speechSynthesizer(
            _ synthesizer: AVSpeechSynthesizer, willSpeakRangeOfSpeechString characterRange: NSRange,
            utterance: AVSpeechUtterance
        ) { ranges += 1 }
    }

    private final class Capture: @unchecked Sendable {
        var frames = 0
        var peak: Float = 0
        var sampleRate = 0.0
        var first: TimeInterval = -1
        var done = false
        let start = Date()
    }

    private static func speakThroughSystem(_ id: String) async {
        // how the system registered the extension: the manufacturer name is
        // the group the voices are listed under in the VoiceOver settings
        let speechSynthesizers = AudioComponentDescription(
            componentType: 0x6175_7370 /* ausp */, componentSubType: 0x6D62_7370 /* mbsp */,
            componentManufacturer: 0, componentFlags: 0, componentFlagsMask: 0)
        for component in AVAudioUnitComponentManager.shared().components(matching: speechSynthesizers) {
            report("audio unit name '\(component.name)' manufacturer '\(component.manufacturerName)'")
        }
        // the system needs a moment (up to a minute) to learn about a new voice
        var voice: AVSpeechSynthesisVoice?
        for attempt in 0..<45 {
            voice = AVSpeechSynthesisVoice.speechVoices().first { $0.identifier.hasSuffix(".speech." + id) }
            if voice != nil {
                report("voice \(voice!.identifier) '\(voice!.name)' \(voice!.language) listed after \(attempt * 2) s")
                break
            }
            AVSpeechSynthesisProviderVoice.updateSpeechVoices()
            try? await Task.sleep(nanoseconds: 2_000_000_000)
        }
        guard let voice else {
            report("voice \(id): NOT LISTED by the system")
            return
        }
        let synthesizer = AVSpeechSynthesizer()
        let listener = Listener()
        synthesizer.delegate = listener
        let long = Catalog.shared.voice(id)?.language == "hr"
            ? "Dobar dan. Ovo je proba sinteze govora, 4. listopada 2026. godine. Druga rečenica je malo duža od prve."
            : "Good afternoon. This is a speech synthesis test, on October 4th 2026. The second sentence is longer."
        for (text, rate) in [(long, Float(0.5)), (long, Float(0.75)), ("a", Float(0.5)), ("😀 5 < 6 & 7", Float(0.5))] {
            let capture = await write(text, rate: rate, voice: voice, synthesizer: synthesizer)
            report(String(
                format: "speak rate %.2f: %d frames at %.0f Hz (%.2f s), peak %.3f, first audio %d ms, all %d ms, %d ranges, complete=%d",
                rate, capture.frames, capture.sampleRate,
                capture.sampleRate > 0 ? Double(capture.frames) / capture.sampleRate : 0, capture.peak,
                Int(capture.first * 1000), Int(Date().timeIntervalSince(capture.start) * 1000), listener.ranges,
                capture.done ? 1 : 0))
            listener.ranges = 0
        }
        // stop in the middle, then speak again at once (what VoiceOver does all day)
        for round in 1...5 {
            let interrupted = AVSpeechUtterance(string: long)
            interrupted.voice = voice
            let started = Capture()
            synthesizer.write(interrupted) { buffer in
                if let b = buffer as? AVAudioPCMBuffer, b.frameLength > 0 { started.frames += Int(b.frameLength) }
            }
            try? await Task.sleep(nanoseconds: 30_000_000)
            synthesizer.stopSpeaking(at: .immediate)
            let capture = await write("OK", rate: 0.5, voice: voice, synthesizer: synthesizer)
            report("interrupt \(round): stopped after \(started.frames) frames, next utterance \(capture.frames) frames, first audio \(Int(capture.first * 1000)) ms, complete=\(capture.done ? 1 : 0)")
        }
    }

    private static func write(
        _ text: String, rate: Float, voice: AVSpeechSynthesisVoice, synthesizer: AVSpeechSynthesizer
    ) async -> Capture {
        let utterance = AVSpeechUtterance(string: text)
        utterance.voice = voice
        utterance.rate = rate
        let capture = Capture()
        synthesizer.write(utterance) { buffer in
            guard let b = buffer as? AVAudioPCMBuffer else { return }
            if b.frameLength == 0 {
                capture.done = true
                return
            }
            if capture.first < 0 { capture.first = Date().timeIntervalSince(capture.start) }
            capture.frames += Int(b.frameLength)
            capture.sampleRate = b.format.sampleRate
            if let data = b.floatChannelData {
                for i in 0..<Int(b.frameLength) { capture.peak = max(capture.peak, abs(data[0][i])) }
            } else if let data = b.int16ChannelData {
                for i in 0..<Int(b.frameLength) { capture.peak = max(capture.peak, Float(abs(Int(data[0][i]))) / 32768) }
            }
        }
        for _ in 0..<300 where !capture.done {
            try? await Task.sleep(nanoseconds: 20_000_000)
        }
        return capture
    }
}
#endif
