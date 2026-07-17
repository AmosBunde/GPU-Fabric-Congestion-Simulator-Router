#pragma once

#include <cstdint>
#include <queue>
#include <vector>

#include "gpufab/flow.hpp"
#include "gpufab/router/irouter.hpp"
#include "gpufab/time.hpp"
#include "gpufab/topology.hpp"

namespace gpufab {

// Event granularity — the packet-train / chunk model (locked Decision 1).
// The engine schedules one event per chunk-hop, not per packet: fine enough
// to see queue buildup once the Phase 2 data plane lands, coarse enough to
// run ~1024 nodes replaying all-reduce in seconds.
enum class EventType : std::uint8_t {
  FlowStart,     // ask router for a path, inject the flow's chunks at src
  ChunkArrival,  // a chunk reached path[hop]; forward or complete the flow
  // Phase 2: CongestionUpdate — periodic per-link telemetry refresh
};

struct Event {
  Ps time_ps = 0;
  std::uint64_t seq = 0;  // insertion order; deterministic tie-break
  EventType type = EventType::FlowStart;
  std::int32_t flow_idx = -1;
  std::int32_t chunk_idx = -1;
  std::int32_t hop_idx = -1;
};

struct EngineStats {
  std::uint64_t events_processed = 0;
  Ps end_time_ps = 0;
};

// Single-threaded deterministic discrete-event engine. Virtual clock only —
// no wall-clock, no sockets, no threads anywhere in the model path.
class Engine {
 public:
  Engine(Topology& topo, IRouter& router, std::int64_t chunk_bytes);

  void add_flow(Flow flow);
  EngineStats run();

  const std::vector<Flow>& flows() const { return flows_; }

 private:
  struct EventOrder {
    bool operator()(const Event& a, const Event& b) const {
      if (a.time_ps != b.time_ps) return a.time_ps > b.time_ps;
      return a.seq > b.seq;
    }
  };

  void schedule(Ps time_ps, EventType type, std::int32_t flow_idx,
                std::int32_t chunk_idx, std::int32_t hop_idx);
  void on_flow_start(const Event& ev);
  void on_chunk_arrival(const Event& ev);
  std::int32_t num_chunks(const Flow& f) const;
  std::int64_t chunk_size(const Flow& f, std::int32_t chunk_idx) const;

  Topology& topo_;
  IRouter& router_;
  std::int64_t chunk_bytes_;
  std::vector<Flow> flows_;
  std::vector<std::int32_t> chunks_remaining_;
  std::priority_queue<Event, std::vector<Event>, EventOrder> queue_;
  std::uint64_t next_seq_ = 0;
  EngineStats stats_;
};

}  // namespace gpufab
