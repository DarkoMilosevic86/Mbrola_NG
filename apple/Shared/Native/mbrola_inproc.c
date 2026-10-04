/*
 * mbrola_inproc - the MBROLA synthesizer inside the process (MBROLA NG, Apple)
 *
 * iOS does not let an app start another program, so the separate
 * mbrola_ng_synth process of the other platforms (ANALYSIS 4.2, 5.9) cannot
 * exist there. On Apple platforms the unmodified MBROLA engine
 * (external/mbrola, compiled through its own LibMultiChannel/lib2.c) is
 * therefore part of the speech extension itself, behind the same small
 * interface the pipe protocol offers: .pho text in, 16-bit PCM out.
 * The speech extension is its own process, so a damaged voice still cannot
 * take VoiceOver or an app down with it.
 *
 * LICENSE: this file and MBROLA are under the GNU Affero General Public
 * License v3 (synth/LICENSE, external/mbrola/LICENSE). A program that
 * contains them - the Apple app and its speech extension - is distributed
 * under the terms of that license as a whole; the GPL-2.0-or-later parts of
 * MBROLA NG allow this ("or later" -> GPL v3, section 13).
 *
 * Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors (this file).
 * MBROLA: Copyright (c) 1995-2018 Faculte Polytechnique de Mons (TCTS lab).
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The multi-channel library is used (one Mbrola object per channel, no
 * channel state in globals), so several engines can exist in one process.
 * MBROLA's error buffer is still one global: every call into MBROLA is made
 * under one lock.
 */
#include <pthread.h>

/* MBROLA: one translation unit, as its own Makefile builds the library */
#include "../../../external/mbrola/LibMultiChannel/lib2.c"

#include "mbrola_inproc.h"

struct mbng_mbrola {
  Database* dba;
  Fifo* fifo;
  Input* input;
  Parser* parser;
  Mbrola* engine;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* Copies MBROLA's last error message and clears it. Call with the lock held. */
static void take_error(char* error, int error_size) {
  if (error && error_size > 0) {
    strncpy(error, errbuffer, (size_t)error_size - 1);
    error[error_size - 1] = 0;
  }
  lasterr_code = 0;
  errbuffer[0] = 0;
}

static void close_locked(mbng_mbrola* m) {
  if (m->engine) close_Mbrola(m->engine);
  if (m->parser) m->parser->close_Parser(m->parser);
  if (m->input) m->input->close_Input(m->input);
  if (m->fifo) close_Fifo(m->fifo);
  if (m->dba) m->dba->close_Database(m->dba);
  free(m);
}

mbng_mbrola* mbng_mbrola_open(const char* database, int* sample_rate, char* error, int error_size) {
  mbng_mbrola* m;
  char* name;
  if (error && error_size > 0) error[0] = 0;
  if (!database) return NULL;
  m = (mbng_mbrola*)calloc(1, sizeof(mbng_mbrola));
  name = strdup(database);
  if (!m || !name) {
    free(m);
    free(name);
    return NULL;
  }
  pthread_mutex_lock(&g_lock);
  lasterr_code = 0;
  errbuffer[0] = 0;
  /* the same steps as MBROLA's own init_rename_MBR (LibOneChannel) */
  m->dba = init_rename_Database(name, NULL, NULL);
  if (m->dba) {
    float pitch = (float)Freq(m->dba) / (float)MBRPeriod(m->dba);
    m->fifo = init_Fifo(FIFO_SIZE);
    m->input = init_InputFifo(m->fifo);
    m->parser = init_ParserInput(m->input, sil_phon(m->dba), pitch, 1.0f, 1.0f, ";", NULL);
    m->engine = init_Mbrola(m->dba);
  }
  if (!m->dba || !m->fifo || !m->input || !m->parser || !m->engine) {
    take_error(error, error_size);
    close_locked(m);
    m = NULL;
  } else {
    set_parser_Mbrola(m->engine, m->parser);
    set_no_error_Mbrola(m->engine, 1); /* a missing diphone must never stop the speech */
    if (sample_rate) *sample_rate = (int)VoiceFreq(m->engine);
    take_error(NULL, 0);
  }
  pthread_mutex_unlock(&g_lock);
  free(name);
  return m;
}

void mbng_mbrola_close(mbng_mbrola* m) {
  if (!m) return;
  pthread_mutex_lock(&g_lock);
  close_locked(m);
  pthread_mutex_unlock(&g_lock);
}

int mbng_mbrola_write(mbng_mbrola* m, const char* pho) {
  int n;
  if (!m || !pho) return 0;
  pthread_mutex_lock(&g_lock);
  n = write_Fifo(m->fifo, (char*)pho);
  pthread_mutex_unlock(&g_lock);
  return n;
}

static void reset_locked(mbng_mbrola* m) {
  reset_Mbrola(m->engine);
  m->parser->reset_Parser(m->parser);
  reset_Fifo(m->fifo);
}

int mbng_mbrola_read(mbng_mbrola* m, short* samples, int max, char* error, int error_size) {
  int n;
  if (error && error_size > 0) error[0] = 0;
  if (!m || !samples || max <= 0) return 0;
  pthread_mutex_lock(&g_lock);
  n = readtype_Mbrola(m->engine, samples, max, LIN16);
  if (n < 0) {
    take_error(error, error_size);
    reset_locked(m);
  }
  pthread_mutex_unlock(&g_lock);
  return n;
}

void mbng_mbrola_reset(mbng_mbrola* m) {
  if (!m) return;
  pthread_mutex_lock(&g_lock);
  reset_locked(m);
  take_error(NULL, 0);
  pthread_mutex_unlock(&g_lock);
}
