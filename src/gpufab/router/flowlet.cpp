#include "gpufab/router/flowlet.hpp"

#include <algorithm>

namespace gpufab {

std::vector<NodeId> FlowletRouter::route(const Flow& flow,
                                         const FabricView& view) {
  const Topology& topo = view.topo();
  if (flow.src == flow.dst) return {flow.src};
  const NodeId src_leaf = topo.leaf_of_host(flow.src);
  const NodeId dst_leaf = topo.leaf_of_host(flow.dst);
  if (src_leaf == dst_leaf) return {flow.src, src_leaf, flow.dst};

  std::int32_t best_spine = 0;
  std::int64_t best_score = -1;
  for (std::int32_t s = 0; s < topo.num_spines(); ++s) {
    const NodeId spine = topo.spine_node(s);
    const std::int64_t up = view.queue_bytes(topo.link_between(src_leaf, spine));
    const std::int64_t down =
        view.queue_bytes(topo.link_between(spine, dst_leaf));
    const std::int64_t score = std::max(up, down);
    if (best_score < 0 || score < best_score) {
      best_score = score;
      best_spine = s;
    }
  }
  return {flow.src, src_leaf, topo.spine_node(best_spine), dst_leaf, flow.dst};
}

}  // namespace gpufab
