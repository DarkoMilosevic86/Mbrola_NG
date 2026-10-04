// MBROLA NG - SSML of a speech request -> segments of the core
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

/// One step of an utterance, in the order it is spoken (mbng_segment).
enum SpeechSegment: Equatable, Sendable {
    /// `base`: index of the text's first UTF-16 unit in `SpeechPlan.sourceStart`.
    case text(String, base: Int)
    case mark(index: Int, name: String)
    case pause(milliseconds: Int)
    case rate(percent: Int)
    case pitch(percent: Int)
    case spell(Bool)
}

/// The system hands every request to the extension as SSML (W3C Speech
/// Synthesis Markup Language): the text, wrapped in the speed, pitch and
/// volume VoiceOver or the app asked for. This is the same small, forgiving
/// parser as the Speech Dispatcher module's (linux/speechd/ssml.cpp): speak,
/// prosody, break, mark, say-as, sub and p/s are understood, every other tag
/// is dropped and its content spoken. Unlike there, <s> does not start a
/// paragraph: a sentence that brings no punctuation is followed by the pause
/// of a comma (see `case "s"`). In addition it remembers where each
/// character of the spoken text stands in the SSML, because the system wants
/// the word positions it highlights expressed in the SSML string.
struct SpeechPlan: Equatable, Sendable {
    var segments: [SpeechSegment] = []
    /// Volume of the outermost <prosody>, 1 = unchanged.
    var volume = 1.0
    var marks: [String] = []
    /// For every UTF-16 unit of all text segments together: the range of the
    /// SSML string (UTF-16 offsets) it was read from.
    var sourceStart: [Int32] = []
    var sourceEnd: [Int32] = []

    /// Range in the SSML string of `length` units of spoken text at `offset`
    /// (the unit the core reports word positions in).
    func sourceRange(offset: Int, length: Int) -> NSRange? {
        guard length > 0, offset >= 0, offset + length <= sourceStart.count else { return nil }
        let start = Int(sourceStart[offset])
        let end = Int(sourceEnd[offset + length - 1])
        return end > start ? NSRange(location: start, length: end - start) : nil
    }

    init() {}

    /// `ssml` = false: the whole input is plain text.
    init(_ input: String, ssml: Bool = true) {
        var parser = Parser(Array(input.utf16))
        if ssml {
            parser.parse()
        } else {
            parser.plainText()
        }
        self = parser.plan
    }
}

private struct Parser {
    var plan = SpeechPlan()
    private let s: [UInt16]

    // text not yet turned into a segment, with the source range of each unit
    private var pending: [UInt16] = []
    private var pendingStart: [Int32] = []
    private var pendingEnd: [Int32] = []

    private struct Frame {
        var name: String
        var rate: Double
        var pitch: Double
        var spell: Bool
        var skip: Bool
    }
    private var stack: [Frame] = []
    private var rate = 1.0
    private var pitch = 1.0
    private var spell = 0   // nesting depth of say-as characters
    private var skip = 0    // nesting depth of ignored content (<sub> body, <desc>)
    private var volumeSeen = false
    private var softLines = false   // SSML: see softenLineBreaks()
    private var sawText = false     // the request has text of its own (not only separators put here)

    /// A separator that had no text before it to follow: see flush().
    private struct Held {
        var text: [UInt16]
        var start: [Int32]
        var end: [Int32]
        var at: Int             // plan.segments.count when it was put
    }
    private var held: Held?

    init(_ units: [UInt16]) { s = units }

    // ------------------------------------------------------------ output
    private mutating func put(_ unit: UInt16, from start: Int, to end: Int) {
        pending.append(unit)
        pendingStart.append(Int32(start))
        pendingEnd.append(Int32(end))
    }

    private mutating func put(_ text: String, from start: Int, to end: Int) {
        for u in text.utf16 { put(u, from: start, to: end) }
    }

