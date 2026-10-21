// Metal Shading Language sources (embedded so the executable is self-contained).
// Compiled once at startup with device.makeLibrary(source:options:).
//
// GL -> Metal conventions used throughout:
// - Clip-space depth runs 0..1. Window depth values are the same numbers GL
//   stores (d = f/(f-n) * (1 - n/z)); only the depth <-> NDC mapping changed.
// - Textures and [[position]] have a top-left origin. Screen-space targets are
//   written and read in that same convention, and fullscreen passes get
//   uv = position.xy / target size, so sampling stays self-consistent. Only
//   explicit uv <-> NDC conversions flip y (background, SSAO, shadow lookup).
// - Cube faces keep the GL face basis table: uv = texel / size, ndc = uv*2-1.

enum Shaders {
  static let source = mslCommon + mslLighting + mslMesh + mslObject + mslBoard + mslFullscreen + mslIBL + mslPost +
    mslParticles
}

// ===========================================================================
// shared constants / helpers / samplers / uniform blocks
// ===========================================================================
extension Shaders {
  fileprivate static let mslCommon = #"""
#include <metal_stdlib>
using namespace metal;

#define PI 3.14159265359
float ign(float2 p) { return fract(52.9829189 * fract(dot(p, float2(0.06711056, 0.00583715)))); }
float hash12(float2 p) {
  float3 p3 = fract(float3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}
float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
// window depth -> linear view distance (Metal's [0, 1] depth range)
float linZ(float d, float2 nf) { return nf.x * nf.y / (nf.y - d * (nf.y - nf.x)); }

constexpr sampler sLinear(filter::linear, address::clamp_to_edge);
constexpr sampler sNearest(filter::nearest, address::clamp_to_edge);
constexpr sampler sTrilinear(filter::linear, mip_filter::linear, address::clamp_to_edge);
constexpr sampler sNoise(filter::linear, mip_filter::linear, address::repeat);
constexpr sampler sBoard(filter::linear, mip_filter::linear, address::clamp_to_edge, max_anisotropy(8));
constexpr sampler sShadowCmp(filter::linear, address::clamp_to_edge, compare_func::less_equal);

// ---- uniform blocks (mirrored by the Swift structs in Renderer.swift) ----
// per pass: camera, key light, shadows, IBL, point light count
struct PassU {
  float4x4 viewProj;
  float4x4 view;
  float4x4 lightVP;
  float3 camPos;
  float3 lightDir;
  float3 lightColor;
  float2 invScreen;
  float shadowTexel;
  float shadowWorld;
  float shadowRange;
  float prefilterMaxLod;
  float ambient;
  float time;
  float boardHalf;
  int shadowsOn;
  int numLights;
  int ssaoOn;
  int clip;          // 1: clip at the board plane (reflection pass)
};

// per draw item
struct DrawU {
  float4x4 model;
  float4 rig;        // x bend (rad), y bend direction (rad), z squash/stretch, w twist (rad)
  float4 glow;       // rgb + strength
  float3 texOffset;
  float3 crackColor;
  float3 dissolveColor;
  float rigH;        // rest height of the piece
  float dissolve;
  float crack;
  float heat;
  float groundAO;
  int mat;           // 0 white marble, 1 black marble, 2 sword, 3 iron, 4 medieval figure
  int team;          // medieval: 0 white army, 1 black army
  int boneCount;     // > 0: skinned
  float stone;       // medieval: turned to salt
};

// board-only parameters (occluders, rings and the 8x8 state live in their own buffers)
struct BoardU {
  float4 check;
  int numOcc;
  int numRings;
  int reflectOn;
  float reflectLod;
};
"""#
}

// ===========================================================================
// PBR + shadows + IBL
// ===========================================================================
extension Shaders {
  fileprivate static let mslLighting = #"""
// Resources every lit fragment needs. Lights: [0..7] pos + radius, [8..15] colour.
#define LIGHT_PARAMS constant PassU& u, constant float4* lights, depth2d<float> shadowMap, \
  texturecube<float> irradiance, texturecube<float> prefilter, texture2d<float> brdfLut, \
  texture2d<float> ssaoTex, float2 fragCoord
#define LIGHT_ARGS u, lights, shadowMap, irradiance, prefilter, brdfLut, ssaoTex, fragCoord
#define LIGHT_BINDINGS constant PassU& u [[buffer(2)]], constant float4* lights [[buffer(3)]], \
  depth2d<float> shadowMap [[texture(0)]], texturecube<float> irradiance [[texture(1)]], \
  texturecube<float> prefilter [[texture(2)]], texture2d<float> brdfLut [[texture(3)]], \
  texture2d<float> ssaoTex [[texture(4)]]

constant float2 POISSON[16] = {
  float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725), float2(-0.09418410, -0.92938870),
  float2(0.34495938, 0.29387760), float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
  float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379), float2(0.44323325, -0.97511554),
  float2(0.53742981, -0.47373420), float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
  float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590), float2(0.19984126, 0.78641367),
  float2(0.14383161, -0.14100790)};

float D_GGX(float NoH, float a) {
  float a2 = a * a;
  float d = NoH * NoH * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d + 1e-7);
}
float V_Smith(float NoV, float NoL, float a) {
  float a2 = a * a;
  float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
  float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
  return 0.5 / max(gv + gl, 1e-5);
}
float3 F_Schlick(float VoH, float3 f0) { return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0); }
float3 F_SchlickR(float NoV, float3 f0, float r) {
  return f0 + (max(float3(1.0 - r), f0) - f0) * pow(1.0 - NoV, 5.0);
}

// The key light is a softbox hanging over the board: everything outside
// the pool of light around it falls off into darkness.
float lightPool(float3 p) { return mix(1.0, 0.04, smoothstep(5.2, 12.0, length(p.xz))); }

// light clip space -> shadow map uv (rows run top-down in Metal) and [0, 1] depth
float3 shadowCoord(float4 lp) { return float3(lp.xy / lp.w * float2(0.5, -0.5) + 0.5, lp.z / lp.w); }

// Raw (non-comparison) shadow map depth for the blocker search and the thickness.
float rawDepth(float2 uv, LIGHT_PARAMS) {
  return shadowMap.sample(sNearest, uv, level(0));
}

float shadowAt(float3 wpos, float3 n, thread float& thick, LIGHT_PARAMS) {
  thick = 10.0;
  float3 L = u.lightDir;
  float NoL = dot(n, L);
  float3 p = wpos + n * (0.010 * (1.0 - saturate(NoL)) + 0.003);
  float3 sc = shadowCoord(u.lightVP * float4(p, 1.0));
  if (sc.x < 0.001 || sc.x > 0.999 || sc.y < 0.001 || sc.y > 0.999 || sc.z > 1.0) return 1.0;
  float rot = ign(fragCoord) * 6.2831853;
  float2x2 R = float2x2(float2(cos(rot), sin(rot)), float2(-sin(rot), cos(rot)));
  // distance light travelled inside the object (for translucency)
  float3 sc2 = shadowCoord(u.lightVP * float4(wpos - n * 0.006, 1.0));
  // nearest occluder depth in a small neighbourhood: robust at light-space silhouettes
  float dmin = rawDepth(sc2.xy, LIGHT_ARGS);
  float tt = 2.5 * u.shadowTexel;
  dmin = min(dmin, rawDepth(sc2.xy + float2(tt, 0.0), LIGHT_ARGS));
  dmin = min(dmin, rawDepth(sc2.xy - float2(tt, 0.0), LIGHT_ARGS));
  dmin = min(dmin, rawDepth(sc2.xy + float2(0.0, tt), LIGHT_ARGS));
  dmin = min(dmin, rawDepth(sc2.xy - float2(0.0, tt), LIGHT_ARGS));
  thick = max(0.0, sc2.z - dmin) * u.shadowRange;
  // PCSS blocker search
  float searchR = 0.09 / u.shadowWorld;
  float bsum = 0.0, bcnt = 0.0;
  for (int i = 0; i < 8; ++i) {
    float2 o = R * POISSON[i * 2] * searchR;
    float d = rawDepth(sc.xy + o, LIGHT_ARGS);
    if (d < sc.z - 0.00015) { bsum += d; bcnt += 1.0; }
  }
  if (bcnt < 0.5) return 1.0;
  float dist = (sc.z - bsum / bcnt) * u.shadowRange;
  float pen = clamp(dist * 0.05, 0.0, 0.12);
  float r = max(pen / u.shadowWorld, 1.6 * u.shadowTexel);
  float s = 0.0;
  for (int i = 0; i < 16; ++i) {
    float2 o = R * POISSON[i] * r;
    s += shadowMap.sample_compare(sShadowCmp, sc.xy + o, sc.z - 0.00012, level(0));
  }
  return s / 16.0;
}

