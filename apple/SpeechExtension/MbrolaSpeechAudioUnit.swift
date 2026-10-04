// MBROLA NG - the speech synthesis provider (VoiceOver, Spoken Content, apps)
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import AVFoundation
import Accelerate
import os

/// What VoiceOver and every other app talk to: an Audio Unit of type
/// "speech synthesizer" (ausp). The system
///   - asks `speechVoices` which voices exist  (= the installed voices),
///   - hands over a request as SSML            (`synthesizeSpeechRequest`),
///   - pulls the audio through the render block until it is told the
///     utterance is complete, or cancels it    (`cancelSpeechRequest`).
///
/// A speech synthesizer unit is rendered offline (the system pulls as fast as
/// the unit delivers and plays the result itself), so the render block may
/// synthesize: it pulls from the core exactly the samples it was asked for.
/// The first sound is there after well under a millisecond and nothing is
/// synthesized that a cancelled utterance would throw away.
public final class MbrolaSpeechAudioUnit: AVSpeechSynthesisProviderAudioUnit, @unchecked Sendable {
    /// Sample rate of the output bus = the rate of every voice in the
    /// catalog. A voice with another rate is resampled (Synthesis).
    static let outputSampleRate = 16000

    private let outputBus: AUAudioUnitBus
    private var busArray: AUAudioUnitBusArray!
    private let synthesis = Synthesis()

    @objc
    public override init(
        componentDescription: AudioComponentDescription,
        options: AudioComponentInstantiationOptions
    ) throws {
        // mono 32-bit float, the standard format of audio units
        guard let format = AVAudioFormat(
            commonFormat: .pcmFormatFloat32, sampleRate: Double(Self.outputSampleRate),
            channels: 1, interleaved: false)
        else { throw NSError(domain: NSOSStatusErrorDomain, code: Int(kAudioUnitErr_FormatNotSupported)) }
        outputBus = try AUAudioUnitBus(format: format)
        try super.init(componentDescription: componentDescription, options: options)
        busArray = AUAudioUnitBusArray(audioUnit: self, busType: .output, busses: [outputBus])
    }

    public override var outputBusses: AUAudioUnitBusArray { busArray }

    public override var channelCapabilities: [NSNumber]? { [0, 1] }

    public override func allocateRenderResources() throws {
        try super.allocateRenderResources()
        synthesis.prepare(maximumFrames: Int(maximumFramesToRender))
    }

    // ------------------------------------------------------------- voices
    public override var speechVoices: [AVSpeechSynthesisProviderVoice] {
        get {
            VoiceStore.installed().map { voice in
                // the name as the system's voice lists show it; the synthesizer's
                // name comes first, since a voice may be listed without its group
                let v = AVSpeechSynthesisProviderVoice(
                    name: "MBROLA NG " + voice.name, identifier: voice.systemIdentifier,
                    primaryLanguages: [voice.locale],
                    supportedLanguages: [voice.locale] + voice.alsoFor)
                switch voice.gender {
                case "male": v.gender = .male
                case "female": v.gender = .female
                default: v.gender = .unspecified
                }
                v.version = voice.version
                v.voiceSize = VoiceStore.size(voice.id)
                return v
            }
        }
        set {}
    }

    // ------------------------------------------------------------ requests
    public override func synthesizeSpeechRequest(_ speechRequest: AVSpeechSynthesisProviderRequest) {
        synthesis.start(speechRequest)
    }

    public override func cancelSpeechRequest() {
        synthesis.cancel()
    }

    public override var internalRenderBlock: AUInternalRenderBlock {
        let synthesis = self.synthesis
        return { [weak self] actionFlags, _, frameCount, _, outputData, _, _ in
            let buffers = UnsafeMutableAudioBufferListPointer(outputData)
            guard let first = buffers.first else { return kAudioUnitErr_InvalidParameter }
            let frames = Int(frameCount)
            let (target, finished, markers, request) = synthesis.render(frames: frames, into: first.mData)
            guard let target else { return kAudioUnitErr_TooManyFramesToProcess }
            for i in 0..<buffers.count {
                // the host normally supplies the buffer; without one, ours is handed out
                if buffers[i].mData == nil { buffers[i].mData = UnsafeMutableRawPointer(target) }
                else if i > 0 { memcpy(buffers[i].mData, target, frames * MemoryLayout<Float32>.size) }
                buffers[i].mDataByteSize = UInt32(frames * MemoryLayout<Float32>.size)
            }
            if !markers.isEmpty, let request, let deliver = self?.speechSynthesisOutputMetadataBlock {
                deliver(markers, request)
            }
            if finished {
                actionFlags.pointee.insert(.offlineUnitRenderAction_Complete)
            }
            return noErr
        }
    }
}

/// The current utterance and the engines, shared by the thread that brings
/// the requests and the thread that pulls the audio.
private final class Synthesis: @unchecked Sendable {
    private let log = Logger(subsystem: SharedContainer.bundleIdentifier, category: "speech")
    private let lock = NSLock()

    // One engine per voice that has spoken: a voice database stays open and
    // switching between voices (VoiceOver's language rotor) costs nothing.
    private var engines: [String: SpeechEngine] = [:]

    private struct Utterance {
        let engine: SpeechEngine
        let request: AVSpeechSynthesisProviderRequest
        let plan: SpeechPlan
        var resampler: Resampler?
        var lastRange = NSRange(location: NSNotFound, length: 0)
    }
    private var current: Utterance?

