// Chess rules: board representation, legal move generation, SAN, FEN.
import Foundation

/// Namespace for chess constants and free functions.
enum Chess {
  static let white = 0, black = 1
  static let none = 0, pawn = 1, knight = 2, bishop = 3, rook = 4, queen = 5, king = 6

  // Castling-rights bits.
  static let castleWK = 1, castleWQ = 2, castleBK = 4, castleBQ = 8

  // A piece is stored as (type | color << 3); 0 means empty.
  @inline(__always) static func makePiece(_ type: Int, _ color: Int) -> Int { type | (color << 3) }
  @inline(__always) static func pieceType(_ p: Int) -> Int { p & 7 }
  @inline(__always) static func pieceColor(_ p: Int) -> Int { p >> 3 }

  @inline(__always) static func sqFile(_ sq: Int) -> Int { sq & 7 }
  @inline(__always) static func sqRank(_ sq: Int) -> Int { sq >> 3 }
  @inline(__always) static func makeSq(_ file: Int, _ rank: Int) -> Int { rank * 8 + file }

  static func sqName(_ sq: Int) -> String {
    String(decoding: [UInt8(truncatingIfNeeded: 97 + sqFile(sq)), UInt8(truncatingIfNeeded: 49 + sqRank(sq))],
           as: UTF8.self)
  }

  /// " PNBRQK"[type]
  static func pieceLetter(_ type: Int) -> String {
    switch type {
    case pawn: return "P"
    case knight: return "N"
    case bishop: return "B"
    case rook: return "R"
    case queen: return "Q"
    case king: return "K"
    default: return " "
    }
  }
}

// ---------------------------------------------------------------------------
// Move
// ---------------------------------------------------------------------------
struct Move: Equatable {
  var from: UInt8 = 0, to: UInt8 = 0, promo: UInt8 = 0, flags: UInt8 = 0
  var piece: UInt8 = 0, captured: UInt8 = 0  // filled at generation

  static let fCapture: UInt8 = 1, fEP: UInt8 = 2, fCastle: UInt8 = 4, fDouble: UInt8 = 8, fPromo: UInt8 = 16

  @inline(__always) static func == (a: Move, b: Move) -> Bool {
    a.from == b.from && a.to == b.to && a.promo == b.promo
  }
  var isNull: Bool { from == 0 && to == 0 }
  var isCapture: Bool { flags & Move.fCapture != 0 }
  var isCastle: Bool { flags & Move.fCastle != 0 }
  var isPromo: Bool { flags & Move.fPromo != 0 }
  // Square of the piece that gets captured (differs from 'to' for en passant).
  var captureSquare: Int { flags & Move.fEP != 0 ? Int(to ^ 8) : Int(to) }

  var uci: String {
    var s = Chess.sqName(Int(from)) + Chess.sqName(Int(to))
    if promo != 0 && promo < 6 { s += Chess.pieceLetter(Int(promo)).lowercased() }
    return s
  }
}

// ---------------------------------------------------------------------------
// Precomputed attack tables and Zobrist keys
// ---------------------------------------------------------------------------
/// All tables live in one allocation that is built once and never freed. Hot
/// code receives this one-pointer struct as a parameter instead of reading a
/// global, since globals pay a lazy-initialisation check on every access.
struct ChessTables {
  let base: UnsafeMutableRawPointer

  // Byte offsets: zPiece UInt64[16][64], zCastle UInt64[16], zEp UInt64[8],
  // zSide UInt64, knightT UInt8[64][8], knightN UInt8[64], kingT UInt8[64][8],
  // kingN UInt8[64], ray UInt8[64][8][8], rayN UInt8[64][8].
  private static let size = 14152

