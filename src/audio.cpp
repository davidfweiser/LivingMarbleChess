#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <mutex>
#include <random>
#include <vector>

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
      addDampedSine(b, t, 1800 + r.next() * 5500, 0.25f + 0.35f * r.next(), 120 + r.next() * 120);
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

}  // namespace

struct Audio::Impl {
  std::vector<Buf> sounds;
  struct Voice {
    int sfx = -1;
    double pos = 0;
    float rate = 1, l = 0, r = 0;
  };
  Voice voices[40];
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
    for (unsigned i = 0; i < frames * 2; ++i) out[i] = std::tanh(out[i] * master);
  }
};

#ifdef LM_AUDIO
static void lmAudioCallback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
  auto* impl = static_cast<Audio::Impl*>(dev->pUserData);
  impl->mix(static_cast<float*>(out), frames);
}
#endif

Audio::Audio() {}
Audio::~Audio() { shutdown(); }

bool Audio::init() {
#ifdef LM_AUDIO
  impl_ = new Impl;
  impl_->sounds = synthAll();
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = SR;
  cfg.dataCallback = lmAudioCallback;
  cfg.pUserData = impl_;
  if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) {
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  if (ma_device_start(&impl_->device) != MA_SUCCESS) {
    ma_device_uninit(&impl_->device);
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
}

bool Audio::dumpWav(const char* dir) {
  static const char* names[SFX_COUNT] = {"step",  "thud",    "clack", "shatter", "whoosh", "clang",
                                         "boom",  "explode", "charge", "zap",    "crackle", "chime",
                                         "select", "check",  "mate",  "debris",  "rumble"};
  std::vector<Buf> S = synthAll();
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
  return true;
}

void Audio::setVolume(float v) {
  volume_ = v;
  if (impl_) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->master = v;
  }
}
