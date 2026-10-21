// Alpha-beta chess engine running on a background thread.
import Foundation
import os

struct AILevel {
  let name: String
  let maxDepth: Int
  let timeLimit: Double  // seconds
  let evalNoise: Int     // centipawns of deterministic noise (weaker play)
}

let kAILevels: [AILevel] = [
  AILevel(name: "Squire", maxDepth: 2, timeLimit: 0.4, evalNoise: 90),
  AILevel(name: "Knight", maxDepth: 3, timeLimit: 0.8, evalNoise: 30),
  AILevel(name: "Bishop", maxDepth: 5, timeLimit: 1.2, evalNoise: 8),
  AILevel(name: "King", maxDepth: 64, timeLimit: 2.5, evalNoise: 0),
  AILevel(name: "Grandmaster", maxDepth: 64, timeLimit: 6.0, evalNoise: 0),
]

// ---------------------------------------------------------------------------
// Evaluation tables
// ---------------------------------------------------------------------------
/// Piece values and piece-square tables, expanded per piece code (type | color << 3)
/// with the colour mirroring applied, in one never-freed allocation. Like
/// ChessTables, hot code receives this one-pointer struct instead of reading a global.
struct EvalTables {
  // Int32 offsets: mg [16][64] = VAL + PST, eg [16][64] = VAL + endgame PST,
  // val [8] = VAL, passed [8] = PASSED.
  let base: UnsafeMutablePointer<Int32>

  @inline(__always) func mg(_ p: Int, _ s: Int) -> Int { Int(base[(p &<< 6) | s]) }
  @inline(__always) func eg(_ p: Int, _ s: Int) -> Int { Int(base[1024 | (p &<< 6) | s]) }
  @inline(__always) func val(_ t: Int) -> Int { Int(base[2048 | t]) }
  @inline(__always) func passed(_ adv: Int) -> Int { Int(base[2056 | adv]) }

  static let shared = EvalTables()

