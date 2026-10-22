#include "shaders.h"

namespace shaders {

// ===========================================================================
const char* kCommon = R"GLSL(
#define PI 3.14159265359
float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3 saturate(vec3 x) { return clamp(x, vec3(0.0), vec3(1.0)); }
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
)GLSL";

// ===========================================================================
const char* kLighting = R"GLSL(
uniform vec3 uCamPos;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform mat4 uLightVP;
uniform sampler2DShadow uShadowCmp;
uniform sampler2D uShadowRaw;
uniform float uShadowTexel;
uniform float uShadowWorld;
uniform float uShadowRange;
uniform int uShadowsOn;
uniform samplerCube uIrradiance;
uniform samplerCube uPrefilter;
uniform sampler2D uBrdfLut;
uniform float uPrefilterMaxLod;
uniform float uAmbient;
uniform int uNumLights;
uniform vec4 uLightPos[8];
uniform vec4 uLightCol[8];
uniform sampler2D uSSAO;
uniform int uSSAOOn;
uniform vec2 uInvScreen;

const vec2 POISSON[16] = vec2[16](
  vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870),
  vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
  vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379), vec2(0.44323325, -0.97511554),
  vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
  vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367),
  vec2(0.14383161, -0.14100790));

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
vec3 F_Schlick(float VoH, vec3 f0) { return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0); }
vec3 F_SchlickR(float NoV, vec3 f0, float r) {
  return f0 + (max(vec3(1.0 - r), f0) - f0) * pow(1.0 - NoV, 5.0);
}

// The key light is a softbox hanging over the board: everything outside
// the pool of light around it falls off into darkness.
float lightPool(vec3 p) { return mix(1.0, 0.04, smoothstep(5.2, 12.0, length(p.xz))); }

float shadowAt(vec3 wpos, vec3 n, out float thick) {
  thick = 10.0;
  vec3 L = uLightDir;
  float NoL = dot(n, L);
  vec3 p = wpos + n * (0.010 * (1.0 - saturate(NoL)) + 0.003);
  vec4 lp = uLightVP * vec4(p, 1.0);
  vec3 sc = lp.xyz / lp.w * 0.5 + 0.5;
  if (sc.x < 0.001 || sc.x > 0.999 || sc.y < 0.001 || sc.y > 0.999 || sc.z > 1.0) return 1.0;
  float rot = ign(gl_FragCoord.xy) * 6.2831853;
  mat2 R = mat2(cos(rot), sin(rot), -sin(rot), cos(rot));
  // distance light travelled inside the object (for translucency)
  vec4 lp2 = uLightVP * vec4(wpos - n * 0.006, 1.0);
  vec3 sc2 = lp2.xyz / lp2.w * 0.5 + 0.5;
  // nearest occluder depth in a small neighbourhood: robust at light-space silhouettes
  float dmin = texture(uShadowRaw, sc2.xy).r;
  float tt = 2.5 * uShadowTexel;
  dmin = min(dmin, texture(uShadowRaw, sc2.xy + vec2(tt, 0.0)).r);
  dmin = min(dmin, texture(uShadowRaw, sc2.xy - vec2(tt, 0.0)).r);
  dmin = min(dmin, texture(uShadowRaw, sc2.xy + vec2(0.0, tt)).r);
  dmin = min(dmin, texture(uShadowRaw, sc2.xy - vec2(0.0, tt)).r);
  thick = max(0.0, sc2.z - dmin) * uShadowRange;
  // PCSS blocker search
  float searchR = 0.09 / uShadowWorld;
  float bsum = 0.0, bcnt = 0.0;
  for (int i = 0; i < 8; ++i) {
    vec2 o = R * POISSON[i * 2] * searchR;
    float d = texture(uShadowRaw, sc.xy + o).r;
    if (d < sc.z - 0.00015) { bsum += d; bcnt += 1.0; }
  }
  if (bcnt < 0.5) return 1.0;
  float dist = (sc.z - bsum / bcnt) * uShadowRange;
  float pen = clamp(dist * 0.05, 0.0, 0.12);
  float r = max(pen / uShadowWorld, 1.6 * uShadowTexel);
  float s = 0.0;
  for (int i = 0; i < 16; ++i) {
    vec2 o = R * POISSON[i] * r;
    s += texture(uShadowCmp, vec3(sc.xy + o, sc.z - 0.00012));
  }
  return s / 16.0;
}

struct Surface {
  vec3 P;
  vec3 N;
  vec3 V;
  vec3 albedo;
  float rough;
  float metal;
  float ao;
  float wrap;
  vec3 sss;
  float specOcc;
};

vec3 shadeSurface(Surface s, float shadow, float thick) {
  vec3 N = s.N, V = s.V, L = uLightDir;
  float NoV = max(dot(N, V), 1e-4);
  float NoL = dot(N, L);
  vec3 f0 = mix(vec3(0.04), s.albedo, s.metal);
  vec3 diffC = s.albedo * (1.0 - s.metal);
  float pool = lightPool(s.P);
  vec3 Fv = F_SchlickR(NoV, f0, s.rough);

  // --- key light (diffuse; its specular lives in the environment softbox)
  float w = s.wrap;
  float wrapped = max(0.0, (NoL + w) / ((1.0 + w) * (1.0 + w)));
  vec3 col = diffC * (1.0 - Fv) * (1.0 / PI) * uLightColor * wrapped * shadow * pool;
  // subsurface translucency: thin marble glows when back-lit
  col += s.sss * diffC * uLightColor * pool * exp(-max(thick, 0.012) * 16.0) * saturate(0.3 - NoL * 0.7) * 0.18;

  // --- dynamic point lights (sparks, magic, muzzle flashes)
  for (int i = 0; i < 8; ++i) {
    if (i >= uNumLights) break;
    vec3 Lv = uLightPos[i].xyz - s.P;
    float d2 = dot(Lv, Lv);
    float d = sqrt(d2);
    vec3 Ld = Lv / max(d, 1e-4);
    float fall = saturate(1.0 - pow(d / uLightPos[i].w, 4.0));
    fall *= fall;
    vec3 rad = uLightCol[i].rgb * fall / (d2 + 0.04);
    float nl = saturate(dot(N, Ld));
    vec3 H = normalize(V + Ld);
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));
    float a = max(s.rough * s.rough, 0.02);
    vec3 F = F_Schlick(VoH, f0);
    vec3 spec = D_GGX(NoH, a) * V_Smith(NoV, nl, a) * F;
    col += (diffC * (1.0 - F) / PI + spec) * rad * nl;
    col += s.sss * diffC * rad * saturate(0.25 - dot(N, Ld) * 0.6) * 0.3;
  }

  // --- image based ambient
  float ao = s.ao;
  if (uSSAOOn == 1) ao *= texture(uSSAO, gl_FragCoord.xy * uInvScreen).r;
  vec3 R = reflect(-V, N);
  vec3 irr = texture(uIrradiance, N).rgb;
  vec3 pre = textureLod(uPrefilter, R, s.rough * uPrefilterMaxLod).rgb;
  vec2 ab = texture(uBrdfLut, vec2(NoV, s.rough)).rg;
  vec3 specIBL = pre * (Fv * ab.x + ab.y);
  float keyAlign = smoothstep(0.72, 0.97, dot(R, L));
  specIBL *= mix(1.0, shadow, keyAlign * 0.92);
  float so = saturate(pow(NoV + ao, exp2(-16.0 * s.rough - 1.0)) - 1.0 + ao) * s.specOcc;
  col += (diffC * (1.0 - Fv) * irr * ao * mix(1.0, pool, 0.75) + specIBL * so) * uAmbient;
  return col;
}
)GLSL";

// ===========================================================================
const char* kMeshVS = R"GLSL(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in float aAO;
layout(location = 3) in float aAux;
layout(location = 4) in float aBones;  // bone0 + 64 * bone1
layout(location = 5) in float aMat;    // material + 0.998 * weight of bone0
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform vec4 uRig;     // x bend (rad), y bend direction (rad), z squash/stretch, w twist (rad)
uniform float uRigH;   // rest height of the piece
uniform int uClip;
uniform int uBoneCount;  // > 0: skinned
uniform mat4 uBones[20];
out vec3 vWorld;
out vec3 vNrm;
out vec3 vObj;
out vec3 vObjN;
out float vAO;
out float vAux;
flat out float vMat;

