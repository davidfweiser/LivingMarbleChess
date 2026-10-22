// Procedurally synthesised sound effects (no audio files), mixed in real time.
// Every effect is built once at start-up from damped sines, filtered noise and
// a few oscillators (the medieval army's organ, choir, thunder and the rest, the
// aliens' ray guns, saucers and cows, and the ghosts' cheer, are made
// on a background thread right after). An AVAudioSourceNode mixes up to 40 voices (pan, pitch via
// resampling, volume) and the background music (Music.swift) at 44.1 kHz, and AVAudioEngine
// converts to the device rate.
import AVFoundation
#if os(macOS)
import CoreAudio
#endif
import Foundation
import os

// Helper types are nested in Audio so they cannot clash with other files.
extension Audio {
  // MARK: - Synthesis

  fileprivate enum Synth {
    static let SR = 44100
    static let TAU = 6.283185307179586

    /// xorshift32 with the C++ seed, so the noise is sample-identical.
    struct XorShift32 {
      var s: UInt32 = 12345
      mutating func next() -> Float {
        s ^= s &<< 13
        s ^= s &>> 17
        s ^= s &<< 5
        return Float(s & 0xffffff) / Float(0xffffff)
      }
      mutating func bi() -> Float { next() * 2 - 1 }
    }

    struct Biquad {
      var b0: Float = 1, b1: Float = 0, b2: Float = 0, a1: Float = 0, a2: Float = 0, z1: Float = 0, z2: Float = 0
      mutating func bandpass(_ f: Float, _ q: Float) {
        let w = Float(TAU * Double(f) / Double(SR)), al = sin(w) / (2 * q), c = cos(w)
        let a0 = 1 + al
        b0 = al / a0
        b1 = 0
        b2 = -al / a0
        a1 = -2 * c / a0
        a2 = (1 - al) / a0
      }
      mutating func lowpass(_ f: Float, _ q: Float = 0.707) {
        let w = Float(TAU * Double(f) / Double(SR)), al = sin(w) / (2 * q), c = cos(w)
        let a0 = 1 + al
        b0 = (1 - c) / 2 / a0
        b1 = (1 - c) / a0
        b2 = (1 - c) / 2 / a0
        a1 = -2 * c / a0
        a2 = (1 - al) / a0
      }
      mutating func highpass(_ f: Float, _ q: Float = 0.707) {
        let w = Float(TAU * Double(f) / Double(SR)), al = sin(w) / (2 * q), c = cos(w)
        let a0 = 1 + al
        b0 = (1 + c) / 2 / a0
        b1 = -(1 + c) / a0
        b2 = (1 + c) / 2 / a0
        a1 = -2 * c / a0
        a2 = (1 - al) / a0
      }
      mutating func run(_ x: Float) -> Float {
        let y = b0 * x + z1
        z1 = b1 * x - a1 * y + z2
        z2 = b2 * x - a2 * y
        return y
      }
    }

    typealias Buf = [Float]

    static func make(_ seconds: Float) -> Buf { Buf(repeating: 0, count: Int(seconds * Float(SR))) }

    static func normalize(_ b: inout Buf, _ peak: Float) {
      var m: Float = 1e-6
      for v in b { m = max(m, abs(v)) }
      for i in b.indices { b[i] *= peak / m }
      // tiny fade-out to avoid clicks
      let n = min(b.count, 256)
      for i in 0..<n { b[b.count - 1 - i] *= Float(i) / Float(n) }
    }

    static func addDampedSine(_ b: inout Buf, _ t0: Float, _ f: Float, _ amp: Float, _ decay: Float,
                              _ sweep: Float = 0) {
      let s0 = Int(t0 * Float(SR))
      var ph = 0.0
      var i = s0
      while i < b.count {
        let t = Float(i - s0) / Float(SR)
        let env = amp * exp(-t * decay)
        if env < 1e-5 { break }
        let ff = f * exp(-sweep * t)
        ph += TAU * Double(ff) / Double(SR)
        b[i] += env * Float(sin(ph))
        i += 1
      }
    }

    static func addNoise(_ b: inout Buf, _ r: inout XorShift32, _ t0: Float, _ amp: Float, _ decay: Float,
                         _ flt: Biquad, _ attack: Float = 0.001) {
      var flt = flt  // by value, like the C++
      let s0 = Int(t0 * Float(SR))
      var i = s0
      while i < b.count {
        let t = Float(i - s0) / Float(SR)
        let env = amp * exp(-t * decay) * min(1, t / attack)
        if t > 0.01 && env < 1e-5 { break }
        b[i] += env * flt.run(r.bi())
        i += 1
      }
    }

    /// All effects, indexed by Sfx.rawValue. The RNG is shared and consumed in
    /// the same order as the C++ (clang evaluates call arguments left to right).
    static func synthAll() -> [Buf] {
      var S = [Buf](repeating: [], count: Sfx.allCases.count)
      var r = XorShift32()
      do {  // STEP
        var b = make(0.14)
        addDampedSine(&b, 0, 150, 1.0, 38, 3.0)
        var f = Biquad()
        f.bandpass(1600, 1.2)
        addNoise(&b, &r, 0, 0.9, 90, f)
        normalize(&b, 0.7)
        S[Sfx.step.rawValue] = b
      }
      do {  // THUD
        var b = make(0.45)
        addDampedSine(&b, 0, 78, 1.0, 11, 1.6)
        var f = Biquad()
        f.lowpass(420)
        addNoise(&b, &r, 0, 1.4, 16, f)
        var c = Biquad()
        c.bandpass(1800, 1.0)
        addNoise(&b, &r, 0, 0.4, 120, c)
        normalize(&b, 0.9)
        S[Sfx.thud.rawValue] = b
      }
      do {  // CLACK
        var b = make(0.3)
        addDampedSine(&b, 0, 1850, 0.8, 38)
        addDampedSine(&b, 0, 2930, 0.6, 52)
        addDampedSine(&b, 0, 4150, 0.4, 70)
        addDampedSine(&b, 0, 640, 0.5, 30)
        var f = Biquad()
        f.bandpass(3000, 0.8)
        addNoise(&b, &r, 0, 1.0, 140, f)
        normalize(&b, 0.9)
        S[Sfx.clack.rawValue] = b
      }
      do {  // SHATTER
        var b = make(1.4)
        var f = Biquad()
        f.lowpass(2500)
        addNoise(&b, &r, 0, 1.2, 9, f)
        addDampedSine(&b, 0, 95, 0.8, 14, 1.0)
        for _ in 0..<90 {
          let t = -log(1 - r.next() * 0.995) * 0.16
          let freq = 1800 + r.next() * 5500
          let amp = 0.25 + 0.35 * r.next()
          let decay = 120 + r.next() * 120
          addDampedSine(&b, t, freq, amp, decay)
        }
        normalize(&b, 0.95)
        S[Sfx.shatter.rawValue] = b
      }
      do {  // WHOOSH
        var b = make(0.5)
        var f = Biquad()
        for i in b.indices {
          let t = Float(i) / Float(b.count)
          let s = sin(t * 3.14159)
          let fc = 450 + 2200 * s
          if i % 32 == 0 { f.bandpass(fc, 1.6) }
          let env = s * s  // pow(s, 2), which clang emits as s * s
          b[i] = f.run(r.bi()) * env
        }
        normalize(&b, 0.8)
        S[Sfx.whoosh.rawValue] = b
      }
      do {  // CLANG
        var b = make(2.2)
        let ratio: [Float] = [1.0, 2.76, 5.40, 8.93, 13.34, 1.5]
        let amp: [Float] = [1.0, 0.6, 0.45, 0.28, 0.16, 0.3]
        let dec: [Float] = [2.5, 3.5, 5.0, 7.0, 10.0, 4.0]
        for i in 0..<6 { addDampedSine(&b, 0, 455 * ratio[i], amp[i], dec[i]) }
        var f = Biquad()
        f.highpass(2500)
        addNoise(&b, &r, 0, 1.5, 60, f)
        normalize(&b, 0.9)
        S[Sfx.clang.rawValue] = b
      }
      do {  // BOOM
        var b = make(2.4)
        addDampedSine(&b, 0, 85, 1.0, 2.6, 0.9)
        var f = Biquad()
        f.lowpass(320)
        addNoise(&b, &r, 0, 2.2, 3.2, f)
        var c = Biquad()
        c.highpass(1500)
        addNoise(&b, &r, 0, 1.0, 40, c)
        normalize(&b, 1.0)
        S[Sfx.boom.rawValue] = b
      }
      do {  // EXPLODE
        var b = make(2.6)
        var f = Biquad()
        for i in b.indices {
          let t = Float(i) / Float(SR)
          if i % 64 == 0 { f.lowpass(200 + 1400 * exp(-t * 3)) }
          b[i] = f.run(r.bi()) * 2 * exp(-t * 2.8) * min(1, t / 0.004)
        }
        addDampedSine(&b, 0, 55, 0.9, 4.0, 0.6)
        for _ in 0..<40 {
          let t = r.next() * 1.2
          let freq = 1500 + r.next() * 4000
          addDampedSine(&b, t, freq, 0.15 * exp(-t * 2), 150)
        }
        normalize(&b, 1.0)
        S[Sfx.explode.rawValue] = b
      }
      do {  // CHARGE
        var b = make(1.3)
        var ph = 0.0, pm = 0.0
        for i in b.indices {
          let t = Float(i) / Float(SR)
          let u = t / 1.3
          let fc: Float = 260 + 780 * u * u
          pm += TAU * Double(fc) * 1.5 / Double(SR)
          let idx: Float = 2.5 * (1 - u)
          ph += TAU * Double(fc) / Double(SR) + Double(idx) * sin(pm) * TAU * 3.0 / Double(SR)
          let env: Float = min(1, t / 0.3) * (1 - pow(u, 6))
          let trem: Float = 0.75 + 0.25 * sin(t * 30 * (1 + u))
          let osc: Double = sin(ph) + 0.35 * sin(ph * 2.01) + 0.2 * sin(ph * 3.02)
          b[i] = Float(osc) * env * trem
        }
        normalize(&b, 0.6)
        S[Sfx.charge.rawValue] = b
      }
      do {  // ZAP
        var b = make(0.6)
        var ph = 0.0
        for i in b.indices {
          let t = Float(i) / Float(SR)
          let freq: Float = 300 + 1700 * exp(-t * 9)
          ph += TAU * Double(freq) / Double(SR)
          let sq: Float = sin(ph) > 0 ? 1 : -1
          let x: Float = 0.6 * Float(sin(ph)) + 0.25 * sq + 0.3 * r.bi()
          b[i] = x * exp(-t * 6)
        }
        var f = Biquad()
        f.lowpass(5000)
        for i in b.indices { b[i] = f.run(b[i]) }
        normalize(&b, 0.75)
        S[Sfx.zap.rawValue] = b
      }
      do {  // CRACKLE
        var b = make(0.5)
        var f = Biquad()
        f.bandpass(4200, 0.7)
        for i in b.indices {
          let t = Float(i) / Float(SR)
          let x = r.next() < 0.03 ? r.bi() * 3 : r.bi() * 0.15
          let buzz: Float = sin(Double(t) * TAU * 120.0) > 0 ? 0.15 : -0.15
          b[i] = (f.run(x) + buzz * 0.4) * exp(-t * 5)
        }
        normalize(&b, 0.7)
        S[Sfx.crackle.rawValue] = b
      }
      do {  // CHIME
        var b = make(2.0)
        let notes: [Float] = [1046.5, 1318.5, 1568.0]
        for n in 0..<3 {
          let t0 = Float(n) * 0.09
          addDampedSine(&b, t0, notes[n], 0.6, 2.6)
          addDampedSine(&b, t0, notes[n] * 2.41, 0.25, 4.0)
          addDampedSine(&b, t0, notes[n] * 3.89, 0.12, 6.0)
        }
        normalize(&b, 0.6)
        S[Sfx.chime.rawValue] = b
      }
      do {  // SELECT
        var b = make(0.09)
        addDampedSine(&b, 0, 880, 0.8, 80)
        addDampedSine(&b, 0, 1720, 0.4, 110)
        normalize(&b, 0.45)
        S[Sfx.select.rawValue] = b
      }
      do {  // CHECK (two dramatic horn notes)
        var b = make(1.0)
        var f = Biquad()
        f.lowpass(1500)
        var ph = 0.0
        for i in b.indices {
          let t = Float(i) / Float(SR)
          let freq: Float = t < 0.28 ? 392 : 523.25
          let lt: Float = t < 0.28 ? t : t - 0.28
          let len: Float = t < 0.28 ? 0.28 : 0.72
          let env = min(1, lt / 0.02) * max(0, 1 - lt / len)
          ph += TAU * Double(freq) * (1.0 + 0.004 * sin(Double(t) * 30.0)) / Double(SR)
          let saw = Float(2.0 * (ph / TAU - floor(ph / TAU + 0.5)))
          b[i] = f.run(saw) * env
        }
        normalize(&b, 0.55)
        S[Sfx.check.rawValue] = b
      }
      do {  // MATE (gong)
        var b = make(4.5)
        let ratio: [Float] = [1.0, 1.47, 2.09, 2.56, 3.11, 4.23]
        let dec: [Float] = [0.7, 0.9, 1.2, 1.5, 1.9, 2.6]
        for i in 0..<6 { addDampedSine(&b, 0, 92 * ratio[i], 1 / (1 + Float(i) * 0.4), dec[i]) }
        var f = Biquad()
        f.lowpass(900)
        addNoise(&b, &r, 0, 0.8, 8, f)
        normalize(&b, 0.9)
        S[Sfx.mate.rawValue] = b
      }
      do {  // DEBRIS
        var b = make(0.16)
        addDampedSine(&b, 0, 2300, 0.7, 70)
        addDampedSine(&b, 0, 3650, 0.4, 90)
        addDampedSine(&b, 0, 900, 0.3, 60)
        var f = Biquad()
        f.bandpass(2800, 1.0)
        addNoise(&b, &r, 0, 0.5, 150, f)
        normalize(&b, 0.6)
        S[Sfx.debris.rawValue] = b
      }
      do {  // RUMBLE
        var b = make(1.6)
        var f = Biquad()
        f.lowpass(140)
        for i in b.indices {
          let t = Float(i) / Float(SR)
          let env = sin(min(1, t / 1.6) * 3.14159)
          let x: Float = f.run(r.bi()) * 3 + 0.4 * Float(sin(TAU * 42.0 * Double(t)))
          b[i] = x * env * (0.8 + 0.2 * sin(t * 23))
        }
        normalize(&b, 0.8)
        S[Sfx.rumble.rawValue] = b
      }
      return S
    }

    /// Every effect, the medieval army's, the aliens' and the ghosts' included (for the .wav dump).
    static func synthEverything() -> [Buf] {
      var S = synthAll()
      synthMedieval(&S)
      synthAlien(&S)
      synthGhosts(&S)
      return S
    }

    // MARK: The medieval army's sounds
    // Float/double mixing follows the C++ exactly (a float literal in a double
    // expression is promoted, hence fd(0.6) for 0.6f).

    @inline(__always) static func fd(_ x: Float) -> Double { Double(x) }

    /// -1..1 ramp of a phase in radians.
    @inline(__always) static func sawtooth(_ ph: Double) -> Float {
      let c: Double = ph / TAU
      return Float(2.0 * (c - floor(c)) - 1.0)
    }

    /// A large stone hall: Freeverb-style parallel combs and series allpasses.
    static func reverb(_ b: inout Buf, _ wet: Float, _ feedback: Float, _ damp: Float, _ size: Float) {
      let combLen = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617]
      let apLen = [556, 441, 341, 225]
      // delay lines (8 combs then 4 allpasses) in one block, plus their state
      let len = UnsafeMutablePointer<Int>.allocate(capacity: 12)
      let pos = UnsafeMutablePointer<Int>.allocate(capacity: 12)
      let line = UnsafeMutablePointer<UnsafeMutablePointer<Float>>.allocate(capacity: 12)
      let store = UnsafeMutablePointer<Float>.allocate(capacity: 8)
      store.initialize(repeating: 0, count: 8)
      var total = 0
      for k in 0..<12 {
        len[k] = Int(Float(k < 8 ? combLen[k] : apLen[k - 8]) * size)
        pos[k] = 0
        total += len[k]
      }
      let mem = UnsafeMutablePointer<Float>.allocate(capacity: total)
      mem.initialize(repeating: 0, count: total)
      var o = 0
      for k in 0..<12 {
        line[k] = mem + o
        o += len[k]
      }
      defer {
        mem.deallocate()
        len.deallocate()
        pos.deallocate()
        line.deallocate()
        store.deallocate()
      }
      b.withUnsafeMutableBufferPointer { p in
        for i in 0..<p.count {
          let x = p[i] * 0.015
          var y: Float = 0
          for k in 0..<8 {
            let c = line[k] + pos[k]
            let o = c.pointee
            store[k] = o * (1 - damp) + store[k] * damp
            c.pointee = x + store[k] * feedback
            pos[k] += 1
            if pos[k] >= len[k] { pos[k] = 0 }
            y += o
          }
          for k in 8..<12 {
            let a = line[k] + pos[k]
            let o = a.pointee
            a.pointee = y + o * 0.5
            y = o - y
            pos[k] += 1
            if pos[k] >= len[k] { pos[k] = 0 }
          }
          p[i] = p[i] * (1 - wet * 0.5) + y * wet * 3
        }
      }
    }

