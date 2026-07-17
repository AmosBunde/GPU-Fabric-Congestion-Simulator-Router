#pragma once

#include <cstdint>
#include <map>
#include <random>
#include <vector>

#include "gpufab/router/irouter.hpp"

namespace gpufab {

// RL adaptive router: tabular contextual-bandit Q-learning, online within
// the run (reported as such — there is no separate training phase).
//
//   State:  (src-leaf, dst-leaf, per-spine congestion bucket vector), where
//           each candidate spine's worse fabric hop is quantized into
//           {empty <64 KiB, busy <512 KiB, congested >=512 KiB}. Exactly the
//           FabricView the flowlet heuristic sees — information parity by
//           construction, so any delta is attributable to the policy.
//   Action: spine index.
//   Reward: negative experienced queueing delay (us) of delivered chunks,
//           reported by the engine per delivery and attributed to the flow's
//           most recent decision (a stated approximation).
//   Update: Q += alpha * (r - Q)   (bandit-style; decisions are treated as
//           independent given the congestion state).
//   Policy: epsilon-greedy with 1/(1+n/decay) decay, drawn from a named,
//           logged registry stream; unexplored actions start at Q=0, above
//           every negative-reward estimate, giving optimistic exploration.
//
// The contract holds: no wall-clock, no fabric mutation, seeded randomness.
class RlRouter : public IRouter {
 public:
  struct Params {
    double alpha = 0.1;
    double epsilon0 = 0.2;
    double epsilon_decay = 1000.0;  // decisions until epsilon halves
    std::int64_t bucket_lo_bytes = 64 * 1024;
    std::int64_t bucket_hi_bytes = 512 * 1024;
  };

  RlRouter(Params params, std::mt19937_64& rng)
      : params_(params), rng_(rng) {}

  std::string name() const override { return "rl"; }
  std::vector<NodeId> route(const Flow& flow, const FabricView& view) override;
  Ps reroute_gap_ps() const override { return 0; }  // decide per chunk
  void on_chunk_feedback(const Flow& flow, Ps queue_delay_ps) override;

  // Greedy (argmax-Q) spine for a state — exposed for tests.
  std::int32_t greedy_action(std::int64_t state_key) const;
  std::int64_t state_key(const Flow& flow, const FabricView& view) const;

 private:
  struct Cell {
    double q = 0.0;
    std::int64_t visits = 0;
  };

  Params params_;
  std::mt19937_64& rng_;
  std::map<std::int64_t, std::vector<Cell>> table_;  // state -> per-action Q
  std::map<std::int64_t, std::pair<std::int64_t, std::int32_t>>
      last_decision_;  // flow id -> (state, action)
  std::int64_t decisions_ = 0;
};

}  // namespace gpufab
