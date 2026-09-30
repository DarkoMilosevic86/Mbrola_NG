#!/usr/bin/env python3
# MBROLA NG - draws the Google Play store graphics
# Copyright (c) 2026 Darko Milošević
# SPDX-License-Identifier: GPL-2.0-or-later
#
#   python android/images/make_images.py      (needs: pip install pillow)
#
# Writes into android/images:
#   icon-512.png                 app icon for the store listing (512 x 512)
#   feature-graphic-en.png / -hr.png   feature graphic (1024 x 500)
# The icon is the same drawing as the launcher icon
# (app/src/main/res/drawable/ic_launcher_foreground.xml).
# Text uses the Poppins font (SIL Open Font License), downloaded once into
# build/fonts. Phone screenshots are taken from the running app.
import os
import urllib.request

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
FONTS = os.path.join(ROOT, "build", "fonts")
FONT_URL = "https://raw.githubusercontent.com/google/fonts/main/ofl/poppins/Poppins-%s.ttf"

TOP = (0x00, 0x92, 0x9A)       # icon_background_top
BOTTOM = (0x00, 0x4F, 0x5C)    # icon_background_bottom
WHITE = (255, 255, 255)
AMBER = (0xFF, 0xB8, 0x6E)

# speech bubble and waveform, in the 108 x 108 grid of the launcher icon
BARS = [(38, 5), (46, 11), (54, 15), (62, 9), (70, 6)]   # x centre, half height


def font(weight, size):
	os.makedirs(FONTS, exist_ok=True)
	path = os.path.join(FONTS, "Poppins-%s.ttf" % weight)
	if not os.path.isfile(path):
		urllib.request.urlretrieve(FONT_URL % weight, path)
	return ImageFont.truetype(path, size)


def gradient(width, height, top=TOP, bottom=BOTTOM):
	img = Image.new("RGB", (width, height))
	px = img.load()
	for y in range(height):
		t = y / max(1, height - 1)
		c = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
		for x in range(width):
			px[x, y] = c
	return img


def draw_logo(img, left, top, size, bar_colour=None):
	"""The bubble with the waveform cut out, in a size x size square.
	Drawn 4x larger and scaled down for smooth edges."""
	k = 4
	s = size * k / 108.0
	layer = Image.new("L", (size * k, size * k), 0)
	d = ImageDraw.Draw(layer)
	d.rounded_rectangle([27 * s, 30 * s, 81 * s, 70 * s], radius=10 * s, fill=255)
	d.polygon([(37 * s, 69 * s), (37 * s, 80 * s), (49 * s, 69 * s)], fill=255)
	bars = Image.new("L", (size * k, size * k), 0)
	b = ImageDraw.Draw(bars)
	for x, h in BARS:
		b.rounded_rectangle([(x - 2.5) * s, (50 - h) * s, (x + 2.5) * s, (50 + h) * s], radius=2.5 * s, fill=255)
	if bar_colour is None:
		# bars are holes in the bubble
		layer = Image.composite(Image.new("L", layer.size, 0), layer, bars)
	layer = layer.resize((size, size), Image.LANCZOS)
	img.paste(Image.new("RGB", (size, size), WHITE), (left, top), layer)
	if bar_colour is not None:
		bars = bars.resize((size, size), Image.LANCZOS)
		img.paste(Image.new("RGB", (size, size), bar_colour), (left, top), bars)


def icon():
	img = gradient(512, 512)
	# the launcher shows the middle 72/108 of the adaptive icon: same crop here
	size = round(512 * 108 / 72)
	off = (512 - size) // 2
	draw_logo(img, off, off, size)
	img.save(os.path.join(HERE, "icon-512.png"), optimize=True)


def feature(lang, tagline, line2):
	w, h = 1024, 500
	img = gradient(w, h)
	d = ImageDraw.Draw(img)
	# soft sound waves behind the text
	for i, r in enumerate((150, 230, 310, 390)):
		box = [250 - r, 250 - r, 250 + r, 250 + r]
		shade = tuple(min(255, c + 22 - i * 4) for c in gradient(1, h).getpixel((0, 250)))
		d.ellipse(box, outline=shade, width=3)
	draw_logo(img, 60, 60, 380)
	x = 450
	d.text((x, 118), "MBROLA NG", font=font("Bold", 84), fill=WHITE)
	d.text((x, 232), tagline, font=font("Medium", 38), fill=AMBER)
	d.text((x, 296), line2, font=font("Regular", 28), fill=(0xD6, 0xF1, 0xF0))
	img.save(os.path.join(HERE, "feature-graphic-%s.png" % lang), optimize=True)


def main():
	icon()
	feature("en", "Text-to-speech for Android", "Croatian and English voices for TalkBack")
	feature("hr", "Pretvaranje teksta u govor", "Hrvatski i engleski glasovi za TalkBack")
	for name in sorted(os.listdir(HERE)):
		if name.endswith(".png"):
			with Image.open(os.path.join(HERE, name)) as im:
				print(name, im.size)


if __name__ == "__main__":
	main()
