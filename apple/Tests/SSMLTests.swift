// MBROLA NG - tests of the SSML parser
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import XCTest
@testable import MbrolaNG

final class SSMLTests: XCTestCase {
    /// The part of the SSML a piece of spoken text came from.
    private func source(_ plan: SpeechPlan, _ ssml: String, offset: Int, length: Int) -> String? {
        plan.sourceRange(offset: offset, length: length).map { (ssml as NSString).substring(with: $0) }
    }

    // What the system really sends (captured from AVSpeechSynthesizer, macOS 27).
    func testSystemRequestPlain() {
        let plan = SpeechPlan("<speak>Hello world</speak>")
        XCTAssertEqual(plan.segments, [.text("Hello world", base: 0)])
        XCTAssertEqual(plan.volume, 1)
    }

    func testSystemRequestRate() {
        XCTAssertEqual(
            SpeechPlan(#"<speak><prosody rate="12.5%">Hi</prosody></speak>"#).segments,
            [.rate(percent: 13), .text("Hi", base: 0), .rate(percent: 100)])
        XCTAssertEqual(
            SpeechPlan(#"<speak><prosody rate="160.00002%">Hi</prosody></speak>"#).segments,
            [.rate(percent: 160), .text("Hi", base: 0), .rate(percent: 100)])
        XCTAssertEqual(
            SpeechPlan(#"<speak><prosody rate="400.0%">Hi</prosody></speak>"#).segments.first,
            .rate(percent: 400))
    }

    func testSystemRequestPitchAndVolume() {
        var plan = SpeechPlan(#"<speak><prosody pitch="-25.0%" volume="silent">Hi</prosody></speak>"#)
        XCTAssertEqual(plan.segments, [.pitch(percent: 75), .text("Hi", base: 0), .pitch(percent: 100)])
        XCTAssertEqual(plan.volume, 0)
        plan = SpeechPlan(#"<speak><prosody pitch="+100.0%" volume="-6.0206003dB">Hi</prosody></speak>"#)
        XCTAssertEqual(plan.segments.first, .pitch(percent: 200))
        XCTAssertEqual(plan.volume, 0.5, accuracy: 0.001)
        plan = SpeechPlan(#"<speak><prosody pitch="+29.999996%" volume="-12.041201dB">Hi</prosody></speak>"#)
        XCTAssertEqual(plan.segments.first, .pitch(percent: 130))
        XCTAssertEqual(plan.volume, 0.25, accuracy: 0.001)
    }

    /// `say "[[rate 400]] embedded [[slnc 200]] command"`
    func testSystemRequestEmbeddedCommands() {
        let ssml = #"<speak>            <prosody rate="208.00002%"> embedded <break time="200ms"/></prosody>            <prosody rate="208.00002%"> command</prosody></speak>"#
        let plan = SpeechPlan(ssml)
        XCTAssertEqual(plan.segments.filter { if case .pause = $0 { return true } else { return false } },
                       [.pause(milliseconds: 200)])
        XCTAssertEqual(plan.segments.filter { $0 == .rate(percent: 208) }.count, 2)
        XCTAssertEqual(plan.segments.last, .rate(percent: 100))
    }

    func testEntitiesAndSourceRanges() {
        let ssml = "<speak>Hello &lt;there&gt; &amp; 5 &gt; 4 &#x161;&#273;</speak>"
        let plan = SpeechPlan(ssml)
        XCTAssertEqual(plan.segments, [.text("Hello <there> & 5 > 4 šđ", base: 0)])
        XCTAssertEqual(source(plan, ssml, offset: 0, length: 5), "Hello")
        XCTAssertEqual(source(plan, ssml, offset: 6, length: 7), "&lt;there&gt;")
        XCTAssertEqual(source(plan, ssml, offset: 14, length: 1), "&amp;")
        XCTAssertEqual(source(plan, ssml, offset: 22, length: 2), "&#x161;&#273;")
        XCTAssertNil(plan.sourceRange(offset: 23, length: 5))
        XCTAssertNil(plan.sourceRange(offset: 0, length: 0))
    }

    func testSourceRangesAcrossSegmentsAndEmoji() {
        let ssml = #"<speak>One 😀 <prosody rate="150%">two</prosody> three</speak>"#
        let plan = SpeechPlan(ssml)
        guard case .text(let first, let base0) = plan.segments[0],
              case .text(let second, let base1) = plan.segments[2],
              case .text(let third, let base2) = plan.segments[4]
        else { return XCTFail("\(plan.segments)") }
        XCTAssertEqual([first, second, third], ["One 😀 ", "two", " three"])
        XCTAssertEqual(base0, 0)
        XCTAssertEqual(base1, first.utf16.count)
        XCTAssertEqual(base2, first.utf16.count + second.utf16.count)
        XCTAssertEqual(source(plan, ssml, offset: 4, length: 2), "😀")       // a surrogate pair
        XCTAssertEqual(source(plan, ssml, offset: base1, length: 3), "two")
        XCTAssertEqual(source(plan, ssml, offset: base2 + 1, length: 5), "three")
    }

    func testNestedProsodyIsRestored() {
        let plan = SpeechPlan(
            #"<speak><prosody rate="200%">a<prosody rate="+50%" pitch="+2st">b</prosody>c</prosody>d</speak>"#)
        XCTAssertEqual(plan.segments, [
            .rate(percent: 200), .text("a", base: 0),
            .rate(percent: 300), .pitch(percent: 112), .text("b", base: 1),
            .rate(percent: 200), .pitch(percent: 100), .text("c", base: 2),
            .rate(percent: 100), .text("d", base: 3),
        ])
    }

    func testNamedValues() {
        XCTAssertEqual(SpeechPlan(#"<speak><prosody rate="x-fast" pitch="low" volume="loud">a</prosody></speak>"#).segments.prefix(2),
                       [.rate(percent: 200), .pitch(percent: 85)])
        XCTAssertEqual(SpeechPlan(#"<speak><prosody volume="loud">a</prosody></speak>"#).volume, 1.5)
        XCTAssertEqual(SpeechPlan(#"<speak><prosody volume="50">a</prosody></speak>"#).volume, 0.5)
        XCTAssertEqual(SpeechPlan(#"<speak><prosody volume="+6dB">a</prosody></speak>"#).volume, 1.995, accuracy: 0.01)
        // unknown units leave the value alone
        XCTAssertEqual(SpeechPlan(#"<speak><prosody pitch="120Hz" rate="fastest">a</prosody></speak>"#).segments,
                       [.text("a", base: 0)])
    }

    func testBreakMarkSayAsSub() {
        let ssml = #"<speak>Plain <break time="1.5s"/> and <break strength="strong"/><say-as interpret-as="characters">abc</say-as> <mark name="m1"/> <sub alias="World Wide Web">WWW</sub> end</speak>"#
        let plan = SpeechPlan(ssml)
        XCTAssertEqual(plan.segments, [
            .text("Plain ", base: 0), .pause(milliseconds: 1500), .text(" and ", base: 6),
            .pause(milliseconds: 700), .spell(true), .text("abc", base: 11), .spell(false),
            .text(" ", base: 14), .mark(index: 0, name: "m1"), .text(" World Wide Web end", base: 15),
        ])
        XCTAssertEqual(plan.marks, ["m1"])
        // the substituted words point at the <sub> tag they replace
        XCTAssertEqual(source(plan, ssml, offset: 16, length: 5), #"<sub alias="World Wide Web">"#)
    }

    func testParagraphsAndIgnoredMarkup() {
        let plan = SpeechPlan(
            "<?xml version=\"1.0\"?><!-- c --><speak xmlns=\"http://www.w3.org/2001/10/synthesis\"><p>One</p><s>Two</s><desc>no</desc><voice name=\"x\"><emphasis>Three</emphasis></voice><![CDATA[a < b]]><audio src=\"x\"/></speak>")
        XCTAssertEqual(plan.segments, [.text("\u{2029}One\u{2029}, Two Threea < b", base: 0)])
    }

    /// What VoiceOver says about an element: its name, then what it is.
    func testSystemRequestSentences() {
        let plan = SpeechPlan(
            #"<speak><prosody rate="160.00002%"><s><lang xml:lang="hr-HR">MBROLA NG</lang></s><s><lang xml:lang="hr-HR">Naslov</lang></s></prosody></speak>"#)
        XCTAssertEqual(plan.segments, [.rate(percent: 160), .text("MBROLA NG , Naslov", base: 0), .rate(percent: 100)])
    }

    /// What VoiceOver on the Mac says when a window comes to the front.
    func testSystemRequestShortBreaks() {
        let plan = SpeechPlan(
            #"<speak><prosody pitch="+0.0%" rate="250.0%" volume="+0.0dB"><lang xml:lang="hr"><voice name="">Terminal</voice><voice name=""><break time="250.0ms"/></voice><voice name=""><break time="60.0ms"/>mbrola</voice><voice name=""><break time="60.0ms"/>window</voice><voice name=""><say-as interpret-as="characters">a</say-as><break time="60.0ms"/></voice></lang></prosody></speak>"#)
        XCTAssertEqual(plan.segments, [
            .rate(percent: 250), .text("Terminal", base: 0), .pause(milliseconds: 250),
            .text(", mbrola, window", base: 8), .spell(true), .text("a", base: 24), .spell(false),
            .rate(percent: 100)])
        XCTAssertEqual(plan.sourceStart.count, 25)
    }

    /// The lines of a label are read like the parts of a list.
    func testLineBreaksInText() {
        let ssml = "<speak><s>Heineken  \nSponzorirano, Javno\nHeineken 0.0.\r\nDodatne\n\nSa zvukom\n</s></speak>"
        let plan = SpeechPlan(ssml)
        XCTAssertEqual(plan.segments, [.text("Heineken, Sponzorirano, Javno, Heineken 0.0. Dodatne\nSa zvukom", base: 0)])
        XCTAssertEqual(plan.sourceStart.count, plan.sourceEnd.count)
        XCTAssertEqual(source(plan, ssml, offset: 10, length: 12), "Sponzorirano")
        XCTAssertEqual(SpeechPlan("a\nb", ssml: false).segments, [.text("a\nb", base: 0)])
    }

    /// Separators are only taken away where they are not what is to be said.
    func testCommasThatAreSpoken() {
        XCTAssertEqual(SpeechPlan("<speak>,</speak>").segments, [.text(",", base: 0)])
        XCTAssertEqual(
            SpeechPlan(#"<speak><say-as interpret-as="characters">,</say-as></speak>"#).segments,
            [.spell(true), .text(",", base: 0), .spell(false)])
        XCTAssertEqual(
            SpeechPlan("<speak><say-as interpret-as=\"characters\">\n</say-as></speak>").segments,
            [.spell(true), .text("\n", base: 0), .spell(false)])
        XCTAssertEqual(SpeechPlan(#"<speak><break time="60ms"/></speak>"#).segments, [])
        XCTAssertEqual(SpeechPlan("<speak>a,<break time=\"60ms\"/></speak>").segments, [.text("a", base: 0)])
    }

    func testPlainTextIsNotParsed() {
        let text = "a < b & c <speak>"
        let plan = SpeechPlan(text, ssml: false)
        XCTAssertEqual(plan.segments, [.text(text, base: 0)])
        XCTAssertEqual(plan.sourceRange(offset: 2, length: 1), NSRange(location: 2, length: 1))
    }

    /// Whatever arrives, the parser must neither crash nor lose the text.
    func testMalformedInput() {
        XCTAssertEqual(SpeechPlan("").segments, [])
        XCTAssertEqual(SpeechPlan("5 < 6 and 7 > 3 & more &unknown; &#xZZ; &").segments,
                       [.text("5 < 6 and 7 > 3 & more &unknown; &#xZZ; &", base: 0)])
        XCTAssertEqual(SpeechPlan("<speak>open <prosody rate=\"200%\">never closed").segments,
                       [.text("open ", base: 0), .rate(percent: 200), .text("never closed", base: 5)])
        XCTAssertEqual(SpeechPlan("</prosody></speak>text<").segments, [.text("text<", base: 0)])
        XCTAssertEqual(SpeechPlan("<speak><say-as interpret-as=\"characters\">x").segments,
                       [.spell(true), .text("x", base: 0), .spell(false)])
        XCTAssertEqual(SpeechPlan("<speak><break time=\"-5s\"/><break time=\"999s\"/>a</speak>").segments,
                       [.pause(milliseconds: 400), .pause(milliseconds: 60000), .text("a", base: 0)])
        XCTAssertEqual(SpeechPlan("<speak>&#0;&#x110000;&#xD800;</speak>").segments,
                       [.text("\u{FFFD}\u{FFFD}\u{FFFD}", base: 0)])
        for junk in ["<", "<<>>", "<a", "<![CDATA[", "<!--", "&#;", "<prosody rate=>", "<prosody rate='", "< speak >"] {
            _ = SpeechPlan(junk)
        }
    }
}
