// Physically based renderer: IBL, PCSS shadows, planar reflections, SSAO,
// MSAA HDR pipeline, depth of field, bloom and filmic tonemapping.
// Metal port of the OpenGL 3.3 renderer; the shader sources live in Shaders.swift.
import Foundation
import Metal
import simd

// ---------------------------------------------------------------------------
// Uniform blocks. Member order and types mirror the MSL structs in
// Shaders.swift (vec3 is 16 bytes in both languages, so layouts match).
// ---------------------------------------------------------------------------
extension Renderer {
  fileprivate struct PassUniforms {
    var viewProj = mat4.identity
    var view = mat4.identity
    var lightVP = mat4.identity
    var camPos = vec3(0)
    var lightDir = vec3(0)
    var lightColor = vec3(0)
    var invScreen = vec2(0)
    var shadowTexel: Float = 0
    var shadowWorld: Float = 0
    var shadowRange: Float = 0
    var prefilterMaxLod: Float = 0
    var ambient: Float = 0
    var time: Float = 0
    var boardHalf: Float = 0
    var shadowsOn: Int32 = 0
    var numLights: Int32 = 0
    var ssaoOn: Int32 = 0
    var clip: Int32 = 0
  }

  fileprivate struct DrawUniforms {
    var model = mat4.identity
    var rig = vec4(0, 0, 1, 0)  // bend, bendDir, squash, twist
    var glow = vec4(0)
    var texOffset = vec3(0)
    var crackColor = vec3(1.0, 0.6, 0.25)
    var dissolveColor = vec3(1.0, 0.8, 0.4)
    var rigH: Float = 1
    var dissolve: Float = 0
    var crack: Float = 0
    var heat: Float = 0
    var groundAO: Float = 1
    var mat: Int32 = 0

    init() {}
    init(_ it: DrawItem) {
      model = it.model
      rig = it.rig
      glow = it.glow
      texOffset = it.texOffset
      crackColor = it.crackColor
      dissolveColor = it.dissolveColor
      rigH = it.rigH
      dissolve = it.dissolve
      crack = it.crack
      heat = it.heat
      groundAO = it.groundAO ? 1 : 0
      mat = it.material.rawValue
    }
  }

  fileprivate struct BoardUniforms {
    var check = vec4(0)
    var numOcc: Int32 = 0
    var numRings: Int32 = 0
    var reflectOn: Int32 = 0
    var reflectLod: Float = 0
  }

  fileprivate struct FaceUniforms {
    var fwd = vec3(0), right = vec3(0), up = vec3(0)
    var keyDir = vec3(0)
    var includeKey: Int32 = 0
    var roughness: Float = 0
    var envRes: Float = 0
  }

  fileprivate struct BackgroundUniforms {
    var invViewProj: mat4
    var camPos: vec3
    var bgLod: Float
    var bgIntensity: Float
  }

  fileprivate struct SsaoUniforms {
    var proj: mat4
    var invProj: mat4
    var radius: Float
  }

  fileprivate struct SsaoBlurUniforms {
    var texel: vec2
    var nearFar: vec2
  }

  fileprivate struct BloomUniforms {
    var texel: vec2  // down: source texel size; up: tap radius
    var first: Int32
  }

  fileprivate struct DofCocUniforms {
    var nearFar: vec2
    var srcTexel: vec2
    var focus: Float
    var aperture: Float
    var maxCoc: Float
  }

  fileprivate struct DofBokehUniforms {
    var texel: vec2
    var maxCoc: Float
  }

  fileprivate struct CompositeUniforms {
    var flash: vec3
    var bloomStrength: Float
    var exposure: Float
    var vignette: Float
    var grain: Float
    var time: Float
    var fade: Float
    var bloomOn: Int32
  }

  fileprivate struct ParticleUniforms {
    var view: mat4
    var proj: mat4
    var invScreen: vec2
    var nearFar: vec2
  }

  fileprivate enum Blend { case none, premultiplied, additive }

  /// A mesh pass compiled without dissolve support (no discard: keeps the GPU's hidden
  /// surface removal) and with it, for items that are dissolving.
  fileprivate struct MeshPipes {
    let plain: MTLRenderPipelineState
    let dissolve: MTLRenderPipelineState
  }

  fileprivate struct RendererError: Error, CustomStringConvertible {
    let description: String
  }

  /// std::mt19937 + libc++ uniform_real_distribution<float>(0, 1), so the SSAO
  /// kernel is the same one the GL build used.
  fileprivate struct MT19937 {
    private var mt = [UInt32](repeating: 0, count: 624)
    private var index = 624
    init(seed: UInt32) {
      mt[0] = seed
      for i in 1..<624 { mt[i] = 1_812_433_253 &* (mt[i - 1] ^ (mt[i - 1] >> 30)) &+ UInt32(i) }
    }
    mutating func next() -> UInt32 {
      if index >= 624 {
        for i in 0..<624 {
          let y = (mt[i] & 0x8000_0000) | (mt[(i + 1) % 624] & 0x7fff_ffff)
          var v = mt[(i + 397) % 624] ^ (y >> 1)
          if y & 1 != 0 { v ^= 0x9908_b0df }
          mt[i] = v
        }
        index = 0
      }
      var y = mt[index]
      index += 1
      y ^= y >> 11
      y ^= (y << 7) & 0x9d2c_5680
      y ^= (y << 15) & 0xefc6_0000
      y ^= y >> 18
      return y
    }
    mutating func uniform() -> Float { Float(next()) / Float(4_294_967_296.0) }
  }

  // GL cube face basis (forward, right, up per face, +X -X +Y -Y +Z -Z).
  fileprivate static let cubeFaces: [(f: vec3, r: vec3, u: vec3)] = [
    (vec3(1, 0, 0), vec3(0, 0, -1), vec3(0, -1, 0)), (vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0)),
    (vec3(0, 1, 0), vec3(1, 0, 0), vec3(0, 0, 1)), (vec3(0, -1, 0), vec3(1, 0, 0), vec3(0, 0, -1)),
    (vec3(0, 0, 1), vec3(1, 0, 0), vec3(0, -1, 0)), (vec3(0, 0, -1), vec3(-1, 0, 0), vec3(0, -1, 0)),
  ]
}

