// MBROLA NG - one synthesis engine (one voice) of the core
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

struct SpeechEngineError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

/// A word, sentence start or mark on the time line of the utterance.
struct SpeechEvent: Sendable {
    enum Kind: Sendable { case word, sentence, mark, end }
    var kind: Kind
    var sample: Int         // audio position, in samples from the utterance start
    var textOffset: Int     // word / sentence: UTF-16 position in the spoken text
    var textLength: Int
    var markIndex: Int
}

/// Swift face of the core's C API (core/include/mbrola_ng.h) for one voice.
/// Every call except `cancel()` must come from one thread at a time.
final class SpeechEngine: @unchecked Sendable {
    let voiceID: String
    let sampleRate: Int
    private let handle: OpaquePointer

    init(voice: Voice) throws {
        guard let language = BundledResources.languageFile(voice.language) else {
            throw SpeechEngineError(message: "language data missing: \(voice.language)")
        }
        guard let database = VoiceStore.database(voice.id), VoiceStore.isInstalled(voice.id) else {
            throw SpeechEngineError(message: "voice \(voice.id) is not installed")
        }
        var config = mbng_config()
        config.struct_size = UInt32(MemoryLayout<mbng_config>.size)
        config.offset_unit = Int32(MBNG_OFFSET_UTF16)
        config.base_pitch = Int32(voice.basePitch)
        var error: Int32 = 0
        let created = language.path.withCString { languagePath in
            database.path.withCString { voicePath in
                // "the synthesis program": MBROLA is inside this process on
                // Apple platforms (Native/synthproc_inproc.cpp), any name will do
                "in-process".withCString { synthPath in
                    voice.phonemeMap.withCString { phonemeMap in
                        config.language_path = languagePath
                        config.voice_path = voicePath
                        config.synth_path = synthPath
                        config.phoneme_map = phonemeMap
                        return mbng_create(&config, &error)
                    }
                }
            }
        }
        guard let created else {
            throw SpeechEngineError(message: String(cString: mbng_last_error(nil)))
        }
        handle = created
        voiceID = voice.id
        var rate: Int32 = 16000
        mbng_get_audio_format(created, &rate, nil, nil)
        sampleRate = Int(rate)
    }

    deinit {
        mbng_destroy(handle)
    }

    var lastError: String { String(cString: mbng_last_error(handle)) }

    /// - Parameters:
    ///   - rate: speed asked by the system, 1 = normal (multiplies the voice's own setting)
    ///   - volume: volume asked by the system, 1 = unchanged
    func apply(_ s: VoiceSettings, rate: Double = 1, volume: Double = 1) {
        func set(_ param: Int, _ value: Int) { mbng_set_param(handle, Int32(param), Int32(value)) }
        set(MBNG_PARAM_RATE, min(max(Int(Double(s.rate) * rate + 0.5), 20), 800))
        set(MBNG_PARAM_PITCH, min(max(s.pitch, 25), 400))
        set(MBNG_PARAM_RANGE, min(max(s.modulation, 0), 300))
        set(MBNG_PARAM_VOLUME, min(max(Int(Double(s.volume) * volume + 0.5), 0), 400))
        set(MBNG_PARAM_DIGITS, s.numbers.rawValue)
    }

    /// Starts a new utterance (the previous one is dropped).
    func begin(_ segments: [SpeechSegment]) -> Bool {
        // the core copies the text inside mbng_begin; until then it lives here
        var buffers: [UnsafeMutablePointer<UInt16>] = []
        defer { buffers.forEach { $0.deallocate() } }
        func copy(_ text: String) -> (UnsafePointer<UInt16>, Int32) {
            let units = Array(text.utf16)
            let p = UnsafeMutablePointer<UInt16>.allocate(capacity: max(units.count, 1))
            p.update(from: units, count: units.count)
            buffers.append(p)
            return (UnsafePointer(p), Int32(units.count))
        }
        var raw: [mbng_segment] = []
        raw.reserveCapacity(segments.count)
        for segment in segments {
            var r = mbng_segment()
            switch segment {
            case .text(let text, let base):
                r.type = Int32(MBNG_SEG_TEXT)
                (r.text16, r.length) = copy(text)
                r.offset_base = Int32(base)
            case .mark(let index, let name):
                r.type = Int32(MBNG_SEG_MARK)
                (r.text16, r.length) = copy(name)
                r.value = Int32(index)
            case .pause(let ms):
                r.type = Int32(MBNG_SEG_BREAK)
                r.value = Int32(ms)
            case .rate(let percent):
                r.type = Int32(MBNG_SEG_RATE)
                r.value = Int32(percent)
            case .pitch(let percent):
                r.type = Int32(MBNG_SEG_PITCH)
                r.value = Int32(percent)
            case .spell(let on):
                r.type = Int32(on ? MBNG_SEG_SPELL_ON : MBNG_SEG_SPELL_OFF)
            }
            raw.append(r)
        }
        return mbng_begin(handle, raw, Int32(raw.count)) == MBNG_OK
    }

    private var rawEvents = [mbng_event](repeating: mbng_event(), count: 32)

