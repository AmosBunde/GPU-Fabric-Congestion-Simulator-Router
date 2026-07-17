#include "gpufab/router/ecmp.hpp"

#include "gpufab/rng.hpp"

namespace gpufab {

std::vector<NodeId> EcmpRouter::route(const Flow& flow, const Topology& topo) {
  if (flow.src == flow.dst) return {flow.src};
  const NodeId src_leaf = topo.leaf_of_host(flow.src);
  const NodeId dst_leaf = topo.leaf_of_host(flow.dst);
  if (src_leaf == dst_leaf) return {flow.src, src_leaf, flow.dst};

  const std::uint64_t h =
      splitmix64(static_cast<std::uint64_t>(flow.id) ^ salt_);
  const auto spine_idx =
      static_cast<std::int32_t>(h % static_cast<std::uint64_t>(topo.num_spines()));
  return {flow.src, src_leaf, topo.spine_node(spine_idx), dst_leaf, flow.dst};
}

}  // namespace gpufab
