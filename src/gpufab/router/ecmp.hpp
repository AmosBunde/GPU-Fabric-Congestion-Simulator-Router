#pragma once

#include "gpufab/router/irouter.hpp"

#include <cstdint>

namespace gpufab {

// Static ECMP: the control every adaptive scheme must beat. Spine choice is a
// deterministic hash of the flow id and a per-run salt — no congestion
// awareness by design.
class EcmpRouter : public IRouter {
 public:
  explicit EcmpRouter(std::uint64_t salt) : salt_(salt) {}

  std::string name() const override { return "ecmp"; }
  std::vector<NodeId> route(const Flow& flow, const Topology& topo) override;

 private:
  std::uint64_t salt_;
};

}  // namespace gpufab
