// MBROLA NG - state of the voice manager screens
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

import android.app.Application
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Download state of one voice. */
data class Download(val done: Long, val total: Long)

/** A license waiting for the user's decision. */
data class LicenseRequest(val voice: VoiceEntry, val text: String)

class VoicesViewModel(app: Application) : AndroidViewModel(app) {
    private val context get() = getApplication<Application>()
    val catalog: Catalog = Catalog.get(app)
    private val player = Player(app)

    var installed by mutableStateOf<List<VoiceEntry>>(emptyList())
        private set

    /** Voice id -> progress, while its license or files are being downloaded. */
    val downloads = mutableStateMapOf<String, Download>()
    var license by mutableStateOf<LicenseRequest?>(null)
        private set

    /** Text for the message dialog (errors, "installed"). */
    var message by mutableStateOf<String?>(null)

    /** Voice whose "Try" is playing now. */
    var playing by mutableStateOf<String?>(null)
        private set

    private val jobs = HashMap<String, Job>()

    init {
        viewModelScope.launch {
            VoiceStore.changes.collect {
                installed = withContext(Dispatchers.IO) { VoiceStore.installed(context) }
            }
        }
    }

    fun isInstalled(voice: VoiceEntry): Boolean = installed.any { it.id == voice.id }

    fun hasSample(voice: VoiceEntry): Boolean = player.hasSample(voice.id)

    // ---------------------------------------------------------------- install
    /** Install, step 1: fetch the license and ask. */
    fun requestInstall(voice: VoiceEntry) {
        if (voice.id in downloads) return
        downloads[voice.id] = Download(0, voice.downloadSize)
        jobs[voice.id] = viewModelScope.launch {
            try {
                val text = VoiceInstaller.fetchLicense(context, voice)
                license = LicenseRequest(voice, text)
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                downloads.remove(voice.id)
                message = errorText(e)
            }
        }
    }

    /** Install, step 2: the user accepted the license. */
    fun acceptLicense() {
        val voice = license?.voice ?: return
        license = null
        jobs[voice.id] = viewModelScope.launch {
            try {
                VoiceInstaller.install(context, voice) { done, total ->
                    downloads[voice.id] = Download(done, total)
                }
                message = context.getString(R.string.voice_installed, voice.names.localized(context))
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                message = errorText(e)
            } finally {
                downloads.remove(voice.id)
            }
        }
    }

    fun declineLicense() {
        val voice = license?.voice ?: return
        license = null
        downloads.remove(voice.id)
        VoiceInstaller.discard(context, voice)
    }

    fun cancelDownload(voice: VoiceEntry) {
        jobs.remove(voice.id)?.cancel()
        downloads.remove(voice.id)
        if (license?.voice?.id == voice.id) license = null
        VoiceInstaller.discard(context, voice)
    }

    private fun errorText(e: Exception): String = when (e) {
        is VoiceInstaller.VoiceException -> context.getString(e.reason)
        else -> context.getString(R.string.error_generic, e.message ?: e.javaClass.simpleName)
    }

    fun remove(voice: VoiceEntry) {
        if (playing == voice.id) stopPlaying()
        viewModelScope.launch(Dispatchers.IO) { VoiceStore.remove(context, voice.id) }
    }

    // -------------------------------------------------------------------- try
    /** Installed voice: speaks the "installed and working" sentence; otherwise the recorded sample. */
    fun tryVoice(voice: VoiceEntry) {
        if (playing == voice.id) {
            stopPlaying()
            return
        }
        playing = voice.id
        if (isInstalled(voice)) {
            player.speak(
                viewModelScope, voice, VoiceSettings.load(context, voice.id),
                SampleTexts.installedAndWorking(voice.language),
            ) { error ->
                if (playing == voice.id) playing = null
                if (error != null) message = context.getString(R.string.error_speak, error)
            }
        } else {
            player.playSample(voice.id) {
                if (playing == voice.id) playing = null
            }
        }
    }

    fun stopPlaying() {
        player.stop()
        playing = null
    }

    override fun onCleared() {
        player.stop()
    }
}
