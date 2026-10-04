// MBROLA NG - main screen: installed voices and the voices to install
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

enum Route: Hashable {
    case voice(String)
    case about
}

struct VoiceListView: View {
    @EnvironmentObject private var library: VoiceLibrary
    @EnvironmentObject private var player: PreviewPlayer
    @State private var path: [Route] = {
        #if DEBUG
        DebugSelfTest.initialPath
        #else
        []
        #endif
    }()

    var body: some View {
        NavigationStack(path: $path) {
            Form {
                if !library.storageAvailable {
                    Section {
                        Label(
                            "Voices cannot be stored: the app is not signed with its App Group.",
                            systemImage: "exclamationmark.triangle")
                    }
                }

                Section("Installed voices") {
                    if library.installed.isEmpty {
                        Text("No voice is installed yet. Choose a voice below and install it.")
                            .foregroundStyle(.secondary)
                    }
                    ForEach(library.installed) { voice in
                        NavigationLink(value: Route.voice(voice.id)) {
                            VoiceRow(voice: voice, installed: true)
                        }
                    }
                }

                ForEach(library.catalog.languages) { language in
                    let voices = library.available(in: language)
                    if !voices.isEmpty {
                        Section("Voices to install: \(language.name)") {
                            ForEach(voices) { voice in
                                NavigationLink(value: Route.voice(voice.id)) {
                                    VoiceRow(voice: voice, installed: false)
                                }
                            }
                        }
                    }
                }

                Section("Using the voices") {
                    Text(SystemHelp.text)
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }

                Section {
                    NavigationLink(value: Route.about) {
                        Label("About MBROLA NG", systemImage: "info.circle")
                    }
                }
            }
            .formStyle(.grouped)  // on the Mac, the look of the other screens
            .navigationTitle("MBROLA NG")
            .navigationDestination(for: Route.self) { route in
                switch route {
                case .voice(let id):
                    if let voice = library.catalog.voice(id) {
                        VoiceDetailView(voice: voice)
                    }
                case .about:
                    AboutView()
                }
            }
        }
        .alert(
            "MBROLA NG",
            isPresented: Binding(
                get: { library.failure != nil || player.failure != nil },
                set: { if !$0 { library.failure = nil; player.failure = nil } })
        ) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(library.failure?.message ?? player.failure?.message ?? "")
        }
        .onAppear { library.refresh() }
    }
}

/// Where the system lets the user choose the voice.
enum SystemHelp {
    static var text: LocalizedStringKey {
        #if os(macOS)
        "Installed voices are offered by macOS like its own. For VoiceOver, choose the voice in VoiceOver Utility › Speech. For reading text aloud, choose it in System Settings › Accessibility › Spoken Content › System voice › Manage Voices. A newly installed voice can take up to a minute to appear."
        #else
        "Installed voices are offered by the system like its own. For VoiceOver, choose the voice in Settings › Accessibility › VoiceOver › Speech. For Speak Screen and Speak Selection, choose it in Settings › Accessibility › Spoken Content › Voices. A newly installed voice can take up to a minute to appear."
        #endif
    }
}

struct VoiceRow: View {
    @EnvironmentObject private var library: VoiceLibrary
    let voice: Voice
    let installed: Bool

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(voice.name)
                Text(detail)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            if case .downloading(let done, let total) = library.installs[voice.id] {
                ProgressView(value: Double(done), total: Double(max(total, 1)))
                    .progressViewStyle(.circular)
                    .accessibilityHidden(true)  // the row says it
            }
        }
        .accessibilityElement(children: .combine)
    }

    private var detail: String {
        switch library.installs[voice.id] {
        case .downloading(let done, let total):
            return String(localized: "Downloading: \(done.formatted(.byteCount(style: .file))) of \(total.formatted(.byteCount(style: .file)))")
        case .fetchingLicense, .license:
            return String(localized: "Waiting for the license")
        case nil:
            let size = installed ? VoiceStore.size(voice.id) : voice.downloadSize
            return size.formatted(.byteCount(style: .file))
        }
    }
}
