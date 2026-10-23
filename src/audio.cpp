#include "audio.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "music.h"

#ifdef LM_AUDIO
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#include "miniaudio.h"
#endif

namespace {

constexpr int SR = 44100;
constexpr double TAU = 6.283185307179586;

struct Rng {
  uint32_t s = 12345;
  float next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (s & 0xffffff) / float(0xffffff);
  }
  float bi() { return next() * 2.0f - 1.0f; }
};

struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void bandpass(float f, float q) {
    float w = (float)(TAU * f / SR), al = std::sin(w) / (2 * q), c = std::cos(w);
    float a0 = 1 + al;
    b0 = al / a0;
    b1 = 0;
    b2 = -al / a0;
    a1 = -2 * c / a0;
    a2 = (1 - al) / a0;
  }
  void lowpass(float f, float q = 0.707f) {
    float w = (float)(TAU * f / SR), al = std::sin(w) / (2 * q), c = std::cos(w);
    float a0 = 1 + al;
    b0 = (1 - c) / 2 / a0;
    b1 = (1 - c) / a0;
    b2 = (1 - c) / 2 / a0;
    a1 = -2 * c / a0;
    a2 = (1 - al) / a0;
  }
  void highpass(float f, float q = 0.707f) {
    float w = (float)(TAU * f / SR), al = std::sin(w) / (2 * q), c = std::cos(w);
    float a0 = 1 + al;
    b0 = (1 + c) / 2 / a0;
    b1 = -(1 + c) / a0;
    b2 = (1 + c) / 2 / a0;
    a1 = -2 * c / a0;
    a2 = (1 - al) / a0;
  }
  float run(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

using Buf = std::vector<float>;

Buf make(float seconds) { return Buf((size_t)(seconds * SR), 0.0f); }

void normalize(Buf& b, float peak) {
  float m = 1e-6f;
  for (float v : b) m = std::max(m, std::fabs(v));
  for (float& v : b) v *= peak / m;
  // tiny fade-out to avoid clicks
  size_t n = std::min<size_t>(b.size(), 256);
  for (size_t i = 0; i < n; ++i) b[b.size() - 1 - i] *= (float)i / n;
}

void addDampedSine(Buf& b, float t0, float f, float amp, float decay, float sweep = 0.0f) {
  size_t s0 = (size_t)(t0 * SR);
  double ph = 0;
  for (size_t i = s0; i < b.size(); ++i) {
    float t = (float)(i - s0) / SR;
    float env = amp * std::exp(-t * decay);
    if (env < 1e-5f) break;
    float ff = f * std::exp(-sweep * t);
    ph += TAU * ff / SR;
    b[i] += env * (float)std::sin(ph);
  }
}

void addNoise(Buf& b, Rng& r, float t0, float amp, float decay, Biquad flt, float attack = 0.001f) {
  size_t s0 = (size_t)(t0 * SR);
  for (size_t i = s0; i < b.size(); ++i) {
    float t = (float)(i - s0) / SR;
    float env = amp * std::exp(-t * decay) * std::min(1.0f, t / attack);
    if (t > 0.01f && env < 1e-5f) break;
    b[i] += env * flt.run(r.bi());
  }
}

void synthMedieval(std::vector<Buf>& S);

std::vector<Buf> synthAll() {
  std::vector<Buf> S(SFX_COUNT);
  Rng r;
  {  // STEP
    Buf b = make(0.14f);
    addDampedSine(b, 0, 150, 1.0f, 38, 3.0f);
    Biquad f;
    f.bandpass(1600, 1.2f);
    addNoise(b, r, 0, 0.9f, 90, f);
    normalize(b, 0.7f);
    S[SFX_STEP] = b;
  }
  {  // THUD
    Buf b = make(0.45f);
    addDampedSine(b, 0, 78, 1.0f, 11, 1.6f);
    Biquad f;
    f.lowpass(420);
    addNoise(b, r, 0, 1.4f, 16, f);
    Biquad c;
    c.bandpass(1800, 1.0f);
    addNoise(b, r, 0, 0.4f, 120, c);
    normalize(b, 0.9f);
    S[SFX_THUD] = b;
  }
  {  // CLACK
    Buf b = make(0.3f);
    addDampedSine(b, 0, 1850, 0.8f, 38);
    addDampedSine(b, 0, 2930, 0.6f, 52);
    addDampedSine(b, 0, 4150, 0.4f, 70);
    addDampedSine(b, 0, 640, 0.5f, 30);
    Biquad f;
    f.bandpass(3000, 0.8f);
    addNoise(b, r, 0, 1.0f, 140, f);
    normalize(b, 0.9f);
    S[SFX_CLACK] = b;
  }
  {  // SHATTER
    Buf b = make(1.4f);
    Biquad f;
    f.lowpass(2500);
    addNoise(b, r, 0, 1.2f, 9, f);
    addDampedSine(b, 0, 95, 0.8f, 14, 1.0f);
    for (int i = 0; i < 90; ++i) {
      float t = -std::log(1.0f - r.next() * 0.995f) * 0.16f;
      float fr = 1800 + r.next() * 5500, a = 0.25f + 0.35f * r.next(), d = 120 + r.next() * 120;  // in this order
      addDampedSine(b, t, fr, a, d);
    }
    normalize(b, 0.95f);
    S[SFX_SHATTER] = b;
  }
  {  // WHOOSH
    Buf b = make(0.5f);
    Biquad f;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / b.size();
      float fc = 450.0f + 2200.0f * std::sin(t * 3.14159f);
      if (i % 32 == 0) f.bandpass(fc, 1.6f);
      float env = std::pow(std::sin(t * 3.14159f), 2.0f);
      b[i] = f.run(r.bi()) * env;
    }
    normalize(b, 0.8f);
    S[SFX_WHOOSH] = b;
  }
  {  // CLANG
    Buf b = make(2.2f);
    const float ratio[6] = {1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 1.5f};
    const float amp[6] = {1.0f, 0.6f, 0.45f, 0.28f, 0.16f, 0.3f};
    const float dec[6] = {2.5f, 3.5f, 5.0f, 7.0f, 10.0f, 4.0f};
    for (int i = 0; i < 6; ++i) addDampedSine(b, 0, 455 * ratio[i], amp[i], dec[i]);
    Biquad f;
    f.highpass(2500);
    addNoise(b, r, 0, 1.5f, 60, f);
    normalize(b, 0.9f);
    S[SFX_CLANG] = b;
  }
  {  // BOOM
    Buf b = make(2.4f);
    addDampedSine(b, 0, 85, 1.0f, 2.6f, 0.9f);
    Biquad f;
    f.lowpass(320);
    addNoise(b, r, 0, 2.2f, 3.2f, f);
    Biquad c;
    c.highpass(1500);
    addNoise(b, r, 0, 1.0f, 40, c);
    normalize(b, 1.0f);
    S[SFX_BOOM] = b;
  }
  {  // EXPLODE
    Buf b = make(2.6f);
    Biquad f;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      if (i % 64 == 0) f.lowpass(200.0f + 1400.0f * std::exp(-t * 3.0f));
      b[i] = f.run(r.bi()) * 2.0f * std::exp(-t * 2.8f) * std::min(1.0f, t / 0.004f);
    }
    addDampedSine(b, 0, 55, 0.9f, 4.0f, 0.6f);
    for (int i = 0; i < 40; ++i) {
      float t = r.next() * 1.2f;
      addDampedSine(b, t, 1500 + r.next() * 4000, 0.15f * std::exp(-t * 2.0f), 150);
    }
    normalize(b, 1.0f);
    S[SFX_EXPLODE] = b;
  }
  {  // CHARGE
    Buf b = make(1.3f);
    double ph = 0, pm = 0;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      float u = t / 1.3f;
      float fc = 260.0f + 780.0f * u * u;
      pm += TAU * fc * 1.5 / SR;
      float idx = 2.5f * (1.0f - u);
      ph += TAU * fc / SR + idx * std::sin(pm) * TAU * 3.0 / SR;
      float env = std::min(1.0f, t / 0.3f) * (1.0f - std::pow(u, 6.0f));
      float trem = 0.75f + 0.25f * std::sin(t * 30.0f * (1.0f + u));
      b[i] = (float)(std::sin(ph) + 0.35 * std::sin(ph * 2.01) + 0.2 * std::sin(ph * 3.02)) * env * trem;
    }
    normalize(b, 0.6f);
    S[SFX_CHARGE] = b;
  }
  {  // ZAP
    Buf b = make(0.6f);
    double ph = 0;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      float f = 300.0f + 1700.0f * std::exp(-t * 9.0f);
      ph += TAU * f / SR;
      float sq = std::sin(ph) > 0 ? 1.0f : -1.0f;
      b[i] = (0.6f * (float)std::sin(ph) + 0.25f * sq + 0.3f * r.bi()) * std::exp(-t * 6.0f);
    }
    Biquad f;
    f.lowpass(5000);
    for (float& v : b) v = f.run(v);
    normalize(b, 0.75f);
    S[SFX_ZAP] = b;
  }
  {  // CRACKLE
    Buf b = make(0.5f);
    Biquad f;
    f.bandpass(4200, 0.7f);
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      float x = r.next() < 0.03f ? r.bi() * 3.0f : r.bi() * 0.15f;
      float buzz = std::sin(t * TAU * 120.0) > 0 ? 0.15f : -0.15f;
      b[i] = (f.run(x) + buzz * 0.4f) * std::exp(-t * 5.0f);
    }
    normalize(b, 0.7f);
    S[SFX_CRACKLE] = b;
  }
  {  // CHIME
    Buf b = make(2.0f);
    const float notes[3] = {1046.5f, 1318.5f, 1568.0f};
    for (int n = 0; n < 3; ++n) {
      float t0 = n * 0.09f;
      addDampedSine(b, t0, notes[n], 0.6f, 2.6f);
      addDampedSine(b, t0, notes[n] * 2.41f, 0.25f, 4.0f);
      addDampedSine(b, t0, notes[n] * 3.89f, 0.12f, 6.0f);
    }
    normalize(b, 0.6f);
    S[SFX_CHIME] = b;
  }
  {  // SELECT
    Buf b = make(0.09f);
    addDampedSine(b, 0, 880, 0.8f, 80);
    addDampedSine(b, 0, 1720, 0.4f, 110);
    normalize(b, 0.45f);
    S[SFX_SELECT] = b;
  }
  {  // CHECK (two dramatic horn notes)
    Buf b = make(1.0f);
    Biquad f;
    f.lowpass(1500);
    double ph = 0;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      float freq = t < 0.28f ? 392.0f : 523.25f;
      float lt = t < 0.28f ? t : t - 0.28f;
      float len = t < 0.28f ? 0.28f : 0.72f;
      float env = std::min(1.0f, lt / 0.02f) * std::max(0.0f, 1.0f - lt / len);
      ph += TAU * freq * (1.0 + 0.004 * std::sin(t * 30.0)) / SR;
      float saw = (float)(2.0 * (ph / TAU - std::floor(ph / TAU + 0.5)));
      b[i] = f.run(saw) * env;
    }
    normalize(b, 0.55f);
    S[SFX_CHECK] = b;
  }
  {  // MATE (gong)
    Buf b = make(4.5f);
    const float ratio[6] = {1.0f, 1.47f, 2.09f, 2.56f, 3.11f, 4.23f};
    const float dec[6] = {0.7f, 0.9f, 1.2f, 1.5f, 1.9f, 2.6f};
    for (int i = 0; i < 6; ++i) addDampedSine(b, 0, 92 * ratio[i], 1.0f / (1 + i * 0.4f), dec[i]);
    Biquad f;
    f.lowpass(900);
    addNoise(b, r, 0, 0.8f, 8, f);
    normalize(b, 0.9f);
    S[SFX_MATE] = b;
  }
  {  // DEBRIS
    Buf b = make(0.16f);
    addDampedSine(b, 0, 2300, 0.7f, 70);
    addDampedSine(b, 0, 3650, 0.4f, 90);
    addDampedSine(b, 0, 900, 0.3f, 60);
    Biquad f;
    f.bandpass(2800, 1.0f);
    addNoise(b, r, 0, 0.5f, 150, f);
    normalize(b, 0.6f);
    S[SFX_DEBRIS] = b;
  }
  {  // RUMBLE
    Buf b = make(1.6f);
    Biquad f;
    f.lowpass(140);
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      float env = std::sin(std::min(1.0f, t / 1.6f) * 3.14159f);
      b[i] = (f.run(r.bi()) * 3.0f + 0.4f * (float)std::sin(TAU * 42.0 * t)) * env * (0.8f + 0.2f * std::sin(t * 23.0f));
    }
    normalize(b, 0.8f);
    S[SFX_RUMBLE] = b;
  }
  return S;
}

