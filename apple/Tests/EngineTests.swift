// MBROLA NG - tests of the catalog, the settings, the resampler and the engine
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import XCTest
@testable import MbrolaNG

final class CatalogTests: XCTestCase {
    func testBundledCatalogAndLanguages() throws {
        let catalog = Catalog.shared
        XCTAssertEqual(catalog.languages.map(\.code), ["hr", "en"])
        for language in catalog.languages {
            XCTAssertNotNil(BundledResources.languageFile(language.code), language.code)
        }
        let cr1 = try XCTUnwrap(catalog.voice("cr1"))
        XCTAssertEqual(cr1.locale, "hr-HR")
        XCTAssertEqual(cr1.alsoFor, ["sr-RS", "bs-BA", "sr-ME"])
        XCTAssertEqual(cr1.basePitch, 105)
        XCTAssertEqual(cr1.phonemeMap, "")
        XCTAssertEqual(cr1.licenseFile, "license.txt")
        XCTAssertEqual(cr1.downloadSize, 3515376 + 1674)
        XCTAssertEqual(try XCTUnwrap(catalog.voice("us1")).locale, "en-US")
        let en1 = try XCTUnwrap(catalog.voice("en1"))
        XCTAssertEqual(en1.locale, "en-GB")
        XCTAssertEqual(en1.phonemeMap, "A=A: AI=aI E=e EI=eI O=O: i=i: k_h=k p_h=p r==3: t_h=t u=u:")
        for voice in catalog.voices {
            XCTAssertFalse(voice.files.isEmpty, voice.id)
            XCTAssertTrue(voice.files.contains { $0.name == voice.id }, voice.id)
            for file in voice.files {
                XCTAssertFalse(file.urls.isEmpty, file.name)
                XCTAssertEqual(file.sha256.count, 64, file.name)
            }
        }
    }

    func testOnlyBundledLanguagesAndHttpsAddresses() throws {
        let json = """
        {"languages":[{"code":"hr"},{"code":"xx"}],
         "voices":[{"id":"v1","language":"hr","files":[{"name":"v1","urls":["http://a/b","https://c/d"]}]},
                   {"id":"v2","language":"xx"}]}
        """
        let catalog = try Catalog(json: Data(json.utf8)) { $0 == "hr" }
        XCTAssertEqual(catalog.voices.map(\.id), ["v1"])
        XCTAssertEqual(catalog.voices[0].files[0].urls.map(\.absoluteString), ["https://c/d"])
        XCTAssertEqual(catalog.voices[0].locale, "hr")
    }

    func testSystemIdentifier() {
        XCTAssertEqual(Voice.id(fromSystemIdentifier: "cr1"), "cr1")
        XCTAssertEqual(Voice.id(fromSystemIdentifier: "io.github.darkomilosevic86.mbrolang.speech.us1"), "us1")
    }
}

