// MBROLA NG - Kotlin side of the JNI bridge (cpp/mbng_jni.cpp)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

/** The core C API (core/include/mbrola_ng.h). Use [Engine], not this. */
object NativeEngine {
    init {
        System.loadLibrary("mbrola_ng_jni")
    }

    // MBNG_PARAM_*
    const val PARAM_RATE = 1
    const val PARAM_PITCH = 2
    const val PARAM_RANGE = 3
    const val PARAM_VOLUME = 4
    const val PARAM_EMOJI = 6
    const val PARAM_DIGITS = 8

    // MBNG_EVENT_*
    const val EVENT_WORD = 1

    /** Size of the `events` array of [nativeRead]: count + 32 events of 4 ints. */
    const val EVENTS_SIZE = 1 + 32 * 4

    @JvmStatic external fun nativeCreate(
        language: String, voice: String, synth: String, basePitch: Int, phonemeMap: String,
    ): Long

    @JvmStatic external fun nativeDestroy(handle: Long)
    @JvmStatic external fun nativeSampleRate(handle: Long): Int
    @JvmStatic external fun nativeSetParam(handle: Long, param: Int, value: Int): Int
    @JvmStatic external fun nativeBegin(handle: Long, text: String, spell: Boolean): Int
    @JvmStatic external fun nativeRead(handle: Long, pcm: ByteArray, events: IntArray): Int
    @JvmStatic external fun nativeCancel(handle: Long)
    @JvmStatic external fun nativeLastError(handle: Long): String
}