struct Surface {
  float3 P;
  float3 N;
  float3 V;
  float3 albedo;
  float rough;
  float metal;
  float ao;
  float wrap;
  float3 sss;
  float specOcc;
};

float3 shadeSurface(Surface s, float shadow, float thick, LIGHT_PARAMS) {
  float3 N = s.N, V = s.V, L = u.lightDir;
  float NoV = max(dot(N, V), 1e-4);
  float NoL = dot(N, L);
  float3 f0 = mix(float3(0.04), s.albedo, s.metal);
  float3 diffC = s.albedo * (1.0 - s.metal);
  float pool = lightPool(s.P);
  float3 Fv = F_SchlickR(NoV, f0, s.rough);

  // --- key light (diffuse; its specular lives in the environment softbox)
  float w = s.wrap;
  float wrapped = max(0.0, (NoL + w) / ((1.0 + w) * (1.0 + w)));
  float3 col = diffC * (1.0 - Fv) * (1.0 / PI) * u.lightColor * wrapped * shadow * pool;
  // subsurface translucency: thin marble glows when back-lit
  col += s.sss * diffC * u.lightColor * pool * exp(-max(thick, 0.012) * 16.0) * saturate(0.3 - NoL * 0.7) * 0.18;

  // --- dynamic point lights (sparks, magic, muzzle flashes)
  for (int i = 0; i < 8; ++i) {
    if (i >= u.numLights) break;
    float4 lpos = lights[i];
    float3 Lv = lpos.xyz - s.P;
    float d2 = dot(Lv, Lv);
    float d = sqrt(d2);
    float3 Ld = Lv / max(d, 1e-4);
    float fall = saturate(1.0 - pow(d / lpos.w, 4.0));
    fall *= fall;
    float3 rad = lights[8 + i].rgb * fall / (d2 + 0.04);
    float nl = saturate(dot(N, Ld));
    float3 H = normalize(V + Ld);
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));
    float a = max(s.rough * s.rough, 0.02);
    float3 F = F_Schlick(VoH, f0);
    float3 spec = D_GGX(NoH, a) * V_Smith(NoV, nl, a) * F;
    col += (diffC * (1.0 - F) / PI + spec) * rad * nl;
    col += s.sss * diffC * rad * saturate(0.25 - dot(N, Ld) * 0.6) * 0.3;
  }

  // --- image based ambient
  float ao = s.ao;
  if (u.ssaoOn == 1) ao *= ssaoTex.sample(sLinear, fragCoord * u.invScreen).r;
  float3 R = reflect(-V, N);
  float3 irr = irradiance.sample(sTrilinear, N, level(0)).rgb;
  float3 pre = prefilter.sample(sTrilinear, R, level(s.rough * u.prefilterMaxLod)).rgb;
  float2 ab = brdfLut.sample(sLinear, float2(NoV, s.rough)).rg;
  float3 specIBL = pre * (Fv * ab.x + ab.y);
  float keyAlign = smoothstep(0.72, 0.97, dot(R, L));
  specIBL *= mix(1.0, shadow, keyAlign * 0.92);
  float so = saturate(pow(NoV + ao, exp2(-16.0 * s.rough - 1.0)) - 1.0 + ao) * s.specOcc;
  col += (diffC * (1.0 - Fv) * irr * ao * mix(1.0, pool, 0.75) + specIBL * so) * u.ambient;
  return col;
}
"""#
}

// ===========================================================================
// deformable mesh vertex shader (rig) + shadow / prepass fragments
// ===========================================================================
extension Shaders {
  fileprivate static let mslMesh = #"""
struct MeshIn {
  float3 pos [[attribute(0)]];
  float3 nrm [[attribute(1)]];
  float ao [[attribute(2)]];
  float aux [[attribute(3)]];
  float bones [[attribute(4)]];  // bone0 + 64 * bone1
  float mat [[attribute(5)]];    // material + 0.998 * weight of bone0
};
struct MeshOut {
  float4 position [[position]];
  float3 world;
  float3 nrm;
  float3 obj;
  float3 objN;
  float ao;
  float aux;
  float mat [[flat]];
  float clip [[clip_distance]] [1];
};
// same varyings without the clip distance (fragment stage_in can't take it)
struct MeshFrag {
  float4 position [[position]];
  float3 world;
  float3 nrm;
  float3 obj;
  float3 objN;
  float ao;
  float aux;
  float mat [[flat]];
};

float3 deform(float3 p, thread float3& n, float4 rig, float rigH) {
  float H = max(rigH, 0.1);
  float s = max(rig.z, 0.05);
  float is = rsqrt(s);
  p.y *= s;
  p.xz *= is;
  n = normalize(float3(n.x * sqrt(s), n.y / s, n.z * sqrt(s)));
  float Hs = H * s;
  // twist increases with height
  float ta = rig.w * clamp(p.y / Hs, 0.0, 1.2);
  float ct = cos(ta), st = sin(ta);
  p.xz = float2(ct * p.x - st * p.z, st * p.x + ct * p.z);
  n.xz = float2(ct * n.x - st * n.z, st * n.x + ct * n.z);
  float b = rig.x;
  if (abs(b) > 1e-4) {
    float2 d = float2(cos(rig.y), sin(rig.y));
    float uu = dot(p.xz, d);
    float2 perp = p.xz - d * uu;
    float R = Hs / b;
    float a = b * (p.y / Hs);
    float ca = cos(a), sa = sin(a);
    float u2 = R - (R - uu) * ca;
    float y2 = (R - uu) * sa;
    p = float3(d.x * u2 + perp.x, y2, d.y * u2 + perp.y);
    float nu = dot(n.xz, d);
    float2 nperp = n.xz - d * nu;
    float nu2 = nu * ca + n.y * sa;
    float ny2 = -nu * sa + n.y * ca;
    n = float3(d.x * nu2 + nperp.x, ny2, d.y * nu2 + nperp.y);
  }
  return p;
}

// bones: the skinning palette (at least one matrix is always bound)
vertex MeshOut meshVS(MeshIn in [[stage_in]], constant DrawU& d [[buffer(1)]], constant PassU& u [[buffer(2)]],
                      constant float4x4* bones [[buffer(3)]]) {
  float3 n = in.nrm;
  float3 p = in.pos;
  if (d.boneCount > 0) {
    float b1 = floor(in.bones / 64.0 + 0.001);
    float b0 = in.bones - 64.0 * b1;
    float w0 = clamp(fract(in.mat) / 0.998, 0.0, 1.0);
    int i0 = clamp(int(b0 + 0.5), 0, d.boneCount - 1), i1 = clamp(int(b1 + 0.5), 0, d.boneCount - 1);
    float4x4 M = bones[i0] * w0 + bones[i1] * (1.0 - w0);
    p = (M * float4(p, 1.0)).xyz;
    n = float3x3(M[0].xyz, M[1].xyz, M[2].xyz) * n;
  }
  p = deform(p, n, d.rig, d.rigH);
  float4 w = d.model * float4(p, 1.0);
  MeshOut o;
  o.world = w.xyz;
  o.nrm = float3x3(d.model[0].xyz, d.model[1].xyz, d.model[2].xyz) * n;
  o.obj = in.pos;
  o.objN = in.nrm;
  o.ao = in.ao;
  o.aux = in.aux;
  o.mat = floor(in.mat + 0.0001);
  o.position = u.viewProj * w;
  o.clip[0] = (u.clip == 1) ? (w.y + 0.002) : 1.0;
  return o;
}

// Mesh fragment shaders are compiled with and without dissolve support. Without
// it the pipeline never discards, so Apple GPUs keep hidden surface removal on.
constant bool kDissolve [[function_constant(0)]];

// dissolve helper shared by shadow/prepass/object shaders
float dissolveField(float3 o, float3 texOffset, texture3d<float> noise) {
  return noise.sample(sNoise, o * 2.1 + texOffset * 0.37).r * 0.78 +
         0.22 * (1.0 - clamp(o.y / 1.7, 0.0, 1.0));
}

fragment void shadowFS(MeshFrag in [[stage_in]], constant DrawU& d [[buffer(1)]],
                       texture3d<float> noise [[texture(5)]]) {
  if (d.dissolve > 0.0 && dissolveField(in.obj, d.texOffset, noise) < d.dissolve) discard_fragment();
}

