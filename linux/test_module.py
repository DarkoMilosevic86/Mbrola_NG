#!/usr/bin/env python3
# MBROLA NG - tests sd_mbrola_ng without Speech Dispatcher: plays the server's
# side of the output module protocol and writes the received audio to WAV.
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   python3 linux/test_module.py [--module PATH] [--config FILE]
#                                [--language hr] [--voice cr1] [--wav out.wav]
#                                ["text to speak"]
# Default module: ~/.local/libexec/speech-dispatcher-modules/sd_mbrola_ng,
# then the system module folders.

import argparse
import os
import subprocess
import sys
import time
import wave

MODULE_DIRS = [
	os.path.expanduser("~/.local/libexec/speech-dispatcher-modules"),
	"/usr/lib/x86_64-linux-gnu/speech-dispatcher-modules",
	"/usr/lib/aarch64-linux-gnu/speech-dispatcher-modules",
	"/usr/libexec/speech-dispatcher-modules",
	"/usr/lib/speech-dispatcher-modules",
	"/usr/lib64/speech-dispatcher-modules",
]


class Failure(Exception):
	pass


class Module:
	def __init__(self, path, config):
		self.p = subprocess.Popen([path, config or "/nonexistent/mbrola_ng.conf"],
			stdin=subprocess.PIPE, stdout=subprocess.PIPE)

	def send(self, text):
		self.p.stdin.write(text.encode("utf-8"))
		self.p.stdin.flush()

	def line(self):
		b = self.p.stdout.readline()
		if not b:
			raise Failure("the module closed its output (crashed?)")
		return b

	def reply(self, expect):
		"""Reads a (multi-line) reply; its last line must start with `expect`."""
		lines = []
		while True:
			l = self.line().decode("utf-8", "replace").rstrip("\n")
			lines.append(l)
			if len(l) >= 4 and l[3] == " ":
				break
		if not lines[-1].startswith(expect):
			raise Failure("expected %s, got: %s" % (expect, " | ".join(lines)))
		return lines

	def params(self, cmd, ack, values):
		self.send(cmd + "\n")
		self.reply(ack)
		self.send("".join("%s=%s\n" % kv for kv in values.items()) + ".\n")

	def speak(self, cmd, text, stop_after=None):
		"""Returns (pcm bytes, sample rate, [events])."""
		self.send(cmd + "\n")
		self.reply("202")
		body = "".join(("." + l if l.startswith(".") else l) + "\n" for l in text.split("\n"))
		self.send(body + ".\n")
		self.reply("200")
		pcm, rate, events, blocks = bytearray(), 0, [], 0
		while True:
			l = self.line()
			if l.startswith(b"705-"):
				params = {}
				while not l.startswith(b"705-AUDIO\0"):
					k, v = l[4:].decode().strip().split("=")
					params[k] = int(v)
					l = self.line()
				data = l[len(b"705-AUDIO\0"):-1]
				out, i = bytearray(), 0
				while i < len(data):
					if data[i] == 0x7D:
						out.append(data[i + 1] ^ 0x20)
						i += 2
					else:
						out.append(data[i])
						i += 1
				if len(out) != params["num_samples"] * 2:
					raise Failure("audio block: %d bytes, expected %d" % (len(out), params["num_samples"] * 2))
				if self.line() != b"705 AUDIO\n":
					raise Failure("audio block not terminated")
				pcm += out
				rate = params["sample_rate"]
				blocks += 1
				if stop_after is not None and blocks == stop_after:
					self.send("STOP\n")
				continue
			s = l.decode("utf-8", "replace").rstrip("\n")
			if s.startswith("700-"):
				events.append(("mark", s[4:], len(pcm) // 2))
				self.reply("700")
			elif s.startswith("706-"):
				events.append(("icon", s[4:], len(pcm) // 2))
				self.reply("706")
			elif s.startswith("701"):
				events.append(("begin", "", 0))
			elif s[:3] in ("702", "703", "704"):
				events.append(({"702": "end", "703": "stop", "704": "pause"}[s[:3]], "", len(pcm) // 2))
				return bytes(pcm), rate, events
			else:
				raise Failure("unexpected event: " + s)


def findModule():
	for d in MODULE_DIRS:
		p = os.path.join(d, "sd_mbrola_ng")
		if os.access(p, os.X_OK):
			return p
	raise SystemExit("sd_mbrola_ng not found; use --module PATH")


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--module")
	ap.add_argument("--config")
	ap.add_argument("--language", default="hr")
	ap.add_argument("--voice", default="NULL")
	ap.add_argument("--wav", default="mbrola_ng_test.wav")
	ap.add_argument("text", nargs="?",
		default="Dobar dan. Ovo je MBROLA NG na Linuxu, 29. 9. 2026. u 15:30 sati! Kako ste?")
	a = ap.parse_args()
	m = Module(a.module or findModule(), a.config)
	ok = True

	def step(name, fn):
		nonlocal ok
		t = time.time()
		try:
			r = fn()
			print("ok   %-28s %6.0f ms" % (name, (time.time() - t) * 1000))
			return r
		except Failure as e:
			ok = False
			print("FAIL %-28s %s" % (name, e))
			return None

	m.send("INIT\n")
	init = step("INIT", lambda: m.reply("299"))
	if init is None:
		return 1
	print("     " + " / ".join(init[:-1]))

	def audio():
		m.params("AUDIO", "207", {"audio_output_method": "server"})
		m.reply("203")
	step("AUDIO (server)", audio)

	def settings(**over):
		v = {"pitch": 0, "pitch_range": 0, "rate": 0, "volume": 100, "punctuation_mode": "none",
			"spelling_mode": "off", "cap_let_recogn": "none", "voice": "male1",
			"language": a.language, "synthesis_voice": a.voice}
		v.update(over)
		m.params("SET", "203", v)
		m.reply("203")
	step("SET", settings)

	def voices():
		m.send("LIST VOICES\n")
		return m.reply("200")
	lv = step("LIST VOICES", voices)
	if lv:
		print("     " + ", ".join(l[4:].replace("\t", " ") for l in lv[:-1]))

	ssml = "<speak>" + a.text.replace("&", "&amp;").replace("<", "&lt;").replace(". ", '.<mark name="__spd_0"/> ', 1) + "</speak>"
	res = step("SPEAK (SSML + mark)", lambda: m.speak("SPEAK", ssml))
	if res:
		pcm, rate, events = res
		print("     %.2f s of audio at %d Hz; events: %s" % (len(pcm) / 2.0 / max(rate, 1), rate,
			", ".join("%s%s@%d" % (e[0], ":" + e[1] if e[1] else "", e[2]) for e in events)))
		if [e[0] for e in events] != ["begin", "mark", "end"] or not pcm:
			ok = False
			print("FAIL expected begin, mark, audio, end")
		with wave.open(a.wav, "wb") as w:
			w.setnchannels(1)
			w.setsampwidth(2)
			w.setframerate(rate)
			w.writeframes(pcm)
		print("     written to %s (listen: aplay %s)" % (a.wav, a.wav))

	def stop():
		pcm, rate, events = m.speak("SPEAK", "<speak>" + "Ovo je dugačka rečenica koja se prekida. " * 20 + "</speak>", stop_after=1)
		if events[-1][0] != "stop":
			raise Failure("expected 703 STOP, got " + events[-1][0])
		return events
	step("STOP while speaking", stop)

	step("CHAR", lambda: m.speak("CHAR", "č"))
	step("KEY", lambda: m.speak("KEY", "shift_a"))
	step("SOUND_ICON", lambda: m.speak("SOUND_ICON", "button"))
	settings(rate=100, pitch=-50, spelling_mode="on")
	step("SPEAK fast, spelled", lambda: m.speak("SPEAK", "<speak>HNK</speak>"))
	settings(language="en", synthesis_voice="NULL")
	step("SPEAK English (if installed)", lambda: m.speak("SPEAK", "<speak>Hello, world.</speak>"))

	m.send("QUIT\n")
	step("QUIT", lambda: m.reply("210"))
	m.p.wait(5)
	print("all tests passed" if ok else "SOME TESTS FAILED")
	return 0 if ok else 1


if __name__ == "__main__":
	sys.exit(main())
