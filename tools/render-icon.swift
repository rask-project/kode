// Renders assets/kode.svg (the icon on the macOS grid: an 824 px body in
// the 1024 px canvas, drawn by tools/make-app-icons.py) to assets/kode.png
// and to an .iconset for iconutil, with the system's drop shadow under the
// body. These are the legacy icon files: with Xcode the bundle's icon is
// compiled from assets/kode.icon.
//
//   swift tools/render-icon.swift SVG PNG ICONSET
import AppKit

let args = CommandLine.arguments
guard args.count == 4, let svg = NSImage(contentsOf: URL(fileURLWithPath: args[1])) else {
    FileHandle.standardError.write("usage: render-icon.swift SVG PNG ICONSET\n".data(using: .utf8)!)
    exit(1)
}

func render(_ size: Int) -> Data {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                               bytesPerRow: 0, bitsPerPixel: 0)!
    rep.size = NSSize(width: size, height: size)
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    NSGraphicsContext.current?.imageInterpolation = .high
    let scale = CGFloat(size) / 1024
    let canvas = NSRect(x: 0, y: 0, width: size, height: size)

    // The shadow follows the body's shape (the SVG's own alpha).
    let shadow = NSShadow()
    shadow.shadowColor = NSColor.black.withAlphaComponent(0.3)
    shadow.shadowOffset = NSSize(width: 0, height: -10 * scale)
    shadow.shadowBlurRadius = 20 * scale
    shadow.set()
    svg.draw(in: canvas, from: .zero, operation: .sourceOver, fraction: 1)

    NSGraphicsContext.restoreGraphicsState()
    return rep.representation(using: .png, properties: [:])!
}

try! render(1024).write(to: URL(fileURLWithPath: args[2]))
let iconset = URL(fileURLWithPath: args[3])
try! FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
for base in [16, 32, 128, 256, 512] {
    try! render(base).write(to: iconset.appendingPathComponent("icon_\(base)x\(base).png"))
    try! render(base * 2).write(to: iconset.appendingPathComponent("icon_\(base)x\(base)@2x.png"))
}
