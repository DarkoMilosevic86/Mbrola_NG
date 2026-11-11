// MBROLA NG - voice catalog (Resources/catalog/catalog.json: downloaded by
// Catalog.refresh, with the copy in the bundle as the fallback)
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import Foundation

/// The Resources repository (github.com/Mbrola-NG/Resources) holds the voice
/// catalog and the short samples that "Play a Sample" plays for a voice which
/// is not installed. Neither is part of the app beyond one fallback:
///
/// - the catalog is fetched when the app starts and written into the App
///   Group container, where the speech extension reads it too; the copy in
///   the extension bundle is used until that succeeds, and whenever the
///   downloaded one cannot be used;
/// - a sample is fetched when the button is pressed and kept in the caches
///   folder. Its address, size and SHA-256 come from the catalog entry of the
///   voice, so it is verified exactly like a voice database.
///
/// `branch` is a moving branch: a bad commit there would reach every
/// installed app at once. A downloaded catalog is therefore taken into use
/// only when it parses, carries `catalogSchema` and still has a language this
/// build can speak; otherwise the last good copy stays.
enum Remote {
    static let branch = "main"
    static let base = "https://raw.githubusercontent.com/Mbrola-NG/Resources/" + branch + "/"
    static let catalogURL = URL(string: base + "catalog/catalog.json")!
    /// The schema of catalog.json this version understands.
    static let catalogSchema = 1
}

struct VoiceFile: Hashable, Sendable {
    let name: String
    let urls: [URL]       // https only, mirrors in order
    let size: Int64
    let sha256: String
}

struct Voice: Identifiable, Hashable, Sendable {
    let id: String                      // "cr1": also the name of the database file
    let language: String                // code of the language data: "hr"
    let names: [String: String]         // UI language -> display name
    let gender: String
    let age: String
    let sampleRate: Int
    let version: String
    let basePitch: Int                  // Hz, 0 = the language's default
    let phonemeMap: String              // "A=A: E=e ..." (the form the core takes)
    let files: [VoiceFile]
    /// The recorded sample, downloaded on demand; not part of the voice.
    let sample: VoiceFile?
    let licenseFile: String?
    let licenseSummary: [String: String]
    let locale: String                  // BCP 47: "hr-HR"
    /// Other languages the voice can also read ("sr-RS": the Croatian data reads Serbian too).
    let alsoFor: [String]

    var downloadSize: Int64 { files.reduce(0) { $0 + $1.size } }
    var name: String { names.localized }
    var licenseSummaryText: String { licenseSummary.localized }

    /// The identifier given to the system. The system puts the extension's
    /// bundle identifier in front of it: AVSpeechSynthesisVoice.identifier is
    /// "io.github.darkomilosevic86.mbrolang.speech.cr1".
    var systemIdentifier: String { id }

    /// Voice id from the identifier of a speech request (with or without the prefix).
    static func id(fromSystemIdentifier identifier: String) -> String {
        identifier.split(separator: ".").last.map(String.init) ?? identifier
    }
}

struct VoiceLanguage: Identifiable, Hashable, Sendable {
    let code: String
    let names: [String: String]
    let locale: String
    let voices: [Voice]

    var id: String { code }
    var name: String { names.localized }
}

extension Dictionary where Key == String, Value == String {
    /// The text for the user's language from {"en": ..., "hr": ...}.
    var localized: String {
        for identifier in Locale.preferredLanguages {
            let code = Locale(identifier: identifier).language.languageCode?.identifier ?? identifier
            if let text = self[code] { return text }
            if code == "en" { break }  // English is asked for: do not skip to a later language
        }
        return self["en"] ?? values.sorted().first ?? ""
    }
}

struct Catalog: Sendable {
    let languages: [VoiceLanguage]
    /// "schema" of the file; 0 when the file does not say.
    let schema: Int

    var voices: [Voice] { languages.flatMap(\.voices) }
    func voice(_ id: String) -> Voice? { voices.first { $0.id == id } }
    func language(_ code: String) -> VoiceLanguage? { languages.first { $0.code == code } }

    /// The catalog in use, limited to languages whose .dat is in the bundle:
    /// the downloaded one when it is usable, else the bundled copy. Never
    /// touches the network - the speech extension reads this before the first
    /// unlock, when nothing may be downloaded.
    static let shared: Catalog = load()

    /// Where [refresh] keeps the downloaded catalog: the App Group container,
    /// so the app and the speech extension read the same file.
    static var downloadedURL: URL? { SharedContainer.url?.appendingPathComponent("catalog.json") }