vec3 deform(vec3 p, inout vec3 n) {
  float H = max(uRigH, 0.1);
  float s = max(uRig.z, 0.05);
  float is = inversesqrt(s);
  p.y *= s;
  p.xz *= is;
  n = normalize(vec3(n.x * sqrt(s), n.y / s, n.z * sqrt(s)));
  float Hs = H * s;
  // twist increases with height
  float ta = uRig.w * clamp(p.y / Hs, 0.0, 1.2);
  float ct = cos(ta), st = sin(ta);
  p.xz = vec2(ct * p.x - st * p.z, st * p.x + ct * p.z);
  n.xz = vec2(ct * n.x - st * n.z, st * n.x + ct * n.z);
  float b = uRig.x;
  if (abs(b) > 1e-4) {
    vec2 d = vec2(cos(uRig.y), sin(uRig.y));
    float u = dot(p.xz, d);
    vec2 perp = p.xz - d * u;
    float R = Hs / b;
    float a = b * (p.y / Hs);
    float ca = cos(a), sa = sin(a);
    float u2 = R - (R - u) * ca;
    float y2 = (R - u) * sa;
    p = vec3(d.x * u2 + perp.x, y2, d.y * u2 + perp.y);
    float nu = dot(n.xz, d);
    vec2 nperp = n.xz - d * nu;
    float nu2 = nu * ca + n.y * sa;
    float ny2 = -nu * sa + n.y * ca;
    n = vec3(d.x * nu2 + nperp.x, ny2, d.y * nu2 + nperp.y);
  }
  return p;
}

void main() {
  vec3 n = aNrm;
  vec3 p = aPos;
  if (uBoneCount > 0) {
    float b1 = floor(aBones / 64.0 + 0.001);
    float b0 = aBones - 64.0 * b1;
    float w0 = clamp(fract(aMat) / 0.998, 0.0, 1.0);
    mat4 M = uBones[int(b0 + 0.5)] * w0 + uBones[int(b1 + 0.5)] * (1.0 - w0);
    p = (M * vec4(p, 1.0)).xyz;
    n = mat3(M) * n;
  }
  p = deform(p, n);
  vec4 w = uModel * vec4(p, 1.0);
  vWorld = w.xyz;
  vNrm = mat3(uModel) * n;
  vObj = aPos;
  vObjN = aNrm;
  vAO = aAO;
  vAux = aAux;
  vMat = floor(aMat + 0.0001);
  gl_Position = uViewProj * w;
  gl_ClipDistance[0] = (uClip == 1) ? (w.y + 0.002) : 1.0;
}
)GLSL";

// dissolve helper shared by shadow/prepass/object shaders
#define LM_DISSOLVE                                                                  \
  "uniform float uDissolve;\n"                                                       \
  "uniform vec3 uTexOffset;\n"                                                       \
  "uniform sampler3D uNoise;\n"                                                      \
  "float dissolveField(vec3 o) {\n"                                                  \
  "  return texture(uNoise, o * 2.1 + uTexOffset * 0.37).r * 0.78 +\n"              \
  "         0.22 * (1.0 - clamp(o.y / 1.7, 0.0, 1.0));\n"                            \
  "}\n"

const char* kShadowFS = LM_DISSOLVE R"GLSL(
in vec3 vObj;
void main() {
  if (uDissolve > 0.0 && dissolveField(vObj) < uDissolve) discard;
}
)GLSL";

const char* kPrepassFS = LM_DISSOLVE R"GLSL(
in vec3 vWorld;
in vec3 vNrm;
in vec3 vObj;
uniform mat4 uView;
out vec4 oN;
void main() {
  if (uDissolve > 0.0 && dissolveField(vObj) < uDissolve) discard;
  vec3 n = normalize(mat3(uView) * normalize(vNrm));
  if (!gl_FrontFacing) n = -n;
  oN = vec4(n, 1.0);
}
)GLSL";

// ===========================================================================
const char* kObjectFS = LM_DISSOLVE R"GLSL(
in vec3 vWorld;
in vec3 vNrm;
in vec3 vObj;
in vec3 vObjN;
in float vAO;
in float vAux;
flat in float vMat;
uniform int uMat;        // 0 white marble, 1 black marble, 2 sword, 3 iron, 4 medieval figure, 5 alien, 6 glass
uniform int uTeam;       // medieval / aliens: 0 white army, 1 black army
uniform float uTime;
uniform vec4 uGlow;      // rgb + strength
uniform float uCrack;
uniform vec3 uCrackColor;
uniform vec3 uDissolveColor;
uniform float uHeat;
uniform float uStone;
uniform float uGroundAO;
uniform float uGhost;    // > 0: a translucent ghost (its opacity)
out vec4 oColor;

float hash13(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}

