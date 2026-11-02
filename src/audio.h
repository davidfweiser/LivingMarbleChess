// Procedurally synthesised sound effects (no audio files) and the background
// music (music.h), mixed in real time.
#pragma once
#include <string>

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
  // the Romans
  SFX_SLASH,     // a blade cutting through flesh
  SFX_STAB,      // a blade driven in
  SFX_BLOOD,     // blood splashing on the board
  SFX_DRAW,      // a sword drawn from its scabbard
  SFX_PARRY,     // blade on blade
  SFX_SHIELD,    // a blow on a wooden shield with a bronze boss
  SFX_GRUNT,     // a soldier's grunt of effort
  SFX_SHOUT,     // a battle cry
  SFX_DEATH,     // a dying groan
  SFX_FALL,      // a body in armour hitting the ground
  SFX_CROWD,     // the arena's crowd roaring
  SFX_CORNU,     // a Roman horn call
  SFX_WHEELS,    // chariot wheels rumbling over the board
  SFX_WHIP,      // a whip crack
  SFX_CREAK,     // timber creaking (the siege tower)
  SFX_RAM,       // the battering ram striking
  SFX_SCORPION,  // the bolt-thrower loosing
  SFX_LION,      // a lion's roar
  SFX_GULP,      // drinking, then choking
  // the ghosts of captured pieces
  SFX_CHEER,     // a far-off little crowd of ghosts cheering
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
  static bool dumpWav(const char* dir);  // debug: write every effect and song as a .wav
  float volume() const { return volume_; }

  // The background music: every piece type has its own themes in each army (music.h).
  void playMusic(int piece);  // the music of this piece (0 pawn .. 5 king), from the next bar
  void setMusicArmy(int set); // whose music: 0 Living Marble, 1 medieval, 2 aliens, 3 Romans
  void resumeMusic();         // a new game: start it again if it was stopped
  void stopMusic();           // the game is over: fade it out
  void setMusic(bool on);
  void setMusicVolume(float v);
  bool music() const { return music_; }
  float musicVolume() const { return musicVolume_; }
  std::string nowPlaying() const;  // "Gallop (knight)", or "" while silent

  struct Impl;

 private:
  Impl* impl_ = nullptr;
  bool ok_ = false;
  float volume_ = 0.8f;
  bool music_ = true;
  float musicVolume_ = 0.7f;
  int musicArmy_ = 0;
};
