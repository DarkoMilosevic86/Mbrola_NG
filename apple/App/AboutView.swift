// MBROLA NG - about the app, licenses
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct AboutView: View {
    private static let sourceURL = URL(string: "https://github.com/DarkoMilosevic86/Mbrola_NG")!
    private static let privacyURL = URL(string: "https://github.com/DarkoMilosevic86/Mbrola_NG/blob/master/PRIVACY.md")!
    private static let mbrolaURL = URL(string: "https://github.com/numediart/MBROLA")!

    private var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
    }

    var body: some View {
        Form {
            Section {
                LabeledContent("Version", value: version)
            } footer: {
                Text("MBROLA NG is free text-to-speech software built on the MBROLA speech synthesizer. It consists of parts by different authors, listed below. The authors of MBROLA NG are not the authors or owners of MBROLA and are not affiliated with them.")
            }
            Section {
                Credit(title: "MBROLA NG", detail: "Text processing, languages and the versions for NVDA, Windows, Linux and Android: Darko Milošević. Copyright © 2026 Darko Milošević. GNU General Public License, version 2 or later.")
                Credit(title: "MBROLA NG for iOS, iPadOS and macOS", detail: "This app and its speech extension: Hrvoje Katić. GNU General Public License, version 2 or later.")
                Credit(title: "MBROLA speech synthesizer", detail: "Authors: Thierry Dutoit and Vincent Pagel. Copyright © 1995–2018 Faculté Polytechnique de Mons (TCTS Lab), Belgium. GNU Affero General Public License, version 3 or later.")
                Credit(title: "Voices", detail: "The voices are not part of MBROLA NG or of the MBROLA synthesizer. Each voice belongs to those who created it and has its own license, which is shown before you download the voice and on the voice's page.")
                Credit(title: "CMU Pronouncing Dictionary", detail: "English pronunciations. Copyright © 1993–2015 Carnegie Mellon University. BSD-style license.")
                Credit(title: "Unicode CLDR", detail: "Emoji names. Copyright © Unicode, Inc. Unicode License v3.")
            } header: {
                Text("Authors and licenses")
            } footer: {
                Text("Because the MBROLA synthesizer is built into this app, the app as a whole is distributed under the GNU Affero General Public License, version 3. It comes with no warranty. You may redistribute it under the terms of that license.")
            }
            Section {
                LicenseLink(title: "GNU Affero General Public License, version 3", file: "AGPL-3.0")
                LicenseLink(title: "GNU General Public License, version 2", file: "GPL-2.0")
                LicenseLink(title: "CMU Pronouncing Dictionary License", file: "CMUdict")
                LicenseLink(title: "Unicode License v3", file: "Unicode-3.0")
            } header: {
                Text("Licenses")
            } footer: {
                Text("The license of each voice is on the voice's page.")
            }
            Section {
                Link(destination: Self.sourceURL) {
                    Label("Source Code and Licenses", systemImage: "chevron.left.forwardslash.chevron.right")
                }
                Link(destination: Self.mbrolaURL) {
                    Label("MBROLA Source Code", systemImage: "waveform")
                }
                Link(destination: Self.privacyURL) {
                    Label("Privacy Policy", systemImage: "hand.raised")
                }
            } footer: {
                Text("The app collects no data. It uses the internet only to download the voices you install.")
            }
        }
        .formStyle(.grouped)
        .navigationTitle("About MBROLA NG")
        #if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        #endif
    }
}

/// One part of the software: what it is, then who made it and under which license.
private struct Credit: View {
    let title: LocalizedStringKey
    let detail: LocalizedStringKey

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title)
            Text(detail)
                .font(.callout)
                .foregroundStyle(.secondary)
        }
        .accessibilityElement(children: .combine)
    }
}

/// The full text of a license that comes with the app (App/Licenses).
private struct LicenseLink: View {
    let title: LocalizedStringKey
    let file: String

    /// The paragraphs, unwrapped: the files are wrapped for 80 columns.
    private var paragraphs: [String] {
        guard let url = Bundle.main.url(forResource: file, withExtension: "txt"),
              let text = try? String(contentsOf: url, encoding: .utf8) else { return [] }
        return text.components(separatedBy: "\n\n")
            .map { $0.split(whereSeparator: \.isNewline)
                .map { $0.trimmingCharacters(in: .whitespaces) }
                .joined(separator: " ") }
            .filter { !$0.isEmpty }
    }

    var body: some View {
        NavigationLink(title) {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 12) {
                    ForEach(Array(paragraphs.enumerated()), id: \.offset) { _, paragraph in
                        Text(verbatim: paragraph)
                            .font(.callout)
                            .textSelection(.enabled)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                }
                .padding()
            }
            .navigationTitle(title)
        }
    }
}