    /// A long ringing partial (exponential decay), computed with a rotating phasor.
    static func addRing(_ b: inout Buf, _ t0: Float, _ f: Float, _ amp: Float, _ decay: Float) {
      let s0 = Int(t0 * Float(SR))
      let c = cos(TAU * Double(f) / Double(SR)), sn = sin(TAU * Double(f) / Double(SR))
      var x = 0.0, y = 1.0  // phasor (sin, cos)
      var env = Double(amp)
      let k = exp(Double(-decay) / Double(SR))
      b.withUnsafeMutableBufferPointer { p in
        var i = s0
        while i < p.count {
          p[i] += Float(env * x)
          let nx = x * c + y * sn, ny = y * c - x * sn
          x = nx
          y = ny
          env *= k
          if env < 1e-5 { break }
          i += 1
        }
      }
    }

    static func envAR(_ t: Float, _ a: Float, _ hold: Float, _ r: Float) -> Float {
      if t < 0 { return 0 }
      if t < a { return t / a }
      if t < a + hold { return 1 }
      return max(0, 1 - (t - a - hold) / r)
    }

    static let organH: [Float] = [1.0, 0.75, 0.42, 0.5, 0.22, 0.3, 0.1, 0.22, 0.08, 0.12, 0.05, 0.09]

    /// A pipe organ note: principal + octave + mixture ranks, a breath of wind
    /// noise and the "chiff" of the pipe speaking. 'reed' adds a trumpet stop.
    static func organNote(_ b: inout Buf, _ r: inout XorShift32, _ t0: Float, _ dur: Float, _ f: Float, _ amp: Float,
                          _ reed: Float) {
      // one period of the registration as a wavetable, played twice (slightly detuned) for the chorus of ranks
      let N = 2048
      var table = [Float](repeating: 0, count: N + 1)
      table.withUnsafeMutableBufferPointer { tb in
        for k in 0..<12 {
          if f * Float(k + 1) > 9000 { break }
          let a = organH[k] * (1 + reed * (k % 2 == 0 ? 0.6 : 1.4) * (k > 0 ? 1 : 0))
          for i in 0..<N {
            let w: Double = TAU * Double(k + 1) * Double(i) / Double(N)
            tb[i] += a * Float(sin(w))
          }
        }
        tb[N] = tb[0]
      }
      let s0 = Int(t0 * Float(SR)), n = Int((dur + 0.25) * Float(SR))
      var p0 = 0.0, p1 = 0.37
      let d0 = Double(f) / Double(SR), d1 = Double(f) * 1.0007 / Double(SR)
      var chiff = Biquad()
      chiff.bandpass(min(6000, f * 3), 3)
      var windF = Biquad()
      windF.bandpass(min(5000, f * 2), 1.2)
      table.withUnsafeBufferPointer { tb in
        @inline(__always) func look(_ ph: Double) -> Float {
          let x = ph * Double(N)
          let i = Int(x)
          let fr = Float(x - Double(i))
          return tb[i] + (tb[i + 1] - tb[i]) * fr
        }
        b.withUnsafeMutableBufferPointer { p in
          var i = 0
          while i < n && s0 + i < p.count {
            let t = Float(i) / Float(SR)
            let env = envAR(t, 0.045, dur - 0.045, 0.18)
            if env <= 0 && t > dur { break }
            p0 += d0
            if p0 >= 1 { p0 -= 1 }
            p1 += d1
            if p1 >= 1 { p1 -= 1 }
            let v = look(p0) + 0.6 * look(p1)
            let wind = windF.run(r.bi()) * 0.012
            let ch: Float = t < 0.06 ? chiff.run(r.bi()) * 0.5 * (1 - t / 0.06) : 0
            p[s0 + i] += (v * 0.2 + wind + ch) * env * amp
            i += 1
          }
        }
      }
    }

    static func polyblep(_ tIn: Double, _ dt: Double) -> Float {
      var t = tIn
      if t < dt {
        t /= dt
        return Float(t + t - t * t - 1)
      }
      if t > 1 - dt {
        t = (t - 1) / dt
        return Float(t * t + t + t + 1)
      }
      return 0
    }