// ---------------------------------------------------------------------------
final class Renderer {
  let device: MTLDevice
  /// The composite writes gamma-encoded values itself (like the GL version).
  static let outputPixelFormat: MTLPixelFormat = .bgra8Unorm

  private(set) var settings: RenderSettings
  private(set) var info = ""

  var pieces = (0..<Kind.count).map { _ in PieceGpu() }
  var sword = GpuMesh(), cannonball = GpuMesh(), board = GpuMesh()
  var libraryReady = false

  // Shadow depth bias (GL: glPolygonOffset(1.5, 3.0) on a 24-bit buffer). Metal's
  // constant unit on depth32Float is 2^(exponent - 23) of the primitive's depth: 2^-25
  // around the board (light-space depth ~0.48) vs GL's fixed 2^-24, hence 2 x 3.
  // (The shader's normal offset and compare margins dominate anyway: no acne even at 0.)
  private static let shadowBias: Float = 6.0
  private static let shadowSlopeBias: Float = 1.5

  private static let framesInFlight = 3
  private static let bloomLevels = 6

  private let queue: MTLCommandQueue  // init-time work and texture uploads
  private var library: MTLLibrary!
  private let meshLayout = MTLVertexDescriptor()
  private let memoryless: Bool

  // pipelines (the MSAA-dependent ones are rebuilt when the sample count changes)
  private var mpShadow: MeshPipes!, mpPrepass: MeshPipes!, mpObjectRefl: MeshPipes!, mpObject: MeshPipes!
  private var psBoard: MTLRenderPipelineState!
  private var psBackground: MTLRenderPipelineState!, psParticle: MTLRenderPipelineState!
  private var psEnv: MTLRenderPipelineState!, psIrr: MTLRenderPipelineState!
  private var psPrefilter: MTLRenderPipelineState!, psBrdf: MTLRenderPipelineState!
  private var psSsao: MTLRenderPipelineState!, psSsaoBlur: MTLRenderPipelineState!
  private var psBloomDown: MTLRenderPipelineState!, psBloomUp: MTLRenderPipelineState!
  private var psDofCoc: MTLRenderPipelineState!, psDofBokeh: MTLRenderPipelineState!
  private var psDofComp: MTLRenderPipelineState!, psComposite: MTLRenderPipelineState!
  private var pipelineSamples = 0
  private var dsWrite: MTLDepthStencilState!, dsTest: MTLDepthStencilState!

  private var noiseTex: MTLTexture!, boardAlbedo: MTLTexture!, boardDetail: MTLTexture!
  private var white: MTLTexture!  // stands in for disabled SSAO / reflection inputs
  private var boardHalf: Float = 4.62
  private var envCube: MTLTexture!, envNoKeyCube: MTLTexture!, irrCube: MTLTexture!
  private var prefCube: MTLTexture!, brdfLut: MTLTexture!
  private let envRes = 256, prefRes = 128, prefMips = 6

  private var shadowTex: MTLTexture!
  private var shadowResAlloc = 0
  private var lightVP = mat4.identity
  private var lightDir = vec3(0)
  private var shadowWorld: Float = 14, shadowRange: Float = 50

  private var tw = 0, th = 0  // internal render size
  private var msColor: MTLTexture?, msDepth: MTLTexture!, hdrTex: MTLTexture!
  private var preNormal: MTLTexture!, preDepth: MTLTexture!
  private var ssaoTex: MTLTexture!, ssaoBlurTex: MTLTexture!
  private var reflTex: MTLTexture!, reflDepth: MTLTexture!
  private var rw = 0, rh = 0
  private var dofCocTex: MTLTexture!, dofBokehTex: MTLTexture!, dofOutTex: MTLTexture!
  private var bloomTex: [MTLTexture] = []
  private var ssaoKernel = [vec4](repeating: vec4(0), count: 16)

  // CPU-written per-frame data (particle instances): a ring guarded by a semaphore
  private let frameSemaphore = DispatchSemaphore(value: Renderer.framesInFlight)
  private var frameIndex = 0
  private var particleBuffers = [MTLBuffer?](repeating: nil, count: Renderer.framesInFlight)

  init?(device: MTLDevice, settings s: RenderSettings) {
    self.device = device
    settings = s
    guard let q = device.makeCommandQueue() else { return nil }
    queue = q
    queue.label = "renderer uploads"
    memoryless = device.supportsFamily(.apple1)
    info = "\(device.name) | Metal | \(device.hasUnifiedMemory ? "unified memory" : "discrete memory")"

    // Vertex: pos float3 @0, nrm float3 @16, ao float @32, aux float @36, stride 48
    meshLayout.attributes[0].format = .float3
    meshLayout.attributes[0].offset = 0
    meshLayout.attributes[1].format = .float3
    meshLayout.attributes[1].offset = 16
    meshLayout.attributes[2].format = .float
    meshLayout.attributes[2].offset = 32
    meshLayout.attributes[3].format = .float
    meshLayout.attributes[3].offset = 36
    for i in 0..<4 { meshLayout.attributes[i].bufferIndex = 0 }
    meshLayout.layouts[0].stride = MemoryLayout<Vertex>.stride

    do {
      library = try device.makeLibrary(source: Shaders.source, options: nil)
    } catch {
      // the compiler log ("program_source:LINE:COL: error: ...") is the localized description
      Renderer.logError("[shader] Metal shader compile error:\n\((error as NSError).localizedDescription)")
      return nil
    }
    do {
      try buildPrograms()
    } catch {
      Renderer.logError("[shader] \(error)")
      return nil
    }
    let dsd = MTLDepthStencilDescriptor()
    dsd.depthCompareFunction = .lessEqual
    dsd.isDepthWriteEnabled = true
    dsWrite = device.makeDepthStencilState(descriptor: dsd)
    dsd.isDepthWriteEnabled = false
    dsTest = device.makeDepthStencilState(descriptor: dsd)

    // 3D noise
    noiseTex = uploadImage(Textures.bakeNoise3D(res: 64), format: .rgba8Unorm, mipmapped: true, label: "noise3d")
    white = uploadImage(TexImage(w: 1, h: 1, d: 1, ch: 4, px: [255, 255, 255, 255]), format: .rgba8Unorm,
                        mipmapped: false, label: "white")
    // placeholder board textures until the real ones are baked
    boardAlbedo = uploadImage(TexImage(w: 1, h: 1, d: 1, ch: 4, px: [120, 80, 50, 100]), format: .rgba8Unorm_srgb,
                              mipmapped: false, label: "board albedo")
    boardDetail = uploadImage(TexImage(w: 1, h: 1, d: 1, ch: 4, px: [128, 128, 0, 255]), format: .rgba8Unorm,
                              mipmapped: false, label: "board detail")
    guard noiseTex != nil, white != nil, boardAlbedo != nil, boardDetail != nil else { return nil }

    // SSAO kernel
    var rng = MT19937(seed: 42)
    for i in 0..<16 {
      let x = rng.uniform() * 2 - 1
      let y = rng.uniform() * 2 - 1
      let z = rng.uniform()
      var k = normalize(vec3(x, y, z)) * rng.uniform()
      var sc = Float(i) / 16
      sc = 0.1 + 0.9 * sc * sc
      k *= sc
      ssaoKernel[i] = vec4(k, 0)
    }

    // light
    lightDir = normalize(vec3(-0.78, 1.0, 0.32))
    let center = vec3(0, 0.3, 0)
    let R: Float = 7
    let lv = lookAt(center + lightDir * 25, center, vec3(0, 1, 0))
    let lp = ortho(-R, R, -R, R, 1, 51)
    lightVP = lp * lv
    shadowWorld = 2 * R
    shadowRange = 50

    buildIBL()
    applySettings(s)
    if mpObject == nil { return nil }
  }