  private init() {
    // Piece-square tables, written with rank 8 on the first row (white's view).
    let PST_P = [0, 0, 0, 0, 0, 0, 0, 0, 50, 50, 50, 50, 50, 50, 50, 50,
                 10, 10, 20, 30, 30, 20, 10, 10, 5, 5, 10, 25, 25, 10, 5, 5,
                 0, 0, 0, 22, 22, 0, 0, 0, 5, -5, -10, 0, 0, -10, -5, 5,
                 5, 10, 10, -20, -20, 10, 10, 5, 0, 0, 0, 0, 0, 0, 0, 0]
    let PST_N = [-50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0, 0, 0, 0, -20, -40,
                 -30, 0, 10, 15, 15, 10, 0, -30, -30, 5, 15, 20, 20, 15, 5, -30,
                 -30, 0, 15, 20, 20, 15, 0, -30, -30, 5, 10, 15, 15, 10, 5, -30,
                 -40, -20, 0, 5, 5, 0, -20, -40, -50, -40, -30, -30, -30, -30, -40, -50]
    let PST_B = [-20, -10, -10, -10, -10, -10, -10, -20, -10, 0, 0, 0, 0, 0, 0, -10,
                 -10, 0, 5, 10, 10, 5, 0, -10, -10, 5, 5, 10, 10, 5, 5, -10,
                 -10, 0, 10, 10, 10, 10, 0, -10, -10, 10, 10, 10, 10, 10, 10, -10,
                 -10, 5, 0, 0, 0, 0, 5, -10, -20, -10, -10, -10, -10, -10, -10, -20]
    let PST_R = [0, 0, 0, 0, 0, 0, 0, 0, 5, 10, 10, 10, 10, 10, 10, 5, -5, 0, 0, 0, 0, 0,
                 0, -5, -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0,
                 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5, 0, 0, 0, 5, 5, 0, 0, 0]
    let PST_Q = [-20, -10, -10, -5, -5, -10, -10, -20, -10, 0, 0, 0, 0, 0, 0, -10,
                 -10, 0, 5, 5, 5, 5, 0, -10, -5, 0, 5, 5, 5, 5, 0, -5,
                 0, 0, 5, 5, 5, 5, 0, -5, -10, 5, 5, 5, 5, 5, 0, -10,
                 -10, 0, 5, 0, 0, 0, 0, -10, -20, -10, -10, -5, -5, -10, -10, -20]
    let PST_K_MG = [-30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
                    -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
                    -20, -30, -30, -40, -40, -30, -30, -20, -10, -20, -20, -20, -20, -20, -20, -10,
                    20, 20, 0, 0, 0, 0, 20, 20, 20, 30, 10, 0, 0, 10, 30, 20]
    let PST_K_EG = [-50, -40, -30, -20, -20, -30, -40, -50, -30, -20, -10, 0, 0, -10, -20, -30,
                    -30, -10, 20, 30, 30, 20, -10, -30, -30, -10, 30, 40, 40, 30, -10, -30,
                    -30, -10, 30, 40, 40, 30, -10, -30, -30, -10, 20, 30, 30, 20, -10, -30,
                    -30, -30, 0, 0, 0, 0, -30, -30, -50, -30, -30, -30, -30, -30, -30, -50]
    let VAL = [0, 100, 320, 330, 500, 900, 0, 0]
    let PASSED = [0, 5, 10, 20, 35, 60, 100, 0]

    base = .allocate(capacity: 2064)
    base.initialize(repeating: 0, count: 2064)
    for c in 0..<2 {
      for t in 1...6 {
        let p = Chess.makePiece(t, c)
        for s in 0..<64 {
          let idx = c == Chess.white ? (s ^ 56) : s  // tableIndex()
          var mg = 0, eg = 0
          switch t {
          case Chess.pawn: mg = PST_P[idx]; eg = PST_P[idx] / 2
          case Chess.knight: mg = PST_N[idx]; eg = mg
          case Chess.bishop: mg = PST_B[idx]; eg = mg
          case Chess.rook: mg = PST_R[idx]; eg = mg
          case Chess.queen: mg = PST_Q[idx]; eg = mg
          default: mg = PST_K_MG[idx]; eg = PST_K_EG[idx]
          }
          base[p * 64 + s] = Int32(VAL[t] + mg)
          base[1024 + p * 64 + s] = Int32(VAL[t] + eg)
        }
      }
    }
    for i in 0..<8 {
      base[2048 + i] = Int32(VAL[i])
      base[2056 + i] = Int32(PASSED[i])
    }
  }
}

private let kInf = 32000
private let kMate = 30000

extension Chess {
  /// Static evaluation from the side-to-move perspective.
  static func evaluate(_ b: Board) -> Int { evaluate(b, EvalTables.shared) }

