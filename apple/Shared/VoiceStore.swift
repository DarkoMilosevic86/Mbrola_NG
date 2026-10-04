// MBROLA NG - installed voices and their settings (shared container)
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

/// Voices live in <container>/voices/<id>/<id> (the MBROLA database, a normal
/// file MBROLA opens by path) next to the voice's license (ANALYSIS 10.9).
enum VoiceStore {
    static func directory(_ id: String) -> URL? {
        SharedContainer.voicesDirectory?.appendingPathComponent(id, isDirectory: true)
    }

    static func database(_ id: String) -> URL? {
        directory(id)?.appendingPathComponent(id)
    }

    static func isInstalled(_ id: String) -> Bool {
        guard let db = database(id) else { return false }
        var isDirectory: ObjCBool = false
        return FileManager.default.fileExists(atPath: db.path, isDirectory: &isDirectory)
            && !isDirectory.boolValue
    }

    /// Installed voices, in the order of the catalog.
    static func installed(in catalog: Catalog = .shared) -> [Voice] {
        catalog.voices.filter { isInstalled($0.id) }
    }

    /// Bytes the voice takes on the device.
    static func size(_ id: String) -> Int64 {
        guard let dir = directory(id),
              let files = try? FileManager.default.contentsOfDirectory(
                at: dir, includingPropertiesForKeys: [.fileSizeKey])
        else { return 0 }
        return files.reduce(0) { $0 + Int64((try? $1.resourceValues(forKeys: [.fileSizeKey]))?.fileSize ?? 0) }
    }

    static func licenseText(_ voice: Voice) -> String? {
        guard let name = voice.licenseFile, let dir = directory(voice.id) else { return nil }
        return readText(dir.appendingPathComponent(name))
    }

    /// The voice licenses are old text files: UTF-8 where valid, else Latin-1.
    static func readText(_ url: URL) -> String? {
        guard let data = try? Data(contentsOf: url) else { return nil }
        return String(data: data, encoding: .utf8) ?? String(data: data, encoding: .isoLatin1)
    }

    static func remove(_ id: String) throws {
        guard let dir = directory(id) else { return }
        if FileManager.default.fileExists(atPath: dir.path) {
            try FileManager.default.removeItem(at: dir)
        }
        VoiceSettings.clear(id)
    }
}

/// How numbers are read (MBNG_PARAM_DIGITS).
enum NumberMode: Int, Codable, CaseIterable, Sendable {
    case whole = 0, digits = 1, pairs = 2
}

/// Settings of one voice. Speed, pitch and volume asked by the system
/// (VoiceOver's own controls) are applied on top of these.
struct VoiceSettings: Codable, Equatable, Sendable {
    var rate = 100          // percent, 100 = normal
    var pitch = 100         // percent
    var modulation = 100    // intonation range, percent (0 = monotone)
    var volume = 100        // percent
    var emoji = true
    var numbers = NumberMode.whole

    static let rateRange = 50...400
    static let pitchRange = 50...200
    static let modulationRange = 0...200
    static let volumeRange = 10...200

    init() {}

    // Tolerant decoding: a settings file written by another version still loads.
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let d = VoiceSettings()
        rate = Self.clamp(try c.decodeIfPresent(Int.self, forKey: .rate) ?? d.rate, Self.rateRange)
        pitch = Self.clamp(try c.decodeIfPresent(Int.self, forKey: .pitch) ?? d.pitch, Self.pitchRange)
        modulation = Self.clamp(
            try c.decodeIfPresent(Int.self, forKey: .modulation) ?? d.modulation, Self.modulationRange)
        volume = Self.clamp(try c.decodeIfPresent(Int.self, forKey: .volume) ?? d.volume, Self.volumeRange)
        emoji = try c.decodeIfPresent(Bool.self, forKey: .emoji) ?? d.emoji
        numbers = (try? c.decodeIfPresent(NumberMode.self, forKey: .numbers)) ?? d.numbers
    }

    private static func clamp(_ v: Int, _ r: ClosedRange<Int>) -> Int { min(max(v, r.lowerBound), r.upperBound) }

    // ------------------------------------------------------------- storage
    // One small JSON file in the shared container, not UserDefaults: it is
    // written without file protection, so the extension can read it before
    // the first unlock, and its modification date tells the extension when
    // to read it again.
    private static let lock = NSLock()
    nonisolated(unsafe) private static var cache: [String: VoiceSettings] = [:]
    nonisolated(unsafe) private static var cacheDate: Date?

    private static func modificationDate(_ url: URL) -> Date? {
        (try? FileManager.default.attributesOfItem(atPath: url.path))?[.modificationDate] as? Date
    }

    /// Call with `lock` held.
    private static func refresh() {
        guard let url = SharedContainer.settingsFile else { return }
        guard let date = modificationDate(url) else {
            cache = [:]
            cacheDate = nil
            return
        }
        if date == cacheDate { return }
        if let data = try? Data(contentsOf: url),
           let all = try? JSONDecoder().decode([String: VoiceSettings].self, from: data) {
            cache = all
            cacheDate = date
        }
        // an unreadable file (being replaced right now) keeps the previous values
    }

    private static func write() {
        guard let url = SharedContainer.settingsFile else { return }
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        guard let data = try? encoder.encode(cache) else { return }
        try? data.write(to: url, options: [.atomic, .noFileProtection])
        cacheDate = modificationDate(url)
    }

    static func load(_ voiceID: String) -> VoiceSettings {
        lock.lock()
        defer { lock.unlock() }
        refresh()
        return cache[voiceID] ?? VoiceSettings()
    }

    static func save(_ settings: VoiceSettings, for voiceID: String) {
        lock.lock()
        defer { lock.unlock() }
        refresh()
        if settings == VoiceSettings() {
            cache[voiceID] = nil
        } else {
            cache[voiceID] = settings
        }
        write()
    }

    static func clear(_ voiceID: String) {
        save(VoiceSettings(), for: voiceID)
    }
}
