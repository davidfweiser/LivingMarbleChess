// CPU-baked procedural textures: inlaid wooden chessboard, 3D noise.
import Foundation
import simd

enum Textures {
  @inline(__always) private static func hashU(_ x0: UInt32) -> UInt32 {
    var x = x0
    x ^= x >> 16
    x = x &* 0x7feb_352d
    x ^= x >> 15
    x = x &* 0x846c_a68b
    x ^= x >> 16
    return x
  }

  @inline(__always) private static func h2(_ x: Int, _ y: Int, _ seed: UInt32) -> Float {
    let k = UInt32(truncatingIfNeeded: x) &* 1_597_334_677 ^ UInt32(truncatingIfNeeded: y) &* 3_812_015_801
      ^ seed &* 2_654_435_769
    return Float(hashU(k) & 0xffffff) / Float(0xffffff)
  }

  @inline(__always) private static func h3(_ x: Int, _ y: Int, _ z: Int, _ seed: UInt32) -> Float {
    let k = UInt32(truncatingIfNeeded: x) &* 1_597_334_677 ^ UInt32(truncatingIfNeeded: y) &* 3_812_015_801
      ^ UInt32(truncatingIfNeeded: z) &* 2_798_796_415 ^ seed &* 2_654_435_769
    return Float(hashU(k) & 0xffffff) / Float(0xffffff)
  }

  @inline(__always) private static func fade(_ t: Float) -> Float { t * t * t * (t * (t * 6 - 15) + 10) }

  @inline(__always) private static func noise2(_ x: Float, _ y: Float, _ seed: UInt32) -> Float {
    let fxf = x.rounded(.down), fyf = y.rounded(.down)
    let ix = Int(fxf), iy = Int(fyf)
    let fx = x - fxf, fy = y - fyf
    let u = fade(fx), v = fade(fy)
    let a = h2(ix, iy, seed), b = h2(ix + 1, iy, seed), c = h2(ix, iy + 1, seed), d = h2(ix + 1, iy + 1, seed)
    return mix(mix(a, b, u), mix(c, d, u), v)
  }

  @inline(__always) private static func fbm2(_ x0: Float, _ y0: Float, _ seed: UInt32, _ oct: Int = 4) -> Float {
    var x = x0, y = y0
    var s: Float = 0, a: Float = 0.5, n: Float = 0
    for i in 0..<oct {
      s += a * noise2(x, y, seed &+ UInt32(i) &* 31)
      n += a
      x *= 2.03
      y *= 2.03
      a *= 0.5
    }
    return s / n
  }

  // ---------------------------------------------------------------------------
  // Stroke font for board coordinates (a-h, 1-8). Glyph box ~ 0.6 x 0.85.
  // ---------------------------------------------------------------------------
  private typealias Poly = [vec2]
  private typealias Glyph = [Poly]

  private static func arc(_ cx: Float, _ cy: Float, _ rx: Float, _ ry: Float, _ a0: Float, _ a1: Float,
                          _ n: Int = 24) -> Poly {
    var p: Poly = []
    for i in 0...n {
      let a = radians(a0 + (a1 - a0) * Float(i) / Float(n))
      p.append(vec2(cx + rx * cos(a), cy + ry * sin(a)))
    }
    return p
  }

  private static func line(_ l: [Float]) -> Poly {
    stride(from: 0, to: l.count, by: 2).map { vec2(l[$0], l[$0 + 1]) }
  }

