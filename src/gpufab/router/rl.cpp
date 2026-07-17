#include "gpufab/router/rl.hpp"

#include <algorithm>

namespace gpufab {

namespace {
std::int32_t bucket(std::int64_t queue_bytes, std::int64_t lo,
                    std::int64_t hi) {
  if (queue_bytes < lo) return 0;
  if (queue_bytes < hi) return 1;
  return 2;
}
}  // namespace

std::int64_t RlRouter::state_key(const Flow& flow,
                                 const FabricView& view) const {
  const Topology& topo = view.topo();
  const NodeId src_leaf = topo.leaf_of_host(flow.src);
  const NodeId dst_leaf = topo.leaf_of_host(flow.dst);
  std::int64_t key = src_leaf * topo.num_leaves() + dst_leaf;
  for (std::int32_t s = 0; s < topo.num_spines(); ++s) {
    const NodeId spine = topo.spine_node(s);
    const std::int64_t worse =
        std::max(view.queue_bytes(topo.link_between(src_leaf, spine)),
                 view.queue_bytes(topo.link_between(spine, dst_leaf)));
    key = key * 3 + bucket(worse, params_.bucket_lo_bytes,
                           params_.bucket_hi_bytes);
  }
  return key;
}

std::int32_t RlRouter::greedy_action(std::int64_t state_key) const {
  auto it = table_.find(state_key);
  if (it == table_.end()) return 0;
  const auto& cells = it->second;
  std::int32_t best = 0;
  for (std::int32_t a = 1; a < static_cast<std::int32_t>(cells.size()); ++a) {
    if (cells[static_cast<std::size_t>(a)].q >
        cells[static_cast<std::size_t>(best)].q) {
      best = a;
    }
  }
  return best;
}

std::vector<NodeId> RlRouter::route(const Flow& flow, const FabricView& view) {
  const Topology& topo = view.topo();
  if (flow.src == flow.dst) return {flow.src};
  const NodeId src_leaf = topo.leaf_of_host(flow.src);
  const NodeId dst_leaf = topo.leaf_of_host(flow.dst);
  if (src_leaf == dst_leaf) return {flow.src, src_leaf, flow.dst};

  const std::int64_t key = state_key(flow, view);
  auto& cells = table_[key];
  if (cells.empty()) {
    cells.assign(static_cast<std::size_t>(topo.num_spines()), Cell{});
  }

  ++decisions_;
  const double epsilon =
      params_.epsilon0 /
      (1.0 + static_cast<double>(decisions_) / params_.epsilon_decay);
  std::int32_t action;
  if (std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < epsilon) {
    action = static_cast<std::int32_t>(
        rng_() % static_cast<std::uint64_t>(topo.num_spines()));
  } else {
    action = greedy_action(key);
  }

  last_decision_[flow.id] = {key, action};
  return {flow.src, src_leaf, topo.spine_node(action), dst_leaf, flow.dst};
}

void RlRouter::on_chunk_feedback(const Flow& flow, Ps queue_delay_ps) {
  auto it = last_decision_.find(flow.id);
  if (it == last_decision_.end()) return;  // same-leaf flow: no decision
  const auto [key, action] = it->second;
  auto& cell = table_[key][static_cast<std::size_t>(action)];
  const double reward =
      -static_cast<double>(queue_delay_ps) / static_cast<double>(kPsPerUs);
  cell.q += params_.alpha * (reward - cell.q);
  ++cell.visits;
}

}  // namespace gpufab