    private mutating func put(scalar: UInt32, from start: Int, to end: Int) {
        let valid = Unicode.Scalar(scalar).flatMap { $0.value == 0 ? nil : $0 } ?? "\u{FFFD}"
        for u in String(Character(valid)).utf16 { put(u, from: start, to: end) }
    }

    /// A line break inside the text of a request is not the end of a sentence
    /// or of a paragraph, as it is to the core: VoiceOver reads the lines of a
    /// label ("Heineken\nSponsored, Public\n...") like the parts of a list.
    /// One line break becomes the pause of a comma (nothing, when the line
    /// ends with punctuation of its own), an empty line the pause of a full
    /// stop. The paragraph separator the parser itself puts for <p> stays.
    private mutating func softenLineBreaks() {
        guard pending.contains(where: isLineBreak) else { return }
        var text: [UInt16] = []
        var starts: [Int32] = []
        var ends: [Int32] = []
        var i = 0
        while i < pending.count {
            var j = i
            var lines = 0
            while j < pending.count, isSpace(pending[j]) || isLineBreak(pending[j]) {
                if isLineBreak(pending[j]), !(pending[j] == 0x0A && j > i && pending[j - 1] == 0x0D) { lines += 1 }
                j += 1
            }
            if lines == 0 {
                j = max(j, i + 1)
                text.append(contentsOf: pending[i..<j])
                starts.append(contentsOf: pendingStart[i..<j])
                ends.append(contentsOf: pendingEnd[i..<j])
            } else {
                let punctuated = text.last.map { Array(".,;:!?\u{2026}".utf16).contains($0) } ?? false
                let separator = lines > 1 ? "\n" : punctuated ? " " : ", "
                for u in separator.utf16 {
                    text.append(u)
                    starts.append(pendingStart[i])
                    ends.append(pendingEnd[j - 1])
                }
            }
            i = j
        }
        pending = text
        pendingStart = starts
        pendingEnd = ends
    }

    /// A comma or space at the very end would only make the last phrase sound
    /// unfinished (the separators put between lines and sentences).
    /// Text that is spelled, or that is nothing but commas, is said as it is.
    private mutating func trimEnd() {
        let untrimmed = plan
        defer {
            let spoken = plan.segments.contains { if case .text = $0 { return true } else { return false } }
            if !spoken && sawText { plan = untrimmed }
        }
        while let k = plan.segments.lastIndex(where: { if case .text = $0 { return true } else { return false } }),
              case .text(let text, let base) = plan.segments[k] {
            var spelled = false
            for segment in plan.segments[..<k] { if case .spell(let on) = segment { spelled = on } }
            if spelled { return }
            var units = Array(text.utf16)
            while let last = units.last, isSpace(last) || last == 0x2C { units.removeLast() }
            let removed = text.utf16.count - units.count
            plan.sourceStart.removeLast(removed)
            plan.sourceEnd.removeLast(removed)
            if !units.isEmpty {
                plan.segments[k] = .text(String(decoding: units, as: UTF16.self), base: base)
                return
            }
            plan.segments.remove(at: k)  // nothing but separators: look at the text before
        }
    }

