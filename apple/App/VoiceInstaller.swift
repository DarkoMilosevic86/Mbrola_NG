// MBROLA NG - downloads, verifies and installs a voice (ANALYSIS 10.7)
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import AVFoundation
import CryptoKit
import Foundation

enum VoiceInstallError: LocalizedError {
    case storage
    case download(String)
    case damaged(String)
    case notMbrola

    var errorDescription: String? {
        switch self {
        case .storage:
            return String(localized: "Voices cannot be stored: the app is not signed with its App Group.")
        case .download:
            return String(localized: "The download failed. Check the internet connection and try again.")
        case .damaged:
            return String(localized: "The downloaded file is damaged. Try again.")
        case .notMbrola:
            return String(localized: "The downloaded file is not an MBROLA voice.")
        }
    }
}

enum VoiceInstaller {
    private static let receiver = ChunkReceiver()
    private static let session: URLSession = {
        let c = URLSessionConfiguration.ephemeral  // no cookies, no cache: nothing is kept
        c.timeoutIntervalForRequest = 30
        c.requestCachePolicy = .reloadIgnoringLocalCacheData
        return URLSession(configuration: c, delegate: receiver, delegateQueue: nil)
    }()

    /// A folder for one installation attempt, next to the installed voices
    /// (the same volume, so the finished voice is moved into place at once).
    static func newStagingDirectory(_ voice: Voice) throws -> URL {
        guard let voices = SharedContainer.voicesDirectory else { throw VoiceInstallError.storage }
        return voices.appendingPathComponent(".download-\(voice.id)-\(UUID().uuidString)", isDirectory: true)
    }

    /// Removes what interrupted installations left behind (app start).
    static func removeLeftovers() {
        guard let voices = SharedContainer.voicesDirectory,
              let names = try? FileManager.default.contentsOfDirectory(atPath: voices.path)
        else { return }
        for name in names where name.hasPrefix(".download-") {
            try? FileManager.default.removeItem(at: voices.appendingPathComponent(name))
        }
    }

    /// Downloads `file` from the first mirror that works and checks its size and
    /// SHA-256. `onProgress` gets the bytes of this file received so far.
    private static func download(
        _ file: VoiceFile, to destination: URL, onProgress: (Int64) -> Void
    ) async throws {
        var last: Error = VoiceInstallError.download("no https address")
        for url in file.urls {
            do {
                try Task.checkCancellation()
                FileManager.default.createFile(atPath: destination.path, contents: nil)
                let handle = try FileHandle(forWritingTo: destination)
                defer { try? handle.close() }
                var hash = SHA256()
                var size: Int64 = 0
                for try await chunk in receiver.chunks(of: url, in: session) {
                    try handle.write(contentsOf: chunk)
                    hash.update(data: chunk)
                    size += Int64(chunk.count)
                    onProgress(size)
                }
                try Task.checkCancellation()  // a cancelled task ends the stream without an error
                if file.size > 0 && size != file.size { throw VoiceInstallError.damaged(file.name) }
                let digest = hash.finalize().map { String(format: "%02x", $0) }.joined()
                if file.sha256.count == 64 && digest != file.sha256.lowercased() {
                    throw VoiceInstallError.damaged(file.name)
                }
                return
            } catch let error as VoiceInstallError {
                if case .damaged = error { throw error }
                last = error
            } catch is CancellationError {
                throw CancellationError()
            } catch let error as URLError where error.code == .cancelled {
                throw CancellationError()
            } catch {
                last = VoiceInstallError.download(error.localizedDescription)
            }
            onProgress(0)  // the next mirror starts the file again
        }
        throw last
    }

    /// Step 1: the license text (shown before anything else is downloaded).
    static func fetchLicense(_ voice: Voice, into dir: URL) async throws -> String {
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        guard let file = voice.files.first(where: { $0.name == voice.licenseFile }) else { return "" }
        let destination = dir.appendingPathComponent(file.name)
        try await download(file, to: destination) { _ in }
        return VoiceStore.readText(destination) ?? ""
    }

