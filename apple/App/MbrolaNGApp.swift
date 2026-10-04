// MBROLA NG - the app: voice manager for the speech synthesis extension
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

@main
struct MbrolaNGApp: App {
    @StateObject private var library = VoiceLibrary()
    @StateObject private var player = PreviewPlayer()

    var body: some Scene {
        WindowGroup {
            VoiceListView()
                .environmentObject(library)
                .environmentObject(player)
                #if os(macOS)
                .frame(minWidth: 460, idealWidth: 540, minHeight: 480, idealHeight: 680)
                #endif
        }
        #if os(macOS)
        .commands {
            CommandGroup(replacing: .newItem) {}  // one window is all there is
        }
        #endif
    }
}