fragment float4 prepassFS(MeshFrag in [[stage_in]], bool front [[front_facing]], constant DrawU& d [[buffer(1)]],
                          constant PassU& u [[buffer(2)]], texture3d<float> noise [[texture(5)]]) {
  if (kDissolve && d.dissolve > 0.0 && dissolveField(in.obj, d.texOffset, noise) < d.dissolve) discard_fragment();
  float3 n = normalize(float3x3(u.view[0].xyz, u.view[1].xyz, u.view[2].xyz) * normalize(in.nrm));
  if (!front) n = -n;
  return float4(n, 1.0);
}
"""#
}

// ===========================================================================
// marble, steel, iron, fragments
// ===========================================================================
extension Shaders {
  fileprivate static let mslObject = #"""
float hash13(float3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}
// GLSL's mod (floored), unlike fmod
float gmod(float x, float y) { return x - y * floor(x / y); }

// Painted medieval figures: every vertex carries one of these materials.
// The white army wears ivory and azure over bright steel; the black army
// sable and gules over blackened steel.
void medievalSurface(int id, bool black, float3 o, float3 on, float aux, float3 texOffset, float3 V,
                     texture3d<float> noise, thread float3& albedo, thread float& rough, thread float& metal,
                     thread float& wrap, thread float3& sss, thread float3& N, thread float3& emissive,
                     thread float& specOcc) {
  float n1 = noise.sample(sNoise, o * 3.1 + texOffset * 0.13).r;
  float n2 = noise.sample(sNoise, o * 11.0 + texOffset * 0.07).g;
  float n3 = noise.sample(sNoise, o * 37.0).b;
  if (id == 0) {  // stone: ashlar courses with mortar joints
    float3 an = abs(on);
    float u = an.x > an.z ? o.z : o.x;
    if (an.y > 0.8) u = o.x + o.z * 0.3;
    float h = 0.052;
    float row = floor(o.y / h);
    float uu = u / 0.095 + 0.5 * gmod(row, 2.0);
    float fy = fract(o.y / h), fu = fract(uu);
    float joint = 1.0 - smoothstep(0.0, 0.06, min(min(fy, 1.0 - fy) * h / 0.095, min(fu, 1.0 - fu)));
    if (an.y > 0.8) joint *= 0.3;
    float blockVar = hash13(float3(floor(uu), row, 7.0)) - 0.5;
    float3 base = black ? float3(0.12, 0.115, 0.11) : float3(0.70, 0.66, 0.58);
    albedo = base * (0.86 + 0.22 * n1 + 0.2 * blockVar + 0.12 * (n2 - 0.5));
    albedo = mix(albedo, black ? float3(0.07) : float3(0.48, 0.45, 0.40), joint * 0.75);
    rough = 0.82 + 0.1 * n2;
    N = normalize(N + (float3(n2, n3, n1) - 0.5) * 0.35);
    specOcc = 0.6;
  } else if (id == 1) {  // wood
    float g = noise.sample(sNoise, float3(o.x * 9.0, o.y * 1.2, o.z * 9.0) + texOffset * 0.1).r;
    float ring = abs(fract(g * 7.0) - 0.5) * 2.0;
    float3 base = black ? float3(0.055, 0.036, 0.026) : float3(0.48, 0.33, 0.19);
    albedo = base * (0.75 + 0.35 * ring + 0.15 * n2);
    rough = black ? 0.32 : 0.48;
    rough += 0.1 * n3;
  } else if (id == 2 || id == 12 || id == 13) {  // steel, mail, wrought iron
    metal = 1.0;
    if (id == 13) {
      albedo = float3(0.09, 0.085, 0.085) * (0.8 + 0.5 * n1);
      albedo = mix(albedo, float3(0.18, 0.07, 0.03), smoothstep(0.55, 0.8, n2) * 0.6);
      rough = 0.55 + 0.2 * n3;
      metal = 0.75;
    } else {
      albedo = black ? float3(0.13, 0.13, 0.145) : float3(0.64, 0.64, 0.66);
      albedo *= 0.88 + 0.2 * n1;
      rough = (black ? 0.34 : 0.2) + 0.12 * n2;
      if (id == 12) {  // chain mail: little rings
        float2 q = float2(o.x + o.z, o.y) / 0.0075;
        q.x += 0.5 * gmod(floor(q.y), 2.0);
        float2 f = fract(q) - 0.5;
        float ringD = abs(length(f) - 0.32);
        float rr = 1.0 - smoothstep(0.06, 0.16, ringD);
        albedo *= 0.35 + 0.75 * rr;
        rough = mix(0.6, rough + 0.05, rr);
        N = normalize(N + float3(f.x, 0.0, f.y) * rr * 0.6);
      }
    }
  } else if (id == 3) {  // gold
    albedo = float3(1.0, 0.74, 0.32) * (0.9 + 0.15 * n1);
    metal = 1.0;
    rough = 0.2 + 0.1 * n2;
  } else if (id == 4 || id == 5 || id == 18 || id == 20) {  // cloth, heraldic cloth, velvet, linen
    float3 base;
    if (id == 4) base = black ? float3(0.04, 0.038, 0.037) : float3(0.76, 0.73, 0.65);
    else if (id == 5) base = black ? float3(0.36, 0.05, 0.04) : float3(0.075, 0.14, 0.33);
    else if (id == 18) base = black ? float3(0.26, 0.02, 0.035) : float3(0.05, 0.07, 0.26);
    else base = float3(0.84, 0.82, 0.76);
    float weave = 0.92 + 0.08 * sin(o.x * 2600.0) * sin(o.y * 2600.0);
    albedo = base * (0.85 + 0.25 * n1) * weave;
    rough = id == 18 ? 0.7 : 0.88;
    wrap = 0.35;
    float NoV = saturate(dot(N, V));
    emissive += base * pow(1.0 - NoV, 3.0) * (id == 18 ? 0.5 : 0.18);  // fabric sheen
    specOcc = 0.5;
  } else if (id == 6) {  // skin
    albedo = float3(0.60, 0.40, 0.30) * (0.92 + 0.12 * n1);
    rough = 0.6;
    wrap = 0.45;
    sss = float3(1.0, 0.4, 0.25) * 1.0;
  } else if (id == 7) {  // hair: a colour per figure
    float h = hash13(floor(texOffset * 3.0) + 0.5);
    float3 c = h < 0.3 ? float3(0.12, 0.07, 0.035) : (h < 0.55 ? float3(0.32, 0.17, 0.07) :
               (h < 0.75 ? float3(0.55, 0.42, 0.22) : float3(0.6, 0.58, 0.55)));
    float strand = noise.sample(sNoise, float3(o.x * 60.0, o.y * 8.0, o.z * 60.0)).r;
    albedo = c * (0.7 + 0.5 * strand);
    rough = 0.55;
  } else if (id == 8) {  // horse coat
    if (black) {
      albedo = float3(0.035, 0.03, 0.028) * (0.8 + 0.4 * n1);
      rough = 0.38;
    } else {
      float dap = smoothstep(0.45, 0.62, noise.sample(sNoise, o * 7.0 + texOffset * 0.1).g);
      albedo = mix(float3(0.66, 0.64, 0.61), float3(0.42, 0.41, 0.40), dap * 0.6) * (0.92 + 0.1 * n2);
      rough = 0.5;
    }
    wrap = 0.2;
  } else if (id == 9) {  // leather
    albedo = float3(0.21, 0.115, 0.06) * (0.75 + 0.4 * n1);
    rough = 0.5 + 0.15 * n2;
  } else if (id == 10) {  // ermine
    float2 q = float2(o.x + o.z, o.y) / 0.04;
    q.x += 0.5 * gmod(floor(q.y), 2.0);
    float spot = 1.0 - smoothstep(0.08, 0.14, length(fract(q) - 0.5) - 0.0);
    albedo = mix(float3(0.9, 0.89, 0.85) * (0.9 + 0.1 * n2), float3(0.02), spot);
    rough = 0.95;
    wrap = 0.4;
  } else if (id == 11) {  // gems
    albedo = black ? float3(0.6, 0.02, 0.04) : float3(0.05, 0.12, 0.7);
    rough = 0.06;
    emissive += albedo * 0.25;
  } else if (id == 14) {  // dragon scales
    float2 q = float2(atan2(o.x, o.z) * 0.08 + o.x * 0.3, o.y) / 0.022;
    q.x += 0.5 * gmod(floor(q.y), 2.0);
    float2 f = fract(q) - float2(0.5, 0.2);
    float sc = smoothstep(0.55, 0.2, length(f * float2(1.0, 1.4)));
    float3 base = black ? float3(0.09, 0.022, 0.02) : float3(0.42, 0.30, 0.08);
    albedo = base * (0.55 + 0.6 * sc) * (0.85 + 0.3 * n1);
    metal = black ? 0.2 : 0.55;
    rough = 0.35 + 0.2 * (1.0 - sc);
    N = normalize(N + float3(f.x, 0.0, f.y) * 0.5 * sc);
  } else if (id == 15) {  // wing membrane
    albedo = (black ? float3(0.22, 0.03, 0.025) : float3(0.5, 0.27, 0.1)) * (0.8 + 0.3 * n1);
    rough = 0.6;
    wrap = 0.6;
    sss = black ? float3(1.0, 0.2, 0.1) * 2.0 : float3(1.0, 0.6, 0.2) * 2.0;
  } else if (id == 16) {  // horn, teeth, claws
    albedo = float3(0.82, 0.76, 0.62) * (0.85 + 0.2 * n1);
    rough = 0.38;
  } else if (id == 17) {  // dragon eye
    albedo = float3(0.05);
    rough = 0.1;
    emissive += float3(4.0, 2.2, 0.4);
  } else if (id == 19) {  // shadowy interior
    albedo = float3(0.012, 0.01, 0.009);
    rough = 0.95;
    specOcc = 0.0;
  } else if (id == 22) {  // mane and tail
    float strand = noise.sample(sNoise, float3(o.x * 70.0, o.y * 6.0, o.z * 70.0)).r;
    albedo = (black ? float3(0.025, 0.022, 0.02) : float3(0.72, 0.70, 0.66)) * (0.65 + 0.6 * strand);
    rough = 0.5;
  } else if (id == 21) {  // slate roof: overlapping shingles
    float row = floor(o.y / 0.022);
    float u = atan2(o.x, o.z) * 0.12 / 0.018 + 0.5 * gmod(row, 2.0);
    float edge = smoothstep(0.75, 1.0, fract(o.y / 0.022)) +
                 0.6 * (1.0 - smoothstep(0.0, 0.08, abs(fract(u) - 0.5) - 0.42));
    float3 base = black ? float3(0.07, 0.07, 0.08) : float3(0.22, 0.24, 0.27);
    albedo = base * (0.8 + 0.3 * hash13(float3(floor(u), row, 3.0))) * (1.0 - 0.45 * saturate(edge));
    rough = 0.55;
  }
  // fresh fracture faces: the plaster inside the paint
  if (aux > 0.0) {
    float3 broken = black ? float3(0.11, 0.105, 0.1) : float3(0.84, 0.82, 0.78);
    albedo = mix(albedo, broken * (0.85 + 0.3 * n3), aux);
    rough = mix(rough, 0.85, aux);
    metal = mix(metal, 0.0, aux);
    sss *= 1.0 - aux;
  }
}

