// MBROLA NG - the small activities the Android TTS framework calls
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.speech.tts.TextToSpeech
import java.util.Locale

/** ACTION_CHECK_TTS_DATA: which languages have a voice installed. */
class CheckVoiceData : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val installed = VoiceStore.installed(this)
        val available = ArrayList(installed.flatMap { it.allLocales }.distinct())
        // A voice set to speak every language also covers the languages of
        // the phone itself (the system settings enable "Play" only for
        // languages listed here).
        val forced = EngineSettings.forcedVoice(this)
        if (forced != null && installed.any { it.id == forced }) {
            val locales = resources.configuration.locales
            for (i in 0 until locales.size()) {
                val l = locales[i]
                val tag = runCatching {
                    l.isO3Language + (if (l.country.isNotEmpty()) "-" + l.isO3Country else "")
                }.getOrNull()
                if (!tag.isNullOrEmpty() && tag !in available) available += tag
            }
        }
        val unavailable = ArrayList(
            Catalog.get(this).voices.flatMap { it.allLocales }.distinct().filter { it !in available }
        )
        val result = Intent()
            .putStringArrayListExtra(TextToSpeech.Engine.EXTRA_AVAILABLE_VOICES, available)
            .putStringArrayListExtra(TextToSpeech.Engine.EXTRA_UNAVAILABLE_VOICES, unavailable)
        setResult(
            if (available.isEmpty()) TextToSpeech.Engine.CHECK_VOICE_DATA_FAIL
            else TextToSpeech.Engine.CHECK_VOICE_DATA_PASS,
            result,
        )
        finish()
    }
}

/** ACTION_GET_SAMPLE_TEXT: the sentence the system's "Listen to an example" speaks. */
class GetSampleText : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val lang = intent.getStringExtra("language") ?: Locale.getDefault().language
        // "hrv" or "hr" -> the language code of our data ("hr")
        val code = Catalog.get(this).languages.firstOrNull {
            it.code.equals(lang, true) || it.voices.any { v -> v.speaks(lang) }
        }?.code ?: EngineSettings.forcedVoice(this)?.let { Catalog.get(this).voice(it)?.language } ?: "en"
        setResult(
            TextToSpeech.LANG_AVAILABLE,
            Intent().putExtra(TextToSpeech.Engine.EXTRA_SAMPLE_TEXT, SampleTexts.sample(code)),
        )
        finish()
    }
}
