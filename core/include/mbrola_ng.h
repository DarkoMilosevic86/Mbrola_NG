/*
 * MBROLA NG - core C API (ANALYSIS 5.4)
 * Copyright (c) 2026 Darko Milošević
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Plain C, ABI-stable, used by every host: the SAPI 5 engine and the NVDA
 * add-on (both in MBROLA_NG.dll), the Speech Dispatcher module and the
 * Android JNI bridge.
 *
 * Threading: one thread per engine instance for every call except
 * mbng_cancel(), which may be called from any thread at any time.
 * Strings are UTF-8 unless stated otherwise.
 */
#ifndef MBROLA_NG_H
#define MBROLA_NG_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define MBNG_CALL __cdecl
#else
#define MBNG_CALL
#endif

#if defined(MBNG_BUILD_SHARED)
#if defined(_WIN32)
#define MBNG_API __declspec(dllexport)
#else
#define MBNG_API __attribute__((visibility("default")))
#endif
#else
#define MBNG_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MBNG_API_VERSION 1

typedef struct mbng_engine mbng_engine;

/* ---------------------------------------------------------------- results */
enum {
  MBNG_OK = 0,
  MBNG_ERR_ARGUMENT = -1,  /* invalid argument / buffer too small        */
  MBNG_ERR_LANGUAGE = -2,  /* language file missing, corrupt or too new  */
  MBNG_ERR_VOICE = -3,     /* voice missing or not usable                */
  MBNG_ERR_SYNTH = -4,     /* synthesis process not available            */
  MBNG_ERR_STATE = -5,     /* call not valid in the current state        */
  MBNG_ERR_MEMORY = -6,
  MBNG_ERR_INTERNAL = -7
};

/* Unit of text offsets reported in events. */
enum {
  MBNG_OFFSET_UTF8 = 0,      /* bytes of UTF-8 input                   */
  MBNG_OFFSET_UTF16 = 1,     /* UTF-16 code units (SAPI, Android)       */
  MBNG_OFFSET_CODEPOINT = 2  /* Unicode code points                     */
};

typedef struct mbng_config {
  uint32_t struct_size;        /* sizeof(mbng_config)                            */
  const char* language_path;   /* <code>.dat (developer builds: also a source
                                  folder, see mbng_set_source_loader)             */
  const void* language_data;   /* alternatively: the .dat in memory (Android)    */
  size_t language_size;
  const char* voice_path;      /* MBROLA voice database (e.g. .../voices/cr1/cr1) */
  const char* synth_path;      /* mbrola_ng_synth executable                     */
  int32_t offset_unit;         /* MBNG_OFFSET_*                                  */
  int32_t base_pitch;          /* Hz of the voice; 0 = language default          */
  /* --- added in API 1.1 (read only when struct_size covers it) --- */
  const char* phoneme_map;     /* voice symbols that differ from the language's:
                                  "lang=voice ..." e.g. "A=A: E=e i=i:"; NULL
                                  or "" = the voice uses the language symbols   */
} mbng_config;

/* ------------------------------------------------------------- parameters */
enum {
  MBNG_PARAM_RATE = 1,          /* percent, 100 = normal (20 .. 800)            */
  MBNG_PARAM_PITCH = 2,         /* percent, 100 = normal                        */
  MBNG_PARAM_RANGE = 3,         /* intonation range percent, 0 = monotone       */
  MBNG_PARAM_VOLUME = 4,        /* percent, 100 = normal                        */
  MBNG_PARAM_PUNCTUATION = 5,   /* 0 none, 1 some, 2 most, 3 all                */
  MBNG_PARAM_EMOJI = 6,         /* 0/1 read emoji                               */
  MBNG_PARAM_CAPITALS = 7,      /* 0/1 say the capital prefix when spelling     */
  MBNG_PARAM_DIGITS = 8,        /* 0/1 read all numbers digit by digit          */
  MBNG_PARAM_AUTO_SPELL = 9,    /* 0/1 spell a text that is one character       */
  MBNG_PARAM_PHONEME_EVENTS = 10 /* 0/1 produce MBNG_EVENT_PHONEME              */
};

