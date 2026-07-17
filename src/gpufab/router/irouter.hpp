#pragma once

#include <string>
#include <vector>

#include "gpufab/flow.hpp"
#include "gpufab/topology.hpp"

namespace gpufab {

// The single interface the whole project pivots on. ECMP, flowlet/CONGA, and
// the RL policy all live behind it with zero engine changes.
//
// Contract: a router sees the topology and (from Phase 3) link telemetry.
// It never sees wall-clock time — only virtual quantities derived from the
// simulation. Violating that breaks reproducibility and invalidates every
// comparison built on top.
class IRouter {
 public:
  virtual ~IRouter() = default;

  virtual std::string name() const = 0;

  // Return the full node path src -> ... -> dst for a flow at flow start.
  // Every consecutive pair must be a link in `topo`.
  virtual std::vector<NodeId> route(const Flow& flow, const Topology& topo) = 0;
};

}  // namespace gpufab