    static func load() -> Catalog {
        let hasLanguage: (String) -> Bool = { BundledResources.languageFile($0) != nil }
        if let url = downloadedURL, let data = try? Data(contentsOf: url),
           let catalog = try? Catalog(json: data, hasLanguage: hasLanguage), catalog.isUsable {
            return catalog
        }
        guard let url = BundledResources.catalogURL, let data = try? Data(contentsOf: url) else {
            return Catalog(languages: [])
        }
        return (try? Catalog(json: data, hasLanguage: hasLanguage)) ?? Catalog(languages: [])
    }

    /// A catalog this version will take into use.
    var isUsable: Bool { schema == Remote.catalogSchema && !languages.isEmpty }

    /// Downloads the catalog and writes it where the app and the extension
    /// read it. Returns the new catalog, or nil when nothing changes: no
    /// network, or a catalog this version will not take.
    static func refresh() async -> Catalog? {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 20
        configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        let session = URLSession(configuration: configuration)
        defer { session.finishTasksAndInvalidate() }
        do {
            let (data, response) = try await session.data(from: Remote.catalogURL)
            guard (response as? HTTPURLResponse)?.statusCode == 200 else { return nil }
            let fresh = try Catalog(json: data) { BundledResources.languageFile($0) != nil }
            guard fresh.isUsable, let url = downloadedURL else { return nil }
            try data.write(to: url, options: .atomic)
            // VoiceOver speaks on the lock screen: the extension has to read
            // this before the first unlock, like the voices themselves
            SharedContainer.makeAvailableBeforeFirstUnlock(url)
            return fresh
        } catch {
            return nil  // the catalog in use stays
        }
    }

    init(languages: [VoiceLanguage], schema: Int = Remote.catalogSchema) {
        self.languages = languages
        self.schema = schema
    }

    init(json data: Data, hasLanguage: (String) -> Bool = { _ in true }) throws {
        let raw = try JSONDecoder().decode(RawCatalog.self, from: data)
        schema = raw.schema ?? 0
        languages = raw.languages.compactMap { l in
            guard hasLanguage(l.code) else { return nil }
            let locale = l.apple_locale ?? l.code
            let voices = raw.voices.filter { $0.language == l.code }.map { v in
                Voice(
                    id: v.id,
                    language: l.code,
                    names: v.names ?? [:],
                    gender: v.gender ?? "",
                    age: v.age ?? "",
                    sampleRate: v.sample_rate ?? 16000,
                    version: v.version ?? "",
                    basePitch: Int(v.voice_config?.base_pitch ?? 0),
                    phonemeMap: (v.voice_config?.phoneme_map ?? [:])
                        .filter { !$0.key.isEmpty && !$0.value.isEmpty && !($0.key + $0.value).contains(" ") }
                        .sorted { $0.key < $1.key }
                        .map { "\($0.key)=\($0.value)" }
                        .joined(separator: " "),
                    files: (v.files ?? []).map(Catalog.file),
                    sample: v.sample.map(Catalog.file),
                    licenseFile: v.license?.file.flatMap { $0.isEmpty ? nil : $0 },
                    licenseSummary: v.license?.summary ?? [:],
                    locale: v.apple_locale ?? locale,
                    alsoFor: l.apple_also_for ?? [])
            }
            return VoiceLanguage(code: l.code, names: l.names ?? [:], locale: locale, voices: voices)
        }
    }

    /// https addresses only, mirrors kept in the order the catalog gives them.
    private static func file(_ f: RawFile) -> VoiceFile {
        VoiceFile(
            name: f.name,
            urls: (f.urls ?? []).compactMap { URL(string: $0) }
                .filter { $0.scheme?.lowercased() == "https" },
            size: f.size ?? 0,
            sha256: f.sha256 ?? "")
    }

    // The file as it is written (other platforms' keys are ignored).
    private struct RawCatalog: Decodable {
        let schema: Int?
        let languages: [RawLanguage]
        let voices: [RawVoice]
    }
    private struct RawLanguage: Decodable {
        let code: String
        let names: [String: String]?
        let apple_locale: String?
        let apple_also_for: [String]?
    }
    private struct RawVoice: Decodable {
        let id: String
        let language: String
        let apple_locale: String?
        let names: [String: String]?
        let gender: String?
        let age: String?
        let sample_rate: Int?
        let version: String?
        let files: [RawFile]?
        let sample: RawFile?
        let license: RawLicense?
        let voice_config: RawConfig?
    }
    private struct RawFile: Decodable {
        let name: String
        let urls: [String]?
        let size: Int64?
        let sha256: String?
    }
    private struct RawLicense: Decodable {
        let file: String?
        let summary: [String: String]?
    }
    private struct RawConfig: Decodable {
        let base_pitch: Double?
        let phoneme_map: [String: String]?
    }
}