  @inline(__always) func zPiece(_ p: Int, _ s: Int) -> UInt64 {
    base.load(fromByteOffset: (p &<< 9) | (s &<< 3), as: UInt64.self)
  }
  @inline(__always) func zCastle(_ c: Int) -> UInt64 { base.load(fromByteOffset: 8192 &+ (c &<< 3), as: UInt64.self) }
  @inline(__always) func zEp(_ f: Int) -> UInt64 { base.load(fromByteOffset: 8320 &+ (f &<< 3), as: UInt64.self) }
  @inline(__always) var zSide: UInt64 { base.load(fromByteOffset: 8384, as: UInt64.self) }
  @inline(__always) func knightT(_ s: Int, _ i: Int) -> Int {
    Int(base.load(fromByteOffset: 8392 &+ (s &<< 3) &+ i, as: UInt8.self))
  }
  @inline(__always) func knightN(_ s: Int) -> Int { Int(base.load(fromByteOffset: 8904 &+ s, as: UInt8.self)) }
  @inline(__always) func kingT(_ s: Int, _ i: Int) -> Int {
    Int(base.load(fromByteOffset: 8968 &+ (s &<< 3) &+ i, as: UInt8.self))
  }
  @inline(__always) func kingN(_ s: Int) -> Int { Int(base.load(fromByteOffset: 9480 &+ s, as: UInt8.self)) }
  @inline(__always) func ray(_ s: Int, _ d: Int, _ i: Int) -> Int {
    Int(base.load(fromByteOffset: 9544 &+ (s &<< 6) &+ (d &<< 3) &+ i, as: UInt8.self))
  }
  @inline(__always) func rayN(_ s: Int, _ d: Int) -> Int {
    Int(base.load(fromByteOffset: 13640 &+ (s &<< 3) &+ d, as: UInt8.self))
  }

  static let shared = ChessTables()

  private init() {
    base = UnsafeMutableRawPointer.allocate(byteCount: ChessTables.size, alignment: 16)
    base.initializeMemory(as: UInt8.self, repeating: 0, count: ChessTables.size)
    let kdf = [1, 2, 2, 1, -1, -2, -2, -1]
    let kdr = [2, 1, -1, -2, -2, -1, 1, 2]
    let gdf = [0, 0, 1, -1, 1, -1, 1, -1]
    let gdr = [1, -1, 0, 0, 1, 1, -1, -1]
    func put(_ off: Int, _ v: Int) { base.storeBytes(of: UInt8(v), toByteOffset: off, as: UInt8.self) }
    for s in 0..<64 {
      let f = s & 7, r = s >> 3
      var kn = 0, kg = 0
      for i in 0..<8 {
        var nf = f + kdf[i], nr = r + kdr[i]
        if nf >= 0 && nf < 8 && nr >= 0 && nr < 8 { put(8392 + s * 8 + kn, nr * 8 + nf); kn += 1 }
        nf = f + gdf[i]
        nr = r + gdr[i]
        if nf >= 0 && nf < 8 && nr >= 0 && nr < 8 { put(8968 + s * 8 + kg, nr * 8 + nf); kg += 1 }
      }
      put(8904 + s, kn)
      put(9480 + s, kg)
      // directions: 0 N, 1 S, 2 E, 3 W (orthogonal) / 4 NE, 5 NW, 6 SE, 7 SW (diagonal)
      for d in 0..<8 {
        var n = 0, nf = f, nr = r
        while true {
          nf += gdf[d]
          nr += gdr[d]
          if nf < 0 || nf > 7 || nr < 0 || nr > 7 { break }
          put(9544 + s * 64 + d * 8 + n, nr * 8 + nf)
          n += 1
        }
        put(13640 + s * 8 + d, n)
      }
    }
    // Zobrist keys: the same SplitMix64 stream as the C++ (Math.swift's Rng),
    // drawn in the C++ order zPiece, zSide, zCastle, zEp.
    var rng = Rng(seed: 0x9E37_79B9_7F4A_7C15)
    func key(_ i: Int) { base.storeBytes(of: rng.next(), toByteOffset: i * 8, as: UInt64.self) }
    for i in 0..<1024 { key(i) }
    key(1048)
    for i in 0..<16 { key(1024 + i) }
    for i in 0..<8 { key(1040 + i) }
  }
}

