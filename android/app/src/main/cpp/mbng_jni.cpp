// MBROLA NG - JNI bridge between the Android app and the core C API
// Copyright (c) 2026 Darko Milošević
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Kotlin side: NativeEngine.kt. One engine = one voice (and its own
// mbrola_ng_synth process). Text goes in as UTF-16 (Java strings), so the
// word offsets that come back are UTF-16 offsets, as Android expects.
#include <jni.h>

#include <cstring>
#include <string>

#include "mbrola_ng.h"

namespace {

std::string to_utf8(JNIEnv* env, jstring s) {
  if (!s) return std::string();
  const char* c = env->GetStringUTFChars(s, nullptr);
  std::string out(c ? c : "");
  if (c) env->ReleaseStringUTFChars(s, c);
  return out;
}

mbng_engine* engine(jlong h) { return reinterpret_cast<mbng_engine*>(h); }

jlong create(JNIEnv* env, jclass, jstring language, jstring voice, jstring synth, jint base_pitch,
             jstring phoneme_map) {
  std::string lang = to_utf8(env, language), db = to_utf8(env, voice), exe = to_utf8(env, synth),
              pmap = to_utf8(env, phoneme_map);
  mbng_config c;
  std::memset(&c, 0, sizeof c);
  c.struct_size = sizeof c;
  c.language_path = lang.c_str();
  c.voice_path = db.c_str();
  c.synth_path = exe.c_str();
  c.offset_unit = MBNG_OFFSET_UTF16;
  c.base_pitch = base_pitch;
  c.phoneme_map = pmap.c_str();
  int error = 0;
  return reinterpret_cast<jlong>(mbng_create(&c, &error));
}

void destroy(JNIEnv*, jclass, jlong h) { mbng_destroy(engine(h)); }

jint sample_rate(JNIEnv*, jclass, jlong h) {
  int rate = 16000;
  mbng_get_audio_format(engine(h), &rate, nullptr, nullptr);
  return rate;
}

jint set_param(JNIEnv*, jclass, jlong h, jint param, jint value) {
  return mbng_set_param(engine(h), param, value);
}

// Starts an utterance: one text segment, optionally in character mode.
jint begin(JNIEnv* env, jclass, jlong h, jstring text, jboolean spell) {
  if (!text) return MBNG_ERR_ARGUMENT;
  const jsize len = env->GetStringLength(text);
  const jchar* chars = env->GetStringChars(text, nullptr);
  if (!chars) return MBNG_ERR_MEMORY;
  mbng_segment segs[3];
  std::memset(segs, 0, sizeof segs);
  int n = 0;
  if (spell) segs[n++].type = MBNG_SEG_SPELL_ON;
  segs[n].type = MBNG_SEG_TEXT;
  segs[n].text16 = reinterpret_cast<const uint16_t*>(chars);
  segs[n].length = len;
  ++n;
  if (spell) segs[n++].type = MBNG_SEG_SPELL_OFF;
  int r = mbng_begin(engine(h), segs, n);  // the text is copied by the core
  env->ReleaseStringChars(text, chars);
  return r;
}

// Fills `pcm` (16-bit little-endian samples) and `events`:
// events[0] = count, then {type, sample position, text offset, text length}
// per event. Returns the number of BYTES written, 0 at the end, < 0 on error.
jint read(JNIEnv* env, jclass, jlong h, jbyteArray pcm, jintArray events) {
  const jsize cap_bytes = env->GetArrayLength(pcm);
  const jsize cap_events = (env->GetArrayLength(events) - 1) / 4;
  mbng_event ev[32];
  int n = 0;
  jbyte* buf = env->GetByteArrayElements(pcm, nullptr);
  if (!buf) return MBNG_ERR_MEMORY;
  int got = mbng_read(engine(h), reinterpret_cast<int16_t*>(buf), cap_bytes / 2, ev,
                      cap_events < 32 ? cap_events : 32, &n);
  env->ReleaseByteArrayElements(pcm, buf, got > 0 ? 0 : JNI_ABORT);
  jint out[1 + 32 * 4];
  out[0] = n;
  for (int i = 0; i < n; ++i) {
    out[1 + i * 4] = ev[i].type;
    out[2 + i * 4] = static_cast<jint>(ev[i].sample);
    out[3 + i * 4] = ev[i].text_offset;
    out[4 + i * 4] = ev[i].text_length;
  }
  env->SetIntArrayRegion(events, 0, 1 + n * 4, out);
  return got > 0 ? got * 2 : got;
}

void cancel(JNIEnv*, jclass, jlong h) { mbng_cancel(engine(h)); }

jstring last_error(JNIEnv* env, jclass, jlong h) {
  return env->NewStringUTF(mbng_last_error(engine(h)));
}

const JNINativeMethod kMethods[] = {
    {"nativeCreate", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)J",
     reinterpret_cast<void*>(create)},
    {"nativeDestroy", "(J)V", reinterpret_cast<void*>(destroy)},
    {"nativeSampleRate", "(J)I", reinterpret_cast<void*>(sample_rate)},
    {"nativeSetParam", "(JII)I", reinterpret_cast<void*>(set_param)},
    {"nativeBegin", "(JLjava/lang/String;Z)I", reinterpret_cast<void*>(begin)},
    {"nativeRead", "(J[B[I)I", reinterpret_cast<void*>(read)},
    {"nativeCancel", "(J)V", reinterpret_cast<void*>(cancel)},
    {"nativeLastError", "(J)Ljava/lang/String;", reinterpret_cast<void*>(last_error)},
};

}  // namespace

// The Kotlin class is given at build time, so renaming the app package
// needs no change here.
#ifndef MBNG_JNI_CLASS
#define MBNG_JNI_CLASS "io/github/darkomilosevic86/mbrolang/NativeEngine"
#endif

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
  jclass cls = env->FindClass(MBNG_JNI_CLASS);
  if (!cls) return JNI_ERR;
  if (env->RegisterNatives(cls, kMethods, sizeof kMethods / sizeof kMethods[0]) != JNI_OK) return JNI_ERR;
  return JNI_VERSION_1_6;
}
