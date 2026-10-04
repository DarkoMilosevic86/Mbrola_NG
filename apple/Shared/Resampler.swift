// MBROLA NG - sample rate conversion for voices that differ from the output bus
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

/// Linear-interpolation sample rate converter for a voice whose database
/// rate differs from the output bus (none of the catalog's voices today).
struct Resampler {
    private let step: Double            // input samples per output sample
    private let scale: Double           // output position per input position (for events)
    private var position = 0.0          // input position of the next output sample
    private var window: [Int16] = []    // input samples from `windowStart` on
    private var windowStart = 0
    private var ended = false
    private var block = [Int16](repeating: 0, count: 2048)

    init(from input: Int, to output: Int) {
        step = Double(input) / Double(output)
        scale = Double(output) / Double(input)
    }

    /// Returns the frames written; fewer than `frames` = the input ended.
    mutating func render(
        _ out: UnsafeMutablePointer<Float32>, frames: Int,
        read: (UnsafeMutablePointer<Int16>, Int, inout [SpeechEvent]) -> Int,
        events deliver: ([SpeechEvent]) -> Void
    ) -> Int {
        var produced = 0
        while produced < frames {
            let index = Int(position)
            // two neighbours are needed: read on until they are there
            while !ended && index + 1 >= windowStart + window.count {
                var events: [SpeechEvent] = []
                let n = block.withUnsafeMutableBufferPointer { read($0.baseAddress!, $0.count, &events) }
                if n > 0 { window.append(contentsOf: block[0..<n]) }
                if n < block.count && events.isEmpty { ended = true }
                deliver(events.map {
                    var e = $0
                    e.sample = Int(Double(e.sample) * scale)
                    return e
                })
            }
            let end = windowStart + window.count
            if index >= end { break }
            let a = Double(window[index - windowStart])
            let b = index + 1 < end ? Double(window[index + 1 - windowStart]) : a
            let t = position - Double(index)
            out[produced] = Float32((a + (b - a) * t) / 32768.0)
            produced += 1
            position += step
            // forget input that is behind
            let keepFrom = Int(position)
            if keepFrom - windowStart > 4096 {
                window.removeFirst(keepFrom - windowStart)
                windowStart = keepFrom
            }
        }
        return produced
    }
}
