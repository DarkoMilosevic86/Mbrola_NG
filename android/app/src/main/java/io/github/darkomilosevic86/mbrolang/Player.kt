// MBROLA NG - "Try": speaks with an installed voice, or plays the recorded
// sample of a voice that is not installed
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.media.MediaPlayer
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class Player(private val context: Context) {
    private var job: Job? = null
    @Volatile private var engine: Engine? = null
    private var media: MediaPlayer? = null

    private val attributes: AudioAttributes = AudioAttributes.Builder()
        .setUsage(AudioAttributes.USAGE_MEDIA)
        .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
        .build()

    /** Samples are assets/samples/<voice id>.wav, made by tools/make_voice_samples.py. */
    fun hasSample(voiceId: String): Boolean =
        runCatching { context.assets.open("samples/$voiceId.wav").close() }.isSuccess

    fun stop() {
        engine?.cancel()
        job?.cancel()
        job = null
        media?.let {
            runCatching { it.stop() }
            it.release()
        }
        media = null
    }

    /** Plays the recorded sample; [onDone] runs on the main thread. */
    fun playSample(voiceId: String, onDone: () -> Unit) {
        stop()
        try {
            val fd = context.assets.openFd("samples/$voiceId.wav")
            val mp = MediaPlayer()
            mp.setAudioAttributes(attributes)
            mp.setDataSource(fd.fileDescriptor, fd.startOffset, fd.length)
            fd.close()
            mp.setOnCompletionListener {
                it.release()
                if (media === it) media = null
                onDone()
            }
            mp.prepare()
            mp.start()
            media = mp
        } catch (e: Exception) {
            Log.e(TAG, "sample $voiceId", e)
            onDone()
        }
    }

    /**
     * Speaks [text] with an installed voice and [settings] through its own
     * engine, independent of the system's TTS settings.
     * [onDone] gets an error message or null, on the main thread.
     */
    fun speak(scope: CoroutineScope, voice: VoiceEntry, settings: VoiceSettings, text: String, onDone: (String?) -> Unit) {
        stop()
        job = scope.launch(Dispatchers.IO) {
            var error: String? = null
            var track: AudioTrack? = null
            var e: Engine? = null
            try {
                e = Engine.open(context, voice)
                engine = e
                // "Try" has no system speed/volume to follow: normal speed, full volume
                e.apply(settings)
                if (!e.begin(text)) throw java.io.IOException(e.lastError)
                val rate = e.sampleRate
                val min = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT)
                track = AudioTrack.Builder()
                    .setAudioAttributes(attributes)
                    .setAudioFormat(
                        AudioFormat.Builder().setSampleRate(rate)
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build()
                    )
                    .setBufferSizeInBytes(maxOf(min, 8192))
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .build()
                track.play()
                val pcm = ByteArray(4096)
                val events = IntArray(NativeEngine.EVENTS_SIZE)
                var frames = 0L
                while (isActive) {
                    val n = e.read(pcm, events)
                    if (n < 0) throw java.io.IOException(e.lastError)
                    if (n > 0) {
                        track.write(pcm, 0, n)
                        frames += n / 2
                    }
                    if (n == 0 && events[0] == 0) break
                }
                // let the buffered audio play out (never longer than the audio
                // itself lasts: some devices stop counting frames early)
                track.stop()
                val deadline = System.currentTimeMillis() + frames * 1000 / rate + 500
                while (isActive && track.playbackHeadPosition < frames && System.currentTimeMillis() < deadline) {
                    delay(30)
                }
            } catch (x: kotlinx.coroutines.CancellationException) {
                throw x
            } catch (x: Exception) {
                Log.e(TAG, "speak ${voice.id}", x)
                error = x.message ?: x.toString()
            } finally {
                engine = null
                track?.let {
                    runCatching { it.pause(); it.flush() }
                    it.release()
                }
                e?.close()
            }
            withContext(Dispatchers.Main) { onDone(error) }
        }
    }

    private companion object {
        const val TAG = "MbrolaNG"
    }
}