  static func evaluate(_ b: Board, _ E: EvalTables) -> Int {
    b.withSquares { sq in
      withUnsafeTemporaryAllocation(of: Int.self, capacity: 48) { buf in
        // per colour and file: pawn count, lowest and highest pawn rank ([2][8] each)
        let pawnsOnFile = buf.baseAddress!, minPawnRank = pawnsOnFile + 16, maxPawnRank = pawnsOnFile + 32
        for i in 0..<16 {
          pawnsOnFile[i] = 0
          minPawnRank[i] = 9
          maxPawnRank[i] = -1
        }
        for s in 0..<64 {
          let p = Int(sq[s])
          if p & 7 != Chess.pawn { continue }
          let i = (p >> 3) << 3 | (s & 7), r = s >> 3
          pawnsOnFile[i] += 1
          minPawnRank[i] = min(minPawnRank[i], r)
          maxPawnRank[i] = max(maxPawnRank[i], r)
        }
        // mg and eg accumulate white minus black directly.
        var mg = 0, eg = 0
        var phase = 0
        var bishopsW = 0, bishopsB = 0
        for s in 0..<64 {
          let p = Int(sq[s])
          if p == 0 { continue }
          let c = p >> 3, t = p & 7
          let f = s & 7, r = s >> 3
          let sign = 1 - 2 * c
          // material + piece-square value (incl. the pawn's halved endgame PST)
          mg += sign * E.mg(p, s)
          eg += sign * E.eg(p, s)
          switch t {
          case Chess.pawn:
            // passed pawn?
            var passed = true
            let them = (c ^ 1) << 3
            var df = -1
            while df <= 1 && passed {
              let nf = f + df
              df += 1
              if nf < 0 || nf > 7 { continue }
              if c == Chess.white ? (maxPawnRank[them | nf] > r)
                : (minPawnRank[them | nf] < r && minPawnRank[them | nf] < 9) {
                passed = false
              }
            }
            if passed {
              let adv = c == Chess.white ? r : 7 - r
              mg += sign * (E.passed(adv) / 2)
              eg += sign * E.passed(adv)
            }
            let own = c << 3
            let isolated = (f == 0 || pawnsOnFile[own | (f - 1)] == 0) && (f == 7 || pawnsOnFile[own | (f + 1)] == 0)
            if isolated {
              mg -= sign * 10
              eg -= sign * 12
            }
          case Chess.knight:
            phase += 1
          case Chess.bishop:
            if c == 0 { bishopsW += 1 } else { bishopsB += 1 }
            phase += 1
          case Chess.rook:
            if pawnsOnFile[c << 3 | f] == 0 {
              let bonus = pawnsOnFile[(c ^ 1) << 3 | f] != 0 ? 8 : 18
              mg += sign * bonus
              eg += sign * (bonus / 2)
            }
            phase += 2
          case Chess.queen:
            phase += 4
          case Chess.king:
            // pawn shield
            let dir = c == Chess.white ? 1 : -1
            var shield = 0
            let ownPawn = UInt8(Chess.makePiece(Chess.pawn, c))
            for df in -1...1 {
              let nf = f + df, nr = r + dir
              if nf < 0 || nf > 7 || nr < 0 || nr > 7 { continue }
              if sq[nr * 8 + nf] == ownPawn { shield += 1 }
            }
            mg += sign * shield * 12
          default:
            break
          }
        }
        if bishopsW >= 2 {
          mg += 30
          eg += 45
        }
        if bishopsB >= 2 {
          mg -= 30
          eg -= 45
        }
        for c in 0..<2 {
          let sign = 1 - 2 * c
          for f in 0..<8 where pawnsOnFile[c << 3 | f] > 1 {
            mg -= sign * 12 * (pawnsOnFile[c << 3 | f] - 1)
            eg -= sign * 18 * (pawnsOnFile[c << 3 | f] - 1)
          }
        }
        if phase > 24 { phase = 24 }
        var score = (mg * phase + eg * (24 - phase)) / 24
        score += b.side == Chess.white ? 10 : -10  // tempo
        return b.side == Chess.white ? score : -score
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------
/// One transposition-table slot (16 bytes, the C++ layout).
struct TTEntry {
  var key: UInt64 = 0
  var score: Int16 = 0
  var depth: Int8 = 0
  var flag: UInt8 = 0  // 0 empty, 1 exact, 2 lower, 3 upper
  var from: UInt8 = 0, to: UInt8 = 0, promo: UInt8 = 0, pad: UInt8 = 0
}

/// State shared between the UI thread and the search thread (guarded by a lock).
private struct EngineState {
  var running = false, done = false, stop = false
  var best = Move()
  var depth = 0, score = 0
  var nodes: UInt64 = 0
}

private let kMaxPly = 256      // per-ply buffers; deeper nodes return the static eval
private let kPlyMoves = 256    // move slots per ply

@inline(__always) private func nowNanos() -> UInt64 { clock_gettime_nsec_np(CLOCK_UPTIME_RAW) }

/// The search proper. It is a struct with mutating methods, used in place
/// through a pointer, so field accesses compile to plain loads and stores
/// (class properties would pay dynamic exclusivity checks). All buffers are
/// allocated once: no allocation happens per node.
private struct Searcher {
  let T = ChessTables.shared
  let E = EvalTables.shared
  let tt: UnsafeMutablePointer<TTEntry>
  let ttMask: UInt64
  let killers: UnsafeMutablePointer<Move>   // [128][2]
  let history: UnsafeMutablePointer<Int32>  // [16][64]
  let moves: UnsafeMutablePointer<Move>     // [kMaxPly][kPlyMoves] move lists
  let scores: UnsafeMutablePointer<Int32>   // [kMaxPly][kPlyMoves] ordering scores
  var path: UnsafeMutablePointer<UInt64>    // game history + search path hashes
  var pathCap = 0
  var pathLen = 0
  let shared: OSAllocatedUnfairLock<EngineState>
  var t0: UInt64 = 0
  var timeLimit = 1.0
  var noise = 0
  var nodes: UInt64 = 0
  var aborted = false
  var rootBest = Move()
  var rootScore = 0

  init(_ shared: OSAllocatedUnfairLock<EngineState>) {
    self.shared = shared
    let ttSize = 1 << 20
    tt = .allocate(capacity: ttSize)
    tt.initialize(repeating: TTEntry(), count: ttSize)
    ttMask = UInt64(ttSize - 1)
    killers = .allocate(capacity: 256)
    killers.initialize(repeating: Move(), count: 256)
    history = .allocate(capacity: 1024)
    history.initialize(repeating: 0, count: 1024)
    moves = .allocate(capacity: kMaxPly * kPlyMoves)
    moves.initialize(repeating: Move(), count: kMaxPly * kPlyMoves)
    scores = .allocate(capacity: kMaxPly * kPlyMoves)
    scores.initialize(repeating: 0, count: kMaxPly * kPlyMoves)
    pathCap = 1024
    path = .allocate(capacity: pathCap)
  }

  func free() {
    tt.deallocate()
    killers.deallocate()
    history.deallocate()
    moves.deallocate()
    scores.deallocate()
    path.deallocate()
  }

  @inline(__always) mutating func timeUp() -> Bool {
    if nodes & 2047 == 0 { checkTime() }
    return aborted
  }

  @inline(never) mutating func checkTime() {
    let el = Double(nowNanos() &- t0) * 1e-9
    if el > timeLimit || shared.withLock({ $0.stop }) { aborted = true }
  }

  @inline(__always) func evalNoisy(_ b: Board) -> Int {
    var e = Chess.evaluate(b, E)
    if noise != 0 {
      let h = b.hash &* 0x9E37_79B9_7F4A_7C15
      e += Int((h >> 40) % UInt64(2 * noise + 1)) - noise
    }
    return e
  }

  // path[n-1] is the current position; earlier positions with the same side to
  // move lie 2, 4, ... plies before it, back to the last capture or pawn move
  @inline(__always) func isRepetition(_ b: Board) -> Bool {
    let n = pathLen
    var i = 2
    while i <= b.halfmove && i < n {
      if path[n - 1 - i] == b.hash { return true }
      i += 2
    }
    return false
  }

  @inline(__always) func scoreMove(_ m: Move, _ ttMove: Move, _ ply: Int) -> Int32 {
    if m == ttMove { return 2_000_000 }
    if m.flags & Move.fPromo != 0 { return Int32(1_500_000 + E.val(Int(m.promo))) }
    if m.flags & Move.fCapture != 0 {
      return Int32(1_000_000 + E.val(Int(m.captured) & 7) * 10 - E.val(Int(m.piece) & 7) / 10)
    }
    if ply < 128 {
      if m == killers[ply * 2] { return 900_000 }
      if m == killers[ply * 2 + 1] { return 800_000 }
    }
    return history[Int(m.piece) << 6 | Int(m.to)]
  }

  /// Moves the best-scored remaining move to slot i (selection sort step).
  @inline(__always) func pickNext(_ ml: UnsafeMutablePointer<Move>, _ sc: UnsafeMutablePointer<Int32>, _ i: Int,
                                  _ n: Int) {
    var bi = i
    var j = i + 1
    while j < n {
      if sc[j] > sc[bi] { bi = j }
      j += 1
    }
    let m = ml[i]
    ml[i] = ml[bi]
    ml[bi] = m
    let s = sc[i]
    sc[i] = sc[bi]
    sc[bi] = s
  }

  mutating func qsearch(_ b: Board, _ alphaIn: Int, _ beta: Int, _ ply: Int) -> Int {
    nodes &+= 1
    if timeUp() { return 0 }
    var alpha = alphaIn
    let stand = evalNoisy(b)
    if stand >= beta { return stand }
    if stand > alpha { alpha = stand }
    if ply > 60 { return stand }
    let ml = moves + ply * kPlyMoves, sc = scores + ply * kPlyMoves
    let n = b.genPseudo(ml, true, T)
    for i in 0..<n { sc[i] = scoreMove(ml[i], Move(), 127) }
    for i in 0..<n {
      pickNext(ml, sc, i, n)
      let m = ml[i]
      // delta pruning
      if m.flags & Move.fPromo == 0 && stand + E.val(Int(m.captured) & 7) + 200 < alpha { continue }
      var c = b
      if !c.make(m, T) { continue }
      let s = -qsearch(c, -beta, -alpha, ply + 1)
      if aborted { return 0 }
      if s >= beta { return s }
      if s > alpha { alpha = s }
    }
    return alpha
  }

  mutating func search(_ b: Board, _ depthIn: Int, _ alphaIn: Int, _ betaIn: Int, _ ply: Int,
                       _ allowNull: Bool) -> Int {
    if ply >= kMaxPly { return evalNoisy(b) }  // out of per-ply buffers (never reached in practice)
    if timeUp() { return 0 }
    var depth = depthIn, alpha = alphaIn, beta = betaIn
    if ply > 0 {
      if b.halfmove >= 100 || isRepetition(b) { return 0 }
      // mate distance pruning
      alpha = max(alpha, -kMate + ply)
      beta = min(beta, kMate - ply - 1)
      if alpha >= beta { return alpha }
    }
    let inCheck = b.inCheck(T)
    if inCheck { depth += 1 }
    if depth <= 0 { return qsearch(b, alpha, beta, ply) }
    nodes &+= 1

    let e = tt + Int(truncatingIfNeeded: b.hash & ttMask)
    var ttMove = Move()
    if e.pointee.key == b.hash && e.pointee.flag != 0 {
      ttMove.from = e.pointee.from
      ttMove.to = e.pointee.to
      ttMove.promo = e.pointee.promo
      if ply > 0 && Int(e.pointee.depth) >= depth {
        var s = Int(e.pointee.score)
        if s > kMate - 200 { s -= ply }
        if s < -kMate + 200 { s += ply }
        if e.pointee.flag == 1 { return s }
        if e.pointee.flag == 2 && s >= beta { return s }
        if e.pointee.flag == 3 && s <= alpha { return s }
      }
    }

    let pvNode = beta - alpha > 1
    if !pvNode && !inCheck && depth <= 3 && ply > 0 {
      let st = evalNoisy(b)
      if st - 120 * depth >= beta { return st }  // reverse futility
    }
    if allowNull && !inCheck && !pvNode && depth >= 3 && ply > 0 && b.nonPawnMaterial(b.side) > 0 {
      if evalNoisy(b) >= beta {
        var c = b
        c.makeNull(T)
        path[pathLen] = c.hash
        pathLen += 1
        let R = depth >= 6 ? 3 : 2
        let sc = -search(c, depth - 1 - R, -beta, -beta + 1, ply + 1, false)
        pathLen -= 1
        if aborted { return 0 }
        if sc >= beta && sc < kMate - 200 { return beta }
      }
    }

    let ml = moves + ply * kPlyMoves, scs = scores + ply * kPlyMoves
    let n = b.genPseudo(ml, false, T)
    for i in 0..<n { scs[i] = scoreMove(ml[i], ttMove, ply) }

    var legal = 0
    var bestScore = -kInf
    var best = Move()
    let origAlpha = alpha
    for i in 0..<n {
      pickNext(ml, scs, i, n)
      let m = ml[i]
      var c = b
      if !c.make(m, T) { continue }
      legal += 1
      path[pathLen] = c.hash
      pathLen += 1
      let quiet = m.flags & (Move.fCapture | Move.fPromo) == 0
      var sc: Int
      if legal == 1 {
        sc = -search(c, depth - 1, -beta, -alpha, ply + 1, true)
      } else {
        // late move reductions + principal variation search
        var red = 0
        if depth >= 3 && legal > 3 && quiet && !inCheck && !c.inCheck(T) {
          red = 1 + (legal > 10 ? 1 : 0)
          if depth - 1 - red < 1 { red = depth - 2 }
        }
        sc = -search(c, depth - 1 - red, -alpha - 1, -alpha, ply + 1, true)
        if sc > alpha && red != 0 { sc = -search(c, depth - 1, -alpha - 1, -alpha, ply + 1, true) }
        if sc > alpha && sc < beta { sc = -search(c, depth - 1, -beta, -alpha, ply + 1, true) }
      }
      pathLen -= 1
      if aborted { return 0 }
      if sc > bestScore {
        bestScore = sc
        best = m
        if ply == 0 {
          rootBest = m
          rootScore = sc
        }
      }
      if sc > alpha { alpha = sc }
      if alpha >= beta {
        if quiet && ply < 128 {
          if !(killers[ply * 2] == m) {
            killers[ply * 2 + 1] = killers[ply * 2]
            killers[ply * 2] = m
          }
          let hi = Int(m.piece) << 6 | Int(m.to)
          history[hi] += Int32(depth * depth)
          if history[hi] > 700_000 {
            for k in 0..<1024 { history[k] /= 2 }
          }
        }
        break
      }
    }
    if legal == 0 { return inCheck ? -kMate + ply : 0 }

    var store = bestScore
    if store > kMate - 200 { store += ply }
    if store < -kMate + 200 { store -= ply }
    if e.pointee.key != b.hash || depth >= Int(e.pointee.depth) || bestScore >= beta {
      e.pointee.key = b.hash
      e.pointee.score = Int16(truncatingIfNeeded: store)
      e.pointee.depth = Int8(truncatingIfNeeded: min(depth, 120))
      e.pointee.flag = bestScore >= beta ? 2 : (bestScore > origAlpha ? 1 : 3)
      e.pointee.from = best.from
      e.pointee.to = best.to
      e.pointee.promo = best.promo
    }
    return bestScore
  }

  mutating func run(_ root: Board, _ hist: [UInt64], _ L: AILevel, _ outDepth: inout Int,
                    _ outScore: inout Int) -> Move {
    t0 = nowNanos()
    timeLimit = L.timeLimit
    noise = L.evalNoise
    nodes = 0
    aborted = false
    // path = hist (+ root), with room for one hash per search ply
    if pathCap < hist.count + kMaxPly + 2 {
      path.deallocate()
      pathCap = hist.count + kMaxPly + 2
      path = .allocate(capacity: pathCap)
    }
    pathLen = 0
    for h in hist {
      path[pathLen] = h
      pathLen += 1
    }
    if pathLen == 0 || path[pathLen - 1] != root.hash {
      path[pathLen] = root.hash
      pathLen += 1
    }
    killers.update(repeating: Move(), count: 256)
    history.update(repeating: 0, count: 1024)
    let legal = root.genLegal()
    if legal.isEmpty { return Move() }
    var best = legal[0]
    outDepth = 0
    outScore = 0
    if legal.count == 1 { return best }
    var d = 1
    while d <= L.maxDepth {
      rootBest = Move()
      let sc = search(root, d, -kInf, kInf, 0, false)
      if aborted {
        // partial iteration: accept the best move found so far if it beat previous
        if !rootBest.isNull && d > 1 {
          for m in legal where m == rootBest && rootScore > outScore - 50 { best = m }
        }
        break
      }
      for m in legal where m == rootBest { best = m }
      outDepth = d
      outScore = sc
      if sc > kMate - 100 || sc < -kMate + 100 { break }
      let el = Double(nowNanos() &- t0) * 1e-9
      if el > timeLimit * 0.55 { break }
      d += 1
    }
    return best
  }
}

// ---------------------------------------------------------------------------
// Engine front end
// ---------------------------------------------------------------------------
/// Closure box handed to the new pthread.
private final class ThreadBody {
  let run: () -> Void
  init(_ run: @escaping () -> Void) { self.run = run }
}

/// Starts a joinable thread with an 8 MB stack (the std::thread equivalent).
private func spawnThread(_ body: @escaping () -> Void) -> pthread_t? {
  var attr = pthread_attr_t()
  pthread_attr_init(&attr)
  defer { pthread_attr_destroy(&attr) }
  pthread_attr_setstacksize(&attr, 8 << 20)
  pthread_attr_set_qos_class_np(&attr, QOS_CLASS_USER_INITIATED, 0)
  let ctx = Unmanaged.passRetained(ThreadBody(body)).toOpaque()
  var tid: pthread_t?
  let rc = pthread_create(&tid, &attr, { ctx in
    Unmanaged<ThreadBody>.fromOpaque(ctx).takeRetainedValue().run()
    return nil
  }, ctx)
  if rc != 0 {
    Unmanaged<ThreadBody>.fromOpaque(ctx).release()
    return nil
  }
  return tid
}

final class AIEngine {
  private let state = OSAllocatedUnfairLock(initialState: EngineState())
  private let impl: UnsafeMutablePointer<Searcher>
  private var thread: pthread_t?

  init() {
    impl = .allocate(capacity: 1)
    impl.initialize(to: Searcher(state))
  }

  deinit {
    cancel()
    impl.pointee.free()
    impl.deinitialize(count: 1)
    impl.deallocate()
  }

  var thinking: Bool { state.withLock { $0.running } }
  var done: Bool { state.withLock { $0.done } }
  var result: Move { state.withLock { $0.best } }
  // stats of last search
  var lastDepth: Int { state.withLock { $0.depth } }
  var lastScore: Int { state.withLock { $0.score } }
  var lastNodes: UInt64 { state.withLock { $0.nodes } }

  /// Stops the search and waits for the search thread.
  func cancel() {
    state.withLock { $0.stop = true }
    if let t = thread {
      pthread_join(t, nil)
      thread = nil
    }
    state.withLock {
      $0.stop = false
      $0.running = false
    }
  }

  /// Starts searching on a background thread; poll done/thinking, then read result.
  func start(_ b: Board, _ history: [UInt64], _ level: Int) {
    cancel()
    state.withLock {
      $0.done = false
      $0.running = true
    }
    let impl = self.impl, state = self.state
    let L = kAILevels[max(0, min(4, level))]
    let body = {
      var d = 0, s = 0
      let m = impl.pointee.run(b, history, L, &d, &s)
      let (depth, score, n) = (d, s, impl.pointee.nodes)
      state.withLock {
        $0.best = m
        $0.depth = depth
        $0.score = score
        $0.nodes = n
        $0.done = true
        $0.running = false
      }
    }
    thread = spawnThread(body)
    if thread == nil { body() }  // could not create a thread: search inline
  }

  /// Synchronous search (used for tests / benchmarking).
  func searchSync(_ b: Board, _ history: [UInt64], _ level: Int) -> Move {
    searchSync(b, history, level: kAILevels[max(0, min(4, level))])
  }

  /// Synchronous search with a custom level (tests / benchmarking).
  func searchSync(_ b: Board, _ history: [UInt64], level L: AILevel) -> Move {
    cancel()
    var d = 0, s = 0
    let m = impl.pointee.run(b, history, L, &d, &s)
    let (depth, score, n) = (d, s, impl.pointee.nodes)
    state.withLock {
      $0.depth = depth
      $0.score = score
      $0.nodes = n
    }
    return m
  }
}
