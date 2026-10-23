#include "music.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace music {

// music_score.cpp: the instruments and the songs, in pieces (compilers limit a string's length), then nullptr
extern const char* const kScore[];

namespace {

constexpr int SR = 44100;
constexpr double TAU = 6.283185307179586;
constexpr float PI_F = 3.14159265f;
constexpr int kVoices = 64;
constexpr int kTab = 4096;        // wavetable length (two guard samples follow)
constexpr int kKS = 2048;         // plucked-string delay line per voice (down to 22 Hz)
constexpr int kBlock = 256;       // the mix is made in blocks of this many samples
constexpr int kEcho = 65536;      // echo line per channel (1.49 s)
constexpr float kLevel = 0.5f;    // music under the sound effects

struct Rng {
  uint32_t s = 0x9e3779b9u;
  uint32_t u32() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  float next() { return (float)(u32() & 0xffffff) / (float)0xffffff; }
  float bi() { return next() * 2.0f - 1.0f; }
};

// ---------------------------------------------------------------------------
// Instruments
// ---------------------------------------------------------------------------
enum Kind { K_SYNTH = 0, K_FM, K_KS, K_DRUM };
enum Wave { W_SAW = 0, W_SQR, W_TRI, W_SIN, W_ORGAN, W_ORGAN2, W_FLUTE };
enum Filt { F_NONE = 0, F_LP, F_BP, F_VOX };
enum Drum { D_MEMBRANE = 0, D_SNARE, D_CLAP, D_METAL, D_SHAKER };

struct Patch {
  std::string name;
  int kind = K_SYNTH, wave = W_SAW, filt = F_NONE, uni = 1;
  float det = 0, pw = 0.5f, sub = 0, noise = 0;            // oscillators: unison detune (cents), pulse width, sub octave, breath
  float a = 0.005f, d = 0.3f, s = 0.7f, r = 0.1f;          // amplitude envelope (s), sustain level
  float hz = 0, cut = 0, res = 0, fenv = 0, fatk = 0, fdec = 0.2f;  // filter: hz + cut * note frequency, envelope in octaves
  float vibRate = 0, vibDepth = 0, vibDelay = 0, scoop = 0, drive = 0, tremRate = 0, tremDepth = 0;
  float glide = 0, bend = 0, bendTime = 0.05f, wahRate = 0, wahDepth = 0;  // portamento (s), a pitch sweep, a filter LFO
  float ratio = 1, index = 0, idec = 0.5f, p2 = 0, p2amp = 0, p2dec = 0.3f;  // FM, and an extra partial
  float bright = 0.6f, damp = 0.5f, decay = 1.0f;          // plucked string
  float f = 100, pamt = 0, pdec = 0.05f, nf = 1000, ndec = 0.02f, snap = 0, tone = 0.5f, metal = 0.5f;  // drums
  float gain = 0.5f, pan = 0, rev = 0.1f, del = 0;
};

// Zavalishin's trapezoidal state-variable filter.
struct Svf {
  float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 2;
  void set(float fc, float res) {
    fc = std::min(std::max(fc, 20.0f), 18000.0f);
    float g = std::tan(PI_F * fc / (float)SR);
    k = 2.0f - 1.9f * res;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }
  void run(float x, float& lp, float& bp) {
    float v3 = x - ic2;
    float v1 = a1 * ic1 + a2 * v3;
    float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    lp = v2;
    bp = v1;
  }
};

struct Voice {
  int patch = -1;  // -1: free
  int age = 0, gateAt = 0, stage = 0;  // stage 0 attack, 1 decay/sustain, 2 release
  float env = 0, aInc = 0, dCoef = 0, rCoef = 0;
  float freq = 0, amp = 0, gl = 0, gr = 0, rev = 0, del = 0, slide = 0;
  double ph[6] = {0, 0, 0, 0, 0, 0}, inc[6] = {0, 0, 0, 0, 0, 0};
  double phSub = 0, phM = 0, ph2 = 0;
  float det[3] = {1, 1, 1};
  Svf flt, fx[3];
  float trem = 1, idx = 0, p2g = 0;
  // plucked string
  int ksW = 0;
  float ksN = 0, ksG = 1, ksPrev = 0;
  // drums
  float dAmp = 0, dCoef2 = 0, nAmp = 0, nCoef = 0, e1 = 0, e2 = 0, c1 = 0, c2 = 0;
  Rng rng;
};

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

inline float lookup(const float* t, float ph) {
  float x = ph * (float)kTab;
  int i = (int)x;
  float fr = x - (float)i;
  return t[i] + (t[i + 1] - t[i]) * fr;
}

inline float frac(float x) { return x - std::floor(x); }

float mtof(int note) { return 440.0f * std::exp2((float)(note - 69) / 12.0f); }

// ---------------------------------------------------------------------------
// Effects: a Freeverb-style hall and a ping-pong echo
// ---------------------------------------------------------------------------
struct Reverb {
  std::vector<float> buf;
  int off[24] = {}, len[24] = {}, idx[24] = {};
  float store[16] = {};
  void init() {
    static const int comb[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static const int ap[4] = {556, 441, 341, 225};
    int o = 0;
    for (int k = 0; k < 24; ++k) {
      int n = k < 16 ? comb[k & 7] + (k >= 8 ? 23 : 0) : ap[(k - 16) & 3] + (k >= 20 ? 23 : 0);
      off[k] = o;
      len[k] = n;
      o += n;
    }
    buf.assign((size_t)o, 0.0f);
  }
  void clear() {
    std::fill(buf.begin(), buf.end(), 0.0f);
    for (int k = 0; k < 16; ++k) store[k] = 0;
  }
  void run(float x, float& l, float& r) {
    float in = x * 0.015f, outL = 0, outR = 0;
    for (int k = 0; k < 16; ++k) {
      float* b = &buf[(size_t)off[k]];
      float o = b[idx[k]];
      store[k] = o * 0.75f + store[k] * 0.25f;
      b[idx[k]] = in + store[k] * 0.84f;
      if (++idx[k] >= len[k]) idx[k] = 0;
      if (k < 8) outL += o;
      else outR += o;
    }
    for (int k = 16; k < 24; ++k) {
      float* b = &buf[(size_t)off[k]];
      float& y = k < 20 ? outL : outR;
      float o = b[idx[k]];
      b[idx[k]] = y + o * 0.5f;
      y = o - y;
      if (++idx[k] >= len[k]) idx[k] = 0;
    }
    l = outL * 3.0f;
    r = outR * 3.0f;
  }
};

struct Echo {
  std::vector<float> L, R;
  int w = 0, len = 11025;
  float lp = 0;
  void init() {
    L.assign(kEcho, 0.0f);
    R.assign(kEcho, 0.0f);
  }
  void clear() {
    std::fill(L.begin(), L.end(), 0.0f);
    std::fill(R.begin(), R.end(), 0.0f);
    lp = 0;
  }
  void run(float x, float& l, float& r) {
    int rp = (w - len) & (kEcho - 1);
    float dl = L[rp], dr = R[rp];
    lp += 0.4f * (dr - lp);
    L[w] = x + lp * 0.42f;
    R[w] = dl;
    w = (w + 1) & (kEcho - 1);
    l = dl * 0.7f;
    r = dr * 0.7f;
  }
};

// ---------------------------------------------------------------------------
// The score
// ---------------------------------------------------------------------------
struct Song {
  std::string title;
  int army = 0, piece = 0, beat = 4, bar = 16, bars = 8, echo = 3;
  float bpm = 120, swing = 0, gain = 1;
  double sps = 0;          // samples per step
  int64_t len = 0;         // samples
  int first = 0, count = 0;  // its events
  int echoLen = 0;
};

struct Track {
  int patch = 0;
  float vol = 1, pan = 0, rev = 0, del = 0;
};

struct Event {
  int64_t t = 0;  // sample, from the start of the song
  int len = 0;    // samples until the note is released
  int track = 0, note = 0, seq = 0;
  float vel = 0.8f;
};

double stepTime(const Song& s, int step) {
  double t = (double)step * s.sps;
  if (s.beat % 2 == 0 && (step & 1)) t += (double)s.swing * s.sps;
  return t;
}
int64_t stepSample(const Song& s, int step) { return (int64_t)stepTime(s, step); }

struct Chord {
  int root = 0, third = 4, fifth = 7, sev = 12, n = 3;
  int tones[5] = {0, 4, 7, 0, 0};
};

// one step of a part: a rest, notes starting, or the notes before held on
struct Spec {
  int kind = 0, value = 0, oct = 0;  // kind 0 a MIDI note, 1 a chord degree, 2 the chord, 3 a power chord
};
struct Cell {
  int type = 0;  // 0 rest, 1 start, 2 hold
  float vel = 0.8f;
  int first = 0, count = 0;  // its specs
};

const char* const kPieceNames[kPieces] = {"pawn", "knight", "bishop", "rook", "queen", "king"};
const char* const kArmyNames[kArmies] = {"marble", "medieval", "alien"};
constexpr int kSlots = kArmies * kPieces;  // an army's piece: army * kPieces + piece

std::vector<std::string> splitWS(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    size_t j = i;
    while (j < s.size() && s[j] != ' ' && s[j] != '\t') ++j;
    if (j > i) out.push_back(s.substr(i, j - i));
    i = j;
  }
  return out;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

std::vector<float> floats(const std::string& s) {
  std::vector<float> v;
  size_t i = 0;
  while (i <= s.size()) {
    size_t j = s.find(',', i);
    if (j == std::string::npos) j = s.size();
    v.push_back(std::strtof(s.substr(i, j - i).c_str(), nullptr));
    i = j + 1;
  }
  return v;
}

bool isDigits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s)
    if (c < '0' || c > '9') return false;
  return true;
}

