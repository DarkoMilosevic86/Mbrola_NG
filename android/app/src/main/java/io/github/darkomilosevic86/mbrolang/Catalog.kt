// MBROLA NG - voice catalog (Resources/catalog/catalog.json: downloaded by
// Catalog.refresh, with the copy in the assets as the fallback - see Remote)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

import android.content.Context
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale

data class VoiceFile(val name: String, val urls: List<String>, val size: Long, val sha256: String)

data class VoiceEntry(
    val id: String,
    val language: String,               // language code of the .dat: "hr"
    val names: Map<String, String>,     // UI language -> display name
    val gender: String,
    val version: String,
    val basePitch: Int,
    val phonemeMap: String,             // "A=A: E=e ..." (the form the core takes)
    val files: List<VoiceFile>,
    /** The recorded "Try" sample, downloaded on demand; not part of the voice. */
    val sample: VoiceFile?,
    val licenseFile: String?,
    val licenseSummary: Map<String, String>,
    val androidLocale: String,          // "hrv-HRV"
    /** Other locales this voice also serves ("srp-SRB": the Croatian data reads Serbian too). */
    val alsoFor: List<String> = emptyList(),
    val json: String,                   // the catalog entry itself (kept with an installed voice)
) {
    val downloadSize: Long get() = files.sumOf { it.size }

    /** ISO 639-3 language and ISO 3166 alpha-3 country, as the TTS framework uses them. */
    val iso3Language: String get() = androidLocale.substringBefore('-').lowercase(Locale.ROOT)
    val iso3Country: String get() = androidLocale.substringAfter('-', "").uppercase(Locale.ROOT)

    /** Every locale ("hrv-HRV", "srp-SRB", ...) the voice is offered for. */
    val allLocales: List<String> get() = listOf(androidLocale) + alsoFor

    /** True if the voice speaks the ISO 639-3 language [iso3]. */
    fun speaks(iso3: String): Boolean =
        allLocales.any { it.substringBefore('-').equals(iso3, ignoreCase = true) }

    /** java.util.Locale of the voice (two-letter codes where they exist). */
    val locale: Locale by lazy {
        val country = Locale.getISOCountries().firstOrNull {
            runCatching { Locale("", it).isO3Country }.getOrNull().equals(iso3Country, ignoreCase = true)
        } ?: ""
        Locale(language, country)
    }
}

data class LanguageEntry(
    val code: String,
    val names: Map<String, String>,
    val androidLocale: String,
    val voices: List<VoiceEntry>,
)

class Catalog(val languages: List<LanguageEntry>) {
    val voices: List<VoiceEntry> get() = languages.flatMap { it.voices }
    fun voice(id: String): VoiceEntry? = voices.firstOrNull { it.id == id }
    fun language(code: String): LanguageEntry? = languages.firstOrNull { it.code == code }