fragment float4 objectFS(MeshFrag in [[stage_in]], bool front [[front_facing]], LIGHT_BINDINGS,
                         constant DrawU& d [[buffer(1)]], texture3d<float> noise [[texture(5)]]) {
  float2 fragCoord = in.position.xy;
  float3 N = normalize(in.nrm);
  if (!front) N = -N;
  float3 V = normalize(u.camPos - in.world);
  float3 emissive = float3(0.0);
  float specOcc = 1.0;
  if (kDissolve && d.dissolve > 0.0) {
    float edge = dissolveField(in.obj, d.texOffset, noise) - d.dissolve;
    if (edge < 0.0) discard_fragment();
    emissive += d.dissolveColor * (1.0 - smoothstep(0.0, 0.06, edge)) * 7.0;
  }
  float3 albedo = float3(0.5);
  float rough = 0.5, metalness = 0.0, wrap = 0.0;
  float3 sss = float3(0.0);
  if (d.mat <= 1) {
    bool black = d.mat == 1;
    // domain-warped noise gives wispy, branching veins like real stone
    float3 q = in.obj * 1.05 + d.texOffset;
    float3 wv = float3(noise.sample(sNoise, q * 0.21).r, noise.sample(sNoise, q * 0.21 + float3(0.31, 0.57, 0.11)).g,
                       noise.sample(sNoise, q * 0.21 + float3(0.73, 0.19, 0.43)).r) - 0.5;
    float f = noise.sample(sNoise, q * 0.33 + wv * 0.55).r;
    float f2 = noise.sample(sNoise, q * 0.9 + wv * 0.9 + float3(0.5)).g;
    float band = abs(fract(f * 2.6 + 0.1 * f2) - 0.5) * 2.0;
    float band2 = abs(fract(f2 * 3.5) - 0.5) * 2.0;
    float vein = 1.0 - smoothstep(0.0, 0.07, band);
    float halo = 1.0 - smoothstep(0.0, 0.35, band);
    float hair = (1.0 - smoothstep(0.0, 0.035, band2)) * smoothstep(0.45, 0.7, f);
    float cloud = noise.sample(sNoise, q * 0.55 + float3(0.2)).g;
    float n3 = noise.sample(sNoise, q * 2.4 + float3(0.71)).b;
    if (!black) {
      float3 base = float3(0.90, 0.88, 0.84) * (0.93 + 0.09 * cloud);
      albedo = mix(base, float3(0.56, 0.57, 0.60), saturate(vein * 0.55 + halo * 0.10 + hair * 0.30));
      wrap = 0.5;
      sss = float3(1.0, 0.78, 0.58) * 1.6;
    } else {
      float3 base = float3(0.011, 0.011, 0.013) * (0.8 + 0.4 * cloud);
      albedo = mix(base, float3(0.62, 0.60, 0.56), saturate(vein * 0.85 + halo * 0.04 + hair * 0.55));
    }
    rough = mix(0.05, 0.11, n3) + vein * 0.04;
    float interior = in.aux;
    if (interior > 0.0) {
      float g = noise.sample(sNoise, in.obj * 9.0).b;
      float3 broken = black ? float3(0.07, 0.07, 0.075) * (0.7 + 0.6 * g) : float3(0.86, 0.85, 0.82) * (0.85 + 0.3 * g);
      albedo = mix(albedo, broken, interior);
      rough = mix(rough, 0.8, interior);
      N = normalize(N + interior * 0.4 * (noise.sample(sNoise, in.obj * 12.0).rgb - 0.5));
      sss *= 1.0 - interior;
    }
  } else if (d.mat == 2) {
    float m = in.aux;
    if (m < 0.5) {
      albedo = float3(0.66, 0.67, 0.70);
      metalness = 1.0;
      rough = 0.13 + 0.07 * noise.sample(sNoise, in.obj * float3(30.0, 1.5, 30.0)).b;
    } else if (m < 1.5) {
      albedo = float3(1.0, 0.72, 0.30);
      metalness = 1.0;
      rough = 0.22;
    } else {
      albedo = float3(0.06, 0.03, 0.02);
      rough = 0.55;
    }
  } else if (d.mat == 3) {
    float g = noise.sample(sNoise, in.obj * 6.0).r;
    albedo = float3(0.06, 0.06, 0.065) * (0.7 + 0.6 * g);
    metalness = 0.85;
    rough = 0.42 + 0.2 * g;
    emissive += float3(1.0, 0.33, 0.07) * d.heat * (0.6 + 0.8 * g) * 4.0;
  } else {
    medievalSurface(int(in.mat + 0.5), d.team == 1, in.obj, normalize(in.objN), in.aux, d.texOffset, V, noise, albedo,
                    rough, metalness, wrap, sss, N, emissive, specOcc);
    if (d.stone > 0.0) {  // turning to salt: pale, crystalline, glittering
      float g = noise.sample(sNoise, in.obj * 18.0 + d.texOffset * 0.3).r;
      float front = smoothstep(0.0, 0.15, d.stone * 1.3 - (1.0 - saturate(in.obj.y / 1.3)) * 0.3 - g * 0.15);
      albedo = mix(albedo, float3(0.88, 0.87, 0.84) * (0.85 + 0.25 * g), front);
      rough = mix(rough, 0.75, front);
      metalness = mix(metalness, 0.0, front);
      wrap = mix(wrap, 0.3, front);
      emissive += float3(1.0, 0.95, 0.85) * front * pow(g, 12.0) * 6.0 * d.stone;
    }
    if (d.heat > 0.0) {  // scorched by dragon fire
      float g = noise.sample(sNoise, in.obj * 5.0 + d.texOffset * 0.2).r;
      albedo *= 1.0 - 0.85 * saturate(d.heat * 1.5);
      emissive += float3(1.0, 0.3, 0.05) * saturate(d.heat * 1.4 - 0.3) * (0.4 + 1.2 * g) * 5.0;
    }
  }
  if (d.crack > 0.0) {
    float c = noise.sample(sNoise, in.obj * 1.4 + d.texOffset * 0.2).a;
    float c2 = noise.sample(sNoise, in.obj * 3.1 + d.texOffset * 0.3).a;
    float w = d.crack * 0.11;
    float line = (1.0 - smoothstep(w * 0.45, w, c)) +
                 0.7 * (1.0 - smoothstep(w * 0.25, w * 0.55, c2)) * step(0.35, d.crack);
    line = saturate(line);
    albedo *= 1.0 - 0.85 * line;
    emissive += d.crackColor * line * (2.0 + 14.0 * d.crack);
  }
  float ao = in.ao;
  float h = abs(in.world.y);  // debris falling past the board is not grounded
  ao *= 1.0 - d.groundAO * 0.5 * exp(-h / 0.07) * saturate(0.6 - 0.6 * N.y);
  float NoV = saturate(dot(N, V));
  if (d.glow.a > 0.0) emissive += d.glow.rgb * d.glow.a * (pow(1.0 - NoV, 2.5) * 1.7 + 0.10);

  float thick = 10.0;
  float sh = 1.0;
  if (u.shadowsOn == 1) sh = shadowAt(in.world, N, thick, LIGHT_ARGS);
  Surface s;
  s.P = in.world;
  s.N = N;
  s.V = V;
  s.albedo = albedo;
  s.rough = clamp(rough, 0.03, 1.0);
  s.metal = metalness;
  s.ao = ao;
  s.wrap = wrap;
  s.sss = sss;
  s.specOcc = specOcc;
  float3 col = shadeSurface(s, sh, thick, LIGHT_ARGS) + emissive;
  return float4(col, 1.0);
}
"""#
}

// ===========================================================================
// lacquered inlaid wood board
// ===========================================================================
extension Shaders {
  fileprivate static let mslBoard = #"""