// "C", "F#", "Bb" -> pitch class; returns the characters used (0 if none)
int pitchClass(const std::string& s, size_t at, int& pc) {
  static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // A..G
  if (at >= s.size() || s[at] < 'A' || s[at] > 'G') return 0;
  pc = base[s[at] - 'A'];
  size_t n = 1;
  if (at + 1 < s.size() && s[at + 1] == '#') {
    pc += 1;
    n = 2;
  } else if (at + 1 < s.size() && s[at + 1] == 'b') {
    pc -= 1;
    n = 2;
  }
  pc = (pc + 12) % 12;
  return (int)n;
}

// "C4", "F#5", "Bb3" -> MIDI note, or -1
int noteNumber(const std::string& s) {
  int pc = 0;
  int n = pitchClass(s, 0, pc);
  if (n == 0 || !isDigits(s.substr((size_t)n))) return -1;
  return 12 * (std::atoi(s.c_str() + n) + 1) + pc;
}

bool chordOf(const std::string& q, Chord& c) {
  struct Q {
    const char* name;
    int third, fifth, sev, n, t[5];
  };
  static const Q table[] = {
      {"", 4, 7, 12, 3, {0, 4, 7, 0, 0}},        {"m", 3, 7, 12, 3, {0, 3, 7, 0, 0}},
      {"7", 4, 7, 10, 4, {0, 4, 7, 10, 0}},      {"m7", 3, 7, 10, 4, {0, 3, 7, 10, 0}},
      {"maj7", 4, 7, 11, 4, {0, 4, 7, 11, 0}},   {"dim", 3, 6, 9, 3, {0, 3, 6, 0, 0}},
      {"m7b5", 3, 6, 10, 4, {0, 3, 6, 10, 0}},   {"aug", 4, 8, 12, 3, {0, 4, 8, 0, 0}},
      {"sus4", 5, 7, 12, 3, {0, 5, 7, 0, 0}},    {"sus2", 2, 7, 12, 3, {0, 2, 7, 0, 0}},
      {"6", 4, 7, 9, 4, {0, 4, 7, 9, 0}},        {"m6", 3, 7, 9, 4, {0, 3, 7, 9, 0}},
      {"5", 7, 7, 12, 2, {0, 7, 0, 0, 0}},       {"9", 4, 7, 10, 5, {0, 4, 7, 10, 14}},
      {"add9", 4, 7, 12, 4, {0, 4, 7, 14, 0}},   {"m9", 3, 7, 10, 5, {0, 3, 7, 10, 14}},
  };
  for (const Q& e : table)
    if (q == e.name) {
      c.third = e.third;
      c.fifth = e.fifth;
      c.sev = e.sev;
      c.n = e.n;
      for (int i = 0; i < 5; ++i) c.tones[i] = e.t[i];
      return true;
    }
  return false;
}

int degreeSemis(int deg, const Chord& c) {
  if (deg >= 8) return degreeSemis(deg - 7, c) + 12;
  switch (deg) {
    case 1: return 0;
    case 2: return 2;
    case 3: return c.third;
    case 4: return 5;
    case 5: return c.fifth;
    case 6: return 9;
    default: return c.sev;
  }
}

int mod12(int x) { return ((x % 12) + 12) % 12; }

}  // namespace

// ---------------------------------------------------------------------------
struct Engine::Impl {
  std::vector<Patch> patches;
  std::vector<Song> songs;
  std::vector<Track> tracks;
  std::vector<Event> events;
  std::vector<std::string> warn;
  std::string score;
  int crash = -1;  // the cymbal that marks a change of theme
  std::vector<float> tabs;
  Voice voices[kVoices];
  std::vector<float> ks;
  Reverb reverb;
  Echo echo;
  float dryL[kBlock], dryR[kBlock], revIn[kBlock], delIn[kBlock];
  // the sequencer
  int cur = -1, curBar = 0, ev = 0, evEnd = 0, pending = -1, want = -1;
  int64_t pos = 0, barEnd = 0, since = 0;
  int pieceSong[kSlots], pieceBar[kSlots], lastSong[kSlots];
  int army = 0;
  std::vector<float> lastFreq;  // per instrument: its last note, for glide
  bool enabled = true, single = false;
  float fade = 1, fadeTarget = 1, fadeStep = 1.0f / (2.5f * SR);
  float volume = 0.7f, volS = 0.7f, songGain = 1, songGainS = 1;
  float duckGain = 1, duckDepth = 1;
  float dcL = 0, dcR = 0, dcOutL = 0, dcOutR = 0;  // DC blocker on the way out
  int duckHold = 0, tail = 0;
  Rng pick, noise;

  explicit Impl(const char* text) {
    for (int p = 0; p < kSlots; ++p) {
      pieceSong[p] = -1;
      pieceBar[p] = 0;
      lastSong[p] = -1;
    }
    makeTables();
    ks.assign((size_t)kVoices * kKS, 0.0f);
    reverb.init();
    echo.init();
    noise.s = 20240611u;
    score = text;
    parse(text);
    for (size_t i = 0; i < patches.size(); ++i)
      if (patches[i].name == "crash") crash = (int)i;
    lastFreq.assign(patches.size(), 0.0f);
  }

