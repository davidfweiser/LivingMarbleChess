// Mesh data produced by the procedural sculpting and consumed by the renderer.
import Foundation
import simd

/// Signed distance field: negative inside.
typealias SDFFn = (vec3) -> Float

/// Stride 48 bytes: pos @0, nrm @16, ao @32, aux @36 (matches the Metal vertex descriptor).
struct Vertex {
  var pos: vec3
  var nrm: vec3
  var ao: Float   // baked ambient occlusion (1 = open)
  var aux: Float  // fragment: 1 on fresh fracture faces; sword: material id
  init(pos: vec3, nrm: vec3, ao: Float = 1, aux: Float = 0) {
    self.pos = pos
    self.nrm = nrm
    self.ao = ao
    self.aux = aux
  }
}

struct MeshData {
  var v: [Vertex] = []
  var idx: [UInt32] = []
  var bmin = vec3(0), bmax = vec3(0)

  mutating func computeBounds() {
    guard let first = v.first else {
      bmin = vec3(0)
      bmax = vec3(0)
      return
    }
    var lo = first.pos, hi = first.pos
    for x in v {
      lo = vmin(lo, x.pos)
      hi = vmax(hi, x.pos)
    }
    bmin = lo
    bmax = hi
  }

  mutating func translate(_ d: vec3) {
    for i in v.indices { v[i].pos += d }
    bmin += d
    bmax += d
  }
}

struct Fragment {
  var mesh = MeshData()     // vertices relative to 'centroid'
  var centroid = vec3(0)    // in piece-local space
  var volume: Float = 0
  var radius: Float = 0     // bounding radius around centroid
  var hull: [vec3] = []     // contact points (relative to centroid)
}

struct PieceAssets {
  var lod0 = MeshData(), lod1 = MeshData()
  var shards: [Fragment] = []  // Voronoi fracture into chunks
  var halves: [Fragment] = []  // two halves cut by a diagonal plane
  var sliceN = vec3(0.8, 0.6, 0)
  var slicePoint = vec3(0)
  var height: Float = 1, radius: Float = 0.37
}

final class Library: @unchecked Sendable {  // filled on a loader thread, read after it finishes
  var piece = [PieceAssets](repeating: PieceAssets(), count: Kind.count)
  var sword = MeshData()       // aux: 0 steel, 1 gold, 2 leather
  var cannonball = MeshData()
  var board = MeshData()       // closed slab, y = 0 is the playing surface
  var boardHalf: Float = 4.62, boardThickness: Float = 0.32
}

/// Loading progress, written by the builder thread and read by the UI.
final class BuildProgress: @unchecked Sendable {
  private let lock = NSLock()
  private var done_ = 0, total_ = 1, finished_ = false, stage_ = ""

  var done: Int {
    get { lock.withLock { done_ } }
    set { lock.withLock { done_ = newValue } }
  }
  var total: Int {
    get { lock.withLock { total_ } }
    set { lock.withLock { total_ = newValue } }
  }
  var finished: Bool {
    get { lock.withLock { finished_ } }
    set { lock.withLock { finished_ = newValue } }
  }
  var stage: String {
    get { lock.withLock { stage_ } }
    set { lock.withLock { stage_ = newValue } }
  }
  func step() { lock.withLock { done_ += 1 } }
}
