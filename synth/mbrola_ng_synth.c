/*
 * mbrola_ng_synth - MBROLA speech synthesizer behind a pipe (MBROLA NG)
 *
 * This program is the unmodified MBROLA engine (external/mbrola, compiled
 * through its own LibOneChannel/lib1.c) plus this small pipe loop. It is a
 * SEPARATE program, licensed under the GNU Affero General Public License v3
 * like MBROLA itself (see LICENSE in this folder). MBROLA NG talks to it
 * only through the pipe protocol below, using MBROLA's public .pho format
 * (ANALYSIS 4.2 option A, 5.9).
 *
 * Copyright (c) 2026 Darko Milošević (this file).
 * MBROLA: Copyright (c) 1995-2018 Faculte Polytechnique de Mons (TCTS lab).
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * usage: mbrola_ng_synth <voice database>
 *
 * PROTOCOL (all integers 32-bit little-endian)
 *   startup, stdout:  "MBNG" u32 sample_rate        (or an error frame)
 *   request, stdin:   u32 n, n bytes of .pho text (ending with "#")
 *                     n = 0: reset the synthesizer
 *   response, stdout: frames  u32 n, n bytes of 16-bit mono PCM   (n > 0)
 *                     end     u32 0                (chunk complete)
 *                     error   u32 0xFFFFFFFF, u32 n, n bytes message,
 *                             followed by the end frame
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "common.h"
#include "parser_export.h"
#include "onechannel.h"

static short pcm[4096];

static void put_u32(unsigned int v) {
  unsigned char b[4];
  b[0] = (unsigned char)(v & 0xFF);
  b[1] = (unsigned char)((v >> 8) & 0xFF);
  b[2] = (unsigned char)((v >> 16) & 0xFF);
  b[3] = (unsigned char)((v >> 24) & 0xFF);
  fwrite(b, 1, 4, stdout);
}

static int get_u32(unsigned int* v) {
  unsigned char b[4];
  if (fread(b, 1, 4, stdin) != 4) return 0;
  *v = (unsigned int)b[0] | ((unsigned int)b[1] << 8) | ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24);
  return 1;
}

static void send_error(void) {
  char msg[300];
  unsigned int n;
  msg[0] = 0;
  lastErrorStr_MBR(msg, sizeof(msg));
  n = (unsigned int)strlen(msg);
  put_u32(0xFFFFFFFFu);
  put_u32(n);
  fwrite(msg, 1, n, stdout);
  resetError_MBR();
}

/* Reads all audio MBROLA can produce now; returns 0 on error. */
static int drain(void) {
  for (;;) {
    int n = read_MBR(pcm, (int)(sizeof(pcm) / sizeof(pcm[0])));
    if (n < 0) return 0;
    if (n == 0) return 1;
    put_u32((unsigned int)n * 2u);
    fwrite(pcm, 2, (size_t)n, stdout);
  }
}

/* Feeds the text in pieces that fit MBROLA's input fifo (8 KB). */
static int synthesize(char* text) {
  char* p = text;
  while (*p) {
    char* start = p;
    char saved;
    size_t len = 0;
    /* take whole lines up to ~2000 bytes */
    while (p[len] && len < 2000) {
      char* nl = strchr(p + len, '\n');
      size_t l = nl ? (size_t)(nl - (p + len)) + 1 : strlen(p + len);
      if (len > 0 && len + l > 2000) break;
      len += l;
    }
    p = start + len;
    saved = *p;
    *p = 0;
    if (write_MBR(start) <= 0 && len > 0) {
      *p = saved;
      return 0;
    }
    *p = saved;
    if (!drain()) return 0;
  }
  return drain();
}

int main(int argc, char** argv) {
  char* text = NULL;
  unsigned int cap = 0;
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  if (argc != 2) {
    fprintf(stderr, "usage: mbrola_ng_synth <voice database>\n");
    return 2;
  }
  if (init_MBR(argv[1]) < 0) {
    send_error();
    put_u32(0);
    fflush(stdout);
    return 1;
  }
  setNoError_MBR(1); /* a missing diphone must never stop the speech */
  fwrite("MBNG", 1, 4, stdout);
  put_u32((unsigned int)getFreq_MBR());
  fflush(stdout);

  for (;;) {
    unsigned int n;
    if (!get_u32(&n)) break; /* pipe closed: the engine is gone */
    if (n == 0) {
      reset_MBR();
      put_u32(0);
      fflush(stdout);
      continue;
    }
    if (n > 16u * 1024u * 1024u) break;
    if (n + 1 > cap) {
      char* t = (char*)realloc(text, n + 1);
      if (!t) break;
      text = t;
      cap = n + 1;
    }
    if (fread(text, 1, n, stdin) != n) break;
    text[n] = 0;
    if (!synthesize(text)) {
      send_error();
      reset_MBR();
    }
    put_u32(0);
    fflush(stdout);
  }
  free(text);
  close_MBR();
  return 0;
}