    /// A men's choir singing "ah": detuned PolyBLEP sawtooth voices with vibrato
    /// through vowel formants. A note may start a little before the buffer (t0 < 0):
    /// that part is cut off.
    static func choirNote(_ b: inout Buf, _ r: inout XorShift32, _ t0: Float, _ dur: Float, _ f: Float, _ amp: Float,
                          _ voices: Int) {
      let s0 = Int(t0 * Float(SR)), n = Int((dur + 0.4) * Float(SR))
      var tmp = [Float](repeating: 0, count: n)
      tmp.withUnsafeMutableBufferPointer { tp in
        for _ in 0..<voices {
          var ph = Double(r.next())
          let detune = pow(Float(2), (r.next() - 0.5) * 18 / 1200)
          let vibPh = r.next() * 6.28, vibRate = 5 + r.next() * 1.2
          var drift: Float = 0
          for i in 0..<n {
            let t = Float(i) / Float(SR)
            drift += (r.bi() * 0.02 - drift * 0.001)
            let vib: Float = 1 + 0.006 * sin(t * vibRate * 6.2831 + vibPh) * min(1, t / 0.4)
            let fr: Float = f * detune * (1 + drift * 0.000578) * vib
            let dt = Double(fr / Float(SR))
            ph += dt
            if ph >= 1 { ph -= 1 }
            let saw = Float(2 * ph - 1) - polyblep(ph, dt)
            tp[i] += saw * envAR(t, 0.14 + 0.04 * r.next(), dur - 0.14, 0.35)
          }
        }
      }
      var f1 = Biquad(), f2 = Biquad(), f3 = Biquad(), f4 = Biquad()
      f1.bandpass(730, 7)
      f2.bandpass(1090, 9)
      f3.bandpass(2440, 14)
      f4.lowpass(3500)
      tmp.withUnsafeBufferPointer { tp in
        b.withUnsafeMutableBufferPointer { p in
          for i in 0..<n {
            let o = s0 + i
            if o >= p.count { break }
            let x = tp[i]
            let y = f1.run(x) * 1.0 + f2.run(x) * 0.55 + f3.run(x) * 0.22
            let v = f4.run(y) * amp / Float(voices)
            if o >= 0 { p[o] += v }
          }
        }
      }
    }

    /// Filtered noise with an envelope given by a function.
    static func noiseShaped(_ b: inout Buf, _ r: inout XorShift32, _ t0: Float, _ dur: Float, _ flt: Biquad,
                            _ amp: Float, _ env: (Float) -> Float) {
      var flt = flt  // by value, like the C++
      let s0 = Int(t0 * Float(SR)), n = Int(dur * Float(SR))
      b.withUnsafeMutableBufferPointer { p in
        var i = 0
        while i < n && s0 + i < p.count {
          p[s0 + i] += flt.run(r.bi()) * env(Float(i) / Float(SR)) * amp
          i += 1
        }
      }
    }

    /// The medieval army's effects (Sfx.hymn ... Sfx.bite) into S, which holds every Sfx.
    /// Made on a background thread after start-up (about half a second of work).
    static func synthMedieval(_ S: inout [Buf]) {
      var r = XorShift32(s: 777)
      do {  // HYMN: Dies irae - "Di-es i-rae, di-es il-la" - organ over a drone, a men's choir, a cathedral
        var b = make(7.6)
        let D3: Float = 146.83, C3: Float = 130.81, E3: Float = 164.81, F3: Float = 174.61
        let mel: [Float] = [F3, E3, F3, D3, E3, C3, D3, D3]
        let step: Float = 0.5
        // open-fifth drone (D and A) swelling underneath
        organNote(&b, &r, 0, 4.9, 73.42, 0.55, 0)
        organNote(&b, &r, 0.25, 4.65, 110, 0.35, 0)
        for i in 0..<8 {
          var t0 = Float(i) * step
          var dur: Float = i == 7 ? 1.4 : step * 0.92
          if i == 7 { t0 = 7 * step }
          if i == 6 { dur = step * 0.95 }
          organNote(&b, &r, t0, dur, mel[i] * 2, 0.75, 0)
          organNote(&b, &r, t0, dur, mel[i], 0.5, 0)
          choirNote(&b, &r, t0 - 0.02, dur + 0.05, mel[i], 2.2, 6)
        }
        // the final D: full organ with reeds and a deep pedal
        let tf = 7 * step
        for f: Float in [36.71, 73.42, 146.83, 220.0, 293.66, 349.23, 440.0] {
          organNote(&b, &r, tf, 1.4, f, f < 80 ? 0.4 : 0.15, 0.8)
        }
        choirNote(&b, &r, tf, 1.45, 110, 0.9, 5)
        reverb(&b, 0.55, 0.9, 0.25, 1.6)
        normalize(&b, 0.9)
        S[Sfx.hymn.rawValue] = b
      }
      do {  // ORGAN: one dark chord with reeds (D minor with a menacing flat sixth)
        var b = make(5.0)
        for f: Float in [36.71, 73.42, 146.83, 174.61, 220.0, 233.08, 293.66] {
          organNote(&b, &r, 0, 2.8, f, f < 80 ? 0.8 : 0.4, 1.0)
        }
        choirNote(&b, &r, 0.05, 2.7, 146.83, 1.0, 5)
        choirNote(&b, &r, 0.05, 2.7, 220.0, 0.7, 5)
        reverb(&b, 0.55, 0.9, 0.25, 1.6)
        normalize(&b, 0.85)
        S[Sfx.organ.rawValue] = b
      }
      do {  // THUNDER: a splitting crack, a boom, then the long roll
        var b = make(5.0)
        var hp = Biquad()
        hp.highpass(1800)
        for k in 0..<7 {  // the crack: a few ragged bursts
          let t0 = r.next() * 0.09
          noiseShaped(&b, &r, t0, 0.06, hp, 0.9 * (1 - Float(k) * 0.1)) { exp(-$0 * 60) }
        }
        var bp = Biquad()
        bp.bandpass(900, 0.8)
        noiseShaped(&b, &r, 0, 0.4, bp, 1.4) { exp(-$0 * 9) }
        var lp = Biquad()
        lp.lowpass(170)
        noiseShaped(&b, &r, 0.02, 1.5, lp, 6.0) { min(1, $0 / 0.02) * exp(-$0 * 2.2) }
        addDampedSine(&b, 0.02, 52, 0.9, 2.5, 0.3)
        // the roll: rumbling swells that die away
        var lp2 = Biquad()
        lp2.lowpass(260)
        let rolls: [Float] = [0.5, 1.1, 1.6, 2.4, 3.1]
        for k in 0..<5 {
          let a = 4.8 * exp(-Float(k) * 0.4)
          noiseShaped(&b, &r, rolls[k], 1.8, lp2, a) { t in
            sin(min(1, t / 1.8) * 3.14159) * (0.7 + 0.3 * sin(t * 17))
          }
        }
        reverb(&b, 0.3, 0.86, 0.4, 1.3)
        normalize(&b, 1.0)
        S[Sfx.thunder.rawValue] = b
      }
      do {  // WIND: gusting, whistling storm wind
        var b = make(3.6)
        var f = Biquad(), w = Biquad()
        b.withUnsafeMutableBufferPointer { p in
          for i in 0..<p.count {
            let t = Float(i) / Float(SR)
            if i % 64 == 0 {
              let fc: Float = 380 + 420 * (0.5 + 0.5 * sin(t * 1.7)) + 150 * sin(t * 4.3)
              f.bandpass(fc, 1.4)
              w.bandpass(1200 + 500 * sin(t * 0.9 + 1), 22)
            }
            let gust: Float = 0.55 + 0.45 * sin(t * 2.3) * sin(t * 0.7 + 0.4)
            let env = sin(min(1, t / 3.6) * 3.14159)
            let x = r.bi()
            p[i] = (f.run(x) * 1.0 + w.run(x) * 0.6) * gust * env
          }
        }
        normalize(&b, 0.7)
        S[Sfx.wind.rawValue] = b
      }
      do {  // ROAR: a growling throat through a shifting vowel, breath and a sub-bass rumble
        var b = make(2.4)
        var ph = 0.0
        var jitter: Float = 0
        var fA = Biquad(), fB = Biquad(), fC = Biquad(), br = Biquad(), sub = Biquad()
        sub.lowpass(90)
        br.bandpass(1300, 0.7)
        b.withUnsafeMutableBufferPointer { p in
          for i in 0..<p.count {
            let t = Float(i) / Float(SR)
            let u = t / 2.2
            let pitch: Float = 75 + 45 * sin(min(1, u * 1.6) * 3.14159) - 20 * u
            jitter += (r.bi() * 0.6 - jitter * 0.02)
            ph += TAU * Double(pitch) * Double(1 + 0.012 * jitter) / Double(SR)
            let rattle = Float(fd(0.6) + fd(0.4) * sin(Double(t) * TAU * 28 + Double(sin(t * 13))))
            let saw = sawtooth(ph)
            let x = saw * rattle
            if i % 64 == 0 {
              let open = sin(min(1, u * 1.3) * 3.14159)
              fA.bandpass(450 + 350 * open, 5)
              fB.bandpass(900 + 400 * open, 6)
              fC.bandpass(2300, 8)
            }
            let voice = fA.run(x) * 1.0 + fB.run(x) * 0.7 + fC.run(x) * 0.25
            let breath = br.run(r.bi()) * 0.6
            let env = envAR(t, 0.15, 1.3, 0.75)
            let throat: Float = tanh((voice * 2.5 + breath) * 1.5) + sub.run(r.bi()) * 2
            let s2 = Float(Double(throat) + fd(0.4) * sin(Double(t) * TAU * 42))
            p[i] = s2 * env
          }
        }
        var dark1 = Biquad(), dark2 = Biquad()  // a throat this big has no hiss
        dark1.lowpass(2600)
        dark2.lowpass(2600)
        for i in b.indices { b[i] = dark2.run(dark1.run(b[i])) }
        reverb(&b, 0.2, 0.82, 0.4, 1.0)
        normalize(&b, 0.95)
        S[Sfx.roar.rawValue] = b
      }
      do {  // FIRE: the roar of a jet of flame, crackling
        var b = make(2.8)
        var lp = Biquad(), bp = Biquad(), low = Biquad()
        lp.lowpass(2400)
        bp.bandpass(700, 0.6)
        low.lowpass(160)
        b.withUnsafeMutableBufferPointer { p in
          for i in 0..<p.count {
            let t = Float(i) / Float(SR)
            let env = envAR(t, 0.08, 1.8, 0.9)
            let flutter: Float = 0.7 + 0.3 * sin(t * 61 + sin(t * 7) * 3)
            let x = r.bi()
            let crack: Float = r.next() < 0.0015 ? r.bi() * 6 : 0
            let body: Float = lp.run(x) * 0.7 + bp.run(x) * 1.2 + low.run(x) * 2.5
            p[i] = body * flutter * env + crack * env
          }
        }
        normalize(&b, 0.85)
        S[Sfx.fire.rawValue] = b
      }
      do {  // CHAIN: a creak of timber, then the chain running out link by link
        var b = make(1.9)
        var ph = 0.0
        var cr = Biquad()
        cr.bandpass(900, 4)
        for i in 0..<Int(0.45 * Float(SR)) {  // the creak
          let t = Float(i) / Float(SR)
          let f: Float = 180 + 60 * sin(t * 9) + 40 * r.bi() * 0.1
          ph += TAU * Double(f) / Double(SR)
          let saw = sawtooth(ph)
          let stick: Float = sin(Double(t) * TAU * 23) > fd(0.2) ? 1 : 0.3
          b[i] += cr.run(saw) * stick * sin(t / 0.45 * 3.14159) * 0.8
        }
        var t: Float = 0.2
        var gap: Float = 0.09
        while t < 1.6 {  // links clinking, accelerating as the bridge falls
          for _ in 0..<3 {
            addDampedSine(&b, t + r.next() * 0.008, 2400 + r.next() * 3500, 0.3 + 0.3 * r.next(), 90 + r.next() * 80)
          }
          var h = Biquad()
          h.highpass(3000)
          addNoise(&b, &r, t, 0.35, 200, h)
          t += gap
          gap = max(0.028, gap * 0.93)
        }
        normalize(&b, 0.8)
        S[Sfx.chain.rawValue] = b
      }
      do {  // GRIND: stone dragged over stone
        var b = make(1.6)
        var bp = Biquad(), lp = Biquad()
        bp.bandpass(320, 0.9)
        lp.lowpass(900)
        var grain: Float = 0
        b.withUnsafeMutableBufferPointer { p in
          for i in 0..<p.count {
            let t = Float(i) / Float(SR)
            if i % 600 == 0 { grain = 0.4 + 0.6 * r.next() }
            let env = envAR(t, 0.1, 1.1, 0.4)
            let x = r.bi()
            let crunch: Float = r.next() < 0.002 ? r.bi() * 3 : 0
            p[i] = (bp.run(x) * 1.5 + lp.run(x) * 0.6) * grain * env + crunch * env
          }
        }
        normalize(&b, 0.75)
        S[Sfx.grind.rawValue] = b
      }
      do {  // STOMP: a giant stone foot on the board
        var b = make(1.1)
        addDampedSine(&b, 0, 44, 1.2, 6.0, 0.5)
        addDampedSine(&b, 0, 88, 0.6, 12.0, 0.8)
        var lp = Biquad(), bp = Biquad()
        lp.lowpass(300)
        addNoise(&b, &r, 0, 2.2, 9, lp)
        bp.bandpass(1800, 1.0)
        addNoise(&b, &r, 0, 0.8, 40, bp)
        for _ in 0..<12 { addDampedSine(&b, 0.02 + r.next() * 0.3, 1500 + r.next() * 3000, 0.15, 120) }
        normalize(&b, 1.0)
        S[Sfx.stomp.rawValue] = b
      }
      do {  // HOOF: a shod hoof knocking on wood (a quick double strike)
        var b = make(0.2)
        for k in 0..<2 {
          let t0 = Float(k) * 0.018, a: Float = k == 0 ? 1.0 : 0.6
          addDampedSine(&b, t0, 380, a, 60)
          addDampedSine(&b, t0, 1050, a * 0.5, 90)
          addDampedSine(&b, t0, 2600, a * 0.25, 140)
          var bp = Biquad()
          bp.bandpass(2000, 1.0)
          addNoise(&b, &r, t0, a * 0.8, 160, bp)
        }
        normalize(&b, 0.7)
        S[Sfx.hoof.rawValue] = b
      }
      do {  // NEIGH: a whinny - a high trilling voice sliding down, ending in a snort
        var b = make(1.5)
        var ph = 0.0
        var f1 = Biquad(), f2 = Biquad(), f3 = Biquad(), nb = Biquad()
        f1.bandpass(900, 3)
        f2.bandpass(1700, 4)
        f3.bandpass(3000, 5)
        nb.bandpass(1400, 1)
        b.withUnsafeMutableBufferPointer { p in
          for i in 0..<Int(1.15 * Float(SR)) {
            let t = Float(i) / Float(SR)
            let u = t / 1.15
            let base: Float = u < 0.12 ? 520 + 4500 * u : 1060 - 640 * (u - 0.12) / 0.88
            let tr = sin(Double(t) * TAU * Double(17 - 5 * u))  // the trill
            let trill = Float(1 + Double(0.06 + 0.06 * u) * tr)
            ph += TAU * Double(base) * Double(trill) / Double(SR)
            let saw = sawtooth(ph)
            let env = Float(Double(envAR(t, 0.04, 0.85, 0.26)) * (fd(0.75) + fd(0.25) * tr))
            let v: Float = f1.run(saw) * 1.0 + f2.run(saw) * 0.7 + f3.run(saw) * 0.3 + nb.run(r.bi()) * 0.35
            p[i] += v * env
          }
        }
        var sn = Biquad()
        sn.bandpass(500, 0.8)
        noiseShaped(&b, &r, 1.18, 0.3, sn, 1.4) { min(1, $0 / 0.02) * exp(-$0 * 14) }
        reverb(&b, 0.15, 0.8, 0.5, 0.8)
        normalize(&b, 0.8)
        S[Sfx.neigh.rawValue] = b
      }
      do {  // BELL: a great church bell (hum, prime, minor tierce, quint, nominal ...)
        var b = make(6.5)
        let prime: Float = 147
        let ratio: [Float] = [0.5, 1.0, 1.2, 1.5, 2.0, 2.5, 3.0, 4.2]
        let amp: [Float] = [0.7, 0.8, 0.6, 0.3, 0.7, 0.25, 0.3, 0.15]
        let dec: [Float] = [0.45, 0.7, 0.9, 1.3, 1.1, 2.0, 2.2, 3.5]
        for k in 0..<8 {
          addRing(&b, 0, prime * ratio[k], amp[k], dec[k])
          addRing(&b, 0, prime * ratio[k] * 1.0035, amp[k] * 0.6, dec[k] * 1.1)  // beating doublet
        }
        var strike = Biquad()
        strike.bandpass(2500, 1.0)
        addNoise(&b, &r, 0, 0.6, 80, strike)
        reverb(&b, 0.4, 0.88, 0.3, 1.5)
        normalize(&b, 0.85)
        S[Sfx.bell.rawValue] = b
      }
      do {  // SPLINTER: a lance bursting into splinters
        var b = make(1.0)
        var bp = Biquad()
        bp.bandpass(2800, 0.9)
        addNoise(&b, &r, 0, 2.0, 45, bp)
        addDampedSine(&b, 0, 210, 0.8, 25)
        addDampedSine(&b, 0, 520, 0.5, 35)
        for _ in 0..<45 {
          let t = -log(1 - r.next() * 0.99) * 0.12
          var c = Biquad()
          c.bandpass(1200 + r.next() * 3000, 2.0)
          addNoise(&b, &r, t, 0.3 + 0.4 * r.next(), 250, c)
          if r.next() < 0.4 { addDampedSine(&b, t, 400 + r.next() * 900, 0.2, 60) }
        }
        normalize(&b, 0.9)
        S[Sfx.splinter.rawValue] = b
      }
      do {  // ARMOR: a booted step with the jingle of mail
        var b = make(0.32)
        addDampedSine(&b, 0, 130, 0.8, 40, 2.0)
        var lp = Biquad()
        lp.lowpass(1200)
        addNoise(&b, &r, 0, 0.6, 60, lp)
        for _ in 0..<10 {
          addDampedSine(&b, 0.01 + r.next() * 0.12, 4000 + r.next() * 5000, 0.08 + 0.08 * r.next(),
                        140 + r.next() * 80)
        }
        normalize(&b, 0.6)
        S[Sfx.armor.rawValue] = b
      }
      do {  // SLIDE: a heavy wooden throne sliding over the board
        var b = make(1.3)
        var bp = Biquad(), res = Biquad()
        bp.bandpass(800, 0.9)
        res.bandpass(240, 6)
        var stick: Float = 0
        b.withUnsafeMutableBufferPointer { p in
          for i in 0..<p.count {
            let t = Float(i) / Float(SR)
            if i % 900 == 0 { stick = 0.5 + 0.5 * r.next() }
            let chatter = Float(fd(0.6) + fd(0.4) * sin(Double(t) * TAU * Double(38 + 10 * sin(t * 3))))
            let env = envAR(t, 0.08, 0.85, 0.35)
            let x = r.bi()
            p[i] = (bp.run(x) * 1.0 + res.run(x) * 0.8) * chatter * stick * env
          }
        }
        normalize(&b, 0.6)
        S[Sfx.slide.rawValue] = b
      }
      do {  // BITE: jaws snapping shut on stone
        var b = make(0.8)
        addDampedSine(&b, 0, 1900, 0.6, 120)
        var hp = Biquad()
        hp.highpass(1500)
        addNoise(&b, &r, 0, 1.0, 90, hp)
        addDampedSine(&b, 0.01, 70, 1.0, 14, 0.8)
        for _ in 0..<40 {
          let t = 0.02 + -log(1 - r.next() * 0.99) * 0.09
          addDampedSine(&b, t, 1500 + r.next() * 4500, 0.15 + 0.25 * r.next(), 140 + r.next() * 120)
        }
        var lp = Biquad()
        lp.lowpass(600)
        addNoise(&b, &r, 0.02, 1.2, 18, lp)
        normalize(&b, 0.95)
        S[Sfx.bite.rawValue] = b
      }
    }