    /// `separator`: the pending text is a pause the parser put itself (", "
    /// between sentences, lines and short breaks), not text of the request.
    ///
    /// Such a comma must never be heard. The core spells a text that is one
    /// character (the echo of a typed key), and every text segment is a text
    /// of its own to it: ", " alone between two spelled characters
    /// (<say-as>a</say-as><break/><say-as>b</say-as>, VoiceOver on iOS
    /// spelling) was read as "a comma b comma", and so was a comma put
    /// inside <say-as>. A separator is
    /// therefore only text next to other text: it follows the text before it
    /// or waits for the text after it. Where there is neither (spelled
    /// characters, a change of speed or pitch on both sides) it becomes a
    /// break, which the core follows with the pause of a comma; inside
    /// spelled text, where the characters are said one by one anyway, and
    /// at the end of the request it is nothing.
    private mutating func flush(separator: Bool = false) {
        defer {
            pending.removeAll(keepingCapacity: true)
            pendingStart.removeAll(keepingCapacity: true)
            pendingEnd.removeAll(keepingCapacity: true)
        }
        if pending.isEmpty || skip > 0 { return }
        var separator = separator
        let blank = !pending.contains { !isSpace($0) && !isLineBreak($0) }
        if softLines && spell == 0 {
            softenLineBreaks()
            if blank && pending.contains(0x2C) { separator = true }  // a line break between two tags
        }
        var hasTextBefore = false  // (spaces alone are not text to follow)
        if case .text(let previous)? = plan.segments.last { hasTextBefore = previous.0.utf16.contains { !isSpace($0) } }
        if separator || (blank && spell == 0 && held?.at == plan.segments.count) {
            if separator && spell > 0 { return }
            if held != nil {
                // more separators, or spaces after one, wait with it
                held!.text += pending
                held!.start += pendingStart
                held!.end += pendingEnd
                return
            }
            if !hasTextBefore {
                held = Held(text: pending, start: pendingStart, end: pendingEnd, at: plan.segments.count)
                return
            }
        } else if let h = held {
            if !blank {
                held = nil
                if spell == 0 && h.at == plan.segments.count {
                    pending = h.text + pending
                    pendingStart = h.start + pendingStart
                    pendingEnd = h.end + pendingEnd
                } else {
                    plan.segments.insert(.pause(milliseconds: Parser.separatorPause), at: h.at)
                }
            }
        }
        let text = String(decoding: pending, as: UTF16.self)
        if case .text(let previous, let base)? = plan.segments.last {
            plan.segments[plan.segments.count - 1] = .text(previous + text, base: base)
        } else {
            plan.segments.append(.text(text, base: plan.sourceStart.count))
        }
        plan.sourceStart.append(contentsOf: pendingStart)
        plan.sourceEnd.append(contentsOf: pendingEnd)
    }

    // ------------------------------------------------------------- input
    private func isSpace(_ u: UInt16) -> Bool { u == 0x20 || u == 0x09 || u == 0x0A || u == 0x0D }
    private func isLineBreak(_ u: UInt16) -> Bool { u == 0x0A || u == 0x0D || u == 0x85 || u == 0x2028 }
    private func isAlpha(_ u: UInt16) -> Bool { (u >= 0x41 && u <= 0x5A) || (u >= 0x61 && u <= 0x7A) }

    private func matches(_ literal: String, at i: Int) -> Bool {
        let l = Array(literal.utf16)
        return i + l.count <= s.count && Array(s[i..<(i + l.count)]) == l
    }

    private func find(_ literal: String, from i: Int) -> Int? {
        let l = Array(literal.utf16)
        guard !l.isEmpty, i <= s.count - l.count else { return nil }
        var k = i
        while k <= s.count - l.count {
            if s[k] == l[0] && Array(s[k..<(k + l.count)]) == l { return k }
            k += 1
        }
        return nil
    }

    private func string(_ range: Range<Int>) -> String {
        String(decoding: s[range], as: UTF16.self)
    }

    /// Decodes the entity at `i` (s[i] == "&"): the scalar and the index after ";".
    private func entity(at i: Int) -> (UInt32, Int)? {
        var semi = i + 1
        while semi < s.count && semi - i <= 12 && s[semi] != 0x3B { semi += 1 }
        guard semi < s.count, s[semi] == 0x3B, semi - i <= 12 else { return nil }
        let name = string((i + 1)..<semi)
        let value: UInt32?
        switch name {
        case "lt": value = 0x3C
        case "gt": value = 0x3E
        case "amp": value = 0x26
        case "quot": value = 0x22
        case "apos": value = 0x27
        case "nbsp": value = 0xA0
        default:
            if name.hasPrefix("#x") || name.hasPrefix("#X") {
                value = UInt32(name.dropFirst(2), radix: 16)
            } else if name.hasPrefix("#") {
                value = UInt32(name.dropFirst(1), radix: 10)
            } else {
                value = nil
            }
        }
        guard let v = value else { return nil }
        return (v, semi + 1)
    }

