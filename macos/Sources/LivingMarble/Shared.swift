// Small declarations shared by several modules.
import Foundation

/// Piece kinds as used by geometry, animation and the battles (chess type - 1).
enum Kind {
  static let pawn = 0
  static let knight = 1
  static let bishop = 2
  static let rook = 3
  static let queen = 4
  static let king = 5
  static let count = 6
  static func fromChessType(_ t: Int) -> Int { t - 1 }
}

/// Procedurally synthesised sound effects.
enum Sfx: Int, CaseIterable {
  case step = 0  // light stone footstep on wood
  case thud      // heavy landing
  case clack     // stone on stone
  case shatter   // marble breaking
  case whoosh    // fast swing
  case clang     // steel ring
  case boom      // cannon
  case explode   // impact explosion
  case charge    // magic charging
  case zap       // magic bolt
  case crackle   // lightning
  case chime     // promotion / materialise
  case select    // UI pick
  case check     // check horn
  case mate      // checkmate gong
  case debris    // small chunk bounce
  case rumble    // low rumble
  // the medieval army
  case hymn      // pipe organ and choir: the Dies irae
  case organ     // one dark organ chord
  case thunder   // lightning crack and rolling thunder
  case wind      // storm wind
  case roar      // dragon roar
  case fire      // dragon fire breath
  case chain     // drawbridge chains and creaking timber
  case grind     // stone grinding as the gatehouse slides
  case stomp     // a giant stone footstep
  case hoof      // a hoof on the board
  case neigh     // a horse's whinny
  case bell      // church bell
  case splinter  // a lance shattering
  case armor     // armoured footstep
  case slide     // a throne sliding over the board
  case bite      // jaws crunching stone
  // the aliens
  case pew       // ray-gun shot
  case ufo       // flying-saucer warble
  case theremin  // an eerie theremin tune
  case beam      // tractor beam
  case gibber    // alien chatter
  case slurp     // a tongue slurping something in
  case burp      // and the burp afterwards
  case boing     // a spring
  case whistle   // slide whistle (blasting off)
  case splat     // a blob of goo landing
  case moo       // a cow
  case pop       // pop!
  case shrink    // shrink ray
  case heatray   // the tripod's heat ray
  case horn      // the tripod's horn
  case twinkle   // a twinkle in the sky
  case sizzle    // acid
  case hatch     // an egg cracking open, chirps
  case drone     // the mothership
  case sputter   // a machine sputtering and dying
}

/// CPU-side image (2D or 3D), 8 bits per channel.
struct TexImage {
  var w = 0, h = 0, d = 1, ch = 4
  var px: [UInt8] = []
}

/// Runs fn(0..<n) across all cores and waits for completion.
func parallelFor(_ n: Int, _ fn: (Int) -> Void) {
  if n <= 0 { return }
  DispatchQueue.concurrentPerform(iterations: n, execute: fn)
}
