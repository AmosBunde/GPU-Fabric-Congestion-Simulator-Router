#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "gpufab/engine.hpp"
#include "gpufab/topology.hpp"

namespace gpufab {

// One flow, A to B. The Phase 1 walking-skeleton workload.
class SingleFlowWorkload : public IWorkload {
 public:
  SingleFlowWorkload(NodeId src, NodeId dst, std::int64_t bytes)
      : src_(src), dst_(dst), bytes_(bytes) {}
  std::string name() const override { return "single_flow"; }
  void start(Engine& engine) override;

 private:
  NodeId src_, dst_;
  std::int64_t bytes_;
};

// N synchronized senders to one receiver — the classic congestion pathology.
// Senders are spread round-robin over the other hosts, so the receiver's
// leaf->host link is the shared bottleneck.
class IncastWorkload : public IWorkload {
 public:
  IncastWorkload(const Topology& topo, std::int32_t senders, NodeId receiver,
                 std::int64_t bytes_per_sender);
  std::string name() const override { return "incast"; }
  void start(Engine& engine) override;

 private:
  std::vector<NodeId> senders_;
  NodeId receiver_;
  std::int64_t bytes_;
};

// Bulk-synchronous ring all-reduce over hosts 0..K-1: 2*(K-1) steps, each
// host sending data_bytes/K to its ring successor per step; the next step
// starts when every transfer of the current step has completed.
class RingAllReduceWorkload : public IWorkload {
 public:
  RingAllReduceWorkload(std::int32_t hosts, std::int64_t data_bytes);
  std::string name() const override { return "allreduce_ring"; }
  void start(Engine& engine) override;
  void on_flow_complete(Engine& engine, const Flow& flow) override;

  std::int32_t total_steps() const { return 2 * (hosts_ - 1); }

 private:
  void launch_step(Engine& engine, Ps at_ps);

  std::int32_t hosts_;
  std::int64_t slice_bytes_;
  std::int32_t step_ = 0;
  std::int32_t outstanding_ = 0;
  std::int64_t next_flow_id_ = 0;
};

// Seeded uniform permutation: every host sends to a derangement target
// (Sattolo's algorithm — a single cycle, so no host sends to itself).
class PermutationWorkload : public IWorkload {
 public:
  PermutationWorkload(const Topology& topo, std::int64_t bytes,
                      std::mt19937_64& rng);
  std::string name() const override { return "permutation"; }
  void start(Engine& engine) override;

  const std::vector<NodeId>& targets() const { return targets_; }

 private:
  std::vector<NodeId> targets_;  // targets_[h] = destination of host h
  std::int64_t bytes_;
};

}  // namespace gpufab