/* --------------------------------------------------------------- segments */
enum {
  MBNG_SEG_TEXT = 1,       /* text (+ offset_base)                              */
  MBNG_SEG_MARK = 2,       /* index mark: value = index, text = name (optional) */
  MBNG_SEG_BREAK = 3,      /* silence: value = milliseconds                     */
  MBNG_SEG_RATE = 4,       /* value = percent of the current rate parameter     */
  MBNG_SEG_PITCH = 5,      /* value = percent                                   */
  MBNG_SEG_RANGE = 6,      /* value = percent                                   */
  MBNG_SEG_SPELL_ON = 7,   /* character mode on                                 */
  MBNG_SEG_SPELL_OFF = 8,  /* character mode off                                */
  MBNG_SEG_PHONEMES = 9    /* text = SAMPA phonemes separated by spaces         */
};

typedef struct mbng_segment {
  int32_t type;
  const char* text;        /* UTF-8 ...                                         */
  const uint16_t* text16;  /* ... or UTF-16 (if text is NULL)                   */
  int32_t length;          /* in code units; -1 = zero-terminated              */
  int32_t offset_base;     /* added to the offsets reported for this segment    */
  int32_t value;
} mbng_segment;

/* ----------------------------------------------------------------- events */
enum {
  MBNG_EVENT_WORD = 1,
  MBNG_EVENT_SENTENCE = 2,
  MBNG_EVENT_MARK = 3,
  MBNG_EVENT_PHONEME = 4,
  MBNG_EVENT_END = 5       /* end of the utterance                             */
};

typedef struct mbng_event {
  int32_t type;
  int64_t sample;          /* audio position (samples from utterance start)     */
  int32_t text_offset;     /* WORD / SENTENCE: position in the input text       */
  int32_t text_length;
  int32_t value;           /* MARK: index; PHONEME: duration in ms              */
  char name[64];           /* MARK: name; PHONEME: symbol                       */
} mbng_event;

/* -------------------------------------------------------------- functions */
MBNG_API int MBNG_CALL mbng_api_version(void);

MBNG_API mbng_engine* MBNG_CALL mbng_create(const mbng_config* config, int* error);
MBNG_API void MBNG_CALL mbng_destroy(mbng_engine* engine);

MBNG_API int MBNG_CALL mbng_get_audio_format(mbng_engine* engine, int* sample_rate, int* bits, int* channels);
MBNG_API int MBNG_CALL mbng_set_param(mbng_engine* engine, int param, int value);
MBNG_API int MBNG_CALL mbng_get_param(mbng_engine* engine, int param, int* value);

/* Starts a new utterance (cancels the previous one). */
MBNG_API int MBNG_CALL mbng_begin(mbng_engine* engine, const mbng_segment* segments, int count);

/* PULL model: fills `pcm` with up to max_samples 16-bit samples and returns
 * the number written, the events up to the end of that block in `events`
 * (sample positions from the utterance start). Returns 0 when the
 * utterance is finished (the MBNG_EVENT_END event comes with the last
 * calls) or cancelled, < 0 on error. Needs voice_path and synth_path. */
MBNG_API int MBNG_CALL mbng_read(mbng_engine* engine, int16_t* pcm, int max_samples,
                                 mbng_event* events, int max_events, int* n_events);

/* Developer / synthesis-layer access: the next chunk (one sentence) as
 * MBROLA .pho text, zero-terminated, ending with the flush command '#'.
 * Returns the text length, 0 at the end of the utterance, < 0 on error.
 * If `size` is too small, nothing is consumed and the required size
 * (including the terminator) is returned negated minus 1000
 * (i.e. -(1000 + needed)). Event sample positions are relative to the
 * utterance start. */
MBNG_API int MBNG_CALL mbng_read_pho(mbng_engine* engine, char* buf, int size,
                                     mbng_event* events, int max_events, int* n_events);

/* Thread-safe; stops the current utterance immediately. */
MBNG_API void MBNG_CALL mbng_cancel(mbng_engine* engine);

/* Message of the last error of this engine (or of mbng_create when engine
 * is NULL, per thread). Never NULL. */
MBNG_API const char* MBNG_CALL mbng_last_error(mbng_engine* engine);

#ifdef __cplusplus
}
#endif

#endif /* MBROLA_NG_H */
