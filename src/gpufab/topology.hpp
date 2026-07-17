#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "gpufab/time.hpp"

namespace gpufab {

using NodeId = std::int32_t;
using LinkId = std::int32_t;

enum class NodeKind { Host, Leaf, Spine };

struct Link {
  NodeId src = -1;
  NodeId dst = -1;
  std::int64_t gbps = 0;
  Ps prop_ps = 0;
  // Virtual time until which the transmitter is serializing earlier chunks.
  Ps busy_until_ps = 0;

  // Egress queue model: bytes standing in the FIFO (waiting + in service),
  // tail-dropped against buffer_bytes; chunks are ECN-marked on enqueue when
  // the standing queue is at or above ecn_threshold_bytes. Defaults are
  // effectively infinite / never-mark; main.cpp applies configured values.
  std::int64_t queue_bytes = 0;
  std::int64_t buffer_bytes = INT64_MAX / 4;
  std::int64_t ecn_threshold_bytes = INT64_MAX / 4;

  // Cumulative counters for telemetry.
  std::int64_t drops = 0;
  std::int64_t ecn_marks = 0;
  std::int64_t bytes_dequeued = 0;
};

// Two-tier Clos: hosts -> leaves -> spines, every leaf connected to every
// spine. Nodes are numbered hosts first, then leaves, then spines.
class Topology {
 public:
  static Topology clos2(std::int32_t hosts_per_leaf, std::int32_t leaves,
                        std::int32_t spines, std::int64_t host_gbps,
                        std::int64_t fabric_gbps, Ps prop_ps);

  std::int32_t num_hosts() const { return hosts_per_leaf_ * leaves_; }
  std::int32_t num_leaves() const { return leaves_; }
  std::int32_t num_spines() const { return spines_; }
  std::int32_t num_nodes() const {
    return num_hosts() + leaves_ + spines_;
  }

  NodeKind kind(NodeId n) const;
  NodeId leaf_of_host(NodeId host) const;
  NodeId leaf_node(std::int32_t leaf_idx) const { return num_hosts() + leaf_idx; }
  NodeId spine_node(std::int32_t spine_idx) const {
    return num_hosts() + leaves_ + spine_idx;
  }

  LinkId link_between(NodeId a, NodeId b) const;  // throws if absent
  Link& link(LinkId id) { return links_[static_cast<std::size_t>(id)]; }
  const Link& link(LinkId id) const {
    return links_[static_cast<std::size_t>(id)];
  }
  std::size_t num_links() const { return links_.size(); }

 private:
  LinkId add_link(NodeId src, NodeId dst, std::int64_t gbps, Ps prop_ps);

  std::int32_t hosts_per_leaf_ = 0;
  std::int32_t leaves_ = 0;
  std::int32_t spines_ = 0;
  std::vector<Link> links_;
  std::map<std::pair<NodeId, NodeId>, LinkId> link_index_;
};

}  // namespace gpufab