    // MARK: The aliens' sounds

    /// A sine whose frequency follows freq(t), with envelope env(t), added at t0.
    static func addSweep(_ b: inout Buf, _ t0: Float, _ dur: Float, _ freq: (Float) -> Float,
                         _ env: (Float) -> Float) {
      let s0 = Int(t0 * Float(SR)), n = Int(dur * Float(SR))
      var ph = 0.0
      b.withUnsafeMutableBufferPointer { p in
        var i = 0
        while i < n && s0 + i < p.count {
          let t = Float(i) / Float(SR)
          ph += TAU * Double(freq(t)) / Double(SR)
          p[s0 + i] += Float(sin(ph)) * env(t)
          i += 1
        }
      }
    }

    /// A band-limited sawtooth (PolyBLEP) following freq(t).
    static func addSaw(_ b: inout Buf, _ t0: Float, _ dur: Float, _ freq: (Float) -> Float,
                       _ env: (Float) -> Float) {
      let s0 = Int(t0 * Float(SR)), n = Int(dur * Float(SR))
      var ph = 0.0
      b.withUnsafeMutableBufferPointer { p in
        var i = 0
        while i < n && s0 + i < p.count {
          let t = Float(i) / Float(SR)
          let dt = Double(freq(t) / Float(SR))
          ph += dt
          if ph >= 1 { ph -= 1 }
          p[s0 + i] += (Float(2 * ph - 1) - polyblep(ph, dt)) * env(t)
          i += 1
        }
      }
    }