fragment float4 boardFS(MeshFrag in [[stage_in]], LIGHT_BINDINGS,
                        constant BoardU& b [[buffer(4)]],
                        constant float4* occ [[buffer(5)]],      // x, z, radius, strength
                        constant float4* rings [[buffer(6)]],    // [0..5] x, z, radius, intensity; [6..11] rgb, width
                        constant uchar4* state [[buffer(7)]],    // 8x8: r move target, g selected, b last move, a hover
                        texture3d<float> noise [[texture(5)]],
                        texture2d<float> boardAlbedo [[texture(6)]],
                        texture2d<float> boardDetail [[texture(7)]],
                        texture2d<float> reflection [[texture(8)]]) {
  float2 fragCoord = in.position.xy;
  float3 vWorld = in.world;
  float3 Ng = normalize(in.nrm);
  float3 V = normalize(u.camPos - vWorld);
  bool top = Ng.y > 0.985 && vWorld.y > -0.003;
  bool under = Ng.y < -0.985;  // the board floats, so its underside is on show
  float2 xz = vWorld.xz;
  float m = max(abs(xz.x), abs(xz.y));
  // underside rim: sample the plain outer frame; sides: project the frame inward with depth.
  // Both stay clear of the gold lettering.
  if (under) xz *= (u.boardHalf - 0.06) / max(m, 1e-3);
  else if (!top) xz -= normalize(Ng.xz + float2(1e-5)) * min(-vWorld.y * 0.55 + 0.03, 0.15);
  float2 uv = (xz + u.boardHalf) / (2.0 * u.boardHalf);
  // seen edge-on the sides would pick very blurry mips that smear the lettering in: cap the footprint
  float2 gx = dfdx(uv), gy = dfdy(uv);
  if (!top) {
    float lim = 0.02 / (2.0 * u.boardHalf);
    gx *= min(1.0, lim / max(length(gx), 1e-8));
    gy *= min(1.0, lim / max(length(gy), 1e-8));
  }
  float4 A = boardAlbedo.sample(sBoard, uv, gradient2d(gx, gy));
  float4 D = boardDetail.sample(sBoard, uv, gradient2d(gx, gy));
  // all implicit-derivative fetches happen here, before any fragment can leave early
  float smudge = noise.sample(sNoise, float3(vWorld.xz * 0.31, 0.37)).g;
  float micro = noise.sample(sNoise, float3(vWorld.xz * 3.1, 0.71)).b;
  float2 peel = (float2(noise.sample(sNoise, float3(vWorld.xz * 6.0, 0.13)).r,
                        noise.sample(sNoise, float3(vWorld.xz * 6.0, 0.53)).g) - 0.5) * 0.006;
  float fib = noise.sample(sNoise, float3(vWorld.xz * 11.0, 0.21)).r;
  float mottle = noise.sample(sNoise, float3(vWorld.xz * 0.7, 0.63)).g;

  if (under) {
    // a walnut rim framing a green baize panel
    float rim = smoothstep(u.boardHalf - 0.235, u.boardHalf - 0.225, m);
    float groove = 1.0 - smoothstep(0.0, 0.006, abs(m - (u.boardHalf - 0.23)));
    float3 felt = float3(0.020, 0.072, 0.040) * (0.75 + 0.4 * fib) * (0.8 + 0.4 * mottle);
    Surface s;
    s.P = vWorld;
    s.N = Ng;
    s.V = V;
    s.albedo = mix(felt, A.rgb, rim) * (1.0 - 0.7 * groove);
    s.rough = mix(0.95, A.a, rim);
    s.metal = 0.0;
    s.ao = 1.0;
    s.wrap = 0.0;
    s.sss = float3(0.0);
    s.specOcc = mix(0.15, 0.8, rim);
    float3 col = shadeSurface(s, 1.0, 10.0, LIGHT_ARGS);
    // the key light never reaches the underside; a soft bounce from the room keeps it readable
    col += s.albedo * float3(1.0, 0.86, 0.70) * (0.55 + 0.35 * smoothstep(0.0, u.boardHalf, m));
    return float4(col, 1.0);
  }
  float3 albedo = A.rgb;
  float rough = A.a;
  float metalness = D.b;
  float cavity = D.a;
  float3 N = Ng;
  if (top) {
    float2 nxz = D.rg * 2.0 - 1.0;
    N = normalize(float3(nxz.x, sqrt(max(0.0, 1.0 - dot(nxz, nxz))), nxz.y));
  }
  float ao = mix(1.0, cavity, 0.8);
  if (top) {
    for (int i = 0; i < 32; ++i) {
      if (i >= b.numOcc) break;
      float d = length(xz - occ[i].xy);
      float r = occ[i].z;
      float o = 1.0 - smoothstep(r * 0.55, r * 2.0, d);
      ao *= 1.0 - 0.6 * o * occ[i].w;
    }
  }
  float thick = 10.0;
  float sh = 1.0;
  if (u.shadowsOn == 1) sh = shadowAt(vWorld, Ng, thick, LIGHT_ARGS);
  Surface s;
  s.P = vWorld;
  s.N = N;
  s.V = V;
  s.albedo = albedo;
  s.rough = rough;
  s.metal = metalness;
  s.ao = ao;
  s.wrap = 0.0;
  s.sss = float3(0.0);
  s.specOcc = mix(0.25, 1.0, metalness);
  float3 base = shadeSurface(s, sh, thick, LIGHT_ARGS);

  // --- lacquer clear coat with planar reflections (with faint smudges / orange peel)
  float ccRough = 0.032 + 0.07 * smoothstep(0.5, 0.85, smudge) + 0.012 * micro;
  float NoV = max(dot(Ng, V), 1e-4);
  float Fc = 0.04 + 0.96 * pow(1.0 - NoV, 5.0);
  float3 Rv = reflect(-V, Ng);
  float3 env = prefilter.sample(sTrilinear, Rv, level(ccRough * u.prefilterMaxLod)).rgb * u.ambient;
  env *= mix(1.0, sh, smoothstep(0.72, 0.97, dot(Rv, u.lightDir)) * 0.92);
  float3 cc = env;
  if (top && b.reflectOn == 1) {
    // screen-space uv runs top-down here, so the y distortion sign is flipped vs GL
    float2 suv = fragCoord * u.invScreen + (N.xz - Ng.xz) * float2(0.03, 0.03) + peel;
    float4 r = reflection.sample(sTrilinear, suv, level(b.reflectLod + ccRough * 14.0 - 0.45));
    cc = r.rgb + (1.0 - r.a) * env;
  }
  // point lights glint in the lacquer
  for (int i = 0; i < 8; ++i) {
    if (i >= u.numLights) break;
    float4 lpos = lights[i];
    float3 Lv = lpos.xyz - vWorld;
    float d2 = dot(Lv, Lv);
    float3 Ld = Lv * rsqrt(d2);
    float fall = saturate(1.0 - pow(sqrt(d2) / lpos.w, 4.0));
    float3 H = normalize(V + Ld);
    float nl = saturate(dot(Ng, Ld));
    float a = max(ccRough, 0.04);
    cc += D_GGX(saturate(dot(Ng, H)), a) * V_Smith(NoV, nl, a) * lights[8 + i].rgb * fall * fall / (d2 + 0.04) * nl;
  }
  float3 col = base * (1.0 - Fc) + cc * Fc * mix(1.0, ao, 0.6);

  // --- interactive markings (projected light on the board)
  if (top && abs(vWorld.x) < 4.0 && abs(vWorld.z) < 4.0) {
    float2 sqf = float2(vWorld.x + 4.0, 4.0 - vWorld.z);
    float2 cell = clamp(floor(sqf), float2(0.0), float2(7.0));
    float2 lp = fract(sqf) - 0.5;
    float4 st = float4(state[int(cell.y) * 8 + int(cell.x)]) / 255.0;
    float dc = length(lp);
    float bd = max(abs(lp.x), abs(lp.y));
    float3 em = float3(0.0);
    em += float3(1.0, 0.70, 0.32) * st.b * 0.06;
    em += float3(0.85, 0.92, 1.0) * st.a * (1.0 - smoothstep(0.0, 0.022, abs(bd - 0.462))) * 0.8;
    float selB = 1.0 - smoothstep(0.0, 0.028, abs(bd - 0.455));
    em += float3(1.0, 0.80, 0.42) * st.g * (selB * 2.4 + 0.10 * (1.0 - smoothstep(0.3, 0.5, bd)));
    float pulse = 0.82 + 0.18 * sin(u.time * 4.0);
    if (st.r > 0.75) em += float3(1.0, 0.78, 0.42) * (1.0 - smoothstep(0.075, 0.12, dc)) * 1.7 * pulse;
    else if (st.r > 0.25) em += float3(1.0, 0.30, 0.12) * (1.0 - smoothstep(0.0, 0.032, abs(dc - 0.41))) * 2.6 * pulse;
    col += em;
  }
  if (top) {
    for (int i = 0; i < 6; ++i) {
      if (i >= b.numRings) break;
      float4 rg = rings[i], rc = rings[6 + i];
      float d = length(vWorld.xz - rg.xy);
      float w = rc.w;
      float ring = 1.0 - smoothstep(0.0, w, abs(d - rg.z));
      float inner = (1.0 - smoothstep(0.0, rg.z + 0.01, d)) * 0.25;
      col += rc.rgb * rg.w * (ring + inner * ring * 0.0 + inner * 0.3);
    }
  }
  if (top && b.check.w > 0.0) {
    float d = length(vWorld.xz - b.check.xy);
    col += float3(1.0, 0.10, 0.04) * b.check.w * (1.0 - smoothstep(0.12, 0.6, d)) * (0.7 + 0.3 * sin(u.time * 7.0)) * 1.6;
  }
  return float4(col, 1.0);
}
"""#
}

// ===========================================================================
// fullscreen triangle + background
// ===========================================================================
extension Shaders {
  fileprivate static let mslFullscreen = #"""
