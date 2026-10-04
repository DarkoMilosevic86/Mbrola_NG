// MBROLA NG - entry point of the speech synthesis extension
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import AVFoundation
import CoreAudioKit

/// The system loads the extension (Info.plist: NSExtensionPrincipalClass) and
/// asks this factory for the audio unit that synthesizes speech.
public final class AudioUnitFactory: NSObject, AUAudioUnitFactory {
    public func beginRequest(with context: NSExtensionContext) {}

    @objc
    public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        try MbrolaSpeechAudioUnit(componentDescription: componentDescription, options: [])
    }
}
