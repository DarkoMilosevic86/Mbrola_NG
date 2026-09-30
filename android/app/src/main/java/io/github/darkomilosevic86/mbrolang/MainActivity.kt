// MBROLA NG - main screen: the voice manager (ANALYSIS 14.4)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.darkomilosevic86.mbrolang.ui.HomeScreen
import io.github.darkomilosevic86.mbrolang.ui.LanguageScreen
import io.github.darkomilosevic86.mbrolang.ui.MbrolaTheme
import io.github.darkomilosevic86.mbrolang.ui.VoiceManagerDialogs

/**
 * Installed voices, the languages to install voices from, and per-voice
 * actions. Also opened by the system for "install voice data".
 */
class MainActivity : ComponentActivity() {
    @OptIn(ExperimentalMaterial3Api::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            MbrolaTheme {
                val vm: VoicesViewModel = viewModel()
                val context = LocalContext.current
                // code of the language whose voices are shown; null = home
                var languageCode by rememberSaveable { mutableStateOf<String?>(null) }
                val language = languageCode?.let { vm.catalog.language(it) }
                BackHandler(enabled = language != null) { languageCode = null }

                Scaffold(
                    topBar = {
                        TopAppBar(
                            title = {
                                Text(language?.names?.localized(context) ?: stringResource(R.string.app_name))
                            },
                            navigationIcon = {
                                if (language != null) {
                                    IconButton(onClick = { languageCode = null }) {
                                        Icon(
                                            Icons.AutoMirrored.Filled.ArrowBack,
                                            contentDescription = stringResource(R.string.back),
                                        )
                                    }
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
                    if (language == null) {
                        HomeScreen(
                            vm, padding,
                            onOpenLanguage = { languageCode = it.code },
                            onVoiceSettings = ::openVoiceSettings,
                            onOpenTtsSettings = ::openTtsSettings,
                            onOpenSource = ::openSource,
                        )
                    } else {
                        LanguageScreen(vm, language, padding, onVoiceSettings = ::openVoiceSettings)
                    }
                    VoiceManagerDialogs(vm)
                }
            }
        }
    }

    private fun openVoiceSettings(voice: VoiceEntry) {
        startActivity(
            Intent(this, VoiceSettingsActivity::class.java).putExtra(VoiceSettingsActivity.EXTRA_VOICE, voice.id)
        )
    }

    /** Android does not let an app make itself the preferred engine: the user chooses it here. */
    private fun openTtsSettings() {
        try {
            startActivity(Intent("com.android.settings.TTS_SETTINGS").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        } catch (e: ActivityNotFoundException) {
            startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
        }
    }

    private fun openSource() {
        try {
            startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(SOURCE_URL)))
        } catch (e: ActivityNotFoundException) {
            // no browser: nothing to do
        }
    }

    private companion object {
        const val SOURCE_URL = "https://github.com/DarkoMilosevic86/Mbrola_NG"
    }
}
