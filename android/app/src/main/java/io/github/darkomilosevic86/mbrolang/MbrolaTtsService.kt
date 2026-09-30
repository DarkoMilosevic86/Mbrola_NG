// MBROLA NG - the Android text-to-speech engine (ANALYSIS 14.2)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

import android.media.AudioFormat
import android.speech.tts.SynthesisCallback
import android.speech.tts.SynthesisRequest
import android.speech.tts.TextToSpeech
import android.speech.tts.TextToSpeechService
import android.speech.tts.Voice
import android.util.Log
import java.util.Locale

/**
 * What TalkBack and every other app talk to. Languages and voices offered
 * are exactly the installed voices; speed and volume come from the request
 * unless the voice's settings say to use its own (VoiceSettings).
 *
 * Direct boot aware: it runs before the first unlock, so it touches only
 * device-protected storage (VoiceStore.storage), the APK's assets and the
 * native library folder.
 */
class MbrolaTtsService : TextToSpeechService() {
    @Volatile private var stopRequested = false
    @Volatile private var currentLanguage = arrayOf("eng", "USA", "")

    // Installed voices, re-read when the voice manager changes something.
    private var cachedTick = -1
    private var cachedVoices: List<VoiceEntry> = emptyList()

    private fun voices(): List<VoiceEntry> = synchronized(this) {
        val tick = VoiceStore.changes.value
        if (tick != cachedTick) {
            cachedVoices = VoiceStore.installed(this)
            cachedTick = tick
        }
        cachedVoices
    }

    /** "hr" or "hrv" -> "hrv". */
    private fun iso3Language(lang: String?): String {
        if (lang.isNullOrEmpty()) return ""
        return runCatching { Locale(lang).isO3Language }.getOrNull()?.takeIf { it.isNotEmpty() }
            ?: lang.lowercase(Locale.ROOT)
    }

    /** "US" or "USA" -> "USA". */
    private fun iso3Country(country: String?): String {
        if (country.isNullOrEmpty()) return ""
        return runCatching { Locale("", country).isO3Country }.getOrNull()?.takeIf { it.isNotEmpty() }
            ?: country.uppercase(Locale.ROOT)
    }

    /** The voice set to speak every language (EngineSettings), if it is installed. */
    private fun forcedVoice(): VoiceEntry? {
        val id = EngineSettings.forcedVoice(this) ?: return null
        return voices().firstOrNull { it.id == id }
    }

    private fun pickVoice(lang: String?, country: String?): VoiceEntry? {
        forcedVoice()?.let { return it }
        val l = iso3Language(lang)
        val c = iso3Country(country)
        // own language first ("hrv" for a Croatian voice), then languages it also serves ("srp")
        val candidates = voices().filter { it.iso3Language == l }.ifEmpty { voices().filter { it.speaks(l) } }
        return candidates.firstOrNull { c.isNotEmpty() && it.iso3Country == c } ?: candidates.firstOrNull()
    }

    override fun onIsLanguageAvailable(lang: String?, country: String?, variant: String?): Int {
        // one voice for everything: every language is "available"
        if (forcedVoice() != null) return TextToSpeech.LANG_COUNTRY_AVAILABLE
        val l = iso3Language(lang)
        val candidates = voices().filter { it.speaks(l) }
        if (candidates.isEmpty()) {
            val known = Catalog.get(this).voices.any { it.speaks(l) }
            return if (known) TextToSpeech.LANG_MISSING_DATA else TextToSpeech.LANG_NOT_SUPPORTED
        }
        val c = iso3Country(country)
        val countryKnown = c.isNotEmpty() && candidates.any { v ->
            v.allLocales.any { it.equals("$l-$c", ignoreCase = true) }
        }
        return if (countryKnown) TextToSpeech.LANG_COUNTRY_AVAILABLE else TextToSpeech.LANG_AVAILABLE
    }

    override fun onGetLanguage(): Array<String> = currentLanguage

    override fun onLoadLanguage(lang: String?, country: String?, variant: String?): Int {
        val result = onIsLanguageAvailable(lang, country, variant)
        if (result >= TextToSpeech.LANG_AVAILABLE) {
            // report the language that was asked for (it may be one the voice
            // only serves, or any language at all with a forced voice)
            currentLanguage = arrayOf(
                iso3Language(lang),
                if (result >= TextToSpeech.LANG_COUNTRY_AVAILABLE) iso3Country(country) else "",
                "",
            )
        }
        return result
    }

    override fun onGetVoices(): List<Voice> = voices().map {
        Voice(it.id, it.locale, Voice.QUALITY_NORMAL, Voice.LATENCY_LOW, false, emptySet())
    }

    override fun onIsValidVoiceName(voiceName: String?): Int =
        if (voices().any { it.id == voiceName }) TextToSpeech.SUCCESS else TextToSpeech.ERROR

    override fun onLoadVoice(voiceName: String?): Int = onIsValidVoiceName(voiceName)

    override fun onGetDefaultVoiceNameFor(lang: String?, country: String?, variant: String?): String? =
        pickVoice(lang, country)?.id

    override fun onStop() {
        stopRequested = true
        EngineRegistry.cancel()
    }

    override fun onSynthesizeText(request: SynthesisRequest, callback: SynthesisCallback) {
        stopRequested = false
        val voice = forcedVoice()
            ?: voices().firstOrNull { it.id == request.voiceName }
            ?: pickVoice(request.language, request.country)
        if (voice == null) {
            callback.error(TextToSpeech.ERROR_NOT_INSTALLED_YET)
            return
        }
        val text = request.charSequenceText?.toString() ?: ""
        synchronized(EngineRegistry.lock) {
            val engine = try {
                EngineRegistry.acquire(this, voice)
            } catch (e: Exception) {
                Log.e(TAG, "cannot load voice ${voice.id}", e)
                callback.error(TextToSpeech.ERROR_SYNTHESIS)
                return
            }
            // The framework applies the request's volume to the audio itself,
            // so the "system volume" case leaves the samples untouched (100 %).
            engine.apply(VoiceSettings.load(this, voice.id), request.speechRate, request.pitch, 100)
            if (callback.start(engine.sampleRate, AudioFormat.ENCODING_PCM_16BIT, 1) != TextToSpeech.SUCCESS) return
            if (!engine.begin(text)) {
                Log.e(TAG, "begin failed: ${engine.lastError}")
                callback.error(TextToSpeech.ERROR_SYNTHESIS)
                return
            }
            val pcm = ByteArray((callback.maxBufferSize.coerceIn(1024, 8192)) and 1.inv())
            val events = IntArray(NativeEngine.EVENTS_SIZE)
            while (!stopRequested) {
                val n = engine.read(pcm, events)
                if (n < 0) {
                    Log.e(TAG, "synthesis failed: ${engine.lastError}")
                    callback.error(TextToSpeech.ERROR_SYNTHESIS)
                    return
                }
                for (i in 0 until events[0]) {
                    if (events[1 + i * 4] != NativeEngine.EVENT_WORD) continue
                    val start = events[3 + i * 4]
                    val end = start + events[4 + i * 4]
                    if (start >= 0 && end <= text.length && end > start) {
                        callback.rangeStart(events[2 + i * 4], start, end)
                    }
                }
                if (n > 0 && callback.audioAvailable(pcm, 0, n) != TextToSpeech.SUCCESS) break
                if (n == 0 && events[0] == 0) break
            }
            if (stopRequested) engine.cancel()
            callback.done()
        }
    }

    override fun onDestroy() {
        EngineRegistry.release()
        super.onDestroy()
    }

    private companion object {
        const val TAG = "MbrolaNG"
    }
}
