// MBROLA NG - draws the app icons of the Apple app
// Copyright (c) 2026 Darko Milošević and the MBROLA NG contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
//   swift apple/Scripts/make_icons.swift
//
// Writes apple/App/Assets.xcassets/AppIcon.appiconset. The drawing is the
// one of the Android launcher icon (android/images/make_images.py): a speech
// bubble with a waveform cut out, on a teal gradient. iOS gets the full
// square (the system rounds it), macOS the rounded square with the margin
// its icons have.
import AppKit

let top = CGColor(red: 0x00 / 255.0, green: 0x92 / 255.0, blue: 0x9A / 255.0, alpha: 1)
let bottom = CGColor(red: 0x00 / 255.0, green: 0x4F / 255.0, blue: 0x5C / 255.0, alpha: 1)
// x centre and half height of the bars, in the 108 x 108 grid of the drawing
let bars: [(CGFloat, CGFloat)] = [(38, 5), (46, 11), (54, 15), (62, 9), (70, 6)]

func icon(pixels: Int, mac: Bool) -> Data {
    let size = CGFloat(pixels)
    let space = CGColorSpace(name: CGColorSpace.sRGB)!
    let ctx = CGContext(
        data: nil, width: pixels, height: pixels, bitsPerComponent: 8, bytesPerRow: 0, space: space,
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    // y grows downwards, as in the drawing's grid
    ctx.translateBy(x: 0, y: size)
    ctx.scaleBy(x: 1, y: -1)

    // background
    let plate = mac
        ? CGRect(x: size * 100 / 1024, y: size * 100 / 1024, width: size * 824 / 1024, height: size * 824 / 1024)
        : CGRect(x: 0, y: 0, width: size, height: size)
    ctx.saveGState()
    if mac {
        ctx.addPath(CGPath(
            roundedRect: plate, cornerWidth: plate.width * 0.2237, cornerHeight: plate.width * 0.2237,
            transform: nil))
        ctx.clip()
    }
    let gradient = CGGradient(colorsSpace: space, colors: [top, bottom] as CFArray, locations: [0, 1])!
    ctx.drawLinearGradient(
        gradient, start: CGPoint(x: 0, y: plate.minY), end: CGPoint(x: 0, y: plate.maxY), options: [])
    ctx.restoreGState()

    // the launcher shows the middle 72 of the 108 grid: the same crop here
    let unit = plate.width / 72
    ctx.translateBy(x: plate.minX - 18 * unit, y: plate.minY - 18 * unit)
    ctx.scaleBy(x: unit, y: unit)

    // bubble with its tail, the bars cut out (even-odd)
    let shape = CGMutablePath()
    shape.addRoundedRect(in: CGRect(x: 27, y: 30, width: 54, height: 40), cornerWidth: 10, cornerHeight: 10)
    let bubble = CGMutablePath()
    bubble.addPath(shape)
    let tail = CGMutablePath()
    tail.move(to: CGPoint(x: 37, y: 69))
    tail.addLine(to: CGPoint(x: 37, y: 80))
    tail.addLine(to: CGPoint(x: 49, y: 69))
    tail.closeSubpath()
    ctx.setFillColor(CGColor(red: 1, green: 1, blue: 1, alpha: 1))
    ctx.addPath(tail)
    ctx.fillPath()
    for (x, h) in bars {
        bubble.addRoundedRect(
            in: CGRect(x: x - 2.5, y: 50 - h, width: 5, height: 2 * h), cornerWidth: 2.5, cornerHeight: 2.5)
    }
    ctx.addPath(bubble)
    ctx.fillPath(using: .evenOdd)

    let rep = NSBitmapImageRep(cgImage: ctx.makeImage()!)
    return rep.representation(using: .png, properties: [:])!
}

let folder = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
    .appendingPathComponent("../App/Assets.xcassets/AppIcon.appiconset").standardizedFileURL
try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)

var images: [[String: String]] = []
try icon(pixels: 1024, mac: false).write(to: folder.appendingPathComponent("icon-ios-1024.png"))
images.append(["filename": "icon-ios-1024.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"])
for (points, scale) in [(16, 1), (16, 2), (32, 1), (32, 2), (128, 1), (128, 2), (256, 1), (256, 2), (512, 1), (512, 2)] {
    let name = "icon-mac-\(points)@\(scale)x.png"
    try icon(pixels: points * scale, mac: true).write(to: folder.appendingPathComponent(name))
    images.append(["filename": name, "idiom": "mac", "scale": "\(scale)x", "size": "\(points)x\(points)"])
}
let contents: [String: Any] = ["images": images, "info": ["author": "xcode", "version": 1]]
try JSONSerialization.data(withJSONObject: contents, options: [.prettyPrinted, .sortedKeys])
    .write(to: folder.appendingPathComponent("Contents.json"))
print("wrote \(images.count) icons to \(folder.path)")