// ---------------------------------------------------------------------------
// The medieval army's sounds
// ---------------------------------------------------------------------------
// A large stone hall: Freeverb-style parallel combs and series allpasses.
void reverb(Buf& b, float wet, float feedback, float damp, float size) {
  static const int combLen[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
  static const int apLen[4] = {556, 441, 341, 225};
  std::vector<std::vector<float>> comb(8), ap(4);
  std::vector<float> store(8, 0.0f);
  std::vector<size_t> ci(8, 0), ai(4, 0);
  for (int k = 0; k < 8; ++k) comb[k].assign((size_t)(combLen[k] * size), 0.0f);
  for (int k = 0; k < 4; ++k) ap[k].assign((size_t)(apLen[k] * size), 0.0f);
  for (size_t i = 0; i < b.size(); ++i) {
    float x = b[i] * 0.015f, y = 0;
    for (int k = 0; k < 8; ++k) {
      float o = comb[k][ci[k]];
      store[k] = o * (1 - damp) + store[k] * damp;
      comb[k][ci[k]] = x + store[k] * feedback;
      if (++ci[k] >= comb[k].size()) ci[k] = 0;
      y += o;
    }
    for (int k = 0; k < 4; ++k) {
      float o = ap[k][ai[k]];
      ap[k][ai[k]] = y + o * 0.5f;
      y = o - y;
      if (++ai[k] >= ap[k].size()) ai[k] = 0;
    }
    b[i] = b[i] * (1 - wet * 0.5f) + y * wet * 3.0f;
  }
}

// a long ringing partial (exponential decay), computed with a rotating phasor
void addRing(Buf& b, float t0, float f, float amp, float decay) {
  size_t s0 = (size_t)(t0 * SR);
  double c = std::cos(TAU * f / SR), sn = std::sin(TAU * f / SR);
  double x = 0, y = 1;  // phasor (sin, cos)
  double env = amp, k = std::exp(-decay / (double)SR);
  for (size_t i = s0; i < b.size(); ++i) {
    b[i] += (float)(env * x);
    double nx = x * c + y * sn, ny = y * c - x * sn;
    x = nx;
    y = ny;
    env *= k;
    if (env < 1e-5) break;
  }
}

float envAR(float t, float a, float hold, float r) {
  if (t < 0) return 0;
  if (t < a) return t / a;
  if (t < a + hold) return 1;
  return std::max(0.0f, 1.0f - (t - a - hold) / r);
}

// A pipe organ note: principal + octave + mixture ranks, a breath of wind
// noise and the "chiff" of the pipe speaking. 'reed' adds a trumpet stop.
void organNote(Buf& b, Rng& r, float t0, float dur, float f, float amp, float reed) {
  static const float H[12] = {1.0f, 0.75f, 0.42f, 0.5f, 0.22f, 0.3f, 0.1f, 0.22f, 0.08f, 0.12f, 0.05f, 0.09f};
  // one period of the registration as a wavetable, played twice (slightly detuned) for the chorus of ranks
  static constexpr int N = 2048;  // static, so the lambda below can use it uncaptured (MSVC)
  std::vector<float> table(N + 1, 0.0f);
  for (int k = 0; k < 12; ++k) {
    if (f * (k + 1) > 9000.0f) break;
    float a = H[k] * (1.0f + reed * (k % 2 == 0 ? 0.6f : 1.4f) * (k > 0 ? 1.0f : 0.0f));
    for (int i = 0; i < N; ++i) table[i] += a * (float)std::sin(TAU * (k + 1) * i / N);
  }
  table[N] = table[0];
  size_t s0 = (size_t)(t0 * SR), n = (size_t)((dur + 0.25f) * SR);
  double p0 = 0, p1 = 0.37;
  const double d0 = f / (double)SR, d1 = f * 1.0007 / (double)SR;
  Biquad chiff;
  chiff.bandpass(std::min(6000.0f, f * 3.0f), 3.0f);
  Biquad windF;
  windF.bandpass(std::min(5000.0f, f * 2.0f), 1.2f);
  auto look = [&table](double ph) {
    double x = ph * N;
    int i = (int)x;
    float fr = (float)(x - i);
    return table[i] + (table[i + 1] - table[i]) * fr;
  };
  for (size_t i = 0; i < n && s0 + i < b.size(); ++i) {
    float t = (float)i / SR;
    float env = envAR(t, 0.045f, dur - 0.045f, 0.18f);
    if (env <= 0 && t > dur) break;
    p0 += d0;
    if (p0 >= 1.0) p0 -= 1.0;
    p1 += d1;
    if (p1 >= 1.0) p1 -= 1.0;
    float v = look(p0) + 0.6f * look(p1);
    float wind = windF.run(r.bi()) * 0.012f;
    float ch = t < 0.06f ? chiff.run(r.bi()) * 0.5f * (1.0f - t / 0.06f) : 0.0f;
    b[s0 + i] += (v * 0.2f + wind + ch) * env * amp;
  }
}

// A men's choir singing "ah": detuned PolyBLEP sawtooth voices with vibrato
// through vowel formants.
float polyblep(double t, double dt) {
  if (t < dt) {
    t /= dt;
    return (float)(t + t - t * t - 1.0);
  }
  if (t > 1.0 - dt) {
    t = (t - 1.0) / dt;
    return (float)(t * t + t + t + 1.0);
  }
  return 0.0f;
}
void choirNote(Buf& b, Rng& r, float t0, float dur, float f, float amp, int voices) {
  long s0 = (long)(t0 * SR);  // may start just before the buffer
  size_t n = (size_t)((dur + 0.4f) * SR);
  std::vector<float> tmp(n, 0.0f);
  for (int v = 0; v < voices; ++v) {
    double ph = r.next();
    float detune = std::pow(2.0f, (r.next() - 0.5f) * 18.0f / 1200.0f);
    float vibPh = r.next() * 6.28f, vibRate = 5.0f + r.next() * 1.2f;
    float drift = 0;
    for (size_t i = 0; i < n; ++i) {
      float t = (float)i / SR;
      drift += (r.bi() * 0.02f - drift * 0.001f);
      float fr = f * detune * (1.0f + drift * 0.000578f) *
                 (1.0f + 0.006f * std::sin(t * vibRate * 6.2831f + vibPh) * std::min(1.0f, t / 0.4f));
      double dt = fr / SR;
      ph += dt;
      if (ph >= 1.0) ph -= 1.0;
      float saw = (float)(2.0 * ph - 1.0) - polyblep(ph, dt);
      tmp[i] += saw * envAR(t, 0.14f + 0.04f * r.next(), dur - 0.14f, 0.35f);
    }
  }
  Biquad f1, f2, f3, f4;
  f1.bandpass(730, 7.0f);
  f2.bandpass(1090, 9.0f);
  f3.bandpass(2440, 14.0f);
  f4.lowpass(3500);
  for (size_t i = 0; i < n && s0 + (long)i < (long)b.size(); ++i) {
    float x = tmp[i];
    float y = f4.run(f1.run(x) * 1.0f + f2.run(x) * 0.55f + f3.run(x) * 0.22f);
    if (s0 + (long)i >= 0) b[s0 + i] += y * amp / (float)voices;
  }
}

// filtered noise with an envelope given by a function
template <class EnvF>
void noiseShaped(Buf& b, Rng& r, float t0, float dur, Biquad flt, float amp, EnvF env) {
  size_t s0 = (size_t)(t0 * SR), n = (size_t)(dur * SR);
  for (size_t i = 0; i < n && s0 + i < b.size(); ++i) b[s0 + i] += flt.run(r.bi()) * env((float)i / SR) * amp;
}

void synthMedieval(std::vector<Buf>& S) {
  Rng r;
  r.s = 777;
  {  // HYMN: Dies irae - "Di-es i-rae, di-es il-la" - organ over a drone, a men's choir, a cathedral
    Buf b = make(7.6f);
    const float D3 = 146.83f, C3 = 130.81f, E3 = 164.81f, F3 = 174.61f;
    const float mel[8] = {F3, E3, F3, D3, E3, C3, D3, D3};
    const float step = 0.5f;
    // open-fifth drone (D and A) swelling underneath
    organNote(b, r, 0.0f, 4.9f, 73.42f, 0.55f, 0.0f);
    organNote(b, r, 0.25f, 4.65f, 110.0f, 0.35f, 0.0f);
    for (int i = 0; i < 8; ++i) {
      float t0 = i * step;
      float dur = i == 7 ? 1.4f : step * 0.92f;
      if (i == 7) t0 = 7 * step;
      if (i == 6) dur = step * 0.95f;
      organNote(b, r, t0, dur, mel[i] * 2.0f, 0.75f, 0.0f);
      organNote(b, r, t0, dur, mel[i], 0.5f, 0.0f);
      choirNote(b, r, t0 - 0.02f, dur + 0.05f, mel[i], 2.2f, 6);
    }
    // the final D: full organ with reeds and a deep pedal
    float tf = 7 * step;
    for (float f : {36.71f, 73.42f, 146.83f, 220.0f, 293.66f, 349.23f, 440.0f})
      organNote(b, r, tf, 1.4f, f, f < 80.0f ? 0.4f : 0.15f, 0.8f);
    choirNote(b, r, tf, 1.45f, 110.0f, 0.9f, 5);
    reverb(b, 0.55f, 0.9f, 0.25f, 1.6f);
    normalize(b, 0.9f);
    S[SFX_HYMN] = b;
  }
  {  // ORGAN: one dark chord with reeds (D minor with a menacing flat sixth)
    Buf b = make(5.0f);
    for (float f : {36.71f, 73.42f, 146.83f, 174.61f, 220.0f, 233.08f, 293.66f})
      organNote(b, r, 0.0f, 2.8f, f, f < 80.0f ? 0.8f : 0.4f, 1.0f);
    choirNote(b, r, 0.05f, 2.7f, 146.83f, 1.0f, 5);
    choirNote(b, r, 0.05f, 2.7f, 220.0f, 0.7f, 5);
    reverb(b, 0.55f, 0.9f, 0.25f, 1.6f);
    normalize(b, 0.85f);
    S[SFX_ORGAN] = b;
  }
  {  // THUNDER: a splitting crack, a boom, then the long roll
    Buf b = make(5.0f);
    Biquad hp;
    hp.highpass(1800);
    for (int k = 0; k < 7; ++k) {  // the crack: a few ragged bursts
      float t0 = r.next() * 0.09f;
      noiseShaped(b, r, t0, 0.06f, hp, 0.9f * (1.0f - k * 0.1f), [](float t) { return std::exp(-t * 60.0f); });
    }
    Biquad bp;
    bp.bandpass(900, 0.8f);
    noiseShaped(b, r, 0.0f, 0.4f, bp, 1.4f, [](float t) { return std::exp(-t * 9.0f); });
    Biquad lp;
    lp.lowpass(170);
    noiseShaped(b, r, 0.02f, 1.5f, lp, 6.0f, [](float t) { return std::min(1.0f, t / 0.02f) * std::exp(-t * 2.2f); });
    addDampedSine(b, 0.02f, 52, 0.9f, 2.5f, 0.3f);
    // the roll: rumbling swells that die away
    Biquad lp2;
    lp2.lowpass(260);
    float rolls[5] = {0.5f, 1.1f, 1.6f, 2.4f, 3.1f};
    for (int k = 0; k < 5; ++k) {
      float a = 4.8f * std::exp(-k * 0.4f);
      noiseShaped(b, r, rolls[k], 1.8f, lp2, a,
                  [](float t) { return std::sin(std::min(1.0f, t / 1.8f) * 3.14159f) * (0.7f + 0.3f * std::sin(t * 17.0f)); });
    }
    reverb(b, 0.3f, 0.86f, 0.4f, 1.3f);
    normalize(b, 1.0f);
    S[SFX_THUNDER] = b;
  }
  {  // WIND: gusting, whistling storm wind
    Buf b = make(3.6f);
    Biquad f, w;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      if (i % 64 == 0) {
        f.bandpass(380.0f + 420.0f * (0.5f + 0.5f * std::sin(t * 1.7f)) + 150.0f * std::sin(t * 4.3f), 1.4f);
        w.bandpass(1200.0f + 500.0f * std::sin(t * 0.9f + 1.0f), 22.0f);
      }
      float gust = 0.55f + 0.45f * std::sin(t * 2.3f) * std::sin(t * 0.7f + 0.4f);
      float env = std::sin(std::min(1.0f, t / 3.6f) * 3.14159f);
      float x = r.bi();
      b[i] = (f.run(x) * 1.0f + w.run(x) * 0.6f) * gust * env;
    }
    normalize(b, 0.7f);
    S[SFX_WIND] = b;
  }
  {  // ROAR: a growling throat through a shifting vowel, breath and a sub-bass rumble
    Buf b = make(2.4f);
    double ph = 0;
    float jitter = 0;
    Biquad fA, fB, fC, br, sub;
    sub.lowpass(90);
    br.bandpass(1300, 0.7f);
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      float u = t / 2.2f;
      float pitch = 75.0f + 45.0f * std::sin(std::min(1.0f, u * 1.6f) * 3.14159f) - 20.0f * u;
      jitter += (r.bi() * 0.6f - jitter * 0.02f);
      ph += TAU * pitch * (1.0f + 0.012f * jitter) / SR;
      float rattle = 0.6f + 0.4f * std::sin(t * TAU * 28.0f + std::sin(t * 13.0f));
      float saw = (float)(2.0 * (ph / TAU - std::floor(ph / TAU)) - 1.0);
      float x = saw * rattle;
      if (i % 64 == 0) {
        float open = std::sin(std::min(1.0f, u * 1.3f) * 3.14159f);
        fA.bandpass(450.0f + 350.0f * open, 5.0f);
        fB.bandpass(900.0f + 400.0f * open, 6.0f);
        fC.bandpass(2300.0f, 8.0f);
      }
      float voice = fA.run(x) * 1.0f + fB.run(x) * 0.7f + fC.run(x) * 0.25f;
      float breath = br.run(r.bi()) * 0.6f;
      float env = envAR(t, 0.15f, 1.3f, 0.75f);
      float s2 = std::tanh((voice * 2.5f + breath) * 1.5f) + sub.run(r.bi()) * 2.0f + 0.4f * std::sin(t * TAU * 42.0f);
      b[i] = s2 * env;
    }
    Biquad dark1, dark2;  // a throat this big has no hiss
    dark1.lowpass(2600);
    dark2.lowpass(2600);
    for (float& v : b) v = dark2.run(dark1.run(v));
    reverb(b, 0.2f, 0.82f, 0.4f, 1.0f);
    normalize(b, 0.95f);
    S[SFX_ROAR] = b;
  }
  {  // FIRE: the roar of a jet of flame, crackling
    Buf b = make(2.8f);
    Biquad lp, bp, low;
    lp.lowpass(2400);
    bp.bandpass(700, 0.6f);
    low.lowpass(160);
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      float env = envAR(t, 0.08f, 1.8f, 0.9f);
      float flutter = 0.7f + 0.3f * std::sin(t * 61.0f + std::sin(t * 7.0f) * 3.0f);
      float x = r.bi();
      float crack = r.next() < 0.0015f ? r.bi() * 6.0f : 0.0f;
      b[i] = (lp.run(x) * 0.7f + bp.run(x) * 1.2f + low.run(x) * 2.5f) * flutter * env + crack * env;
    }
    normalize(b, 0.85f);
    S[SFX_FIRE] = b;
  }
  {  // CHAIN: a creak of timber, then the chain running out link by link
    Buf b = make(1.9f);
    double ph = 0;
    Biquad cr;
    cr.bandpass(900, 4.0f);
    for (size_t i = 0; i < (size_t)(0.45f * SR); ++i) {  // the creak
      float t = (float)i / SR;
      float f = 180.0f + 60.0f * std::sin(t * 9.0f) + 40.0f * r.bi() * 0.1f;
      ph += TAU * f / SR;
      float saw = (float)(2.0 * (ph / TAU - std::floor(ph / TAU)) - 1.0);
      float stick = std::sin(t * TAU * 23.0f) > 0.2f ? 1.0f : 0.3f;
      b[i] += cr.run(saw) * stick * std::sin(t / 0.45f * 3.14159f) * 0.8f;
    }
    float t = 0.2f;
    float gap = 0.09f;
    while (t < 1.6f) {  // links clinking, accelerating as the bridge falls
      for (int k = 0; k < 3; ++k) {
        // draws in a fixed order (argument evaluation order differs between compilers)
        float dt = r.next() * 0.008f, f = 2400 + r.next() * 3500, a = 0.3f + 0.3f * r.next(), d = 90 + r.next() * 80;
        addDampedSine(b, t + dt, f, a, d);
      }
      Biquad h;
      h.highpass(3000);
      addNoise(b, r, t, 0.35f, 200, h);
      t += gap;
      gap = std::max(0.028f, gap * 0.93f);
    }
    normalize(b, 0.8f);
    S[SFX_CHAIN] = b;
  }
  {  // GRIND: stone dragged over stone
    Buf b = make(1.6f);
    Biquad bp, lp;
    bp.bandpass(320, 0.9f);
    lp.lowpass(900);
    float grain = 0;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      if (i % 600 == 0) grain = 0.4f + 0.6f * r.next();
      float env = envAR(t, 0.1f, 1.1f, 0.4f);
      float x = r.bi();
      float crunch = r.next() < 0.002f ? r.bi() * 3.0f : 0.0f;
      b[i] = (bp.run(x) * 1.5f + lp.run(x) * 0.6f) * grain * env + crunch * env;
    }
    normalize(b, 0.75f);
    S[SFX_GRIND] = b;
  }
  {  // STOMP: a giant stone foot on the board
    Buf b = make(1.1f);
    addDampedSine(b, 0, 44, 1.2f, 6.0f, 0.5f);
    addDampedSine(b, 0, 88, 0.6f, 12.0f, 0.8f);
    Biquad lp, bp;
    lp.lowpass(300);
    addNoise(b, r, 0, 2.2f, 9, lp);
    bp.bandpass(1800, 1.0f);
    addNoise(b, r, 0, 0.8f, 40, bp);
    for (int i = 0; i < 12; ++i) {
      float t = 0.02f + r.next() * 0.3f, f = 1500 + r.next() * 3000;
      addDampedSine(b, t, f, 0.15f, 120);
    }
    normalize(b, 1.0f);
    S[SFX_STOMP] = b;
  }
  {  // HOOF: a shod hoof knocking on wood (a quick double strike)
    Buf b = make(0.2f);
    for (int k = 0; k < 2; ++k) {
      float t0 = k * 0.018f, a = k == 0 ? 1.0f : 0.6f;
      addDampedSine(b, t0, 380, a, 60);
      addDampedSine(b, t0, 1050, a * 0.5f, 90);
      addDampedSine(b, t0, 2600, a * 0.25f, 140);
      Biquad bp;
      bp.bandpass(2000, 1.0f);
      addNoise(b, r, t0, a * 0.8f, 160, bp);
    }
    normalize(b, 0.7f);
    S[SFX_HOOF] = b;
  }
  {  // NEIGH: a whinny - a high trilling voice sliding down, ending in a snort
    Buf b = make(1.5f);
    double ph = 0;
    Biquad f1, f2, f3, nb;
    f1.bandpass(900, 3.0f);
    f2.bandpass(1700, 4.0f);
    f3.bandpass(3000, 5.0f);
    nb.bandpass(1400, 1.0f);
    for (size_t i = 0; i < (size_t)(1.15f * SR); ++i) {
      float t = (float)i / SR;
      float u = t / 1.15f;
      float base = u < 0.12f ? 520.0f + 4500.0f * u : 1060.0f - 640.0f * (u - 0.12f) / 0.88f;
      float trill = 1.0f + (0.06f + 0.06f * u) * std::sin(t * TAU * (17.0f - 5.0f * u));
      ph += TAU * base * trill / SR;
      float saw = (float)(2.0 * (ph / TAU - std::floor(ph / TAU)) - 1.0);
      float env = envAR(t, 0.04f, 0.85f, 0.26f) * (0.75f + 0.25f * std::sin(t * TAU * (17.0f - 5.0f * u)));
      float v = f1.run(saw) * 1.0f + f2.run(saw) * 0.7f + f3.run(saw) * 0.3f + nb.run(r.bi()) * 0.35f;
      b[i] += v * env;
    }
    Biquad sn;
    sn.bandpass(500, 0.8f);
    noiseShaped(b, r, 1.18f, 0.3f, sn, 1.4f, [](float t) { return std::min(1.0f, t / 0.02f) * std::exp(-t * 14.0f); });
    reverb(b, 0.15f, 0.8f, 0.5f, 0.8f);
    normalize(b, 0.8f);
    S[SFX_NEIGH] = b;
  }
  {  // BELL: a great church bell (hum, prime, minor tierce, quint, nominal ...)
    Buf b = make(6.5f);
    const float prime = 147.0f;
    const float ratio[8] = {0.5f, 1.0f, 1.2f, 1.5f, 2.0f, 2.5f, 3.0f, 4.2f};
    const float amp[8] = {0.7f, 0.8f, 0.6f, 0.3f, 0.7f, 0.25f, 0.3f, 0.15f};
    const float dec[8] = {0.45f, 0.7f, 0.9f, 1.3f, 1.1f, 2.0f, 2.2f, 3.5f};
    for (int k = 0; k < 8; ++k) {
      addRing(b, 0, prime * ratio[k], amp[k], dec[k]);
      addRing(b, 0, prime * ratio[k] * 1.0035f, amp[k] * 0.6f, dec[k] * 1.1f);  // beating doublet
    }
    Biquad strike;
    strike.bandpass(2500, 1.0f);
    addNoise(b, r, 0, 0.6f, 80, strike);
    reverb(b, 0.4f, 0.88f, 0.3f, 1.5f);
    normalize(b, 0.85f);
    S[SFX_BELL] = b;
  }
  {  // SPLINTER: a lance bursting into splinters
    Buf b = make(1.0f);
    Biquad bp;
    bp.bandpass(2800, 0.9f);
    addNoise(b, r, 0, 2.0f, 45, bp);
    addDampedSine(b, 0, 210, 0.8f, 25);
    addDampedSine(b, 0, 520, 0.5f, 35);
    for (int i = 0; i < 45; ++i) {
      float t = -std::log(1.0f - r.next() * 0.99f) * 0.12f;
      Biquad c;
      c.bandpass(1200 + r.next() * 3000, 2.0f);
      addNoise(b, r, t, 0.3f + 0.4f * r.next(), 250, c);
      if (r.next() < 0.4f) addDampedSine(b, t, 400 + r.next() * 900, 0.2f, 60);
    }
    normalize(b, 0.9f);
    S[SFX_SPLINTER] = b;
  }
  {  // ARMOR: a booted step with the jingle of mail
    Buf b = make(0.32f);
    addDampedSine(b, 0, 130, 0.8f, 40, 2.0f);
    Biquad lp;
    lp.lowpass(1200);
    addNoise(b, r, 0, 0.6f, 60, lp);
    for (int i = 0; i < 10; ++i) {
      float t = 0.01f + r.next() * 0.12f, f = 4000 + r.next() * 5000, a = 0.08f + 0.08f * r.next(),
            d = 140 + r.next() * 80;
      addDampedSine(b, t, f, a, d);
    }
    normalize(b, 0.6f);
    S[SFX_ARMOR] = b;
  }
  {  // SLIDE: a heavy wooden throne sliding over the board
    Buf b = make(1.3f);
    Biquad bp, res;
    bp.bandpass(800, 0.9f);
    res.bandpass(240, 6.0f);
    float stick = 0;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      if (i % 900 == 0) stick = 0.5f + 0.5f * r.next();
      float chatter = 0.6f + 0.4f * std::sin(t * TAU * (38.0f + 10.0f * std::sin(t * 3.0f)));
      float env = envAR(t, 0.08f, 0.85f, 0.35f);
      float x = r.bi();
      b[i] = (bp.run(x) * 1.0f + res.run(x) * 0.8f) * chatter * stick * env;
    }
    normalize(b, 0.6f);
    S[SFX_SLIDE] = b;
  }
  {  // BITE: jaws snapping shut on stone
    Buf b = make(0.8f);
    addDampedSine(b, 0, 1900, 0.6f, 120);
    Biquad hp;
    hp.highpass(1500);
    addNoise(b, r, 0, 1.0f, 90, hp);
    addDampedSine(b, 0.01f, 70, 1.0f, 14, 0.8f);
    for (int i = 0; i < 40; ++i) {
      float t = 0.02f + -std::log(1.0f - r.next() * 0.99f) * 0.09f;
      float f = 1500 + r.next() * 4500, a = 0.15f + 0.25f * r.next(), d = 140 + r.next() * 120;
      addDampedSine(b, t, f, a, d);
    }
    Biquad lp;
    lp.lowpass(600);
    addNoise(b, r, 0.02f, 1.2f, 18, lp);
    normalize(b, 0.95f);
    S[SFX_BITE] = b;
  }
}