    static func decodeEntities(_ text: String) -> String {
        var p = Parser(Array(text.utf16))
        var i = 0
        while i < p.s.count {
            if p.s[i] == 0x26, let (scalar, next) = p.entity(at: i) {
                p.put(scalar: scalar, from: i, to: next)
                i = next
            } else {
                p.put(p.s[i], from: i, to: i + 1)
                i += 1
            }
        }
        return String(decoding: p.pending, as: UTF16.self)
    }

    private struct Tag {
        var name = ""           // lower case, without namespace prefix
        var closing = false
        var empty = false       // <x/>
        var attributes: [String: String] = [:]
    }

    /// Parses the inside of <...> (without the brackets).
    private func tag(_ range: Range<Int>) -> Tag {
        var t = Tag()
        var i = range.lowerBound
        let n = range.upperBound
        func skipSpace() { while i < n && isSpace(s[i]) { i += 1 } }
        skipSpace()
        if i < n && s[i] == 0x2F {
            t.closing = true
            i += 1
        }
        var start = i
        while i < n && !isSpace(s[i]) && s[i] != 0x2F { i += 1 }
        t.name = string(start..<i).lowercased()
        if let colon = t.name.lastIndex(of: ":") { t.name = String(t.name[t.name.index(after: colon)...]) }
        while i < n {
            skipSpace()
            if i >= n { break }
            if s[i] == 0x2F {
                t.empty = true
                i += 1
                continue
            }
            start = i
            while i < n && s[i] != 0x3D && !isSpace(s[i]) && s[i] != 0x2F { i += 1 }
            let key = string(start..<i).lowercased()
            skipSpace()
            var value = ""
            if i < n && s[i] == 0x3D {
                i += 1
                skipSpace()
                if i < n && (s[i] == 0x22 || s[i] == 0x27) {
                    let quote = s[i]
                    i += 1
                    start = i
                    while i < n && s[i] != quote { i += 1 }
                    value = string(start..<i)
                    if i < n { i += 1 }
                } else {
                    start = i
                    while i < n && !isSpace(s[i]) && s[i] != 0x2F { i += 1 }
                    value = string(start..<i)
                }
            }
            if !key.isEmpty { t.attributes[key] = Parser.decodeEntities(value) }
        }
        return t
    }

    // ------------------------------------------------------------ values
    /// Leading number of "12.5%", "+3st", "-20": the value and the rest (the unit).
    private static func number(_ v: String) -> (Double, String)? {
        var end = v.startIndex
        var seenDigit = false
        var seenDot = false
        for (k, c) in zip(v.indices, v) {
            if c.isASCII && c.isNumber {
                seenDigit = true
            } else if c == "." && !seenDot {
                seenDot = true
            } else if (c == "+" || c == "-") && k == v.startIndex {
                // sign
            } else {
                break
            }
            end = v.index(after: k)
        }
        guard seenDigit, let x = Double(v[..<end]) else { return nil }
        return (x, v[end...].trimmingCharacters(in: .whitespaces))
    }

    /// "500ms", "1.5s"
    private static func milliseconds(_ v: String?) -> Int? {
        guard let v = v?.lowercased(), let (x, unit) = number(v), x >= 0 else { return nil }
        let ms: Double
        switch unit {
        case "s": ms = x * 1000
        case "ms", "": ms = x
        default: return nil
        }
        return Int(min(ms, 60000) + 0.5)
    }

    /// VoiceOver on the Mac puts <break time="60ms"/> between the parts of
    /// what it says ("Photos", "widget"). A break ends the phrase before it,
    /// which the core follows with the pause of a comma by itself - longer
    /// than such a break. Below this length (ms) a break therefore is that
    /// comma and nothing more; a longer one is silence in addition to it.
    static let phraseBreak = 100

    /// The break (ms) a separator becomes where it cannot be a comma in text
    /// (see flush()): it only has to end the phrase.
    static let separatorPause = 1

