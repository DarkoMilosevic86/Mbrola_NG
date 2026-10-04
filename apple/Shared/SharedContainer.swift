// MBROLA NG - the folder shared by the app and its speech extension
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation
#if os(macOS)
import Security
#endif

/// The app installs voices and writes voice settings; the speech extension -
/// another process, started by the system - reads them. Both meet in the
/// App Group container.
///
/// iOS:   `group.<bundle id>`                (set in the entitlements)
/// macOS: `<team id>.<bundle id>`            (the form macOS grants without
///        a provisioning profile; read back from the entitlements because
///        the team id is only known when the app is signed)
enum SharedContainer {
    static let bundleIdentifier = "io.github.darkomilosevic86.mbrolang"

    static let groupIdentifier: String? = {
        #if os(macOS)
        guard let task = SecTaskCreateFromSelf(nil),
              let value = SecTaskCopyValueForEntitlement(
                task, "com.apple.security.application-groups" as CFString, nil),
              let groups = value as? [String]
        else { return nil }
        return groups.first { $0.hasSuffix(bundleIdentifier) } ?? groups.first
        #else
        return "group." + bundleIdentifier
        #endif
    }()

    /// nil when the build is not signed with the App Group (the app then
    /// explains it instead of installing voices the extension cannot see).
    static let url: URL? = {
        guard let group = groupIdentifier,
              let url = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: group)
        else { return nil }
        // macOS returns a path even without the entitlement: only a folder
        // that can really be made there proves the container is ours
        do {
            try FileManager.default.createDirectory(
                at: url.appendingPathComponent("voices", isDirectory: true), withIntermediateDirectories: true)
        } catch {
            return nil
        }
        return url
    }()

    static var voicesDirectory: URL? { url?.appendingPathComponent("voices", isDirectory: true) }
    static var settingsFile: URL? { url?.appendingPathComponent("voice-settings.json") }

    /// VoiceOver speaks on the lock screen before the first unlock after a
    /// restart: everything the extension reads must be readable then.
    static func makeAvailableBeforeFirstUnlock(_ url: URL) {
        #if os(iOS)
        try? FileManager.default.setAttributes(
            [.protectionKey: FileProtectionType.none], ofItemAtPath: url.path)
        #endif
    }
}

/// Files that ship inside the speech extension (language data, the voice
/// catalog). The app reads them from the extension embedded in it, so they
/// exist once.
enum BundledResources {
    static let extensionName = "MbrolaNGSpeech.appex"

    static let bundle: Bundle = {
        if Bundle.main.bundleURL.pathExtension == "appex" { return Bundle.main }
        if let plugIns = Bundle.main.builtInPlugInsURL,
           let b = Bundle(url: plugIns.appendingPathComponent(extensionName)) {
            return b
        }
        return Bundle.main
    }()

    static var catalogURL: URL? { bundle.url(forResource: "catalog", withExtension: "json") }

    static var languagesDirectory: URL? {
        bundle.resourceURL?.appendingPathComponent("languages", isDirectory: true)
    }

    static func languageFile(_ code: String) -> URL? {
        guard let url = languagesDirectory?.appendingPathComponent(code + ".dat"),
              FileManager.default.fileExists(atPath: url.path)
        else { return nil }
        return url
    }
}