  // ---- tables: sine, two organs, a flute -------------------------------
  void makeTables() {
    tabs.assign((size_t)4 * (kTab + 2), 0.0f);
    struct H {
      int n;
      double amp;
    };
    static const H organ[] = {{1, 1.0}, {2, 0.8}, {3, 0.6}, {4, 0.45}, {6, 0.3}, {8, 0.25}};
    static const H organ2[] = {{1, 1.0}, {2, 0.7}, {3, 0.5}, {4, 0.5}, {5, 0.25}, {6, 0.35}, {8, 0.3}, {10, 0.12}, {12, 0.15}, {16, 0.1}};
    static const H flute[] = {{1, 1.0}, {2, 0.3}, {3, 0.12}, {4, 0.05}};
    const H* sets[4] = {nullptr, organ, organ2, flute};
    const int counts[4] = {1, 6, 10, 4};
    std::vector<double> v(kTab);
    for (int t = 0; t < 4; ++t) {
      double m = 1e-9;
      for (int i = 0; i < kTab; ++i) {
        double x = 0;
        if (t == 0) x = std::sin(TAU * i / kTab);
        else
          for (int k = 0; k < counts[t]; ++k) x += sets[t][k].amp * std::sin(TAU * sets[t][k].n * i / kTab);
        v[i] = x;
        m = std::max(m, std::fabs(x));
      }
      float* tab = &tabs[(size_t)t * (kTab + 2)];
      for (int i = 0; i < kTab; ++i) tab[i] = (float)(v[i] / m);
      tab[kTab] = tab[0];
      tab[kTab + 1] = tab[1];
    }
  }
  const float* table(int wave) const { return &tabs[(size_t)(wave - W_SIN) * (kTab + 2)]; }
  float sinT(float ph) const { return lookup(&tabs[0], ph); }

  // ---- parsing ---------------------------------------------------------
  void warning(const std::string& where, const std::string& what) { warn.push_back(where + ": " + what); }

  int patchIndex(const std::string& name) const {
    for (size_t i = 0; i < patches.size(); ++i)
      if (patches[i].name == name) return (int)i;
    return -1;
  }

  void parse(const char* text) {
    std::vector<std::string> lines;
    std::string all(text);
    size_t i = 0;
    while (i <= all.size()) {
      size_t j = all.find('\n', i);
      if (j == std::string::npos) j = all.size();
      std::string raw = all.substr(i, j - i);
      i = j + 1;
      std::string t = trim(raw);
      if (t.empty() || t[0] == '#') continue;  // a comment ('#' is also a sharp, so only whole lines)
      if ((raw[0] == ' ' || raw[0] == '\t') && !lines.empty()) lines.back() += " " + t;
      else lines.push_back(t);
    }
    std::vector<std::string> songLines;
    for (const std::string& l : lines) {
      std::string key = l.substr(0, l.find_first_of(" \t:"));
      if (key == "patch") parsePatch(l);
      else if (key == "song") {
        if (!songLines.empty()) buildSong(songLines);
        songLines.assign(1, l);
      } else if (!songLines.empty()) songLines.push_back(l);
      else warning("score", "a line outside any song: " + l);
    }
    if (!songLines.empty()) buildSong(songLines);
  }

  void parsePatch(const std::string& line) {
    std::vector<std::string> tk = splitWS(line);
    if (tk.size() < 3) {
      warning("patch", line);
      return;
    }
    Patch P;
    P.name = tk[1];
    static const char* kinds[] = {"synth", "fm", "ks", "drum"};
    P.kind = -1;
    for (int k = 0; k < 4; ++k)
      if (tk[2] == kinds[k]) P.kind = k;
    if (P.kind < 0) {
      warning(P.name, "unknown kind " + tk[2]);
      return;
    }
    if (P.kind == K_KS) {
      P.a = 0.001f;
      P.s = 1.0f;
    }
    size_t at = 3;
    if (P.kind == K_SYNTH || P.kind == K_DRUM) {
      static const char* waves[] = {"saw", "sqr", "tri", "sin", "organ", "organ2", "flute"};
      static const char* drums[] = {"membrane", "snare", "clap", "metal", "shaker"};
      int found = -1;
      if (tk.size() > 3) {
        if (P.kind == K_SYNTH) {
          for (int k = 0; k < 7; ++k)
            if (tk[3] == waves[k]) found = k;
        } else {
          for (int k = 0; k < 5; ++k)
            if (tk[3] == drums[k]) found = k;
        }
      }
      if (found < 0) {
        warning(P.name, "needs a wave or drum type");
        return;
      }
      P.wave = found;
      at = 4;
    }
    bool filtSet = false;
    for (; at < tk.size(); ++at) {
      size_t eq = tk[at].find('=');
      if (eq == std::string::npos) {
        warning(P.name, "bad setting " + tk[at]);
        continue;
      }
      std::string k = tk[at].substr(0, eq), v = tk[at].substr(eq + 1);
      std::vector<float> f = floats(v);
      float x = f[0];
      if (k == "filt") {
        filtSet = true;
        P.filt = v == "lp" ? F_LP : v == "bp" ? F_BP : v == "vox" ? F_VOX : F_NONE;
      } else if (k == "uni") P.uni = std::min(3, std::max(1, (int)x));
      else if (k == "det") P.det = x;
      else if (k == "pw") P.pw = x;
      else if (k == "sub") P.sub = x;
      else if (k == "noise") P.noise = x;
      else if (k == "a") P.a = x;
      else if (k == "d") P.d = x;
      else if (k == "s") P.s = x;
      else if (k == "r") P.r = x;
      else if (k == "hz") P.hz = x;
      else if (k == "cut") P.cut = x;
      else if (k == "res") P.res = x;
      else if (k == "fenv") P.fenv = x;
      else if (k == "fatk") P.fatk = x;
      else if (k == "fdec") P.fdec = x;
      else if (k == "vib" && f.size() == 3) {
        P.vibRate = f[0];
        P.vibDepth = f[1];
        P.vibDelay = f[2];
      } else if (k == "trem" && f.size() == 2) {
        P.tremRate = f[0];
        P.tremDepth = f[1];
      } else if (k == "scoop") P.scoop = x;
      else if (k == "glide") P.glide = x;
      else if (k == "bend" && f.size() == 2) {
        P.bend = f[0];
        P.bendTime = f[1];
      } else if (k == "wah" && f.size() == 2) {
        P.wahRate = f[0];
        P.wahDepth = f[1];
      } else if (k == "drive") P.drive = x;
      else if (k == "ratio") P.ratio = x;
      else if (k == "index") P.index = x;
      else if (k == "idec") P.idec = x;
      else if (k == "p2") P.p2 = x;
      else if (k == "p2amp") P.p2amp = x;
      else if (k == "p2dec") P.p2dec = x;
      else if (k == "bright") P.bright = x;
      else if (k == "damp") P.damp = x;
      else if (k == "decay") P.decay = x;
      else if (k == "f") P.f = x;
      else if (k == "pamt") P.pamt = x;
      else if (k == "pdec") P.pdec = x;
      else if (k == "nf") P.nf = x;
      else if (k == "ndec") P.ndec = x;
      else if (k == "snap") P.snap = x;
      else if (k == "tone") P.tone = x;
      else if (k == "metal") P.metal = x;
      else if (k == "gain") P.gain = x;
      else if (k == "pan") P.pan = x;
      else if (k == "rev") P.rev = x;
      else if (k == "del") P.del = x;
      else warning(P.name, "unknown setting " + tk[at]);
    }
    if (!filtSet && (P.cut > 0 || P.hz > 0)) P.filt = F_LP;
    if (patchIndex(P.name) >= 0) warning(P.name, "defined twice");
    patches.push_back(P);
  }

  // Splits a part into bars at '|'; a bar may end with "*N" to repeat it.
  // Returns the bars, each as its tokens (drums: each character a token).
  std::vector<std::vector<std::string>> bars(const std::string& data, bool chars) {
    std::vector<std::vector<std::string>> out;
    std::string spaced;
    for (char c : data) {
      if (c == '|') spaced += " | ";
      else spaced += c;
    }
    std::vector<std::string> tk = splitWS(spaced);
    std::vector<std::string> seg;
    auto flush = [&]() {
      int rep = 1;
      if (!seg.empty() && seg.back().size() > 1 && seg.back()[0] == '*' && isDigits(seg.back().substr(1))) {
        rep = std::atoi(seg.back().c_str() + 1);
        seg.pop_back();
      }
      if (chars) {
        std::vector<std::string> cs;
        for (const std::string& s : seg)
          for (char c : s) cs.push_back(std::string(1, c));
        seg = cs;
      }
      if (!seg.empty())
        for (int k = 0; k < rep; ++k) out.push_back(seg);
      seg.clear();
    };
    for (const std::string& t : tk) {
      if (t == "|") flush();
      else if (chars && t.size() > 1 && t[0] == '*' && isDigits(t.substr(1))) seg.push_back(t);
      else if (chars && t.find('*') != std::string::npos && t.find('*') > 0) {
        size_t st = t.find('*');
        seg.push_back(t.substr(0, st));
        seg.push_back(t.substr(st));
      } else seg.push_back(t);
    }
    flush();
    return out;
  }