    private static func breakStrength(_ v: String?) -> Int {
        switch v?.lowercased() {
        case "none": return 0
        case "x-weak": return 100
        case "weak": return 200
        case "strong": return 700
        case "x-strong": return 1200
        default: return 400
        }
    }

    /// prosody rate / pitch value -> factor (1 = unchanged), relative to `current`.
    private static func factor(_ raw: String?, rate isRate: Bool, current: Double) -> Double {
        guard let v = raw?.trimmingCharacters(in: .whitespaces).lowercased(), !v.isEmpty else { return current }
        switch (v, isRate) {
        case ("default", _), ("medium", _): return 1
        case ("x-slow", true): return 0.5
        case ("slow", true): return 0.75
        case ("fast", true): return 1.5
        case ("x-fast", true): return 2
        case ("x-low", false): return 0.7
        case ("low", false): return 0.85
        case ("high", false): return 1.2
        case ("x-high", false): return 1.4
        default: break
        }
        guard let (x, unit) = number(v) else { return current }
        let relative = v.hasPrefix("+") || v.hasPrefix("-")
        let f: Double
        switch unit {
        case "%": f = relative ? current * (1 + x / 100) : x / 100
        case "st": f = current * pow(2, x / 12)
        case "" where isRate: f = x                    // SSML 1.0: rate as a multiplier
        default: return current                        // Hz and other units: unsupported
        }
        return min(max(f, 0.1), 8)
    }

    /// prosody volume -> factor (1 = unchanged).
    private static func volume(_ raw: String?) -> Double? {
        guard let v = raw?.trimmingCharacters(in: .whitespaces).lowercased(), !v.isEmpty else { return nil }
        switch v {
        case "default", "medium": return 1
        case "silent": return 0
        case "x-soft": return 0.25
        case "soft": return 0.5
        case "loud": return 1.5
        case "x-loud": return 2
        default: break
        }
        guard let (x, unit) = number(v) else { return nil }
        let relative = v.hasPrefix("+") || v.hasPrefix("-")
        let f: Double
        switch unit {
        case "db": f = pow(10, x / 20)                 // SSML 1.1: a change in decibels
        case "%": f = relative ? 1 + x / 100 : x / 100
        case "": f = relative ? 1 + x / 100 : x / 100  // SSML 1.0: 0 ... 100
        default: return nil
        }
        return min(max(f, 0), 4)
    }

    // ------------------------------------------------------------- parse
    mutating func plainText() {
        for (i, u) in s.enumerated() { put(u, from: i, to: i + 1) }
        flush()
    }

