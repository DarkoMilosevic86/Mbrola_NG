// MBROLA NG - about the app, licenses
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct AboutView: View {
    private static let sourceURL = URL(string: "https://github.com/DarkoMilosevic86/Mbrola_NG")!

    private var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
    }

    var body: some View {
        Form {
            // the same statement and link as in the Android app
            Section {
                Text("MBROLA NG \(version). Free software under the GNU GPL, version 2 or later. The MBROLA synthesizer is under the GNU AGPL v3. Each voice has its own license.")
            }
            Section {
                Link(destination: Self.sourceURL) {
                    Label("Source code and licenses", systemImage: "chevron.left.forwardslash.chevron.right")
                }
            }
        }
        .formStyle(.grouped)
        .navigationTitle("About MBROLA NG")
        #if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        #endif
    }
}
