// swift-tools-version: 6.0
// Living Marble - native macOS version (Swift, Metal, AppKit/SwiftUI, AVAudioEngine).
import PackageDescription

let package = Package(
  name: "LivingMarble",
  platforms: [.macOS(.v14)],
  targets: [
    .executableTarget(
      name: "LivingMarble",
      path: "Sources/LivingMarble",
      swiftSettings: [
        .unsafeFlags(["-Ounchecked", "-wmo"], .when(configuration: .release)),
        // the pieces are sculpted at startup: unoptimised builds would take minutes to load
        .unsafeFlags(["-O"], .when(configuration: .debug)),
      ]
    ),
  ],
  swiftLanguageModes: [.v5]
)
