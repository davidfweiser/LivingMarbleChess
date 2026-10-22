// The background music. Every type of chess piece has three upbeat themes of
// its own, written as little text scores (MusicScore.swift) and played by a
// small real-time synthesiser: no audio files. When a piece moves, the music
// switches to one of its themes at the next bar line; each piece's theme picks
// up where it left off, and when one ends another of that piece's themes is
// chosen at random.
//
// A port of music.cpp that plays the same: the score is read the same way and
// every voice, filter and effect does the same arithmetic. MusicScore parses
// the text (once, off the audio thread); MusicEngine plays it, and everything
// it touches while rendering is plain values in manually allocated memory: no
// ARC, no Swift arrays and no allocation on the audio thread.
import Foundation

let musicPieceNames = ["pawn", "knight", "bishop", "rook", "queen", "king"]

private let mSR = 44100
private let mTAU = 6.283185307179586
private let mPI: Float = 3.14159265
private let mVoices = 64
private let mTab = 4096        // wavetable length (two guard samples follow)
private let mKS = 2048         // plucked-string delay line per voice (down to 22 Hz)
private let mBlock = 256       // the mix is made in blocks of this many samples
private let mEcho = 65536      // echo line per channel (1.49 s)
private let mLevel: Float = 0.5  // music under the sound effects
private let mPieces = 6

// kinds, waves, filters and drums
private let kSynth = 0, kFM = 1, kKS = 2, kDrum = 3
private let wSaw = 0, wSqr = 1, wTri = 2, wSin = 3, wOrgan = 4, wOrgan2 = 5, wFlute = 6
private let fNone = 0, fLP = 1, fBP = 2, fVox = 3
private let dMembrane = 0, dSnare = 1, dClap = 2, dMetal = 3, dShaker = 4

private struct MRng {
  var s: UInt32 = 0x9e37_79b9
  mutating func u32() -> UInt32 {
    s ^= s &<< 13
    s ^= s &>> 17
    s ^= s &<< 5
    return s
  }
  mutating func next() -> Float { Float(u32() & 0xffffff) / Float(0xffffff) }
  mutating func bi() -> Float { next() * 2 - 1 }
}

// ---------------------------------------------------------------------------
// MARK: - Instruments
// ---------------------------------------------------------------------------
private struct MPatch {
  var kind = kSynth, wave = wSaw, filt = fNone, uni = 1
  var det: Float = 0, pw: Float = 0.5, sub: Float = 0, noise: Float = 0
  var a: Float = 0.005, d: Float = 0.3, s: Float = 0.7, r: Float = 0.1
  var hz: Float = 0, cut: Float = 0, res: Float = 0, fenv: Float = 0, fatk: Float = 0, fdec: Float = 0.2
  var vibRate: Float = 0, vibDepth: Float = 0, vibDelay: Float = 0, scoop: Float = 0, drive: Float = 0
  var tremRate: Float = 0, tremDepth: Float = 0
  var ratio: Float = 1, index: Float = 0, idec: Float = 0.5, p2: Float = 0, p2amp: Float = 0, p2dec: Float = 0.3
  var bright: Float = 0.6, damp: Float = 0.5, decay: Float = 1
  var f: Float = 100, pamt: Float = 0, pdec: Float = 0.05, nf: Float = 1000, ndec: Float = 0.02
  var snap: Float = 0, tone: Float = 0.5, metal: Float = 0.5
  var gain: Float = 0.5, pan: Float = 0, rev: Float = 0.1, del: Float = 0
}

/// Zavalishin's trapezoidal state-variable filter.
private struct MSvf {
  var ic1: Float = 0, ic2: Float = 0, a1: Float = 0, a2: Float = 0, a3: Float = 0, k: Float = 2
  mutating func set(_ fcIn: Float, _ res: Float) {
    let fc = min(max(fcIn, 20), 18000)
    let g = tan(mPI * fc / Float(mSR))
    k = 2 - 1.9 * res
    a1 = 1 / (1 + g * (g + k))
    a2 = g * a1
    a3 = g * a2
  }
  mutating func run(_ x: Float, _ lp: inout Float, _ bp: inout Float) {
    let v3 = x - ic2
    let v1 = a1 * ic1 + a2 * v3
    let v2 = ic2 + a2 * ic1 + a3 * v3
    ic1 = 2 * v1 - ic1
    ic2 = 2 * v2 - ic2
    lp = v2
    bp = v1
  }
}

/// One playing note. Its oscillator phases and increments (6 each), unison detunes (3) and
/// filters (the main one and 3 more) live in the engine's per-voice arrays.
private struct MVoice {
  var patch = -1  // -1: free
  var age = 0, gateAt = 0, stage = 0  // stage 0 attack, 1 decay/sustain, 2 release
  var env: Float = 0, aInc: Float = 0, dCoef: Float = 0, rCoef: Float = 0
  var freq: Float = 0, amp: Float = 0, gl: Float = 0, gr: Float = 0, rev: Float = 0, del: Float = 0
  var phSub = 0.0, phM = 0.0, ph2 = 0.0
  var trem: Float = 1, idx: Float = 0, p2g: Float = 0
  // plucked string
  var ksW = 0
  var ksN: Float = 0, ksG: Float = 1, ksPrev: Float = 0
  // drums
  var dAmp: Float = 0, dCoef2: Float = 0, nAmp: Float = 0, nCoef: Float = 0
  var e1: Float = 0, e2: Float = 0, c1: Float = 0, c2: Float = 0
  var rng = MRng()
}

@inline(__always) private func polyblep(_ tIn: Double, _ dt: Double) -> Float {
  var t = tIn
  if t < dt {
    t /= dt
    return Float(t + t - t * t - 1.0)
  }
  if t > 1.0 - dt {
    t = (t - 1.0) / dt
    return Float(t * t + t + t + 1.0)
  }
  return 0
}

@inline(__always) private func lookup(_ t: UnsafeMutablePointer<Float>, _ ph: Float) -> Float {
  let x = ph * Float(mTab)
  let i = Int(x)
  let fr = x - Float(i)
  return t[i] + (t[i + 1] - t[i]) * fr
}

@inline(__always) private func frac(_ x: Float) -> Float { x - floor(x) }

private func mtof(_ note: Int) -> Float { 440 * exp2(Float(note - 69) / 12) }

/// The six square waves of a cymbal (as on the TR-808); a switch, as the audio thread may not allocate.
private func metalFrequency(_ k: Int) -> Float {
  switch k {
  case 0: return 205.3
  case 1: return 304.4
  case 2: return 369.6
  case 3: return 522.7
  case 4: return 540.0
  default: return 800.0
  }
}

// ---------------------------------------------------------------------------
// MARK: - The score
// ---------------------------------------------------------------------------
private struct MSong {
  var piece = 0, beat = 4, bar = 16, bars = 8, echo = 3
  var bpm: Float = 120, swing: Float = 0, gain: Float = 1
  var sps = 0.0    // samples per step
  var len = 0      // samples
  var first = 0, count = 0  // its events
  var echoLen = 0
}

private struct MTrack {
  var patch = 0
  var vol: Float = 1, pan: Float = 0, rev: Float = 0, del: Float = 0
}

private struct MEvent {
  var t = 0    // sample, from the start of the song
  var len = 0  // samples until the note is released
  var track = 0, note = 0, seq = 0
  var vel: Float = 0.8
}

private func stepTime(_ s: MSong, _ step: Int) -> Double {
  var t = Double(step) * s.sps
  if s.beat % 2 == 0 && (step & 1) != 0 { t += Double(s.swing) * s.sps }
  return t
}
private func stepSample(_ s: MSong, _ step: Int) -> Int { Int(stepTime(s, step)) }