    /// Fills `pcm` with up to `count` samples and appends the events that fall
    /// into them. Fewer than `count` samples (and no events) = the utterance
    /// is finished or was cancelled; negative = error.
    func read(_ pcm: UnsafeMutablePointer<Int16>, count: Int, events: inout [SpeechEvent]) -> Int {
        var n: Int32 = 0
        let got = mbng_read(handle, pcm, Int32(count), &rawEvents, Int32(rawEvents.count), &n)
        for e in rawEvents[0..<Int(max(n, 0))] {
            let kind: SpeechEvent.Kind
            switch Int(e.type) {
            case MBNG_EVENT_WORD: kind = .word
            case MBNG_EVENT_SENTENCE: kind = .sentence
            case MBNG_EVENT_MARK: kind = .mark
            case MBNG_EVENT_END: kind = .end
            default: continue
            }
            events.append(SpeechEvent(
                kind: kind, sample: Int(e.sample), textOffset: Int(e.text_offset),
                textLength: Int(e.text_length), markIndex: Int(e.value)))
        }
        return Int(got)
    }

    /// Stops the current utterance; safe from any thread.
    func cancel() {
        mbng_cancel(handle)
    }
}

/// Shortens the pauses in the audio of the core. On Apple's systems a voice
/// is heard next to the system's own, which pause far more briefly, above
/// all between the parts of what VoiceOver says about an element. The core
/// has no parameter for its pauses (they are part of the language data and
/// the same on every platform), so the silence is taken out of what it
/// delivers: a short pause (a comma, between VoiceOver's parts) is halved,
/// of what a pause is longer than `knee` four fifths stay (the end of a
/// sentence remains one). Silence is held back until the sound after it
/// arrives, which also lets the caller decide how much of it is left at the
/// end of an utterance.
struct PauseShortener<Sample: SignedNumeric & Comparable> {
    /// Below this a sample is silence (MBROLA's pauses are digital silence).
    let level: Sample
    /// Shorter stretches (the closure of a "p" or "t") are left alone.
    let shortest: Int
    /// What is left of a pause at least, so that phrases never run together.
    let least: Int
    /// Up to here a pause is halved.
    let knee: Int

    private var withheld = 0    // silent samples read and not yet passed on
    private var position = 0    // samples read
    private var removed = 0
    private var cuts: [(position: Int, removed: Int)] = []

    init(level: Sample, sampleRate: Int) {
        self.level = level
        shortest = sampleRate * 30 / 1000
        least = sampleRate * 25 / 1000
        knee = sampleRate * 200 / 1000
    }

    // (not abs(): the lowest value of an integer sample has none)
    private func isSilent(_ x: Sample) -> Bool { x < level && x > -level }

    private func kept(_ silence: Int) -> Int {
        if silence < shortest { return silence }
        if silence <= knee { return max(least, silence / 2) }
        return knee / 2 + (silence - knee) * 4 / 5
    }

    private mutating func release(into out: inout [Sample], at input: Int, limit: Int? = nil) {
        if withheld == 0 { return }
        let pass = min(kept(withheld), limit ?? Int.max)
        out.append(contentsOf: repeatElement(0, count: pass))
        if pass != withheld {
            removed += withheld - pass
            cuts.append((input, removed))
        }
        withheld = 0
    }

    mutating func process(_ input: UnsafeBufferPointer<Sample>, into out: inout [Sample]) {
        var i = 0
        let n = input.count
        while i < n {
            var j = i
            if isSilent(input[i]) {
                while j < n && isSilent(input[j]) { j += 1 }
                withheld += j - i
            } else {
                release(into: &out, at: position + i)
                while j < n && !isSilent(input[j]) { j += 1 }
                out.append(contentsOf: UnsafeBufferPointer(rebasing: input[i..<j]))
            }
            i = j
        }
        position += n
    }

    /// The end of the utterance: the silence still held back, at most `tail` samples of it.
    mutating func finish(tail: Int? = nil, into out: inout [Sample]) {
        release(into: &out, at: position, limit: tail)
    }

    /// Where a position in the audio that was read (an event) is in the audio passed on.
    func outputPosition(_ input: Int) -> Int {
        max(input - (cuts.last { $0.position <= input }?.removed ?? 0), 0)
    }
}

/// The whole utterance at once (the app's "Try"; tests).
extension SpeechEngine {
    /// The pauses as the system hears them from the extension (the app's "Try").
    func shortenPauses(_ all: [Int16]) -> [Int16] {
        var shortener = PauseShortener<Int16>(level: 4, sampleRate: sampleRate)
        var samples: [Int16] = []
        all.withUnsafeBufferPointer { shortener.process($0, into: &samples) }
        shortener.finish(into: &samples)
        return samples
    }

    func synthesize(_ plan: SpeechPlan, settings: VoiceSettings) throws -> [Int16] {
        apply(settings, volume: plan.volume)
        guard begin(plan.segments) else { throw SpeechEngineError(message: lastError) }
        var samples: [Int16] = []
        var events: [SpeechEvent] = []
        let block = 4096
        let buffer = UnsafeMutablePointer<Int16>.allocate(capacity: block)
        defer { buffer.deallocate() }
        while true {
            events.removeAll(keepingCapacity: true)
            let n = read(buffer, count: block, events: &events)
            if n < 0 { throw SpeechEngineError(message: lastError) }
            samples.append(contentsOf: UnsafeBufferPointer(start: buffer, count: n))
            if n < block && events.isEmpty { break }
        }
        return samples
    }
}