    /// The aliens' effects (Sfx.pew ... Sfx.sputter) into S, which holds every Sfx.
    /// Made on the background thread right after the medieval army's.
    static func synthAlien(_ S: inout [Buf]) {
      var r = XorShift32(s: 4242)
      do {  // PEW: a falling laser chirp with a click
        var b = make(0.4)
        addSweep(&b, 0, 0.4, { (t: Float) -> Float in 320 + 1650 * exp(-t * 13) },
                 { (t: Float) -> Float in exp(-t * 8) * min(1, t / 0.003) })
        addSweep(&b, 0, 0.4, { (t: Float) -> Float in 2 * (320 + 1650 * exp(-t * 13)) },
                 { (t: Float) -> Float in 0.35 * exp(-t * 11) * min(1, t / 0.003) })
        var hp = Biquad()
        hp.highpass(3000)
        addNoise(&b, &r, 0, 0.4, 120, hp)
        normalize(&b, 0.7)
        S[Sfx.pew.rawValue] = b
      }
      do {  // UFO: the classic theremin warble, with a hum underneath
        var b = make(2.4)
        let env = { (t: Float) -> Float in envAR(t, 0.3, 1.6, 0.5) }
        addSweep(&b, 0, 2.4, { (t: Float) -> Float in
          let w: Float = 140 * sin(t * 6.2831 * 5.5)
          return 520 + w + 60 * sin(t * 2.2)
        }, env)
        addSweep(&b, 0, 2.4, { (t: Float) -> Float in 1.5 * (520 + 140 * sin(t * 6.2831 * 5.5 + 0.4)) },
                 { (t: Float) -> Float in 0.3 * env(t) })
        addSweep(&b, 0, 2.4, { (_: Float) -> Float in 62 },
                 { (t: Float) -> Float in 0.5 * env(t) * (0.7 + 0.3 * sin(t * 6.2831 * 9)) })
        reverb(&b, 0.25, 0.8, 0.3, 0.8)
        normalize(&b, 0.6)
        S[Sfx.ufo.rawValue] = b
      }
      do {  // THEREMIN: an eerie little tune, sliding from note to note
        var b = make(4.6)
        let notes: [Float] = [440.0, 659.25, 587.33, 523.25, 440.0, 349.23, 329.63, 440.0]
        let times: [Float] = [0.0, 0.55, 1.1, 1.45, 1.8, 2.4, 2.8, 3.3]
        let freq = { (t: Float) -> Float in
          var k = 0
          while k < 7 && t >= times[k + 1] { k += 1 }
          var f = notes[k]
          if k > 0 {  // portamento from the previous note
            let u = min(1, (t - times[k]) / 0.12)
            f = notes[k - 1] + (notes[k] - notes[k - 1]) * (u * u * (3 - 2 * u))
          }
          let vib: Float = 0.016 * sin(t * 6.2831 * 6.3) * min(1, t / 0.3)
          return f * (1 + vib)
        }
        let env = { (t: Float) -> Float in envAR(t, 0.15, 3.65, 0.7) }
        addSweep(&b, 0, 4.6, freq, env)
        addSweep(&b, 0, 4.6, { (t: Float) -> Float in 2 * freq(t) }, { (t: Float) -> Float in 0.12 * env(t) })
        reverb(&b, 0.45, 0.84, 0.3, 1.0)
        normalize(&b, 0.6)
        S[Sfx.theremin.rawValue] = b
      }
      do {  // BEAM: a shimmering chord climbing as the beam lifts its catch, with sparkles
        var b = make(2.8)
        let ratios: [Float] = [1.0, 1.5, 2.0, 3.0]
        for k in 0..<4 {
          let rt = ratios[k], kf = Float(k)
          addSweep(&b, 0, 2.8, { (t: Float) -> Float in rt * 220 * pow(Float(2), t / 2.6) },
                   { (t: Float) -> Float in
                     let shimmer: Float = 0.75 + 0.25 * sin(t * 6.2831 * 12 + kf)
                     return envAR(t, 0.35, 1.9, 0.5) * shimmer / (1 + kf)
                   })
        }
        for _ in 0..<40 {
          let t = r.next() * 2.5
          let f = 3000 + r.next() * 4000
          addDampedSine(&b, t, f, 0.08, 60)
        }
        normalize(&b, 0.6)
        S[Sfx.beam.rawValue] = b
      }
      do {  // GIBBER: "blip blorp zib-zib" - fast formant syllables at random pitches
        var b = make(1.4)
        var t: Float = 0.02
        var i = 0
        while i < 9 && t < 1.25 {
          let len = 0.06 + r.next() * 0.09
          let f0 = 260 + r.next() * 420
          let glide = (r.next() - 0.4) * 1.2
          let vowel = r.next()
          var syl = make(len + 0.02)
          addSaw(&syl, 0, len, { (u: Float) -> Float in
            let wobble: Float = 1 + 0.04 * sin(u * 6.2831 * 30)
            return f0 * (1 + glide * u / len) * wobble
          }, { (u: Float) -> Float in envAR(u, 0.008, len - 0.03, 0.022) })
          var f1 = Biquad(), f2 = Biquad()
          f1.bandpass(400 + 500 * vowel, 6)
          f2.bandpass(1200 + 1400 * (1 - vowel), 8)
          let s0 = Int(t * Float(SR))
          var k = 0
          while k < syl.count && s0 + k < b.count {
            b[s0 + k] += f1.run(syl[k]) + 0.6 * f2.run(syl[k])
            k += 1
          }
          t += len + (r.next() < 0.3 ? 0.07 : 0.015)
          i += 1
        }
        normalize(&b, 0.7)
        S[Sfx.gibber.rawValue] = b
      }
      do {  // SLURP: wet noise with a rising resonance and bubbles, a pop at the end
        var b = make(0.75)
        var bp = Biquad()
        for i in 0..<b.count {
          let t = Float(i) / Float(SR)
          if i % 64 == 0 { bp.bandpass(350 + 2600 * t / 0.7, 4) }
          let flutter: Float = 0.6 + 0.4 * sin(t * 6.2831 * 23) * sin(t * 6.2831 * 7)
          b[i] += bp.run(r.bi()) * envAR(t, 0.05, 0.5, 0.15) * flutter * 2
        }
        for _ in 0..<6 {
          let t = 0.05 + r.next() * 0.55
          let f = 500 + r.next() * 900
          addDampedSine(&b, t, f, 0.25, 50, -3)
        }
        addDampedSine(&b, 0.66, 700, 0.6, 40, 6)
        normalize(&b, 0.7)
        S[Sfx.slurp.rawValue] = b
      }
      do {  // BURP: a low, jittery pulse through an "uh" formant
        var b = make(1.0)
        var src = make(1.0)
        var ph = 0.0
        var jit: Float = 0
        for i in 0..<src.count {
          let t = Float(i) / Float(SR)
          jit += (r.bi() * 0.4 - jit * 0.02)
          let f: Float = 92 * (1 - 0.15 * t) * (1 + 0.01 * jit)
          let dt = Double(f / Float(SR))
          ph += dt
          if ph >= 1 { ph -= 1 }
          let env: Float = envAR(t, 0.04, 0.55, 0.3) * (0.8 + 0.2 * sin(t * 6.2831 * 7))
          src[i] = (Float(2 * ph - 1) - polyblep(ph, dt)) * env
        }
        var f1 = Biquad(), f2 = Biquad(), lp = Biquad()
        f1.bandpass(520, 4)
        f2.bandpass(1050, 6)
        lp.lowpass(1600)
        for i in 0..<b.count { b[i] = lp.run(f1.run(src[i]) * 1.2 + f2.run(src[i]) * 0.5 + src[i] * 0.15) }
        normalize(&b, 0.8)
        S[Sfx.burp.rawValue] = b
      }
      do {  // BOING: a spring
        var b = make(0.9)
        addSweep(&b, 0, 0.9, { (t: Float) -> Float in
          let wob: Float = 1 + 0.45 * exp(-t * 3.5) * sin(t * 6.2831 * 13)
          return 210 * wob * (1 + 0.3 * t)
        }, { (t: Float) -> Float in exp(-t * 3.2) * min(1, t / 0.004) })
        addSweep(&b, 0, 0.9, { (t: Float) -> Float in 420 * (1 + 0.45 * exp(-t * 3.5) * sin(t * 6.2831 * 13)) },
                 { (t: Float) -> Float in 0.25 * exp(-t * 5) })
        normalize(&b, 0.7)
        S[Sfx.boing.rawValue] = b
      }
      do {  // WHISTLE: a slide whistle, up and up (blasting off)
        var b = make(1.5)
        addSweep(&b, 0, 1.5, { (t: Float) -> Float in
          600 * pow(Float(3.6), t / 1.3) * (1 + 0.012 * sin(t * 6.2831 * 6))
        }, { (t: Float) -> Float in envAR(t, 0.04, 1.2, 0.25) })
        var bp = Biquad()
        bp.bandpass(2500, 1)
        noiseShaped(&b, &r, 0, 1.5, bp, 0.05) { envAR($0, 0.04, 1.2, 0.25) }
        normalize(&b, 0.6)
        S[Sfx.whistle.rawValue] = b
      }
      do {  // SPLAT: a wet thump and squelches
        var b = make(0.7)
        addDampedSine(&b, 0, 85, 1.0, 14, 1.0)
        var lp = Biquad()
        lp.lowpass(1400)
        addNoise(&b, &r, 0, 1.2, 16, lp)
        for _ in 0..<7 {
          let t = 0.02 + r.next() * 0.4
          let f = 300 + r.next() * 700
          var bp = Biquad()
          bp.bandpass(f, 5)
          addNoise(&b, &r, t, 0.6, 45, bp)
        }
        normalize(&b, 0.85)
        S[Sfx.splat.rawValue] = b
      }
      do {  // MOO
        var b = make(1.7)
        var src = make(1.7)
        addSaw(&src, 0, 1.6, { (t: Float) -> Float in
          let low: Float = 3 * sin(t * 6.2831 * 5) * min(1, t / 0.5)
          return 128 - 22 * t / 1.6 + low
        }, { (t: Float) -> Float in envAR(t, 0.18, 1.05, 0.35) })
        var f1 = Biquad(), f2 = Biquad(), f3 = Biquad()
        f1.bandpass(330, 5)
        f2.bandpass(820, 7)
        f3.lowpass(2500)
        for i in 0..<b.count {
          let t = Float(i) / Float(SR)
          let open = min(1, t / 0.35)  // "mm" opening into "oo"
          b[i] = f3.run(f1.run(src[i]) * 1.3 + f2.run(src[i]) * 0.5 * open + src[i] * 0.05)
        }
        reverb(&b, 0.15, 0.7, 0.4, 0.6)
        normalize(&b, 0.8)
        S[Sfx.moo.rawValue] = b
      }
      do {  // POP
        var b = make(0.25)
        addSweep(&b, 0, 0.2, { (t: Float) -> Float in 1100 * exp(-t * 25) + 180 },
                 { (t: Float) -> Float in exp(-t * 28) })
        var hp = Biquad()
        hp.highpass(2000)
        addNoise(&b, &r, 0, 0.8, 200, hp)
        normalize(&b, 0.8)
        S[Sfx.pop.rawValue] = b
      }
      do {  // SHRINK: "wee-oo-wee-oo" all the way down, with sparkles
        var b = make(1.2)
        addSweep(&b, 0, 1.2, { (t: Float) -> Float in
          1700 * pow(Float(0.12), t / 1.1) * (1 + 0.12 * sin(t * 6.2831 * 16))
        }, { (t: Float) -> Float in envAR(t, 0.02, 0.95, 0.2) })
        for _ in 0..<18 {
          let t = r.next() * 1.0
          let f = 4000 + r.next() * 3000
          addDampedSine(&b, t, f, 0.1, 70)
        }
        normalize(&b, 0.6)
        S[Sfx.shrink.rawValue] = b
      }
      do {  // HEATRAY: an electric scream - a rising whine over a ring-modulated buzz, crackling
        var b = make(2.3)
        var p1 = 0.0, p2 = 0.0, p3 = 0.0
        var hp = Biquad()
        hp.highpass(4000)
        for i in 0..<b.count {
          let t = Float(i) / Float(SR)
          let env = envAR(t, 0.25, 1.7, 0.3)
          let whine: Float = 950 + 700 * min(1, t / 1.2)
          p1 += TAU * Double(whine) / Double(SR)
          p2 += TAU * 118.0 / Double(SR)
          p3 += TAU * 1370.0 / Double(SR)
          let harm: Double = sin(p2) + 0.5 * sin(2 * p2) + 0.33 * sin(3 * p2)
          let buzz = Float(harm) * Float(sin(p3))
          let scream = Float(sin(p1 + 0.8 * sin(p2 * 3.0))) * 0.6
          b[i] = (scream + buzz * 0.5 + hp.run(r.bi()) * 0.5) * env
        }
        normalize(&b, 0.75)
        S[Sfx.heatray.rawValue] = b
      }
      do {  // HORN: the tripod's two-note moan
        var b = make(2.9)
        let freq = { (t: Float) -> Float in t < 1 ? 110 : 110 - 18 * min(1, (t - 1) / 0.25) }
        let env = { (t: Float) -> Float in envAR(t, 0.12, 2.1, 0.5) * (t > 0.95 && t < 1.05 ? 0.75 : 1) }
        var src = make(2.9)
        addSaw(&src, 0, 2.8, freq, env)
        addSaw(&src, 0, 2.8, { (t: Float) -> Float in freq(t) * 1.006 }, env)
        addSaw(&src, 0, 2.8, { (t: Float) -> Float in freq(t) * 0.5 }, { (t: Float) -> Float in 0.7 * env(t) })
        var lp = Biquad()
        for i in 0..<b.count {
          let t = Float(i) / Float(SR)
          if i % 64 == 0 { lp.lowpass(500 + 1400 * exp(-fmod(t, Float(1)) * 2), 1.4) }
          b[i] = lp.run(src[i])
        }
        reverb(&b, 0.55, 0.88, 0.35, 1.3)
        normalize(&b, 0.85)
        S[Sfx.horn.rawValue] = b
      }
      do {  // TWINKLE: a star in the sky
        var b = make(1.0)
        addRing(&b, 0, 2637, 0.6, 5)
        addRing(&b, 0, 3951, 0.3, 7)
        addRing(&b, 0.07, 5274, 0.25, 9)
        addRing(&b, 0.12, 3136, 0.2, 8)
        normalize(&b, 0.55)
        S[Sfx.twinkle.rawValue] = b
      }
      do {  // SIZZLE: frying acid with bubbles
        var b = make(1.7)
        var hp = Biquad()
        hp.highpass(2500)
        for i in 0..<b.count {
          let t = Float(i) / Float(SR)
          let crack: Float = r.next() < 0.004 ? 1 : 0.15
          b[i] += hp.run(r.bi()) * crack * envAR(t, 0.08, 1.1, 0.45)
        }
        for _ in 0..<14 {
          let t = r.next() * 1.4
          let f = 600 + r.next() * 1000
          addDampedSine(&b, t, f, 0.12, 40, -2)
        }
        normalize(&b, 0.6)
        S[Sfx.sizzle.rawValue] = b
      }
      do {  // HATCH: the shell cracks, then little chirps
        var b = make(1.2)
        for i in 0..<5 {
          let t = 0.03 * Float(i) + r.next() * 0.02
          let f = 2200 + r.next() * 2200
          addDampedSine(&b, t, f, 0.5, 140)
          var hp = Biquad()
          hp.highpass(1800)
          addNoise(&b, &r, t, 0.5, 120, hp)
        }
        for i in 0..<4 {
          let t = 0.35 + 0.17 * Float(i) + r.next() * 0.05
          let f0 = 1500 + r.next() * 700
          addSweep(&b, t, 0.1, { (u: Float) -> Float in f0 * (1 + 6 * u) },
                   { (u: Float) -> Float in 0.5 * sin(u / 0.1 * 3.14159) })
        }
        normalize(&b, 0.7)
        S[Sfx.hatch.rawValue] = b
      }
      do {  // DRONE: the mothership - deep detuned throbbing chord swelling up
        var b = make(4.8)
        var src = make(4.8)
        let env = { (t: Float) -> Float in envAR(t, 1.2, 2.6, 1.0) * (0.75 + 0.25 * sin(t * 6.2831 * 0.9)) }
        for f: Float in [41.2, 41.5, 61.7, 82.4] { addSaw(&src, 0, 4.8, { _ in f }, env) }
        var lp = Biquad()
        lp.lowpass(420, 1.2)
        for i in 0..<b.count { b[i] = lp.run(src[i]) * 0.5 }
        addSweep(&b, 0, 4.8, { (t: Float) -> Float in 880 + 30 * sin(t * 6.2831 * 4) },
                 { (t: Float) -> Float in 0.04 * env(t) })
        reverb(&b, 0.4, 0.86, 0.4, 1.2)
        normalize(&b, 0.85)
        S[Sfx.drone.rawValue] = b
      }
      do {  // SPUTTER: a machine coughing and dying
        var b = make(1.7)
        var ph = 0.0
        var on = true
        var next: Float = 0
        for i in 0..<b.count {
          let t = Float(i) / Float(SR)
          if t >= next {
            let u = r.next()
            on = u > 0.25 + 0.4 * t / 1.6
            let len = r.next()
            next = t + 0.03 + 0.08 * len
          }
          let f: Float = 70 + 560 * exp(-t * 2.2)
          ph += TAU * Double(f) / Double(SR)
          let sq: Float = sin(ph) > 0 ? 1 : -1
          b[i] = (on ? sq * 0.45 + Float(sin(ph)) * 0.4 : 0) * envAR(t, 0.01, 1.3, 0.3)
        }
        var lp = Biquad()
        lp.lowpass(2500)
        for i in 0..<b.count { b[i] = lp.run(b[i]) }
        var hp = Biquad()
        hp.highpass(3000)
        for _ in 0..<10 {
          let t = r.next() * 1.5
          addNoise(&b, &r, t, 0.4, 90, hp)
        }
        normalize(&b, 0.7)
        S[Sfx.sputter.rawValue] = b
      }
    }

