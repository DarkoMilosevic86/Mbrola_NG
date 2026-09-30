// MBROLA NG - voice settings; also the engine's settings screen that the
// system opens from its text-to-speech settings (res/xml/tts_engine.xml)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import io.github.darkomilosevic86.mbrolang.ui.MbrolaTheme
import kotlin.math.roundToInt

class VoiceSettingsActivity : ComponentActivity() {
    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val requested = intent.getStringExtra(EXTRA_VOICE)
        setContent {
            MbrolaTheme {
                Scaffold(
                    topBar = {
                        TopAppBar(
                            title = { Text(stringResource(R.string.voice_settings)) },
                            navigationIcon = {
                                IconButton(onClick = { finish() }) {
                                    Icon(
                                        Icons.AutoMirrored.Filled.ArrowBack,
                                        contentDescription = stringResource(R.string.back),
                                    )
                                }
                            },
                            colors = TopAppBarDefaults.topAppBarColors(
                                containerColor = MaterialTheme.colorScheme.primaryContainer,
                                titleContentColor = MaterialTheme.colorScheme.onPrimaryContainer,
                                navigationIconContentColor = MaterialTheme.colorScheme.onPrimaryContainer,
                            ),
                        )
                    },
                ) { padding ->
                    Column(
                        Modifier
                            .padding(padding)
                            .verticalScroll(rememberScrollState())
                            .padding(horizontal = 16.dp, vertical = 12.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        SettingsContent(requested)
                    }
                }
            }
        }
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    private fun SettingsContent(requested: String?) {
        val context = LocalContext.current
        val voices = remember { VoiceStore.installed(context) }
        if (voices.isEmpty()) {
            Text(stringResource(R.string.no_voice_for_settings), style = MaterialTheme.typography.bodyLarge)
            Spacer(Modifier.height(12.dp))
            Button(onClick = {
                startActivity(Intent(context, MainActivity::class.java))
                finish()
            }) { Text(stringResource(R.string.open_voice_manager)) }
            return
        }
        var voiceId by rememberSaveable { mutableStateOf(voices.firstOrNull { it.id == requested }?.id ?: voices[0].id) }
        val voice = voices.firstOrNull { it.id == voiceId } ?: voices[0]
        var settings by remember(voice.id) { mutableStateOf(VoiceSettings.load(context, voice.id)) }
        fun update(s: VoiceSettings) {
            settings = s
            VoiceSettings.save(context, voice.id, s)
        }

        // Voice picker (the system opens this screen without naming a voice)
        if (voices.size > 1) {
            var open by remember { mutableStateOf(false) }
            ExposedDropdownMenuBox(expanded = open, onExpandedChange = { open = it }) {
                OutlinedTextField(
                    value = voice.names.localized(context),
                    onValueChange = {},
                    readOnly = true,
                    label = { Text(stringResource(R.string.voice)) },
                    trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = open) },
                    modifier = Modifier
                        .menuAnchor(MenuAnchorType.PrimaryNotEditable)
                        .fillMaxWidth(),
                )
                ExposedDropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                    voices.forEach { v ->
                        DropdownMenuItem(
                            text = { Text(v.names.localized(context)) },
                            onClick = {
                                voiceId = v.id
                                open = false
                            },
                        )
                    }
                }
            }
        } else {
            Text(
                voice.names.localized(context),
                style = MaterialTheme.typography.titleLarge,
                modifier = Modifier.semantics { heading() },
            )
        }
        Spacer(Modifier.height(8.dp))

        // ---- one voice for every language
        var forced by remember { mutableStateOf(EngineSettings.forcedVoice(context)) }
        CheckRow(stringResource(R.string.always_use_voice), forced == voice.id) {
            forced = if (it) voice.id else null
            EngineSettings.setForcedVoice(context, forced)
        }
        Hint(stringResource(R.string.always_use_voice_hint))
        HorizontalDivider(Modifier.padding(top = 8.dp))

        // ---- speed
        CheckRow(stringResource(R.string.use_own_rate), settings.useOwnRate) { update(settings.copy(useOwnRate = it)) }
        if (!settings.useOwnRate) Hint(stringResource(R.string.rate_from_system))
        PercentSlider(
            stringResource(R.string.speed), settings.rate, VoiceSettings.RATE_MIN, VoiceSettings.RATE_MAX, 10,
            enabled = settings.useOwnRate,
        ) { update(settings.copy(rate = it)) }
        HorizontalDivider()

        // ---- pitch and modulation
        PercentSlider(
            stringResource(R.string.pitch), settings.pitch, VoiceSettings.PITCH_MIN, VoiceSettings.PITCH_MAX, 5,
        ) { update(settings.copy(pitch = it)) }
        PercentSlider(
            stringResource(R.string.modulation), settings.modulation, 0, VoiceSettings.MODULATION_MAX, 10,
        ) { update(settings.copy(modulation = it)) }
        HorizontalDivider()

        // ---- volume
        CheckRow(stringResource(R.string.use_own_volume), settings.useOwnVolume) { update(settings.copy(useOwnVolume = it)) }
        if (!settings.useOwnVolume) Hint(stringResource(R.string.volume_from_system))
        PercentSlider(
            stringResource(R.string.volume), settings.volume, 0, VoiceSettings.VOLUME_MAX, 10,
            enabled = settings.useOwnVolume,
        ) { update(settings.copy(volume = it)) }
        HorizontalDivider()

        // ---- text
        CheckRow(stringResource(R.string.read_emoji), settings.emoji) { update(settings.copy(emoji = it)) }
        Text(
            stringResource(R.string.numbers),
            style = MaterialTheme.typography.titleMedium,
            modifier = Modifier
                .padding(top = 12.dp)
                .semantics { heading() },
        )
        Column(Modifier.selectableGroup()) {
            listOf(
                NumberMode.WHOLE to R.string.numbers_whole,
                NumberMode.DIGITS to R.string.numbers_digits,
                NumberMode.PAIRS to R.string.numbers_pairs,
            ).forEach { (mode, label) ->
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(min = 48.dp)
                        .selectable(
                            selected = settings.numbers == mode,
                            role = Role.RadioButton,
                            onClick = { update(settings.copy(numbers = mode)) },
                        ),
                ) {
                    RadioButton(selected = settings.numbers == mode, onClick = null)
                    Text(stringResource(label), Modifier.padding(start = 12.dp))
                }
            }
        }
        HorizontalDivider()

        // ---- try / reset
        val scope = rememberCoroutineScope()
        val player = remember { Player(applicationContext) }
        var playing by remember { mutableStateOf(false) }
        var error by remember { mutableStateOf<String?>(null) }
        DisposableEffect(Unit) { onDispose { player.stop() } }
        Spacer(Modifier.height(8.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = {
                if (playing) {
                    player.stop()
                    playing = false
                } else {
                    playing = true
                    error = null
                    player.speak(scope, voice, settings, SampleTexts.installedAndWorking(voice.language)) {
                        playing = false
                        error = it
                    }
                }
            }) { Text(stringResource(if (playing) R.string.stop else R.string.try_voice)) }
            OutlinedButton(onClick = { update(VoiceSettings()) }) { Text(stringResource(R.string.reset)) }
        }
        error?.let {
            Text(stringResource(R.string.error_speak, it), color = MaterialTheme.colorScheme.error)
        }
        Spacer(Modifier.height(24.dp))
    }

    @Composable
    private fun Hint(text: String) {
        Text(
            text, style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }

    @Composable
    private fun CheckRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = 48.dp)
                .toggleable(value = checked, role = Role.Checkbox, onValueChange = onChange),
        ) {
            Checkbox(checked = checked, onCheckedChange = null)
            Text(label, Modifier.padding(start = 12.dp), style = MaterialTheme.typography.bodyLarge)
        }
    }

    /** A labelled slider in percent; TalkBack reads "Speed, 150 %". */
    @Composable
    private fun PercentSlider(
        label: String, value: Int, min: Int, max: Int, step: Int,
        enabled: Boolean = true, onChange: (Int) -> Unit,
    ) {
        val percent = stringResource(R.string.percent, value)
        Row(Modifier.padding(top = 8.dp)) {
            Text(label, Modifier.weight(1f), style = MaterialTheme.typography.titleMedium,
                color = if (enabled) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant)
            Text(percent, style = MaterialTheme.typography.titleMedium,
                color = if (enabled) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Slider(
            value = value.toFloat(),
            onValueChange = { onChange(((it / step).roundToInt() * step).coerceIn(min, max)) },
            valueRange = min.toFloat()..max.toFloat(),
            steps = (max - min) / step - 1,
            enabled = enabled,
            modifier = Modifier.semantics {
                contentDescription = label
                stateDescription = percent
            },
        )
    }

    companion object {
        /** Voice id to show; absent when the system opens the engine settings. */
        const val EXTRA_VOICE = "voice"
    }
}
