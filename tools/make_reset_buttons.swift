import AppKit

// Apps Scriptへ埋め込む初期化ボタン画像の定義。
struct ButtonSpec {
    let filename: String
    let title: String
    let subtitle: String
    let background: NSColor
    let accent: NSColor
}

// 出力先は第1引数で指定し、省略時は現在のフォルダーを使う。
let outputDirectory = CommandLine.arguments.count > 1
    ? URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
    : URL(fileURLWithPath: FileManager.default.currentDirectoryPath, isDirectory: true)

try FileManager.default.createDirectory(
    at: outputDirectory,
    withIntermediateDirectories: true
)

// 通常初期化と完全初期化を色で明確に区別する。
let specs = [
    ButtonSpec(
        filename: "reset-inventory.png",
        title: "RESET INVENTORY",
        subtitle: "Clear Scans + Inventory",
        background: NSColor(calibratedRed: 0.075, green: 0.118, blue: 0.196, alpha: 1),
        accent: NSColor(calibratedRed: 0.133, green: 0.827, blue: 0.933, alpha: 1)
    ),
    ButtonSpec(
        filename: "full-reset.png",
        title: "FULL RESET",
        subtitle: "Also clear ProductMaster",
        background: NSColor(calibratedRed: 0.50, green: 0.055, blue: 0.090, alpha: 1),
        accent: NSColor(calibratedRed: 0.984, green: 0.451, blue: 0.498, alpha: 1)
    )
]

let size = NSSize(width: 280, height: 68)

// AppKitで高解像度描画し、Apps Scriptが扱えるPNGとして保存する。
for spec in specs {
    let image = NSImage(size: size)
    image.lockFocus()

    let bounds = NSRect(origin: .zero, size: size)
    let backgroundPath = NSBezierPath(roundedRect: bounds.insetBy(dx: 1, dy: 1), xRadius: 12, yRadius: 12)
    spec.background.setFill()
    backgroundPath.fill()

    let border = NSColor.white.withAlphaComponent(0.18)
    border.setStroke()
    backgroundPath.lineWidth = 1
    backgroundPath.stroke()

    let accentPath = NSBezierPath(roundedRect: NSRect(x: 10, y: 11, width: 5, height: 46), xRadius: 2.5, yRadius: 2.5)
    spec.accent.setFill()
    accentPath.fill()

    let titleStyle = NSMutableParagraphStyle()
    titleStyle.alignment = .left
    let titleAttributes: [NSAttributedString.Key: Any] = [
        .font: NSFont.systemFont(ofSize: 18, weight: .semibold),
        .foregroundColor: NSColor.white,
        .paragraphStyle: titleStyle,
        .kern: 0.8
    ]
    spec.title.draw(in: NSRect(x: 28, y: 34, width: 238, height: 24), withAttributes: titleAttributes)

    let subtitleAttributes: [NSAttributedString.Key: Any] = [
        .font: NSFont.systemFont(ofSize: 11, weight: .medium),
        .foregroundColor: NSColor.white.withAlphaComponent(0.72),
        .paragraphStyle: titleStyle,
        .kern: 0.15
    ]
    spec.subtitle.draw(in: NSRect(x: 28, y: 14, width: 238, height: 17), withAttributes: subtitleAttributes)

    image.unlockFocus()

    guard let tiff = image.tiffRepresentation,
          let bitmap = NSBitmapImageRep(data: tiff),
          let png = bitmap.representation(using: .png, properties: [:]) else {
        throw NSError(domain: "ButtonRenderer", code: 1)
    }
    try png.write(to: outputDirectory.appendingPathComponent(spec.filename))
}
