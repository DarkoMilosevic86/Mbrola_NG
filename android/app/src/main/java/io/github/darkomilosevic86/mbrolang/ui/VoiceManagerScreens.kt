// MBROLA NG - voice manager: home screen, language screen, dialogs
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang.ui

import android.text.format.Formatter
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import io.github.darkomilosevic86.mbrolang.BuildConfig
import io.github.darkomilosevic86.mbrolang.Download
import io.github.darkomilosevic86.mbrolang.LanguageEntry
import io.github.darkomilosevic86.mbrolang.R
import io.github.darkomilosevic86.mbrolang.VoiceEntry
import io.github.darkomilosevic86.mbrolang.VoicesViewModel
import io.github.darkomilosevic86.mbrolang.localized

private val ScreenPadding = PaddingValues(horizontal = 16.dp, vertical = 12.dp)

@Composable
private fun SectionTitle(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.titleLarge,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier
            .padding(top = 12.dp, bottom = 4.dp)
            .semantics { heading() },
    )
}

@Composable
private fun IconLabel(icon: ImageVector, text: String) {
    Icon(icon, contentDescription = null, modifier = Modifier.size(18.dp))
    Spacer(Modifier.size(6.dp))
    Text(text)
}

/** Home: installed voices, languages to install from, system engine hint. */
@Composable
fun HomeScreen(
    vm: VoicesViewModel,
    contentPadding: PaddingValues,
    onOpenLanguage: (LanguageEntry) -> Unit,
    onVoiceSettings: (VoiceEntry) -> Unit,
    onOpenTtsSettings: () -> Unit,
    onOpenSource: () -> Unit,
) {
    val context = LocalContext.current
    LazyColumn(
        contentPadding = contentPadding,
        verticalArrangement = Arrangement.spacedBy(10.dp),
        modifier = Modifier.padding(ScreenPadding),
    ) {
        item { SectionTitle(stringResource(R.string.installed_voices)) }
        if (vm.installed.isEmpty()) {
            item {
                Text(stringResource(R.string.no_voice_installed), style = MaterialTheme.typography.bodyLarge)
            }
        }
        items(vm.installed, key = { "installed-" + it.id }) { voice ->
            VoiceCard(vm, voice, showLanguage = true, onVoiceSettings = onVoiceSettings)
        }

        item { SectionTitle(stringResource(R.string.install_voices)) }
        item {
            Text(stringResource(R.string.choose_language_hint), style = MaterialTheme.typography.bodyMedium)
        }
        items(vm.catalog.languages, key = { "language-" + it.code }) { language ->
            val count = language.voices.size
            val have = language.voices.count { vm.isInstalled(it) }
            Card(
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer),
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable(role = Role.Button) { onOpenLanguage(language) },
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .heightIn(min = 72.dp)
                        .padding(horizontal = 16.dp, vertical = 12.dp),
                ) {
                    Column(Modifier.weight(1f)) {
                        Text(language.names.localized(context), style = MaterialTheme.typography.titleMedium)
                        Text(
                            pluralStringResource(R.plurals.voice_count, count, count) + ", " +
                                stringResource(R.string.installed_count, have),
                            style = MaterialTheme.typography.bodyMedium,
                        )
                    }
                    Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null)
                }
            }
        }

        item { SectionTitle(stringResource(R.string.system_engine_title)) }
        item {
            Text(stringResource(R.string.system_engine_text), style = MaterialTheme.typography.bodyMedium)
            Spacer(Modifier.height(8.dp))
            Button(onClick = onOpenTtsSettings) { Text(stringResource(R.string.open_tts_settings)) }
        }

        item {
            Spacer(Modifier.height(8.dp))
            Text(
                stringResource(R.string.about_text, BuildConfig.VERSION_NAME),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            TextButton(onClick = onOpenSource) { Text(stringResource(R.string.source_code)) }
        }
    }
}

/** The voices of one language. */
@Composable
fun LanguageScreen(
    vm: VoicesViewModel,
    language: LanguageEntry,
    contentPadding: PaddingValues,
    onVoiceSettings: (VoiceEntry) -> Unit,
) {
    LazyColumn(
        contentPadding = contentPadding,
        verticalArrangement = Arrangement.spacedBy(10.dp),
        modifier = Modifier.padding(ScreenPadding),
    ) {
        items(language.voices, key = { it.id }) { voice ->
            VoiceCard(vm, voice, showLanguage = false, onVoiceSettings = onVoiceSettings)
        }
    }
}

