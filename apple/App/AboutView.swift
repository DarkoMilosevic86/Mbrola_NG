// MBROLA NG - about the app, licenses
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct AboutView: View {
    private static let sourceURL = URL(string: "https://github.com/DarkoMilosevic86/Mbrola_NG")!
    private static let privacyURL = URL(string: "https://github.com/DarkoMilosevic86/Mbrola_NG/blob/master/PRIVACY.md")!

    private var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
    }

    var body: some View {
        Form {
            Section {
                LabeledContent("Version", value: version)
            } footer: {
                Text("MBROLA NG is text-to-speech built on the MBROLA diphone synthesizer, by Darko Milošević. It is free software under the GNU General Public License, version 2 or later. The MBROLA synthesizer, which is part of this app, is under the GNU Affero General Public License, version 3. Each voice has its own license.")
            }
            Section {
                Link(destination: Self.sourceURL) {
                    Label("Source Code and Licenses", systemImage: "chevron.left.forwardslash.chevron.right")
                }
                Link(destination: Self.privacyURL) {
                    Label("Privacy Policy", systemImage: "hand.raised")
                }
            } footer: {
                Text("The app collects no data. It uses the internet only to download the voices you install. The English pronunciations come from the CMU Pronouncing Dictionary, the emoji names from Unicode CLDR.")
            }
        }
        .formStyle(.grouped)
        .navigationTitle("About MBROLA NG")
        #if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        #endif
    }
}