private struct MChord {
  var root = 0, third = 4, fifth = 7, sev = 12, n = 3
  var tones = [0, 4, 7, 0, 0]
}

/// One step of a part: a rest, notes starting, or the notes before held on.
private struct MSpec {
  var kind = 0, value = 0, oct = 0  // kind 0 a MIDI note, 1 a chord degree, 2 the chord, 3 a power chord
}
private struct MCell {
  var type = 0  // 0 rest, 1 start, 2 hold
  var vel: Float = 0.8
  var first = 0, count = 0  // its specs
}

private func splitWS(_ s: String) -> [String] {
  s.split(whereSeparator: { $0 == " " || $0 == "\t" }).map(String.init)
}

private func trim(_ s: String) -> String { s.trimmingCharacters(in: CharacterSet(charactersIn: " \t\r")) }

/// strtof / atoi, as the C++ uses them: a number at the start of the text, 0 if none.
private func cFloat(_ s: String) -> Float { strtof(s, nil) }
private func cInt(_ s: String) -> Int { Int(atoi(s)) }

private func floats(_ s: String) -> [Float] {
  s.split(separator: ",", omittingEmptySubsequences: false).map { cFloat(String($0)) }
}

private func isDigits(_ s: Substring) -> Bool { !s.isEmpty && s.allSatisfy { $0 >= "0" && $0 <= "9" } }
private func isDigits(_ s: String) -> Bool { isDigits(Substring(s)) }

/// "C", "F#", "Bb" at the start of s -> (pitch class, characters used); 0 characters if none.
private func pitchClass(_ s: String) -> (pc: Int, n: Int) {
  let b = Array(s.utf8)
  guard let c = b.first, c >= 65, c <= 71 else { return (0, 0) }  // A..G
  let base = [9, 11, 0, 2, 4, 5, 7]
  var pc = base[Int(c) - 65], n = 1
  if b.count > 1 && b[1] == 35 {  // #
    pc += 1
    n = 2
  } else if b.count > 1 && b[1] == 98 {  // b
    pc -= 1
    n = 2
  }
  return ((pc + 12) % 12, n)
}

/// "C4", "F#5", "Bb3" -> MIDI note, or -1.
private func noteNumber(_ s: String) -> Int {
  let (pc, n) = pitchClass(s)
  let rest = s.dropFirst(n)
  if n == 0 || !isDigits(rest) { return -1 }
  return 12 * (cInt(String(rest)) + 1) + pc
}

private func chordOf(_ q: String) -> MChord? {
  let table: [(String, Int, Int, Int, Int, [Int])] = [
    ("", 4, 7, 12, 3, [0, 4, 7, 0, 0]), ("m", 3, 7, 12, 3, [0, 3, 7, 0, 0]),
    ("7", 4, 7, 10, 4, [0, 4, 7, 10, 0]), ("m7", 3, 7, 10, 4, [0, 3, 7, 10, 0]),
    ("maj7", 4, 7, 11, 4, [0, 4, 7, 11, 0]), ("dim", 3, 6, 9, 3, [0, 3, 6, 0, 0]),
    ("m7b5", 3, 6, 10, 4, [0, 3, 6, 10, 0]), ("aug", 4, 8, 12, 3, [0, 4, 8, 0, 0]),
    ("sus4", 5, 7, 12, 3, [0, 5, 7, 0, 0]), ("sus2", 2, 7, 12, 3, [0, 2, 7, 0, 0]),
    ("6", 4, 7, 9, 4, [0, 4, 7, 9, 0]), ("m6", 3, 7, 9, 4, [0, 3, 7, 9, 0]),
    ("5", 7, 7, 12, 2, [0, 7, 0, 0, 0]), ("9", 4, 7, 10, 5, [0, 4, 7, 10, 14]),
    ("add9", 4, 7, 12, 4, [0, 4, 7, 14, 0]), ("m9", 3, 7, 10, 5, [0, 3, 7, 10, 14]),
  ]
  for e in table where e.0 == q {
    return MChord(root: 0, third: e.1, fifth: e.2, sev: e.3, n: e.4, tones: e.5)
  }
  return nil
}

private func degreeSemis(_ deg: Int, _ c: MChord) -> Int {
  if deg >= 8 { return degreeSemis(deg - 7, c) + 12 }
  switch deg {
  case 1: return 0
  case 2: return 2
  case 3: return c.third
  case 4: return 5
  case 5: return c.fifth
  case 6: return 9
  default: return c.sev
  }
}

private func mod12(_ x: Int) -> Int { ((x % 12) + 12) % 12 }

/// The parsed score: the instruments, the songs and every note of them.
final class MusicScore {
  fileprivate var patches: [MPatch] = []
  fileprivate var patchNames: [String] = []
  fileprivate var songs: [MSong] = []
  fileprivate var tracks: [MTrack] = []
  fileprivate var events: [MEvent] = []
  private(set) var titles: [String] = []
  /// Problems found in the score (wrong bar lengths, unknown instruments ...); empty when all is well.
  private(set) var warnings: [String] = []

  init(_ text: String) {
    parse(text)
  }

  var songCount: Int { songs.count }
  func songPiece(_ s: Int) -> Int { songs[s].piece }

  private func warning(_ whereIn: String, _ what: String) { warnings.append(whereIn + ": " + what) }

  private func patchIndex(_ name: String) -> Int { patchNames.firstIndex(of: name) ?? -1 }

  private static func key(_ l: String) -> String {
    String(l.prefix { $0 != " " && $0 != "\t" && $0 != ":" })
  }

  private func parse(_ text: String) {
    var lines: [String] = []
    for rawSub in text.split(separator: "\n", omittingEmptySubsequences: false) {
      let raw = String(rawSub)
      let t = trim(raw)
      if t.isEmpty || t.hasPrefix("#") { continue }  // a comment ('#' is also a sharp, so only whole lines)
      if (raw.hasPrefix(" ") || raw.hasPrefix("\t")) && !lines.isEmpty {
        lines[lines.count - 1] += " " + t
      } else {
        lines.append(t)
      }
    }
    var songLines: [String] = []
    for l in lines {
      let key = Self.key(l)
      if key == "patch" {
        parsePatch(l)
      } else if key == "song" {
        if !songLines.isEmpty { buildSong(songLines) }
        songLines = [l]
      } else if !songLines.isEmpty {
        songLines.append(l)
      } else {
        warning("score", "a line outside any song: " + l)
      }
    }
    if !songLines.isEmpty { buildSong(songLines) }
  }