    companion object {
        @Volatile private var cached: Catalog? = null

        /**
         * The catalog in use, limited to languages whose .dat is in the app:
         * the downloaded one ([refresh]) when there is one, else the copy in
         * the assets. Never touches the network - the TTS service calls this
         * before the first unlock.
         */
        fun get(context: Context): Catalog = cached ?: synchronized(this) {
            cached ?: load(context.applicationContext).also { cached = it }
        }

        /**
         * Downloads the catalog and takes it into use. Returns true when the
         * catalog in use changed. A catalog that does not parse, has another
         * schema or no language this build can speak is ignored, and the one
         * in use stays - as it does when there is no network.
         */
        suspend fun refresh(context: Context): Boolean = withContext(Dispatchers.IO) {
            val app = context.applicationContext
            try {
                val connection = URL(Remote.CATALOG_URL).openConnection() as HttpURLConnection
                connection.connectTimeout = 15000
                connection.readTimeout = 20000
                val text = try {
                    if (connection.responseCode != 200) throw java.io.IOException("HTTP ${connection.responseCode}")
                    connection.inputStream.bufferedReader().use { it.readText() }
                } finally {
                    connection.disconnect()
                }
                val fresh = parse(text, available(app))
                if (fresh.languages.isEmpty()) throw java.io.IOException("no usable language in the catalog")
                val file = cacheFile(app)
                val tmp = File(file.parentFile, "catalog.json.new")
                file.parentFile?.mkdirs()
                tmp.writeText(text)
                if (!tmp.renameTo(file)) {
                    tmp.delete()
                    throw java.io.IOException("cannot write $file")
                }
                cached = fresh
                true
            } catch (e: Exception) {
                // offline, or a catalog this version will not take: keep the one in use
                Log.i(TAG, "catalog not refreshed: ${e.message}")
                false
            }
        }

        /** The downloaded catalog; in device-protected storage, like the voices. */
        private fun cacheFile(context: Context): File =
            File(VoiceStore.storage(context).filesDir, "catalog.json")

        /** Languages this build can speak: those whose compiled .dat is in the assets. */
        private fun available(context: Context): Set<String> =
            (context.assets.list("languages") ?: emptyArray())
                .filter { it.endsWith(".dat") }.map { it.removeSuffix(".dat") }.toSet()

        private fun load(context: Context): Catalog {
            val codes = available(context)
            val downloaded = runCatching { cacheFile(context).takeIf { it.isFile }?.readText() }.getOrNull()
            if (downloaded != null) {
                val catalog = runCatching { parse(downloaded, codes) }.getOrNull()
                if (catalog != null && catalog.languages.isNotEmpty()) return catalog
                Log.w(TAG, "the downloaded catalog is unusable: falling back to the bundled one")
            }
            val bundled = runCatching {
                context.assets.open("catalog.json").bufferedReader().use { it.readText() }
            }.getOrNull() ?: return Catalog(emptyList())
            return runCatching { parse(bundled, codes) }.getOrNull() ?: Catalog(emptyList())
        }

        /** Parses catalog.json; throws when it is not a catalog this version understands. */
        private fun parse(text: String, available: Set<String>): Catalog {
            val root = JSONObject(text)
            require(root.optInt("schema") == Remote.CATALOG_SCHEMA) {
                "catalog schema ${root.optInt("schema")}, expected ${Remote.CATALOG_SCHEMA}"
            }
            val voiceArray = root.getJSONArray("voices")
            val languageArray = root.getJSONArray("languages")
            val languages = ArrayList<LanguageEntry>()
            for (i in 0 until languageArray.length()) {
                val l = languageArray.getJSONObject(i)
                val code = l.getString("code")
                if (code !in available) continue
                val locale = l.optString("android_locale", code)
                val alsoFor = ArrayList<String>()
                val aa = l.optJSONArray("android_also_for")
                if (aa != null) for (k in 0 until aa.length()) alsoFor += aa.getString(k)
                val voices = ArrayList<VoiceEntry>()
                for (k in 0 until voiceArray.length()) {
                    val v = voiceArray.getJSONObject(k)
                    if (v.optString("language") == code) voices += parseVoice(v, locale).copy(alsoFor = alsoFor)
                }
                languages += LanguageEntry(code, stringMap(l.optJSONObject("names")), locale, voices)
            }
            return Catalog(languages)
        }

        private fun parseFile(f: JSONObject): VoiceFile {
            val urls = ArrayList<String>()
            val ua = f.optJSONArray("urls")
            if (ua != null) for (k in 0 until ua.length()) urls += ua.getString(k)
            return VoiceFile(f.getString("name"), urls, f.optLong("size"), f.optString("sha256"))
        }

        fun parseVoice(v: JSONObject, languageLocale: String): VoiceEntry {
            val cfg = v.optJSONObject("voice_config") ?: JSONObject()
            val map = cfg.optJSONObject("phoneme_map")
            val pairs = ArrayList<String>()
            if (map != null) for (k in map.keys()) {
                val x = map.optString(k)
                if (k.isNotEmpty() && x.isNotEmpty() && ' ' !in k + x) pairs += "$k=$x"
            }
            val files = ArrayList<VoiceFile>()
            val fa = v.optJSONArray("files")
            if (fa != null) for (i in 0 until fa.length()) files += parseFile(fa.getJSONObject(i))
            val license = v.optJSONObject("license")
            return VoiceEntry(
                id = v.getString("id"),
                language = v.optString("language"),
                names = stringMap(v.optJSONObject("names")),
                gender = v.optString("gender"),
                version = v.optString("version"),
                basePitch = cfg.optDouble("base_pitch", 0.0).toInt(),
                phonemeMap = pairs.joinToString(" "),
                files = files,
                sample = v.optJSONObject("sample")?.let { parseFile(it) },
                licenseFile = license?.optString("file")?.takeIf { it.isNotEmpty() },
                licenseSummary = stringMap(license?.optJSONObject("summary")),
                androidLocale = v.optString("android_locale").ifEmpty { languageLocale },
                json = v.toString(),
            )
        }

        private const val TAG = "MbrolaNG"

        private fun stringMap(o: JSONObject?): Map<String, String> {
            val m = LinkedHashMap<String, String>()
            if (o != null) for (k in o.keys()) m[k] = o.optString(k)
            return m
        }
    }
}

/** Picks the text for the app's UI language from {"en": ..., "hr": ...}. */
fun Map<String, String>.localized(context: Context): String {
    val ui = context.resources.configuration.locales[0].language
    return this[ui] ?: this["en"] ?: values.firstOrNull() ?: ""
}
