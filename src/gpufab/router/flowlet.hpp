#pragma once

#include "gpufab/router/irouter.hpp"

namespace gpufab {

// CONGA-style reactive load balancer: at every flowlet boundary, pick the
// spine whose two-hop path (src-leaf uplink + downlink to dst-leaf) has the
// least standing congestion. Congestion metric = queued bytes on the worse
// of the two fabric hops; ties break to the lowest spine index for
// determinism. Purely reactive — no learning, no configuration beyond the
// flowlet gap — which is exactly what makes it the honest heuristic baseline
// the RL router must justify itself against.
class FlowletRouter : public IRouter {
 public:
  explicit FlowletRouter(Ps gap_ps) : gap_ps_(gap_ps) {}

  std::string name() const override { return "flowlet"; }
  std::vector<NodeId> route(const Flow& flow, const FabricView& view) override;
  Ps reroute_gap_ps() const override { return gap_ps_; }

 private:
  Ps gap_ps_;
};

}  // namespace gpufab