  private func parsePatch(_ line: String) {
    let tk = splitWS(line)
    if tk.count < 3 {
      warning("patch", line)
      return
    }
    var P = MPatch()
    let name = tk[1]
    guard let kind = ["synth", "fm", "ks", "drum"].firstIndex(of: tk[2]) else {
      warning(name, "unknown kind " + tk[2])
      return
    }
    P.kind = kind
    if P.kind == kKS {
      P.a = 0.001
      P.s = 1
    }
    var at = 3
    if P.kind == kSynth || P.kind == kDrum {
      let waves = ["saw", "sqr", "tri", "sin", "organ", "organ2", "flute"]
      let drums = ["membrane", "snare", "clap", "metal", "shaker"]
      var found = -1
      if tk.count > 3 { found = (P.kind == kSynth ? waves : drums).firstIndex(of: tk[3]) ?? -1 }
      if found < 0 {
        warning(name, "needs a wave or drum type")
        return
      }
      P.wave = found
      at = 4
    }
    var filtSet = false
    while at < tk.count {
      defer { at += 1 }
      guard let eq = tk[at].firstIndex(of: "=") else {
        warning(name, "bad setting " + tk[at])
        continue
      }
      let k = String(tk[at][..<eq]), v = String(tk[at][tk[at].index(after: eq)...])
      let f = floats(v)
      let x = f[0]
      switch k {
      case "filt":
        filtSet = true
        P.filt = v == "lp" ? fLP : v == "bp" ? fBP : v == "vox" ? fVox : fNone
      case "uni": P.uni = min(3, max(1, Int(x)))
      case "det": P.det = x
      case "pw": P.pw = x
      case "sub": P.sub = x
      case "noise": P.noise = x
      case "a": P.a = x
      case "d": P.d = x
      case "s": P.s = x
      case "r": P.r = x
      case "hz": P.hz = x
      case "cut": P.cut = x
      case "res": P.res = x
      case "fenv": P.fenv = x
      case "fatk": P.fatk = x
      case "fdec": P.fdec = x
      case "vib" where f.count == 3:
        P.vibRate = f[0]
        P.vibDepth = f[1]
        P.vibDelay = f[2]
      case "trem" where f.count == 2:
        P.tremRate = f[0]
        P.tremDepth = f[1]
      case "scoop": P.scoop = x
      case "drive": P.drive = x
      case "ratio": P.ratio = x
      case "index": P.index = x
      case "idec": P.idec = x
      case "p2": P.p2 = x
      case "p2amp": P.p2amp = x
      case "p2dec": P.p2dec = x
      case "bright": P.bright = x
      case "damp": P.damp = x
      case "decay": P.decay = x
      case "f": P.f = x
      case "pamt": P.pamt = x
      case "pdec": P.pdec = x
      case "nf": P.nf = x
      case "ndec": P.ndec = x
      case "snap": P.snap = x
      case "tone": P.tone = x
      case "metal": P.metal = x
      case "gain": P.gain = x
      case "pan": P.pan = x
      case "rev": P.rev = x
      case "del": P.del = x
      default: warning(name, "unknown setting " + tk[at])
      }
    }
    if !filtSet && (P.cut > 0 || P.hz > 0) { P.filt = fLP }
    if patchIndex(name) >= 0 { warning(name, "defined twice") }
    patches.append(P)
    patchNames.append(name)
  }

  /// Splits a part into bars at '|'; a bar may end with "*N" to repeat it.
  /// Returns the bars, each as its tokens (drums: each character a token).
  private func bars(_ data: String, _ chars: Bool) -> [[String]] {
    var out: [[String]] = []
    let tk = splitWS(data.replacingOccurrences(of: "|", with: " | "))
    var seg: [String] = []
    func flush() {
      var rep = 1
      if let last = seg.last, last.count > 1, last.hasPrefix("*"), isDigits(last.dropFirst()) {
        rep = cInt(String(last.dropFirst()))
        seg.removeLast()
      }
      if chars { seg = seg.flatMap { $0.map { String($0) } } }
      if !seg.isEmpty {
        for _ in 0..<max(rep, 0) { out.append(seg) }
      }
      seg.removeAll()
    }
    for t in tk {
      if t == "|" {
        flush()
      } else if chars && t.count > 1 && t.hasPrefix("*") && isDigits(t.dropFirst()) {
        seg.append(t)
      } else if chars, let st = t.firstIndex(of: "*"), st != t.startIndex {
        seg.append(String(t[..<st]))
        seg.append(String(t[st...]))
      } else {
        seg.append(t)
      }
    }
    flush()
    return out
  }