  // -------------------------------------------------------------------------
  // pipelines
  // -------------------------------------------------------------------------
  private func makePipeline(_ label: String, vs: String, fs: String?, color: MTLPixelFormat = .invalid,
                            depth: MTLPixelFormat = .invalid, samples: Int = 1, blend: Blend = .none,
                            mesh: Bool = false, dissolve: Bool? = nil) throws -> MTLRenderPipelineState {
    let d = MTLRenderPipelineDescriptor()
    d.label = label
    guard let v = library.makeFunction(name: vs) else { throw RendererError(description: "missing function \(vs)") }
    d.vertexFunction = v
    if let fs {
      if let dissolve {
        let cv = MTLFunctionConstantValues()
        var on = dissolve
        cv.setConstantValue(&on, type: .bool, index: 0)
        d.fragmentFunction = try library.makeFunction(name: fs, constantValues: cv)
      } else {
        guard let f = library.makeFunction(name: fs) else { throw RendererError(description: "missing function \(fs)") }
        d.fragmentFunction = f
      }
    }
    if mesh { d.vertexDescriptor = meshLayout }
    d.colorAttachments[0].pixelFormat = color
    if blend != .none {
      let ca = d.colorAttachments[0]!
      ca.isBlendingEnabled = true
      ca.rgbBlendOperation = .add
      ca.alphaBlendOperation = .add
      ca.sourceRGBBlendFactor = .one
      ca.sourceAlphaBlendFactor = .one
      let dst: MTLBlendFactor = blend == .additive ? .one : .oneMinusSourceAlpha
      ca.destinationRGBBlendFactor = dst
      ca.destinationAlphaBlendFactor = dst
    }
    d.depthAttachmentPixelFormat = depth
    d.rasterSampleCount = samples
    do {
      return try device.makeRenderPipelineState(descriptor: d)
    } catch {
      throw RendererError(description: "pipeline '\(label)': \(error)")
    }
  }

  private func buildPrograms() throws {
    let hdr = MTLPixelFormat.rgba16Float, dep = MTLPixelFormat.depth32Float
    // only dissolving items need a fragment stage in the shadow map
    mpShadow = MeshPipes(plain: try makePipeline("shadow", vs: "meshVS", fs: nil, depth: dep, mesh: true),
                         dissolve: try makePipeline("shadow (dissolve)", vs: "meshVS", fs: "shadowFS", depth: dep,
                                                    mesh: true))
    mpPrepass = try meshPipes("prepass", fs: "prepassFS", color: hdr, depth: dep)
    mpObjectRefl = try meshPipes("object (reflection)", fs: "objectFS", color: hdr, depth: dep)
    psEnv = try makePipeline("envgen", vs: "fullscreenVS", fs: "envGenFS", color: hdr)
    psIrr = try makePipeline("irradiance", vs: "fullscreenVS", fs: "irradianceFS", color: hdr)
    psPrefilter = try makePipeline("prefilter", vs: "fullscreenVS", fs: "prefilterFS", color: hdr)
    psBrdf = try makePipeline("brdf", vs: "fullscreenVS", fs: "brdfLutFS", color: .rg16Float)
    psSsao = try makePipeline("ssao", vs: "fullscreenVS", fs: "ssaoFS", color: .r8Unorm)
    psSsaoBlur = try makePipeline("ssaoblur", vs: "fullscreenVS", fs: "ssaoBlurFS", color: .r8Unorm)
    psBloomDown = try makePipeline("bloomdown", vs: "fullscreenVS", fs: "bloomDownFS", color: hdr)
    psBloomUp = try makePipeline("bloomup", vs: "fullscreenVS", fs: "bloomUpFS", color: hdr, blend: .additive)
    psDofCoc = try makePipeline("dofcoc", vs: "fullscreenVS", fs: "dofCocFS", color: hdr)
    psDofBokeh = try makePipeline("dofbokeh", vs: "fullscreenVS", fs: "dofBokehFS", color: hdr)
    psDofComp = try makePipeline("dofcomp", vs: "fullscreenVS", fs: "dofCompositeFS", color: hdr)
    psComposite = try makePipeline("composite", vs: "fullscreenVS", fs: "compositeFS", color: Renderer.outputPixelFormat)
  }

  private func meshPipes(_ label: String, fs: String, color: MTLPixelFormat, depth: MTLPixelFormat,
                         samples: Int = 1) throws -> MeshPipes {
    MeshPipes(plain: try makePipeline(label, vs: "meshVS", fs: fs, color: color, depth: depth, samples: samples,
                                      mesh: true, dissolve: false),
              dissolve: try makePipeline(label + " (dissolve)", vs: "meshVS", fs: fs, color: color, depth: depth,
                                         samples: samples, mesh: true, dissolve: true))
  }

