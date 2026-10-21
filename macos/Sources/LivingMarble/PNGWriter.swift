// PNG output through ImageIO.
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

/// Writes 8-bit BGRA pixels (top row first, alpha ignored) as an opaque PNG.
@discardableResult
func writePNG(_ path: String, _ w: Int, _ h: Int, bgra: [UInt8]) -> Bool {
  guard w > 0, h > 0, bgra.count >= w * h * 4,
        let provider = CGDataProvider(data: Data(bgra) as CFData),
        let image = CGImage(width: w, height: h, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: w * 4,
                            space: CGColorSpace(name: CGColorSpace.sRGB)!,
                            bitmapInfo: CGBitmapInfo(rawValue: CGBitmapInfo.byteOrder32Little.rawValue |
                                                     CGImageAlphaInfo.noneSkipFirst.rawValue),
                            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent),
        let dest = CGImageDestinationCreateWithURL(URL(fileURLWithPath: path) as CFURL, UTType.png.identifier as CFString, 1, nil)
  else { return false }
  CGImageDestinationAddImage(dest, image, nil)
  return CGImageDestinationFinalize(dest)
}