// ---------------------------------------------------------------------------
// The aliens' sounds
// ---------------------------------------------------------------------------
// a sine whose frequency follows freq(t), with envelope env(t), added at t0
template <class FreqF, class EnvF>
void addSweep(Buf& b, float t0, float dur, FreqF freq, EnvF env) {
  size_t s0 = (size_t)(t0 * SR), n = (size_t)(dur * SR);
  double ph = 0;
  for (size_t i = 0; i < n && s0 + i < b.size(); ++i) {
    float t = (float)i / SR;
    ph += TAU * freq(t) / SR;
    b[s0 + i] += (float)std::sin(ph) * env(t);
  }
}

// a band-limited sawtooth (PolyBLEP) following freq(t)
template <class FreqF, class EnvF>
void addSaw(Buf& b, float t0, float dur, FreqF freq, EnvF env) {
  size_t s0 = (size_t)(t0 * SR), n = (size_t)(dur * SR);
  double ph = 0;
  for (size_t i = 0; i < n && s0 + i < b.size(); ++i) {
    float t = (float)i / SR;
    double dt = freq(t) / SR;
    ph += dt;
    if (ph >= 1.0) ph -= 1.0;
    b[s0 + i] += ((float)(2.0 * ph - 1.0) - polyblep(ph, dt)) * env(t);
  }
}