  private static func glyphs() -> [Glyph] {
    var g = [Glyph](repeating: [], count: 16)
    // letters a..h
    g[0] = [arc(0.27, 0.27, 0.22, 0.27, 0, 360), line([0.49, 0.56, 0.49, 0.0])]
    g[1] = [line([0.06, 0.86, 0.06, 0.0]), arc(0.28, 0.27, 0.22, 0.27, 0, 360)]
    g[2] = [arc(0.29, 0.27, 0.235, 0.27, 40, 320)]
    g[3] = [arc(0.25, 0.27, 0.22, 0.27, 0, 360), line([0.47, 0.86, 0.47, 0.0])]
    g[4] = [line([0.06, 0.28, 0.52, 0.28]), arc(0.29, 0.27, 0.235, 0.27, 0, 318)]
    g[5] = [line([0.20, 0.0, 0.20, 0.66]), arc(0.38, 0.66, 0.18, 0.18, 180, 30),
            line([0.04, 0.52, 0.40, 0.52])]
    g[6] = [arc(0.25, 0.33, 0.215, 0.235, 0, 360), line([0.465, 0.57, 0.465, -0.05]),
            arc(0.255, -0.05, 0.21, 0.17, 0, -165)]
    g[7] = [line([0.06, 0.86, 0.06, 0.0]), arc(0.275, 0.33, 0.215, 0.22, 180, 0),
            line([0.49, 0.33, 0.49, 0.0])]
    // digits 1..8
    g[8] = [line([0.13, 0.68, 0.32, 0.85, 0.32, 0.0]), line([0.12, 0.0, 0.52, 0.0])]
    do {
      var p = arc(0.29, 0.62, 0.22, 0.22, 165, -35)
      p.append(vec2(0.06, 0.0))
      p.append(vec2(0.53, 0.0))
      g[9] = [p]
    }
    g[10] = [arc(0.28, 0.645, 0.20, 0.20, 150, -90), arc(0.28, 0.24, 0.24, 0.24, 90, -150)]
    g[11] = [line([0.40, 0.0, 0.40, 0.85, 0.04, 0.28, 0.56, 0.28])]
    do {
      let p = line([0.50, 0.85, 0.11, 0.85, 0.08, 0.47])
      let q = arc(0.28, 0.28, 0.245, 0.28, 135, -150)
      g[12] = [p, q]
    }
    g[13] = [arc(0.29, 0.27, 0.23, 0.27, 0, 360), arc(0.50, 0.30, 0.44, 0.55, 180, 100)]
    g[14] = [line([0.05, 0.85, 0.53, 0.85, 0.20, 0.0])]
    g[15] = [arc(0.28, 0.645, 0.19, 0.20, 0, 360), arc(0.28, 0.235, 0.235, 0.235, 0, 360)]
    return g
  }

  /// Glyph strokes flattened to segments (a, b), with segStart[g]..<segStart[g + 1] per glyph.
  private struct GlyphSegments {
    var seg: [vec4] = []  // xy = a, zw = b
    var segStart: [Int] = []
    init(_ G: [Glyph]) {
      for g in G {
        segStart.append(seg.count)
        for pl in g where pl.count > 1 {
          for i in 0..<(pl.count - 1) { seg.append(vec4(pl[i].x, pl[i].y, pl[i + 1].x, pl[i + 1].y)) }
        }
      }
      segStart.append(seg.count)
    }
  }

  @inline(__always) private static func distToGlyph(_ seg: UnsafePointer<vec4>, _ s0: Int, _ s1: Int,
                                                    _ p: vec2) -> Float {
    var best: Float = 1e9
    for k in s0..<s1 {
      let a = vec2(seg[k].x, seg[k].y), b = vec2(seg[k].z, seg[k].w)
      let pa = p - a, ba = b - a
      let h = clamp(dot(pa, ba) / max(1e-9, dot(ba, ba)), 0, 1)
      best = min(best, length(pa - ba * h))
    }
    return best
  }

  private struct WoodParams {
    var y0: Float = 0, z0: Float = 0, tilt: Float = 0, freq: Float = 0, angle: Float = 0
    var early = vec3(0), late = vec3(0)
    var seed: UInt32 = 0
  }

