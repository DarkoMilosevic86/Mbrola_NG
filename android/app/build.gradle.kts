// MBROLA NG - Android app (TextToSpeech engine + voice manager)
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// Release signing: android/keystore.properties (never committed), made by
// android/create_keystore.py. Without it, release builds are left unsigned.
val keystoreFile = rootProject.file("keystore.properties")
val keystore = Properties().apply {
    if (keystoreFile.isFile) keystoreFile.reader(Charsets.UTF_8).use { load(it) }
}

// Language data (.dat), the voice catalog and the voice samples are put here
// by `python build.py android` (languages are compiled by the desktop build).
val generatedAssets = rootProject.file("generated/assets")

android {
    namespace = "io.github.darkomilosevic86.mbrolang"
    compileSdk = 36
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "io.github.darkomilosevic86.mbrolang"
        minSdk = 29
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static", "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON")
                targets += listOf("mbrola_ng_jni", "mbrola_ng_synth")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets["main"].assets.srcDir(generatedAssets)

    signingConfigs {
        if (keystoreFile.isFile) {
            create("release") {
                storeFile = rootProject.file(keystore.getProperty("storeFile"))
                storePassword = keystore.getProperty("storePassword")
                keyAlias = keystore.getProperty("keyAlias")
                keyPassword = keystore.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.findByName("release")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
    packaging {
        // mbrola_ng_synth is an executable: it must exist as a file in the
        // native library folder, so native libraries are extracted on install.
        jniLibs.useLegacyPackaging = true
        resources.excludes += "/META-INF/{AL2.0,LGPL2.1}"
    }
    androidResources {
        // .dat files are memory-friendly when stored uncompressed
        noCompress += listOf("dat", "wav")
        generateLocaleConfig = true
    }
    lint {
        checkReleaseBuilds = false
    }
}

// A clear message instead of an app that starts without languages.
tasks.register("checkGeneratedAssets") {
    doLast {
        if (!File(generatedAssets, "catalog.json").isFile ||
            File(generatedAssets, "languages").listFiles()?.any { it.name.endsWith(".dat") } != true
        ) {
            throw GradleException(
                "Language data is missing in ${generatedAssets}. Run `python build.py android` " +
                    "(or `python build.py android --prepare` once, then build in Android Studio)."
            )
        }
    }
}
tasks.named("preBuild") { dependsOn("checkGeneratedAssets") }

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2025.05.00")
    implementation(composeBom)
    implementation("androidx.core:core-ktx:1.16.0")
    implementation("androidx.activity:activity-compose:1.10.1")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.9.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.9.0")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-core")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")
    debugImplementation("androidx.compose.ui:ui-tooling")
}