    // MARK: The ghosts' cheer

    /// The ghosts' effect (Sfx.cheer) into S, which holds every Sfx.
    /// Made on the background thread right after the aliens'.
    static func synthGhosts(_ S: inout [Buf]) {
      var r = XorShift32(s: 777)
      do {  // CHEER: a far-off little crowd of ghosts going "yaaay!", clapping, with a shimmer
        var b = make(2.4), v = make(2.4)
        for _ in 0..<7 {
          let t0 = r.next() * 0.12
          let f0: Float = 220 + 260 * r.next()
          let vib = r.next() * 6.28
          addSaw(&v, t0, 1.4, { (t: Float) -> Float in
            let up: Float = 1 + 0.35 * min(t / 0.22, 1) - 0.12 * max(0, t - 0.6)
            let wob: Float = 1 + 0.012 * sin(t * 6.2831 * 5.5 + vib)
            return f0 * up * wob
          }, { (t: Float) -> Float in envAR(t, 0.06, 0.55, 0.6) * 0.3 })
        }
        var f1 = Biquad(), f2 = Biquad(), f3 = Biquad(), f4 = Biquad()  // the vowel "ah"
        f1.bandpass(730, 6.0)
        f2.bandpass(1090, 8.0)
        f3.bandpass(2440, 12.0)
        f4.lowpass(3500)
        for i in b.indices {
          let x = v[i]
          let a1 = f1.run(x), a2 = f2.run(x) * 0.6, a3 = f3.run(x) * 0.25
          b[i] = f4.run(a1 + a2 + a3)
        }
        var cf = Biquad()
        cf.bandpass(1600, 1.1)
        for _ in 0..<14 {
          let t: Float = 0.1 + r.next() * 1.3
          addNoise(&b, &r, t, 0.5, 60, cf)
        }
        for _ in 0..<6 {
          let t = r.next() * 1.5
          let f: Float = 1800 + r.next() * 1800
          addDampedSine(&b, t, f, 0.06, 5.0)
        }
        reverb(&b, 0.65, 0.84, 0.3, 1.15)
        normalize(&b, 0.55)
        S[Sfx.cheer.rawValue] = b
      }
    }
  }

  // MARK: - Real-time mixer

  /// One play() request, handed from the game threads to the render thread.
  fileprivate struct PlayCmd {
    var sfx: Int32 = -1
    var rate: Float = 1, l: Float = 0, r: Float = 0
    // filled in by the render thread when it picks the request up
    var buf: UnsafeMutablePointer<Float>? = nil
    var len = 0
  }

  fileprivate struct Voice {
    var sfx: Int32 = -1  // -1 = free
    var pos: Double = 0
    var rate: Float = 1, l: Float = 0, r: Float = 0
    var buf: UnsafeMutablePointer<Float>? = nil
    var len = 0
  }

  /// Ring indices and master volume; only touched with `lock` held.
  fileprivate struct SharedState {
    var head = 0, tail = 0  // free-running, head - tail = queued commands
    var master: Float = 0.8
    var musicHead = 0, musicTail = 0  // the same for the music's commands
    var musicVolume: Float = 0.7
    var musicPlaying = -1  // the song playing, written by the render thread
  }

  /// The music's commands besides a piece's number (0 pawn .. 5 king).
  fileprivate enum MusicCommand: Int32 {
    case resume = 10, stop, off, on
  }

  /// Everything the render block touches lives in plain manually allocated
  /// memory, so mixing involves no ARC, no Swift arrays and no allocation. The
  /// struct itself is just pointers and is captured by value in the render block.
  fileprivate struct Mixer: @unchecked Sendable {
    // Stored instead of static so the render thread never hits a lazy global.
    let voiceCount = 40
    let queueCap = 256  // power of two
    let buffersOffset = MemoryLayout<AudioBufferList>.offset(of: \AudioBufferList.mBuffers) ?? 8
    // The effects back to back: the start-up ones in `arena`, the medieval army's
    // and the aliens' in a second block added by publish(). Neither moves until free().
    let soundCount: Int
    let arena: UnsafeMutablePointer<Float>
    let lateArena: UnsafeMutablePointer<UnsafeMutablePointer<Float>?>
    // Where each effect starts (nil while it is still being made) and its length,
    // guarded by `lock`.
    let bufs: UnsafeMutablePointer<UnsafeMutablePointer<Float>?>
    let lengths: UnsafeMutablePointer<Int>
    // play()/setVolume() -> render thread hand-off, guarded by `lock`.
    let lock: UnsafeMutablePointer<os_unfair_lock>
    let shared: UnsafeMutablePointer<SharedState>
    let ring: UnsafeMutablePointer<PlayCmd>
    let musicQueueCap = 16  // power of two
    let musicRing: UnsafeMutablePointer<Int32>
    // Render thread only.
    let voices: UnsafeMutablePointer<Voice>
    let pending: UnsafeMutablePointer<PlayCmd>
    let master: UnsafeMutablePointer<Float>
    let music: UnsafeMutablePointer<MusicEngine>
    let musicPending: UnsafeMutablePointer<Int32>
    let duck: UnsafeMutablePointer<Float>  // per effect: how far it turns the music down

    /// `sounds` has one entry per Sfx; the empty ones are added later by publish().
    init(sounds: [[Float]], master vol: Float, music score: MusicScore, musicOn: Bool, musicVolume: Float,
         duck duckBy: [Float]) {
      soundCount = sounds.count
      bufs = .allocate(capacity: soundCount)
      bufs.initialize(repeating: nil, count: soundCount)
      lengths = .allocate(capacity: soundCount)
      lengths.initialize(repeating: 0, count: soundCount)
      arena = Mixer.pack(sounds, bufs, lengths)
      lateArena = .allocate(capacity: 1)
      lateArena.initialize(to: nil)
      lock = .allocate(capacity: 1)
      lock.initialize(to: os_unfair_lock())
      shared = .allocate(capacity: 1)
      shared.initialize(to: SharedState(master: vol))
      ring = .allocate(capacity: queueCap)
      ring.initialize(repeating: PlayCmd(), count: queueCap)
      voices = .allocate(capacity: voiceCount)
      voices.initialize(repeating: Voice(), count: voiceCount)
      pending = .allocate(capacity: queueCap)
      pending.initialize(repeating: PlayCmd(), count: queueCap)
      master = .allocate(capacity: 1)
      master.initialize(to: vol)
      shared.pointee.musicVolume = musicVolume
      musicRing = .allocate(capacity: musicQueueCap)
      musicRing.initialize(repeating: 0, count: musicQueueCap)
      musicPending = .allocate(capacity: musicQueueCap)
      musicPending.initialize(repeating: 0, count: musicQueueCap)
      duck = .allocate(capacity: soundCount)
      duck.initialize(repeating: 1, count: soundCount)
      for (i, d) in duckBy.prefix(soundCount).enumerated() { duck[i] = d }
      // set up before the render thread first sees it
      music = .allocate(capacity: 1)
      music.initialize(to: MusicEngine(score))
      music.pointee.seed(UInt32.random(in: 1...UInt32.max))
      music.pointee.setEnabled(musicOn)
      music.pointee.setVolume(musicVolume)
    }

    /// Copies the non-empty sounds into one new block and points bufs/lengths at them.
    private static func pack(_ sounds: [[Float]], _ bufs: UnsafeMutablePointer<UnsafeMutablePointer<Float>?>,
                             _ lengths: UnsafeMutablePointer<Int>) -> UnsafeMutablePointer<Float> {
      let total = sounds.reduce(0) { $0 + $1.count }
      let a = UnsafeMutablePointer<Float>.allocate(capacity: max(total, 1))
      var o = 0
      for (i, s) in sounds.enumerated() where !s.isEmpty {
        s.withUnsafeBufferPointer { if let p = $0.baseAddress { (a + o).initialize(from: p, count: s.count) } }
        bufs[i] = a + o
        lengths[i] = s.count
        o += s.count
      }
      return a
    }

    /// Any thread, once: makes the effects that were still missing at init playable
    /// (their entries in `sounds`; the others are ignored).
    func publish(_ sounds: [[Float]]) {
      let n = min(sounds.count, soundCount)
      let fresh = Array(sounds.prefix(n))
      let b = UnsafeMutablePointer<UnsafeMutablePointer<Float>?>.allocate(capacity: max(n, 1))
      b.initialize(repeating: nil, count: max(n, 1))
      let l = UnsafeMutablePointer<Int>.allocate(capacity: max(n, 1))
      l.initialize(repeating: 0, count: max(n, 1))
      let block = Mixer.pack(fresh, b, l)
      os_unfair_lock_lock(lock)
      let first = lateArena.pointee == nil
      if first {
        lateArena.pointee = block
        for i in 0..<n where b[i] != nil && bufs[i] == nil {
          bufs[i] = b[i]
          lengths[i] = l[i]
        }
      }
      os_unfair_lock_unlock(lock)
      if !first { block.deallocate() }
      b.deallocate()
      l.deallocate()
    }

    /// Only once the engine is stopped and the source node released.
    func free() {
      arena.deallocate()
      lateArena.pointee?.deallocate()
      lateArena.deallocate()
      bufs.deallocate()
      lengths.deallocate()
      lock.deallocate()
      shared.deallocate()
      ring.deallocate()
      voices.deallocate()
      pending.deallocate()
      master.deallocate()
      music.pointee.free()
      music.deinitialize(count: 1)
      music.deallocate()
      musicRing.deallocate()
      musicPending.deallocate()
      duck.deallocate()
    }

    /// Any thread. The critical section is a handful of stores; when the queue
    /// is full (render thread stalled) the request is dropped.
    func push(_ c: PlayCmd) {
      os_unfair_lock_lock(lock)
      if shared.pointee.head &- shared.pointee.tail < queueCap {
        ring[shared.pointee.head & (queueCap - 1)] = c
        shared.pointee.head &+= 1
      }
      os_unfair_lock_unlock(lock)
    }

    func setMaster(_ v: Float) {
      os_unfair_lock_lock(lock)
      shared.pointee.master = v
      os_unfair_lock_unlock(lock)
    }

    /// Any thread: a command for the music, carried out on the render thread.
    func pushMusic(_ c: Int32) {
      os_unfair_lock_lock(lock)
      if shared.pointee.musicHead &- shared.pointee.musicTail < musicQueueCap {
        musicRing[shared.pointee.musicHead & (musicQueueCap - 1)] = c
        shared.pointee.musicHead &+= 1
      }
      os_unfair_lock_unlock(lock)
    }

    func setMusicVolume(_ v: Float) {
      os_unfair_lock_lock(lock)
      shared.pointee.musicVolume = v
      os_unfair_lock_unlock(lock)
    }

    /// The song playing (as of the last block mixed), or -1.
    func playingSong() -> Int {
      os_unfair_lock_lock(lock)
      let s = shared.pointee.musicPlaying
      os_unfair_lock_unlock(lock)
      return s
    }

    /// Render thread: never blocks. If a play() holds the lock right now the
    /// queue is simply picked up on the next block.
    private func drainQueue() {
      guard os_unfair_lock_trylock(lock) else { return }
      let tail = shared.pointee.tail, n = shared.pointee.head &- tail
      for k in 0..<n {
        var c = ring[(tail &+ k) & (queueCap - 1)]
        let s = Int(c.sfx)
        if s >= 0 && s < soundCount {
          c.buf = bufs[s]
          c.len = lengths[s]
        }
        pending[k] = c
      }
      shared.pointee.tail = shared.pointee.head
      master.pointee = shared.pointee.master
      let mt = shared.pointee.musicTail, mn = shared.pointee.musicHead &- mt
      for k in 0..<mn { musicPending[k] = musicRing[(mt &+ k) & (musicQueueCap - 1)] }
      shared.pointee.musicTail = shared.pointee.musicHead
      let musicVolume = shared.pointee.musicVolume
      shared.pointee.musicPlaying = music.pointee.playing()
      os_unfair_lock_unlock(lock)
      for k in 0..<mn {
        let c = musicPending[k]
        switch MusicCommand(rawValue: c) {
        case .resume: music.pointee.resume()
        case .stop: music.pointee.stop()
        case .off: music.pointee.setEnabled(false)
        case .on: music.pointee.setEnabled(true)
        case nil: music.pointee.play(Int(c))
        }
      }
      music.pointee.setVolume(musicVolume)
      for k in 0..<n { startVoice(pending[k]) }
    }

    private func startVoice(_ c: PlayCmd) {
      guard c.buf != nil && c.len > 1 else { return }  // not made yet: silence
      var slot = -1
      for k in 0..<voiceCount where voices[k].sfx < 0 {
        slot = k
        break
      }
      if slot < 0 {  // steal the oldest-progressed voice
        slot = 0
        for k in 0..<voiceCount where voices[k].pos > voices[slot].pos { slot = k }
      }
      voices[slot] = Voice(sfx: c.sfx, pos: 0, rate: c.rate, l: c.l, r: c.r, buf: c.buf, len: c.len)
      let depth = duck[Int(c.sfx)]
      if depth < 1 { music.pointee.duck(Int(Float(c.len) / c.rate), depth) }
    }

    /// Render block body. Returns true when the output is pure silence.
    func render(_ frames: Int, _ abl: UnsafeMutablePointer<AudioBufferList>) -> Bool {
      // Plain C access to the buffer array: UnsafeMutableAudioBufferListPointer is
      // a resilient overlay type and would call into the Swift runtime here.
      let count = Int(abl.pointee.mNumberBuffers)
      let bufs = (UnsafeMutableRawPointer(abl) + buffersOffset).assumingMemoryBound(to: AudioBuffer.self)
      guard count > 0, let p0 = bufs[0].mData?.assumingMemoryBound(to: Float.self) else { return true }
      // Our format is deinterleaved stereo (two buffers); cope with interleaved too.
      let outL = p0, outR: UnsafeMutablePointer<Float>, step: Int
      if count > 1, let p1 = bufs[1].mData?.assumingMemoryBound(to: Float.self) {
        outR = p1
        step = 1
      } else if bufs[0].mNumberChannels > 1 {
        outR = p0 + 1
        step = Int(bufs[0].mNumberChannels)
      } else {
        for i in 0..<frames { p0[i] = 0 }
        return true
      }

      drainQueue()
      for i in 0..<frames {
        outL[i * step] = 0
        outR[i * step] = 0
      }
      var active = false
      for k in 0..<voiceCount {
        let v = voices + k
        guard v.pointee.sfx >= 0, let b = v.pointee.buf else { continue }
        active = true
        let len = v.pointee.len
        let rate = Double(v.pointee.rate), gl = v.pointee.l, gr = v.pointee.r
        var pos = v.pointee.pos
        for i in 0..<frames {
          let p = Int(pos)
          if p + 1 >= len {
            v.pointee.sfx = -1
            break
          }
          let fr = Float(pos - Double(p))
          let s = b[p] * (1 - fr) + b[p + 1] * fr
          outL[i * step] += s * gl
          outR[i * step] += s * gr
          pos += rate
        }
        v.pointee.pos = pos
      }
      if music.pointee.render(outL, outR, frames, step) { active = true }
      if !active { return true }
      let m = master.pointee
      for i in 0..<frames {
        outL[i * step] = tanh(outL[i * step] * m)
        outR[i * step] = tanh(outR[i * step] * m)
      }
      return false
    }
  }
}