  /// Returns the color (sRGB-ish), and outputs height and roughness.
  @inline(__always) private static func wood(_ w: WoodParams, _ u: Float, _ v: Float, _ height: inout Float,
                                             _ rough: inout Float) -> vec3 {
    let ca = cos(w.angle), sa = sin(w.angle)
    let uu = ca * u - sa * v, vv = sa * u + ca * v
    // growth rings of a log lying along uu, cut tangentially
    let wy = vv + w.y0 + 0.11 * (fbm2(uu * 0.55, vv * 2.2, w.seed) - 0.5)
    let wz = w.z0 + uu * w.tilt + 0.06 * (fbm2(uu * 1.3 + 5.0, vv * 3.0, w.seed &+ 7) - 0.5)
    let r = (wy * wy + wz * wz).squareRoot()
    var ring = r * w.freq + 0.35 * fbm2(uu * 0.9, vv * 9.0, w.seed &+ 13)
    ring -= ring.rounded(.down)
    let late = smoothstep(0.50, 0.86, ring) * (1 - smoothstep(0.90, 1.0, ring))
    let fiber = fbm2(uu * 2.5, vv * 140.0, w.seed &+ 21, 3)
    let pn = noise2(uu * 34.0, vv * 420.0, w.seed &+ 33)
    let pore = smoothstep(0.70, 0.86, pn)
    let fig = fbm2(uu * 0.35, vv * 0.8, w.seed &+ 41, 3)
    var c = mix(w.early, w.late, late * 0.85)
    c *= 0.91 + 0.14 * fiber
    c *= 0.93 + 0.14 * fig
    c *= 1 - 0.30 * pore
    height = -0.45 * late - 0.9 * pore + 0.15 * fiber
    rough = 0.38 + 0.18 * pore + 0.06 * late
    return c
  }

  @inline(__always) private static func q8(_ v: Float) -> UInt8 { UInt8(clamp(v * 255 + 0.5, 0, 255)) }

