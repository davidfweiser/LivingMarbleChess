// Living Marble - 3D Battle Chess, native macOS edition.
// A photoreal chess game where carved marble pieces come alive and fight.
import AppKit

let options = Options.parse(Array(CommandLine.arguments.dropFirst()))

if options.selftest {
  var b = Board()
  b.setStart()
  let n = Chess.perft(b, 5)
  print("perft(5) = \(n) (\(n == 4_865_609 ? "OK" : "FAIL"))")
  exit(n == 4_865_609 ? 0 : 1)
}
if !options.dumpSfx.isEmpty {
  exit(Audio.dumpWav(options.dumpSfx) ? 0 : 1)
}

MainActor.assumeIsolated {
  let app = NSApplication.shared
  let delegate = AppDelegate(options: options)
  app.delegate = delegate
  app.setActivationPolicy(.regular)
  app.run()
  _ = delegate
}