/**
 * One voice. Installed: Voice settings, Try, Remove. Not installed: Try
 * (only when a recorded sample exists) and Install, or the download progress.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun VoiceCard(vm: VoicesViewModel, voice: VoiceEntry, showLanguage: Boolean, onVoiceSettings: (VoiceEntry) -> Unit) {
    val context = LocalContext.current
    val installed = vm.isInstalled(voice)
    val download: Download? = vm.downloads[voice.id]
    val playing = vm.playing == voice.id
    var confirmRemove by remember { mutableStateOf(false) }
    val name = voice.names.localized(context)

    ElevatedCard(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(name, style = MaterialTheme.typography.titleMedium)
                    val language = vm.catalog.language(voice.language)?.names?.localized(context) ?: voice.language
                    val details = buildList {
                        if (showLanguage) add(language)
                        add(
                            if (installed) stringResource(R.string.installed)
                            else Formatter.formatShortFileSize(context, voice.downloadSize)
                        )
                    }
                    Text(details.joinToString(" · "), style = MaterialTheme.typography.bodyMedium)
                }
                if (installed) {
                    Icon(
                        Icons.Filled.CheckCircle, contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                    )
                }
            }
            Spacer(Modifier.height(10.dp))

            if (download != null) {
                val percent = if (download.total > 0) (100 * download.done / download.total).toInt() else 0
                LinearProgressIndicator(
                    progress = { percent / 100f },
                    modifier = Modifier.fillMaxWidth(),
                )
                Spacer(Modifier.height(6.dp))
                Text(
                    stringResource(
                        R.string.downloading,
                        Formatter.formatShortFileSize(context, download.done),
                        Formatter.formatShortFileSize(context, download.total),
                    ),
                    style = MaterialTheme.typography.bodyMedium,
                    // announced by TalkBack as it changes, without taking focus
                    modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
                )
                TextButton(onClick = { vm.cancelDownload(voice) }) { Text(stringResource(R.string.cancel)) }
                return@Column
            }

            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (installed) {
                    FilledTonalButton(onClick = { onVoiceSettings(voice) }) {
                        IconLabel(Icons.Filled.Settings, stringResource(R.string.voice_settings))
                    }
                    OutlinedButton(onClick = { vm.tryVoice(voice) }) {
                        IconLabel(Icons.Filled.PlayArrow, stringResource(if (playing) R.string.stop else R.string.try_voice))
                    }
                    OutlinedButton(
                        onClick = { confirmRemove = true },
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error),
                    ) {
                        IconLabel(Icons.Filled.Delete, stringResource(R.string.remove))
                    }
                } else {
                    if (vm.hasSample(voice)) {
                        OutlinedButton(onClick = { vm.tryVoice(voice) }) {
                            IconLabel(Icons.Filled.PlayArrow, stringResource(if (playing) R.string.stop else R.string.try_voice))
                        }
                    }
                    Button(onClick = { vm.requestInstall(voice) }) { Text(stringResource(R.string.install)) }
                }
            }
        }
    }

    if (confirmRemove) {
        AlertDialog(
            onDismissRequest = { confirmRemove = false },
            title = { Text(stringResource(R.string.remove_title)) },
            text = { Text(stringResource(R.string.remove_message, name)) },
            confirmButton = {
                TextButton(onClick = {
                    confirmRemove = false
                    vm.remove(voice)
                }) { Text(stringResource(R.string.remove)) }
            },
            dismissButton = {
                TextButton(onClick = { confirmRemove = false }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }
}

/** License of the voice about to be installed, and the message dialog. */
@Composable
fun VoiceManagerDialogs(vm: VoicesViewModel) {
    val context = LocalContext.current
    vm.license?.let { request ->
        val summary = request.voice.licenseSummary.localized(context)
        AlertDialog(
            onDismissRequest = { vm.declineLicense() },
            title = { Text(stringResource(R.string.license_title, request.voice.names.localized(context))) },
            text = {
                Column(Modifier.verticalScroll(rememberScrollState())) {
                    if (summary.isNotEmpty()) {
                        Text(summary, style = MaterialTheme.typography.bodyLarge)
                        Spacer(Modifier.height(12.dp))
                    }
                    Text(request.text.trim(), style = MaterialTheme.typography.bodySmall)
                }
            },
            confirmButton = {
                Button(onClick = { vm.acceptLicense() }) { Text(stringResource(R.string.accept_and_install)) }
            },
            dismissButton = {
                TextButton(onClick = { vm.declineLicense() }) { Text(stringResource(R.string.decline)) }
            },
        )
    }
    vm.message?.let { text ->
        AlertDialog(
            onDismissRequest = { vm.message = null },
            text = { Text(text) },
            confirmButton = {
                TextButton(onClick = { vm.message = null }) { Text(stringResource(android.R.string.ok)) }
            },
        )
    }
}
