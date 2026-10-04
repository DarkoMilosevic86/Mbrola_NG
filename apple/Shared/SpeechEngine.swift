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
        set(MBNG_PARAM_EMOJI, s.emoji ? 1 : 0)
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

/// The whole utterance at once (the app's "Try"; tests).
extension SpeechEngine {
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