// ---------------------------------------------------------------------------
// Board
// ---------------------------------------------------------------------------
struct Board {
  var sq = SIMD64<UInt8>(repeating: 0)  // fixed-size value storage, no heap allocation
  var side = Chess.white
  var castling = 0
  var ep = -1
  var halfmove = 0
  var fullmove = 1
  var kingSq = SIMD2<Int>(4, 60)  // indexed by color
  var hash: UInt64 = 0

  init() {}

  /// Runs body with the 64 squares as bytes.
  @inline(__always) func withSquares<R>(_ body: (UnsafePointer<UInt8>) -> R) -> R {
    withUnsafeBytes(of: sq) { body($0.baseAddress!.assumingMemoryBound(to: UInt8.self)) }
  }

  mutating func setStart() { _ = setFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") }

  private static func charToPiece(_ c: UInt8) -> Int {
    let color = (c >= 97 && c <= 122) ? Chess.black : Chess.white
    switch c | 32 {
    case UInt8(ascii: "p"): return Chess.makePiece(Chess.pawn, color)
    case UInt8(ascii: "n"): return Chess.makePiece(Chess.knight, color)
    case UInt8(ascii: "b"): return Chess.makePiece(Chess.bishop, color)
    case UInt8(ascii: "r"): return Chess.makePiece(Chess.rook, color)
    case UInt8(ascii: "q"): return Chess.makePiece(Chess.queen, color)
    case UInt8(ascii: "k"): return Chess.makePiece(Chess.king, color)
    default: return 0
    }
  }

  private static func pieceToChar(_ p: Int) -> String {
    let c = Chess.pieceLetter(Chess.pieceType(p))
    return Chess.pieceColor(p) == Chess.black ? c.lowercased() : c
  }

  /// Leading integer of a token, like `istream >> int` (nil if there is none).
  private static func leadingInt(_ s: Substring) -> Int? {
    var v = 0, neg = false, any = false
    for (i, c) in s.utf8.enumerated() {
      if i == 0 && (c == UInt8(ascii: "-") || c == UInt8(ascii: "+")) {
        neg = c == UInt8(ascii: "-")
        continue
      }
      guard c >= 48 && c <= 57 else { break }
      v = v &* 10 &+ Int(c - 48)
      any = true
    }
    return any ? (neg ? -v : v) : nil
  }

  @discardableResult mutating func setFEN(_ fen: String) -> Bool {
    let fields = fen.split(whereSeparator: { $0 == " " || $0 == "\t" || $0 == "\n" || $0 == "\r" })
    let placement = fields.count > 0 ? fields[0] : ""
    let stm = fields.count > 1 ? fields[1] : "w"
    let cast = fields.count > 2 ? fields[2] : "-"
    let eps = fields.count > 3 ? fields[3] : "-"
    var hm = 0, fm = 1
    if fields.count > 4 {
      // a failed extraction stores 0 and stops the stream
      if let v = Board.leadingInt(fields[4]) {
        hm = v
        if fields.count > 5 { fm = Board.leadingInt(fields[5]) ?? 0 }
      }
    }
    sq = SIMD64<UInt8>(repeating: 0)
    var r = 7, f = 0
    var kings = (-1, -1)
    for c in placement.utf8 {
      if c == UInt8(ascii: "/") {
        r -= 1
        f = 0
      } else if c >= UInt8(ascii: "1") && c <= UInt8(ascii: "8") {
        f += Int(c) - 48
      } else {
        let p = Board.charToPiece(c)
        if p == 0 || r < 0 || f > 7 { return false }
        sq[r * 8 + f] = UInt8(p)
        if Chess.pieceType(p) == Chess.king {
          if Chess.pieceColor(p) == 0 { kings.0 = r * 8 + f } else { kings.1 = r * 8 + f }
        }
        f += 1
      }
    }
    if kings.0 < 0 || kings.1 < 0 { return false }
    kingSq = SIMD2(kings.0, kings.1)
    side = stm == "b" ? Chess.black : Chess.white
    castling = 0
    for c in cast {
      if c == "K" { castling |= Chess.castleWK }
      if c == "Q" { castling |= Chess.castleWQ }
      if c == "k" { castling |= Chess.castleBK }
      if c == "q" { castling |= Chess.castleBQ }
    }
    ep = -1
    let e = Array(eps.utf8)
    if e.count == 2 { ep = Chess.makeSq(Int(e[0]) - 97, Int(e[1]) - 49) }
    halfmove = hm
    fullmove = fm
    hash = computeHash()
    return true
  }

  func fen() -> String {
    var s = ""
    for r in stride(from: 7, through: 0, by: -1) {
      var empty = 0
      for f in 0..<8 {
        let p = Int(sq[r * 8 + f])
        if p == 0 {
          empty += 1
          continue
        }
        if empty != 0 {
          s += String(empty)
          empty = 0
        }
        s += Board.pieceToChar(p)
      }
      if empty != 0 { s += String(empty) }
      if r != 0 { s += "/" }
    }
    s += side == Chess.white ? " w " : " b "
    var c = ""
    if castling & Chess.castleWK != 0 { c += "K" }
    if castling & Chess.castleWQ != 0 { c += "Q" }
    if castling & Chess.castleBK != 0 { c += "k" }
    if castling & Chess.castleBQ != 0 { c += "q" }
    s += c.isEmpty ? "-" : c
    s += " "
    s += ep >= 0 ? Chess.sqName(ep) : "-"
    s += " \(halfmove) \(fullmove)"
    return s
  }

  func computeHash() -> UInt64 { computeHash(ChessTables.shared) }

  func computeHash(_ T: ChessTables) -> UInt64 {
    var h: UInt64 = withSquares { b in
      var h: UInt64 = 0
      for s in 0..<64 where b[s] != 0 { h ^= T.zPiece(Int(b[s]), s) }
      return h
    }
    if side == Chess.black { h ^= T.zSide }
    h ^= T.zCastle(castling)
    if ep >= 0 { h ^= T.zEp(ep & 7) }
    return h
  }

  // ---- attacks ---------------------------------------------------------------
  @inline(__always)
  static func attacked(_ b: UnsafePointer<UInt8>, _ s: Int, _ by: Int, _ T: ChessTables) -> Bool {
    // pawns
    let pawn = UInt8(truncatingIfNeeded: 1 | (by << 3))
    let f = s & 7
    if by == Chess.white {
      if s >= 8 {
        if f > 0 && b[s - 9] == pawn { return true }
        if f < 7 && b[s - 7] == pawn { return true }
      }
    } else {
      if s < 56 {
        if f > 0 && b[s + 7] == pawn { return true }
        if f < 7 && b[s + 9] == pawn { return true }
      }
    }
    let knight = pawn &+ 1
    for i in 0..<T.knightN(s) where b[T.knightT(s, i)] == knight { return true }
    let king = pawn &+ 5
    for i in 0..<T.kingN(s) where b[T.kingT(s, i)] == king { return true }
    let bishop = pawn &+ 2, rook = pawn &+ 3, queen = pawn &+ 4
    for d in 0..<8 {
      for i in 0..<T.rayN(s, d) {
        let p = b[T.ray(s, d, i)]
        if p == 0 { continue }
        if p == queen { return true }
        if d < 4 ? p == rook : p == bishop { return true }
        break
      }
    }
    return false
  }

  func attacked(_ square: Int, _ byColor: Int) -> Bool { attacked(square, byColor, ChessTables.shared) }
  @inline(__always) func attacked(_ square: Int, _ byColor: Int, _ T: ChessTables) -> Bool {
    withSquares { Board.attacked($0, square, byColor, T) }
  }
  func inCheck() -> Bool { inCheck(ChessTables.shared) }
  func inCheck(_ color: Int) -> Bool { attacked(kingSq[color], color ^ 1, ChessTables.shared) }
  @inline(__always) func inCheck(_ T: ChessTables) -> Bool { attacked(kingSq[side], side ^ 1, T) }

  // ---- move generation -------------------------------------------------------
  @inline(__always)
  private static func add(_ out: UnsafeMutablePointer<Move>, _ n: inout Int, _ from: Int, _ to: Int, _ piece: Int,
                          _ captured: Int, _ flags: Int, _ promo: Int = 0) {
    out[n] = Move(from: UInt8(truncatingIfNeeded: from), to: UInt8(truncatingIfNeeded: to),
                  promo: UInt8(truncatingIfNeeded: promo), flags: UInt8(truncatingIfNeeded: flags),
                  piece: UInt8(truncatingIfNeeded: piece), captured: UInt8(truncatingIfNeeded: captured))
    n &+= 1
  }

  @inline(__always)
  private static func pushPawn(_ out: UnsafeMutablePointer<Move>, _ n: inout Int, _ s: Int, _ target: Int, _ p: Int,
                               _ cap: Int, _ flags: Int, _ promoRank: Int, _ capturesOnly: Bool) {
    if target >> 3 == promoRank {
      var pr = Chess.queen
      while pr >= Chess.knight {
        Board.add(out, &n, s, target, p, cap, flags | Int(Move.fPromo), pr)
        pr -= 1
      }
    } else if !capturesOnly || flags & Int(Move.fCapture) != 0 {
      Board.add(out, &n, s, target, p, cap, flags)
    }
  }

  /// Pseudo-legal moves into out (room for 256); returns the count.
  func genPseudo(_ out: UnsafeMutablePointer<Move>, _ capturesOnly: Bool = false) -> Int {
    genPseudo(out, capturesOnly, ChessTables.shared)
  }

  func genPseudo(_ out: UnsafeMutablePointer<Move>, _ capturesOnly: Bool, _ T: ChessTables) -> Int {
    let us = side, them = side ^ 1, ep = self.ep, castling = self.castling
    return withSquares { b -> Int in
      var n = 0
      for s in 0..<64 {
        let p = Int(b[s])
        if p == 0 || p >> 3 != us { continue }
        let t = p & 7
        if t == Chess.pawn {
          let dir = us == Chess.white ? 8 : -8
          let startRank = us == Chess.white ? 1 : 6
          let promoRank = us == Chess.white ? 7 : 0
          let to = s + dir
          let f = s & 7
          if to >= 0 && to < 64 && b[to] == 0 {
            Board.pushPawn(out, &n, s, to, p, 0, 0, promoRank, capturesOnly)
            if s >> 3 == startRank && b[to + dir] == 0 && !capturesOnly {
              Board.add(out, &n, s, to + dir, p, 0, Int(Move.fDouble))
            }
          }
          for df in stride(from: -1, through: 1, by: 2) {
            let nf = f + df
            if nf < 0 || nf > 7 { continue }
            let c = s + dir + df
            if c < 0 || c > 63 { continue }
            let q = Int(b[c])
            if q != 0 && q >> 3 == them {
              Board.pushPawn(out, &n, s, c, p, q, Int(Move.fCapture), promoRank, capturesOnly)
            } else if c == ep {
              Board.add(out, &n, s, c, p, Chess.makePiece(Chess.pawn, them), Int(Move.fCapture | Move.fEP))
            }
          }
        } else if t == Chess.knight || t == Chess.king {
          let cnt = t == Chess.knight ? T.knightN(s) : T.kingN(s)
          for i in 0..<cnt {
            let to = t == Chess.knight ? T.knightT(s, i) : T.kingT(s, i)
            let q = Int(b[to])
            if q == 0 {
              if !capturesOnly { Board.add(out, &n, s, to, p, 0, 0) }
            } else if q >> 3 == them {
              Board.add(out, &n, s, to, p, q, Int(Move.fCapture))
            }
          }
          if t == Chess.king && !capturesOnly {
            let wr = UInt8(Chess.makePiece(Chess.rook, Chess.white))
            let br = UInt8(Chess.makePiece(Chess.rook, Chess.black))
            if us == Chess.white && s == 4 {
              if castling & Chess.castleWK != 0 && b[5] == 0 && b[6] == 0 && b[7] == wr
                && !Board.attacked(b, 4, them, T) && !Board.attacked(b, 5, them, T)
                && !Board.attacked(b, 6, them, T) {
                Board.add(out, &n, 4, 6, p, 0, Int(Move.fCastle))
              }
              if castling & Chess.castleWQ != 0 && b[3] == 0 && b[2] == 0 && b[1] == 0 && b[0] == wr
                && !Board.attacked(b, 4, them, T) && !Board.attacked(b, 3, them, T)
                && !Board.attacked(b, 2, them, T) {
                Board.add(out, &n, 4, 2, p, 0, Int(Move.fCastle))
              }
            } else if us == Chess.black && s == 60 {
              if castling & Chess.castleBK != 0 && b[61] == 0 && b[62] == 0 && b[63] == br
                && !Board.attacked(b, 60, them, T) && !Board.attacked(b, 61, them, T)
                && !Board.attacked(b, 62, them, T) {
                Board.add(out, &n, 60, 62, p, 0, Int(Move.fCastle))
              }
              if castling & Chess.castleBQ != 0 && b[59] == 0 && b[58] == 0 && b[57] == 0 && b[56] == br
                && !Board.attacked(b, 60, them, T) && !Board.attacked(b, 59, them, T)
                && !Board.attacked(b, 58, them, T) {
                Board.add(out, &n, 60, 58, p, 0, Int(Move.fCastle))
              }
            }
          }
        } else {
          let d0 = t == Chess.bishop ? 4 : 0
          let d1 = t == Chess.rook ? 4 : 8
          for d in d0..<d1 {
            for i in 0..<T.rayN(s, d) {
              let to = T.ray(s, d, i)
              let q = Int(b[to])
              if q == 0 {
                if !capturesOnly { Board.add(out, &n, s, to, p, 0, 0) }
                continue
              }
              if q >> 3 == them { Board.add(out, &n, s, to, p, q, Int(Move.fCapture)) }
              break
            }
          }
        }
      }
      return n
    }
  }

  /// Legal moves into out (room for 256); returns the count.
  func genLegal(_ out: UnsafeMutablePointer<Move>) -> Int { genLegal(out, ChessTables.shared) }

  func genLegal(_ out: UnsafeMutablePointer<Move>, _ T: ChessTables) -> Int {
    let n = genPseudo(out, false, T)
    var k = 0
    for i in 0..<n {
      var c = self
      if c.make(out[i], T) {
        out[k] = out[i]
        k += 1
      }
    }
    return k
  }

  /// Legal moves as an array (convenience for the UI; allocates).
  func genLegal() -> [Move] {
    withUnsafeTemporaryAllocation(of: Move.self, capacity: 256) { buf in
      let n = genLegal(buf.baseAddress!, ChessTables.shared)
      return Array(buf[0..<n])
    }
  }

  // ---- make ------------------------------------------------------------------
  // Applies a pseudo-legal move; returns false if it leaves own king in check
  // (board is then in an undefined state - use on a copy).
  mutating func make(_ m: Move) -> Bool { make(m, ChessTables.shared) }

  @inline(__always)
  mutating func make(_ m: Move, _ T: ChessTables) -> Bool {
    let us = side, them = side ^ 1
    let from = Int(m.from), to = Int(m.to), flags = Int(m.flags)
    // remove old castle/ep from hash
    var h = hash ^ T.zCastle(castling)
    if ep >= 0 { h ^= T.zEp(ep & 7) }
    var cr = castling
    var ksq = kingSq[us]
    var pt = 0
    let safe: Bool = withUnsafeMutableBytes(of: &sq) { raw -> Bool in
      let b = raw.baseAddress!.assumingMemoryBound(to: UInt8.self)
      let p = Int(b[from])
      pt = p & 7
      // capture
      if flags & Int(Move.fCapture) != 0 {
        let cs = flags & Int(Move.fEP) != 0 ? to ^ 8 : to
        h ^= T.zPiece(Int(b[cs]), cs)
        b[cs] = 0
      }
      // move piece
      h ^= T.zPiece(p, from)
      b[from] = 0
      let placed = flags & Int(Move.fPromo) != 0 ? Int(m.promo) | (us << 3) : p
      b[to] = UInt8(truncatingIfNeeded: placed)
      h ^= T.zPiece(placed, to)

      if pt == Chess.king {
        ksq = to
        cr &= us == Chess.white ? ~(Chess.castleWK | Chess.castleWQ) : ~(Chess.castleBK | Chess.castleBQ)
        if flags & Int(Move.fCastle) != 0 {
          let rf: Int, rt: Int
          if to > from {
            rf = from + 3
            rt = from + 1
          } else {
            rf = from - 4
            rt = from - 1
          }
          let rook = Int(b[rf])
          h ^= T.zPiece(rook, rf) ^ T.zPiece(rook, rt)
          b[rt] = UInt8(truncatingIfNeeded: rook)
          b[rf] = 0
        }
      }
      return !Board.attacked(UnsafePointer(b), ksq, them, T)
    }
    // castling rights from rook squares
    if from == 0 || to == 0 { cr &= ~Chess.castleWQ }
    if from == 7 || to == 7 { cr &= ~Chess.castleWK }
    if from == 56 || to == 56 { cr &= ~Chess.castleBQ }
    if from == 63 || to == 63 { cr &= ~Chess.castleBK }

    kingSq[us] = ksq
    castling = cr
    ep = flags & Int(Move.fDouble) != 0 ? (from + to) / 2 : -1
    halfmove = (pt == Chess.pawn || flags & Int(Move.fCapture) != 0) ? 0 : halfmove + 1
    if us == Chess.black { fullmove += 1 }
    side = them
    h ^= T.zSide
    h ^= T.zCastle(cr)
    if ep >= 0 { h ^= T.zEp(ep & 7) }
    hash = h
    return safe
  }

  mutating func makeNull() { makeNull(ChessTables.shared) }

  @inline(__always) mutating func makeNull(_ T: ChessTables) {
    if ep >= 0 { hash ^= T.zEp(ep & 7) }
    ep = -1
    side ^= 1
    hash ^= T.zSide
    halfmove += 1
  }

  // ---- material --------------------------------------------------------------
  func insufficientMaterial() -> Bool {
    withSquares { b in
      var minors = 0
      for s in 0..<64 {
        let p = Int(b[s])
        if p == 0 { continue }
        let t = p & 7
        if t == Chess.pawn || t == Chess.rook || t == Chess.queen { return false }
        if t == Chess.knight || t == Chess.bishop { minors += 1 }
      }
      return minors <= 1
    }
  }

  func nonPawnMaterial(_ color: Int) -> Int {
    withSquares { b in
      var t = 0
      for s in 0..<64 {
        let p = Int(b[s])
        if p == 0 || p >> 3 != color { continue }
        switch p & 7 {
        case Chess.knight, Chess.bishop: t += 3
        case Chess.rook: t += 5
        case Chess.queen: t += 9
        default: break
        }
      }
      return t
    }
  }
}

// ---------------------------------------------------------------------------
// SAN, move lookup, perft
// ---------------------------------------------------------------------------
extension Chess {
  /// SAN for a legal move in the given position (includes +/#).
  static func toSAN(_ b: Board, _ m: Move) -> String {
    var s = ""
    let t = pieceType(Int(m.piece))
    if m.flags & Move.fCastle != 0 {
      s = m.to > m.from ? "O-O" : "O-O-O"
    } else {
      if t != pawn {
        s += pieceLetter(t)
        var clash = false, sameFile = false, sameRank = false
        for o in b.genLegal() where o.to == m.to && o.from != m.from && pieceType(Int(o.piece)) == t {
          clash = true
          if sqFile(Int(o.from)) == sqFile(Int(m.from)) { sameFile = true }
          if sqRank(Int(o.from)) == sqRank(Int(m.from)) { sameRank = true }
        }
        if clash {
          if !sameFile {
            s += String(UnicodeScalar(UInt8(97 + sqFile(Int(m.from)))))
          } else if !sameRank {
            s += String(UnicodeScalar(UInt8(49 + sqRank(Int(m.from)))))
          } else {
            s += sqName(Int(m.from))
          }
        }
        if m.flags & Move.fCapture != 0 { s += "x" }
      } else if m.flags & Move.fCapture != 0 {
        s += String(UnicodeScalar(UInt8(97 + sqFile(Int(m.from)))))
        s += "x"
      }
      s += sqName(Int(m.to))
      if m.flags & Move.fPromo != 0 {
        s += "="
        s += pieceLetter(Int(m.promo))
      }
    }
    var c = b
    _ = c.make(m)
    if c.inCheck() { s += c.genLegal().isEmpty ? "#" : "+" }
    return s
  }