  void buildSong(const std::vector<std::string>& lines) {
    Song S;
    std::vector<std::string> head = splitWS(lines[0]);
    for (size_t i = 1; i < head.size(); ++i) {
      size_t eq = head[i].find('=');
      std::string k = eq == std::string::npos ? head[i] : head[i].substr(0, eq);
      std::string v = eq == std::string::npos ? "" : head[i].substr(eq + 1);
      float x = std::strtof(v.c_str(), nullptr);
      if (k == "title") {
        S.title = v;
        std::replace(S.title.begin(), S.title.end(), '_', ' ');
      } else if (k == "piece") {
        S.piece = -1;
        for (int p = 0; p < kPieces; ++p)
          if (v == kPieceNames[p]) S.piece = p;
      } else if (k == "army") {
        S.army = -1;
        for (int a = 0; a < kArmies; ++a)
          if (v == kArmyNames[a]) S.army = a;
      } else if (k == "bpm") S.bpm = x;
      else if (k == "beat") S.beat = (int)x;
      else if (k == "bar") S.bar = (int)x;
      else if (k == "bars") S.bars = (int)x;
      else if (k == "swing") S.swing = x;
      else if (k == "echo") S.echo = (int)x;
      else if (k == "gain") S.gain = x;
      else warning("song", "unknown setting " + head[i]);
    }
    std::string where = S.title.empty() ? std::string("song") : S.title;
    if (S.army < 0 || S.piece < 0 || S.bpm <= 0 || S.beat <= 0 || S.bar <= 0 || S.bars <= 0) {
      warning(where, "bad song header");
      return;
    }
    S.sps = (double)SR * 60.0 / ((double)S.bpm * (double)S.beat);
    S.len = stepSample(S, S.bars * S.bar);
    S.echoLen = std::min(kEcho - 1, (int)((double)S.echo * S.sps));
    const int total = S.bars * S.bar;

    // the chords, one per step
    std::vector<Chord> chordAt;
    for (size_t li = 1; li < lines.size(); ++li) {
      std::string key = lines[li].substr(0, lines[li].find_first_of(" \t:"));
      if (key != "chords") continue;
      size_t colon = lines[li].find(':');
      std::vector<std::vector<std::string>> bs = bars(colon == std::string::npos ? "" : lines[li].substr(colon + 1), false);
      for (const auto& b : bs) {
        int used = 0;
        for (const std::string& t : b) {
          Chord c;
          int pc = 0;
          int n = pitchClass(t, 0, pc);
          size_t colonAt = t.find(':');
          std::string q = t.substr((size_t)n, colonAt == std::string::npos ? std::string::npos : colonAt - n);
          if (n == 0 || !chordOf(q, c)) {
            warning(where, "unknown chord " + t);
            continue;
          }
          c.root = pc;
          int dur = colonAt == std::string::npos ? S.bar - used % S.bar : std::atoi(t.c_str() + colonAt + 1);
          if (dur <= 0) dur = S.bar;
          for (int k = 0; k < dur; ++k) chordAt.push_back(c);
          used += dur;
        }
        if (used % S.bar != 0) warning(where, "chords: a bar of " + std::to_string(used) + " steps");
      }
    }
    if (chordAt.empty()) chordAt.push_back(Chord());

    std::vector<Event> evs;
    int seq = 0;
    std::string lastData;
    for (size_t li = 1; li < lines.size(); ++li) {
      const std::string& line = lines[li];
      size_t colon = line.find(':');
      std::vector<std::string> tk = splitWS(line.substr(0, colon));
      if (tk.empty() || tk[0] == "chords") continue;
      const std::string& kind = tk[0];
      if ((kind != "drum" && kind != "pat" && kind != "mel") || tk.size() < 2 || colon == std::string::npos) {
        warning(where, "bad line: " + line);
        continue;
      }
      int pi = patchIndex(tk[1]);
      if (pi < 0) {
        warning(where, "unknown instrument " + tk[1]);
        continue;
      }
      std::string part = where + " " + kind + " " + tk[1];
      Track T;
      T.patch = pi;
      T.rev = patches[(size_t)pi].rev;
      T.del = patches[(size_t)pi].del;
      int lo = 48, tr = 0, drumNote = 0;
      float gate = 0.9f;
      std::vector<bool> act((size_t)S.bars, true);
      for (size_t i = 2; i < tk.size(); ++i) {
        size_t eq = tk[i].find('=');
        if (eq == std::string::npos) {
          warning(part, "bad setting " + tk[i]);
          continue;
        }
        std::string k = tk[i].substr(0, eq), v = tk[i].substr(eq + 1);
        float x = std::strtof(v.c_str(), nullptr);
        if (k == "vol") T.vol = x;
        else if (k == "pan") T.pan = x;
        else if (k == "rev") T.rev = x;
        else if (k == "del") T.del = x;
        else if (k == "gate") gate = x;
        else if (k == "tr") tr = std::atoi(v.c_str());
        else if (k == "lo" || k == "note") {
          int n = noteNumber(v);
          if (n < 0) warning(part, "bad note " + v);
          else if (k == "lo") lo = n;
          else drumNote = n;
        } else if (k == "bars") {
          std::fill(act.begin(), act.end(), false);
          size_t a = 0;
          while (a <= v.size()) {
            size_t b = v.find(',', a);
            if (b == std::string::npos) b = v.size();
            std::string r = v.substr(a, b - a);
            size_t dash = r.find('-');
            int x0 = std::atoi(r.c_str()), x1 = dash == std::string::npos ? x0 : std::atoi(r.c_str() + dash + 1);
            if (x0 < 1 || x1 > S.bars || x1 < x0) warning(part, "bad bars " + r);
            for (int bb = std::max(1, x0); bb <= std::min(S.bars, x1); ++bb) act[(size_t)bb - 1] = true;
            a = b + 1;
          }
        } else warning(part, "unknown setting " + tk[i]);
      }

      // the part, as one cell per step
      std::vector<Cell> cells;
      std::vector<Spec> specs;
      std::string data = line.substr(colon + 1);
      if (trim(data) == "same") data = lastData;  // the part before, on another instrument
      else lastData = data;
      std::vector<std::vector<std::string>> bs = bars(data, kind == "drum");
      int dur = 4;
      for (const auto& b : bs) {
        size_t before = cells.size();
        for (const std::string& t : b) {
          Cell c;
          if (kind == "drum") {
            char ch = t[0];
            if (ch == 'x' || ch == 'X' || ch == 'o') {
              c.type = 1;
              c.vel = ch == 'X' ? 1.0f : ch == 'o' ? 0.45f : 0.8f;
            } else if (ch != '.' && ch != '-') warning(part, "bad drum hit " + t);
            cells.push_back(c);
            continue;
          }
          if (kind == "pat") {
            if (t == ".") {
            } else if (t == "-") c.type = 2;
            else {
              std::string body = t;
              while (!body.empty() && (body.back() == '!' || body.back() == '?')) {
                c.vel = body.back() == '!' ? 1.0f : 0.5f;
                body.pop_back();
              }
              Spec sp;
              size_t n = 0;
              if (!body.empty() && (body[0] == 'c' || body[0] == 'p')) {
                sp.kind = body[0] == 'c' ? 2 : 3;
                n = 1;
              } else {
                while (n < body.size() && body[n] >= '0' && body[n] <= '9') ++n;
                sp.kind = 1;
                sp.value = n > 0 ? std::atoi(body.substr(0, n).c_str()) : 0;
              }
              bool ok = sp.kind != 1 || (sp.value >= 1 && sp.value <= 22);
              for (; n < body.size(); ++n) {
                if (body[n] == '\'') sp.oct += 1;
                else if (body[n] == ',') sp.oct -= 1;
                else ok = false;
              }
              if (!ok) {
                warning(part, "bad step " + t);
                cells.push_back(Cell());
                continue;
              }
              c.type = 1;
              c.first = (int)specs.size();
              c.count = 1;
              specs.push_back(sp);
            }
            cells.push_back(c);
            continue;
          }
          // mel: notes with durations
          std::string body = t;
          size_t cut = body.find_first_of(":!?");
          std::string what = body.substr(0, cut);
          if (cut != std::string::npos) {
            for (size_t k = cut; k < body.size(); ++k) {
              if (body[k] == '!') c.vel = 1.0f;
              else if (body[k] == '?') c.vel = 0.5f;
              else if (body[k] == ':') {
                size_t e = k + 1;
                while (e < body.size() && body[e] >= '0' && body[e] <= '9') ++e;
                dur = std::atoi(body.substr(k + 1, e - k - 1).c_str());
                k = e - 1;
              }
            }
          }
          if (dur <= 0) {
            warning(part, "bad length " + t);
            dur = 1;
          }
          int type = 1;
          if (what == "r") type = 0;
          else if (what == "_") type = 2;
          else {
            c.first = (int)specs.size();
            size_t a = 0;
            while (a <= what.size()) {
              size_t e = what.find('+', a);
              if (e == std::string::npos) e = what.size();
              int note = noteNumber(what.substr(a, e - a));
              if (note < 0) warning(part, "bad note " + t);
              else {
                Spec sp;
                sp.value = note;
                specs.push_back(sp);
              }
              a = e + 1;
            }
            c.count = (int)specs.size() - c.first;
            if (c.count == 0) type = 0;
          }
          c.type = type;
          cells.push_back(c);
          Cell rest = c;
          rest.type = type == 0 ? 0 : 2;
          for (int k = 1; k < dur; ++k) cells.push_back(rest);
        }
        int n = (int)(cells.size() - before);
        if (n % S.bar != 0) warning(part, "a bar of " + std::to_string(n) + " steps");
      }
      if (cells.empty()) continue;

      // the events
      int ti = (int)tracks.size();
      tracks.push_back(T);
      std::vector<int> open;
      int openStart = 0;
      auto close = [&](int step) {
        for (int k : open) {
          double ln = (stepTime(S, step) - stepTime(S, openStart)) * (double)gate;
          evs[(size_t)k].len = std::max(1, (int)ln);
        }
        open.clear();
      };
      const int L = (int)cells.size();
      int ai = 0;
      for (int s = 0; s < total; ++s) {
        if (!act[(size_t)(s / S.bar)]) {
          close(s);
          continue;
        }
        const Cell& c = cells[(size_t)(ai % L)];
        ++ai;
        if (c.type == 0) close(s);
        else if (c.type == 1) {
          close(s);
          const Chord& ch = chordAt[(size_t)s % chordAt.size()];
          int root = lo + mod12(ch.root - lo);
          std::vector<int> notes;
          for (int k = 0; k < c.count; ++k) {
            const Spec& sp = specs[(size_t)(c.first + k)];
            if (kind == "drum") notes.push_back(drumNote);
            else if (sp.kind == 0) notes.push_back(sp.value + tr);
            else if (sp.kind == 1) notes.push_back(root + degreeSemis(sp.value, ch) + 12 * sp.oct + tr);
            else if (sp.kind == 2)
              for (int q = 0; q < ch.n; ++q) notes.push_back(lo + mod12(ch.root + ch.tones[q] - lo) + 12 * sp.oct + tr);
            else {
              notes.push_back(root + 12 * sp.oct + tr);
              notes.push_back(root + 7 + 12 * sp.oct + tr);
              notes.push_back(root + 12 + 12 * sp.oct + tr);
            }
          }
          if (kind == "drum") notes.assign(1, drumNote);
          for (int note : notes) {
            if (note < 0 || note > 127) continue;
            Event e;
            e.t = stepSample(S, s);
            e.track = ti;
            e.note = note;
            e.vel = c.vel;
            e.seq = seq++;
            open.push_back((int)evs.size());
            evs.push_back(e);
          }
          openStart = s;
        }
      }
      close(total);
    }
    std::sort(evs.begin(), evs.end(), [](const Event& a, const Event& b) { return a.t != b.t ? a.t < b.t : a.seq < b.seq; });
    S.first = (int)events.size();
    S.count = (int)evs.size();
    events.insert(events.end(), evs.begin(), evs.end());
    songs.push_back(S);
  }