  private func buildSong(_ lines: [String]) {
    var S = MSong()
    var title = ""
    let head = splitWS(lines[0])
    for h in head.dropFirst() {
      let eq = h.firstIndex(of: "=")
      let k = eq.map { String(h[..<$0]) } ?? h
      let v = eq.map { String(h[h.index(after: $0)...]) } ?? ""
      let x = cFloat(v)
      switch k {
      case "title": title = v.replacingOccurrences(of: "_", with: " ")
      case "piece": S.piece = musicPieceNames.firstIndex(of: v) ?? -1
      case "bpm": S.bpm = x
      case "beat": S.beat = Int(x)
      case "bar": S.bar = Int(x)
      case "bars": S.bars = Int(x)
      case "swing": S.swing = x
      case "echo": S.echo = Int(x)
      case "gain": S.gain = x
      default: warning("song", "unknown setting " + h)
      }
    }
    let whereIn = title.isEmpty ? "song" : title
    if S.piece < 0 || S.bpm <= 0 || S.beat <= 0 || S.bar <= 0 || S.bars <= 0 {
      warning(whereIn, "bad song header")
      return
    }
    S.sps = Double(mSR) * 60.0 / (Double(S.bpm) * Double(S.beat))
    S.len = stepSample(S, S.bars * S.bar)
    S.echoLen = min(mEcho - 1, Int(Double(S.echo) * S.sps))
    let total = S.bars * S.bar

    // the chords, one per step
    var chordAt: [MChord] = []
    for line in lines.dropFirst() where Self.key(line) == "chords" {
      let data = line.firstIndex(of: ":").map { String(line[line.index(after: $0)...]) } ?? ""
      for b in bars(data, false) {
        var used = 0
        for t in b {
          let (pc, n) = pitchClass(t)
          let colonAt = t.firstIndex(of: ":")
          let q = String(t.dropFirst(n).prefix { $0 != ":" })
          guard n > 0, var c = chordOf(q) else {
            warning(whereIn, "unknown chord " + t)
            continue
          }
          c.root = pc
          var dur = colonAt == nil ? S.bar - used % S.bar : cInt(String(t[t.index(after: colonAt!)...]))
          if dur <= 0 { dur = S.bar }
          for _ in 0..<dur { chordAt.append(c) }
          used += dur
        }
        if used % S.bar != 0 { warning(whereIn, "chords: a bar of \(used) steps") }
      }
    }
    if chordAt.isEmpty { chordAt.append(MChord()) }

    var evs: [MEvent] = []
    var seq = 0
    var lastData = ""
    for line in lines.dropFirst() {
      let colon = line.firstIndex(of: ":")
      let tk = splitWS(colon.map { String(line[..<$0]) } ?? line)
      if tk.isEmpty || tk[0] == "chords" { continue }
      let kind = tk[0]
      guard kind == "drum" || kind == "pat" || kind == "mel", tk.count >= 2, let colon else {
        warning(whereIn, "bad line: " + line)
        continue
      }
      let pi = patchIndex(tk[1])
      if pi < 0 {
        warning(whereIn, "unknown instrument " + tk[1])
        continue
      }
      let part = whereIn + " " + kind + " " + tk[1]
      var T = MTrack()
      T.patch = pi
      T.rev = patches[pi].rev
      T.del = patches[pi].del
      var lo = 48, tr = 0, drumNote = 0
      var gate: Float = 0.9
      var act = [Bool](repeating: true, count: S.bars)
      for opt in tk.dropFirst(2) {
        guard let eq = opt.firstIndex(of: "=") else {
          warning(part, "bad setting " + opt)
          continue
        }
        let k = String(opt[..<eq]), v = String(opt[opt.index(after: eq)...])
        let x = cFloat(v)
        switch k {
        case "vol": T.vol = x
        case "pan": T.pan = x
        case "rev": T.rev = x
        case "del": T.del = x
        case "gate": gate = x
        case "tr": tr = cInt(v)
        case "lo", "note":
          let n = noteNumber(v)
          if n < 0 {
            warning(part, "bad note " + v)
          } else if k == "lo" {
            lo = n
          } else {
            drumNote = n
          }
        case "bars":
          for i in act.indices { act[i] = false }
          for r in v.split(separator: ",", omittingEmptySubsequences: false).map(String.init) {
            let dash = r.firstIndex(of: "-")
            let x0 = cInt(r), x1 = dash == nil ? x0 : cInt(String(r[r.index(after: dash!)...]))
            if x0 < 1 || x1 > S.bars || x1 < x0 { warning(part, "bad bars " + r) }
            let a = max(1, x0), b = min(S.bars, x1)
            if a <= b {
              for bb in a...b { act[bb - 1] = true }
            }
          }
        default: warning(part, "unknown setting " + opt)
        }
      }

      // the part, as one cell per step
      var cells: [MCell] = []
      var specs: [MSpec] = []
      var data = String(line[line.index(after: colon)...])
      if trim(data) == "same" {
        data = lastData  // the part before, on another instrument
      } else {
        lastData = data
      }
      var dur = 4
      for b in bars(data, kind == "drum") {
        let before = cells.count
        for t in b {
          var c = MCell()
          if kind == "drum" {
            let ch = t.first!
            if ch == "x" || ch == "X" || ch == "o" {
              c.type = 1
              c.vel = ch == "X" ? 1 : ch == "o" ? 0.45 : 0.8
            } else if ch != "." && ch != "-" {
              warning(part, "bad drum hit " + t)
            }
            cells.append(c)
            continue
          }
          if kind == "pat" {
            if t == "." {
            } else if t == "-" {
              c.type = 2
            } else {
              var body = Array(t)
              while let l = body.last, l == "!" || l == "?" {
                c.vel = l == "!" ? 1 : 0.5
                body.removeLast()
              }
              var sp = MSpec()
              var n = 0
              if let f = body.first, f == "c" || f == "p" {
                sp.kind = f == "c" ? 2 : 3
                n = 1
              } else {
                while n < body.count && body[n] >= "0" && body[n] <= "9" { n += 1 }
                sp.kind = 1
                sp.value = n > 0 ? cInt(String(body[0..<n])) : 0
              }
              var ok = sp.kind != 1 || (sp.value >= 1 && sp.value <= 22)
              while n < body.count {
                if body[n] == "'" {
                  sp.oct += 1
                } else if body[n] == "," {
                  sp.oct -= 1
                } else {
                  ok = false
                }
                n += 1
              }
              if !ok {
                warning(part, "bad step " + t)
                cells.append(MCell())
                continue
              }
              c.type = 1
              c.first = specs.count
              c.count = 1
              specs.append(sp)
            }
            cells.append(c)
            continue
          }
          // mel: notes with durations
          let body = Array(t)
          let cut = body.firstIndex { $0 == ":" || $0 == "!" || $0 == "?" } ?? body.count
          let what = String(body[0..<cut])
          var k = cut
          while k < body.count {
            if body[k] == "!" {
              c.vel = 1
            } else if body[k] == "?" {
              c.vel = 0.5
            } else if body[k] == ":" {
              var e = k + 1
              while e < body.count && body[e] >= "0" && body[e] <= "9" { e += 1 }
              dur = cInt(String(body[(k + 1)..<e]))
              k = e - 1
            }
            k += 1
          }
          if dur <= 0 {
            warning(part, "bad length " + t)
            dur = 1
          }
          var type = 1
          if what == "r" {
            type = 0
          } else if what == "_" {
            type = 2
          } else {
            c.first = specs.count
            for piece in what.split(separator: "+", omittingEmptySubsequences: false) {
              let note = noteNumber(String(piece))
              if note < 0 {
                warning(part, "bad note " + t)
              } else {
                var sp = MSpec()
                sp.value = note
                specs.append(sp)
              }
            }
            c.count = specs.count - c.first
            if c.count == 0 { type = 0 }
          }
          c.type = type
          cells.append(c)
          var rest = c
          rest.type = type == 0 ? 0 : 2
          if dur > 1 {
            for _ in 1..<dur { cells.append(rest) }
          }
        }
        let n = cells.count - before
        if n % S.bar != 0 { warning(part, "a bar of \(n) steps") }
      }
      if cells.isEmpty { continue }

      // the events
      let ti = tracks.count
      tracks.append(T)
      var open: [Int] = []
      var openStart = 0
      func close(_ step: Int) {
        for k in open {
          let ln = (stepTime(S, step) - stepTime(S, openStart)) * Double(gate)
          evs[k].len = max(1, Int(ln))
        }
        open.removeAll()
      }
      let L = cells.count
      var ai = 0
      for s in 0..<total {
        if !act[s / S.bar] {
          close(s)
          continue
        }
        let c = cells[ai % L]
        ai += 1
        if c.type == 0 {
          close(s)
        } else if c.type == 1 {
          close(s)
          let ch = chordAt[s % chordAt.count]
          let root = lo + mod12(ch.root - lo)
          var notes: [Int] = []
          for k in 0..<c.count {
            let sp = specs[c.first + k]
            if kind == "drum" {
              notes.append(drumNote)
            } else if sp.kind == 0 {
              notes.append(sp.value + tr)
            } else if sp.kind == 1 {
              notes.append(root + degreeSemis(sp.value, ch) + 12 * sp.oct + tr)
            } else if sp.kind == 2 {
              for q in 0..<ch.n { notes.append(lo + mod12(ch.root + ch.tones[q] - lo) + 12 * sp.oct + tr) }
            } else {
              notes.append(root + 12 * sp.oct + tr)
              notes.append(root + 7 + 12 * sp.oct + tr)
              notes.append(root + 12 + 12 * sp.oct + tr)
            }
          }
          if kind == "drum" { notes = [drumNote] }
          for note in notes {
            if note < 0 || note > 127 { continue }
            var e = MEvent()
            e.t = stepSample(S, s)
            e.track = ti
            e.note = note
            e.vel = c.vel
            e.seq = seq
            seq += 1
            open.append(evs.count)
            evs.append(e)
          }
          openStart = s
        }
      }
      close(total)
    }
    evs.sort { $0.t != $1.t ? $0.t < $1.t : $0.seq < $1.seq }
    S.first = events.count
    S.count = evs.count
    events.append(contentsOf: evs)
    songs.append(S)
    titles.append(title)
  }
}

// ---------------------------------------------------------------------------
// MARK: - Effects: a Freeverb-style hall and a ping-pong echo
// ---------------------------------------------------------------------------
private func alloc<T>(_ n: Int, _ v: T) -> UnsafeMutablePointer<T> {
  let p = UnsafeMutablePointer<T>.allocate(capacity: max(n, 1))
  p.initialize(repeating: v, count: max(n, 1))
  return p
}

private struct MReverb {
  let buf: UnsafeMutablePointer<Float>
  let off = alloc(24, 0), len = alloc(24, 0), idx = alloc(24, 0)
  let store = alloc(16, Float(0))
  let size: Int
  init() {
    let comb = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617]
    let ap = [556, 441, 341, 225]
    var o = 0
    for k in 0..<24 {
      let n = k < 16 ? comb[k & 7] + (k >= 8 ? 23 : 0) : ap[(k - 16) & 3] + (k >= 20 ? 23 : 0)
      off[k] = o
      len[k] = n
      o += n
    }
    size = o
    buf = alloc(o, 0)
  }
  func clear() {
    buf.update(repeating: 0, count: size)
    store.update(repeating: 0, count: 16)
  }
  func free() {
    for p in [off, len, idx] { p.deallocate() }
    store.deallocate()
    buf.deallocate()
  }
  @inline(__always) func run(_ x: Float, _ l: inout Float, _ r: inout Float) {
    let input = x * 0.015
    var outL: Float = 0, outR: Float = 0
    for k in 0..<16 {
      let b = buf + off[k]
      let o = b[idx[k]]
      store[k] = o * 0.75 + store[k] * 0.25
      b[idx[k]] = input + store[k] * 0.84
      idx[k] += 1
      if idx[k] >= len[k] { idx[k] = 0 }
      if k < 8 {
        outL += o
      } else {
        outR += o
      }
    }
    for k in 16..<24 {
      let b = buf + off[k]
      let y = k < 20 ? outL : outR
      let o = b[idx[k]]
      b[idx[k]] = y + o * 0.5
      if k < 20 {
        outL = o - y
      } else {
        outR = o - y
      }
      idx[k] += 1
      if idx[k] >= len[k] { idx[k] = 0 }
    }
    l = outL * 3
    r = outR * 3
  }
}