  /// Pipelines of the multisampled main pass.
  private func buildMainPrograms(samples: Int) throws {
    let hdr = MTLPixelFormat.rgba16Float, dep = MTLPixelFormat.depth32Float
    mpObject = try meshPipes("object", fs: "objectFS", color: hdr, depth: dep, samples: samples)
    psBoard = try makePipeline("board", vs: "meshVS", fs: "boardFS", color: hdr, depth: dep, samples: samples,
                               mesh: true)
    psBackground = try makePipeline("background", vs: "fullscreenVS", fs: "backgroundFS", color: hdr, depth: dep,
                                    samples: samples)
    psParticle = try makePipeline("particle", vs: "particleVS", fs: "particleFS", color: hdr, depth: dep,
                                  samples: samples, blend: .premultiplied)
    pipelineSamples = samples
  }

  // -------------------------------------------------------------------------
  // textures
  // -------------------------------------------------------------------------
  private static func mipCount(_ s: Int) -> Int { Int(floor(log2(Double(max(1, s))))) + 1 }

  private func makeTexture(_ w: Int, _ h: Int, _ fmt: MTLPixelFormat, mips: Bool = false, samples: Int = 1,
                           usage: MTLTextureUsage = [.shaderRead, .renderTarget], transient: Bool = false,
                           label: String) -> MTLTexture {
    let d = MTLTextureDescriptor()
    d.textureType = samples > 1 ? .type2DMultisample : .type2D
    d.pixelFormat = fmt
    d.width = w
    d.height = h
    d.sampleCount = samples
    d.mipmapLevelCount = mips ? Renderer.mipCount(max(w, h)) : 1
    d.usage = usage
    // attachments that never leave tile memory (MSAA colour/depth, reflection depth)
    d.storageMode = transient && memoryless ? .memoryless : .private
    let t = device.makeTexture(descriptor: d)!
    t.label = label
    return t
  }

  private func makeCube(_ res: Int, mips: Int, label: String) -> MTLTexture {
    let d = MTLTextureDescriptor.textureCubeDescriptor(pixelFormat: .rgba16Float, size: res, mipmapped: mips > 1)
    d.mipmapLevelCount = mips
    d.usage = [.shaderRead, .renderTarget]
    d.storageMode = .private
    let t = device.makeTexture(descriptor: d)!
    t.label = label
    return t
  }

  /// Uploads a CPU image (2D, or 3D when d > 1) into a private texture, optionally with a full mip chain.
  private func uploadImage(_ img: TexImage, format: MTLPixelFormat, mipmapped: Bool, label: String) -> MTLTexture? {
    let w = max(1, img.w), h = max(1, img.h), dd = max(1, img.d)
    // the GPU formats here are all 4 channel
    var px = img.px
    if img.ch != 4 {
      px = [UInt8](repeating: 255, count: w * h * dd * 4)
      for i in 0..<(w * h * dd) {
        for c in 0..<min(img.ch, 4) { px[i * 4 + c] = img.px[i * img.ch + c] }
        if img.ch == 1 {
          px[i * 4 + 1] = img.px[i]
          px[i * 4 + 2] = img.px[i]
        }
      }
    }
    guard px.count >= w * h * dd * 4 else { return nil }
    let d = MTLTextureDescriptor()
    d.textureType = dd > 1 ? .type3D : .type2D
    d.pixelFormat = format
    d.width = w
    d.height = h
    d.depth = dd
    d.mipmapLevelCount = mipmapped ? Renderer.mipCount(max(w, h, dd)) : 1
    d.usage = .shaderRead
    d.storageMode = .private
    guard let tex = device.makeTexture(descriptor: d),
          let staging = px.withUnsafeBytes({ device.makeBuffer(bytes: $0.baseAddress!, length: w * h * dd * 4) }),
          let cb = queue.makeCommandBuffer(), let blit = cb.makeBlitCommandEncoder() else { return nil }
    tex.label = label
    blit.copy(from: staging, sourceOffset: 0, sourceBytesPerRow: w * 4, sourceBytesPerImage: w * h * 4,
              sourceSize: MTLSize(width: w, height: h, depth: dd), to: tex, destinationSlice: 0,
              destinationLevel: 0, destinationOrigin: MTLOrigin())
    if mipmapped && d.mipmapLevelCount > 1 { blit.generateMipmaps(for: tex) }
    blit.endEncoding()
    cb.commit()
    cb.waitUntilCompleted()
    return tex
  }

  // -------------------------------------------------------------------------
  func applySettings(_ s: RenderSettings) {
    settings = s
    var samples = 1
    for n in [8, 4, 2] where n <= s.msaa && device.supportsTextureSampleCount(n) {
      samples = n
      break
    }
    settings.msaa = samples
    settings.shadowRes = min(max(s.shadowRes, 256), 16384)
    if shadowResAlloc != settings.shadowRes {
      shadowTex = makeTexture(settings.shadowRes, settings.shadowRes, .depth32Float, label: "shadow map")
      shadowResAlloc = settings.shadowRes
    }
    if pipelineSamples != samples {
      do {
        try buildMainPrograms(samples: samples)
      } catch {
        Renderer.logError("[shader] \(error)")
      }
    }
    tw = 0  // force target re-creation
    th = 0
  }

  func setRenderScale(_ s: Float) { settings.renderScale = s }

  private static func logError(_ msg: String) {
    FileHandle.standardError.write((msg + "\n").data(using: .utf8)!)
  }