    mutating func parse() {
        softLines = true
        defer { trimEnd() }
        var i = 0
        let n = s.count
        while i < n {
            let c = s[i]
            if c == 0x26 {  // &
                sawText = true
                if let (scalar, next) = entity(at: i) {
                    put(scalar: scalar, from: i, to: next)
                    i = next
                } else {
                    put(c, from: i, to: i + 1)
                    i += 1
                }
                continue
            }
            if c != 0x3C {  // <
                if !isSpace(c) { sawText = true }
                put(c, from: i, to: i + 1)
                i += 1
                continue
            }
            // comments, CDATA, processing instructions, doctype
            if matches("<!--", at: i) {
                i = find("-->", from: i + 4).map { $0 + 3 } ?? n
                continue
            }
            if matches("<![CDATA[", at: i) {
                let end = find("]]>", from: i + 9) ?? n
                sawText = true
                for k in (i + 9)..<max(end, i + 9) where k < n { put(s[k], from: k, to: k + 1) }
                i = min(end + 3, n)
                continue
            }
            let next: UInt16 = i + 1 < n ? s[i + 1] : 0
            guard let close = find(">", from: i + 1),
                  isAlpha(next) || next == 0x2F || next == 0x3F || next == 0x21 || next == 0x5F
            else {  // a lone "<": plain text
                put(c, from: i, to: i + 1)
                i += 1
                continue
            }
            let tagStart = i
            let body = (i + 1)..<close
            i = close + 1
            if next == 0x3F || next == 0x21 { continue }
            flush()
            let t = tag(body)
            if t.name.isEmpty { continue }

            if t.closing {
                // pop up to the matching element (tolerates missing end tags)
                guard let k = stack.lastIndex(where: { $0.name == t.name }) else { continue }
                while stack.count > k {
                    let f = stack.removeLast()
                    if f.spell {
                        spell -= 1
                        if spell == 0 { plan.segments.append(.spell(false)) }
                    }
                    if f.skip { skip -= 1 }
                    if f.rate != rate {
                        rate = f.rate
                        plan.segments.append(.rate(percent: Int(rate * 100 + 0.5)))
                    }
                    if f.pitch != pitch {
                        pitch = f.pitch
                        plan.segments.append(.pitch(percent: Int(pitch * 100 + 0.5)))
                    }
                    if f.name == "p" || f.name == "s" {
                        put(f.name == "p" ? "\u{2029}" : " ", from: tagStart, to: i)
                        flush(separator: f.name == "s")
                    }
                }
                continue
            }

            switch t.name {
            case "mark":
                if skip == 0 {
                    let name = t.attributes["name"] ?? ""
                    plan.segments.append(.mark(index: plan.marks.count, name: name))
                    plan.marks.append(name)
                }
            case "break":
                if skip == 0 {
                    let ms = Parser.milliseconds(t.attributes["time"])
                        ?? Parser.breakStrength(t.attributes["strength"])
                    if ms >= Parser.phraseBreak {
                        plan.segments.append(.pause(milliseconds: ms))
                    } else if ms > 0 {
                        put(", ", from: tagStart, to: i)
                        flush(separator: true)
                    }
                }
            default:
                if t.empty { break }  // <audio/> and the like: nothing to say
                var f = Frame(name: t.name, rate: rate, pitch: pitch, spell: false, skip: false)
                switch t.name {
                case "prosody":
                    let newRate = Parser.factor(t.attributes["rate"], rate: true, current: rate)
                    let newPitch = Parser.factor(t.attributes["pitch"], rate: false, current: pitch)
                    if newRate != rate {
                        rate = newRate
                        plan.segments.append(.rate(percent: Int(rate * 100 + 0.5)))
                    }
                    if newPitch != pitch {
                        pitch = newPitch
                        plan.segments.append(.pitch(percent: Int(pitch * 100 + 0.5)))
                    }
                    // the engine has one volume per utterance: the first one given
                    if !volumeSeen, let v = Parser.volume(t.attributes["volume"]) {
                        plan.volume = v
                        volumeSeen = true
                    }
                case "say-as":
                    let how = (t.attributes["interpret-as"] ?? "").lowercased()
                    if ["characters", "spell-out", "character", "char", "verbatim"].contains(how) {
                        f.spell = true
                        spell += 1
                        if spell == 1 { plan.segments.append(.spell(true)) }
                    }
                case "sub":
                    if skip == 0, let alias = t.attributes["alias"] {
                        put(alias, from: tagStart, to: i)
                        flush()
                    }
                    f.skip = true
                    skip += 1
                case "desc":
                    f.skip = true
                    skip += 1
                case "p":
                    put("\u{2029}", from: tagStart, to: i)
                    flush()
                case "s":
                    // VoiceOver sends the parts of what it says about an
                    // element as sentences (<s>Settings</s><s>Heading</s>)
                    // and the system's voices read them like "Settings,
                    // Heading". A line break would be a paragraph to the
                    // core: its longest pause, between every two parts.
                    if !plan.sourceStart.isEmpty {
                        put(", ", from: tagStart, to: i)
                        flush(separator: true)
                    }
                default:
                    break
                }
                stack.append(f)
            }
        }
        flush()
        if spell > 0 { plan.segments.append(.spell(false)) }  // unclosed elements
    }
}
