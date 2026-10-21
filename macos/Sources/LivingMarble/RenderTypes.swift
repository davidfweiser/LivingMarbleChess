// Types shared between the scene (World / App) and the Metal renderer.
import Metal
import simd

final class GpuMesh {
  var vertexBuffer: MTLBuffer?
  var indexBuffer: MTLBuffer?
  var count = 0
  var isEmpty: Bool { vertexBuffer == nil || indexBuffer == nil || count == 0 }

  func upload(_ m: MeshData, device: MTLDevice) {
    vertexBuffer = nil
    indexBuffer = nil
    count = 0
    guard !m.v.isEmpty, !m.idx.isEmpty else { return }
    vertexBuffer = m.v.withUnsafeBytes { device.makeBuffer(bytes: $0.baseAddress!, length: $0.count, options: .storageModeShared) }
    indexBuffer = m.idx.withUnsafeBytes { device.makeBuffer(bytes: $0.baseAddress!, length: $0.count, options: .storageModeShared) }
    count = m.idx.count
  }
}

struct PieceGpu {
  var lod0 = GpuMesh(), lod1 = GpuMesh()
  var shards: [GpuMesh] = [], halves: [GpuMesh] = []
}

enum Material: Int32 {
  case white = 0, black = 1, sword = 2, iron = 3, medieval = 4, alien = 5, glass = 6
}

struct DrawItem {
  var mesh: GpuMesh?
  var meshLow: GpuMesh?
  var model = mat4.identity
  var rig = vec4(0, 0, 1, 0)  // bend, bendDir, squash, twist
  var rigH: Float = 1
  var material = Material.white
  var texOffset = vec3(0)
  var glow = vec4(0)
  var crack: Float = 0
  var crackColor = vec3(1.0, 0.6, 0.25)
  var dissolve: Float = 0
  var dissolveColor = vec3(1.0, 0.8, 0.4)
  var heat: Float = 0
  var stone: Float = 0  // medieval: turned to salt
  var castShadow = true
  var reflect = true
  var groundAO = true
  var glass = false       // see-through (drawn blended after everything solid)
  var ghost: Float = 0    // > 0: a translucent ghost (its opacity), drawn after everything solid
  var ghostId: Int32 = -1 // the items of one ghost share an id
  var bones: [mat4] = []  // skinned meshes (medieval and alien figures), at most Medieval.maxBones
  var team: Int32 = 0     // medieval / aliens: 0 white army, 1 black army
}

struct PointLight {
  var pos: vec3
  var radius: Float
  var color: vec3
}

/// 64 bytes per particle, matches the particle vertex shader layout.
struct ParticleGPU {
  var posSize = vec4(0)
  var color = vec4(0)    // rgb (premultiplied), alpha (0 = additive)
  var velType = vec4(0)  // velocity (for stretching), type
  var misc = vec4(0)     // rotation, stretch, seed, life fraction
}

struct RenderSettings {
  var shadowRes = 4096
  var msaa = 4
  var ssao = true
  var reflections = true
  var dof = true
  var bloom = true
  var shadows = true
  var renderScale: Float = 1

  static func preset(_ p: Int) -> RenderSettings {
    var s = RenderSettings()
    switch p {
    case 0:
      s.shadowRes = 1024
      s.msaa = 1
      s.ssao = false
      s.reflections = false
      s.dof = false
    case 1:
      s.shadowRes = 2048
      s.msaa = 2
      s.dof = false
    case 2:
      s.shadowRes = 4096
      s.msaa = 4
    default:
      s.shadowRes = 4096
      s.msaa = 8
    }
    return s
  }
}

struct SceneView {
  var view = mat4.identity, proj = mat4.identity
  var camPos = vec3(0)
  var nearZ: Float = 0.1, farZ: Float = 100
  var focusDist: Float = 12
  var aperture: Float = 0  // CoC scale in half-res pixels (0 disables DoF)
  var time: Float = 0
  var exposure: Float = 1
  var fade: Float = 1
  var flash = vec3(0)
  var items: [DrawItem] = []
  var lights: [PointLight] = []
  var particles: [ParticleGPU] = []
  var boardState = [UInt8](repeating: 0, count: 64 * 4)  // per square: r move target, g selected, b last move, a hover
  var check = vec4(0)
  var occluders: [vec4] = []         // x, z, radius, strength
  var rings: [vec4] = [], ringColors: [vec4] = []  // ground shockwaves
}