  // ---------------------------------------------------------------------------
  /// Board top: 'albedo' (sRGB rgb + roughness in a) and 'detail'
  /// (r,g = normal xy * 0.5 + 0.5, b = gold inlay mask, a = cavity/seam).
  /// Covers [-half, half]^2 in world xz with +z toward white.
  static func bakeBoard(res: Int, half: Float) -> (albedo: TexImage, detail: TexImage) {
    if res <= 0 { return (TexImage(), TexImage()) }
    var albedo = TexImage(w: res, h: res, d: 1, ch: 4, px: [UInt8](repeating: 0, count: res * res * 4))
    var detail = TexImage(w: res, h: res, d: 1, ch: 4, px: [UInt8](repeating: 0, count: res * res * 4))
    let height = UnsafeMutablePointer<Float>.allocate(capacity: res * res)
    let metal = UnsafeMutablePointer<Float>.allocate(capacity: res * res)
    let cav = UnsafeMutablePointer<Float>.allocate(capacity: res * res)
    defer {
      height.deallocate()
      metal.deallocate()
      cav.deallocate()
    }

    var sq = [WoodParams](repeating: WoodParams(), count: 64)
    var frame = [WoodParams](repeating: WoodParams(), count: 4)
    for i in 0..<64 {
      let f = i & 7, r = i >> 3
      let dark = ((f + r) & 1) == 0
      var w = WoodParams()
      w.seed = 1000 + UInt32(i) * 17
      w.y0 = (h2(i, 1, 99) - 0.5) * 4.0
      w.z0 = 0.25 + h2(i, 2, 99) * 0.9
      w.tilt = (h2(i, 3, 99) - 0.5) * 0.16
      w.freq = 15.0 + h2(i, 4, 99) * 8.0
      // grain direction alternates in 2x2 blocks
      w.angle = (((f + r) & 2) != 0 ? 0.0 : 1.5707963) + (h2(i, 5, 99) - 0.5) * 0.06
      let tone = 0.94 + h2(i, 6, 99) * 0.1
      if dark {
        w.early = vec3(0.40, 0.255, 0.165) * tone
        w.late = vec3(0.24, 0.140, 0.085) * tone
      } else {
        w.early = vec3(0.89, 0.76, 0.58) * tone
        w.late = vec3(0.78, 0.61, 0.42) * tone
      }
      sq[i] = w
    }
    for i in 0..<4 {
      var w = WoodParams()
      w.seed = 5000 + UInt32(i) * 101
      w.y0 = (h2(i, 11, 7) - 0.5) * 3.0
      w.z0 = 0.4 + h2(i, 12, 7) * 1.2
      w.tilt = (h2(i, 13, 7) - 0.5) * 0.05
      w.freq = 11.0 + h2(i, 14, 7) * 5.0
      w.angle = 0
      w.early = vec3(0.33, 0.175, 0.10)
      w.late = vec3(0.17, 0.085, 0.045)
      frame[i] = w
    }
    let G = GlyphSegments(glyphs())
    let texel = 2 * half / Float(res)
    let lineA: Float = 4.035, lineAw: Float = 0.011
    let lineB: Float = 4.49, lineBw: Float = 0.007
    let labelC: Float = 4.30, labelSize: Float = 0.235, stroke: Float = 0.022

    // contiguous copies of the lookup tables for the parallel loop
    let sqp = UnsafeMutablePointer<WoodParams>.allocate(capacity: sq.count)
    let frp = UnsafeMutablePointer<WoodParams>.allocate(capacity: frame.count)
    let seg = UnsafeMutablePointer<vec4>.allocate(capacity: max(1, G.seg.count))
    let ss = UnsafeMutablePointer<Int>.allocate(capacity: G.segStart.count)
    sqp.initialize(from: sq, count: sq.count)
    frp.initialize(from: frame, count: frame.count)
    seg.initialize(from: G.seg, count: G.seg.count)
    ss.initialize(from: G.segStart, count: G.segStart.count)
    defer {
      sqp.deallocate()
      frp.deallocate()
      seg.deallocate()
      ss.deallocate()
    }

    albedo.px.withUnsafeMutableBufferPointer { ab in
      let ap = ab.baseAddress!

      // inlaid gold label: glyph gi centred at (cx, cz), rot = 0 white-facing, pi black-facing
      @inline(__always) func label(_ x: Float, _ z: Float, _ gi: Int, _ cx: Float, _ cz: Float,
                                   _ rot: Float) -> Float {
        let dx = x - cx, dz = z - cz
        let c = cos(rot), s = sin(rot)
        let gx = (c * dx - s * dz) / labelSize + 0.29
        let gy = (-(s * dx + c * dz)) / labelSize + 0.42
        if gx < -0.2 || gx > 0.8 || gy < -0.4 || gy > 1.1 { return 0 }
        let d = distToGlyph(seg, ss[gi], ss[gi + 1], vec2(gx, gy)) * labelSize
        return 1 - smoothstep(stroke * 0.5 - texel, stroke * 0.5 + texel, d)
      }

      parallelFor(res) { j in
        for i in 0..<res {
          let x = -half + (Float(i) + 0.5) * texel
          let z = -half + (Float(j) + 0.5) * texel
          let ax = abs(x), az = abs(z)
          var hgt: Float = 0, rough: Float = 0.4, met: Float = 0, cv: Float = 1
          var col: vec3
          if ax < 4.0 && az < 4.0 {
            let f = min(7, Int((x + 4.0).rounded(.down)))
            let r = min(7, Int((4.0 - z).rounded(.down)))
            col = wood(sqp[r * 8 + f], x * 1.0, z * 1.0, &hgt, &rough)
            // seams between squares
            let fx = x + 4.0, fz = z + 4.0
            let sx = abs(fx - fx.rounded()), sz = abs(fz - fz.rounded())
            let s = min(sx, sz)
            let seam = 1 - smoothstep(0.0015, 0.0045, s)
            col *= 1 - 0.55 * seam
            hgt -= 1.6 * seam
            cv = 1 - 0.7 * seam
            rough = mix(rough, 0.8, seam)
          } else {
            let strip: Int
            let u: Float, v: Float
            if ax > az {
              strip = x > 0 ? 0 : 1
              u = z
              v = x
            } else {
              strip = z > 0 ? 2 : 3
              u = x
              v = z
            }
            col = wood(frp[strip], u, v, &hgt, &rough)
            // miter seam at the corners
            let md = abs(ax - az) * 0.7071
            var seam: Float = (ax > 4.0 && az > 4.0) ? 1 - smoothstep(0.0015, 0.004, md) : 0
            // seam between playing field and frame
            let edge = max(ax, az)
            seam = max(seam, 1 - smoothstep(0.0015, 0.0045, abs(edge - 4.0)))
            col *= 1 - 0.5 * seam
            hgt -= 1.5 * seam
            cv = 1 - 0.7 * seam
            // brass inlay lines
            let la = 1 - smoothstep(lineAw - texel, lineAw + texel, abs(edge - lineA))
            let lb = 1 - smoothstep(lineBw - texel, lineBw + texel, abs(edge - lineB))
            var m = max(la, lb)
            // coordinate labels
            var lm: Float = 0
            if az > 4.05 && ax < 4.0 {
              let f = min(7, Int((x + 4.0).rounded(.down)))
              let cx = Float(f) - 3.5
              if z > 0 {
                lm = max(lm, label(x, z, f, cx, labelC, 0.0))
              } else {
                lm = max(lm, label(x, z, f, cx, -labelC, 3.14159265))
              }
            }
            if ax > 4.05 && az < 4.0 {
              let r = min(7, Int((4.0 - z).rounded(.down)))
              let cz = 3.5 - Float(r)
              if x < 0 {
                lm = max(lm, label(x, z, 8 + r, -labelC, cz, 0.0))
              } else {
                lm = max(lm, label(x, z, 8 + r, labelC, cz, 3.14159265))
              }
            }
            m = max(m, lm)
            if m > 0 {
              col = mix(col, vec3(0.95, 0.76, 0.42), m)
              rough = mix(rough, 0.28, m)
              hgt = mix(hgt, -0.3, m)
              met = m
            }
          }
          let k = j * res + i
          height[k] = hgt
          metal[k] = met
          cav[k] = cv
          let a = ap + k * 4
          a[0] = q8(col.x)
          a[1] = q8(col.y)
          a[2] = q8(col.z)
          a[3] = q8(rough)
        }
      }
    }

    // normals from height
    let strength = 0.0011 / texel  // height units -> slope
    detail.px.withUnsafeMutableBufferPointer { db in
      let dp = db.baseAddress!
      parallelFor(res) { j in
        @inline(__always) func H(_ x: Int, _ y: Int) -> Float {
          height[max(0, min(res - 1, y)) * res + max(0, min(res - 1, x))]
        }
        for i in 0..<res {
          let dx = (H(i + 1, j) - H(i - 1, j)) * 0.5 * strength
          let dz = (H(i, j + 1) - H(i, j - 1)) * 0.5 * strength
          let n = normalize(vec3(-dx, 1, -dz))
          let k = j * res + i
          let d = dp + k * 4
          d[0] = q8(n.x * 0.5 + 0.5)
          d[1] = q8(n.z * 0.5 + 0.5)
          d[2] = q8(metal[k])
          d[3] = q8(cav[k])
        }
      }
    }
    return (albedo, detail)
  }