// Painted medieval figures: every vertex carries one of these materials.
// The white army wears ivory and azure over bright steel; the black army
// sable and gules over blackened steel.
void medievalSurface(int id, bool black, vec3 o, vec3 on, inout vec3 albedo, inout float rough, inout float metal,
                     inout float wrap, inout vec3 sss, inout vec3 N, inout vec3 emissive, inout float specOcc) {
  float n1 = texture(uNoise, o * 3.1 + uTexOffset * 0.13).r;
  float n2 = texture(uNoise, o * 11.0 + uTexOffset * 0.07).g;
  float n3 = texture(uNoise, o * 37.0).b;
  if (id == 0) {  // stone: ashlar courses with mortar joints
    vec3 an = abs(on);
    float u = an.x > an.z ? o.z : o.x;
    if (an.y > 0.8) u = o.x + o.z * 0.3;
    float h = 0.052;
    float row = floor(o.y / h);
    float uu = u / 0.095 + 0.5 * mod(row, 2.0);
    float fy = fract(o.y / h), fu = fract(uu);
    float joint = 1.0 - smoothstep(0.0, 0.06, min(min(fy, 1.0 - fy) * h / 0.095, min(fu, 1.0 - fu)));
    if (an.y > 0.8) joint *= 0.3;
    float blockVar = hash13(vec3(floor(uu), row, 7.0)) - 0.5;
    vec3 base = black ? vec3(0.12, 0.115, 0.11) : vec3(0.70, 0.66, 0.58);
    albedo = base * (0.86 + 0.22 * n1 + 0.2 * blockVar + 0.12 * (n2 - 0.5));
    albedo = mix(albedo, black ? vec3(0.07) : vec3(0.48, 0.45, 0.40), joint * 0.75);
    rough = 0.82 + 0.1 * n2;
    N = normalize(N + (vec3(n2, n3, n1) - 0.5) * 0.35 + (joint * 0.0));
    specOcc = 0.6;
  } else if (id == 1) {  // wood
    float g = texture(uNoise, vec3(o.x * 9.0, o.y * 1.2, o.z * 9.0) + uTexOffset * 0.1).r;
    float ring = abs(fract(g * 7.0) - 0.5) * 2.0;
    vec3 base = black ? vec3(0.055, 0.036, 0.026) : vec3(0.48, 0.33, 0.19);
    albedo = base * (0.75 + 0.35 * ring + 0.15 * n2);
    rough = black ? 0.32 : 0.48;
    rough += 0.1 * n3;
  } else if (id == 2 || id == 12 || id == 13) {  // steel, mail, wrought iron
    metal = 1.0;
    if (id == 13) {
      albedo = vec3(0.09, 0.085, 0.085) * (0.8 + 0.5 * n1);
      albedo = mix(albedo, vec3(0.18, 0.07, 0.03), smoothstep(0.55, 0.8, n2) * 0.6);
      rough = 0.55 + 0.2 * n3;
      metal = 0.75;
    } else {
      albedo = black ? vec3(0.13, 0.13, 0.145) : vec3(0.64, 0.64, 0.66);
      albedo *= 0.88 + 0.2 * n1;
      rough = (black ? 0.34 : 0.2) + 0.12 * n2;
      if (id == 12) {  // chain mail: little rings
        vec2 q = vec2(o.x + o.z, o.y) / 0.0075;
        q.x += 0.5 * mod(floor(q.y), 2.0);
        vec2 f = fract(q) - 0.5;
        float ringD = abs(length(f) - 0.32);
        float rr = 1.0 - smoothstep(0.06, 0.16, ringD);
        albedo *= 0.35 + 0.75 * rr;
        rough = mix(0.6, rough + 0.05, rr);
        N = normalize(N + vec3(f.x, 0.0, f.y) * rr * 0.6);
      }
    }
  } else if (id == 3) {  // gold
    albedo = vec3(1.0, 0.74, 0.32) * (0.9 + 0.15 * n1);
    metal = 1.0;
    rough = 0.2 + 0.1 * n2;
  } else if (id == 4 || id == 5 || id == 18 || id == 20) {  // cloth, heraldic cloth, velvet, linen
    vec3 base;
    if (id == 4) base = black ? vec3(0.04, 0.038, 0.037) : vec3(0.76, 0.73, 0.65);
    else if (id == 5) base = black ? vec3(0.36, 0.05, 0.04) : vec3(0.075, 0.14, 0.33);
    else if (id == 18) base = black ? vec3(0.26, 0.02, 0.035) : vec3(0.05, 0.07, 0.26);
    else base = vec3(0.84, 0.82, 0.76);
    float weave = 0.92 + 0.08 * sin(o.x * 2600.0) * sin(o.y * 2600.0);
    albedo = base * (0.85 + 0.25 * n1) * weave;
    rough = id == 18 ? 0.7 : 0.88;
    wrap = 0.35;
    float NoV = saturate(dot(N, normalize(uCamPos - vWorld)));
    emissive += base * pow(1.0 - NoV, 3.0) * (id == 18 ? 0.5 : 0.18);  // fabric sheen
    specOcc = 0.5;
  } else if (id == 6) {  // skin
    albedo = vec3(0.60, 0.40, 0.30) * (0.92 + 0.12 * n1);
    rough = 0.6;
    wrap = 0.45;
    sss = vec3(1.0, 0.4, 0.25) * 1.0;
  } else if (id == 7) {  // hair: a colour per figure
    float h = hash13(floor(uTexOffset * 3.0) + 0.5);
    vec3 c = h < 0.3 ? vec3(0.12, 0.07, 0.035) : (h < 0.55 ? vec3(0.32, 0.17, 0.07) :
             (h < 0.75 ? vec3(0.55, 0.42, 0.22) : vec3(0.6, 0.58, 0.55)));
    float strand = texture(uNoise, vec3(o.x * 60.0, o.y * 8.0, o.z * 60.0)).r;
    albedo = c * (0.7 + 0.5 * strand);
    rough = 0.55;
  } else if (id == 8) {  // horse coat
    if (black) {
      albedo = vec3(0.035, 0.03, 0.028) * (0.8 + 0.4 * n1);
      rough = 0.38;
    } else {
      float dap = smoothstep(0.45, 0.62, texture(uNoise, o * 7.0 + uTexOffset * 0.1).g);
      albedo = mix(vec3(0.66, 0.64, 0.61), vec3(0.42, 0.41, 0.40), dap * 0.6) * (0.92 + 0.1 * n2);
      rough = 0.5;
    }
    wrap = 0.2;
  } else if (id == 9) {  // leather
    albedo = vec3(0.21, 0.115, 0.06) * (0.75 + 0.4 * n1);
    rough = 0.5 + 0.15 * n2;
  } else if (id == 10) {  // ermine
    vec2 q = vec2(o.x + o.z, o.y) / 0.04;
    q.x += 0.5 * mod(floor(q.y), 2.0);
    float spot = 1.0 - smoothstep(0.08, 0.14, length(fract(q) - 0.5) - 0.0);
    albedo = mix(vec3(0.9, 0.89, 0.85) * (0.9 + 0.1 * n2), vec3(0.02), spot);
    rough = 0.95;
    wrap = 0.4;
  } else if (id == 11) {  // gems
    albedo = black ? vec3(0.6, 0.02, 0.04) : vec3(0.05, 0.12, 0.7);
    rough = 0.06;
    emissive += albedo * 0.25;
  } else if (id == 14) {  // dragon scales
    vec2 q = vec2(atan(o.x, o.z) * 0.08 + o.x * 0.3, o.y) / 0.022;
    q.x += 0.5 * mod(floor(q.y), 2.0);
    vec2 f = fract(q) - vec2(0.5, 0.2);
    float sc = smoothstep(0.55, 0.2, length(f * vec2(1.0, 1.4)));
    vec3 base = black ? vec3(0.09, 0.022, 0.02) : vec3(0.42, 0.30, 0.08);
    albedo = base * (0.55 + 0.6 * sc) * (0.85 + 0.3 * n1);
    metal = black ? 0.2 : 0.55;
    rough = 0.35 + 0.2 * (1.0 - sc);
    N = normalize(N + vec3(f.x, 0.0, f.y) * 0.5 * sc);
  } else if (id == 15) {  // wing membrane
    albedo = (black ? vec3(0.22, 0.03, 0.025) : vec3(0.5, 0.27, 0.1)) * (0.8 + 0.3 * n1);
    rough = 0.6;
    wrap = 0.6;
    sss = black ? vec3(1.0, 0.2, 0.1) * 2.0 : vec3(1.0, 0.6, 0.2) * 2.0;
  } else if (id == 16) {  // horn, teeth, claws
    albedo = vec3(0.82, 0.76, 0.62) * (0.85 + 0.2 * n1);
    rough = 0.38;
  } else if (id == 17) {  // dragon eye
    albedo = vec3(0.05);
    rough = 0.1;
    emissive += vec3(4.0, 2.2, 0.4);
  } else if (id == 19) {  // shadowy interior
    albedo = vec3(0.012, 0.01, 0.009);
    rough = 0.95;
    specOcc = 0.0;
  } else if (id == 22) {  // mane and tail
    float strand = texture(uNoise, vec3(o.x * 70.0, o.y * 6.0, o.z * 70.0)).r;
    albedo = (black ? vec3(0.025, 0.022, 0.02) : vec3(0.72, 0.70, 0.66)) * (0.65 + 0.6 * strand);
    rough = 0.5;
  } else if (id == 21) {  // slate roof: overlapping shingles
    float row = floor(o.y / 0.022);
    float u = atan(o.x, o.z) * 0.12 / 0.018 + 0.5 * mod(row, 2.0);
    float edge = smoothstep(0.75, 1.0, fract(o.y / 0.022)) + 0.6 * (1.0 - smoothstep(0.0, 0.08, abs(fract(u) - 0.5) - 0.42));
    vec3 base = black ? vec3(0.07, 0.07, 0.08) : vec3(0.22, 0.24, 0.27);
    albedo = base * (0.8 + 0.3 * hash13(vec3(floor(u), row, 3.0))) * (1.0 - 0.45 * saturate(edge));
    rough = 0.55;
  }
  // fresh fracture faces: the plaster inside the paint
  if (vAux > 0.0) {
    vec3 broken = black ? vec3(0.11, 0.105, 0.1) : vec3(0.84, 0.82, 0.78);
    albedo = mix(albedo, broken * (0.85 + 0.3 * n3), vAux);
    rough = mix(rough, 0.85, vAux);
    metal = mix(metal, 0.0, vAux);
    sss *= 1.0 - vAux;
  }
}
)GLSL"  // two literals: MSVC caps a single string literal at 16 KB
R"GLSL(
// The aliens. The white army is green-skinned in silver and pearl with cyan
// lights, the black army violet-skinned in black and gunmetal with magenta lights.
vec3 alienLight(bool black) { return black ? vec3(1.0, 0.18, 0.62) : vec3(0.25, 1.0, 0.92); }
void alienSurface(int id, bool black, vec3 o, vec3 on, inout vec3 albedo, inout float rough, inout float metal,
                  inout float wrap, inout vec3 sss, inout vec3 N, inout vec3 emissive, inout float specOcc) {
  float n1 = texture(uNoise, o * 3.3 + uTexOffset * 0.13).r;
  float n2 = texture(uNoise, o * 12.0 + uTexOffset * 0.07).g;
  float n3 = texture(uNoise, o * 41.0).b;
  vec3 V = normalize(uCamPos - vWorld);
  float NoV = saturate(dot(N, V));
  if (id == 0 || id == 1) {  // skin: wet, mottled, translucent
    vec3 base = id == 0 ? (black ? vec3(0.30, 0.13, 0.42) : vec3(0.25, 0.52, 0.17))
                        : (black ? vec3(0.62, 0.45, 0.72) : vec3(0.60, 0.80, 0.42));
    float mottle = smoothstep(0.45, 0.75, texture(uNoise, o * 9.0 + uTexOffset * 0.2).g);
    albedo = base * (0.82 + 0.3 * n1) * (1.0 - 0.25 * mottle * float(id == 0));
    rough = 0.3 + 0.18 * n2;
    wrap = 0.5;
    sss = (black ? vec3(1.0, 0.35, 0.75) : vec3(0.7, 1.0, 0.35)) * 1.3;
    N = normalize(N + (vec3(n2, n3, n1) - 0.5) * 0.12);
  } else if (id == 2) {  // big glossy eyes
    albedo = black ? vec3(0.012, 0.004, 0.008) : vec3(0.004, 0.008, 0.012);
    rough = 0.04;
    emissive += alienLight(black) * 0.05 * pow(1.0 - NoV, 3.0);  // a sheen deep in the eye
  } else if (id == 3) {  // jumpsuit: satin
    albedo = black ? vec3(0.03, 0.026, 0.04) : vec3(0.72, 0.74, 0.78);
    albedo *= 0.9 + 0.15 * n1;
    rough = 0.38 + 0.1 * n2;
    metal = 0.25;
    emissive += (black ? vec3(0.25, 0.05, 0.35) : vec3(0.2)) * pow(1.0 - NoV, 3.0) * 0.4;
  } else if (id == 4) {  // chrome
    albedo = black ? vec3(0.26, 0.25, 0.29) : vec3(0.93, 0.94, 0.96);
    metal = 1.0;
    rough = (black ? 0.16 : 0.07) + 0.06 * n2;
  } else if (id == 5) {  // painted hull with panel seams and rivets
    float a = atan(o.x, o.z);
    float r = length(o.xz);
    float seamA = abs(fract(a * 12.0 / 6.2831853) - 0.5) * 2.0;
    float seamR = abs(fract(r / 0.055 + o.y / 0.07) - 0.5) * 2.0;
    float seam = (1.0 - smoothstep(0.0, 0.04, 1.0 - seamA)) + (1.0 - smoothstep(0.0, 0.05, 1.0 - seamR));
    albedo = (black ? vec3(0.045, 0.045, 0.055) : vec3(0.84, 0.85, 0.87)) * (0.93 + 0.12 * n1);
    albedo *= 1.0 - 0.55 * saturate(seam);
    rough = (black ? 0.32 : 0.22) + 0.1 * n2;
    metal = 0.15;
  } else if (id == 6 || id == 18) {  // running lights (chasing round) and plasma
    vec3 c = alienLight(black);
    float chase = id == 6 ? 0.35 + 0.65 * pow(0.5 + 0.5 * sin(atan(o.x, o.z) * 6.0 - uTime * 7.0), 3.0)
                          : 0.7 + 0.3 * sin(uTime * 9.0 + n1 * 6.0);
    albedo = c * 0.3;
    rough = 0.15;
    emissive += c * chase * (id == 6 ? 5.0 : 3.5);
  } else if (id == 7) {  // brain: pink folds, glistening
    float f = texture(uNoise, o * 24.0 + uTexOffset * 0.05).r;
    float sulcus = 1.0 - smoothstep(0.0, 0.12, abs(f - 0.5));
    albedo = vec3(0.86, 0.50, 0.55) * (0.85 + 0.2 * n1) * (1.0 - 0.45 * sulcus);
    rough = 0.25 + 0.2 * sulcus;
    wrap = 0.5;
    sss = vec3(1.0, 0.35, 0.35) * 1.5;
    N = normalize(N + (vec3(texture(uNoise, o * 24.0 + 0.3).r, 0.0, texture(uNoise, o * 24.0 + 0.6).r) - 0.5) * 0.5);
  } else if (id == 8) {  // gold
    albedo = vec3(1.0, 0.76, 0.33) * (0.9 + 0.15 * n1);
    metal = 1.0;
    rough = 0.18 + 0.1 * n2;
  } else if (id == 9) {  // cape and gown: velvet
    vec3 base = black ? vec3(0.42, 0.03, 0.07) : vec3(0.03, 0.30, 0.36);
    albedo = base * (0.8 + 0.3 * n1);
    rough = 0.65;
    wrap = 0.3;
    emissive += base * pow(1.0 - NoV, 3.0) * 0.6;  // velvet sheen
    specOcc = 0.5;
  } else if (id == 10) {  // glowing crystal
    vec3 c = alienLight(black);
    albedo = c * 0.4;
    rough = 0.05;
    emissive += c * (1.2 + 0.6 * sin(uTime * 3.0 + n1 * 4.0));
  } else if (id == 11) {  // slime
    vec3 c = black ? vec3(0.55, 0.12, 0.8) : vec3(0.35, 0.9, 0.08);
    albedo = c * (0.7 + 0.3 * n1);
    rough = 0.04;
    wrap = 0.6;
    sss = c * 2.0;
    emissive += c * 0.25;
  } else if (id == 12) {  // rubber
    albedo = vec3(0.035, 0.035, 0.04) * (0.8 + 0.3 * n1);
    rough = 0.65;
  } else if (id == 13) {  // cowhide: black patches on white
    float patchy = smoothstep(0.52, 0.56, texture(uNoise, o * 4.5 + vec3(1.7)).r);
    albedo = mix(vec3(0.86, 0.84, 0.8), vec3(0.03, 0.028, 0.026), patchy) * (0.9 + 0.12 * n2);
    rough = 0.65;
    wrap = 0.3;
  } else if (id == 14) {  // tongue, gums, cow's nose: wet pink
    albedo = vec3(0.82, 0.42, 0.48) * (0.85 + 0.2 * n1);
    rough = 0.22;
    wrap = 0.4;
    sss = vec3(1.0, 0.3, 0.3);
  } else if (id == 15) {  // teeth
    albedo = vec3(0.9, 0.88, 0.8);
    rough = 0.25;
  } else if (id == 16) {  // the dark inside
    albedo = vec3(0.01, 0.008, 0.01);
    rough = 0.9;
    specOcc = 0.0;
  } else if (id == 17) {  // carapace: glossy, iridescent
    vec3 base = black ? vec3(0.07, 0.02, 0.1) : vec3(0.04, 0.26, 0.24);
    vec3 film = black ? vec3(0.5, 0.1, 0.9) : vec3(0.2, 0.9, 0.6);
    albedo = mix(base, film * 0.4, pow(1.0 - NoV, 2.0)) * (0.85 + 0.25 * n1);
    rough = 0.12 + 0.08 * n2;
    metal = 0.35;
  } else if (id == 19) {  // horn, hooves, claws
    albedo = vec3(0.72, 0.66, 0.55) * (0.8 + 0.25 * n1);
    rough = 0.4;
  } else if (id == 20) {  // leathery egg with veins
    float vein = 1.0 - smoothstep(0.0, 0.05, abs(texture(uNoise, o * 7.0).g - 0.5));
    albedo = mix(vec3(0.62, 0.55, 0.36), black ? vec3(0.45, 0.1, 0.4) : vec3(0.3, 0.55, 0.1), vein * 0.7) *
             (0.85 + 0.2 * n1);
    rough = 0.35;
    wrap = 0.5;
    sss = vec3(1.0, 0.8, 0.4);
  }
  // fresh fracture faces: alien goo inside
  if (vAux > 0.0) {
    vec3 goo = black ? vec3(0.5, 0.1, 0.75) : vec3(0.35, 0.85, 0.1);
    albedo = mix(albedo, goo * (0.8 + 0.3 * n3), vAux);
    rough = mix(rough, 0.08, vAux);
    metal = mix(metal, 0.0, vAux);
    emissive += goo * 0.3 * vAux;
  }
}