  // -------------------------------------------------------------------------
  private func buildIBL() {
    guard let cb = queue.makeCommandBuffer() else { return }
    cb.label = "IBL"
    func renderFaces(_ ps: MTLRenderPipelineState, _ cube: MTLTexture, mip: Int, _ fu: FaceUniforms,
                     env: MTLTexture?) {
      for f in 0..<6 {
        let rp = MTLRenderPassDescriptor()
        rp.colorAttachments[0].texture = cube
        rp.colorAttachments[0].slice = f
        rp.colorAttachments[0].level = mip
        rp.colorAttachments[0].loadAction = .dontCare
        rp.colorAttachments[0].storeAction = .store
        guard let enc = cb.makeRenderCommandEncoder(descriptor: rp) else { continue }
        var u = fu
        u.fwd = Renderer.cubeFaces[f].f
        u.right = Renderer.cubeFaces[f].r
        u.up = Renderer.cubeFaces[f].u
        enc.setRenderPipelineState(ps)
        enc.setCullMode(.none)
        enc.setFragmentBytes(&u, length: MemoryLayout<FaceUniforms>.stride, index: 0)
        if let env { enc.setFragmentTexture(env, index: 0) }
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        enc.endEncoding()
      }
    }
    func genMips(_ t: MTLTexture) {
      guard let blit = cb.makeBlitCommandEncoder() else { return }
      blit.generateMipmaps(for: t)
      blit.endEncoding()
    }
    var fu = FaceUniforms()
    fu.keyDir = lightDir
    fu.includeKey = 1
    envCube = makeCube(envRes, mips: Renderer.mipCount(envRes), label: "env")
    renderFaces(psEnv, envCube, mip: 0, fu, env: nil)
    genMips(envCube)

    envNoKeyCube = makeCube(128, mips: Renderer.mipCount(128), label: "env (no key)")
    fu.includeKey = 0
    renderFaces(psEnv, envNoKeyCube, mip: 0, fu, env: nil)
    genMips(envNoKeyCube)

    irrCube = makeCube(32, mips: 1, label: "irradiance")
    renderFaces(psIrr, irrCube, mip: 0, fu, env: envNoKeyCube)

    prefCube = makeCube(prefRes, mips: prefMips, label: "prefiltered env")
    fu.envRes = Float(envRes)
    for m in 0..<prefMips {
      fu.roughness = Float(m) / Float(prefMips - 1)
      renderFaces(psPrefilter, prefCube, mip: m, fu, env: envCube)
    }

    brdfLut = makeTexture(128, 128, .rg16Float, label: "brdf lut")
    let rp = MTLRenderPassDescriptor()
    rp.colorAttachments[0].texture = brdfLut
    rp.colorAttachments[0].loadAction = .dontCare
    rp.colorAttachments[0].storeAction = .store
    if let enc = cb.makeRenderCommandEncoder(descriptor: rp) {
      enc.setRenderPipelineState(psBrdf)
      enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
      enc.endEncoding()
    }
    cb.commit()
    cb.waitUntilCompleted()
  }

  func uploadLibrary(_ lib: Library) {
    func sync(_ gpu: inout [GpuMesh], _ frags: [Fragment]) {
      while gpu.count < frags.count { gpu.append(GpuMesh()) }
      if gpu.count > frags.count { gpu.removeLast(gpu.count - frags.count) }
      for i in frags.indices { gpu[i].upload(frags[i].mesh, device: device) }
    }
    for k in 0..<Kind.count {
      pieces[k].lod0.upload(lib.piece[k].lod0, device: device)
      pieces[k].lod1.upload(lib.piece[k].lod1, device: device)
      sync(&pieces[k].shards, lib.piece[k].shards)
      sync(&pieces[k].halves, lib.piece[k].halves)
    }
    sword.upload(lib.sword, device: device)
    cannonball.upload(lib.cannonball, device: device)
    board.upload(lib.board, device: device)
    boardHalf = lib.boardHalf
    libraryReady = true
  }

  func uploadBoardTextures(albedo: TexImage, detail: TexImage, half: Float) {
    // trilinear + 8x anisotropy comes from the sBoard sampler in the shader
    if let a = uploadImage(albedo, format: .rgba8Unorm_srgb, mipmapped: true, label: "board albedo") { boardAlbedo = a }
    if let d = uploadImage(detail, format: .rgba8Unorm, mipmapped: true, label: "board detail") { boardDetail = d }
    boardHalf = half
  }

  // -------------------------------------------------------------------------
  private func createTargets(_ w: Int, _ h: Int) {
    tw = w
    th = h
    let samples = max(1, settings.msaa)
    let rt: MTLTextureUsage = .renderTarget
    msColor = samples > 1
      ? makeTexture(w, h, .rgba16Float, samples: samples, usage: rt, transient: true, label: "msaa color") : nil
    msDepth = makeTexture(w, h, .depth32Float, samples: samples, usage: rt, transient: true, label: "main depth")
    hdrTex = makeTexture(w, h, .rgba16Float, label: "hdr")

    preNormal = makeTexture(w, h, .rgba16Float, label: "prepass normal")
    preDepth = makeTexture(w, h, .depth32Float, label: "prepass depth")

    let hw = max(1, w / 2), hh = max(1, h / 2)
    ssaoTex = makeTexture(hw, hh, .r8Unorm, label: "ssao")
    ssaoBlurTex = makeTexture(hw, hh, .r8Unorm, label: "ssao blur")

    rw = hw
    rh = hh
    reflTex = makeTexture(rw, rh, .rgba16Float, mips: true, label: "reflection")
    reflDepth = makeTexture(rw, rh, .depth32Float, usage: rt, transient: true, label: "reflection depth")

    dofCocTex = makeTexture(hw, hh, .rgba16Float, label: "dof coc")
    dofBokehTex = makeTexture(hw, hh, .rgba16Float, label: "dof bokeh")
    dofOutTex = makeTexture(w, h, .rgba16Float, label: "dof out")

    bloomTex = []
    var bw = hw, bh = hh
    for i in 0..<Renderer.bloomLevels {
      bloomTex.append(makeTexture(max(1, bw), max(1, bh), .rgba16Float, label: "bloom \(i)"))
      bw /= 2
      bh /= 2
    }
  }

  // -------------------------------------------------------------------------
  private func lightingUniforms(_ sv: SceneView, reflectionPass: Bool) -> PassUniforms {
    var u = PassUniforms()
    var cam = sv.camPos
    if reflectionPass { cam.y = -cam.y }
    u.camPos = cam
    u.lightDir = lightDir
    u.lightColor = vec3(1.0, 0.92, 0.82) * 4.0
    u.lightVP = lightVP
    u.shadowTexel = 1 / Float(shadowResAlloc)
    u.shadowWorld = shadowWorld
    u.shadowRange = shadowRange
    u.shadowsOn = settings.shadows ? 1 : 0
    u.prefilterMaxLod = Float(prefMips - 1)
    u.ambient = 0.72
    u.numLights = Int32(min(8, sv.lights.count))
    u.ssaoOn = (!reflectionPass && settings.ssao) ? 1 : 0
    u.invScreen = reflectionPass ? vec2(1 / Float(rw), 1 / Float(rh)) : vec2(1 / Float(tw), 1 / Float(th))
    u.time = sv.time
    u.boardHalf = boardHalf
    return u
  }