// MARK: - Audio

final class Audio {
  private(set) var ok = false
  private var volume_: Float = 0.8
  var volume: Float { volume_ }
  /// The background music (Music.swift): every piece type has its own themes.
  private(set) var music = true
  private(set) var musicVolume: Float = 0.7
  private var score: MusicScore?

  private var mixer: Mixer?
  // Engine objects are only touched on `control`, which serialises start,
  // shutdown and restarts after output device changes.
  private let control = DispatchQueue(label: "LivingMarble.Audio")
  private var engine: AVAudioEngine?
  private var source: AVAudioSourceNode?
  private var observers: [NSObjectProtocol] = []
  // the medieval army's, the aliens' and the ghosts' sounds, made in the background after start()
  private let lateSynth = DispatchGroup()

  init() {}
  deinit { shutdown() }

  /// Synthesises every effect and opens the output; false if there is no device.
  @discardableResult func start() -> Bool {
    if ok { return true }
#if os(iOS)
    // Activating the audio session can block for a while, so it happens on the
    // audio queue instead of the caller's (main) thread. iOS always has an output.
    let m = makeMixer()
    mixer = m
    ok = true
    synthesiseLate(m)
    control.async { [weak self] in
      guard let self else { return }
      if Audio.prepareOutput() { _ = self.startEngine(m) }
    }
    return true
#else
    guard Audio.prepareOutput() else { return false }
    let m = makeMixer()
    let started = control.sync { startEngine(m) }
    guard started else {
      m.free()
      return false
    }
    mixer = m
    ok = true
    synthesiseLate(m)
    return true
#endif
  }