// Glass: reflections and a faint tint, more opaque at grazing angles (premultiplied alpha).
vec4 glassColor(vec3 N, vec3 V, bool black) {
  float NoV = max(dot(N, V), 1e-4);
  vec3 R = reflect(-V, N);
  float F = 0.04 + 0.96 * pow(1.0 - NoV, 5.0);
  vec3 env = textureLod(uPrefilter, R, 0.04 * uPrefilterMaxLod).rgb * uAmbient * lightPool(vWorld);
  vec3 tint = black ? vec3(0.5, 0.2, 0.45) : vec3(0.3, 0.55, 0.6);
  float a = saturate(F + 0.07);
  return vec4(env * F * 1.6 + tint * 0.025, a);
}

// A ghost: the figure in its own colours, but see-through - a little more solid
// at the rims, fading away towards its feet, with faint bands shimmering
// upwards (premultiplied alpha).
vec4 ghostColor(vec3 lit, vec3 albedo, vec3 N, vec3 V) {
  float NoV = saturate(dot(N, V));
  float rim = pow(1.0 - NoV, 2.0);
  float n = texture(uNoise, vObj * 2.0 + vec3(0.0, uTime * 0.05, 0.0)).r;
  float band = 0.5 + 0.5 * sin(vWorld.y * 9.0 - uTime * 2.2 + n * 6.0);
  // its own colours, lit, with a little light of their own so that dark ones still show
  vec3 c = (lit + albedo * 0.22) * 1.2 + mix(albedo, vec3(1.0), 0.6) * rim * 0.6;
  float a = uGhost * saturate(0.3 + 0.45 * rim + 0.06 * band) * mix(0.2, 1.0, smoothstep(0.0, 0.3, vObj.y));
  return vec4(c * a, a);
}

