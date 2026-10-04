// MBROLA NG - "Try": speaks with an installed voice, or plays the recorded
// sample of a voice that is not installed
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import AVFoundation
import SwiftUI

/// Sentences spoken by "Try".
enum SampleTexts {
    static func installedAndWorking(_ language: String) -> String {
        switch language {
        case "hr": return "Ukoliko čujete ovu poruku, ovaj glas je instaliran i radi."
        default: return "If you can hear this message, this voice is installed and working."
        }
    }
}

@MainActor
final class PreviewPlayer: NSObject, ObservableObject, AVAudioPlayerDelegate {
    /// Id of the voice that is heard now.
    @Published private(set) var playing: String?
    @Published var failure: VoiceLibrary.Failure?

    private var player: AVAudioPlayer?
    private var task: Task<Void, Never>?

    /// Samples are samples/<voice id>.wav, made by tools/make_voice_samples.py.
    static func sampleURL(_ voice: Voice) -> URL? {
        Bundle.main.url(forResource: voice.id, withExtension: "wav", subdirectory: "samples")
    }

    func stop() {
        task?.cancel()
        task = nil
        player?.stop()
        player = nil
        if playing != nil {
            playing = nil
            #if os(iOS)
            try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
            #endif
        }
    }

    /// Plays the recorded sample of a voice that is not installed.
    func playSample(_ voice: Voice) {
        stop()
        guard let url = PreviewPlayer.sampleURL(voice), let data = try? Data(contentsOf: url) else { return }
        start(data, voice: voice)
    }

    /// Speaks `text` with an installed voice and `settings` through an engine
    /// of its own, independent of the system's speech settings.
    func speak(_ text: String, voice: Voice, settings: VoiceSettings) {
        stop()
        playing = voice.id
        task = Task {
            let result = await Task.detached(priority: .userInitiated) { () -> Result<Data, Error> in
                Result {
                    let engine = try SpeechEngine(voice: voice)
                    let samples = engine.shortenPauses(
                        try engine.synthesize(SpeechPlan(text, ssml: false), settings: settings))
                    return wavFile(samples, sampleRate: engine.sampleRate)
                }
            }.value
            if Task.isCancelled { return }
            switch result {
            case .success(let data):
                start(data, voice: voice)
            case .failure(let error):
                playing = nil
                failure = .init(message: String(localized: "The voice could not speak: \(error.localizedDescription)"))
            }
        }
    }

    private func start(_ wav: Data, voice: Voice) {
        do {
            #if os(iOS)
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .spokenAudio, options: [.duckOthers])
            try session.setActive(true)
            #endif
            let p = try AVAudioPlayer(data: wav, fileTypeHint: AVFileType.wav.rawValue)
            p.delegate = self
            guard p.play() else { throw SpeechEngineError(message: "audio output not available") }
            player = p
            playing = voice.id
        } catch {
            playing = nil
            failure = .init(message: String(localized: "The voice could not speak: \(error.localizedDescription)"))
        }
    }

    nonisolated func audioPlayerDidFinishPlaying(_ player: AVAudioPlayer, successfully flag: Bool) {
        let finished = ObjectIdentifier(player)
        Task { @MainActor in
            if self.player.map(ObjectIdentifier.init) == finished { self.stop() }
        }
    }

    nonisolated func audioPlayerDecodeErrorDidOccur(_ player: AVAudioPlayer, error: Error?) {
        let finished = ObjectIdentifier(player)
        Task { @MainActor in
            if self.player.map(ObjectIdentifier.init) == finished { self.stop() }
        }
    }
}

/// 16-bit mono PCM as a WAV file in memory.
func wavFile(_ samples: [Int16], sampleRate: Int) -> Data {
    var data = Data(capacity: 44 + samples.count * 2)
    func u32(_ v: Int) { withUnsafeBytes(of: UInt32(v).littleEndian) { data.append(contentsOf: $0) } }
    func u16(_ v: Int) { withUnsafeBytes(of: UInt16(v).littleEndian) { data.append(contentsOf: $0) } }
    data.append(contentsOf: Array("RIFF".utf8)); u32(36 + samples.count * 2)
    data.append(contentsOf: Array("WAVEfmt ".utf8)); u32(16); u16(1); u16(1)
    u32(sampleRate); u32(sampleRate * 2); u16(2); u16(16)
    data.append(contentsOf: Array("data".utf8)); u32(samples.count * 2)
    samples.withUnsafeBufferPointer { p in
        p.withMemoryRebound(to: UInt8.self) { data.append($0) }  // all Apple CPUs are little-endian
    }
    return data
}
