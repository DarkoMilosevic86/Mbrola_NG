# MBROLA NG - native NVDA synthesizer driver (ANALYSIS section 12)
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Talks to MBROLA_NG.dll directly (C API, ctypes); the DLL runs the MBROLA
# synthesizer as a separate process (mbrola_ng_synth.exe).

import os
import queue
import threading
from collections import OrderedDict

import addonHandler
import config
import globalVars
import nvwave
import synthDriverHandler
from autoSettingsUtils.driverSetting import BooleanDriverSetting
from logHandler import log
from speech.commands import (
	BreakCommand,
	CharacterModeCommand,
	IndexCommand,
	PitchCommand,
	RateCommand,
	VolumeCommand,
)
from synthDriverHandler import VoiceInfo, synthDoneSpeaking, synthIndexReached

from . import _mbng

addonHandler.initTranslation()

_HERE = os.path.dirname(os.path.abspath(__file__))
_LANG_DIR = os.path.join(_HERE, "languages")

def _uiLanguage():
	try:
		import languageHandler
		return languageHandler.getLanguage()
	except Exception:
		return "en"


def _installedVoices():
	"""OrderedDict id -> (database path, name, language, base pitch,
	phoneme map) of the usable installed voices (ANALYSIS 10.3, 10.6, 10.9)."""
	from . import _voices
	cat, _source = _voices.loadCatalog(online=False)
	found = OrderedDict()
	for v in _voices.installedVoices(cat):
		name = _voices.localized(_voices.voiceNames(v), _uiLanguage())
		found[v["id"]] = (v["db"], name, v["language"], _voices.basePitch(v), _voices.phonemeMap(v))
	return found


def _ratePercent(rate, boost):
	# NVDA 0..100 (50 = normal) -> 50 % .. 400 %; rate boost doubles it
	if rate < 50:
		p = 100.0 * 2 ** ((rate - 50) / 50.0)
	else:
		p = 100.0 * 4 ** ((rate - 50) / 50.0)
	if boost:
		p *= 2
	return max(20, min(800, int(round(p))))


def _pitchPercent(pitch):
	# NVDA 0..100 (50 = normal) -> 50 % .. 200 %
	return max(25, min(400, int(round(100.0 * 2 ** ((pitch - 50) / 50.0)))))


