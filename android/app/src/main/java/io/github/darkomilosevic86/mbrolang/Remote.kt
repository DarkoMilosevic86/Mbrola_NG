// MBROLA NG - where the catalog and the voice samples are downloaded from
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
package io.github.darkomilosevic86.mbrolang

/**
 * The Resources repository (github.com/Mbrola-NG/Resources) holds the voice
 * catalog and the short samples "Try" plays for a voice that is not
 * installed. Neither is part of the APK beyond one fallback:
 *
 *  - the catalog is fetched when the app starts and kept in device-protected
 *    storage; assets/catalog.json is used until that succeeds, and by the TTS
 *    service before the first unlock, when nothing may be downloaded;
 *  - a sample is fetched when "Try" is pressed and kept in the cache folder.
 *    The address, size and SHA-256 come from the catalog entry of the voice,
 *    so a sample is verified exactly like a voice database.
 *
 * [BRANCH] is a moving branch: a bad commit there would reach every installed
 * app at once. A downloaded catalog therefore replaces the one in use only
 * when it parses, carries [CATALOG_SCHEMA] and still has a language this
 * build can speak; otherwise the last good copy stays.
 */
object Remote {
    const val BRANCH = "main"

    const val BASE = "https://raw.githubusercontent.com/Mbrola-NG/Resources/$BRANCH/"

    const val CATALOG_URL = BASE + "catalog/catalog.json"

    /** The schema of catalog.json this version understands. */
    const val CATALOG_SCHEMA = 1
}
