// MBROLA NG - one voice: install it, try it, change its settings, remove it
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI

struct VoiceDetailView: View {
    @EnvironmentObject private var library: VoiceLibrary
    @EnvironmentObject private var player: PreviewPlayer
    @Environment(\.dismiss) private var dismiss

    let voice: Voice
    @State private var settings = VoiceSettings()
    @State private var text = ""
    @State private var confirmRemove = false
    @State private var loaded = false

    private var installed: Bool { library.isInstalled(voice) }
    private var isPlaying: Bool { player.playing == voice.id }

    var body: some View {
        Form {
            if installed {
                trySection
                settingsSection
            } else {
                installSection
            }
            aboutSection
            if installed {
                Section {
                    Button("Remove Voice", role: .destructive) { confirmRemove = true }
                        #if os(macOS)
                        .foregroundStyle(.red)  // a Mac form does not color the role
                        #endif
                }
            }
        }
        .formStyle(.grouped)
        .navigationTitle(voice.name)
        #if os(iOS)
        .navigationBarTitleDisplayMode(.inline)
        .scrollDismissesKeyboard(.interactively)
        #endif
        .onAppear {
            if !loaded {
                settings = VoiceSettings.load(voice.id)
                text = SampleTexts.installedAndWorking(voice.language)
                loaded = true
            }
        }
        .onChange(of: settings) { VoiceSettings.save($0, for: voice.id) }
        .onDisappear { player.stop() }
        .confirmationDialog(
            "Remove the voice \(voice.name) from this device?",
            isPresented: $confirmRemove, titleVisibility: .visible
        ) {
            Button("Remove Voice", role: .destructive) {
                player.stop()
                library.remove(voice)
                dismiss()
            }
            Button("Cancel", role: .cancel) {}
        }
        .sheet(isPresented: licenseShown) {
            if case .license(let license) = library.installs[voice.id] {
                LicenseSheet(voice: voice, license: license)
            }
        }
    }

    private var licenseShown: Binding<Bool> {
        Binding(
            get: { if case .license = library.installs[voice.id] { return true } else { return false } },
            set: { shown in
                // swiped away = declined
                if !shown, case .license = library.installs[voice.id] { library.cancelInstall(voice) }
            })
    }

    // ------------------------------------------------------------ install
    @ViewBuilder private var installSection: some View {
        Section {
            switch library.installs[voice.id] {
            case .downloading(let done, let total):
                ProgressView(value: Double(done), total: Double(max(total, 1))) {
                    Text("Downloading: \(done.formatted(.byteCount(style: .file))) of \(total.formatted(.byteCount(style: .file)))")
                }
                Button("Cancel", role: .cancel) { library.cancelInstall(voice) }
            case .fetchingLicense, .license:
                ProgressView { Text("Waiting for the license") }
                Button("Cancel", role: .cancel) { library.cancelInstall(voice) }
            case nil:
                Button {
                    library.install(voice)
                } label: {
                    Label(
                        "Install (\(voice.downloadSize.formatted(.byteCount(style: .file))))",
                        systemImage: "arrow.down.circle")
                }
                .disabled(!library.storageAvailable)
            }
            if PreviewPlayer.sampleURL(voice) != nil {
                Button {
                    if isPlaying { player.stop() } else { player.playSample(voice) }
                } label: {
                    Label(isPlaying ? "Stop" : "Play a Sample", systemImage: isPlaying ? "stop.fill" : "play.fill")
                }
            }
        } footer: {
            Text("The voice is downloaded from the MBROLA voices repository on GitHub after you accept its license.")
        }
    }

    // ---------------------------------------------------------------- try
    @ViewBuilder private var trySection: some View {
        Section("Try the voice") {
            TextField("Text to speak", text: $text, axis: .vertical)
                .lineLimit(1...5)
                #if os(macOS)
                // a Mac form would put the title to the left and the text
                // to the right; the section header says what it is
                .labelsHidden()
                .multilineTextAlignment(.leading)
                #endif
            Button {
                if isPlaying {
                    player.stop()
                } else {
                    player.speak(text, voice: voice, settings: settings)
                }
            } label: {
                Label(isPlaying ? "Stop" : "Speak", systemImage: isPlaying ? "stop.fill" : "play.fill")
            }
            .disabled(text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
        }
    }

    // ----------------------------------------------------------- settings
    @ViewBuilder private var settingsSection: some View {
        Section {
            PercentSlider(title: "Speed", value: $settings.rate, range: VoiceSettings.rateRange)
            PercentSlider(title: "Pitch", value: $settings.pitch, range: VoiceSettings.pitchRange)
            PercentSlider(title: "Modulation", value: $settings.modulation, range: VoiceSettings.modulationRange)
            PercentSlider(title: "Volume", value: $settings.volume, range: VoiceSettings.volumeRange)
            Toggle("Read emoji", isOn: $settings.emoji)
            Picker("How numbers are read", selection: $settings.numbers) {
                Text("As whole numbers").tag(NumberMode.whole)
                Text("Digit by digit").tag(NumberMode.digits)
                Text("In pairs").tag(NumberMode.pairs)
            }
            Button("Reset to Defaults") { settings = VoiceSettings() }
                .disabled(settings == VoiceSettings())
        } header: {
            Text("Voice settings")
        } footer: {
            Text("These settings apply wherever the voice speaks. The speed, pitch and volume chosen in VoiceOver or in an app are applied on top of them.")
        }
    }

    // -------------------------------------------------------------- about
    @ViewBuilder private var aboutSection: some View {
        Section {
            if let language = library.catalog.language(voice.language) {
                LabeledContent("Language", value: language.name)
            }
            LabeledContent("Version", value: voice.version)
            if installed {
                LabeledContent("Size", value: VoiceStore.size(voice.id).formatted(.byteCount(style: .file)))
            }
            if installed, let license = VoiceStore.licenseText(voice) {
                NavigationLink("License") {
                    ScrollView {
                        Text(license)
                            .font(.callout)
                            .textSelection(.enabled)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding()
                    }
                    .navigationTitle("License")
                }
            }
        } header: {
            Text("About this voice")
        } footer: {
            Text(voice.licenseSummaryText)
        }
    }
}

/// A setting in percent: a slider VoiceOver reads as "Speed, 120 percent".
struct PercentSlider: View {
    let title: LocalizedStringKey
    @Binding var value: Int
    let range: ClosedRange<Int>

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title)
                Spacer()
                Text(Double(value) / 100, format: .percent)
                    .foregroundStyle(.secondary)
                    .monospacedDigit()
            }
            .accessibilityHidden(true)  // the slider carries both
            Slider(
                value: Binding(get: { Double(value) }, set: { value = Int($0.rounded()) }),
                in: Double(range.lowerBound)...Double(range.upperBound), step: 5
            )
            // No label view here: VoiceOver would read it and the accessibility label, "Pitch, Pitch".
            .accessibilityLabel(Text(title))
            .accessibilityValue(Text(Double(value) / 100, format: .percent))
        }
    }
}

/// The license of a voice, shown before it is downloaded (ANALYSIS 10.7).
struct LicenseSheet: View {
    @EnvironmentObject private var library: VoiceLibrary
    let voice: Voice
    let license: String

    var body: some View {
        NavigationStack {
            ScrollView {
                Text(license)
                    .font(.callout)
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding()
            }
            .navigationTitle("License of the voice \(voice.name)")
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Decline", role: .cancel) { library.cancelInstall(voice) }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Accept and Install") { library.acceptLicense(voice) }
                }
            }
        }
        #if os(macOS)
        .frame(minWidth: 480, minHeight: 420)
        #endif
    }
}