void synthAlien(std::vector<Buf>& S) {
  Rng r;
  r.s = 4242;
  {  // PEW: a falling laser chirp with a click
    Buf b = make(0.4f);
    addSweep(b, 0, 0.4f, [](float t) { return 320.0f + 1650.0f * std::exp(-t * 13.0f); },
             [](float t) { return std::exp(-t * 8.0f) * std::min(1.0f, t / 0.003f); });
    addSweep(b, 0, 0.4f, [](float t) { return 2.0f * (320.0f + 1650.0f * std::exp(-t * 13.0f)); },
             [](float t) { return 0.35f * std::exp(-t * 11.0f) * std::min(1.0f, t / 0.003f); });
    Biquad hp;
    hp.highpass(3000);
    addNoise(b, r, 0, 0.4f, 120, hp);
    normalize(b, 0.7f);
    S[SFX_PEW] = b;
  }
  {  // UFO: the classic theremin warble, with a hum underneath
    Buf b = make(2.4f);
    auto env = [](float t) { return envAR(t, 0.3f, 1.6f, 0.5f); };
    addSweep(b, 0, 2.4f, [](float t) { return 520.0f + 140.0f * std::sin(t * 6.2831f * 5.5f) + 60.0f * std::sin(t * 2.2f); }, env);
    addSweep(b, 0, 2.4f, [](float t) { return 1.5f * (520.0f + 140.0f * std::sin(t * 6.2831f * 5.5f + 0.4f)); },
             [env](float t) { return 0.3f * env(t); });
    addSweep(b, 0, 2.4f, [](float) { return 62.0f; },
             [env](float t) { return 0.5f * env(t) * (0.7f + 0.3f * std::sin(t * 6.2831f * 9.0f)); });
    reverb(b, 0.25f, 0.8f, 0.3f, 0.8f);
    normalize(b, 0.6f);
    S[SFX_UFO] = b;
  }
  {  // THEREMIN: an eerie little tune, sliding from note to note
    Buf b = make(4.6f);
    const float notes[8] = {440.0f, 659.25f, 587.33f, 523.25f, 440.0f, 349.23f, 329.63f, 440.0f};
    const float times[8] = {0.0f, 0.55f, 1.1f, 1.45f, 1.8f, 2.4f, 2.8f, 3.3f};
    auto freq = [&](float t) {
      int k = 0;
      while (k < 7 && t >= times[k + 1]) ++k;
      float f = notes[k];
      if (k > 0) {  // portamento from the previous note
        float u = std::min(1.0f, (t - times[k]) / 0.12f);
        f = notes[k - 1] + (notes[k] - notes[k - 1]) * (u * u * (3 - 2 * u));
      }
      return f * (1.0f + 0.016f * std::sin(t * 6.2831f * 6.3f) * std::min(1.0f, t / 0.3f));
    };
    auto env = [](float t) { return envAR(t, 0.15f, 3.65f, 0.7f); };
    addSweep(b, 0, 4.6f, freq, env);
    addSweep(b, 0, 4.6f, [&](float t) { return 2.0f * freq(t); }, [&](float t) { return 0.12f * env(t); });
    reverb(b, 0.45f, 0.84f, 0.3f, 1.0f);
    normalize(b, 0.6f);
    S[SFX_THEREMIN] = b;
  }
  {  // BEAM: a shimmering chord climbing as the beam lifts its catch, with sparkles
    Buf b = make(2.8f);
    const float ratios[4] = {1.0f, 1.5f, 2.0f, 3.0f};
    for (int k = 0; k < 4; ++k) {
      float rt = ratios[k];
      addSweep(b, 0, 2.8f, [rt](float t) { return rt * 220.0f * std::pow(2.0f, t / 2.6f); },
               [k](float t) { return envAR(t, 0.35f, 1.9f, 0.5f) * (0.75f + 0.25f * std::sin(t * 6.2831f * 12.0f + k)) / (1.0f + k); });
    }
    for (int i = 0; i < 40; ++i) {
      float t = r.next() * 2.5f;
      float f = 3000.0f + r.next() * 4000.0f;
      addDampedSine(b, t, f, 0.08f, 60);
    }
    normalize(b, 0.6f);
    S[SFX_BEAM] = b;
  }
  {  // GIBBER: "blip blorp zib-zib" - fast formant syllables at random pitches
    Buf b = make(1.4f);
    float t = 0.02f;
    for (int i = 0; i < 9 && t < 1.25f; ++i) {
      float len = 0.06f + r.next() * 0.09f;
      float f0 = 260.0f + r.next() * 420.0f;
      float glide = (r.next() - 0.4f) * 1.2f;
      float vowel = r.next();
      Buf syl = make(len + 0.02f);
      addSaw(syl, 0, len, [f0, glide, len](float u) { return f0 * (1.0f + glide * u / len) * (1.0f + 0.04f * std::sin(u * 6.2831f * 30.0f)); },
             [len](float u) { return envAR(u, 0.008f, len - 0.03f, 0.022f); });
      Biquad f1, f2;
      f1.bandpass(400.0f + 500.0f * vowel, 6.0f);
      f2.bandpass(1200.0f + 1400.0f * (1.0f - vowel), 8.0f);
      size_t s0 = (size_t)(t * SR);
      for (size_t k = 0; k < syl.size() && s0 + k < b.size(); ++k) b[s0 + k] += f1.run(syl[k]) + 0.6f * f2.run(syl[k]);
      t += len + (r.next() < 0.3f ? 0.07f : 0.015f);
    }
    normalize(b, 0.7f);
    S[SFX_GIBBER] = b;
  }
  {  // SLURP: wet noise with a rising resonance and bubbles, a pop at the end
    Buf b = make(0.75f);
    Biquad bp;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      if (i % 64 == 0) bp.bandpass(350.0f + 2600.0f * t / 0.7f, 4.0f);
      float flutter = 0.6f + 0.4f * std::sin(t * 6.2831f * 23.0f) * std::sin(t * 6.2831f * 7.0f);
      b[i] += bp.run(r.bi()) * envAR(t, 0.05f, 0.5f, 0.15f) * flutter * 2.0f;
    }
    for (int i = 0; i < 6; ++i) {
      float t = 0.05f + r.next() * 0.55f;
      float f = 500.0f + r.next() * 900.0f;
      addDampedSine(b, t, f, 0.25f, 50, -3.0f);
    }
    addDampedSine(b, 0.66f, 700.0f, 0.6f, 40, 6.0f);
    normalize(b, 0.7f);
    S[SFX_SLURP] = b;
  }
  {  // BURP: a low, jittery pulse through an "uh" formant
    Buf b = make(1.0f);
    Buf src = make(1.0f);
    double ph = 0;
    float jit = 0;
    for (size_t i = 0; i < src.size(); ++i) {
      float t = (float)i / SR;
      jit += (r.bi() * 0.4f - jit * 0.02f);
      float f = 92.0f * (1.0f - 0.15f * t) * (1.0f + 0.01f * jit);
      double dt = f / SR;
      ph += dt;
      if (ph >= 1.0) ph -= 1.0;
      float env = envAR(t, 0.04f, 0.55f, 0.3f) * (0.8f + 0.2f * std::sin(t * 6.2831f * 7.0f));
      src[i] = ((float)(2.0 * ph - 1.0) - polyblep(ph, dt)) * env;
    }
    Biquad f1, f2, lp;
    f1.bandpass(520, 4.0f);
    f2.bandpass(1050, 6.0f);
    lp.lowpass(1600);
    for (size_t i = 0; i < b.size(); ++i) b[i] = lp.run(f1.run(src[i]) * 1.2f + f2.run(src[i]) * 0.5f + src[i] * 0.15f);
    normalize(b, 0.8f);
    S[SFX_BURP] = b;
  }
  {  // BOING: a spring
    Buf b = make(0.9f);
    addSweep(b, 0, 0.9f, [](float t) { return 210.0f * (1.0f + 0.45f * std::exp(-t * 3.5f) * std::sin(t * 6.2831f * 13.0f)) * (1.0f + 0.3f * t); },
             [](float t) { return std::exp(-t * 3.2f) * std::min(1.0f, t / 0.004f); });
    addSweep(b, 0, 0.9f, [](float t) { return 420.0f * (1.0f + 0.45f * std::exp(-t * 3.5f) * std::sin(t * 6.2831f * 13.0f)); },
             [](float t) { return 0.25f * std::exp(-t * 5.0f); });
    normalize(b, 0.7f);
    S[SFX_BOING] = b;
  }
  {  // WHISTLE: a slide whistle, up and up (blasting off)
    Buf b = make(1.5f);
    addSweep(b, 0, 1.5f, [](float t) { return 600.0f * std::pow(3.6f, t / 1.3f) * (1.0f + 0.012f * std::sin(t * 6.2831f * 6.0f)); },
             [](float t) { return envAR(t, 0.04f, 1.2f, 0.25f); });
    Biquad bp;
    bp.bandpass(2500, 1.0f);
    noiseShaped(b, r, 0, 1.5f, bp, 0.05f, [](float t) { return envAR(t, 0.04f, 1.2f, 0.25f); });
    normalize(b, 0.6f);
    S[SFX_WHISTLE] = b;
  }
  {  // SPLAT: a wet thump and squelches
    Buf b = make(0.7f);
    addDampedSine(b, 0, 85, 1.0f, 14, 1.0f);
    Biquad lp;
    lp.lowpass(1400);
    addNoise(b, r, 0, 1.2f, 16, lp);
    for (int i = 0; i < 7; ++i) {
      float t = 0.02f + r.next() * 0.4f;
      float f = 300.0f + r.next() * 700.0f;
      Biquad bp;
      bp.bandpass(f, 5.0f);
      addNoise(b, r, t, 0.6f, 45, bp);
    }
    normalize(b, 0.85f);
    S[SFX_SPLAT] = b;
  }
  {  // MOO
    Buf b = make(1.7f);
    Buf src = make(1.7f);
    addSaw(src, 0, 1.6f, [](float t) { return 128.0f - 22.0f * t / 1.6f + 3.0f * std::sin(t * 6.2831f * 5.0f) * std::min(1.0f, t / 0.5f); },
           [](float t) { return envAR(t, 0.18f, 1.05f, 0.35f); });
    Biquad f1, f2, f3;
    f1.bandpass(330, 5.0f);
    f2.bandpass(820, 7.0f);
    f3.lowpass(2500);
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      float open = std::min(1.0f, t / 0.35f);  // "mm" opening into "oo"
      b[i] = f3.run(f1.run(src[i]) * 1.3f + f2.run(src[i]) * 0.5f * open + src[i] * 0.05f);
    }
    reverb(b, 0.15f, 0.7f, 0.4f, 0.6f);
    normalize(b, 0.8f);
    S[SFX_MOO] = b;
  }
  {  // POP
    Buf b = make(0.25f);
    addSweep(b, 0, 0.2f, [](float t) { return 1100.0f * std::exp(-t * 25.0f) + 180.0f; },
             [](float t) { return std::exp(-t * 28.0f); });
    Biquad hp;
    hp.highpass(2000);
    addNoise(b, r, 0, 0.8f, 200, hp);
    normalize(b, 0.8f);
    S[SFX_POP] = b;
  }
  {  // SHRINK: "wee-oo-wee-oo" all the way down, with sparkles
    Buf b = make(1.2f);
    addSweep(b, 0, 1.2f, [](float t) { return 1700.0f * std::pow(0.12f, t / 1.1f) * (1.0f + 0.12f * std::sin(t * 6.2831f * 16.0f)); },
             [](float t) { return envAR(t, 0.02f, 0.95f, 0.2f); });
    for (int i = 0; i < 18; ++i) {
      float t = r.next() * 1.0f;
      float f = 4000.0f + r.next() * 3000.0f;
      addDampedSine(b, t, f, 0.1f, 70);
    }
    normalize(b, 0.6f);
    S[SFX_SHRINK] = b;
  }
  {  // HEATRAY: an electric scream - a rising whine over a ring-modulated buzz, crackling
    Buf b = make(2.3f);
    double p1 = 0, p2 = 0, p3 = 0;
    Biquad hp;
    hp.highpass(4000);
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      float env = envAR(t, 0.25f, 1.7f, 0.3f);
      float whine = 950.0f + 700.0f * std::min(1.0f, t / 1.2f);
      p1 += TAU * whine / SR;
      p2 += TAU * 118.0 / SR;
      p3 += TAU * 1370.0 / SR;
      float buzz = (float)(std::sin(p2) + 0.5 * std::sin(2 * p2) + 0.33 * std::sin(3 * p2)) * (float)std::sin(p3);
      b[i] = ((float)std::sin(p1 + 0.8 * std::sin(p2 * 3.0)) * 0.6f + buzz * 0.5f + hp.run(r.bi()) * 0.5f) * env;
    }
    normalize(b, 0.75f);
    S[SFX_HEATRAY] = b;
  }
  {  // HORN: the tripod's two-note moan
    Buf b = make(2.9f);
    auto freq = [](float t) { return t < 1.0f ? 110.0f : 110.0f - 18.0f * std::min(1.0f, (t - 1.0f) / 0.25f); };
    auto env = [](float t) { return envAR(t, 0.12f, 2.1f, 0.5f) * (t > 0.95f && t < 1.05f ? 0.75f : 1.0f); };
    Buf src = make(2.9f);
    addSaw(src, 0, 2.8f, freq, env);
    addSaw(src, 0, 2.8f, [freq](float t) { return freq(t) * 1.006f; }, env);
    addSaw(src, 0, 2.8f, [freq](float t) { return freq(t) * 0.5f; }, [env](float t) { return 0.7f * env(t); });
    Biquad lp;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      if (i % 64 == 0) lp.lowpass(500.0f + 1400.0f * std::exp(-std::fmod(t, 1.0f) * 2.0f), 1.4f);
      b[i] = lp.run(src[i]);
    }
    reverb(b, 0.55f, 0.88f, 0.35f, 1.3f);
    normalize(b, 0.85f);
    S[SFX_HORN] = b;
  }
  {  // TWINKLE: a star in the sky
    Buf b = make(1.0f);
    addRing(b, 0, 2637.0f, 0.6f, 5.0f);
    addRing(b, 0, 3951.0f, 0.3f, 7.0f);
    addRing(b, 0.07f, 5274.0f, 0.25f, 9.0f);
    addRing(b, 0.12f, 3136.0f, 0.2f, 8.0f);
    normalize(b, 0.55f);
    S[SFX_TWINKLE] = b;
  }
  {  // SIZZLE: frying acid with bubbles
    Buf b = make(1.7f);
    Biquad hp;
    hp.highpass(2500);
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      float crack = r.next() < 0.004f ? 1.0f : 0.15f;
      b[i] += hp.run(r.bi()) * crack * envAR(t, 0.08f, 1.1f, 0.45f);
    }
    for (int i = 0; i < 14; ++i) {
      float t = r.next() * 1.4f;
      float f = 600.0f + r.next() * 1000.0f;
      addDampedSine(b, t, f, 0.12f, 40, -2.0f);
    }
    normalize(b, 0.6f);
    S[SFX_SIZZLE] = b;
  }
  {  // HATCH: the shell cracks, then little chirps
    Buf b = make(1.2f);
    for (int i = 0; i < 5; ++i) {
      float t = 0.03f * i + r.next() * 0.02f;
      float f = 2200.0f + r.next() * 2200.0f;
      addDampedSine(b, t, f, 0.5f, 140);
      Biquad hp;
      hp.highpass(1800);
      addNoise(b, r, t, 0.5f, 120, hp);
    }
    for (int i = 0; i < 4; ++i) {
      float t = 0.35f + 0.17f * i + r.next() * 0.05f;
      float f0 = 1500.0f + r.next() * 700.0f;
      addSweep(b, t, 0.1f, [f0](float u) { return f0 * (1.0f + 6.0f * u); }, [](float u) { return 0.5f * std::sin(u / 0.1f * 3.14159f); });
    }
    normalize(b, 0.7f);
    S[SFX_HATCH] = b;
  }
  {  // DRONE: the mothership - deep detuned throbbing chord swelling up
    Buf b = make(4.8f);
    Buf src = make(4.8f);
    auto env = [](float t) { return envAR(t, 1.2f, 2.6f, 1.0f) * (0.75f + 0.25f * std::sin(t * 6.2831f * 0.9f)); };
    const float fr[4] = {41.2f, 41.5f, 61.7f, 82.4f};
    for (float f : fr) addSaw(src, 0, 4.8f, [f](float) { return f; }, env);
    Biquad lp;
    lp.lowpass(420, 1.2f);
    for (size_t i = 0; i < b.size(); ++i) b[i] = lp.run(src[i]) * 0.5f;
    addSweep(b, 0, 4.8f, [](float t) { return 880.0f + 30.0f * std::sin(t * 6.2831f * 4.0f); },
             [env](float t) { return 0.04f * env(t); });
    reverb(b, 0.4f, 0.86f, 0.4f, 1.2f);
    normalize(b, 0.85f);
    S[SFX_DRONE] = b;
  }
  {  // SPUTTER: a machine coughing and dying
    Buf b = make(1.7f);
    double ph = 0;
    bool on = true;
    float next = 0;
    for (size_t i = 0; i < b.size(); ++i) {
      float t = (float)i / SR;
      if (t >= next) {
        float u = r.next();
        on = u > 0.25f + 0.4f * t / 1.6f;
        float len = r.next();
        next = t + 0.03f + 0.08f * len;
      }
      float f = 70.0f + 560.0f * std::exp(-t * 2.2f);
      ph += TAU * f / SR;
      float sq = std::sin(ph) > 0 ? 1.0f : -1.0f;
      b[i] = (on ? sq * 0.45f + (float)std::sin(ph) * 0.4f : 0.0f) * envAR(t, 0.01f, 1.3f, 0.3f);
    }
    Biquad lp;
    lp.lowpass(2500);
    for (float& v : b) v = lp.run(v);
    Biquad hp;
    hp.highpass(3000);
    for (int i = 0; i < 10; ++i) {
      float t = r.next() * 1.5f;
      addNoise(b, r, t, 0.4f, 90, hp);
    }
    normalize(b, 0.7f);
    S[SFX_SPUTTER] = b;
  }
}