private struct MEcho {
  let L = alloc(mEcho, Float(0)), R = alloc(mEcho, Float(0))
  var w = 0, len = 11025
  var lp: Float = 0
  mutating func clear() {
    L.update(repeating: 0, count: mEcho)
    R.update(repeating: 0, count: mEcho)
    lp = 0
  }
  func free() {
    L.deallocate()
    R.deallocate()
  }
  @inline(__always) mutating func run(_ x: Float, _ l: inout Float, _ r: inout Float) {
    let rp = (w - len) & (mEcho - 1)
    let dl = L[rp], dr = R[rp]
    lp += 0.4 * (dr - lp)
    L[w] = x + lp * 0.42
    R[w] = dl
    w = (w + 1) & (mEcho - 1)
    l = dl * 0.7
    r = dr * 0.7
  }
}

// ---------------------------------------------------------------------------
// MARK: - The engine
// ---------------------------------------------------------------------------
/// Plays the score. Not thread-safe: the owner calls everything from the audio
/// thread (or before it starts). render() never allocates.
struct MusicEngine {
  private let patches: UnsafeMutablePointer<MPatch>
  private let songs: UnsafeMutablePointer<MSong>
  private let tracks: UnsafeMutablePointer<MTrack>
  private let events: UnsafeMutablePointer<MEvent>
  private let songCount: Int
  private let crash: Int  // the cymbal that marks a change of theme
  private let tabs: UnsafeMutablePointer<Float>
  private let voices: UnsafeMutablePointer<MVoice>
  private let phs: UnsafeMutablePointer<Double>   // 6 per voice
  private let incs: UnsafeMutablePointer<Double>  // 6 per voice
  private let dets: UnsafeMutablePointer<Float>   // 3 per voice
  private let svfs: UnsafeMutablePointer<MSvf>    // 4 per voice: the filter, then 3 more
  private let ks: UnsafeMutablePointer<Float>
  private let reverb = MReverb()
  private var echo = MEcho()
  private let dryL = alloc(mBlock, Float(0)), dryR = alloc(mBlock, Float(0))
  private let revIn = alloc(mBlock, Float(0)), delIn = alloc(mBlock, Float(0))
  // the sequencer
  private var cur = -1, curBar = 0, ev = 0, evEnd = 0, pending = -1, want = -1
  private var pos = 0, barEnd = 0, since = 0
  private let pieceSong = alloc(mPieces, -1), pieceBar = alloc(mPieces, 0), lastSong = alloc(mPieces, -1)
  private var enabled = true, single = false
  private var fade: Float = 1, fadeTarget: Float = 1, fadeStep: Float = 1 / (2.5 * Float(mSR))
  private var volume: Float = 0.7, volS: Float = 0.7, songGain: Float = 1, songGainS: Float = 1
  private var duckGain: Float = 1, duckDepth: Float = 1
  private var dcL: Float = 0, dcR: Float = 0, dcOutL: Float = 0, dcOutR: Float = 0  // DC blocker on the way out
  private var duckHold = 0, tail = 0
  private var pick = MRng(), noise = MRng()

  init(_ score: MusicScore) {
    func copy<T>(_ a: [T], _ empty: T) -> UnsafeMutablePointer<T> {
      let p = alloc(a.count, empty)
      for (i, x) in a.enumerated() { p[i] = x }
      return p
    }
    patches = copy(score.patches, MPatch())
    songs = copy(score.songs, MSong())
    tracks = copy(score.tracks, MTrack())
    events = copy(score.events, MEvent())
    songCount = score.songs.count
    tabs = MusicEngine.makeTables()
    voices = alloc(mVoices, MVoice())
    phs = alloc(mVoices * 6, 0.0)
    incs = alloc(mVoices * 6, 0.0)
    dets = alloc(mVoices * 3, Float(1))
    svfs = alloc(mVoices * 4, MSvf())
    ks = alloc(mVoices * mKS, Float(0))
    noise.s = 20_240_611
    crash = score.patchNames.lastIndex(of: "crash") ?? -1
  }

  /// Only once the audio thread no longer calls render().
  func free() {
    patches.deallocate()
    songs.deallocate()
    tracks.deallocate()
    events.deallocate()
    tabs.deallocate()
    voices.deallocate()
    phs.deallocate()
    incs.deallocate()
    dets.deallocate()
    svfs.deallocate()
    ks.deallocate()
    reverb.free()
    echo.free()
    for p in [dryL, dryR, revIn, delIn] { p.deallocate() }
    for p in [pieceSong, pieceBar, lastSong] { p.deallocate() }
  }

  // ---- tables: sine, two organs, a flute -------------------------------
  private static func makeTables() -> UnsafeMutablePointer<Float> {
    let tabs = alloc(4 * (mTab + 2), Float(0))
    let organ: [(Int, Double)] = [(1, 1.0), (2, 0.8), (3, 0.6), (4, 0.45), (6, 0.3), (8, 0.25)]
    let organ2: [(Int, Double)] = [(1, 1.0), (2, 0.7), (3, 0.5), (4, 0.5), (5, 0.25), (6, 0.35), (8, 0.3), (10, 0.12),
                                   (12, 0.15), (16, 0.1)]
    let flute: [(Int, Double)] = [(1, 1.0), (2, 0.3), (3, 0.12), (4, 0.05)]
    let sets = [[], organ, organ2, flute]
    var v = [Double](repeating: 0, count: mTab)
    for t in 0..<4 {
      var m = 1e-9
      for i in 0..<mTab {
        var x = 0.0
        if t == 0 {
          x = sin(mTAU * Double(i) / Double(mTab))
        } else {
          for h in sets[t] { x += h.1 * sin(mTAU * Double(h.0) * Double(i) / Double(mTab)) }
        }
        v[i] = x
        m = max(m, abs(x))
      }
      let tab = tabs + t * (mTab + 2)
      for i in 0..<mTab { tab[i] = Float(v[i] / m) }
      tab[mTab] = tab[0]
      tab[mTab + 1] = tab[1]
    }
    return tabs
  }
  @inline(__always) private func table(_ wave: Int) -> UnsafeMutablePointer<Float> { tabs + (wave - wSin) * (mTab + 2) }
  @inline(__always) private func sinT(_ ph: Float) -> Float { lookup(tabs, ph) }

  // ---- voices ----------------------------------------------------------
  private func allocVoice() -> Int {
    for i in 0..<mVoices where voices[i].patch < 0 { return i }
    var best = -1
    for i in 0..<mVoices where voices[i].stage == 2 && (best < 0 || voices[i].env < voices[best].env) {
      best = i  // the quietest released note
    }
    if best >= 0 { return best }
    best = 0
    for i in 1..<mVoices where voices[i].age > voices[best].age { best = i }
    return best
  }

