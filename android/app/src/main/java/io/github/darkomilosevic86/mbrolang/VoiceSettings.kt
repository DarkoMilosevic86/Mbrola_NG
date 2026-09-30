// MBROLA NG - settings of one voice
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

import android.content.Context

/** How numbers are read (MBNG_PARAM_DIGITS). */
object NumberMode {
    const val WHOLE = 0
    const val DIGITS = 1
    const val PAIRS = 2
}

/** Settings of the engine as a whole (device-protected, like the voice settings). */
object EngineSettings {
    private const val PREFS = "engine_settings"
    private const val FORCED_VOICE = "forcedVoice"

    private fun prefs(context: Context) =
        VoiceStore.storage(context).getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /**
     * Id of the voice that speaks EVERYTHING, whatever language the system or
     * an app asks for (a phone set to a language MBROLA NG does not have
     * would otherwise stay silent); null = voices are chosen by language.
     */
    fun forcedVoice(context: Context): String? = prefs(context).getString(FORCED_VOICE, null)

    fun setForcedVoice(context: Context, voiceId: String?) {
        val e = prefs(context).edit()
        if (voiceId == null) e.remove(FORCED_VOICE) else e.putString(FORCED_VOICE, voiceId)
        e.commit()  // written before the service is asked again
        VoiceStore.notifyChanged()
    }
}

data class VoiceSettings(
    /** false: the speed comes from the system (TalkBack, TTS settings). */
    val useOwnRate: Boolean = false,
    val rate: Int = 100,          // percent, 100 = normal
    val pitch: Int = 100,         // percent
    val modulation: Int = 100,    // intonation range, percent (0 = monotone)
    /** false: the volume comes from the system / the app that speaks. */
    val useOwnVolume: Boolean = false,
    val volume: Int = 100,        // percent
    val emoji: Boolean = true,
    val numbers: Int = NumberMode.WHOLE,
) {
    companion object {
        const val RATE_MIN = 50
        const val RATE_MAX = 400
        const val PITCH_MIN = 50
        const val PITCH_MAX = 200
        const val MODULATION_MAX = 200
        const val VOLUME_MAX = 200

        private const val PREFS = "voice_settings"

        // device-protected storage: the TTS service reads these before the
        // first unlock (direct boot)
        private fun prefs(context: Context) =
            VoiceStore.storage(context).getSharedPreferences(PREFS, Context.MODE_PRIVATE)

        fun load(context: Context, voiceId: String): VoiceSettings {
            val p = prefs(context)
            val d = VoiceSettings()
            return VoiceSettings(
                useOwnRate = p.getBoolean("$voiceId.useOwnRate", d.useOwnRate),
                rate = p.getInt("$voiceId.rate", d.rate),
                pitch = p.getInt("$voiceId.pitch", d.pitch),
                modulation = p.getInt("$voiceId.modulation", d.modulation),
                useOwnVolume = p.getBoolean("$voiceId.useOwnVolume", d.useOwnVolume),
                volume = p.getInt("$voiceId.volume", d.volume),
                emoji = p.getBoolean("$voiceId.emoji", d.emoji),
                numbers = p.getInt("$voiceId.numbers", d.numbers),
            )
        }

        fun save(context: Context, voiceId: String, s: VoiceSettings) {
            prefs(context).edit()
                .putBoolean("$voiceId.useOwnRate", s.useOwnRate)
                .putInt("$voiceId.rate", s.rate)
                .putInt("$voiceId.pitch", s.pitch)
                .putInt("$voiceId.modulation", s.modulation)
                .putBoolean("$voiceId.useOwnVolume", s.useOwnVolume)
                .putInt("$voiceId.volume", s.volume)
                .putBoolean("$voiceId.emoji", s.emoji)
                .putInt("$voiceId.numbers", s.numbers)
                .apply()
        }

        fun clear(context: Context, voiceId: String) {
            val p = prefs(context)
            val e = p.edit()
            p.all.keys.filter { it.startsWith("$voiceId.") }.forEach { e.remove(it) }
            e.apply()
        }
    }
}