// ---------------------------------------------------------------------------
// The ghosts
// ---------------------------------------------------------------------------
void synthGhosts(std::vector<Buf>& S) {
  Rng r;
  r.s = 777;
  {  // CHEER: a far-off little crowd of ghosts going "yaaay!", clapping, with a shimmer
    Buf b = make(2.4f), v = make(2.4f);
    for (int k = 0; k < 7; ++k) {
      float t0 = r.next() * 0.12f;
      float f0 = 220.0f + 260.0f * r.next();
      float vib = r.next() * 6.28f;
      addSaw(v, t0, 1.4f,
             [f0, vib](float t) {
               float up = 1.0f + 0.35f * std::min(t / 0.22f, 1.0f) - 0.12f * std::max(0.0f, t - 0.6f);
               return f0 * up * (1.0f + 0.012f * std::sin(t * 6.2831f * 5.5f + vib));
             },
             [](float t) { return envAR(t, 0.06f, 0.55f, 0.6f) * 0.3f; });
    }
    Biquad f1, f2, f3, f4;  // the vowel "ah"
    f1.bandpass(730, 6.0f);
    f2.bandpass(1090, 8.0f);
    f3.bandpass(2440, 12.0f);
    f4.lowpass(3500);
    for (size_t i = 0; i < b.size(); ++i) {
      float x = v[i];
      b[i] = f4.run(f1.run(x) + f2.run(x) * 0.6f + f3.run(x) * 0.25f);
    }
    Biquad cf;
    cf.bandpass(1600, 1.1f);
    for (int k = 0; k < 14; ++k) {
      float t = 0.1f + r.next() * 1.3f;
      addNoise(b, r, t, 0.5f, 60, cf);
    }
    for (int k = 0; k < 6; ++k) {
      float t = r.next() * 1.5f;
      float f = 1800.0f + r.next() * 1800.0f;
      addDampedSine(b, t, f, 0.06f, 5.0f);
    }
    reverb(b, 0.65f, 0.84f, 0.3f, 1.15f);
    normalize(b, 0.55f);
    S[SFX_CHEER] = b;
  }
}

}  // namespace

