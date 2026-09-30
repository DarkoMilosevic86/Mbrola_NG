// MBROLA NG - voice catalog (catalog/catalog.json, bundled in the app)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

import android.content.Context
import org.json.JSONObject
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

        /** The bundled catalog, limited to languages whose .dat is in the app. */
        fun get(context: Context): Catalog = cached ?: synchronized(this) {
            cached ?: load(context.applicationContext).also { cached = it }
        }

        private fun load(context: Context): Catalog {
            val text = context.assets.open("catalog.json").bufferedReader().use { it.readText() }
            val available = (context.assets.list("languages") ?: emptyArray())
                .filter { it.endsWith(".dat") }.map { it.removeSuffix(".dat") }.toSet()
            val root = JSONObject(text)
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
            if (fa != null) for (i in 0 until fa.length()) {
                val f = fa.getJSONObject(i)
                val urls = ArrayList<String>()
                val ua = f.optJSONArray("urls")
                if (ua != null) for (k in 0 until ua.length()) urls += ua.getString(k)
                files += VoiceFile(f.getString("name"), urls, f.optLong("size"), f.optString("sha256"))
            }
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
                licenseFile = license?.optString("file")?.takeIf { it.isNotEmpty() },
                licenseSummary = stringMap(license?.optJSONObject("summary")),
                androidLocale = v.optString("android_locale").ifEmpty { languageLocale },
                json = v.toString(),
            )
        }

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