struct FsOut {
  float4 position [[position]];
  float2 uv;  // position.xy / target size: top-left origin, matches texture rows
};
vertex FsOut fullscreenVS(uint vid [[vertex_id]]) {
  float2 p = float2(float((vid << 1) & 2), float(vid & 2));
  FsOut o;
  o.uv = float2(p.x, 1.0 - p.y);
  o.position = float4(p * 2.0 - 1.0, 1.0, 1.0);
  return o;
}

struct BackgroundU {
  float4x4 invViewProj;
  float3 camPos;
  float bgLod;
  float bgIntensity;
};
fragment float4 backgroundFS(FsOut in [[stage_in]], constant BackgroundU& u [[buffer(0)]],
                             texturecube<float> envMap [[texture(0)]]) {
  // uv -> NDC flips y (NDC is y-up, uv runs top-down); z = 1 is the far plane
  float4 w = u.invViewProj * float4(in.uv.x * 2.0 - 1.0, 1.0 - in.uv.y * 2.0, 1.0, 1.0);
  float3 dir = normalize(w.xyz / w.w - u.camPos);
  float3 c = envMap.sample(sTrilinear, dir, level(u.bgLod)).rgb;
  // keep very bright emitters from dominating the backdrop
  c = c / (1.0 + luma(c) * 0.6);
  return float4(c * u.bgIntensity, 1.0);
}
"""#
}

// ===========================================================================
// image based lighting generation (run once at startup)
// ===========================================================================
extension Shaders {
  fileprivate static let mslIBL = #"""
struct FaceU {
  float3 fwd;
  float3 right;
  float3 up;
  float3 keyDir;
  int includeKey;
  float roughness;
  float envRes;
};
// direction through the centre of this cube face texel (GL face basis table)
float3 faceDir(constant FaceU& u, float2 uv) {
  float2 ndc = uv * 2.0 - 1.0;
  return normalize(u.fwd + ndc.x * u.right + ndc.y * u.up);
}

float panel(float3 d, float3 c, float3 up, float2 hs, float soft) {
  float dc = dot(d, c);
  if (dc <= 0.0) return 0.0;
  float3 r = normalize(cross(up, c));
  float3 u = cross(c, r);
  float2 q = float2(dot(d, r), dot(d, u)) / dc;
  float2 e = abs(q) - hs;
  float dist = length(max(e, 0.0)) + min(max(e.x, e.y), 0.0);
  return 1.0 - smoothstep(-soft, soft, dist);
}

float3 room(float3 d) {
  float y = d.y;
  float az = atan2(d.z, d.x);
  float column = floor(az / (2.0 * PI) * 30.0);
  float rnd = fract(sin(column * 12.9898) * 43758.5453);
  float shelf = fract(y * 7.0 + rnd * 0.3);
  float books = fract(sin(floor(az * 160.0) * 78.233) * 43758.5453);
  float3 wall = float3(0.020, 0.014, 0.010) * (0.65 + 0.7 * rnd);
  wall *= mix(0.55, 1.0, smoothstep(0.08, 0.16, shelf));
  wall *= 0.8 + 0.4 * books;
  float3 ceilc = float3(0.028, 0.025, 0.022);
  float3 floorc = float3(0.009, 0.006, 0.0045);
  float3 c = mix(floorc, wall, smoothstep(-0.35, -0.05, y));
  c = mix(c, ceilc, smoothstep(0.50, 0.80, y));
  return c;
}

fragment float4 envGenFS(FsOut in [[stage_in]], constant FaceU& u [[buffer(0)]]) {
  float3 d = faceDir(u, in.uv);
  float3 up = float3(0.0, 1.0, 0.0);
  float3 c = room(d);
  // cool fill from the front-right, warm rim strip behind-right, soft overhead
  c += float3(0.78, 0.87, 1.0) * 2.0 * panel(d, normalize(float3(0.85, 0.45, 0.55)), up, float2(0.55, 0.32), 0.25);
  c += float3(1.0, 0.94, 0.86) * 4.5 * panel(d, normalize(float3(0.55, 0.40, -0.85)), up, float2(0.8, 0.045), 0.04);
  c += float3(1.0, 0.96, 0.90) * 1.4 * panel(d, normalize(float3(0.05, 1.0, 0.12)), float3(0.0, 0.0, 1.0), float2(0.4, 0.4), 0.2);
  c += float3(1.0, 0.62, 0.32) * 6.0 * smoothstep(0.9975, 0.9995, dot(d, normalize(float3(-1.0, 0.16, -0.55))));
  c += float3(1.0, 0.62, 0.32) * 6.0 * smoothstep(0.9975, 0.9995, dot(d, normalize(float3(0.95, 0.20, -0.65))));
  c += float3(0.33, 0.43, 0.70) * 1.0 * panel(d, normalize(float3(-1.0, 0.35, -0.30)), up, float2(0.22, 0.34), 0.04);
  if (u.includeKey == 1) c += float3(1.0, 0.94, 0.86) * 34.0 * panel(d, u.keyDir, up, float2(0.13, 0.10), 0.03);
  return float4(c, 1.0);
}