std::vector<Buf> synthEverything() {
  std::vector<Buf> S = synthAll();
  synthMedieval(S);
  synthAlien(S);
  synthGhosts(S);
  return S;
}

struct Audio::Impl {
  std::vector<Buf> sounds;
  std::thread medievalSynth;  // the medieval army's, the aliens' and the ghosts' sounds are made in the background
  struct Voice {
    int sfx = -1;
    double pos = 0;
    float rate = 1, l = 0, r = 0;
  };
  Voice voices[40];
  ::music::Engine music;
  std::mutex mtx;
  float master = 0.8f;
#ifdef LM_AUDIO
  ma_device device;
#endif
  void mix(float* out, unsigned frames) {
    std::lock_guard<std::mutex> lk(mtx);
    for (unsigned i = 0; i < frames * 2; ++i) out[i] = 0;
    for (Voice& v : voices) {
      if (v.sfx < 0) continue;
      const Buf& b = sounds[v.sfx];
      for (unsigned i = 0; i < frames; ++i) {
        size_t p = (size_t)v.pos;
        if (p + 1 >= b.size()) {
          v.sfx = -1;
          break;
        }
        float fr = (float)(v.pos - p);
        float s = b[p] * (1 - fr) + b[p + 1] * fr;
        out[i * 2] += s * v.l;
        out[i * 2 + 1] += s * v.r;
        v.pos += v.rate;
      }
    }
    music.render(out, out + 1, (int)frames, 2);
    for (unsigned i = 0; i < frames * 2; ++i) out[i] = std::tanh(out[i] * master);
  }
};

