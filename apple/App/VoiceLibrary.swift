// MBROLA NG - the voices of this device, as the app's screens see them
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import AVFoundation
import SwiftUI

@MainActor
final class VoiceLibrary: ObservableObject {
    enum InstallState: Equatable {
        case fetchingLicense
        case license(String)                        // shown, waiting for the user's answer
        case downloading(done: Int64, total: Int64)
    }

    struct Failure: Identifiable {
        let id = UUID()
        let message: String
    }

    let catalog = Catalog.shared
    /// false: the build is not signed with its App Group, so the speech
    /// extension could never see a voice installed here.
    let storageAvailable = SharedContainer.voicesDirectory != nil

    @Published private(set) var installed: [Voice] = []
    @Published private(set) var installs: [String: InstallState] = [:]
    @Published var failure: Failure?

    private var tasks: [String: Task<Void, Never>] = [:]
    private var stagings: [String: URL] = [:]

    init() {
        VoiceInstaller.removeLeftovers()
        refresh()
        // the system may hold a voice list from before an update or a restore
        SystemVoices.update()
        #if DEBUG
        DebugSelfTest.runIfRequested { [weak self] in self?.refresh() }
        #endif
    }

    func refresh() {
        installed = VoiceStore.installed(in: catalog)
    }

    func isInstalled(_ voice: Voice) -> Bool { installed.contains(voice) }

    /// Voices of a language that can still be installed.
    func available(in language: VoiceLanguage) -> [Voice] {
        language.voices.filter { !isInstalled($0) }
    }

    // ------------------------------------------------------------ install
    /// Step 1: fetch the license; the screen then shows it (ANALYSIS 10.7).
    func install(_ voice: Voice) {
        guard installs[voice.id] == nil else { return }
        let staging: URL
        do {
            staging = try VoiceInstaller.newStagingDirectory(voice)
        } catch {
            failure = Failure(message: error.localizedDescription)
            return
        }
        stagings[voice.id] = staging
        installs[voice.id] = .fetchingLicense
        tasks[voice.id] = Task {
            do {
                let text = try await VoiceInstaller.fetchLicense(voice, into: staging)
                if Task.isCancelled { return }
                installs[voice.id] = .license(text.isEmpty ? voice.licenseSummaryText : text)
            } catch {
                if Task.isCancelled { return }  // cancelInstall has cleaned up
                finish(voice, error: error)
            }
        }
    }

    /// Step 2: the user accepted the license.
    func acceptLicense(_ voice: Voice) {
        guard case .license = installs[voice.id], let staging = stagings[voice.id] else { return }
        installs[voice.id] = .downloading(done: 0, total: voice.downloadSize)
        tasks[voice.id] = Task {
            do {
                try await VoiceInstaller.install(voice, from: staging) { done, total in
                    Task { @MainActor in
                        guard case .downloading = self.installs[voice.id], self.stagings[voice.id] == staging
                        else { return }
                        self.installs[voice.id] = .downloading(done: done, total: total)
                    }
                }
                // (a cancel that came too late: the voice is installed all the same)
                if stagings[voice.id] == staging { finish(voice, error: nil) } else { refresh() }
                Announcer.announce(String(localized: "The voice \(voice.name) is installed."))
            } catch {
                if Task.isCancelled { return }
                finish(voice, error: error)
            }
        }
    }

    /// Declines the license or stops the download.
    func cancelInstall(_ voice: Voice) {
        tasks[voice.id]?.cancel()
        tasks[voice.id] = nil
        installs[voice.id] = nil
        if let staging = stagings.removeValue(forKey: voice.id) { VoiceInstaller.discard(staging) }
    }

    private func finish(_ voice: Voice, error: Error?) {
        tasks[voice.id] = nil
        installs[voice.id] = nil
        if let staging = stagings.removeValue(forKey: voice.id), error != nil { VoiceInstaller.discard(staging) }
        if let error, !(error is CancellationError) {
            failure = Failure(message: error.localizedDescription)
        }
        refresh()
    }

    // ------------------------------------------------------------- remove
    func remove(_ voice: Voice) {
        do {
            try VoiceStore.remove(voice.id)
        } catch {
            failure = Failure(message: error.localizedDescription)
        }
        refresh()
        SystemVoices.update()
    }
}

/// The system's list of voices (Settings, VoiceOver, every app).
enum SystemVoices {
    /// Tells the system that the installed voices changed; it then asks the
    /// speech extension for the new list. Not on the main thread: the call
    /// waits for a system service and can take seconds.
    static func update() {
        Task.detached(priority: .utility) {
            AVSpeechSynthesisProviderVoice.updateSpeechVoices()
        }
    }
}

/// Tells VoiceOver something that happened without a change it would read by itself.
enum Announcer {
    @MainActor
    static func announce(_ message: String) {
        #if os(macOS)
        NSAccessibility.post(
            element: NSApp as Any, notification: .announcementRequested,
            userInfo: [.announcement: message, .priority: NSAccessibilityPriorityLevel.high.rawValue])
        #else
        UIAccessibility.post(notification: .announcement, argument: message)
        #endif
    }
}