fragment float4 irradianceFS(FsOut in [[stage_in]], constant FaceU& u [[buffer(0)]],
                             texturecube<float> envMap [[texture(0)]]) {
  float3 N = faceDir(u, in.uv);
  float3 up = abs(N.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
  float3 right = normalize(cross(up, N));
  up = cross(N, right);
  float3 acc = float3(0.0);
  float n = 0.0;
  for (float phi = 0.0; phi < 6.2831853; phi += 0.05) {
    for (float th = 0.0; th < 1.5707963; th += 0.05) {
      float3 t = float3(sin(th) * cos(phi), sin(th) * sin(phi), cos(th));
      float3 sd = t.x * right + t.y * up + t.z * N;
      acc += envMap.sample(sTrilinear, sd, level(4.0)).rgb * cos(th) * sin(th);
      n += 1.0;
    }
  }
  return float4(PI * acc / n, 1.0);
}

float vdc(uint bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10;
}
float2 hammersley(uint i, uint n) { return float2(float(i) / float(n), vdc(i)); }
float3 importanceGGX(float2 Xi, float3 N, float r) {
  float a = r * r;
  float phi = 2.0 * PI * Xi.x;
  float ct = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
  float st = sqrt(1.0 - ct * ct);
  float3 H = float3(cos(phi) * st, sin(phi) * st, ct);
  float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
  float3 tx = normalize(cross(up, N));
  float3 ty = cross(N, tx);
  return normalize(tx * H.x + ty * H.y + N * H.z);
}

fragment float4 prefilterFS(FsOut in [[stage_in]], constant FaceU& u [[buffer(0)]],
                            texturecube<float> envMap [[texture(0)]]) {
  float3 N = faceDir(u, in.uv);
  float3 V = N;
  const uint SAMPLES = 384u;
  float3 acc = float3(0.0);
  float wsum = 0.0;
  float a = u.roughness * u.roughness;
  for (uint i = 0u; i < SAMPLES; ++i) {
    float2 Xi = hammersley(i, SAMPLES);
    float3 H = importanceGGX(Xi, N, max(u.roughness, 0.02));
    float3 L = normalize(2.0 * dot(V, H) * H - V);
    float NoL = dot(N, L);
    if (NoL > 0.0) {
      float NoH = max(dot(N, H), 0.0);
      float a2 = a * a;
      float dd = NoH * NoH * (a2 - 1.0) + 1.0;
      float D = a2 / (PI * dd * dd + 1e-7);
      float pdf = D * 0.25 + 1e-4;
      float saTexel = 4.0 * PI / (6.0 * u.envRes * u.envRes);
      float saSample = 1.0 / (float(SAMPLES) * pdf + 1e-4);
      float mip = u.roughness < 0.02 ? 0.0 : 0.5 * log2(saSample / saTexel) + 1.0;
      acc += envMap.sample(sTrilinear, L, level(mip)).rgb * NoL;
      wsum += NoL;
    }
  }
  return float4(acc / max(wsum, 1e-4), 1.0);
}

float gSchlick(float NoV, float r) {
  float k = (r * r) / 2.0;
  return NoV / (NoV * (1.0 - k) + k);
}
fragment float4 brdfLutFS(FsOut in [[stage_in]]) {
  float NoV = max(in.uv.x, 1e-3);
  float r = in.uv.y;
  float3 V = float3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
  float3 N = float3(0.0, 0.0, 1.0);
  float A = 0.0, B = 0.0;
  const uint SAMPLES = 512u;
  for (uint i = 0u; i < SAMPLES; ++i) {
    float2 Xi = hammersley(i, SAMPLES);
    float3 H = importanceGGX(Xi, N, max(r, 0.01));
    float3 L = normalize(2.0 * dot(V, H) * H - V);
    float NoL = max(L.z, 0.0);
    float NoH = max(H.z, 0.0);
    float VoH = max(dot(V, H), 0.0);
    if (NoL > 0.0) {
      float G = gSchlick(NoV, r) * gSchlick(NoL, r);
      float Gv = G * VoH / (NoH * NoV);
      float Fc = pow(1.0 - VoH, 5.0);
      A += (1.0 - Fc) * Gv;
      B += Fc * Gv;
    }
  }
  return float4(A / float(SAMPLES), B / float(SAMPLES), 0.0, 1.0);
}
"""#
}

// ===========================================================================
// SSAO, bloom, depth of field, tonemap composite
// ===========================================================================
extension Shaders {
  fileprivate static let mslPost = #"""
struct SsaoU {
  float4x4 proj;
  float4x4 invProj;
  float radius;
};
// view-space position from the prepass depth (uv -> NDC flips y; depth is already NDC z)
float3 viewPos(depth2d<float> depthTex, float2 uv, float4x4 invProj) {
  float d = depthTex.sample(sNearest, uv);
  float4 p = invProj * float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0);
  return p.xyz / p.w;
}
fragment float4 ssaoFS(FsOut in [[stage_in]], constant SsaoU& u [[buffer(0)]], constant float4* ssaoKernel [[buffer(1)]],
                       depth2d<float> depthTex [[texture(0)]], texture2d<float> normalTex [[texture(1)]]) {
  float d = depthTex.sample(sNearest, in.uv);
  if (d >= 0.99999) return float4(1.0);
  float3 P = viewPos(depthTex, in.uv, u.invProj);
  float3 N = normalize(normalTex.sample(sNearest, in.uv).xyz);
  float a = ign(in.position.xy) * 6.2831853;
  float3 rv = float3(cos(a), sin(a), 0.0);
  float3 T = normalize(rv - N * dot(rv, N));
  float3 B = cross(N, T);
  float3x3 TBN = float3x3(T, B, N);
  float occ = 0.0;
  float radius = u.radius * clamp(-P.z / 10.0, 0.6, 1.6);
  for (int i = 0; i < 16; ++i) {
    float3 sp = P + TBN * ssaoKernel[i].xyz * radius;
    float4 o = u.proj * float4(sp, 1.0);
    float2 suv = float2(o.x / o.w * 0.5 + 0.5, 0.5 - o.y / o.w * 0.5);
    if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
    float sz = viewPos(depthTex, suv, u.invProj).z;
    float range = smoothstep(0.0, 1.0, radius / max(abs(P.z - sz), 1e-4));
    occ += (sz >= sp.z + 0.012 ? 1.0 : 0.0) * range;
  }
  float ao = 1.0 - occ / 16.0;
  return float4(pow(ao, 1.5));
}

struct SsaoBlurU {
  float2 texel;
  float2 nearFar;
};
fragment float4 ssaoBlurFS(FsOut in [[stage_in]], constant SsaoBlurU& u [[buffer(0)]],
                           texture2d<float> aoTex [[texture(0)]], depth2d<float> depthTex [[texture(1)]]) {
  float z0 = linZ(depthTex.sample(sNearest, in.uv), u.nearFar);
  float acc = 0.0, w = 0.0;
  for (int y = -2; y <= 2; ++y)
    for (int x = -2; x <= 2; ++x) {
      float2 uv = in.uv + float2(float(x), float(y)) * u.texel;
      float z = linZ(depthTex.sample(sNearest, uv), u.nearFar);
      float wt = exp(-abs(z - z0) * 18.0 / max(z0, 0.1) * 4.0) * exp(-float(x * x + y * y) * 0.15);
      acc += aoTex.sample(sLinear, uv).r * wt;
      w += wt;
    }
  return float4(acc / max(w, 1e-4));
}

// ---- bloom ----
struct BloomU {
  float2 texel;  // down: source texel size; up: tap radius
  int first;
};
float kw(float3 c) { return 1.0 / (1.0 + luma(c)); }
fragment float4 bloomDownFS(FsOut in [[stage_in]], constant BloomU& u [[buffer(0)]], texture2d<float> src [[texture(0)]]) {
  #define S(ox, oy) src.sample(sLinear, in.uv + float2(ox, oy) * u.texel).rgb
  float3 a = S(-2.0, 2.0), b = S(0.0, 2.0), c = S(2.0, 2.0);
  float3 d = S(-2.0, 0.0), e = S(0.0, 0.0), f = S(2.0, 0.0);
  float3 g = S(-2.0, -2.0), h = S(0.0, -2.0), i = S(2.0, -2.0);
  float3 j = S(-1.0, 1.0), k = S(1.0, 1.0), l = S(-1.0, -1.0), m = S(1.0, -1.0);
  #undef S
  float3 r;
  if (u.first == 1) {
    float3 g0 = (a + b + d + e) * 0.25, g1 = (b + c + e + f) * 0.25, g2 = (d + e + g + h) * 0.25;
    float3 g3 = (e + f + h + i) * 0.25, g4 = (j + k + l + m) * 0.25;
    float w0 = kw(g0) * 0.125, w1 = kw(g1) * 0.125, w2 = kw(g2) * 0.125, w3 = kw(g3) * 0.125, w4 = kw(g4) * 0.5;
    r = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);
  } else {
    r = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
  }
  return float4(max(r, float3(0.0)), 1.0);
}

fragment float4 bloomUpFS(FsOut in [[stage_in]], constant BloomU& u [[buffer(0)]], texture2d<float> src [[texture(0)]]) {
  float2 r = u.texel;
  float2 uv = in.uv;
  float3 c = src.sample(sLinear, uv).rgb * 4.0;
  c += (src.sample(sLinear, uv + float2(-r.x, 0.0)).rgb + src.sample(sLinear, uv + float2(r.x, 0.0)).rgb +
        src.sample(sLinear, uv + float2(0.0, -r.y)).rgb + src.sample(sLinear, uv + float2(0.0, r.y)).rgb) * 2.0;
  c += src.sample(sLinear, uv + float2(-r.x, -r.y)).rgb + src.sample(sLinear, uv + float2(r.x, -r.y)).rgb +
       src.sample(sLinear, uv + float2(-r.x, r.y)).rgb + src.sample(sLinear, uv + float2(r.x, r.y)).rgb;
  return float4(c / 16.0, 1.0);
}