void main() {
  vec3 N = normalize(vNrm);
  if (!gl_FrontFacing) N = -N;
  vec3 V = normalize(uCamPos - vWorld);
  vec3 emissive = vec3(0.0);
  float specOcc = 1.0;
  if (uDissolve > 0.0) {
    float edge = dissolveField(vObj) - uDissolve;
    if (edge < 0.0) discard;
    emissive += uDissolveColor * (1.0 - smoothstep(0.0, 0.06, edge)) * 7.0;
  }
  if (uMat == 6) {
    vec4 g = glassColor(N, V, uTeam == 1);
    oColor = vec4(g.rgb + emissive * g.a, g.a);
    return;
  }
  vec3 albedo = vec3(0.5);
  float rough = 0.5, metal = 0.0, wrap = 0.0;
  vec3 sss = vec3(0.0);
  if (uMat <= 1) {
    bool black = uMat == 1;
    // domain-warped noise gives wispy, branching veins like real stone
    vec3 q = vObj * 1.05 + uTexOffset;
    vec3 wv = vec3(texture(uNoise, q * 0.21).r, texture(uNoise, q * 0.21 + vec3(0.31, 0.57, 0.11)).g,
                   texture(uNoise, q * 0.21 + vec3(0.73, 0.19, 0.43)).r) - 0.5;
    float f = texture(uNoise, q * 0.33 + wv * 0.55).r;
    float f2 = texture(uNoise, q * 0.9 + wv * 0.9 + vec3(0.5)).g;
    float band = abs(fract(f * 2.6 + 0.1 * f2) - 0.5) * 2.0;
    float band2 = abs(fract(f2 * 3.5) - 0.5) * 2.0;
    float vein = 1.0 - smoothstep(0.0, 0.07, band);
    float halo = 1.0 - smoothstep(0.0, 0.35, band);
    float hair = (1.0 - smoothstep(0.0, 0.035, band2)) * smoothstep(0.45, 0.7, f);
    float cloud = texture(uNoise, q * 0.55 + vec3(0.2)).g;
    float n3 = texture(uNoise, q * 2.4 + vec3(0.71)).b;
    if (!black) {
      vec3 base = vec3(0.90, 0.88, 0.84) * (0.93 + 0.09 * cloud);
      albedo = mix(base, vec3(0.56, 0.57, 0.60), saturate(vein * 0.55 + halo * 0.10 + hair * 0.30));
      wrap = 0.5;
      sss = vec3(1.0, 0.78, 0.58) * 1.6;
    } else {
      vec3 base = vec3(0.011, 0.011, 0.013) * (0.8 + 0.4 * cloud);
      albedo = mix(base, vec3(0.62, 0.60, 0.56), saturate(vein * 0.85 + halo * 0.04 + hair * 0.55));
    }
    rough = mix(0.05, 0.11, n3) + vein * 0.04;
    float interior = vAux;
    if (interior > 0.0) {
      float g = texture(uNoise, vObj * 9.0).b;
      vec3 broken = black ? vec3(0.07, 0.07, 0.075) * (0.7 + 0.6 * g) : vec3(0.86, 0.85, 0.82) * (0.85 + 0.3 * g);
      albedo = mix(albedo, broken, interior);
      rough = mix(rough, 0.8, interior);
      N = normalize(N + interior * 0.4 * (texture(uNoise, vObj * 12.0).rgb - 0.5));
      sss *= 1.0 - interior;
    }
  } else if (uMat == 2) {
    float m = vAux;
    if (m < 0.5) {
      albedo = vec3(0.66, 0.67, 0.70);
      metal = 1.0;
      rough = 0.13 + 0.07 * texture(uNoise, vObj * vec3(30.0, 1.5, 30.0)).b;
    } else if (m < 1.5) {
      albedo = vec3(1.0, 0.72, 0.30);
      metal = 1.0;
      rough = 0.22;
    } else {
      albedo = vec3(0.06, 0.03, 0.02);
      rough = 0.55;
    }
  } else if (uMat == 3) {
    float g = texture(uNoise, vObj * 6.0).r;
    albedo = vec3(0.06, 0.06, 0.065) * (0.7 + 0.6 * g);
    metal = 0.85;
    rough = 0.42 + 0.2 * g;
    emissive += vec3(1.0, 0.33, 0.07) * uHeat * (0.6 + 0.8 * g) * 4.0;
  } else {
    if (uMat == 5)
      alienSurface(int(vMat + 0.5), uTeam == 1, vObj, normalize(vObjN), albedo, rough, metal, wrap, sss, N, emissive,
                   specOcc);
    else
      medievalSurface(int(vMat + 0.5), uTeam == 1, vObj, normalize(vObjN), albedo, rough, metal, wrap, sss, N, emissive,
                      specOcc);
    if (uStone > 0.0) {  // turning to salt: pale, crystalline, glittering
      float g = texture(uNoise, vObj * 18.0 + uTexOffset * 0.3).r;
      float front = smoothstep(0.0, 0.15, uStone * 1.3 - (1.0 - saturate(vObj.y / 1.3)) * 0.3 - g * 0.15);
      albedo = mix(albedo, vec3(0.88, 0.87, 0.84) * (0.85 + 0.25 * g), front);
      rough = mix(rough, 0.75, front);
      metal = mix(metal, 0.0, front);
      wrap = mix(wrap, 0.3, front);
      emissive += vec3(1.0, 0.95, 0.85) * front * pow(g, 12.0) * 6.0 * uStone;
    }
    if (uHeat > 0.0) {  // scorched by dragon fire
      float g = texture(uNoise, vObj * 5.0 + uTexOffset * 0.2).r;
      albedo *= 1.0 - 0.85 * saturate(uHeat * 1.5);
      emissive += vec3(1.0, 0.3, 0.05) * saturate(uHeat * 1.4 - 0.3) * (0.4 + 1.2 * g) * 5.0;
    }
  }
  if (uCrack > 0.0) {
    float c = texture(uNoise, vObj * 1.4 + uTexOffset * 0.2).a;
    float c2 = texture(uNoise, vObj * 3.1 + uTexOffset * 0.3).a;
    float w = uCrack * 0.11;
    float line = (1.0 - smoothstep(w * 0.45, w, c)) +
                 0.7 * (1.0 - smoothstep(w * 0.25, w * 0.55, c2)) * step(0.35, uCrack);
    line = saturate(line);
    albedo *= 1.0 - 0.85 * line;
    emissive += uCrackColor * line * (2.0 + 14.0 * uCrack);
  }
  float ao = vAO;
  float h = abs(vWorld.y);  // debris falling past the board is not grounded
  ao *= 1.0 - uGroundAO * 0.5 * exp(-h / 0.07) * saturate(0.6 - 0.6 * N.y);
  float NoV = saturate(dot(N, V));
  if (uGlow.a > 0.0) emissive += uGlow.rgb * uGlow.a * (pow(1.0 - NoV, 2.5) * 1.7 + 0.10);

  float thick = 10.0;
  float sh = 1.0;
  if (uShadowsOn == 1) sh = shadowAt(vWorld, N, thick);
  Surface s;
  s.P = vWorld;
  s.N = N;
  s.V = V;
  s.albedo = albedo;
  s.rough = clamp(rough, 0.03, 1.0);
  s.metal = metal;
  s.ao = ao;
  s.wrap = wrap;
  s.sss = sss;
  s.specOcc = specOcc;
  vec3 col = shadeSurface(s, sh, thick) + emissive;
  if (uGhost > 0.0) {
    oColor = ghostColor(col, albedo, N, V);
    return;
  }
  oColor = vec4(col, 1.0);
}
)GLSL";

// ===========================================================================
const char* kBoardFS = R"GLSL(
in vec3 vWorld;
in vec3 vNrm;
in vec3 vObj;
in float vAO;
in float vAux;
uniform sampler2D uBoardAlbedo;
uniform sampler2D uBoardDetail;
uniform float uBoardHalf;
uniform sampler2D uReflection;
uniform int uReflectOn;
uniform float uReflectLod;
uniform sampler2D uBoardState;
uniform vec4 uCheck;
uniform float uTime;
uniform vec4 uOcc[32];
uniform int uNumOcc;
uniform vec4 uRings[6];      // x, z, radius, intensity
uniform vec4 uRingCol[6];    // rgb, width
uniform int uNumRings;
uniform sampler3D uNoise;
out vec4 oColor;