  private mutating func noteOn(_ pi: Int, _ note: Int, _ vel: Float, _ len: Int, _ T: MTrack) {
    let vi = allocVoice()
    let P = patches[pi]
    let ph = phs + vi * 6, inc = incs + vi * 6, det = dets + vi * 3, fx = svfs + vi * 4 + 1
    var v = MVoice()
    for k in 0..<6 {
      ph[k] = 0
      inc[k] = 0
    }
    for k in 0..<3 { det[k] = 1 }
    for k in 0..<4 { svfs[vi * 4 + k] = MSvf() }
    v.patch = pi
    v.gateAt = max(1, len)
    v.freq = note > 0 ? mtof(note) : P.f
    v.amp = vel * T.vol * P.gain
    let pan = min(1, max(-1, P.pan + T.pan))
    let an = (pan + 1) * 0.25 * mPI
    v.gl = cos(an)
    v.gr = sin(an)
    v.rev = T.rev
    v.del = T.del
    v.rng.s = noise.u32() | 1
    v.aInc = 1 / (max(P.a, 0.0005) * Float(mSR))
    // decay and release times are to -60 dB
    v.dCoef = exp(-6.9078 / (max(P.d, 0.001) * Float(mSR)))
    v.rCoef = exp(-6.9078 / (max(P.r, 0.001) * Float(mSR)))
    if P.kind == kSynth {
      if P.uni == 2 {
        det[0] = exp2(P.det / 2400)
        det[1] = exp2(-P.det / 2400)
      } else if P.uni == 3 {
        det[1] = exp2(P.det / 1200)
        det[2] = exp2(-P.det / 1200)
      }
      if P.uni > 1 {
        for u in 0..<P.uni { ph[u] = Double(v.rng.next()) }
      }
      if P.filt == fVox {
        fx[0].set(730, 0.88)
        fx[1].set(1090, 0.9)
        fx[2].set(2440, 0.93)
      }
    } else if P.kind == kKS {
      // the string's shape when plucked (a triangle, sharper the brighter) and a little noise
      let buf = ks + vi * mKS
      v.ksN = min(max(Float(mSR) / v.freq - P.damp, 2), Float(mKS - 3))
      let m = Int(v.ksN) + 2
      var e: Float = 0, mean: Float = 0, peak: Float = 1e-6
      let at: Float = 0.1 + 0.3 * (1 - P.bright)
      for i in 0..<m {
        let x = Float(i) / Float(m)
        e += P.bright * (v.rng.bi() - e)
        buf[i] = (x < at ? x / at : (1 - x) / (1 - at)) + 0.35 * e
        mean += buf[i]
      }
      mean /= Float(m)
      for i in 0..<m {
        buf[i] -= mean
        peak = max(peak, abs(buf[i]))
      }
      for i in 0..<m { buf[i] /= peak }
      v.ksW = m
      v.ksG = exp(-6.9078 / (v.freq * max(P.decay, 0.01)))
    } else if P.kind == kDrum {
      v.dAmp = 1
      v.dCoef2 = v.dCoef
      v.nAmp = P.snap
      v.nCoef = exp(-1 / (max(P.ndec, 0.001) * Float(mSR)))
      if P.wave == dMembrane {
        fx[0].set(P.nf, 0.1)
      } else if P.wave == dSnare {
        fx[0].set(P.nf, 0.2)
        v.e1 = P.tone
        v.c1 = exp(-1 / (0.055 * Float(mSR)))
      } else if P.wave == dClap {
        fx[0].set(P.nf, 0.5)
        v.e1 = 1
        v.c1 = exp(-1 / (0.0045 * Float(mSR)))
        v.c2 = v.dCoef2
      } else if P.wave == dMetal {
        for k in 0..<6 {
          inc[k] = Double(metalFrequency(k) * (P.f / 100)) / Double(mSR)
          ph[k] = Double(v.rng.next())
        }
        fx[0].set(P.nf, 0.3)
      } else {
        fx[0].set(P.nf, 0.3)
      }
    }
    voices[vi] = v
  }

  /// Control-rate updates, every 32 samples of a voice's life.
  @inline(__always) private func control(_ v: inout MVoice, _ P: MPatch, _ vi: Int) {
    let t = Float(v.age) / Float(mSR)
    let inc = incs + vi * 6
    if P.kind == kDrum {
      if P.wave == dMembrane {
        let f = v.freq * (1 + P.pamt * exp(-t / P.pdec))
        inc[0] = Double(f) / Double(mSR)
        inc[1] = Double(f * P.p2) / Double(mSR)
      } else if P.wave == dSnare {
        let f = v.freq * (1 + 0.15 * exp(-t / 0.02))
        inc[0] = Double(f) / Double(mSR)
        inc[1] = Double(f * 1.84) / Double(mSR)
      }
      return
    }
    var semis: Float = 0
    if P.vibDepth > 0 {
      let ramp = min(1, max(0, (t - P.vibDelay) / 0.3))
      semis += P.vibDepth * ramp * sinT(frac(P.vibRate * t))
    }
    if P.scoop > 0 { semis -= P.scoop * exp(-t / 0.05) }
    let pm = exp2(semis / 12)
    let base = Double(v.freq * pm) / Double(mSR)
    let det = dets + vi * 3
    for u in 0..<3 { inc[u] = base * Double(det[u]) }
    if P.filt == fLP || P.filt == fBP {
      let fe = (P.fatk > 0 && t < P.fatk) ? t / P.fatk : exp(-(t - P.fatk) / P.fdec)
      svfs[vi * 4].set((P.hz + P.cut * v.freq) * exp2(P.fenv * fe), P.res)
    }
    if P.tremDepth > 0 { v.trem = 1 - P.tremDepth * (0.5 + 0.5 * sinT(frac(P.tremRate * t))) }
    if P.kind == kFM {
      v.idx = P.index * exp(-t / P.idec) / Float(mTAU)
      v.p2g = P.p2amp * exp(-t / P.p2dec)
    }
  }

  /// The amplitude envelope; false when the note has died away.
  @inline(__always) private func envelope(_ v: inout MVoice, _ P: MPatch) -> Bool {
    if v.age == v.gateAt && v.stage < 2 { v.stage = 2 }
    if v.stage == 0 {
      v.env += v.aInc
      if v.env >= 1 {
        v.env = 1
        v.stage = 1
      }
    } else if v.stage == 1 {
      v.env = P.s + (v.env - P.s) * v.dCoef
      if P.s <= 0 && v.env < 1e-3 { return false }
    } else {
      v.env *= v.rCoef
      if v.env < 1e-3 { return false }
    }
    return true
  }

  @inline(__always) private func filter(_ vi: Int, _ P: MPatch, _ x: Float) -> Float {
    var lp: Float = 0, bp: Float = 0
    let f = svfs + vi * 4
    switch P.filt {
    case fLP:
      f[0].run(x, &lp, &bp)
      return lp
    case fBP:
      f[0].run(x, &lp, &bp)
      return bp
    case fVox:
      var y = x * 0.04
      let g: (Float, Float, Float) = (1, 0.55, 0.25)
      f[1].run(x, &lp, &bp)
      y += bp * f[1].k * g.0
      f[2].run(x, &lp, &bp)
      y += bp * f[2].k * g.1
      f[3].run(x, &lp, &bp)
      y += bp * f[3].k * g.2
      return y * 2
    default:
      return x
    }
  }

  @inline(__always) private func out(_ v: MVoice, _ i: Int, _ y: Float) {
    dryL[i] += y * v.gl
    dryR[i] += y * v.gr
    revIn[i] += y * v.rev
    delIn[i] += y * v.del
  }