  // ---- voices ----------------------------------------------------------
  int allocVoice() {
    for (int i = 0; i < kVoices; ++i)
      if (voices[i].patch < 0) return i;
    int best = -1;
    for (int i = 0; i < kVoices; ++i)  // the quietest released note
      if (voices[i].stage == 2 && (best < 0 || voices[i].env < voices[best].env)) best = i;
    if (best >= 0) return best;
    best = 0;
    for (int i = 1; i < kVoices; ++i)
      if (voices[i].age > voices[best].age) best = i;
    return best;
  }

  void noteOn(int pi, int note, float vel, int len, const Track& T) {
    int vi = allocVoice();
    Voice& v = voices[vi];
    const Patch& P = patches[(size_t)pi];
    v = Voice();
    v.patch = pi;
    v.gateAt = std::max(1, len);
    v.freq = note > 0 ? mtof(note) : P.f;
    if (P.glide > 0 && note > 0) {  // slides in from the instrument's last note
      if (lastFreq[(size_t)pi] > 0) v.slide = 12.0f * std::log2(lastFreq[(size_t)pi] / v.freq);
      lastFreq[(size_t)pi] = v.freq;
    }
    v.amp = vel * T.vol * P.gain;
    float pan = std::min(1.0f, std::max(-1.0f, P.pan + T.pan));
    float an = (pan + 1.0f) * 0.25f * PI_F;
    v.gl = std::cos(an);
    v.gr = std::sin(an);
    v.rev = T.rev;
    v.del = T.del;
    v.rng.s = noise.u32() | 1u;
    v.aInc = 1.0f / (std::max(P.a, 0.0005f) * (float)SR);
    // decay and release times are to -60 dB
    v.dCoef = std::exp(-6.9078f / (std::max(P.d, 0.001f) * (float)SR));
    v.rCoef = std::exp(-6.9078f / (std::max(P.r, 0.001f) * (float)SR));
    if (P.kind == K_SYNTH) {
      if (P.uni == 2) {
        v.det[0] = std::exp2(P.det / 2400.0f);
        v.det[1] = std::exp2(-P.det / 2400.0f);
      } else if (P.uni == 3) {
        v.det[1] = std::exp2(P.det / 1200.0f);
        v.det[2] = std::exp2(-P.det / 1200.0f);
      }
      if (P.uni > 1)
        for (int u = 0; u < P.uni; ++u) v.ph[u] = (double)v.rng.next();
      if (P.filt == F_VOX) {
        v.fx[0].set(730.0f, 0.88f);
        v.fx[1].set(1090.0f, 0.9f);
        v.fx[2].set(2440.0f, 0.93f);
      }
    } else if (P.kind == K_KS) {
      float* buf = &ks[(size_t)vi * kKS];
      v.ksN = std::min(std::max((float)SR / v.freq - P.damp, 2.0f), (float)(kKS - 3));
      // the string's shape when plucked (a triangle, sharper the brighter) and a little noise
      int m = (int)v.ksN + 2;
      float e = 0, mean = 0, peak = 1e-6f, at = 0.1f + 0.3f * (1.0f - P.bright);
      for (int i = 0; i < m; ++i) {
        float x = (float)i / (float)m;
        e += P.bright * (v.rng.bi() - e);
        buf[i] = (x < at ? x / at : (1.0f - x) / (1.0f - at)) + 0.35f * e;
        mean += buf[i];
      }
      mean /= (float)m;
      for (int i = 0; i < m; ++i) {
        buf[i] -= mean;
        peak = std::max(peak, std::fabs(buf[i]));
      }
      for (int i = 0; i < m; ++i) buf[i] /= peak;
      v.ksW = m;
      v.ksG = std::exp(-6.9078f / (v.freq * std::max(P.decay, 0.01f)));
    } else if (P.kind == K_DRUM) {
      v.dAmp = 1.0f;
      v.dCoef2 = v.dCoef;
      v.nAmp = P.snap;
      v.nCoef = std::exp(-1.0f / (std::max(P.ndec, 0.001f) * (float)SR));
      if (P.wave == D_MEMBRANE) v.fx[0].set(P.nf, 0.1f);
      else if (P.wave == D_SNARE) {
        v.fx[0].set(P.nf, 0.2f);
        v.e1 = P.tone;
        v.c1 = std::exp(-1.0f / (0.055f * (float)SR));
      } else if (P.wave == D_CLAP) {
        v.fx[0].set(P.nf, 0.5f);
        v.e1 = 1.0f;
        v.c1 = std::exp(-1.0f / (0.0045f * (float)SR));
        v.c2 = v.dCoef2;
      } else if (P.wave == D_METAL) {
        static const float mf[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
        for (int k = 0; k < 6; ++k) {
          v.inc[k] = (double)(mf[k] * (P.f / 100.0f)) / (double)SR;
          v.ph[k] = (double)v.rng.next();
        }
        v.fx[0].set(P.nf, 0.3f);
      } else {
        v.fx[0].set(P.nf, 0.3f);
      }
    }
  }

  // control-rate updates, every 32 samples of a voice's life
  void control(Voice& v, const Patch& P) {
    float t = (float)v.age / (float)SR;
    if (P.kind == K_DRUM) {
      if (P.wave == D_MEMBRANE) {
        float f = v.freq * (1.0f + P.pamt * std::exp(-t / P.pdec));
        v.inc[0] = (double)f / (double)SR;
        v.inc[1] = (double)(f * P.p2) / (double)SR;
      } else if (P.wave == D_SNARE) {
        float f = v.freq * (1.0f + 0.15f * std::exp(-t / 0.02f));
        v.inc[0] = (double)f / (double)SR;
        v.inc[1] = (double)(f * 1.84f) / (double)SR;
      }
      return;
    }
    float semis = 0;
    if (P.vibDepth > 0) {
      float ramp = std::min(1.0f, std::max(0.0f, (t - P.vibDelay) / 0.3f));
      semis += P.vibDepth * ramp * sinT(frac(P.vibRate * t));
    }
    if (P.scoop > 0) semis -= P.scoop * std::exp(-t / 0.05f);
    if (P.bend != 0) semis += P.bend * std::exp(-t / P.bendTime);
    if (v.slide != 0) semis += v.slide * std::exp(-t / P.glide);
    float pm = std::exp2(semis / 12.0f);
    double base = (double)(v.freq * pm) / (double)SR;
    for (int u = 0; u < 3; ++u) v.inc[u] = base * (double)v.det[u];
    if (P.filt == F_LP || P.filt == F_BP) {
      float fe = (P.fatk > 0 && t < P.fatk) ? t / P.fatk : std::exp(-(t - P.fatk) / P.fdec);
      float wah = P.wahDepth > 0 ? std::exp2(P.wahDepth * sinT(frac(P.wahRate * t))) : 1.0f;
      v.flt.set((P.hz + P.cut * v.freq) * std::exp2(P.fenv * fe) * wah, P.res);
    }
    if (P.tremDepth > 0) v.trem = 1.0f - P.tremDepth * (0.5f + 0.5f * sinT(frac(P.tremRate * t)));
    if (P.kind == K_FM) {
      v.idx = P.index * std::exp(-t / P.idec) / (float)TAU;
      v.p2g = P.p2amp * std::exp(-t / P.p2dec);
    }
  }

  // amplitude envelope; false when the note has died away
  bool envelope(Voice& v, const Patch& P) {
    if (v.age == v.gateAt && v.stage < 2) v.stage = 2;
    if (v.stage == 0) {
      v.env += v.aInc;
      if (v.env >= 1.0f) {
        v.env = 1.0f;
        v.stage = 1;
      }
    } else if (v.stage == 1) {
      v.env = P.s + (v.env - P.s) * v.dCoef;
      if (P.s <= 0.0f && v.env < 1e-3f) return false;
    } else {
      v.env *= v.rCoef;
      if (v.env < 1e-3f) return false;
    }
    return true;
  }

  float filter(Voice& v, const Patch& P, float x) {
    float lp, bp;
    switch (P.filt) {
      case F_LP: v.flt.run(x, lp, bp); return lp;
      case F_BP: v.flt.run(x, lp, bp); return bp;
      case F_VOX: {
        float y = x * 0.04f;
        static const float g[3] = {1.0f, 0.55f, 0.25f};
        for (int k = 0; k < 3; ++k) {
          v.fx[k].run(x, lp, bp);
          y += bp * v.fx[k].k * g[k];
        }
        return y * 2.0f;
      }
      default: return x;
    }
  }

  void out(Voice& v, int i, float y) {
    dryL[i] += y * v.gl;
    dryR[i] += y * v.gr;
    revIn[i] += y * v.rev;
    delIn[i] += y * v.del;
  }

  void runVoice(int vi, int i0, int n) {
    Voice& v = voices[vi];
    const Patch& P = patches[(size_t)v.patch];
    static const float uniNorm[4] = {1.0f, 1.0f, 0.75f, 0.6f};
    for (int k = 0; k < n; ++k) {
      if ((v.age & 31) == 0) control(v, P);
      float x = 0;
      bool alive = true;
      if (P.kind == K_DRUM) {
        float y = 0;
        float lp, bp;
        switch (P.wave) {
          case D_MEMBRANE: {
            v.ph[0] += v.inc[0];
            if (v.ph[0] >= 1.0) v.ph[0] -= 1.0;
            y = sinT((float)v.ph[0]);
            if (P.p2amp > 0) {
              v.ph[1] += v.inc[1];
              if (v.ph[1] >= 1.0) v.ph[1] -= 1.0;
              y += P.p2amp * sinT((float)v.ph[1]);
            }
            y *= v.dAmp;
            v.fx[0].run(v.rng.bi(), lp, bp);
            y += lp * v.nAmp;
            v.dAmp *= v.dCoef2;
            v.nAmp *= v.nCoef;
            if (P.drive > 0) y = std::tanh(y * P.drive);
            alive = v.dAmp > 1e-3f || v.nAmp > 1e-3f;
            break;
          }
          case D_SNARE: {
            for (int q = 0; q < 2; ++q) {
              v.ph[q] += v.inc[q];
              if (v.ph[q] >= 1.0) v.ph[q] -= 1.0;
            }
            float tone = (sinT((float)v.ph[0]) + 0.5f * sinT((float)v.ph[1])) * v.e1;
            float xin = v.rng.bi();
            v.fx[0].run(xin, lp, bp);
            y = tone + (xin - v.fx[0].k * bp - lp) * v.nAmp;  // the snares: high-passed noise
            v.e1 *= v.c1;
            v.nAmp *= v.dCoef2;
            alive = v.e1 > 1e-3f || v.nAmp > 1e-3f;
            break;
          }
          case D_CLAP: {
            if (v.age == 441 || v.age == 882) v.e1 = 1.0f;
            if (v.age == 1323) v.e2 = 0.85f;
            v.fx[0].run(v.rng.bi(), lp, bp);
            y = bp * (v.e1 + v.e2) * 2.5f;
            v.e1 *= v.c1;
            v.e2 *= v.c2;
            alive = v.age < 1323 || v.e2 > 1e-3f;
            break;
          }
          case D_METAL: {
            float m = 0;
            for (int q = 0; q < 6; ++q) {
              v.ph[q] += v.inc[q];
              if (v.ph[q] >= 1.0) v.ph[q] -= 1.0;
              m += v.ph[q] < 0.5 ? 1.0f : -1.0f;
            }
            float xin = P.metal * m * (1.0f / 6.0f) + (1.0f - P.metal) * v.rng.bi();
            v.fx[0].run(xin, lp, bp);
            float hp = xin - v.fx[0].k * bp - lp;
            float att = P.a > 0.001f && v.age < (int)(P.a * (float)SR) ? (float)v.age / (P.a * (float)SR) : 1.0f;
            y = hp * v.dAmp * att;
            v.dAmp *= v.dCoef2;
            alive = v.dAmp > 1e-3f;
            break;
          }
          default: {  // shaker
            v.fx[0].run(v.rng.bi(), lp, bp);
            float att = v.age < (int)(P.a * (float)SR) ? (float)v.age / (P.a * (float)SR) : 1.0f;
            y = bp * v.dAmp * att * 1.5f;
            if (v.age >= (int)(P.a * (float)SR)) v.dAmp *= v.dCoef2;
            alive = v.dAmp > 1e-3f;
            break;
          }
        }
        if (v.age > 6 * SR) alive = false;
        out(v, i0 + k, y * v.amp);
      } else {
        alive = envelope(v, P);
        if (P.kind == K_SYNTH) {
          if (P.wave <= W_TRI) {
            for (int u = 0; u < P.uni; ++u) {
              double dt = v.inc[u];
              double& ph = v.ph[u];
              ph += dt;
              if (ph >= 1.0) ph -= 1.0;
              if (P.wave == W_SAW) x += (float)(2.0 * ph - 1.0) - polyblep(ph, dt);
              else if (P.wave == W_SQR) {
                double p2 = ph + (1.0 - (double)P.pw);
                if (p2 >= 1.0) p2 -= 1.0;
                x += (ph < (double)P.pw ? 1.0f : -1.0f) + polyblep(ph, dt) - polyblep(p2, dt) - (2.0f * P.pw - 1.0f);
              } else x += (float)(4.0 * std::fabs(ph - 0.5) - 1.0);
            }
          } else {
            const float* tab = table(P.wave);
            for (int u = 0; u < P.uni; ++u) {
              v.ph[u] += v.inc[u];
              if (v.ph[u] >= 1.0) v.ph[u] -= 1.0;
              x += lookup(tab, (float)v.ph[u]);
            }
          }
          x *= uniNorm[P.uni];
          if (P.sub > 0) {
            v.phSub += v.inc[0] * 0.5;
            if (v.phSub >= 1.0) v.phSub -= 1.0;
            x += P.sub * sinT((float)v.phSub);
          }
          if (P.noise > 0) x += P.noise * v.rng.bi();
        } else if (P.kind == K_FM) {
          v.phM += v.inc[0] * (double)P.ratio;
          if (v.phM >= 1.0) v.phM -= 1.0;
          v.ph[0] += v.inc[0];
          if (v.ph[0] >= 1.0) v.ph[0] -= 1.0;
          x = sinT(frac((float)v.ph[0] + sinT((float)v.phM) * v.idx));
          if (P.p2amp > 0) {
            v.ph2 += v.inc[0] * (double)P.p2;
            if (v.ph2 >= 1.0) v.ph2 -= 1.0;
            x += v.p2g * sinT((float)v.ph2);
          }
        } else {  // plucked string
          float* buf = &ks[(size_t)vi * kKS];
          float rp = (float)v.ksW - v.ksN;
          if (rp < 0) rp += (float)kKS;
          int a = (int)rp;
          float fr = rp - (float)a;
          int b = (a + 1) & (kKS - 1);
          float s = buf[a] + (buf[b] - buf[a]) * fr;
          buf[v.ksW] = v.ksG * ((1.0f - P.damp) * s + P.damp * v.ksPrev);
          v.ksPrev = s;
          v.ksW = (v.ksW + 1) & (kKS - 1);
          x = s;
        }
        if (P.drive > 0) x = std::tanh(x * P.drive);
        x = filter(v, P, x);
        out(v, i0 + k, x * v.env * v.amp * v.trem);
      }
      ++v.age;
      if (!alive) {
        v.patch = -1;
        break;
      }
    }
  }

  // ---- the sequencer ---------------------------------------------------
  const Song& song() const { return songs[(size_t)cur]; }
  static int slotOf(const Song& s) { return s.army * kPieces + s.piece; }

  // A random theme of an army's piece (a slot), not 'avoid' if there is another.
  int randomSong(int slot, int avoid) {
    int n = 0;
    for (const Song& s : songs)
      if (slotOf(s) == slot) ++n;
    if (n == 0) return -1;
    int choices = n > 1 && avoid >= 0 && slotOf(songs[(size_t)avoid]) == slot ? n - 1 : n;
    int k = (int)(pick.u32() % (uint32_t)choices);
    for (size_t i = 0; i < songs.size(); ++i) {
      if (slotOf(songs[i]) != slot || (choices < n && (int)i == avoid)) continue;
      if (k-- == 0) return (int)i;
    }
    return -1;
  }

  bool hasSongs(int slot) const {
    for (const Song& s : songs)
      if (slotOf(s) == slot) return true;
    return false;
  }

  // The piece's slot in the army being played (the Living Marble one's if that army has no music for it).
  int slotFor(int piece) const {
    int s = army * kPieces + piece;
    return hasSongs(s) ? s : piece;
  }

  void releaseAll() {
    for (Voice& v : voices)
      if (v.patch >= 0 && patches[(size_t)v.patch].kind != K_DRUM && v.stage < 2) v.stage = 2;
  }

  void startSong(int s, int bar, bool mark) {
    releaseAll();
    cur = s;
    const Song& S = songs[(size_t)s];
    curBar = std::min(std::max(bar, 0), S.bars - 1);
    pos = stepSample(S, curBar * S.bar);
    barEnd = stepSample(S, (curBar + 1) * S.bar);
    ev = S.first;
    evEnd = S.first + S.count;
    while (ev < evEnd && events[(size_t)ev].t < pos) ++ev;
    since = 0;
    songGain = S.gain;
    echo.len = S.echoLen;
    lastSong[slotOf(S)] = s;
    pieceSong[slotOf(S)] = s;
    tail = 4 * SR;
    if (mark && crash >= 0) {
      Track T;
      T.rev = patches[(size_t)crash].rev;
      noteOn(crash, 0, 0.5f, 1, T);
    }
  }

  // The theme of an army's piece (a slot) where it left off (or a new one); false if it has none.
  bool startPiece(int p) {
    int s = pieceSong[p];
    if (s < 0) {
      s = randomSong(p, lastSong[p]);
      pieceBar[p] = 0;
    }
    if (s < 0) return false;
    bool mark = cur >= 0 && slotOf(song()) != p;
    startSong(s, pieceBar[p], mark);
    return true;
  }

  void startNow(int p) {
    fade = 1.0f;
    fadeTarget = 1.0f;
    startPiece(p);
  }

  // A bar line or an event is due at 'pos'.
  void boundary() {
    if (pos >= barEnd) {
      ++curBar;
      const Song& S = song();
      bool atEnd = curBar >= S.bars;
      if (single && atEnd) {
        cur = -1;
        return;
      }
      // a new theme waits for the bar line, and stays for at least two bars
      bool dwelt = since >= 2 * (int64_t)((double)S.bar * S.sps);
      int p = pending;
      int from = slotOf(S), fromBar = curBar;
      if (p >= 0 && p != from && (atEnd || dwelt) && hasSongs(p)) {
        pending = -1;
        pieceBar[from] = atEnd ? 0 : fromBar;
        if (atEnd) pieceSong[from] = -1;
        startPiece(p);
      } else if (atEnd) {
        int next = randomSong(from, cur);
        pieceBar[from] = 0;
        startSong(next, 0, false);
      } else {
        barEnd = stepSample(S, (curBar + 1) * S.bar);
      }
    }
    while (ev < evEnd && events[(size_t)ev].t <= pos) {
      const Event& e = events[(size_t)ev++];
      const Track& T = tracks[(size_t)e.track];
      noteOn(T.patch, e.note, e.vel, e.len, T);
    }
  }

  void halt() {
    if (cur >= 0) pieceBar[slotOf(song())] = curBar;
    cur = -1;
    pending = -1;
    for (Voice& v : voices) v.patch = -1;
    reverb.clear();
    echo.clear();
    dcL = dcR = dcOutL = dcOutR = 0;
    tail = 0;
  }

  void render(float* L, float* R, int frames, int step) {
    int done = 0;
    while (done < frames) {
      int n = std::min(frames - done, kBlock);
      block(L + (size_t)done * step, R + (size_t)done * step, n, step);
      done += n;
    }
  }

  void block(float* L, float* R, int n, int step) {
    bool any = cur >= 0;
    for (const Voice& v : voices)
      if (v.patch >= 0) any = true;
    if (!any && tail <= 0) return;
    for (int i = 0; i < n; ++i) dryL[i] = dryR[i] = revIn[i] = delIn[i] = 0;
    int i = 0;
    while (i < n) {
      int m = n - i;
      if (cur >= 0) {
        boundary();
        if (cur >= 0) {
          int64_t next = barEnd;
          if (ev < evEnd) next = std::min(next, events[(size_t)ev].t);
          if (next - pos < (int64_t)m) m = (int)(next - pos);
        }
      }
      for (int vi = 0; vi < kVoices; ++vi)
        if (voices[vi].patch >= 0) runVoice(vi, i, m);
      if (cur >= 0) {
        pos += m;
        since += m;
      }
      i += m;
    }
    bool sounding = cur >= 0;
    for (const Voice& v : voices)
      if (v.patch >= 0) sounding = true;
    if (sounding) tail = 4 * SR;
    else tail -= n;
    for (int k = 0; k < n; ++k) {
      float rl, rr, el, er;
      reverb.run(revIn[k], rl, rr);
      echo.run(delIn[k], el, er);
      if (fade < fadeTarget) fade = std::min(fadeTarget, fade + fadeStep);
      else if (fade > fadeTarget) fade = std::max(fadeTarget, fade - fadeStep);
      float target = 1.0f;
      if (duckHold > 0) {
        --duckHold;
        target = duckDepth;
      }
      duckGain += (target - duckGain) * (target < duckGain ? 0.002f : 0.00012f);
      volS += (volume - volS) * 0.002f;
      songGainS += (songGain - songGainS) * 0.002f;
      float g = volS * songGainS * fade * duckGain * kLevel;
      float xl = (dryL[k] + rl + el) * g, xr = (dryR[k] + rr + er) * g;
      dcOutL = xl - dcL + 0.9995f * dcOutL;
      dcOutR = xr - dcR + 0.9995f * dcOutR;
      dcL = xl;
      dcR = xr;
      L[(size_t)k * step] += dcOutL;
      R[(size_t)k * step] += dcOutR;
    }
    if (cur >= 0 && fadeTarget <= 0.0f && fade <= 0.0f) halt();
  }
};

// ---------------------------------------------------------------------------
static std::string gameScore() {
  std::string s;
  for (int i = 0; kScore[i]; ++i) s += kScore[i];
  return s;
}

Engine::Engine(const char* score) : d(new Impl(score ? score : gameScore().c_str())) {}
Engine::~Engine() { delete d; }

void Engine::play(int piece) {
  if (piece < 0 || piece >= kPieces) return;
  d->want = piece;
  if (!d->enabled) return;
  int s = d->slotFor(piece);
  if (d->cur < 0) d->startNow(s);
  else {
    d->fadeTarget = 1.0f;
    d->pending = Impl::slotOf(d->song()) == s ? -1 : s;
  }
}

void Engine::setArmy(int army) {
  army = army == 1 || army == 2 ? army : 0;
  if (army == d->army) return;
  d->army = army;
  if (d->cur < 0 || d->fadeTarget <= 0.0f) return;
  // the same piece's music (or the one waiting to come in) in the new army, from the next bar line
  int s = d->slotFor(d->pending >= 0 ? d->pending % kPieces : d->song().piece);
  d->pending = Impl::slotOf(d->song()) == s ? -1 : s;
}

void Engine::resume() {
  if (!d->enabled) return;
  if (d->cur >= 0) {
    d->fadeTarget = 1.0f;
    return;
  }
  int p = (int)(d->pick.u32() % (uint32_t)kPieces);  // a new game: any piece's music
  d->want = p;
  d->startNow(d->slotFor(p));
}

void Engine::stop() {
  d->pending = -1;
  if (d->cur >= 0) {
    d->fadeTarget = 0.0f;
    d->fadeStep = 1.0f / (2.5f * SR);
  }
}

void Engine::setEnabled(bool on) {
  if (on == d->enabled) return;
  d->enabled = on;
  if (!on) {
    d->pending = -1;
    if (d->cur >= 0) {
      d->fadeTarget = 0.0f;
      d->fadeStep = 1.0f / (0.4f * SR);
    }
  } else if (d->cur >= 0) {
    d->fadeTarget = 1.0f;
  } else {
    int p = d->want >= 0 ? d->want : (int)(d->pick.u32() % (uint32_t)kPieces);
    d->want = p;
    d->startNow(d->slotFor(p));
  }
}

void Engine::setVolume(float v) { d->volume = std::min(1.0f, std::max(0.0f, v)); }

void Engine::duck(int samples, float depth) {
  if (depth >= 1.0f || samples <= 0) return;
  if (d->duckHold <= 0 || depth < d->duckDepth) d->duckDepth = depth;
  d->duckHold = std::max(d->duckHold, samples);
}

void Engine::seed(uint32_t s) { d->pick.s = s ? s : 1u; }

int Engine::playing() const { return d->fadeTarget > 0.0f ? d->cur : -1; }

void Engine::render(float* left, float* right, int frames, int step) { d->render(left, right, frames, step); }

int Engine::songCount() const { return (int)d->songs.size(); }
int Engine::songPiece(int s) const { return d->songs[(size_t)s].piece; }
int Engine::songArmy(int s) const { return d->songs[(size_t)s].army; }
const std::string& Engine::songTitle(int s) const { return d->songs[(size_t)s].title; }
const std::vector<std::string>& Engine::warnings() const { return d->warn; }

std::vector<float> Engine::renderSong(int s) {
  Impl one(d->score.c_str());
  one.single = true;
  one.volume = one.volS = 1.0f;
  one.startSong(s, 0, false);
  one.songGainS = one.songGain;
  int64_t n = one.songs[(size_t)s].len + 3 * SR;
  std::vector<float> out((size_t)n * 2, 0.0f);
  for (int64_t i = 0; i < n; i += 4096) {
    int m = (int)std::min<int64_t>(4096, n - i);
    one.render(&out[(size_t)i * 2], &out[(size_t)i * 2 + 1], m, 2);
  }
  return out;
}

}  // namespace music