void main() {
  vec3 Ng = normalize(vNrm);
  vec3 V = normalize(uCamPos - vWorld);
  bool top = Ng.y > 0.985 && vWorld.y > -0.003;
  bool under = Ng.y < -0.985;  // the board floats, so its underside is on show
  vec2 xz = vWorld.xz;
  float m = max(abs(xz.x), abs(xz.y));
  // underside rim: sample the plain outer frame; sides: project the frame inward with depth.
  // Both stay clear of the gold lettering.
  if (under) xz *= (uBoardHalf - 0.06) / max(m, 1e-3);
  else if (!top) xz -= normalize(Ng.xz + vec2(1e-5)) * min(-vWorld.y * 0.55 + 0.03, 0.15);
  vec2 uv = (xz + uBoardHalf) / (2.0 * uBoardHalf);
  // seen edge-on the sides would pick very blurry mips that smear the lettering in: cap the footprint
  vec2 gx = dFdx(uv), gy = dFdy(uv);
  if (!top) {
    float lim = 0.02 / (2.0 * uBoardHalf);
    gx *= min(1.0, lim / max(length(gx), 1e-8));
    gy *= min(1.0, lim / max(length(gy), 1e-8));
  }
  vec4 A = textureGrad(uBoardAlbedo, uv, gx, gy);
  vec4 D = textureGrad(uBoardDetail, uv, gx, gy);
  // all implicit-derivative fetches happen here, before any fragment can leave early
  float smudge = texture(uNoise, vec3(vWorld.xz * 0.31, 0.37)).g;
  float micro = texture(uNoise, vec3(vWorld.xz * 3.1, 0.71)).b;
  vec2 peel = (vec2(texture(uNoise, vec3(vWorld.xz * 6.0, 0.13)).r, texture(uNoise, vec3(vWorld.xz * 6.0, 0.53)).g) - 0.5) * 0.006;
  float fib = texture(uNoise, vec3(vWorld.xz * 11.0, 0.21)).r;
  float mottle = texture(uNoise, vec3(vWorld.xz * 0.7, 0.63)).g;

  if (under) {
    // a walnut rim framing a green baize panel
    float rim = smoothstep(uBoardHalf - 0.235, uBoardHalf - 0.225, m);
    float groove = 1.0 - smoothstep(0.0, 0.006, abs(m - (uBoardHalf - 0.23)));
    vec3 felt = vec3(0.020, 0.072, 0.040) * (0.75 + 0.4 * fib) * (0.8 + 0.4 * mottle);
    Surface s;
    s.P = vWorld;
    s.N = Ng;
    s.V = V;
    s.albedo = mix(felt, A.rgb, rim) * (1.0 - 0.7 * groove);
    s.rough = mix(0.95, A.a, rim);
    s.metal = 0.0;
    s.ao = 1.0;
    s.wrap = 0.0;
    s.sss = vec3(0.0);
    s.specOcc = mix(0.15, 0.8, rim);
    vec3 col = shadeSurface(s, 1.0, 10.0);
    // the key light never reaches the underside; a soft bounce from the room keeps it readable
    col += s.albedo * vec3(1.0, 0.86, 0.70) * (0.55 + 0.35 * smoothstep(0.0, uBoardHalf, m));
    oColor = vec4(col, 1.0);
    return;
  }
  vec3 albedo = A.rgb;
  float rough = A.a;
  float metal = D.b;
  float cavity = D.a;
  vec3 N = Ng;
  if (top) {
    vec2 nxz = D.rg * 2.0 - 1.0;
    N = normalize(vec3(nxz.x, sqrt(max(0.0, 1.0 - dot(nxz, nxz))), nxz.y));
  }
  float ao = mix(1.0, cavity, 0.8);
  if (top) {
    for (int i = 0; i < 32; ++i) {
      if (i >= uNumOcc) break;
      float d = length(xz - uOcc[i].xy);
      float r = uOcc[i].z;
      float o = 1.0 - smoothstep(r * 0.55, r * 2.0, d);
      ao *= 1.0 - 0.6 * o * uOcc[i].w;
    }
  }
  float thick = 10.0;
  float sh = 1.0;
  if (uShadowsOn == 1) sh = shadowAt(vWorld, Ng, thick);
  Surface s;
  s.P = vWorld;
  s.N = N;
  s.V = V;
  s.albedo = albedo;
  s.rough = rough;
  s.metal = metal;
  s.ao = ao;
  s.wrap = 0.0;
  s.sss = vec3(0.0);
  s.specOcc = mix(0.25, 1.0, metal);
  vec3 base = shadeSurface(s, sh, thick);

  // --- lacquer clear coat with planar reflections (with faint smudges / orange peel)
  float ccRough = 0.032 + 0.07 * smoothstep(0.5, 0.85, smudge) + 0.012 * micro;
  float NoV = max(dot(Ng, V), 1e-4);
  float Fc = 0.04 + 0.96 * pow(1.0 - NoV, 5.0);
  vec3 Rv = reflect(-V, Ng);
  vec3 env = textureLod(uPrefilter, Rv, ccRough * uPrefilterMaxLod).rgb * uAmbient;
  env *= mix(1.0, sh, smoothstep(0.72, 0.97, dot(Rv, uLightDir)) * 0.92);
  vec3 cc = env;
  if (top && uReflectOn == 1) {
    vec2 suv = gl_FragCoord.xy * uInvScreen + (N.xz - Ng.xz) * vec2(0.03, -0.03) + peel;
    vec4 r = textureLod(uReflection, suv, uReflectLod + ccRough * 14.0 - 0.45);
    cc = r.rgb + (1.0 - r.a) * env;
  }
  // point lights glint in the lacquer
  for (int i = 0; i < 8; ++i) {
    if (i >= uNumLights) break;
    vec3 Lv = uLightPos[i].xyz - vWorld;
    float d2 = dot(Lv, Lv);
    vec3 Ld = Lv * inversesqrt(d2);
    float fall = saturate(1.0 - pow(sqrt(d2) / uLightPos[i].w, 4.0));
    vec3 H = normalize(V + Ld);
    float nl = saturate(dot(Ng, Ld));
    float a = max(ccRough, 0.04);
    cc += D_GGX(saturate(dot(Ng, H)), a) * V_Smith(NoV, nl, a) * uLightCol[i].rgb * fall * fall / (d2 + 0.04) * nl;
  }
  vec3 col = base * (1.0 - Fc) + cc * Fc * mix(1.0, ao, 0.6);

  // --- interactive markings (projected light on the board)
  if (top && abs(vWorld.x) < 4.0 && abs(vWorld.z) < 4.0) {
    vec2 sqf = vec2(vWorld.x + 4.0, 4.0 - vWorld.z);
    vec2 cell = clamp(floor(sqf), vec2(0.0), vec2(7.0));
    vec2 lp = fract(sqf) - 0.5;
    vec4 st = texelFetch(uBoardState, ivec2(cell), 0);
    float dc = length(lp);
    float bd = max(abs(lp.x), abs(lp.y));
    vec3 em = vec3(0.0);
    em += vec3(1.0, 0.70, 0.32) * st.b * 0.06;
    em += vec3(0.85, 0.92, 1.0) * st.a * (1.0 - smoothstep(0.0, 0.022, abs(bd - 0.462))) * 0.8;
    float selB = 1.0 - smoothstep(0.0, 0.028, abs(bd - 0.455));
    em += vec3(1.0, 0.80, 0.42) * st.g * (selB * 2.4 + 0.10 * (1.0 - smoothstep(0.3, 0.5, bd)));
    float pulse = 0.82 + 0.18 * sin(uTime * 4.0);
    if (st.r > 0.75) em += vec3(1.0, 0.78, 0.42) * (1.0 - smoothstep(0.075, 0.12, dc)) * 1.7 * pulse;
    else if (st.r > 0.25) em += vec3(1.0, 0.30, 0.12) * (1.0 - smoothstep(0.0, 0.032, abs(dc - 0.41))) * 2.6 * pulse;
    col += em;
  }
  if (top) {
    for (int i = 0; i < 6; ++i) {
      if (i >= uNumRings) break;
      float d = length(vWorld.xz - uRings[i].xy);
      float w = uRingCol[i].w;
      float ring = 1.0 - smoothstep(0.0, w, abs(d - uRings[i].z));
      float inner = (1.0 - smoothstep(0.0, uRings[i].z + 0.01, d)) * 0.25;
      col += uRingCol[i].rgb * uRings[i].w * (ring + inner * ring * 0.0 + inner * 0.3);
    }
  }
  if (top && uCheck.w > 0.0) {
    float d = length(vWorld.xz - uCheck.xy);
    col += vec3(1.0, 0.10, 0.04) * uCheck.w * (1.0 - smoothstep(0.12, 0.6, d)) * (0.7 + 0.3 * sin(uTime * 7.0)) * 1.6;
  }
  oColor = vec4(col, 1.0);
}
)GLSL";

// ===========================================================================
const char* kFullscreenVS = R"GLSL(
out vec2 vUV;
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  vUV = p;
  gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);
}
)GLSL";

const char* kBackgroundFS = R"GLSL(
in vec2 vUV;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform samplerCube uEnvMap;
uniform float uBgLod;
uniform float uBgIntensity;
out vec4 oColor;
void main() {
  vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
  vec3 dir = normalize(w.xyz / w.w - uCamPos);
  vec3 c = textureLod(uEnvMap, dir, uBgLod).rgb;
  // keep very bright emitters from dominating the backdrop
  c = c / (1.0 + luma(c) * 0.6);
  oColor = vec4(c * uBgIntensity, 1.0);
}
)GLSL";

// ===========================================================================
const char* kEnvGenFS = R"GLSL(
in vec2 vUV;
uniform vec3 uFwd;
uniform vec3 uRight;
uniform vec3 uUp;
uniform int uIncludeKey;
uniform vec3 uKeyDir;
out vec4 oColor;

float panel(vec3 d, vec3 c, vec3 up, vec2 hs, float soft) {
  float dc = dot(d, c);
  if (dc <= 0.0) return 0.0;
  vec3 r = normalize(cross(up, c));
  vec3 u = cross(c, r);
  vec2 q = vec2(dot(d, r), dot(d, u)) / dc;
  vec2 e = abs(q) - hs;
  float dist = length(max(e, 0.0)) + min(max(e.x, e.y), 0.0);
  return 1.0 - smoothstep(-soft, soft, dist);
}

vec3 room(vec3 d) {
  float y = d.y;
  float az = atan(d.z, d.x);
  float column = floor(az / (2.0 * PI) * 30.0);
  float rnd = fract(sin(column * 12.9898) * 43758.5453);
  float shelf = fract(y * 7.0 + rnd * 0.3);
  float books = fract(sin(floor(az * 160.0) * 78.233) * 43758.5453);
  vec3 wall = vec3(0.020, 0.014, 0.010) * (0.65 + 0.7 * rnd);
  wall *= mix(0.55, 1.0, smoothstep(0.08, 0.16, shelf));
  wall *= 0.8 + 0.4 * books;
  vec3 ceilc = vec3(0.028, 0.025, 0.022);
  vec3 floorc = vec3(0.009, 0.006, 0.0045);
  vec3 c = mix(floorc, wall, smoothstep(-0.35, -0.05, y));
  c = mix(c, ceilc, smoothstep(0.50, 0.80, y));
  return c;
}

