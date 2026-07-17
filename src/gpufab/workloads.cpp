#include "gpufab/workloads.hpp"

#include <stdexcept>

namespace gpufab {

void SingleFlowWorkload::start(Engine& engine) {
  Flow f;
  f.id = 0;
  f.src = src_;
  f.dst = dst_;
  f.bytes = bytes_;
  f.start_ps = 0;
  engine.add_flow(f);
}

IncastWorkload::IncastWorkload(const Topology& topo, std::int32_t senders,
                               NodeId receiver, std::int64_t bytes_per_sender)
    : receiver_(receiver), bytes_(bytes_per_sender) {
  if (senders < 1 || senders >= topo.num_hosts()) {
    throw std::invalid_argument("incast: senders must be in [1, hosts-1]");
  }
  for (std::int32_t i = 0; i < senders; ++i) {
    senders_.push_back((receiver + 1 + i) % topo.num_hosts());
  }
}

void IncastWorkload::start(Engine& engine) {
  std::int64_t id = 0;
  for (NodeId s : senders_) {
    Flow f;
    f.id = id++;
    f.src = s;
    f.dst = receiver_;
    f.bytes = bytes_;
    f.start_ps = 0;
    engine.add_flow(f);
  }
}

RingAllReduceWorkload::RingAllReduceWorkload(std::int32_t hosts,
                                             std::int64_t data_bytes)
    : hosts_(hosts) {
  if (hosts < 2) throw std::invalid_argument("allreduce_ring: hosts >= 2");
  if (data_bytes % hosts != 0) {
    throw std::invalid_argument(
        "allreduce_ring: data_bytes must be divisible by hosts");
  }
  slice_bytes_ = data_bytes / hosts;
}

void RingAllReduceWorkload::start(Engine& engine) { launch_step(engine, 0); }

void RingAllReduceWorkload::launch_step(Engine& engine, Ps at_ps) {
  outstanding_ = hosts_;
  for (std::int32_t h = 0; h < hosts_; ++h) {
    Flow f;
    f.id = next_flow_id_++;
    f.src = h;
    f.dst = (h + 1) % hosts_;
    f.bytes = slice_bytes_;
    f.start_ps = at_ps;
    engine.add_flow(f);
  }
}

void RingAllReduceWorkload::on_flow_complete(Engine& engine, const Flow& flow) {
  if (--outstanding_ > 0) return;
  if (++step_ >= total_steps()) return;
  launch_step(engine, flow.end_ps);  // barrier: next step starts now
}

PermutationWorkload::PermutationWorkload(const Topology& topo,
                                         std::int64_t bytes,
                                         std::mt19937_64& rng)
    : bytes_(bytes) {
  const std::int32_t n = topo.num_hosts();
  if (n < 2) throw std::invalid_argument("permutation: needs >= 2 hosts");
  // Sattolo's algorithm: uniform over single-cycle permutations, hence a
  // derangement, in one deterministic pass over the seeded stream.
  std::vector<NodeId> p(static_cast<std::size_t>(n));
  for (std::int32_t i = 0; i < n; ++i) p[static_cast<std::size_t>(i)] = i;
  for (std::int32_t i = n - 1; i > 0; --i) {
    const auto j = static_cast<std::int32_t>(
        rng() % static_cast<std::uint64_t>(i));  // j in [0, i)
    std::swap(p[static_cast<std::size_t>(i)], p[static_cast<std::size_t>(j)]);
  }
  targets_ = p;
}

void PermutationWorkload::start(Engine& engine) {
  for (std::size_t h = 0; h < targets_.size(); ++h) {
    Flow f;
    f.id = static_cast<std::int64_t>(h);
    f.src = static_cast<NodeId>(h);
    f.dst = targets_[h];
    f.bytes = bytes_;
    f.start_ps = 0;
    engine.add_flow(f);
  }
}

}  // namespace gpufab