  /// Pass uniforms for both stages, point lights and the lighting textures.
  private func bindLighting(_ enc: MTLRenderCommandEncoder, _ pu: PassUniforms, _ sv: SceneView, ssao: MTLTexture) {
    var u = pu
    enc.setVertexBytes(&u, length: MemoryLayout<PassUniforms>.stride, index: 2)
    enc.setFragmentBytes(&u, length: MemoryLayout<PassUniforms>.stride, index: 2)
    var lights = [vec4](repeating: vec4(0), count: 16)  // [0..7] pos + radius, [8..15] colour
    for i in 0..<min(8, sv.lights.count) {
      lights[i] = vec4(sv.lights[i].pos, sv.lights[i].radius)
      lights[8 + i] = vec4(sv.lights[i].color, 0)
    }
    enc.setFragmentBytes(lights, length: 16 * MemoryLayout<vec4>.stride, index: 3)
    enc.setFragmentTexture(shadowTex, index: 0)
    enc.setFragmentTexture(irrCube, index: 1)
    enc.setFragmentTexture(prefCube, index: 2)
    enc.setFragmentTexture(brdfLut, index: 3)
    enc.setFragmentTexture(ssao, index: 4)
    enc.setFragmentTexture(noiseTex, index: 5)
  }

  @inline(__always) private func draw(_ enc: MTLRenderCommandEncoder, _ m: GpuMesh) {
    guard let vb = m.vertexBuffer, let ib = m.indexBuffer, m.count > 0 else { return }
    enc.setVertexBuffer(vb, offset: 0, index: 0)
    enc.drawIndexedPrimitives(type: .triangle, indexCount: m.count, indexType: .uint32, indexBuffer: ib,
                              indexBufferOffset: 0)
  }

  private func drawItems(_ enc: MTLRenderCommandEncoder, _ sv: SceneView, _ pipes: MeshPipes, lowLod: Bool,
                         shadowPass: Bool, reflectPass: Bool) {
    var bound: MTLRenderPipelineState?
    for it in sv.items {
      if shadowPass && !it.castShadow { continue }
      if reflectPass && !it.reflect { continue }
      guard let m = (lowLod && it.meshLow != nil) ? it.meshLow : it.mesh, !m.isEmpty else { continue }
      var du = DrawUniforms(it)
      let ps = it.dissolve > 0 ? pipes.dissolve : pipes.plain
      if ps !== bound {
        enc.setRenderPipelineState(ps)
        bound = ps
      }
      enc.setVertexBytes(&du, length: MemoryLayout<DrawUniforms>.stride, index: 1)
      enc.setFragmentBytes(&du, length: MemoryLayout<DrawUniforms>.stride, index: 1)
      draw(enc, m)
    }
  }

  private func drawBoard(_ enc: MTLRenderCommandEncoder) {
    var du = DrawUniforms()
    enc.setVertexBytes(&du, length: MemoryLayout<DrawUniforms>.stride, index: 1)
    enc.setFragmentBytes(&du, length: MemoryLayout<DrawUniforms>.stride, index: 1)
    draw(enc, board)
  }

  /// One fullscreen-triangle pass into `target` (mip 0).
  private func fullscreen(_ cb: MTLCommandBuffer, _ label: String, _ target: MTLTexture, _ ps: MTLRenderPipelineState,
                          load: MTLLoadAction = .dontCare, _ bind: (MTLRenderCommandEncoder) -> Void) {
    let rp = MTLRenderPassDescriptor()
    rp.colorAttachments[0].texture = target
    rp.colorAttachments[0].loadAction = load
    rp.colorAttachments[0].storeAction = .store
    guard let enc = cb.makeRenderCommandEncoder(descriptor: rp) else { return }
    enc.label = label
    enc.setRenderPipelineState(ps)
    enc.setCullMode(.none)
    bind(enc)
    enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
    enc.endEncoding()
  }

  // -------------------------------------------------------------------------
  /// Encodes the whole frame into cb; the final image goes to `target`
  /// (outputPixelFormat, its size is the output size).
  func render(_ sv: SceneView, commandBuffer cb: MTLCommandBuffer, target: MTLTexture) {
    // the particle instance buffer is CPU written: wait until the GPU has released this slot
    frameSemaphore.wait()
    let sem = frameSemaphore
    cb.addCompletedHandler { _ in sem.signal() }
    let slot = frameIndex % Renderer.framesInFlight
    frameIndex += 1

    let fbW = target.width, fbH = target.height
    let w = max(16, Int(Float(fbW) * settings.renderScale))
    let h = max(16, Int(Float(fbH) * settings.renderScale))
    if w != tw || h != th { createTargets(w, h) }
    let viewProj = sv.proj * sv.view
    let nearFar = vec2(sv.nearZ, sv.farZ)

    // ---- 1. shadow map ----
    if settings.shadows {
      let rp = MTLRenderPassDescriptor()
      rp.depthAttachment.texture = shadowTex
      rp.depthAttachment.loadAction = .clear
      rp.depthAttachment.clearDepth = 1
      rp.depthAttachment.storeAction = .store
      if let enc = cb.makeRenderCommandEncoder(descriptor: rp) {
        enc.label = "shadow map"
        enc.setDepthStencilState(dsWrite)
        enc.setCullMode(.back)
        enc.setFrontFacing(.counterClockwise)
        enc.setDepthBias(Renderer.shadowBias, slopeScale: Renderer.shadowSlopeBias, clamp: 0)
        var pu = PassUniforms()
        pu.viewProj = lightVP
        enc.setVertexBytes(&pu, length: MemoryLayout<PassUniforms>.stride, index: 2)
        enc.setFragmentTexture(noiseTex, index: 5)
        drawItems(enc, sv, mpShadow, lowLod: true, shadowPass: true, reflectPass: false)
        enc.setRenderPipelineState(mpShadow.plain)
        drawBoard(enc)
        enc.endEncoding()
      }
    }

    // ---- 2. planar reflection of everything above the board ----
    // (the lacquered top can only be seen while the camera is above it)
    let reflect = settings.reflections && sv.camPos.y > 0
    if reflect {
      let rp = MTLRenderPassDescriptor()
      rp.colorAttachments[0].texture = reflTex
      rp.colorAttachments[0].loadAction = .clear
      rp.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 0)
      rp.colorAttachments[0].storeAction = .store
      rp.depthAttachment.texture = reflDepth
      rp.depthAttachment.loadAction = .clear
      rp.depthAttachment.clearDepth = 1
      rp.depthAttachment.storeAction = .dontCare
      if let enc = cb.makeRenderCommandEncoder(descriptor: rp) {
        enc.label = "reflection"
        enc.setDepthStencilState(dsWrite)
        enc.setCullMode(.back)
        enc.setFrontFacing(.clockwise)  // the mirror flips the winding
        var pu = lightingUniforms(sv, reflectionPass: true)
        pu.viewProj = sv.proj * sv.view * scale(mat4.identity, vec3(1, -1, 1))
        pu.clip = 1
        bindLighting(enc, pu, sv, ssao: white)
        drawItems(enc, sv, mpObjectRefl, lowLod: true, shadowPass: false, reflectPass: true)
        enc.endEncoding()
      }
      if let blit = cb.makeBlitCommandEncoder() {
        blit.generateMipmaps(for: reflTex)
        blit.endEncoding()
      }
    }

