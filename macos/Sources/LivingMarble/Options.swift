// Command-line options (the same switches as the C++ version).
import Foundation

struct Options {
  var width = 1600, height = 900
  var quality = 1      // mesh/texture quality 0..2
  var gfxPreset = 2    // 0 low .. 3 ultra
  var fullscreen = false
  // capture / testing
  var shotPath = ""           // single screenshot then exit
  var recordDir = ""          // frame sequence then exit
  var frames = 40
  var battle = -1             // stage a battle demo (attacker kind)
  var style = -1              // force one battle style (default: random)
  var shotTimes: [Float] = [] // with --battle: screenshot times
  var shotPrefix = "battle"
  var fen = ""
  var demo = false            // AI vs AI
  var noUI = false
  var hasCam = false
  var noCinematic = false     // keep the camera still (no battle shots, no turning to the side to move)
  var camYaw: Float = 0, camPitch: Float = 0.8, camDist: Float = 12.6
  var camTX: Float = 0, camTY: Float = 0.15, camTZ: Float = 0
  var noAudio = false
  var selftest = false
  var noRender = false        // simulate without drawing (stress tests)
  var level = -1
  var forceMove = ""          // e.g. e5d6, played right after setup
  var selectSq = ""           // e.g. e2, pre-select a piece (screenshots)
  var loadingShot = ""        // capture the loading screen
  var scenario = 0            // canned positions for screenshots
  var pieces = 0              // 0 Living Marble, 1 medieval, 2 aliens
  var dumpSfx = ""

  static let usage = """
    Living Marble - 3D Battle Chess

      --fullscreen            run fullscreen
      --size WxH              window size in points (default 1600x900)
      --quality 0|1|2         sculpting/texture detail (default 1)
      --graphics 0..3         Low / Medium / High / Ultra (default High)
      --demo                  watch the AI play itself
      --level 0..4            AI strength
      --fen "<fen>"           start from a position
      --pieces marble|medieval|alien  which army to play with (default marble)
      --mute                  no sound
      --selftest              verify the move generator (perft)

     capture tools:
      --shot file.png [--frames N] [--scenario 0|1|2|4]   (4: a gallery of ghosts)
      --battle K --shots t1,t2,... [--prefix name]   (K: 0 pawn .. 5 king)
      --fen "<fen>" --move e2e4 --shots t1,t2,...   (screenshots of any move)
      --style S               force battle style S (0..2) instead of a random one
      --record dir [--frames N]
      --camera yaw,pitch,dist[,tx,ty,tz]
      --no-cinematic          keep the camera still
      --no-ui

    """

  /// Parses the arguments (without the program name); prints usage and exits on errors.
  static func parse(_ args: [String]) -> Options {
    var o = Options()
    var i = 0
    func next() -> String {
      i += 1
      return i < args.count ? args[i] : ""
    }
    func floats(_ s: String) -> [Float] { s.split(separator: ",").compactMap { Float($0.trimmingCharacters(in: .whitespaces)) } }
    while i < args.count {
      let a = args[i]
      switch a {
      case "--help", "-h":
        print(usage)
        exit(0)
      case "--fullscreen": o.fullscreen = true
      case "--size":
        let p = next().split(separator: "x").compactMap { Int($0) }
        if p.count == 2 {
          o.width = p[0]
          o.height = p[1]
        }
      case "--quality": o.quality = Int(next()) ?? o.quality
      case "--graphics": o.gfxPreset = Int(next()) ?? o.gfxPreset
      case "--demo": o.demo = true
      case "--fen": o.fen = next()
      case "--mute": o.noAudio = true
      case "--pieces":
        let v = next()
        o.pieces = (v == "medieval" || v == "1") ? 1 : ((v == "alien" || v == "aliens" || v == "2") ? 2 : 0)
      case "--selftest": o.selftest = true
      case "--shot": o.shotPath = next()
      case "--frames": o.frames = Int(next()) ?? o.frames
      case "--scenario": o.scenario = Int(next()) ?? 0
      case "--battle": o.battle = Int(next()) ?? -1
      case "--style": o.style = Int(next()) ?? -1
      case "--prefix": o.shotPrefix = next()
      case "--record": o.recordDir = next()
      case "--no-ui": o.noUI = true
      case "--no-cinematic": o.noCinematic = true
      case "--no-render": o.noRender = true
      case "--level": o.level = Int(next()) ?? -1
      case "--move": o.forceMove = next()
      case "--select": o.selectSq = next()
      case "--loading-shot": o.loadingShot = next()
      case "--dump-sfx": o.dumpSfx = next()
      case "--camera":
        let v = floats(next())
        if v.count >= 3 {
          o.hasCam = true
          o.camYaw = v[0]
          o.camPitch = v[1]
          o.camDist = v[2]
          if v.count >= 6 {
            o.camTX = v[3]
            o.camTY = v[4]
            o.camTZ = v[5]
          }
        }
      case "--shots": o.shotTimes = floats(next())
      default:
        // Launch Services and Xcode add their own arguments (-psn_..., -NSDocumentRevisionsDebugMode ...)
        if a.hasPrefix("-psn_") || (a.hasPrefix("-NS") && !a.hasPrefix("--")) {
          i += 1
        } else {
          FileHandle.standardError.write("unknown option \(a)\n".data(using: .utf8)!)
          print(usage)
          exit(1)
        }
      }
      i += 1
    }
    return o
  }
}
