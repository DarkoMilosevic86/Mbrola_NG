/*
 * mbrola_inproc - the MBROLA synthesizer inside the process (MBROLA NG, Apple)
 *
 * Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors.
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * See mbrola_inproc.c. All functions may be called from any thread; one
 * channel must not be used by two threads at the same time.
 */
#ifndef MBNG_MBROLA_INPROC_H
#define MBNG_MBROLA_INPROC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mbng_mbrola mbng_mbrola;

/* Opens the voice database. NULL on error, with the message in `error`. */
mbng_mbrola* mbng_mbrola_open(const char* database, int* sample_rate, char* error, int error_size);
void mbng_mbrola_close(mbng_mbrola* m);

/* Queues .pho text (whole lines). Returns 0 if MBROLA's input buffer has no
 * room for it now (read first), otherwise the number of bytes taken. */
int mbng_mbrola_write(mbng_mbrola* m, const char* pho);

/* Up to `max` 16-bit samples of what was written. 0 = everything written
 * so far is synthesized; < 0 = error (message in `error`, channel reset). */
int mbng_mbrola_read(mbng_mbrola* m, short* samples, int max, char* error, int error_size);

/* Forgets what was written and not yet read. */
void mbng_mbrola_reset(mbng_mbrola* m);

#ifdef __cplusplus
}
#endif

#endif
