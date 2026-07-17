#include "gpufab/router/flowlet.hpp"

#include <algorithm>
#include <vector>

#include "gpufab/rng.hpp"

namespace gpufab {

std::vector<NodeId> FlowletRouter::route(const Flow& flow,
                                         const FabricView& view) {
  const Topology& topo = view.topo();
  if (flow.src == flow.dst) return {flow.src};
  const NodeId src_leaf = topo.leaf_of_host(flow.src);
  const NodeId dst_leaf = topo.leaf_of_host(flow.dst);
  if (src_leaf == dst_leaf) return {flow.src, src_leaf, flow.dst};

  const std::int32_t n_spines = topo.num_spines();
  std::vector<std::int64_t> score(static_cast<std::size_t>(n_spines));
  std::int64_t best = -1;
  for (std::int32_t s = 0; s < n_spines; ++s) {
    const NodeId spine = topo.spine_node(s);
    const std::int64_t up = view.queue_bytes(topo.link_between(src_leaf, spine));
    const std::int64_t down =
        view.queue_bytes(topo.link_between(spine, dst_leaf));
    score[static_cast<std::size_t>(s)] = std::max(up, down);
    if (best < 0 || score[static_cast<std::size_t>(s)] < best) {
      best = score[static_cast<std::size_t>(s)];
    }
  }

  // Stickiness: keep the current spine while it is within the hysteresis
  // band of the best alternative.
  if (flow.path.size() == 5 && flow.path[1] == src_leaf) {
    const NodeId cur = flow.path[2];
    for (std::int32_t s = 0; s < n_spines; ++s) {
      if (topo.spine_node(s) == cur &&
          score[static_cast<std::size_t>(s)] <= best + hysteresis_bytes_) {
        return {flow.src, src_leaf, cur, dst_leaf, flow.dst};
      }
    }
  }

  // (Re)selection: salted-hash spread over every spine within the band, so
  // simultaneous deciders cannot herd onto one winner.
  std::vector<std::int32_t> candidates;
  for (std::int32_t s = 0; s < n_spines; ++s) {
    if (score[static_cast<std::size_t>(s)] <= best + hysteresis_bytes_) {
      candidates.push_back(s);
    }
  }
  const std::uint64_t h =
      splitmix64(static_cast<std::uint64_t>(flow.id) ^ salt_);
  const std::int32_t pick =
      candidates[h % static_cast<std::uint64_t>(candidates.size())];
  return {flow.src, src_leaf, topo.spine_node(pick), dst_leaf, flow.dst};
}

}  // namespace gpufab