    /// Step 2: the remaining files, verification, then the voice appears.
    static func install(
        _ voice: Voice, from dir: URL, onProgress: @escaping @Sendable (Int64, Int64) -> Void
    ) async throws {
        let fm = FileManager.default
        do {
            try fm.createDirectory(at: dir, withIntermediateDirectories: true)
            let total = voice.downloadSize
            var done = voice.files
                .filter { fm.fileExists(atPath: dir.appendingPathComponent($0.name).path) }
                .reduce(Int64(0)) { $0 + $1.size }
            onProgress(done, total)
            for file in voice.files {
                let destination = dir.appendingPathComponent(file.name)
                if fm.fileExists(atPath: destination.path) { continue }  // the license, already here
                do {
                    try await download(file, to: destination) { onProgress(done + $0, total) }
                } catch {
                    try? fm.removeItem(at: destination)
                    throw error
                }
                done += file.size
            }
            // the database must be what MBROLA opens: magic "MBROLA"
            let database = dir.appendingPathComponent(voice.id)
            let handle = try FileHandle(forReadingFrom: database)
            let magic = try handle.read(upToCount: 6)
            try handle.close()
            guard magic == Data("MBROLA".utf8) else { throw VoiceInstallError.notMbrola }
            try Task.checkCancellation()

            // readable before the first unlock, and not worth a place in backups
            // (the voice can be downloaded again)
            var folder = dir
            var values = URLResourceValues()
            values.isExcludedFromBackup = true
            try? folder.setResourceValues(values)
            SharedContainer.makeAvailableBeforeFirstUnlock(dir)
            for name in (try? fm.contentsOfDirectory(atPath: dir.path)) ?? [] {
                SharedContainer.makeAvailableBeforeFirstUnlock(dir.appendingPathComponent(name))
            }
            if let voices = SharedContainer.voicesDirectory {
                SharedContainer.makeAvailableBeforeFirstUnlock(voices)
            }

            guard let target = VoiceStore.directory(voice.id) else { throw VoiceInstallError.storage }
            if fm.fileExists(atPath: target.path) { try fm.removeItem(at: target) }
            try fm.moveItem(at: dir, to: target)
            SystemVoices.update()
        } catch {
            try? fm.removeItem(at: dir)
            throw error
        }
    }

    static func discard(_ directory: URL) {
        try? FileManager.default.removeItem(at: directory)
    }
}

/// Hands out a download in the blocks the network delivers them in (the
/// session's delegate). Any answer but 200 ends the stream with an error.
private final class ChunkReceiver: NSObject, URLSessionDataDelegate, @unchecked Sendable {
    private typealias Continuation = AsyncThrowingStream<Data, Error>.Continuation
    private let lock = NSLock()
    private var continuations: [Int: Continuation] = [:]

    func chunks(of url: URL, in session: URLSession) -> AsyncThrowingStream<Data, Error> {
        let task = session.dataTask(with: url)
        return AsyncThrowingStream { continuation in
            lock.lock()
            continuations[task.taskIdentifier] = continuation
            lock.unlock()
            continuation.onTermination = { _ in task.cancel() }  // no one reads on (nothing, if it is complete)
            task.resume()
        }
    }

    private func continuation(of task: URLSessionTask, remove: Bool = false) -> Continuation? {
        lock.lock()
        defer { lock.unlock() }
        return remove ? continuations.removeValue(forKey: task.taskIdentifier) : continuations[task.taskIdentifier]
    }

    func urlSession(
        _ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
        completionHandler: @escaping @Sendable (URLSession.ResponseDisposition) -> Void
    ) {
        guard let http = response as? HTTPURLResponse, http.statusCode == 200 else {
            continuation(of: dataTask, remove: true)?.finish(
                throwing: VoiceInstallError.download("HTTP \((response as? HTTPURLResponse)?.statusCode ?? 0)"))
            completionHandler(.cancel)
            return
        }
        completionHandler(.allow)
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        continuation(of: dataTask)?.yield(data)
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        continuation(of: task, remove: true)?.finish(throwing: error)
    }
}
