#include "gpufab/topology.hpp"

namespace gpufab {

Topology Topology::clos2(std::int32_t hosts_per_leaf, std::int32_t leaves,
                         std::int32_t spines, std::int64_t host_gbps,
                         std::int64_t fabric_gbps, Ps prop_ps) {
  if (hosts_per_leaf <= 0 || leaves <= 0 || spines <= 0 || host_gbps <= 0 ||
      fabric_gbps <= 0 || prop_ps < 0) {
    throw std::invalid_argument("clos2: all parameters must be positive");
  }
  Topology t;
  t.hosts_per_leaf_ = hosts_per_leaf;
  t.leaves_ = leaves;
  t.spines_ = spines;
  for (std::int32_t l = 0; l < leaves; ++l) {
    const NodeId leaf = t.leaf_node(l);
    for (std::int32_t h = 0; h < hosts_per_leaf; ++h) {
      const NodeId host = l * hosts_per_leaf + h;
      t.add_link(host, leaf, host_gbps, prop_ps);
      t.add_link(leaf, host, host_gbps, prop_ps);
    }
    for (std::int32_t s = 0; s < spines; ++s) {
      const NodeId spine = t.spine_node(s);
      t.add_link(leaf, spine, fabric_gbps, prop_ps);
      t.add_link(spine, leaf, fabric_gbps, prop_ps);
    }
  }
  return t;
}

NodeKind Topology::kind(NodeId n) const {
  if (n < num_hosts()) return NodeKind::Host;
  if (n < num_hosts() + leaves_) return NodeKind::Leaf;
  if (n < num_nodes()) return NodeKind::Spine;
  throw std::out_of_range("kind: bad node id");
}

NodeId Topology::leaf_of_host(NodeId host) const {
  if (host < 0 || host >= num_hosts()) {
    throw std::out_of_range("leaf_of_host: bad host id");
  }
  return leaf_node(host / hosts_per_leaf_);
}

LinkId Topology::link_between(NodeId a, NodeId b) const {
  auto it = link_index_.find({a, b});
  if (it == link_index_.end()) {
    throw std::runtime_error("link_between: no link " + std::to_string(a) +
                             "->" + std::to_string(b));
  }
  return it->second;
}

LinkId Topology::add_link(NodeId src, NodeId dst, std::int64_t gbps,
                          Ps prop_ps) {
  const LinkId id = static_cast<LinkId>(links_.size());
  links_.push_back(Link{src, dst, gbps, prop_ps, 0});
  link_index_[{src, dst}] = id;
  return id;
}

}  // namespace gpufab