// ---- depth of field ----
struct DofCocU {
  float2 nearFar;
  float2 srcTexel;
  float focus;
  float aperture;
  float maxCoc;
};
fragment float4 dofCocFS(FsOut in [[stage_in]], constant DofCocU& u [[buffer(0)]],
                         texture2d<float> color [[texture(0)]], depth2d<float> depthTex [[texture(1)]]) {
  float3 c = float3(0.0);
  float coc = 0.0;
  for (int i = 0; i < 4; ++i) {
    float2 o = float2(float(i & 1) - 0.5, float(i >> 1) - 0.5) * u.srcTexel;
    c += color.sample(sLinear, in.uv + o).rgb;
    float z = linZ(depthTex.sample(sNearest, in.uv + o), u.nearFar);
    coc += clamp(u.aperture * (z - u.focus) / max(z, 0.01), -u.maxCoc, u.maxCoc);
  }
  return float4(c * 0.25, coc * 0.25);
}

struct DofBokehU {
  float2 texel;
  float maxCoc;
};
fragment float4 dofBokehFS(FsOut in [[stage_in]], constant DofBokehU& u [[buffer(0)]], texture2d<float> src [[texture(0)]]) {
  float4 c0 = src.sample(sLinear, in.uv);
  float r0 = abs(c0.a);
  float3 acc = c0.rgb;
  float wsum = 1.0;
  const int N = 48;
  for (int i = 0; i < N; ++i) {
    float t = (float(i) + 0.5) / float(N);
    float r = sqrt(t) * u.maxCoc;
    float th = float(i) * 2.39996323;
    float2 o = float2(cos(th), sin(th)) * r;
    float4 s = src.sample(sLinear, in.uv + o * u.texel);
    float rs = abs(s.a);
    if (s.a > c0.a) rs = min(rs, r0 * 2.0);
    float w = smoothstep(r - 1.0, r + 0.5, rs);
    acc += s.rgb * w;
    wsum += w;
  }
  return float4(acc / wsum, c0.a);
}

fragment float4 dofCompositeFS(FsOut in [[stage_in]], texture2d<float> sharp [[texture(0)]],
                               texture2d<float> blur [[texture(1)]], texture2d<float> cocTex [[texture(2)]]) {
  float3 s = sharp.sample(sLinear, in.uv).rgb;
  float3 b = blur.sample(sLinear, in.uv).rgb;
  float coc = abs(cocTex.sample(sLinear, in.uv).a);
  float t = smoothstep(0.5, 2.0, coc);
  return float4(mix(s, b, t), 1.0);
}

// ---- ACES tonemap + vignette + grain + fade (writes gamma-encoded values) ----
struct CompositeU {
  float3 flash;
  float bloomStrength;
  float exposure;
  float vignette;
  float grain;
  float time;
  float fade;
  int bloomOn;
};
constant float3x3 ACESIn = float3x3(float3(0.59719, 0.07600, 0.02840), float3(0.35458, 0.90834, 0.13383),
                                    float3(0.04823, 0.01566, 0.83777));
constant float3x3 ACESOut = float3x3(float3(1.60475, -0.10208, -0.00327), float3(-0.53108, 1.10813, -0.07276),
                                     float3(-0.07367, -0.00605, 1.07602));
float3 rrt(float3 v) {
  float3 a = v * (v + 0.0245786) - 0.000090537;
  float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return a / b;
}
float3 toSRGB(float3 c) {
  c = saturate(c);
  return mix(c * 12.92, 1.055 * pow(c, float3(1.0 / 2.4)) - 0.055, step(float3(0.0031308), c));
}
fragment float4 compositeFS(FsOut in [[stage_in]], constant CompositeU& u [[buffer(0)]],
                            texture2d<float> scene [[texture(0)]], texture2d<float> bloom [[texture(1)]]) {
  float3 c = scene.sample(sLinear, in.uv).rgb;
  if (u.bloomOn == 1) c = mix(c, bloom.sample(sLinear, in.uv).rgb, u.bloomStrength);
  c += u.flash;
  c *= u.exposure;
  c = ACESOut * rrt(ACESIn * c);
  float2 q = in.uv - 0.5;
  c *= mix(1.0, smoothstep(0.95, 0.15, length(q * float2(1.25, 1.0))), u.vignette);
  c = toSRGB(c);
  float n = hash12(in.position.xy + fract(u.time * 7.31) * 931.0) - 0.5;
  c += n * (u.grain + 1.5 / 255.0);
  return float4(c * u.fade, 1.0);
}
"""#
}

// ===========================================================================
// soft, depth-faded particles (instanced camera-facing quads)
// ===========================================================================
extension Shaders {
  fileprivate static let mslParticles = #"""
struct ParticleU {
  float4x4 view;
  float4x4 proj;
  float2 invScreen;
  float2 nearFar;
};
struct ParticleIn {
  float4 posSize;
  float4 color;    // rgb (premultiplied), alpha (0 = additive)
  float4 velType;  // velocity (for stretching), type
  float4 misc;     // rotation, stretch, seed, life fraction
};
struct PartOut {
  float4 position [[position]];
  float2 uv;
  float4 color;
  float type;
  float life;
  float seed;
  float3 viewPos;
  float size;
};
vertex PartOut particleVS(uint vid [[vertex_id]], uint iid [[instance_id]], constant ParticleU& u [[buffer(0)]],
                          const device ParticleIn* parts [[buffer(1)]]) {
  ParticleIn pi = parts[iid];
  // triangle strip corners (-1,-1) (1,-1) (-1,1) (1,1)
  float2 corner = float2(float(vid & 1u) * 2.0 - 1.0, float(vid >> 1) * 2.0 - 1.0);
  float3 vp = (u.view * float4(pi.posSize.xyz, 1.0)).xyz;
  float size = pi.posSize.w;
  float2 offs;
  if (pi.misc.y > 0.0) {
    float3 vv = (u.view * float4(pi.velType.xyz, 0.0)).xyz;
    float2 dir = vv.xy;
    float l = length(dir);
    dir = l > 1e-4 ? dir / l : float2(1.0, 0.0);
    float2 perp = float2(-dir.y, dir.x);
    float len = size + l * pi.misc.y;
    offs = dir * corner.x * len + perp * corner.y * size;
  } else {
    float r = pi.misc.x;
    float2x2 R = float2x2(float2(cos(r), sin(r)), float2(-sin(r), cos(r)));
    offs = R * corner * size;
  }
  vp.xy += offs;
  PartOut o;
  o.viewPos = vp;
  o.size = size;
  o.uv = corner;
  o.color = pi.color;
  o.type = pi.velType.w;
  o.life = pi.misc.w;
  o.seed = pi.misc.z;
  o.position = u.proj * float4(vp, 1.0);
  return o;
}

fragment float4 particleFS(PartOut in [[stage_in]], constant ParticleU& u [[buffer(0)]],
                           depth2d<float> depthTex [[texture(0)]], texture3d<float> noise [[texture(1)]]) {
  float2 vUV = in.uv;
  float r2 = dot(vUV, vUV);
  if (r2 > 1.0) discard_fragment();
  float a;
  int type = int(in.type + 0.5);
  if (type == 1) {
    float n = noise.sample(sNoise, float3(vUV * 0.28 + float2(in.seed), in.life * 0.25 + in.seed)).r;
    float n2 = noise.sample(sNoise, float3(vUV * 0.6 + float2(in.seed * 1.7), in.life * 0.4)).g;
    a = (1.0 - smoothstep(0.15, 1.0, r2)) * saturate(0.25 + 1.2 * n * n2 * 2.0);
  } else if (type == 2) {
    a = exp(-r2 * 3.5);
  } else if (type == 3) {
    float ring = abs(sqrt(r2) - 0.82);
    a = exp(-ring * ring * 160.0);
  } else {
    a = exp(-r2 * 4.5);
  }
  // soft fade against the (single-sample) prepass depth
  float sceneZ = linZ(depthTex.sample(sNearest, in.position.xy * u.invScreen), u.nearFar);
  float fragZ = -in.viewPos.z;
  a *= saturate((sceneZ - fragZ) / (in.size * 0.5 + 0.015));
  return float4(in.color.rgb * a, in.color.a * a);
}
"""#
}