void main() {
  vec2 ndc = vUV * 2.0 - 1.0;
  vec3 d = normalize(uFwd + ndc.x * uRight + ndc.y * uUp);
  vec3 up = vec3(0.0, 1.0, 0.0);
  vec3 c = room(d);
  // cool fill from the front-right, warm rim strip behind-right, soft overhead
  c += vec3(0.78, 0.87, 1.0) * 2.0 * panel(d, normalize(vec3(0.85, 0.45, 0.55)), up, vec2(0.55, 0.32), 0.25);
  c += vec3(1.0, 0.94, 0.86) * 4.5 * panel(d, normalize(vec3(0.55, 0.40, -0.85)), up, vec2(0.8, 0.045), 0.04);
  c += vec3(1.0, 0.96, 0.90) * 1.4 * panel(d, normalize(vec3(0.05, 1.0, 0.12)), vec3(0.0, 0.0, 1.0), vec2(0.4, 0.4), 0.2);
  c += vec3(1.0, 0.62, 0.32) * 6.0 * smoothstep(0.9975, 0.9995, dot(d, normalize(vec3(-1.0, 0.16, -0.55))));
  c += vec3(1.0, 0.62, 0.32) * 6.0 * smoothstep(0.9975, 0.9995, dot(d, normalize(vec3(0.95, 0.20, -0.65))));
  c += vec3(0.33, 0.43, 0.70) * 1.0 * panel(d, normalize(vec3(-1.0, 0.35, -0.30)), up, vec2(0.22, 0.34), 0.04);
  if (uIncludeKey == 1) c += vec3(1.0, 0.94, 0.86) * 34.0 * panel(d, uKeyDir, up, vec2(0.13, 0.10), 0.03);
  oColor = vec4(c, 1.0);
}
)GLSL";

const char* kIrradianceFS = R"GLSL(
in vec2 vUV;
uniform vec3 uFwd;
uniform vec3 uRight;
uniform vec3 uUp;
uniform samplerCube uEnvMap;
out vec4 oColor;
void main() {
  vec2 ndc = vUV * 2.0 - 1.0;
  vec3 N = normalize(uFwd + ndc.x * uRight + ndc.y * uUp);
  vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 right = normalize(cross(up, N));
  up = cross(N, right);
  vec3 acc = vec3(0.0);
  float n = 0.0;
  for (float phi = 0.0; phi < 6.2831853; phi += 0.05) {
    for (float th = 0.0; th < 1.5707963; th += 0.05) {
      vec3 t = vec3(sin(th) * cos(phi), sin(th) * sin(phi), cos(th));
      vec3 sd = t.x * right + t.y * up + t.z * N;
      acc += textureLod(uEnvMap, sd, 4.0).rgb * cos(th) * sin(th);
      n += 1.0;
    }
  }
  oColor = vec4(PI * acc / n, 1.0);
}
)GLSL";

#define LM_HAMMERSLEY                                                    \
  "float vdc(uint bits) {\n"                                             \
  "  bits = (bits << 16u) | (bits >> 16u);\n"                            \
  "  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);\n" \
  "  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);\n" \
  "  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);\n" \
  "  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);\n" \
  "  return float(bits) * 2.3283064365386963e-10;\n"                     \
  "}\n"                                                                  \
  "vec2 hammersley(uint i, uint n) { return vec2(float(i) / float(n), vdc(i)); }\n" \
  "vec3 importanceGGX(vec2 Xi, vec3 N, float r) {\n"                     \
  "  float a = r * r;\n"                                                 \
  "  float phi = 2.0 * PI * Xi.x;\n"                                     \
  "  float ct = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));\n"   \
  "  float st = sqrt(1.0 - ct * ct);\n"                                  \
  "  vec3 H = vec3(cos(phi) * st, sin(phi) * st, ct);\n"                 \
  "  vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);\n" \
  "  vec3 tx = normalize(cross(up, N));\n"                               \
  "  vec3 ty = cross(N, tx);\n"                                          \
  "  return normalize(tx * H.x + ty * H.y + N * H.z);\n"                 \
  "}\n"

const char* kPrefilterFS = LM_HAMMERSLEY R"GLSL(
in vec2 vUV;
uniform vec3 uFwd;
uniform vec3 uRight;
uniform vec3 uUp;
uniform samplerCube uEnvMap;
uniform float uRoughness;
uniform float uEnvRes;
out vec4 oColor;
void main() {
  vec2 ndc = vUV * 2.0 - 1.0;
  vec3 N = normalize(uFwd + ndc.x * uRight + ndc.y * uUp);
  vec3 V = N;
  const uint SAMPLES = 384u;
  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  float a = uRoughness * uRoughness;
  for (uint i = 0u; i < SAMPLES; ++i) {
    vec2 Xi = hammersley(i, SAMPLES);
    vec3 H = importanceGGX(Xi, N, max(uRoughness, 0.02));
    vec3 L = normalize(2.0 * dot(V, H) * H - V);
    float NoL = dot(N, L);
    if (NoL > 0.0) {
      float NoH = max(dot(N, H), 0.0);
      float a2 = a * a;
      float dd = NoH * NoH * (a2 - 1.0) + 1.0;
      float D = a2 / (PI * dd * dd + 1e-7);
      float pdf = D * 0.25 + 1e-4;
      float saTexel = 4.0 * PI / (6.0 * uEnvRes * uEnvRes);
      float saSample = 1.0 / (float(SAMPLES) * pdf + 1e-4);
      float mip = uRoughness < 0.02 ? 0.0 : 0.5 * log2(saSample / saTexel) + 1.0;
      acc += textureLod(uEnvMap, L, mip).rgb * NoL;
      wsum += NoL;
    }
  }
  oColor = vec4(acc / max(wsum, 1e-4), 1.0);
}
)GLSL";

const char* kBrdfLutFS = LM_HAMMERSLEY R"GLSL(
in vec2 vUV;
out vec4 oColor;
float gSchlick(float NoV, float r) {
  float k = (r * r) / 2.0;
  return NoV / (NoV * (1.0 - k) + k);
}
void main() {
  float NoV = max(vUV.x, 1e-3);
  float r = vUV.y;
  vec3 V = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
  vec3 N = vec3(0.0, 0.0, 1.0);
  float A = 0.0, B = 0.0;
  const uint SAMPLES = 512u;
  for (uint i = 0u; i < SAMPLES; ++i) {
    vec2 Xi = hammersley(i, SAMPLES);
    vec3 H = importanceGGX(Xi, N, max(r, 0.01));
    vec3 L = normalize(2.0 * dot(V, H) * H - V);
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
  oColor = vec4(A / float(SAMPLES), B / float(SAMPLES), 0.0, 1.0);
}
)GLSL";

// ===========================================================================
const char* kSsaoFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform vec3 uKernel[16];
uniform float uRadius;
out vec4 oAO;
vec3 viewPos(vec2 uv) {
  float d = texture(uDepth, uv).r;
  vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  return p.xyz / p.w;
}
void main() {
  float d = texture(uDepth, vUV).r;
  if (d >= 0.99999) { oAO = vec4(1.0); return; }
  vec3 P = viewPos(vUV);
  vec3 N = normalize(texture(uNormal, vUV).xyz);
  float a = ign(gl_FragCoord.xy) * 6.2831853;
  vec3 rv = vec3(cos(a), sin(a), 0.0);
  vec3 T = normalize(rv - N * dot(rv, N));
  vec3 B = cross(N, T);
  mat3 TBN = mat3(T, B, N);
  float occ = 0.0;
  float radius = uRadius * clamp(-P.z / 10.0, 0.6, 1.6);
  for (int i = 0; i < 16; ++i) {
    vec3 sp = P + TBN * uKernel[i] * radius;
    vec4 o = uProj * vec4(sp, 1.0);
    vec2 suv = o.xy / o.w * 0.5 + 0.5;
    if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
    float sz = viewPos(suv).z;
    float range = smoothstep(0.0, 1.0, radius / max(abs(P.z - sz), 1e-4));
    occ += (sz >= sp.z + 0.012 ? 1.0 : 0.0) * range;
  }
  float ao = 1.0 - occ / 16.0;
  oAO = vec4(pow(ao, 1.5));
}
)GLSL";

const char* kSsaoBlurFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uAO;
uniform sampler2D uDepth;
uniform vec2 uTexel;
uniform vec2 uNearFar;
out vec4 oAO;
float linZ(float d) {
  float z = d * 2.0 - 1.0;
  return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}
void main() {
  float z0 = linZ(texture(uDepth, vUV).r);
  float acc = 0.0, w = 0.0;
  for (int y = -2; y <= 2; ++y)
    for (int x = -2; x <= 2; ++x) {
      vec2 uv = vUV + vec2(float(x), float(y)) * uTexel;
      float z = linZ(texture(uDepth, uv).r);
      float wt = exp(-abs(z - z0) * 18.0 / max(z0, 0.1) * 4.0) * exp(-float(x * x + y * y) * 0.15);
      acc += texture(uAO, uv).r * wt;
      w += wt;
    }
  oAO = vec4(acc / max(w, 1e-4));
}
)GLSL";