  // ---------------------------------------------------------------------------
  @inline(__always) private static func wrap(_ a: Int, _ period: Int) -> Int { ((a % period) + period) % period }

  /// Value noise that repeats every 'period' lattice cells.
  @inline(__always) private static func tileNoise(_ x: Float, _ y: Float, _ z: Float, _ period: Int,
                                                  _ seed: UInt32) -> Float {
    let flx = x.rounded(.down), fly = y.rounded(.down), flz = z.rounded(.down)
    let ix = Int(flx), iy = Int(fly), iz = Int(flz)
    let u = fade(x - flx), v = fade(y - fly), w = fade(z - flz)
    @inline(__always) func H(_ a: Int, _ b: Int, _ c: Int) -> Float {
      h3(wrap(a, period), wrap(b, period), wrap(c, period), seed)
    }
    let n000 = H(ix, iy, iz), n100 = H(ix + 1, iy, iz), n010 = H(ix, iy + 1, iz), n110 = H(ix + 1, iy + 1, iz)
    let n001 = H(ix, iy, iz + 1), n101 = H(ix + 1, iy, iz + 1), n011 = H(ix, iy + 1, iz + 1)
    let n111 = H(ix + 1, iy + 1, iz + 1)
    return mix(mix(mix(n000, n100, u), mix(n010, n110, u), v),
               mix(mix(n001, n101, u), mix(n011, n111, u), v), w)
  }