  /// Finds the legal move matching from/to/promo (promo 0 = any -> queen).
  static func findLegal(_ b: Board, _ from: Int, _ to: Int, _ promo: Int) -> Move? {
    for m in b.genLegal() where Int(m.from) == from && Int(m.to) == to {
      if m.flags & Move.fPromo != 0 && Int(m.promo) != (promo != 0 ? promo : queen) { continue }
      return m
    }
    return nil
  }

  static func perft(_ b: Board, _ depth: Int) -> UInt64 {
    let T = ChessTables.shared
    let n = max(depth, 1)
    let arena = UnsafeMutablePointer<Move>.allocate(capacity: n * 256)
    defer { arena.deallocate() }
    return perft(b, depth, arena, T)
  }

  /// Recursive perft; ml holds 256 moves per remaining ply.
  private static func perft(_ b: Board, _ depth: Int, _ ml: UnsafeMutablePointer<Move>, _ T: ChessTables) -> UInt64 {
    let n = b.genPseudo(ml, false, T)
    var count: UInt64 = 0
    for i in 0..<n {
      var c = b
      if !c.make(ml[i], T) { continue }
      count &+= depth <= 1 ? 1 : perft(c, depth - 1, ml + 256, T)
    }
    return count
  }
}

// ---------------------------------------------------------------------------
// Game record
// ---------------------------------------------------------------------------
enum GameResult {
  case ongoing, whiteMates, blackMates, stalemate, fiftyMove, repetition, material