#ifdef LM_AUDIO
static void lmAudioCallback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
  auto* impl = static_cast<Audio::Impl*>(dev->pUserData);
  impl->mix(static_cast<float*>(out), frames);
}
#endif

static float musicDuck(Sfx s);

Audio::Audio() {}
Audio::~Audio() { shutdown(); }

bool Audio::init() {
#ifdef LM_AUDIO
  impl_ = new Impl;
  impl_->music.seed(std::random_device{}());
  impl_->music.setEnabled(music_);
  impl_->music.setVolume(musicVolume_);
  impl_->music.setArmy(musicArmy_);
  impl_->sounds = synthAll();
  impl_->sounds.resize(SFX_COUNT);
  for (int i = SFX_HYMN; i < SFX_COUNT; ++i) impl_->sounds[i] = make(0.01f);
  Impl* impl = impl_;
  impl_->medievalSynth = std::thread([impl]() {
    std::vector<Buf> med(SFX_COUNT);
    synthMedieval(med);
    synthAlien(med);
    synthGhosts(med);
    std::lock_guard<std::mutex> lk(impl->mtx);
    for (int i = SFX_HYMN; i < SFX_COUNT; ++i) impl->sounds[i] = std::move(med[i]);
  });
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = SR;
  cfg.dataCallback = lmAudioCallback;
  cfg.pUserData = impl_;
  if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) {
    impl_->medievalSynth.join();
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  if (ma_device_start(&impl_->device) != MA_SUCCESS) {
    ma_device_uninit(&impl_->device);
    impl_->medievalSynth.join();
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  ok_ = true;
  return true;
#else
  return false;
#endif
}

void Audio::shutdown() {
#ifdef LM_AUDIO
  if (impl_) {
    if (ok_) ma_device_uninit(&impl_->device);
    if (impl_->medievalSynth.joinable()) impl_->medievalSynth.join();
    delete impl_;
    impl_ = nullptr;
  }
#endif
  ok_ = false;
}

void Audio::play(Sfx s, float vol, float pan, float pitch) {
  if (!ok_ || !impl_ || s < 0 || s >= SFX_COUNT) return;
  std::lock_guard<std::mutex> lk(impl_->mtx);
  Impl::Voice* slot = nullptr;
  for (auto& v : impl_->voices)
    if (v.sfx < 0) {
      slot = &v;
      break;
    }
  if (!slot) {  // steal the oldest-progressed voice
    slot = &impl_->voices[0];
    for (auto& v : impl_->voices)
      if (v.pos > slot->pos) slot = &v;
  }
  float a = (pan + 1.0f) * 0.25f * 3.14159265f;
  slot->sfx = s;
  slot->pos = 0;
  slot->rate = std::max(0.25f, pitch);
  slot->l = vol * std::cos(a);
  slot->r = vol * std::sin(a);
  float depth = musicDuck(s);
  if (depth < 1.0f) impl_->music.duck((int)((float)impl_->sounds[s].size() / slot->rate), depth);
}

// How far the music is turned down while a sound effect plays: the ones that are
// music themselves (the organ, the theremin, the bells ...) would clash with it.
static float musicDuck(Sfx s) {
  switch (s) {
    case SFX_HYMN: return 0.12f;
    case SFX_ORGAN: case SFX_THEREMIN: return 0.2f;
    case SFX_MATE: return 0.25f;
    case SFX_BELL: case SFX_DRONE: return 0.35f;
    case SFX_CHECK: case SFX_HORN: return 0.45f;
    default: return 1.0f;
  }
}

void Audio::playMusic(int piece) {
  if (!ok_ || !impl_) return;
  std::lock_guard<std::mutex> lk(impl_->mtx);
  impl_->music.play(piece);
}

void Audio::setMusicArmy(int set) {
  musicArmy_ = set;
  if (!impl_) return;
  std::lock_guard<std::mutex> lk(impl_->mtx);
  impl_->music.setArmy(set);
}

void Audio::resumeMusic() {
  if (!ok_ || !impl_) return;
  std::lock_guard<std::mutex> lk(impl_->mtx);
  impl_->music.resume();
}

void Audio::stopMusic() {
  if (!ok_ || !impl_) return;
  std::lock_guard<std::mutex> lk(impl_->mtx);
  impl_->music.stop();
}

void Audio::setMusic(bool on) {
  music_ = on;
  if (!impl_) return;
  std::lock_guard<std::mutex> lk(impl_->mtx);
  impl_->music.setEnabled(on);
}

void Audio::setMusicVolume(float v) {
  musicVolume_ = v;
  if (!impl_) return;
  std::lock_guard<std::mutex> lk(impl_->mtx);
  impl_->music.setVolume(v);
}

std::string Audio::nowPlaying() const {
  if (!ok_ || !impl_) return "";
  static const char* const kPiece[::music::kPieces] = {"pawn", "knight", "bishop", "rook", "queen", "king"};
  std::lock_guard<std::mutex> lk(impl_->mtx);
  int s = impl_->music.playing();
  if (s < 0) return "";
  return impl_->music.songTitle(s) + " (" + kPiece[impl_->music.songPiece(s)] + ")";
}

bool Audio::dumpWav(const char* dir) {
  static const char* names[SFX_COUNT] = {"step",    "thud",    "clack",   "shatter", "whoosh", "clang",  "boom",
                                         "explode", "charge",  "zap",     "crackle", "chime",  "select", "check",
                                         "mate",    "debris",  "rumble",  "hymn",    "organ",  "thunder", "wind",
                                         "roar",    "fire",    "chain",   "grind",   "stomp",  "hoof",   "neigh",
                                         "bell",    "splinter", "armor",  "slide",   "bite",   "pew",
                                         "ufo",     "theremin", "beam",   "gibber",  "slurp",  "burp",   "boing",
                                         "whistle", "splat",   "moo",     "pop",     "shrink", "heatray", "horn",
                                         "twinkle", "sizzle",  "hatch",   "drone",   "sputter", "cheer"};
  std::vector<Buf> S = synthEverything();
  for (int i = 0; i < SFX_COUNT; ++i) {
    std::string path = std::string(dir) + "/" + names[i] + ".wav";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t n = (uint32_t)S[i].size(), bytes = n * 2;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(1); w32(SR); w32(SR * 2); w16(2); w16(16);
    std::fwrite("data", 1, 4, f); w32(bytes);
    for (float v : S[i]) w16((uint16_t)(int16_t)std::max(-32767.0f, std::min(32767.0f, v * 32767.0f)));
    std::fclose(f);
  }
  // and every song of the music, in stereo: music-<army>-<piece>-<title>.wav
  static const char* const kPiece[::music::kPieces] = {"pawn", "knight", "bishop", "rook", "queen", "king"};
  static const char* const kArmy[::music::kArmies] = {"marble", "medieval", "alien"};
  ::music::Engine m;
  for (const std::string& w : m.warnings()) std::printf("[music] %s\n", w.c_str());
  for (int i = 0; i < m.songCount(); ++i) {
    std::string title;  // "The King's Parade" -> the-kings-parade
    for (char c : m.songTitle(i))
      if (c == ' ') title += '-';
      else if (std::isalnum((unsigned char)c)) title += (char)std::tolower((unsigned char)c);
    std::string path = std::string(dir) + "/music-" + kArmy[m.songArmy(i)] + "-" + kPiece[m.songPiece(i)] + "-" + title + ".wav";
    std::vector<float> st = m.renderSong(i);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t n = (uint32_t)(st.size() / 2), bytes = n * 4;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(SR); w32(SR * 4); w16(4); w16(16);
    std::fwrite("data", 1, 4, f); w32(bytes);
    for (float v : st) w16((uint16_t)(int16_t)std::max(-32767.0f, std::min(32767.0f, v * 32767.0f)));
    std::fclose(f);
  }
  return true;
}

void Audio::setVolume(float v) {
  volume_ = v;
  if (impl_) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->master = v;
  }
}