  private mutating func runVoice(_ vi: Int, _ i0: Int, _ n: Int) {
    var v = voices[vi]
    let P = patches[v.patch]
    let ph = phs + vi * 6, inc = incs + vi * 6
    let fx0 = svfs + vi * 4 + 1
    let uniNorm: Float = P.uni == 3 ? 0.6 : P.uni == 2 ? 0.75 : 1
    var k = 0
    while k < n {
      if (v.age & 31) == 0 { control(&v, P, vi) }
      var x: Float = 0
      var alive = true
      if P.kind == kDrum {
        var y: Float = 0
        var lp: Float = 0, bp: Float = 0
        switch P.wave {
        case dMembrane:
          ph[0] += inc[0]
          if ph[0] >= 1 { ph[0] -= 1 }
          y = sinT(Float(ph[0]))
          if P.p2amp > 0 {
            ph[1] += inc[1]
            if ph[1] >= 1 { ph[1] -= 1 }
            y += P.p2amp * sinT(Float(ph[1]))
          }
          y *= v.dAmp
          fx0.pointee.run(v.rng.bi(), &lp, &bp)
          y += lp * v.nAmp
          v.dAmp *= v.dCoef2
          v.nAmp *= v.nCoef
          if P.drive > 0 { y = tanh(y * P.drive) }
          alive = v.dAmp > 1e-3 || v.nAmp > 1e-3
        case dSnare:
          for q in 0..<2 {
            ph[q] += inc[q]
            if ph[q] >= 1 { ph[q] -= 1 }
          }
          let tone = (sinT(Float(ph[0])) + 0.5 * sinT(Float(ph[1]))) * v.e1
          let xin = v.rng.bi()
          fx0.pointee.run(xin, &lp, &bp)
          y = tone + (xin - fx0.pointee.k * bp - lp) * v.nAmp  // the snares: high-passed noise
          v.e1 *= v.c1
          v.nAmp *= v.dCoef2
          alive = v.e1 > 1e-3 || v.nAmp > 1e-3
        case dClap:
          if v.age == 441 || v.age == 882 { v.e1 = 1 }
          if v.age == 1323 { v.e2 = 0.85 }
          fx0.pointee.run(v.rng.bi(), &lp, &bp)
          y = bp * (v.e1 + v.e2) * 2.5
          v.e1 *= v.c1
          v.e2 *= v.c2
          alive = v.age < 1323 || v.e2 > 1e-3
        case dMetal:
          var m: Float = 0
          for q in 0..<6 {
            ph[q] += inc[q]
            if ph[q] >= 1 { ph[q] -= 1 }
            m += ph[q] < 0.5 ? 1 : -1
          }
          let xin = P.metal * m * (1 / 6) + (1 - P.metal) * v.rng.bi()
          fx0.pointee.run(xin, &lp, &bp)
          let hp = xin - fx0.pointee.k * bp - lp
          let att: Float = P.a > 0.001 && v.age < Int(P.a * Float(mSR)) ? Float(v.age) / (P.a * Float(mSR)) : 1
          y = hp * v.dAmp * att
          v.dAmp *= v.dCoef2
          alive = v.dAmp > 1e-3
        default:  // shaker
          fx0.pointee.run(v.rng.bi(), &lp, &bp)
          let att: Float = v.age < Int(P.a * Float(mSR)) ? Float(v.age) / (P.a * Float(mSR)) : 1
          y = bp * v.dAmp * att * 1.5
          if v.age >= Int(P.a * Float(mSR)) { v.dAmp *= v.dCoef2 }
          alive = v.dAmp > 1e-3
        }
        if v.age > 6 * mSR { alive = false }
        out(v, i0 + k, y * v.amp)
      } else {
        alive = envelope(&v, P)
        if P.kind == kSynth {
          if P.wave <= wTri {
            for u in 0..<P.uni {
              let dt = inc[u]
              ph[u] += dt
              if ph[u] >= 1 { ph[u] -= 1 }
              let p = ph[u]
              if P.wave == wSaw {
                x += Float(2 * p - 1) - polyblep(p, dt)
              } else if P.wave == wSqr {
                var p2 = p + (1 - Double(P.pw))
                if p2 >= 1 { p2 -= 1 }
                x += (p < Double(P.pw) ? 1 : -1) + polyblep(p, dt) - polyblep(p2, dt) - (2 * P.pw - 1)
              } else {
                x += Float(4 * abs(p - 0.5) - 1)
              }
            }
          } else {
            let tab = table(P.wave)
            for u in 0..<P.uni {
              ph[u] += inc[u]
              if ph[u] >= 1 { ph[u] -= 1 }
              x += lookup(tab, Float(ph[u]))
            }
          }
          x *= uniNorm
          if P.sub > 0 {
            v.phSub += inc[0] * 0.5
            if v.phSub >= 1 { v.phSub -= 1 }
            x += P.sub * sinT(Float(v.phSub))
          }
          if P.noise > 0 { x += P.noise * v.rng.bi() }
        } else if P.kind == kFM {
          v.phM += inc[0] * Double(P.ratio)
          if v.phM >= 1 { v.phM -= 1 }
          ph[0] += inc[0]
          if ph[0] >= 1 { ph[0] -= 1 }
          x = sinT(frac(Float(ph[0]) + sinT(Float(v.phM)) * v.idx))
          if P.p2amp > 0 {
            v.ph2 += inc[0] * Double(P.p2)
            if v.ph2 >= 1 { v.ph2 -= 1 }
            x += v.p2g * sinT(Float(v.ph2))
          }
        } else {  // plucked string
          let buf = ks + vi * mKS
          var rp = Float(v.ksW) - v.ksN
          if rp < 0 { rp += Float(mKS) }
          let a = Int(rp)
          let fr = rp - Float(a)
          let b = (a + 1) & (mKS - 1)
          let s = buf[a] + (buf[b] - buf[a]) * fr
          buf[v.ksW] = v.ksG * ((1 - P.damp) * s + P.damp * v.ksPrev)
          v.ksPrev = s
          v.ksW = (v.ksW + 1) & (mKS - 1)
          x = s
        }
        if P.drive > 0 { x = tanh(x * P.drive) }
        x = filter(vi, P, x)
        out(v, i0 + k, x * v.env * v.amp * v.trem)
      }
      v.age += 1
      if !alive {
        v.patch = -1
        break
      }
      k += 1
    }
    voices[vi] = v
  }

  // ---- the sequencer ---------------------------------------------------
  private mutating func randomSong(_ piece: Int, _ avoid: Int) -> Int {
    var n = 0
    for i in 0..<songCount where songs[i].piece == piece { n += 1 }
    if n == 0 { return -1 }
    let choices = n > 1 && avoid >= 0 && songs[avoid].piece == piece ? n - 1 : n
    var k = Int(pick.u32() % UInt32(choices))
    for i in 0..<songCount {
      if songs[i].piece != piece || (choices < n && i == avoid) { continue }
      if k == 0 { return i }
      k -= 1
    }
    return -1
  }

  private func hasSongs(_ piece: Int) -> Bool {
    for i in 0..<songCount where songs[i].piece == piece { return true }
    return false
  }

  private func releaseAll() {
    for i in 0..<mVoices where voices[i].patch >= 0 && patches[voices[i].patch].kind != kDrum && voices[i].stage < 2 {
      voices[i].stage = 2
    }
  }

  private mutating func startSong(_ s: Int, _ bar: Int, _ mark: Bool) {
    releaseAll()
    cur = s
    let S = songs[s]
    curBar = min(max(bar, 0), S.bars - 1)
    pos = stepSample(S, curBar * S.bar)
    barEnd = stepSample(S, (curBar + 1) * S.bar)
    ev = S.first
    evEnd = S.first + S.count
    while ev < evEnd && events[ev].t < pos { ev += 1 }
    since = 0
    songGain = S.gain
    echo.len = S.echoLen
    lastSong[S.piece] = s
    pieceSong[S.piece] = s
    tail = 4 * mSR
    if mark && crash >= 0 {
      var T = MTrack()
      T.rev = patches[crash].rev
      noteOn(crash, 0, 0.5, 1, T)
    }
  }

