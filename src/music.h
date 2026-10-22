// The background music. Every type of chess piece has three upbeat themes of
// its own, written as little text scores (music_score.cpp) and played by a
// small real-time synthesiser: no audio files. When a piece moves, the music
// switches to one of its themes at the next bar line; each piece's theme picks
// up where it left off, and when one ends another of that piece's themes is
// chosen at random.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace music {

constexpr int kPieces = 6;  // pawn, knight, bishop, rook, queen, king

class Engine {
 public:
  explicit Engine(const char* score = nullptr);  // nullptr: the game's own score
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Control, from the game. Not thread-safe: call under the lock that guards render().
  void play(int piece);   // this piece's music (0 pawn .. 5 king), from the next bar line
  void resume();          // starts again after stop(), with a random piece's music
  void stop();            // fades out (the game is over)
  void setEnabled(bool on);
  void setVolume(float v);
  void duck(int samples, float depth);  // turned down to 'depth' while a musical sound effect plays
  void seed(uint32_t s);
  int playing() const;    // the song being played, or -1

  // Audio thread: adds the music to 'frames' stereo samples (left and right 'step' floats apart).
  // Never allocates.
  void render(float* left, float* right, int frames, int step);

  int songCount() const;
  int songPiece(int song) const;
  const std::string& songTitle(int song) const;
  // Plays one song through on its own (and its echo), as interleaved stereo; for --dump-sfx and tests.
  std::vector<float> renderSong(int song);
  // Problems found in the score (wrong bar lengths, unknown instruments ...); empty when all is well.
  const std::vector<std::string>& warnings() const;

  struct Impl;

 private:
  Impl* d;
};

}  // namespace music
