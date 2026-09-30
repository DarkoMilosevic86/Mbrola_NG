// MBROLA NG - installed voices and language data on the device
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import org.json.JSONObject
import java.io.File
import java.io.IOException

/**
 * Voices live in filesDir/voices/<id>/<id> (the MBROLA database, a normal
 * file that mbrola_ng_synth opens by path) next to entry.json, the catalog
 * entry the voice was installed from (ANALYSIS 10.9).
 *
 * DIRECT BOOT: everything the engine needs to speak - voices, language data,
 * voice settings - is kept in DEVICE-PROTECTED storage ([storage]), which is
 * readable right after the phone starts, before the first unlock. TalkBack
 * must be able to read the lock screen (and the PIN prompt) after a reboot;
 * the default, credential-protected storage is still locked at that point.
 */
object VoiceStore {
    private val tick = MutableStateFlow(0)

    /** Changes whenever a voice is installed or removed (the UI observes it). */
    val changes: StateFlow<Int> get() = tick

    /** The context whose files and preferences are available before the first unlock. */
    fun storage(context: Context): Context = context.createDeviceProtectedStorageContext()

    fun voicesDir(context: Context): File = File(storage(context).filesDir, "voices")

    fun database(context: Context, id: String): File = File(File(voicesDir(context), id), id)

    fun isInstalled(context: Context, id: String): Boolean = database(context, id).isFile

    fun installed(context: Context): List<VoiceEntry> {
        val catalog = Catalog.get(context)
        val supported = catalog.languages.associateBy { it.code }
        val dirs = voicesDir(context).listFiles()?.filter { it.isDirectory }?.sortedBy { it.name } ?: emptyList()
        return dirs.mapNotNull { dir ->
            val id = dir.name
            if (id.startsWith(".") || !File(dir, id).isFile) return@mapNotNull null
            val stored = runCatching {
                val o = JSONObject(File(dir, "entry.json").readText())
                Catalog.parseVoice(o, supported[o.optString("language")]?.androidLocale ?: "")
            }.getOrNull()
            // the current catalog wins (newer voice settings), the stored entry is the fallback
            (catalog.voice(id) ?: stored?.copy(
                alsoFor = catalog.language(stored.language)?.voices?.firstOrNull()?.alsoFor ?: emptyList()
            ))?.takeIf { it.language in supported }
        }
    }

    fun remove(context: Context, id: String) {
        EngineRegistry.release(id)
        if (EngineSettings.forcedVoice(context) == id) EngineSettings.setForcedVoice(context, null)
        File(voicesDir(context), id).deleteRecursively()
        VoiceSettings.clear(context, id)
        notifyChanged()
    }

    fun notifyChanged() {
        tick.value = tick.value + 1
    }

    // ------------------------------------------------------------ languages
    /** <code>.dat as a file: copied from the APK assets once per app version. */
    fun languageFile(context: Context, code: String): File {
        val dir = File(storage(context).filesDir, "languages")
        val file = File(dir, "$code.dat")
        val stamp = File(dir, "$code.stamp")
        val info = context.packageManager.getPackageInfo(context.packageName, 0)
        val want = "${info.longVersionCode}:${info.lastUpdateTime}"
        synchronized(this) {
            if (!file.isFile || runCatching { stamp.readText() }.getOrNull() != want) {
                dir.mkdirs()
                val tmp = File(dir, "$code.tmp")
                context.assets.open("languages/$code.dat").use { input ->
                    tmp.outputStream().use { input.copyTo(it) }
                }
                if (!tmp.renameTo(file)) throw IOException("cannot write $file")
                stamp.writeText(want)
            }
        }
        return file
    }

    /** The mbrola_ng_synth executable (packaged as a native library file). */
    fun synthPath(context: Context): String =
        File(context.applicationInfo.nativeLibraryDir, "libmbrola_ng_synth.so").absolutePath
}