  /// The piece's theme where it left off (or a new one); false if it has none.
  @discardableResult private mutating func startPiece(_ p: Int) -> Bool {
    var s = pieceSong[p]
    if s < 0 {
      s = randomSong(p, lastSong[p])
      pieceBar[p] = 0
    }
    if s < 0 { return false }
    let mark = cur >= 0 && songs[cur].piece != p
    startSong(s, pieceBar[p], mark)
    return true
  }

  private mutating func startNow(_ p: Int) {
    fade = 1
    fadeTarget = 1
    startPiece(p)
  }

  /// A bar line or an event is due at `pos`.
  private mutating func boundary() {
    if pos >= barEnd {
      curBar += 1
      let S = songs[cur]
      let atEnd = curBar >= S.bars
      if single && atEnd {
        cur = -1
        return
      }
      // a new theme waits for the bar line, and stays for at least two bars
      let dwelt = since >= 2 * Int(Double(S.bar) * S.sps)
      let p = pending
      let from = S.piece, fromBar = curBar
      if p >= 0 && p != from && (atEnd || dwelt) && hasSongs(p) {
        pending = -1
        pieceBar[from] = atEnd ? 0 : fromBar
        if atEnd { pieceSong[from] = -1 }
        startPiece(p)
      } else if atEnd {
        let next = randomSong(from, cur)
        pieceBar[from] = 0
        startSong(next, 0, false)
      } else {
        barEnd = stepSample(S, (curBar + 1) * S.bar)
      }
    }
    while ev < evEnd && events[ev].t <= pos {
      let e = events[ev]
      ev += 1
      let T = tracks[e.track]
      noteOn(T.patch, e.note, e.vel, e.len, T)
    }
  }

  private mutating func halt() {
    if cur >= 0 { pieceBar[songs[cur].piece] = curBar }
    cur = -1
    pending = -1
    for i in 0..<mVoices { voices[i].patch = -1 }
    reverb.clear()
    echo.clear()
    dcL = 0
    dcR = 0
    dcOutL = 0
    dcOutR = 0
    tail = 0
  }

  /// Audio thread: adds the music to `frames` stereo samples (left and right `step` floats apart).
  /// False when it is silent and added nothing.
  @discardableResult
  mutating func render(_ left: UnsafeMutablePointer<Float>, _ right: UnsafeMutablePointer<Float>, _ frames: Int,
                       _ step: Int) -> Bool {
    var done = 0, sounded = false
    while done < frames {
      let n = min(frames - done, mBlock)
      if block(left + done * step, right + done * step, n, step) { sounded = true }
      done += n
    }
    return sounded
  }

  private mutating func block(_ L: UnsafeMutablePointer<Float>, _ R: UnsafeMutablePointer<Float>, _ n: Int,
                              _ step: Int) -> Bool {
    var any = cur >= 0
    for i in 0..<mVoices where voices[i].patch >= 0 { any = true }
    if !any && tail <= 0 { return false }
    for i in 0..<n {
      dryL[i] = 0
      dryR[i] = 0
      revIn[i] = 0
      delIn[i] = 0
    }
    var i = 0
    while i < n {
      var m = n - i
      if cur >= 0 {
        boundary()
        if cur >= 0 {
          var next = barEnd
          if ev < evEnd { next = min(next, events[ev].t) }
          if next - pos < m { m = next - pos }
        }
      }
      for vi in 0..<mVoices where voices[vi].patch >= 0 { runVoice(vi, i, m) }
      if cur >= 0 {
        pos += m
        since += m
      }
      i += m
    }
    var sounding = cur >= 0
    for vi in 0..<mVoices where voices[vi].patch >= 0 { sounding = true }
    if sounding {
      tail = 4 * mSR
    } else {
      tail -= n
    }
    for k in 0..<n {
      var rl: Float = 0, rr: Float = 0, el: Float = 0, er: Float = 0
      reverb.run(revIn[k], &rl, &rr)
      echo.run(delIn[k], &el, &er)
      if fade < fadeTarget {
        fade = min(fadeTarget, fade + fadeStep)
      } else if fade > fadeTarget {
        fade = max(fadeTarget, fade - fadeStep)
      }
      var target: Float = 1
      if duckHold > 0 {
        duckHold -= 1
        target = duckDepth
      }
      duckGain += (target - duckGain) * (target < duckGain ? 0.002 : 0.00012)
      volS += (volume - volS) * 0.002
      songGainS += (songGain - songGainS) * 0.002
      let g = volS * songGainS * fade * duckGain * mLevel
      let xl = (dryL[k] + rl + el) * g, xr = (dryR[k] + rr + er) * g
      dcOutL = xl - dcL + 0.9995 * dcOutL
      dcOutR = xr - dcR + 0.9995 * dcOutR
      dcL = xl
      dcR = xr
      L[k * step] += dcOutL
      R[k * step] += dcOutR
    }
    if cur >= 0 && fadeTarget <= 0 && fade <= 0 { halt() }
    return true
  }

  // ---- control, from the game (through the audio thread) --------------
  /// This piece's music (0 pawn .. 5 king), from the next bar line.
  mutating func play(_ piece: Int) {
    if piece < 0 || piece >= mPieces { return }
    want = piece
    if !enabled { return }
    if cur < 0 {
      startNow(piece)
    } else {
      fadeTarget = 1
      pending = songs[cur].piece == piece ? -1 : piece
    }
  }

  /// Starts again after stop(), with a random piece's music.
  mutating func resume() {
    if !enabled { return }
    if cur >= 0 {
      fadeTarget = 1
      return
    }
    let p = Int(pick.u32() % UInt32(mPieces))  // a new game: any piece's music
    want = p
    startNow(p)
  }

  /// Fades out (the game is over).
  mutating func stop() {
    pending = -1
    if cur >= 0 {
      fadeTarget = 0
      fadeStep = 1 / (2.5 * Float(mSR))
    }
  }

  mutating func setEnabled(_ on: Bool) {
    if on == enabled { return }
    enabled = on
    if !on {
      pending = -1
      if cur >= 0 {
        fadeTarget = 0
        fadeStep = 1 / (0.4 * Float(mSR))
      }
    } else if cur >= 0 {
      fadeTarget = 1
    } else {
      let p = want >= 0 ? want : Int(pick.u32() % UInt32(mPieces))
      want = p
      startNow(p)
    }
  }

  mutating func setVolume(_ v: Float) { volume = min(1, max(0, v)) }

  /// Turned down to `depth` while a musical sound effect plays.
  mutating func duck(_ samples: Int, _ depth: Float) {
    if depth >= 1 || samples <= 0 { return }
    if duckHold <= 0 || depth < duckDepth { duckDepth = depth }
    duckHold = max(duckHold, samples)
  }

  mutating func seed(_ s: UInt32) { pick.s = s != 0 ? s : 1 }

  /// The song being played, or -1.
  func playing() -> Int { fadeTarget > 0 ? cur : -1 }

  /// Plays one song through on its own (and its echo), as interleaved stereo; for --dump-sfx and tests.
  static func renderSong(_ score: MusicScore, _ s: Int) -> [Float] {
    var one = MusicEngine(score)
    one.single = true
    one.volume = 1
    one.volS = 1
    one.startSong(s, 0, false)
    one.songGainS = one.songGain
    let n = one.songs[s].len + 3 * mSR
    var out = [Float](repeating: 0, count: n * 2)
    out.withUnsafeMutableBufferPointer { b in
      var i = 0
      while i < n {
        let m = min(4096, n - i)
        one.render(b.baseAddress! + i * 2, b.baseAddress! + i * 2 + 1, m, 2)
        i += m
      }
    }
    one.free()
    return out
  }
}
