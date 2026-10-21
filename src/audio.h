// Procedurally synthesised sound effects (no audio files), mixed in real time.
#pragma once

enum Sfx {
  SFX_STEP = 0,  // light stone footstep on wood
  SFX_THUD,      // heavy landing
  SFX_CLACK,     // stone on stone
  SFX_SHATTER,   // marble breaking
  SFX_WHOOSH,    // fast swing
  SFX_CLANG,     // steel ring
  SFX_BOOM,      // cannon
  SFX_EXPLODE,   // impact explosion
  SFX_CHARGE,    // magic charging
  SFX_ZAP,       // magic bolt
  SFX_CRACKLE,   // lightning
  SFX_CHIME,     // promotion / materialise
  SFX_SELECT,    // UI pick
  SFX_CHECK,     // check horn
  SFX_MATE,      // checkmate gong
  SFX_DEBRIS,    // small chunk bounce
  SFX_RUMBLE,    // low rumble
  // the medieval army
  SFX_HYMN,      // pipe organ and choir: the Dies irae
  SFX_ORGAN,     // one dark organ chord
  SFX_THUNDER,   // lightning crack and rolling thunder
  SFX_WIND,      // storm wind
  SFX_ROAR,      // dragon roar
  SFX_FIRE,      // dragon fire breath
  SFX_CHAIN,     // drawbridge chains and creaking timber
  SFX_GRIND,     // stone grinding as the gatehouse slides
  SFX_STOMP,     // a giant stone footstep
  SFX_HOOF,      // a hoof on the board
  SFX_NEIGH,     // a horse's whinny
  SFX_BELL,      // church bell
  SFX_SPLINTER,  // a lance shattering
  SFX_ARMOR,     // armoured footstep
  SFX_SLIDE,     // a throne sliding over the board
  SFX_BITE,      // jaws crunching stone
  // the aliens
  SFX_PEW,       // ray-gun shot
  SFX_UFO,       // flying-saucer warble
  SFX_THEREMIN,  // an eerie theremin tune
  SFX_BEAM,      // tractor beam
  SFX_GIBBER,    // alien chatter
  SFX_SLURP,     // a tongue slurping something in
  SFX_BURP,      // and the burp afterwards
  SFX_BOING,     // a spring
  SFX_WHISTLE,   // slide whistle (blasting off)
  SFX_SPLAT,     // a blob of goo landing
  SFX_MOO,       // a cow
  SFX_POP,       // pop!
  SFX_SHRINK,    // shrink ray
  SFX_HEATRAY,   // the tripod's heat ray
  SFX_HORN,      // the tripod's horn
  SFX_TWINKLE,   // a twinkle in the sky
  SFX_SIZZLE,    // acid
  SFX_HATCH,     // an egg cracking open, chirps
  SFX_DRONE,     // the mothership
  SFX_SPUTTER,   // a machine sputtering and dying
  SFX_COUNT
};

class Audio {
 public:
  Audio();
  ~Audio();
  bool init();
  void shutdown();
  bool ok() const { return ok_; }
  // pan -1..1, pitch multiplier
  void play(Sfx s, float volume = 1.0f, float pan = 0.0f, float pitch = 1.0f);
  void setVolume(float v);
  static bool dumpWav(const char* dir);  // debug: write every effect as a .wav
  float volume() const { return volume_; }

  struct Impl;

 private:
  Impl* impl_ = nullptr;
  bool ok_ = false;
  float volume_ = 0.8f;
};
