// Alpha-beta chess engine running on a background thread.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "chess.h"

namespace chess {

struct AILevel {
  const char* name;
  int maxDepth;
  double timeLimit;  // seconds
  int evalNoise;     // centipawns of deterministic noise (weaker play)
};
extern const AILevel kAILevels[5];

class AIEngine {
 public:
  AIEngine();
  ~AIEngine();
  void start(const Board& b, const std::vector<uint64_t>& history, int level);
  bool thinking() const { return running_.load(); }
  bool done() const { return done_.load(); }
  Move result() const { return best_; }
  void cancel();
  // stats of last search
  int lastDepth() const { return depth_; }
  int lastScore() const { return score_; }
  uint64_t lastNodes() const { return nodes_; }

  // synchronous search (used for tests / benchmarking)
  Move searchSync(const Board& b, const std::vector<uint64_t>& history, int level);

 private:
  struct Impl;
  Impl* impl_;
  std::thread thread_;
  std::atomic<bool> running_{false}, done_{false}, stopFlag_{false};
  Move best_;
  int depth_ = 0, score_ = 0;
  uint64_t nodes_ = 0;
};

int evaluate(const Board& b);  // from side-to-move perspective

}  // namespace chess
