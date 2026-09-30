// MBROLA NG - downloads, verifies and installs a voice (ANALYSIS 10.7)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import kotlin.coroutines.coroutineContext

object VoiceInstaller {
    /** Verification failures; [reason] is a string resource for the user. */
    class VoiceException(val reason: Int, val detail: String = "") : IOException(detail)

    private fun staging(context: Context, id: String) = File(VoiceStore.voicesDir(context), ".download-$id")

    private suspend fun download(file: VoiceFile, dest: File, onBytes: (Long) -> Unit) {
        var last: Exception? = null
        for (url in file.urls) {
            if (!url.startsWith("https://", ignoreCase = true)) continue
            try {
                val c = URL(url).openConnection() as HttpURLConnection
                c.connectTimeout = 20000
                c.readTimeout = 30000
                try {
                    if (c.responseCode != 200) throw IOException("HTTP ${c.responseCode}")
                    c.inputStream.use { input ->
                        dest.outputStream().use { out ->
                            val buf = ByteArray(1 shl 16)
                            while (true) {
                                coroutineContext.ensureActive()
                                val n = input.read(buf)
                                if (n < 0) break
                                out.write(buf, 0, n)
                                onBytes(n.toLong())
                            }
                        }
                    }
                } finally {
                    c.disconnect()
                }
                last = null
                break
            } catch (e: IOException) {
                last = e
            }
        }
        if (last != null || !dest.isFile) {
            throw VoiceException(R.string.error_download, last?.message ?: "")
        }
        if (file.size > 0 && dest.length() != file.size) throw VoiceException(R.string.error_damaged, file.name)
        if (file.sha256.length == 64 && !sha256(dest).equals(file.sha256, ignoreCase = true)) {
            throw VoiceException(R.string.error_damaged, file.name)
        }
    }

    private fun sha256(f: File): String {
        val md = MessageDigest.getInstance("SHA-256")
        f.inputStream().use { input ->
            val buf = ByteArray(1 shl 16)
            while (true) {
                val n = input.read(buf)
                if (n < 0) break
                md.update(buf, 0, n)
            }
        }
        return md.digest().joinToString("") { "%02x".format(it) }
    }

    /** Step 1: the license text (shown before anything else is downloaded). */
    suspend fun fetchLicense(context: Context, voice: VoiceEntry): String = withContext(Dispatchers.IO) {
        val dir = staging(context, voice.id)
        dir.deleteRecursively()
        dir.mkdirs()
        val file = voice.files.firstOrNull { it.name == voice.licenseFile } ?: return@withContext ""
        val dest = File(dir, file.name)
        download(file, dest) {}
        dest.readText()
    }

    /** Step 2: the remaining files, verification, then the voice appears. */
    suspend fun install(context: Context, voice: VoiceEntry, onProgress: (Long, Long) -> Unit) =
        withContext(Dispatchers.IO) {
            val dir = staging(context, voice.id)
            try {
                dir.mkdirs()
                val total = voice.downloadSize
                var done = voice.files.filter { File(dir, it.name).isFile }.sumOf { it.size }
                onProgress(done, total)
                for (file in voice.files) {
                    val dest = File(dir, file.name)
                    if (dest.isFile) continue  // the license, already here
                    download(file, dest) {
                        done += it
                        onProgress(done, total)
                    }
                }
                val database = File(dir, voice.id)
                val magic = ByteArray(6)
                val ok = database.isFile && database.inputStream().use { it.read(magic) } == 6 &&
                    String(magic, Charsets.ISO_8859_1) == "MBROLA"
                if (!ok) throw VoiceException(R.string.error_not_mbrola, voice.id)
                File(dir, "entry.json").writeText(voice.json)

                val target = File(VoiceStore.voicesDir(context), voice.id)
                EngineRegistry.release(voice.id)
                target.deleteRecursively()
                if (!dir.renameTo(target)) throw IOException("cannot move the voice into place")
                VoiceStore.notifyChanged()
            } finally {
                dir.deleteRecursively()
            }
        }

    fun discard(context: Context, voice: VoiceEntry) {
        staging(context, voice.id).deleteRecursively()
    }
}
