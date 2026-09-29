# MBROLA NG - ctypes binding of the core C API (mbrola_ng.h)
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later

import ctypes
import os
import sys
from ctypes import (
	POINTER, Structure, byref, c_char, c_char_p, c_int, c_int16, c_int32, c_int64,
	c_size_t, c_uint32, c_void_p,
)

API_VERSION = 1

# offsets
OFFSET_UTF16 = 1

# parameters
PARAM_RATE = 1
PARAM_PITCH = 2
PARAM_RANGE = 3
PARAM_VOLUME = 4
PARAM_PUNCTUATION = 5
PARAM_EMOJI = 6
PARAM_CAPITALS = 7
PARAM_DIGITS = 8
PARAM_AUTO_SPELL = 9

# segments
SEG_TEXT = 1
SEG_MARK = 2
SEG_BREAK = 3
SEG_RATE = 4
SEG_PITCH = 5
SEG_RANGE = 6
SEG_SPELL_ON = 7
SEG_SPELL_OFF = 8
SEG_PHONEMES = 9

# events
EVENT_WORD = 1
EVENT_SENTENCE = 2
EVENT_MARK = 3
EVENT_PHONEME = 4
EVENT_END = 5


class _Config(Structure):
	_fields_ = [
		("struct_size", c_uint32),
		("language_path", c_char_p),
		("language_data", c_void_p),
		("language_size", c_size_t),
		("voice_path", c_char_p),
		("synth_path", c_char_p),
		("offset_unit", c_int32),
		("base_pitch", c_int32),
		("phoneme_map", c_char_p),
	]


class _Segment(Structure):
	_fields_ = [
		("type", c_int32),
		("text", c_char_p),
		("text16", c_void_p),
		("length", c_int32),
		("offset_base", c_int32),
		("value", c_int32),
	]


class Event(Structure):
	_fields_ = [
		("type", c_int32),
		("sample", c_int64),
		("text_offset", c_int32),
		("text_length", c_int32),
		("value", c_int32),
		("name", c_char * 64),
	]


class MbngError(RuntimeError):
	pass


def archFolder():
	return "x64" if sys.maxsize > 2 ** 32 else "x86"


class Library:
	"""Loads MBROLA_NG.dll matching the bitness of the running NVDA."""

	def __init__(self, binDir):
		self.binDir = binDir
		self.dll = ctypes.CDLL(os.path.join(binDir, "MBROLA_NG.dll"))
		d = self.dll
		d.mbng_api_version.restype = c_int
		d.mbng_create.argtypes = (POINTER(_Config), POINTER(c_int))
		d.mbng_create.restype = c_void_p
		d.mbng_destroy.argtypes = (c_void_p,)
		d.mbng_destroy.restype = None
		d.mbng_get_audio_format.argtypes = (c_void_p, POINTER(c_int), POINTER(c_int), POINTER(c_int))
		d.mbng_set_param.argtypes = (c_void_p, c_int, c_int)
		d.mbng_begin.argtypes = (c_void_p, POINTER(_Segment), c_int)
		d.mbng_read.argtypes = (c_void_p, POINTER(c_int16), c_int, POINTER(Event), c_int, POINTER(c_int))
		d.mbng_cancel.argtypes = (c_void_p,)
		d.mbng_cancel.restype = None
		d.mbng_last_error.argtypes = (c_void_p,)
		d.mbng_last_error.restype = c_char_p
		if d.mbng_api_version() != API_VERSION:
			raise MbngError("MBROLA_NG.dll has an unsupported API version")

	@property
	def synthPath(self):
		return os.path.join(self.binDir, "mbrola_ng_synth.exe")


class Engine:
	"""One engine instance. Every method except cancel() must be called
	from one thread at a time (the driver uses a lock)."""

	BLOCK = 800  # samples per read (50 ms at 16 kHz)
	MAX_EVENTS = 64

	def __init__(self, lib, languagePath, voicePath, basePitch=0, phonemeMap=""):
		self._lib = lib
		self._d = lib.dll
		cfg = _Config()
		cfg.struct_size = ctypes.sizeof(_Config)
		cfg.language_path = languagePath.encode("utf-8")
		cfg.voice_path = voicePath.encode("utf-8")
		cfg.synth_path = lib.synthPath.encode("utf-8")
		cfg.offset_unit = OFFSET_UTF16
		cfg.base_pitch = int(basePitch or 0)
		cfg.phoneme_map = (phonemeMap or "").encode("utf-8")
		err = c_int(0)
		self._h = self._d.mbng_create(byref(cfg), byref(err))
		if not self._h:
			msg = self._d.mbng_last_error(None) or b""
			raise MbngError("mbng_create failed (%d): %s" % (err.value, msg.decode("utf-8", "replace")))
		rate, bits, ch = c_int(), c_int(), c_int()
		self._d.mbng_get_audio_format(self._h, byref(rate), byref(bits), byref(ch))
		self.sampleRate = rate.value
		self.bitsPerSample = bits.value
		self.channels = ch.value
		self._pcm = (c_int16 * self.BLOCK)()
		self._events = (Event * self.MAX_EVENTS)()

	def setParam(self, param, value):
		if self._h:
			self._d.mbng_set_param(self._h, param, int(value))

	def begin(self, segments):
		"""segments: list of (type, text or None, value)."""
		arr = (_Segment * max(1, len(segments)))()
		keep = []
		for i, (t, text, value) in enumerate(segments):
			arr[i].type = t
			if text is not None:
				b = text.encode("utf-8", "surrogatepass")
				keep.append(b)
				arr[i].text = b
				arr[i].length = len(b)
			else:
				arr[i].length = 0
			arr[i].value = int(value)
		r = self._d.mbng_begin(self._h, arr, len(segments))
		if r < 0:
			raise MbngError(self.lastError())

	def read(self):
		"""Returns (bytes, [(type, samplePos, value)]); bytes is empty at the end."""
		n = c_int(0)
		got = self._d.mbng_read(self._h, self._pcm, self.BLOCK, self._events, self.MAX_EVENTS, byref(n))
		if got < 0:
			raise MbngError(self.lastError())
		events = [(self._events[i].type, self._events[i].sample, self._events[i].value) for i in range(n.value)]
		return ctypes.string_at(self._pcm, got * 2), events

	def cancel(self):
		if self._h:
			self._d.mbng_cancel(self._h)

	def lastError(self):
		msg = self._d.mbng_last_error(self._h) or b""
		return msg.decode("utf-8", "replace")

	def destroy(self):
		if self._h:
			self._d.mbng_destroy(self._h)
			self._h = None
