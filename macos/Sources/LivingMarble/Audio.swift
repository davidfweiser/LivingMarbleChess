// Procedurally synthesised sound effects (no audio files), mixed in real time.
// Every effect is built once at start-up from damped sines, filtered noise and
// a few oscillators. An AVAudioSourceNode mixes up to 40 voices (pan, pitch via
// resampling, volume) at 44.1 kHz and AVAudioEngine converts to the device rate.
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
  }

  // MARK: - Real-time mixer

  /// One play() request, handed from the game threads to the render thread.
  fileprivate struct PlayCmd {
    var sfx: Int32 = -1
    var rate: Float = 1, l: Float = 0, r: Float = 0
  }

  fileprivate struct Voice {
    var sfx: Int32 = -1  // -1 = free
    var pos: Double = 0
    var rate: Float = 1, l: Float = 0, r: Float = 0
  }

  /// Ring indices and master volume; only touched with `lock` held.
  fileprivate struct SharedState {
    var head = 0, tail = 0  // free-running, head - tail = queued commands
    var master: Float = 0.8
  }

  /// Everything the render block touches lives in plain manually allocated
  /// memory, so mixing involves no ARC, no Swift arrays and no allocation. The
  /// struct itself is just pointers and is captured by value in the render block.
  fileprivate struct Mixer: @unchecked Sendable {
    // Stored instead of static so the render thread never hits a lazy global.
    let voiceCount = 40
    let queueCap = 256  // power of two
    let buffersOffset = MemoryLayout<AudioBufferList>.offset(of: \AudioBufferList.mBuffers) ?? 8
    // Read-only after init: every effect back to back, with offsets/lengths.
    let arena: UnsafeMutablePointer<Float>
    let offsets: UnsafeMutablePointer<Int>
    let lengths: UnsafeMutablePointer<Int>
    // play()/setVolume() -> render thread hand-off, guarded by `lock`.
    let lock: UnsafeMutablePointer<os_unfair_lock>
    let shared: UnsafeMutablePointer<SharedState>
    let ring: UnsafeMutablePointer<PlayCmd>
    // Render thread only.
    let voices: UnsafeMutablePointer<Voice>
    let pending: UnsafeMutablePointer<PlayCmd>
    let master: UnsafeMutablePointer<Float>

    init(sounds: [[Float]], master vol: Float) {
      let total = sounds.reduce(0) { $0 + $1.count }
      let a = UnsafeMutablePointer<Float>.allocate(capacity: max(total, 1))
      offsets = .allocate(capacity: sounds.count)
      lengths = .allocate(capacity: sounds.count)
      var o = 0
      for (i, s) in sounds.enumerated() {
        offsets[i] = o
        lengths[i] = s.count
        s.withUnsafeBufferPointer { if let p = $0.baseAddress { (a + o).initialize(from: p, count: s.count) } }
        o += s.count
      }
      arena = a
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
    }

    /// Only once the engine is stopped and the source node released.
    func free() {
      arena.deallocate()
      offsets.deallocate()
      lengths.deallocate()
      lock.deallocate()
      shared.deallocate()
      ring.deallocate()
      voices.deallocate()
      pending.deallocate()
      master.deallocate()
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

    /// Render thread: never blocks. If a play() holds the lock right now the
    /// queue is simply picked up on the next block.
    private func drainQueue() {
      guard os_unfair_lock_trylock(lock) else { return }
      let tail = shared.pointee.tail, n = shared.pointee.head &- tail
      for k in 0..<n { pending[k] = ring[(tail &+ k) & (queueCap - 1)] }
      shared.pointee.tail = shared.pointee.head
      master.pointee = shared.pointee.master
      os_unfair_lock_unlock(lock)
      for k in 0..<n { startVoice(pending[k]) }
    }

    private func startVoice(_ c: PlayCmd) {
      var slot = -1
      for k in 0..<voiceCount where voices[k].sfx < 0 {
        slot = k
        break
      }
      if slot < 0 {  // steal the oldest-progressed voice
        slot = 0
        for k in 0..<voiceCount where voices[k].pos > voices[slot].pos { slot = k }
      }
      voices[slot] = Voice(sfx: c.sfx, pos: 0, rate: c.rate, l: c.l, r: c.r)
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
        let sfx = Int(v.pointee.sfx)
        if sfx < 0 { continue }
        active = true
        let b = arena + offsets[sfx], len = lengths[sfx]
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

  private var mixer: Mixer?
  // Engine objects are only touched on `control`, which serialises start,
  // shutdown and restarts after output device changes.
  private let control = DispatchQueue(label: "LivingMarble.Audio")
  private var engine: AVAudioEngine?
  private var source: AVAudioSourceNode?
  private var observers: [NSObjectProtocol] = []

  init() {}
  deinit { shutdown() }

  /// Synthesises every effect and opens the output; false if there is no device.
  @discardableResult func start() -> Bool {
    if ok { return true }
#if os(iOS)
    // Activating the audio session can block for a while, so it happens on the
    // audio queue instead of the caller's (main) thread. iOS always has an output.
    let m = Mixer(sounds: Synth.synthAll(), master: volume_)
    mixer = m
    ok = true
    control.async { [weak self] in
      guard let self else { return }
      if Audio.prepareOutput() { _ = self.startEngine(m) }
    }
    return true
#else
    guard Audio.prepareOutput() else { return false }
    let m = Mixer(sounds: Synth.synthAll(), master: volume_)
    let started = control.sync { startEngine(m) }
    guard started else {
      m.free()
      return false
    }
    mixer = m
    ok = true
    return true
#endif
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

  /// Debug: writes every effect as a 16-bit mono .wav (same names as the C++).
  static func dumpWav(_ dir: String) -> Bool {
    let names = ["step", "thud", "clack", "shatter", "whoosh", "clang", "boom", "explode", "charge",
                 "zap", "crackle", "chime", "select", "check", "mate", "debris", "rumble"]
    let S = Synth.synthAll()
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
    return true
  }
}