// ===========================================================================
const char* kBloomDownFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uSrcTexel;
uniform int uFirst;
out vec4 oColor;
vec3 S(vec2 o) { return texture(uSrc, vUV + o * uSrcTexel).rgb; }
float kw(vec3 c) { return 1.0 / (1.0 + luma(c)); }
void main() {
  vec3 a = S(vec2(-2.0, 2.0)), b = S(vec2(0.0, 2.0)), c = S(vec2(2.0, 2.0));
  vec3 d = S(vec2(-2.0, 0.0)), e = S(vec2(0.0, 0.0)), f = S(vec2(2.0, 0.0));
  vec3 g = S(vec2(-2.0, -2.0)), h = S(vec2(0.0, -2.0)), i = S(vec2(2.0, -2.0));
  vec3 j = S(vec2(-1.0, 1.0)), k = S(vec2(1.0, 1.0)), l = S(vec2(-1.0, -1.0)), m = S(vec2(1.0, -1.0));
  vec3 r;
  if (uFirst == 1) {
    vec3 g0 = (a + b + d + e) * 0.25, g1 = (b + c + e + f) * 0.25, g2 = (d + e + g + h) * 0.25;
    vec3 g3 = (e + f + h + i) * 0.25, g4 = (j + k + l + m) * 0.25;
    float w0 = kw(g0) * 0.125, w1 = kw(g1) * 0.125, w2 = kw(g2) * 0.125, w3 = kw(g3) * 0.125, w4 = kw(g4) * 0.5;
    r = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);
  } else {
    r = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
  }
  oColor = vec4(max(r, vec3(0.0)), 1.0);
}
)GLSL";

const char* kBloomUpFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uRadius;
out vec4 oColor;
void main() {
  vec2 r = uRadius;
  vec3 c = texture(uSrc, vUV).rgb * 4.0;
  c += (texture(uSrc, vUV + vec2(-r.x, 0.0)).rgb + texture(uSrc, vUV + vec2(r.x, 0.0)).rgb +
        texture(uSrc, vUV + vec2(0.0, -r.y)).rgb + texture(uSrc, vUV + vec2(0.0, r.y)).rgb) * 2.0;
  c += texture(uSrc, vUV + vec2(-r.x, -r.y)).rgb + texture(uSrc, vUV + vec2(r.x, -r.y)).rgb +
       texture(uSrc, vUV + vec2(-r.x, r.y)).rgb + texture(uSrc, vUV + vec2(r.x, r.y)).rgb;
  oColor = vec4(c / 16.0, 1.0);
}
)GLSL";

// ===========================================================================
const char* kDofCocFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uColor;
uniform sampler2D uDepth;
uniform vec2 uNearFar;
uniform float uFocus;
uniform float uAperture;
uniform float uMaxCoc;
uniform vec2 uSrcTexel;
out vec4 oColor;
float linZ(float d) {
  float z = d * 2.0 - 1.0;
  return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}
void main() {
  vec3 c = vec3(0.0);
  float coc = 0.0;
  for (int i = 0; i < 4; ++i) {
    vec2 o = vec2(float(i & 1) - 0.5, float(i >> 1) - 0.5) * uSrcTexel;
    c += texture(uColor, vUV + o).rgb;
    float z = linZ(texture(uDepth, vUV + o).r);
    coc += clamp(uAperture * (z - uFocus) / max(z, 0.01), -uMaxCoc, uMaxCoc);
  }
  oColor = vec4(c * 0.25, coc * 0.25);
}
)GLSL";

const char* kDofBokehFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;
uniform float uMaxCoc;
out vec4 oColor;
void main() {
  vec4 c0 = texture(uSrc, vUV);
  float r0 = abs(c0.a);
  vec3 acc = c0.rgb;
  float wsum = 1.0;
  const int N = 48;
  for (int i = 0; i < N; ++i) {
    float t = (float(i) + 0.5) / float(N);
    float r = sqrt(t) * uMaxCoc;
    float th = float(i) * 2.39996323;
    vec2 o = vec2(cos(th), sin(th)) * r;
    vec4 s = texture(uSrc, vUV + o * uTexel);
    float rs = abs(s.a);
    if (s.a > c0.a) rs = min(rs, r0 * 2.0);
    float w = smoothstep(r - 1.0, r + 0.5, rs);
    acc += s.rgb * w;
    wsum += w;
  }
  oColor = vec4(acc / wsum, c0.a);
}
)GLSL";

const char* kDofCompositeFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uSharp;
uniform sampler2D uBlur;
uniform sampler2D uCoc;
out vec4 oColor;
void main() {
  vec3 s = texture(uSharp, vUV).rgb;
  vec3 b = texture(uBlur, vUV).rgb;
  float coc = abs(texture(uCoc, vUV).a);
  float t = smoothstep(0.5, 2.0, coc);
  oColor = vec4(mix(s, b, t), 1.0);
}
)GLSL";

// ===========================================================================
const char* kCompositeFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform float uVignette;
uniform float uGrain;
uniform float uTime;
uniform float uFade;
uniform vec3 uFlash;
uniform int uBloomOn;
out vec4 oColor;
const mat3 ACESIn = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
const mat3 ACESOut = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
vec3 rrt(vec3 v) {
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return a / b;
}
vec3 toSRGB(vec3 c) {
  c = saturate(c);
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}
void main() {
  vec3 c = texture(uScene, vUV).rgb;
  if (uBloomOn == 1) c = mix(c, texture(uBloom, vUV).rgb, uBloomStrength);
  c += uFlash;
  c *= uExposure;
  c = ACESOut * rrt(ACESIn * c);
  vec2 q = vUV - 0.5;
  c *= mix(1.0, smoothstep(0.95, 0.15, length(q * vec2(1.25, 1.0))), uVignette);
  c = toSRGB(c);
  float n = hash12(gl_FragCoord.xy + fract(uTime * 7.31) * 931.0) - 0.5;
  c += n * (uGrain + 1.5 / 255.0);
  oColor = vec4(c * uFade, 1.0);
}
)GLSL";

// ===========================================================================
const char* kParticleVS = R"GLSL(
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aPosSize;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aVelType;
layout(location = 4) in vec4 aMisc;   // rotation, stretch, seed, life
uniform mat4 uView;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
out float vType;
out float vLife;
out float vSeed;
out vec3 vViewPos;
out float vSize;
void main() {
  vec3 vp = (uView * vec4(aPosSize.xyz, 1.0)).xyz;
  float size = aPosSize.w;
  vec2 offs;
  if (aMisc.y > 0.0) {
    vec3 vv = (uView * vec4(aVelType.xyz, 0.0)).xyz;
    vec2 dir = vv.xy;
    float l = length(dir);
    dir = l > 1e-4 ? dir / l : vec2(1.0, 0.0);
    vec2 perp = vec2(-dir.y, dir.x);
    float len = size + l * aMisc.y;
    offs = dir * aCorner.x * len + perp * aCorner.y * size;
  } else {
    float r = aMisc.x;
    mat2 R = mat2(cos(r), sin(r), -sin(r), cos(r));
    offs = R * aCorner * size;
  }
  vp.xy += offs;
  vViewPos = vp;
  vSize = size;
  vUV = aCorner;
  vColor = aColor;
  vType = aVelType.w;
  vLife = aMisc.w;
  vSeed = aMisc.z;
  gl_Position = uProj * vec4(vp, 1.0);
}
)GLSL";

const char* kParticleFS = R"GLSL(
in vec2 vUV;
in vec4 vColor;
in float vType;
in float vLife;
in float vSeed;
in vec3 vViewPos;
in float vSize;
uniform sampler2D uDepth;
uniform sampler3D uNoise;
uniform vec2 uInvScreen;
uniform vec2 uNearFar;
out vec4 oColor;
float linZ(float d) {
  float z = d * 2.0 - 1.0;
  return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}
void main() {
  float r2 = dot(vUV, vUV);
  if (r2 > 1.0) discard;
  float a;
  int type = int(vType + 0.5);
  if (type == 1) {
    float n = texture(uNoise, vec3(vUV * 0.28 + vec2(vSeed), vLife * 0.25 + vSeed)).r;
    float n2 = texture(uNoise, vec3(vUV * 0.6 + vec2(vSeed * 1.7), vLife * 0.4)).g;
    a = (1.0 - smoothstep(0.15, 1.0, r2)) * saturate(0.25 + 1.2 * n * n2 * 2.0);
  } else if (type == 2) {
    a = exp(-r2 * 3.5);
  } else if (type == 3) {
    float ring = abs(sqrt(r2) - 0.82);
    a = exp(-ring * ring * 160.0);
  } else {
    a = exp(-r2 * 4.5);
  }
  float sceneZ = linZ(texture(uDepth, gl_FragCoord.xy * uInvScreen).r);
  float fragZ = -vViewPos.z;
  a *= saturate((sceneZ - fragZ) / (vSize * 0.5 + 0.015));
  oColor = vec4(vColor.rgb * a, vColor.a * a);
}
)GLSL";

const char* kMipCopyFS = R"GLSL(
in vec2 vUV;
uniform sampler2D uSrc;
out vec4 oColor;
void main() { oColor = texture(uSrc, vUV); }
)GLSL";

}  // namespace shaders