class SynthDriver(synthDriverHandler.SynthDriver):
	name = "mbrola_ng"
	# Translators: name of the synthesizer in NVDA's synthesizer list
	description = "MBROLA NG"

	supportedSettings = (
		synthDriverHandler.SynthDriver.VoiceSetting(),
		synthDriverHandler.SynthDriver.RateSetting(),
		synthDriverHandler.SynthDriver.RateBoostSetting(),
		synthDriverHandler.SynthDriver.PitchSetting(),
		synthDriverHandler.SynthDriver.InflectionSetting(),
		synthDriverHandler.SynthDriver.VolumeSetting(),
		BooleanDriverSetting(
			"readEmoji",
			# Translators: a checkbox in the voice settings
			_("Read &emoji"),
			defaultVal=True,
		),
		BooleanDriverSetting(
			"digitByDigit",
			# Translators: a checkbox in the voice settings
			_("Read numbers &digit by digit"),
			defaultVal=False,
		),
	)
	supportedCommands = {
		IndexCommand,
		CharacterModeCommand,
		BreakCommand,
		PitchCommand,
		RateCommand,
		VolumeCommand,
	}
	supportedNotifications = {synthIndexReached, synthDoneSpeaking}

	@classmethod
	def check(cls):
		# Without an installed voice NVDA must never switch to a silent synth (10.8).
		try:
			binDir = os.path.join(_HERE, _mbng.archFolder())
			return bool(_installedVoices()) and os.path.isfile(os.path.join(binDir, "MBROLA_NG.dll"))
		except Exception:
			return False

	def __init__(self):
		super().__init__()
		self._voices = _installedVoices()
		if not self._voices:
			raise RuntimeError("MBROLA NG: no voice installed")
		self._lib = _mbng.Library(os.path.join(_HERE, _mbng.archFolder()))
		self._lock = threading.Lock()  # every engine call except cancel()
		self._engine = None
		self._voice = None
		self._rate = 50
		self._rateBoost = False
		self._pitch = 50
		self._inflection = 50
		self._volume = 100
		self._readEmoji = True
		self._digitByDigit = False
		self._player = None
		self._generation = 0
		self._queue = queue.Queue()
		self._loadVoice(next(iter(self._voices)))
		self._thread = threading.Thread(target=self._worker, name="MBROLA NG speech", daemon=True)
		self._thread.start()

	# ---------------------------------------------------------------- engine
	def _loadVoice(self, vid):
		db, _name, lang, pitch, pmap = self._voices[vid]
		engine = _mbng.Engine(self._lib, os.path.join(_LANG_DIR, lang + ".dat"), db, pitch, pmap)
		with self._lock:
			old = self._engine
			self._engine = engine
			self._voice = vid
			self._applyParams()
		if old:
			old.cancel()
			with self._lock:
				old.destroy()
		self._makePlayer()

	def _makePlayer(self):
		if self._player:
			self._player.close()
		e = self._engine
		try:
			self._player = nvwave.WavePlayer(
				channels=e.channels,
				samplesPerSec=e.sampleRate,
				bitsPerSample=e.bitsPerSample,
				outputDevice=config.conf["audio"]["outputDevice"],
			)
		except (KeyError, TypeError):
			self._player = nvwave.WavePlayer(
				channels=e.channels,
				samplesPerSec=e.sampleRate,
				bitsPerSample=e.bitsPerSample,
			)

	def _applyParams(self):
		e = self._engine
		if not e:
			return
		e.setParam(_mbng.PARAM_RATE, _ratePercent(self._rate, self._rateBoost))
		e.setParam(_mbng.PARAM_PITCH, _pitchPercent(self._pitch))
		e.setParam(_mbng.PARAM_RANGE, self._inflection * 2)
		e.setParam(_mbng.PARAM_VOLUME, self._volume)
		# NVDA does its own symbol processing and capital announcements (P2)
		e.setParam(_mbng.PARAM_PUNCTUATION, 0)
		e.setParam(_mbng.PARAM_CAPITALS, 0)
		e.setParam(_mbng.PARAM_EMOJI, 1 if self._readEmoji else 0)
		e.setParam(_mbng.PARAM_DIGITS, 1 if self._digitByDigit else 0)

	# ---------------------------------------------------------------- speech
	def speak(self, speechSequence):
		segments = []
		basePitch = _pitchPercent(self._pitch)
		baseRate = _ratePercent(self._rate, self._rateBoost)
		for item in speechSequence:
			if isinstance(item, str):
				if item:
					segments.append((_mbng.SEG_TEXT, item, 0))
			elif isinstance(item, IndexCommand):
				segments.append((_mbng.SEG_MARK, None, item.index))
			elif isinstance(item, CharacterModeCommand):
				segments.append((_mbng.SEG_SPELL_ON if item.state else _mbng.SEG_SPELL_OFF, None, 0))
			elif isinstance(item, BreakCommand):
				segments.append((_mbng.SEG_BREAK, None, item.time))
			elif isinstance(item, PitchCommand):
				segments.append((_mbng.SEG_PITCH, None, _pitchPercent(item.newValue) * 100 // basePitch))
			elif isinstance(item, RateCommand):
				segments.append((_mbng.SEG_RATE, None, _ratePercent(item.newValue, self._rateBoost) * 100 // baseRate))
			# VolumeCommand and other commands: ignored for now
		self._queue.put((self._generation, segments))

	def _worker(self):
		while True:
			item = self._queue.get()
			if item is None:
				break
			gen, segments = item
			if gen != self._generation:
				continue
			try:
				self._speakOne(gen, segments)
			except Exception:
				log.error("MBROLA NG: speech failed", exc_info=True)
			if gen == self._generation and self._queue.empty():
				synthDoneSpeaking.notify(synth=self)

	def _speakOne(self, gen, segments):
		with self._lock:
			if not self._engine:  # voice suspended by the Voice Manager
				return
			self._engine.begin(segments)
		player = self._player
		fedSamples = 0          # samples handed to the player so far
		pending = b""           # last piece, fed later so marks can attach to it
		pendingMarks = []

		def feedPending():
			nonlocal pending, pendingMarks
			if not pending:
				for m in pendingMarks:  # nothing played yet: the mark is reached now
					synthIndexReached.notify(synth=self, index=m)
				pendingMarks = []
				return
			marks = pendingMarks
			onDone = None
			if marks:
				def onDone(marks=marks):
					for m in marks:
						synthIndexReached.notify(synth=self, index=m)
			player.feed(pending, onDone=onDone)
			pending = b""
			pendingMarks = []

		while gen == self._generation:
			with self._lock:
				if not self._engine:
					return
				data, events = self._engine.read()
			if gen != self._generation:
				return
			pos = 0  # byte position inside `data`
			for etype, sample, value in events:
				if etype != _mbng.EVENT_MARK:
					continue
				cut = max(0, min(len(data), (sample - fedSamples) * 2))
				if cut > pos:
					feedPending()
					pending = data[pos:cut]
					pos = cut
				pendingMarks.append(value)
			if pos < len(data):
				feedPending()
				pending = data[pos:]
			fedSamples += len(data) // 2
			if not data and not events:
				break
		if gen != self._generation:
			return
		feedPending()
		player.idle()

	def cancel(self):
		self._generation += 1
		try:
			while True:
				self._queue.get_nowait()
		except queue.Empty:
			pass
		if self._engine:
			self._engine.cancel()
		if self._player:
			self._player.stop()

	def pause(self, switch):
		if self._player:
			self._player.pause(switch)

	def terminate(self):
		self.cancel()
		self._queue.put(None)
		self._thread.join(2)
		with self._lock:
			if self._engine:
				self._engine.destroy()
				self._engine = None
		if self._player:
			self._player.close()
			self._player = None
		super().terminate()

	# -------------------------------------------------------------- settings
	# ------------------------------------------------ used by the Voice Manager
	def suspendVoice(self):
		"""Releases the voice database (ends the synthesis process) so the
		Voice Manager can replace or remove its files."""
		self.cancel()
		with self._lock:
			if self._engine:
				self._engine.destroy()
				self._engine = None

	def resumeVoice(self):
		"""Rescans the installed voices and loads the current (or first)
		voice. Returns False when no voice is left."""
		self._voices = _installedVoices()
		self.__dict__.pop("_availableVoices", None)  # AutoPropertyObject cache
		if not self._voices:
			return False
		vid = self._voice if self._voice in self._voices else next(iter(self._voices))
		self._loadVoice(vid)
		return True

	def _get_availableVoices(self):
		return OrderedDict(
			(vid, VoiceInfo(vid, name, lang)) for vid, (_db, name, lang, _p, _m) in self._voices.items()
		)

	def _get_voice(self):
		return self._voice

	def _set_voice(self, vid):
		if vid in self._voices and vid != self._voice:
			self.cancel()
			self._loadVoice(vid)

	def _setParam(self, attr, value):
		setattr(self, attr, value)
		with self._lock:
			self._applyParams()

	def _get_rate(self):
		return self._rate

	def _set_rate(self, value):
		self._setParam("_rate", value)

	def _get_rateBoost(self):
		return self._rateBoost

	def _set_rateBoost(self, value):
		self._setParam("_rateBoost", bool(value))

	def _get_pitch(self):
		return self._pitch

	def _set_pitch(self, value):
		self._setParam("_pitch", value)

	def _get_inflection(self):
		return self._inflection

	def _set_inflection(self, value):
		self._setParam("_inflection", value)

	def _get_volume(self):
		return self._volume

	def _set_volume(self, value):
		self._setParam("_volume", value)

	def _get_readEmoji(self):
		return self._readEmoji

	def _set_readEmoji(self, value):
		self._setParam("_readEmoji", bool(value))

	def _get_digitByDigit(self):
		return self._digitByDigit

	def _set_digitByDigit(self, value):
		self._setParam("_digitByDigit", bool(value))