final class VoiceSettingsTests: XCTestCase {
    func testDecodingIsTolerantAndClamped() throws {
        let s = try JSONDecoder().decode(
            VoiceSettings.self, from: Data(#"{"rate":9999,"pitch":1,"numbers":7,"future":true}"#.utf8))
        XCTAssertEqual(s.rate, VoiceSettings.rateRange.upperBound)
        XCTAssertEqual(s.pitch, VoiceSettings.pitchRange.lowerBound)
        XCTAssertEqual(s.modulation, 100)
        XCTAssertEqual(s.numbers, .whole)
        XCTAssertTrue(s.emoji)
    }

    func testRoundTrip() throws {
        var s = VoiceSettings()
        s.rate = 150
        s.emoji = false
        s.numbers = .pairs
        XCTAssertEqual(try JSONDecoder().decode(VoiceSettings.self, from: JSONEncoder().encode(s)), s)
    }
}

final class AudioTests: XCTestCase {
    func testWavFile() {
        let wav = wavFile([0, 1, -1, 32767], sampleRate: 16000)
        XCTAssertEqual(wav.count, 44 + 8)
        XCTAssertEqual(String(decoding: wav[0..<4], as: UTF8.self), "RIFF")
        XCTAssertEqual(Array(wav[24..<28]), [0x80, 0x3E, 0, 0])      // 16000
        XCTAssertEqual(Array(wav[44...]), [0, 0, 1, 0, 0xFF, 0xFF, 0xFF, 0x7F])
    }

    /// A 22.05 kHz sine must come out as the same sine at 16 kHz, complete,
    /// whatever the block sizes are.
    func testResampler() {
        let inRate = 22050, outRate = 16000, total = 22050, frequency = 440.0
        var position = 0
        var resampler = Resampler(from: inRate, to: outRate)
        var output: [Float32] = []
        var eventSamples: [Int] = []
        let block = UnsafeMutablePointer<Float32>.allocate(capacity: 700)
        defer { block.deallocate() }
        var ended = false
        var sizes = [1, 700, 333, 512].makeIterator()
        while !ended {
            let want = sizes.next() ?? 512
            let n = resampler.render(block, frames: want) { buffer, count, events in
                let n = min(count, total - position)  // like the engine: short only at the end
                for i in 0..<n {
                    buffer[i] = Int16(20000 * sin(2 * .pi * frequency * Double(position + i) / Double(inRate)))
                }
                if position == 0 && n > 0 {
                    events.append(SpeechEvent(kind: .word, sample: 11025, textOffset: 0, textLength: 1, markIndex: 0))
                }
                position += n
                return n
            } events: { eventSamples.append(contentsOf: $0.map(\.sample)) }
            output.append(contentsOf: UnsafeBufferPointer(start: block, count: n))
            ended = n < want
        }
        XCTAssertEqual(Double(output.count), Double(total) * Double(outRate) / Double(inRate), accuracy: 2)
        XCTAssertEqual(eventSamples, [8000])
        var worst = 0.0
        for (i, sample) in output.dropLast().enumerated() {  // the very last sample has no right neighbour
            let expected = 20000.0 / 32768.0 * sin(2 * .pi * frequency * Double(i) / Double(outRate))
            worst = max(worst, abs(Double(sample) - expected))
        }
        XCTAssertLessThan(worst, 0.01)
    }
}

/// Needs an installed voice (the app's container): skipped otherwise.
final class EngineTests: XCTestCase {
    private func engine(_ id: String) throws -> (SpeechEngine, Voice) {
        let voice = try XCTUnwrap(Catalog.shared.voice(id))
        try XCTSkipUnless(VoiceStore.isInstalled(id), "voice \(id) is not installed")
        return (try SpeechEngine(voice: voice), voice)
    }

    func testSynthesisAndEvents() throws {
        for (id, text, words) in [("cr1", "Dobar dan, svijete.", 3), ("us1", "Hello big world.", 3)] {
            let (engine, _) = try engine(id)
            XCTAssertEqual(engine.sampleRate, 16000)
            let plan = SpeechPlan(text, ssml: false)
            engine.apply(VoiceSettings())
            XCTAssertTrue(engine.begin(plan.segments))
            var samples = 0, peak = 0
            var events: [SpeechEvent] = []
            let buffer = UnsafeMutablePointer<Int16>.allocate(capacity: 512)
            defer { buffer.deallocate() }
            while true {
                let before = events.count
                let n = engine.read(buffer, count: 512, events: &events)
                XCTAssertGreaterThanOrEqual(n, 0)
                for i in 0..<n { peak = max(peak, abs(Int(buffer[i]))) }
                samples += n
                if n < 512 && events.count == before { break }
            }
            XCTAssertGreaterThan(samples, 8000, id)
            XCTAssertGreaterThan(peak, 3000, id)
            let wordEvents = events.filter { $0.kind == .word }
            XCTAssertEqual(wordEvents.count, words, id)
            XCTAssertEqual(wordEvents.map(\.sample), wordEvents.map(\.sample).sorted())
            XCTAssertEqual(plan.sourceRange(offset: wordEvents[0].textOffset, length: wordEvents[0].textLength),
                           NSRange(location: 0, length: 5))
            XCTAssertEqual(events.last?.kind, .end)
            XCTAssertEqual(events.last?.sample, samples)
        }
    }

    func testRateVolumeAndCancel() throws {
        let (engine, _) = try engine("cr1")
        let plan = SpeechPlan("Ovo je rečenica za provjeru brzine i glasnoće govora.", ssml: false)
        let normal = try engine.synthesize(plan, settings: VoiceSettings())
        var fast = VoiceSettings()
        fast.rate = 200
        XCTAssertLessThan(Double(try engine.synthesize(plan, settings: fast).count), Double(normal.count) * 0.7)
        var quiet = VoiceSettings()
        quiet.volume = 50
        let half = try engine.synthesize(plan, settings: quiet)
        func rms(_ samples: [Int16]) -> Double {
            (samples.reduce(0.0) { $0 + Double($1) * Double($1) } / Double(max(samples.count, 1))).squareRoot()
        }
        XCTAssertEqual(rms(half) / rms(normal), 0.5, accuracy: 0.05)

        // cancel in the middle: the utterance ends at once, the next one is whole
        engine.apply(VoiceSettings())
        XCTAssertTrue(engine.begin(plan.segments))
        let buffer = UnsafeMutablePointer<Int16>.allocate(capacity: 512)
        defer { buffer.deallocate() }
        var events: [SpeechEvent] = []
        XCTAssertEqual(engine.read(buffer, count: 512, events: &events), 512)
        engine.cancel()
        XCTAssertEqual(engine.read(buffer, count: 512, events: &events), 0)
        XCTAssertEqual(try engine.synthesize(plan, settings: VoiceSettings()).count, normal.count, accuracy: 400)
    }

    func testEmptyAndOddUtterances() throws {
        let (engine, _) = try engine("us1")
        for text in ["", " ", "\n\n", "...", "😀", "a", String(repeating: "word ", count: 2000)] {
            _ = try engine.synthesize(SpeechPlan(text, ssml: false), settings: VoiceSettings())
        }
        // only marks and pauses
        let plan = SpeechPlan(#"<speak><mark name="a"/><break time="100ms"/><mark name="b"/></speak>"#)
        XCTAssertEqual(try engine.synthesize(plan, settings: VoiceSettings()).count, 1600, accuracy: 200)
    }
}