    // ---- 3. depth / normal prepass ----
    do {
      let rp = MTLRenderPassDescriptor()
      rp.colorAttachments[0].texture = preNormal
      rp.colorAttachments[0].loadAction = .clear
      rp.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 1, alpha: 1)
      rp.colorAttachments[0].storeAction = .store
      rp.depthAttachment.texture = preDepth
      rp.depthAttachment.loadAction = .clear
      rp.depthAttachment.clearDepth = 1
      rp.depthAttachment.storeAction = .store
      if let enc = cb.makeRenderCommandEncoder(descriptor: rp) {
        enc.label = "prepass"
        enc.setDepthStencilState(dsWrite)
        enc.setCullMode(.back)
        enc.setFrontFacing(.counterClockwise)
        var pu = PassUniforms()
        pu.viewProj = viewProj
        pu.view = sv.view
        enc.setVertexBytes(&pu, length: MemoryLayout<PassUniforms>.stride, index: 2)
        enc.setFragmentBytes(&pu, length: MemoryLayout<PassUniforms>.stride, index: 2)
        enc.setFragmentTexture(noiseTex, index: 5)
        drawItems(enc, sv, mpPrepass, lowLod: false, shadowPass: false, reflectPass: false)
        enc.setRenderPipelineState(mpPrepass.plain)
        drawBoard(enc)
        enc.endEncoding()
      }
    }

    // ---- 4. SSAO ----
    if settings.ssao {
      fullscreen(cb, "ssao", ssaoTex, psSsao) { enc in
        var u = SsaoUniforms(proj: sv.proj, invProj: sv.proj.inverse, radius: 0.22)
        enc.setFragmentBytes(&u, length: MemoryLayout<SsaoUniforms>.stride, index: 0)
        enc.setFragmentBytes(ssaoKernel, length: 16 * MemoryLayout<vec4>.stride, index: 1)
        enc.setFragmentTexture(preDepth, index: 0)
        enc.setFragmentTexture(preNormal, index: 1)
      }
      fullscreen(cb, "ssao blur", ssaoBlurTex, psSsaoBlur) { enc in
        var u = SsaoBlurUniforms(texel: vec2(2 / Float(tw), 2 / Float(th)), nearFar: nearFar)
        enc.setFragmentBytes(&u, length: MemoryLayout<SsaoBlurUniforms>.stride, index: 0)
        enc.setFragmentTexture(ssaoTex, index: 0)
        enc.setFragmentTexture(preDepth, index: 1)
      }
    }

    // ---- 5. main HDR pass (MSAA, resolved into hdrTex at the end of the pass) ----
    do {
      let rp = MTLRenderPassDescriptor()
      if let msColor {
        rp.colorAttachments[0].texture = msColor
        rp.colorAttachments[0].resolveTexture = hdrTex
        rp.colorAttachments[0].storeAction = .multisampleResolve  // ---- 6. resolve ----
      } else {
        rp.colorAttachments[0].texture = hdrTex
        rp.colorAttachments[0].storeAction = .store
      }
      rp.colorAttachments[0].loadAction = .clear
      rp.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
      rp.depthAttachment.texture = msDepth
      rp.depthAttachment.loadAction = .clear
      rp.depthAttachment.clearDepth = 1
      rp.depthAttachment.storeAction = .dontCare
      if let enc = cb.makeRenderCommandEncoder(descriptor: rp) {
        enc.label = "main"
        enc.setDepthStencilState(dsWrite)
        enc.setCullMode(.back)
        enc.setFrontFacing(.counterClockwise)
        var pu = lightingUniforms(sv, reflectionPass: false)
        pu.viewProj = viewProj
        bindLighting(enc, pu, sv, ssao: settings.ssao ? ssaoBlurTex : white)

        // board
        enc.setRenderPipelineState(psBoard)
        var bu = BoardUniforms()
        bu.check = sv.check
        bu.reflectOn = reflect ? 1 : 0
        bu.reflectLod = 0.9
        let no = min(32, sv.occluders.count)
        bu.numOcc = Int32(no)
        let nr = min(6, sv.rings.count, sv.ringColors.count)
        bu.numRings = Int32(nr)
        enc.setFragmentBytes(&bu, length: MemoryLayout<BoardUniforms>.stride, index: 4)
        var occ = [vec4](repeating: vec4(0), count: max(1, no))
        for i in 0..<no { occ[i] = sv.occluders[i] }
        enc.setFragmentBytes(occ, length: occ.count * MemoryLayout<vec4>.stride, index: 5)
        var rings = [vec4](repeating: vec4(0), count: 12)  // [0..5] x, z, radius, intensity; [6..11] rgb, width
        for i in 0..<nr {
          rings[i] = sv.rings[i]
          rings[6 + i] = sv.ringColors[i]
        }
        enc.setFragmentBytes(rings, length: 12 * MemoryLayout<vec4>.stride, index: 6)
        var state = [UInt8](repeating: 0, count: 256)  // the 8x8 board state as uchar4[64]
        for i in 0..<min(256, sv.boardState.count) { state[i] = sv.boardState[i] }
        enc.setFragmentBytes(state, length: 256, index: 7)
        enc.setFragmentTexture(boardAlbedo, index: 6)
        enc.setFragmentTexture(boardDetail, index: 7)
        enc.setFragmentTexture(reflect ? reflTex : white, index: 8)
        drawBoard(enc)

        // pieces, fragments, props
        drawItems(enc, sv, mpObject, lowLod: false, shadowPass: false, reflectPass: false)

        // background
        enc.setRenderPipelineState(psBackground)
        enc.setDepthStencilState(dsTest)
        enc.setCullMode(.none)
        var bg = BackgroundUniforms(invViewProj: viewProj.inverse, camPos: sv.camPos, bgLod: 3.2, bgIntensity: 0.35)
        enc.setFragmentBytes(&bg, length: MemoryLayout<BackgroundUniforms>.stride, index: 0)
        enc.setFragmentTexture(envCube, index: 0)
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)

        // particles, sorted back to front
        if !sv.particles.isEmpty, let buf = particleBuffer(slot: slot, count: sv.particles.count) {
          let n = sv.particles.count
          let v = sv.view
          var keyed = [(Float, Int32)]()
          keyed.reserveCapacity(n)
          for i in 0..<n {
            let p = sv.particles[i].posSize
            keyed.append((v[0].z * p.x + v[1].z * p.y + v[2].z * p.z + v[3].z, Int32(i)))
          }
          keyed.sort { $0.0 < $1.0 }
          let dst = buf.contents().bindMemory(to: ParticleGPU.self, capacity: n)
          sv.particles.withUnsafeBufferPointer { src in
            for j in 0..<n { dst[j] = src[Int(keyed[j].1)] }
          }
          enc.setRenderPipelineState(psParticle)
          var u = ParticleUniforms(view: sv.view, proj: sv.proj, invScreen: vec2(1 / Float(tw), 1 / Float(th)),
                                   nearFar: nearFar)
          enc.setVertexBytes(&u, length: MemoryLayout<ParticleUniforms>.stride, index: 0)
          enc.setFragmentBytes(&u, length: MemoryLayout<ParticleUniforms>.stride, index: 0)
          enc.setVertexBuffer(buf, offset: 0, index: 1)
          enc.setFragmentTexture(preDepth, index: 0)
          enc.setFragmentTexture(noiseTex, index: 1)
          enc.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4, instanceCount: n)
        }
        enc.endEncoding()
      }
    }

    var sceneTex: MTLTexture = hdrTex
    // ---- 7. depth of field ----
    if settings.dof && sv.aperture > 0.01 {
      let hw = max(1, tw / 2), hh = max(1, th / 2)
      let maxCoc: Float = 11 * (Float(th) / 720)
      fullscreen(cb, "dof coc", dofCocTex, psDofCoc) { enc in
        var u = DofCocUniforms(nearFar: nearFar, srcTexel: vec2(1 / Float(tw), 1 / Float(th)), focus: sv.focusDist,
                               aperture: sv.aperture * (Float(th) / 720), maxCoc: maxCoc)
        enc.setFragmentBytes(&u, length: MemoryLayout<DofCocUniforms>.stride, index: 0)
        enc.setFragmentTexture(hdrTex, index: 0)
        enc.setFragmentTexture(preDepth, index: 1)
      }
      fullscreen(cb, "dof bokeh", dofBokehTex, psDofBokeh) { enc in
        var u = DofBokehUniforms(texel: vec2(1 / Float(hw), 1 / Float(hh)), maxCoc: maxCoc)
        enc.setFragmentBytes(&u, length: MemoryLayout<DofBokehUniforms>.stride, index: 0)
        enc.setFragmentTexture(dofCocTex, index: 0)
      }
      fullscreen(cb, "dof composite", dofOutTex, psDofComp) { enc in
        enc.setFragmentTexture(hdrTex, index: 0)
        enc.setFragmentTexture(dofBokehTex, index: 1)
        enc.setFragmentTexture(dofCocTex, index: 2)
      }
      sceneTex = dofOutTex
    }

    // ---- 8. bloom ----
    if settings.bloom {
      for i in 0..<Renderer.bloomLevels {
        let src = i == 0 ? sceneTex : bloomTex[i - 1]
        fullscreen(cb, "bloom down \(i)", bloomTex[i], psBloomDown) { enc in
          var u = BloomUniforms(texel: vec2(1 / Float(src.width), 1 / Float(src.height)), first: i == 0 ? 1 : 0)
          enc.setFragmentBytes(&u, length: MemoryLayout<BloomUniforms>.stride, index: 0)
          enc.setFragmentTexture(src, index: 0)
        }
      }
      for i in stride(from: Renderer.bloomLevels - 1, to: 0, by: -1) {
        let src = bloomTex[i]
        fullscreen(cb, "bloom up \(i)", bloomTex[i - 1], psBloomUp, load: .load) { enc in
          var u = BloomUniforms(texel: vec2(1 / Float(src.width), 1 / Float(src.height)), first: 0)
          enc.setFragmentBytes(&u, length: MemoryLayout<BloomUniforms>.stride, index: 0)
          enc.setFragmentTexture(src, index: 0)
        }
      }
    }

    // ---- 9. tonemap + composite to the output ----
    fullscreen(cb, "composite", target, psComposite) { enc in
      var u = CompositeUniforms(flash: sv.flash, bloomStrength: 0.045, exposure: sv.exposure, vignette: 0.55,
                                grain: 0.012, time: sv.time, fade: sv.fade, bloomOn: settings.bloom ? 1 : 0)
      enc.setFragmentBytes(&u, length: MemoryLayout<CompositeUniforms>.stride, index: 0)
      enc.setFragmentTexture(sceneTex, index: 0)
      enc.setFragmentTexture(bloomTex[0], index: 1)
    }
  }

  /// The ring slot's particle instance buffer, grown when needed (safe: the slot is not in flight).
  private func particleBuffer(slot: Int, count: Int) -> MTLBuffer? {
    let need = count * MemoryLayout<ParticleGPU>.stride
    if let b = particleBuffers[slot], b.length >= need { return b }
    let cap = max(4096 * MemoryLayout<ParticleGPU>.stride, need * 2)
    let b = device.makeBuffer(length: cap, options: [.storageModeShared, .cpuCacheModeWriteCombined])
    b?.label = "particles \(slot)"
    particleBuffers[slot] = b
    return b
  }
}