  private func makeMixer() -> Mixer {
    let s = MusicScore(musicScoreText)
    for w in s.warnings { print("[music] \(w)") }
    score = s
    return Mixer(sounds: Synth.synthAll(), master: volume_, music: s, musicOn: music, musicVolume: musicVolume,
                 duck: Sfx.allCases.map { Audio.musicDuck($0) })
  }

  /// How far the music is turned down while a sound effect plays: the ones that are
  /// music themselves (the organ, the theremin, the bells ...) would clash with it.
  private static func musicDuck(_ s: Sfx) -> Float {
    switch s {
    case .hymn: return 0.12
    case .organ, .theremin: return 0.2
    case .mate: return 0.25
    case .bell, .drone: return 0.35
    case .check, .horn: return 0.45
    default: return 1
    }
  }

  /// Makes the medieval army's, the aliens' and the ghosts' sounds on another
  /// thread; until they are ready, playing one of them does nothing.
  private func synthesiseLate(_ m: Mixer) {
    DispatchQueue.global(qos: .userInitiated).async(group: lateSynth) {
      var S = [[Float]](repeating: [], count: Sfx.allCases.count)
      Synth.synthMedieval(&S)
      Synth.synthAlien(&S)
      Synth.synthGhosts(&S)
      m.publish(S)
    }
  }

  func shutdown() {
    control.sync {
      // on the audio queue, where a pending start may still be adding observers
      for o in observers { NotificationCenter.default.removeObserver(o) }
      observers.removeAll()
      if let e = engine {
        e.stop()
        if let s = source { e.detach(s) }
      }
      engine = nil
      source = nil
    }
    lateSynth.wait()  // it writes into the mixer
    mixer?.free()
    mixer = nil
    ok = false
  }

  /// pan -1..1, pitch multiplier. Safe from any thread.
  func play(_ s: Sfx, volume: Float = 1, pan: Float = 0, pitch: Float = 1) {
    guard ok, let m = mixer else { return }
    let a = (pan + 1) * 0.25 * 3.14159265  // equal-power pan
    m.push(PlayCmd(sfx: Int32(s.rawValue), rate: max(0.25, pitch), l: volume * cos(a), r: volume * sin(a)))
  }

  func setVolume(_ v: Float) {
    volume_ = v
    mixer?.setMaster(v)
  }

  /// The music of this piece (0 pawn .. 5 king), from the next bar. Safe from any thread.
  func playMusic(_ piece: Int) { mixer?.pushMusic(Int32(piece)) }
  /// A new game: starts the music again if it was stopped.
  func resumeMusic() { mixer?.pushMusic(MusicCommand.resume.rawValue) }
  /// The game is over: fades the music out.
  func stopMusic() { mixer?.pushMusic(MusicCommand.stop.rawValue) }

  func setMusic(_ on: Bool) {
    music = on
    mixer?.pushMusic(on ? MusicCommand.on.rawValue : MusicCommand.off.rawValue)
  }

  func setMusicVolume(_ v: Float) {
    musicVolume = v
    mixer?.setMusicVolume(v)
  }

  /// "Gallop (knight)", or "" while silent.
  func nowPlaying() -> String {
    guard let m = mixer, let score else { return "" }
    let s = m.playingSong()
    return s >= 0 && s < score.songCount ? "\(score.titles[s]) (\(musicPieceNames[score.songPiece(s)]))" : ""
  }

  /// Starts the engine again if something stopped it (on iOS: a call or Siri, or the
  /// app coming back from the background).
  func resume() {
    guard ok else { return }
    control.async { [weak self] in
      guard let self, let e = self.engine, !e.isRunning else { return }
#if os(iOS)
      try? AVAudioSession.sharedInstance().setActive(true)
#endif
      self.restartEngine()
    }
  }

  // On `control`.
  private func startEngine(_ m: Mixer) -> Bool {
    let e = AVAudioEngine()
    let hw = e.outputNode.outputFormat(forBus: 0)
    guard hw.sampleRate > 0, hw.channelCount > 0,
          let fmt = AVAudioFormat(standardFormatWithSampleRate: Double(Synth.SR), channels: 2) else { return false }
    let src = AVAudioSourceNode(format: fmt) { silence, _, frames, abl in
      if m.render(Int(frames), abl) { silence.pointee = true }
      return noErr
    }
    e.attach(src)
    e.connect(src, to: e.mainMixerNode, format: fmt)
    e.prepare()
    do {
      try e.start()
    } catch {
      print("[audio] could not start the audio engine: \(error)")
      e.detach(src)
      return false
    }
    engine = e
    source = src
    // The engine stops itself when the output device or its format changes.
    observers.append(NotificationCenter.default.addObserver(
      forName: .AVAudioEngineConfigurationChange, object: e, queue: nil) { [weak self] _ in
      self?.control.async { [weak self] in self?.restartEngine() }
    })
#if os(iOS)
    // ... and when a call or another app interrupts it: pick up again once that ends
    observers.append(NotificationCenter.default.addObserver(
      forName: AVAudioSession.interruptionNotification, object: nil, queue: nil) { [weak self] n in
      guard let raw = n.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt,
            AVAudioSession.InterruptionType(rawValue: raw) == .ended else { return }
      self?.resume()
    })
#endif
    return true
  }

  // On `control`. Reconnects the mixer to the (possibly new) hardware format
  // and starts again; if there is no usable device now, waits for the next change.
  private func restartEngine() {
    guard let e = engine else { return }
    e.stop()
    let hw = e.outputNode.outputFormat(forBus: 0)
    guard hw.sampleRate > 0, hw.channelCount > 0 else { return }
    e.connect(e.mainMixerNode, to: e.outputNode, format: hw)
    e.prepare()
    do {
      try e.start()
    } catch {
      print("[audio] could not restart the audio engine: \(error)")
    }
  }

#if os(macOS)
  /// True when there is an output device to play on.
  private static func prepareOutput() -> Bool {
    var dev = AudioObjectID(kAudioObjectUnknown)
    var size = UInt32(MemoryLayout<AudioObjectID>.size)
    var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDefaultOutputDevice,
                                          mScope: kAudioObjectPropertyScopeGlobal,
                                          mElement: kAudioObjectPropertyElementMain)
    let err = AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &dev)
    return err == noErr && dev != kAudioObjectUnknown
  }
#else
  /// Game sounds play even in silent mode (most phones live there), mixed with the player's music.
  private static func prepareOutput() -> Bool {
    let session = AVAudioSession.sharedInstance()
    do {
      try session.setCategory(.playback, options: [.mixWithOthers])
      try session.setActive(true)
    } catch {
      print("[audio] could not set up the audio session: \(error)")
      return false
    }
    return true
  }
#endif

  /// Debug: writes every effect as a 16-bit mono .wav, and every song of the music in
  /// stereo as music-<piece>-<title>.wav (same names as the C++).
  static func dumpWav(_ dir: String) -> Bool {
    let names = ["step", "thud", "clack", "shatter", "whoosh", "clang", "boom", "explode", "charge",
                 "zap", "crackle", "chime", "select", "check", "mate", "debris", "rumble",
                 "hymn", "organ", "thunder", "wind", "roar", "fire", "chain", "grind", "stomp", "hoof",
                 "neigh", "bell", "splinter", "armor", "slide", "bite", "pew", "ufo", "theremin", "beam",
                 "gibber", "slurp", "burp", "boing", "whistle", "splat", "moo", "pop", "shrink", "heatray",
                 "horn", "twinkle", "sizzle", "hatch", "drone", "sputter", "cheer"]
    let S = Synth.synthEverything()
    for (i, buf) in S.enumerated() {
      let n = UInt32(buf.count), bytes = n * 2
      var d = Data(capacity: 44 + Int(bytes))
      func w32(_ v: UInt32) { withUnsafeBytes(of: v.littleEndian) { d.append(contentsOf: $0) } }
      func w16(_ v: UInt16) { withUnsafeBytes(of: v.littleEndian) { d.append(contentsOf: $0) } }
      d.append(contentsOf: Array("RIFF".utf8)); w32(36 + bytes); d.append(contentsOf: Array("WAVEfmt ".utf8))
      w32(16); w16(1); w16(1); w32(UInt32(Synth.SR)); w32(UInt32(Synth.SR * 2)); w16(2); w16(16)
      d.append(contentsOf: Array("data".utf8)); w32(bytes)
      for v in buf { w16(UInt16(bitPattern: Int16(max(-32767, min(32767, v * 32767))))) }
      if !FileManager.default.createFile(atPath: dir + "/" + names[i] + ".wav", contents: d) { return false }
    }
    let score = MusicScore(musicScoreText)
    for w in score.warnings { print("[music] \(w)") }
    for i in 0..<score.songCount {
      let st = MusicEngine.renderSong(score, i)
      let n = UInt32(st.count / 2), bytes = n * 4
      var d = Data(capacity: 44 + Int(bytes))
      func w32(_ v: UInt32) { withUnsafeBytes(of: v.littleEndian) { d.append(contentsOf: $0) } }
      func w16(_ v: UInt16) { withUnsafeBytes(of: v.littleEndian) { d.append(contentsOf: $0) } }
      d.append(contentsOf: Array("RIFF".utf8)); w32(36 + bytes); d.append(contentsOf: Array("WAVEfmt ".utf8))
      w32(16); w16(1); w16(2); w32(UInt32(Synth.SR)); w32(UInt32(Synth.SR * 4)); w16(4); w16(16)
      d.append(contentsOf: Array("data".utf8)); w32(bytes)
      for v in st { w16(UInt16(bitPattern: Int16(max(-32767, min(32767, v * 32767))))) }
      let title = String(score.titles[i].lowercased().compactMap { $0 == " " ? "-" : $0.isASCII && ($0.isLetter || $0.isNumber) ? $0 : nil })
      let path = dir + "/music-" + musicPieceNames[score.songPiece(i)] + "-" + title + ".wav"
      if !FileManager.default.createFile(atPath: path, contents: d) { return false }
    }
    return true
  }
}
