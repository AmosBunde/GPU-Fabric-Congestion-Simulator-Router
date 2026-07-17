#pragma once

#include <cstdint>

#include "gpufab/router/irouter.hpp"

namespace gpufab {

// CONGA-style reactive load balancer with stickiness and hysteresis.
//
// Naive least-queue routing herds: queue telemetry lags send decisions by
// the host->leaf latency, so every sender simultaneously picks the same
// "least loaded" spine, then simultaneously flees it — bang-bang oscillation
// that measures far WORSE than static ECMP (observed 8x worse p50 on
// permutation traffic before this hysteresis existed). Two standard fixes,
// both deterministic:
//
//  - Stickiness: a flow keeps its current spine unless an alternative's
//    congestion is better by more than hysteresis_bytes. The margin also
//    absorbs the flow's OWN standing backlog (default: one window's worth),
//    so an elephant doesn't chase its own queue around the fabric.
//  - Hashed spread among near-equal choices: when several spines are within
//    the hysteresis band (e.g. the cold-start all-zeros state), pick by a
//    salted hash of the flow id, like ECMP — herds can't form on ties.
//
// Congestion metric per spine: worse standing queue of the two fabric hops
// (src-leaf uplink, downlink to dst-leaf).
class FlowletRouter : public IRouter {
 public:
  FlowletRouter(Ps gap_ps, std::uint64_t salt, std::int64_t hysteresis_bytes)
      : gap_ps_(gap_ps), salt_(salt), hysteresis_bytes_(hysteresis_bytes) {}

  std::string name() const override { return "flowlet"; }
  std::vector<NodeId> route(const Flow& flow, const FabricView& view) override;
  Ps reroute_gap_ps() const override { return gap_ps_; }

 private:
  Ps gap_ps_;
  std::uint64_t salt_;
  std::int64_t hysteresis_bytes_;
};

}  // namespace gpufab
