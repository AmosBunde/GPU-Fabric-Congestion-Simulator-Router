#pragma once

#include <string>
#include <vector>

#include "gpufab/flow.hpp"
#include "gpufab/time.hpp"
#include "gpufab/topology.hpp"

namespace gpufab {

// Read-only congestion telemetry a router is allowed to see: live per-link
// standing queues and counters, plus an EWMA of sampled utilization. This is
// the ONLY window routers get onto the data plane — never engine internals,
// never wall-clock time.
class FabricView {
 public:
  FabricView(const Topology& topo, const std::vector<double>& util_ewma)
      : topo_(topo), util_ewma_(util_ewma) {}

  const Topology& topo() const { return topo_; }
  std::int64_t queue_bytes(LinkId l) const {
    return topo_.link(l).queue_bytes;
  }
  std::int64_t drops(LinkId l) const { return topo_.link(l).drops; }
  std::int64_t ecn_marks(LinkId l) const { return topo_.link(l).ecn_marks; }
  // 0.0 when telemetry sampling is disabled.
  double util_ewma(LinkId l) const {
    return util_ewma_[static_cast<std::size_t>(l)];
  }

 private:
  const Topology& topo_;
  const std::vector<double>& util_ewma_;
};

// The single interface the whole project pivots on. ECMP, flowlet/CONGA, and
// the RL policy all live behind it with zero engine changes.
//
// Contract: a router sees the FabricView only. It never sees wall-clock
// time, never mutates fabric state, and draws randomness only from named,
// logged registry streams. Violating any of these breaks reproducibility and
// invalidates every comparison built on top.
class IRouter {
 public:
  virtual ~IRouter() = default;

  virtual std::string name() const = 0;

  // Return the full node path src -> ... -> dst. Called at flow start and —
  // when reroute_gap_ps() >= 0 — again at flowlet boundaries.
  virtual std::vector<NodeId> route(const Flow& flow,
                                    const FabricView& view) = 0;

  // Minimum inter-send gap (virtual ps) before the engine re-consults
  // route() for a flow's next chunk. -1 = never re-route (path pinned at
  // flow start). 0 = re-evaluate on every chunk: safe in this model because
  // the transport is selective-repeat and tolerates reordering — the
  // real-world flowlet-gap constraint exists to protect cumulative-ack TCP.
  virtual Ps reroute_gap_ps() const { return -1; }

  // Experienced queueing delay for a delivered chunk (arrival minus send
  // minus the path's uncongested base delay), reported by the engine.
  // Default no-op; the RL router learns from this.
  virtual void on_chunk_feedback(const Flow& flow, Ps queue_delay_ps) {
    (void)flow;
    (void)queue_delay_ps;
  }
};

}  // namespace gpufab
