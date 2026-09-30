// MBROLA NG - one synthesis engine (one voice) and the engine shared with
// the TTS service
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

import android.content.Context
import java.io.Closeable
import java.io.IOException

/**
 * A core engine for one voice; it owns one mbrola_ng_synth process.
 * Every call except [cancel] must come from one thread at a time.
 */
class Engine private constructor(private var handle: Long, val voiceId: String) : Closeable {
    val sampleRate: Int = NativeEngine.nativeSampleRate(handle)
    private val cancelLock = Any()

    /**
     * @param systemRate   speed asked by the system, percent (100 = normal)
     * @param systemPitch  pitch asked by the system, percent
     * @param systemVolume volume asked by the system, percent
     */
    fun apply(s: VoiceSettings, systemRate: Int = 100, systemPitch: Int = 100, systemVolume: Int = 100) {
        val rate = if (s.useOwnRate) s.rate else systemRate
        val volume = if (s.useOwnVolume) s.volume else systemVolume
        NativeEngine.nativeSetParam(handle, NativeEngine.PARAM_RATE, rate.coerceIn(20, 800))
        NativeEngine.nativeSetParam(handle, NativeEngine.PARAM_PITCH, (s.pitch * systemPitch / 100).coerceIn(25, 400))
        NativeEngine.nativeSetParam(handle, NativeEngine.PARAM_RANGE, s.modulation.coerceIn(0, 300))
        NativeEngine.nativeSetParam(handle, NativeEngine.PARAM_VOLUME, volume.coerceIn(0, 400))
        NativeEngine.nativeSetParam(handle, NativeEngine.PARAM_EMOJI, if (s.emoji) 1 else 0)
        NativeEngine.nativeSetParam(handle, NativeEngine.PARAM_DIGITS, s.numbers)
    }

    fun begin(text: String, spell: Boolean = false): Boolean =
        NativeEngine.nativeBegin(handle, text, spell) == 0

    /** Bytes of 16-bit PCM written to [pcm]; 0 at the end; negative on error. */
    fun read(pcm: ByteArray, events: IntArray): Int = NativeEngine.nativeRead(handle, pcm, events)

    /** Stops the current utterance; safe from any thread. */
    fun cancel() = synchronized(cancelLock) {
        if (handle != 0L) NativeEngine.nativeCancel(handle)
    }

    val lastError: String get() = if (handle != 0L) NativeEngine.nativeLastError(handle) else ""

    override fun close() = synchronized(cancelLock) {
        if (handle != 0L) {
            NativeEngine.nativeDestroy(handle)
            handle = 0
        }
    }

    companion object {
        @Throws(IOException::class)
        fun open(context: Context, voice: VoiceEntry): Engine {
            val language = VoiceStore.languageFile(context, voice.language)
            val database = VoiceStore.database(context, voice.id)
            if (!database.isFile) throw IOException("voice ${voice.id} is not installed")
            val h = NativeEngine.nativeCreate(
                language.absolutePath, database.absolutePath, VoiceStore.synthPath(context),
                voice.basePitch, voice.phonemeMap,
            )
            if (h == 0L) throw IOException(NativeEngine.nativeLastError(0))
            return Engine(h, voice.id)
        }
    }
}

/**
 * The engine used by the TTS service. One voice is loaded at a time (each
 * engine is a process with a voice database in memory); the voice manager
 * releases it before it removes or replaces the voice files.
 */
object EngineRegistry {
    /** Held while an utterance is synthesized and while the engine changes. */
    val lock = Any()
    @Volatile private var current: Engine? = null

    /** Call with [lock] held. */
    fun acquire(context: Context, voice: VoiceEntry): Engine {
        current?.let { if (it.voiceId == voice.id) return it else it.close() }
        current = null
        return Engine.open(context, voice).also { current = it }
    }

    fun cancel() {
        current?.cancel()
    }

    fun release(voiceId: String? = null) {
        val e = current ?: return
        if (voiceId != null && e.voiceId != voiceId) return
        e.cancel()
        synchronized(lock) {
            if (current === e) current = null
            e.close()
        }
    }
}

/** Sentences spoken by "Try" and given to the system as sample text. */
object SampleTexts {
    fun installedAndWorking(language: String): String = when (language) {
        "hr" -> "Ukoliko čujete ovu poruku, ovaj glas je instaliran i radi."
        else -> "If you can hear this message, this voice is installed and working."
    }

    fun sample(language: String): String = when (language) {
        "hr" -> "Ovo je primjer sinteze govora na hrvatskom jeziku."
        else -> "This is an example of speech synthesis in English."
    }
}