    private var pcm = UnsafeMutableBufferPointer<Int16>.allocate(capacity: 4096)
    private var floats = UnsafeMutableBufferPointer<Float32>.allocate(capacity: 4096)
    private var events: [SpeechEvent] = []

    deinit {
        pcm.deallocate()
        floats.deallocate()
    }

    func prepare(maximumFrames: Int) {
        lock.lock()
        defer { lock.unlock() }
        reserve(maximumFrames)
    }

    /// Call with the lock held.
    private func reserve(_ frames: Int) {
        if frames <= pcm.count { return }
        pcm.deallocate()
        floats.deallocate()
        pcm = .allocate(capacity: frames)
        floats = .allocate(capacity: frames)
    }

    /// Call with the lock held.
    private func engine(for voice: Voice) throws -> SpeechEngine {
        if let e = engines[voice.id] { return e }
        let e = try SpeechEngine(voice: voice)
        engines[voice.id] = e
        return e
    }

    func start(_ request: AVSpeechSynthesisProviderRequest) {
        lock.lock()
        defer { lock.unlock() }
        current = nil
        let identifier = request.voice.identifier
        let id = Voice.id(fromSystemIdentifier: identifier)
        guard let voice = Catalog.shared.voice(id) else {
            log.error("unknown voice \(identifier, privacy: .public)")
            return
        }
        do {
            let engine = try engine(for: voice)
            let plan = SpeechPlan(request.ssmlRepresentation)
            #if DEBUG
            log.debug("request \(request.ssmlRepresentation, privacy: .public)")
            #endif
            engine.apply(VoiceSettings.load(id), volume: plan.volume)
            guard engine.begin(plan.segments) else {
                log.error("begin failed: \(engine.lastError, privacy: .public)")
                return
            }
            current = Utterance(
                engine: engine, request: request, plan: plan,
                resampler: engine.sampleRate == MbrolaSpeechAudioUnit.outputSampleRate
                    ? nil : Resampler(from: engine.sampleRate, to: MbrolaSpeechAudioUnit.outputSampleRate))
        } catch {
            // the voice was removed, or its file is damaged: this utterance is
            // silent and ends at once; the engine is tried again next time
            engines[id] = nil
            log.error("voice \(id, privacy: .public): \(error.localizedDescription, privacy: .public)")
        }
    }

    func cancel() {
        // mbng_cancel is the one call that may come while the render thread
        // is inside the engine: it only raises a flag the engine looks at
        // (taken under the lock all the same, so it can never hit an utterance
        // that started in between)
        lock.lock()
        defer { lock.unlock() }
        current?.engine.cancel()
        current = nil
    }

    /// Renders `frames` frames into `destination` (or into an own buffer when
    /// the host gave none). Returns the buffer written to (nil: too many
    /// frames), whether the utterance ended inside this block, and the
    /// markers of the words that start in it.
    func render(frames: Int, into destination: UnsafeMutableRawPointer?)
        -> (UnsafeMutablePointer<Float32>?, Bool, [AVSpeechSynthesisMarker], AVSpeechSynthesisProviderRequest?)
    {
        lock.lock()
        defer { lock.unlock() }
        reserve(frames)
        guard let out = destination?.assumingMemoryBound(to: Float32.self) ?? floats.baseAddress,
              let samples = pcm.baseAddress
        else { return (nil, true, [], nil) }

        guard var utterance = current else {
            // nothing to say (no request, cancelled, voice missing): silence, done
            out.update(repeating: 0, count: frames)
            return (out, true, [], nil)
        }

        events.removeAll(keepingCapacity: true)
        var produced: Int
        if utterance.resampler != nil {
            let engine = utterance.engine
            produced = utterance.resampler!.render(out, frames: frames) { buffer, count, events in
                engine.read(buffer, count: count, events: &events)
            } events: { self.events.append(contentsOf: $0) }
        } else {
            produced = utterance.engine.read(samples, count: frames, events: &events)
            if produced > 0 {
                // 16-bit integer -> float in -1 ... 1
                var scale = Float32(1.0 / 32768.0)
                vDSP_vflt16(samples, 1, out, 1, vDSP_Length(produced))
                vDSP_vsmul(out, 1, &scale, out, 1, vDSP_Length(produced))
            }
        }
        if produced < 0 {
            log.error("synthesis failed: \(utterance.engine.lastError, privacy: .public)")
            produced = 0
        }
        let finished = produced < frames
        if finished { (out + produced).update(repeating: 0, count: frames - produced) }

        // word positions for the host (highlighting, "speak range" callbacks)
        var markers: [AVSpeechSynthesisMarker] = []
        for event in events {
            switch event.kind {
            case .word:
                guard let range = utterance.plan.sourceRange(offset: event.textOffset, length: event.textLength),
                      range != utterance.lastRange  // a number is several words from one place
                else { continue }
                utterance.lastRange = range
                markers.append(AVSpeechSynthesisMarker(
                    markerType: .word, forTextRange: range,
                    atByteSampleOffset: max(event.sample, 0) * MemoryLayout<Float32>.size))
            case .mark:
                if #available(iOS 17.0, macOS 14.0, *), utterance.plan.marks.indices.contains(event.markIndex) {
                    markers.append(AVSpeechSynthesisMarker(
                        bookmarkName: utterance.plan.marks[event.markIndex],
                        atByteSampleOffset: max(event.sample, 0) * MemoryLayout<Float32>.size))
                }
            case .sentence, .end:
                break
            }
        }
        let request = utterance.request
        current = finished ? nil : utterance
        return (out, finished, markers, request)
    }
}