  var text: String {
    switch self {
    case .whiteMates: return "Checkmate - White wins"
    case .blackMates: return "Checkmate - Black wins"
    case .stalemate: return "Stalemate - draw"
    case .fiftyMove: return "Fifty-move rule - draw"
    case .repetition: return "Threefold repetition - draw"
    case .material: return "Insufficient material - draw"
    case .ongoing: return ""
    }
  }
}

/// Full game record with history for undo/repetition.
final class Game {
  private(set) var board = Board()
  private var boards: [Board] = []
  private(set) var moves: [Move] = []
  private(set) var san: [String] = []
  private(set) var hashHistory: [UInt64] = []
  private(set) var result = GameResult.ongoing

  init() { reset() }

  var plyCount: Int { moves.count }

  func reset() {
    board.setStart()
    boards.removeAll()
    moves.removeAll()
    san.removeAll()
    hashHistory = [board.hash]
    result = .ongoing
  }

  @discardableResult func setFEN(_ fen: String) -> Bool {
    var b = Board()
    if !b.setFEN(fen) { return false }
    reset()
    board = b
    hashHistory = [board.hash]
    updateResult()
    return true
  }

  /// Plays m, which must be legal.
  @discardableResult func play(_ m: Move) -> Bool {
    var next = board
    if !next.make(m) { return false }
    san.append(Chess.toSAN(board, m))
    boards.append(board)
    moves.append(m)
    board = next
    hashHistory.append(board.hash)
    updateResult()
    return true
  }

  @discardableResult func undo() -> Bool {
    guard let prev = boards.popLast() else { return false }
    board = prev
    moves.removeLast()
    san.removeLast()
    hashHistory.removeLast()
    result = .ongoing
    updateResult()
    return true
  }

  private func updateResult() {
    if board.genLegal().isEmpty {
      if board.inCheck() {
        result = board.side == Chess.white ? .blackMates : .whiteMates
      } else {
        result = .stalemate
      }
      return
    }
    if board.halfmove >= 100 {
      result = .fiftyMove
      return
    }
    var reps = 0
    for h in hashHistory where h == board.hash { reps += 1 }
    if reps >= 3 {
      result = .repetition
      return
    }
    if board.insufficientMaterial() {
      result = .material
      return
    }
    result = .ongoing
  }
}