  /// Tileable 3D noise: r,g = fbm, b = high-freq value noise, a = worley edge distance.
  static func bakeNoise3D(res: Int) -> TexImage {
    if res <= 0 { return TexImage() }
    var out = TexImage(w: res, h: res, d: res, ch: 4, px: [UInt8](repeating: 0, count: res * res * res * 4))
    let wc = 6  // worley cells per axis
    let feat = UnsafeMutablePointer<vec3>.allocate(capacity: wc * wc * wc)
    defer { feat.deallocate() }
    for i in 0..<(wc * wc * wc) { feat[i] = vec3(h3(i, 0, 0, 777), h3(i, 1, 0, 777), h3(i, 2, 0, 777)) }
    out.px.withUnsafeMutableBufferPointer { ob in
      let op = ob.baseAddress!
      parallelFor(res) { z in
        for y in 0..<res {
          for x in 0..<res {
            let px = (Float(x) + 0.5) / Float(res), py = (Float(y) + 0.5) / Float(res)
            let pz = (Float(z) + 0.5) / Float(res)
            var ch = SIMD3<Float>(0)
            for c in 0..<2 {
              var s: Float = 0, a: Float = 0.5, n: Float = 0
              var per = 4
              for o in 0..<4 {
                let fp = Float(per)
                s += a * tileNoise(px * fp, py * fp, pz * fp, per, UInt32(11 + c * 101 + o * 7))
                n += a
                a *= 0.5
                per *= 2
              }
              ch[c] = s / n
            }
            ch[2] = tileNoise(px * 16, py * 16, pz * 16, 16, 555)
            // worley F2 - F1
            var f1: Float = 1e9, f2: Float = 1e9
            let p = vec3(px * Float(wc), py * Float(wc), pz * Float(wc))
            let cx = Int(p.x.rounded(.down)), cy = Int(p.y.rounded(.down)), cz = Int(p.z.rounded(.down))
            for dz in -1...1 {
              for dy in -1...1 {
                for dx in -1...1 {
                  let gx = cx + dx, gy = cy + dy, gz = cz + dz
                  let wx = wrap(gx, wc), wy = wrap(gy, wc), wz = wrap(gz, wc)
                  let fp = vec3(Float(gx), Float(gy), Float(gz)) + feat[(wz * wc + wy) * wc + wx]
                  let d = length(fp - p)
                  if d < f1 {
                    f2 = f1
                    f1 = d
                  } else if d < f2 {
                    f2 = d
                  }
                }
              }
            }
            let edge = clamp((f2 - f1) * 1.2, 0, 1)
            let o = op + ((z * res + y) * res + x) * 4
            // stretch fbm contrast a bit
            o[0] = q8(clamp((ch[0] - 0.5) * 1.8 + 0.5, 0, 1))
            o[1] = q8(clamp((ch[1] - 0.5) * 1.8 + 0.5, 0, 1))
            o[2] = q8(ch[2])
            o[3] = q8(edge)
          }
        }
      }
    }
    return out
  }
}
