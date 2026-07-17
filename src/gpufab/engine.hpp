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
// to see queue buildup and drops, coarse enough to run ~1024 nodes replaying
// all-reduce in seconds.
enum class EventType : std::uint8_t {
  FlowStart,        // ask router for a path, open the transport window at src
  ChunkArrival,     // a chunk reached path[hop]; forward, or deliver at dst
  AckArrival,       // dst's ack (with ECN echo) reached the source
  TimeoutCheck,     // RTO fired for a chunk; retransmit if still unacked
  LinkDequeue,      // a chunk finished serializing; free its queue bytes
  TelemetrySample,  // periodic per-link congestion snapshot
};

struct Event {
  Ps time_ps = 0;
  std::uint64_t seq = 0;  // insertion order; deterministic tie-break
  EventType type = EventType::FlowStart;
  std::int32_t flow_idx = -1;
  std::int32_t chunk_idx = -1;
  std::int32_t hop_idx = -1;
  std::int32_t epoch = 0;    // TimeoutCheck: send attempt this timer guards
  LinkId link_id = -1;       // LinkDequeue
  std::int64_t bytes = 0;    // LinkDequeue
  bool ecn = false;          // ChunkArrival/AckArrival: congestion-experienced
};

// Minimal windowed transport, per flow: chunks are sent up to a congestion
// window; acks (delayed by the reverse-path propagation, consuming no
// bandwidth) advance the window additively; ECN echoes halve it at most once
// per RTT; a lost chunk is recovered by RTO with exponential backoff, which
// also collapses the window to one chunk. Deliberately TCP-shaped without
// being TCP: just enough for drops to have consequences and congestion to
// self-regulate.
struct TransportParams {
  double window_chunks = 8.0;
  Ps rto_ps = 500 * kPsPerUs;
  std::int32_t max_backoff_doublings = 6;
};

struct EngineStats {
  std::uint64_t events_processed = 0;
  Ps last_flow_end_ps = 0;
  std::int64_t drops = 0;      // summed over links at end of run
  std::int64_t ecn_marks = 0;  // summed over links at end of run
};

// One row of the per-link congestion time series, sampled every
// telemetry interval while any flow is incomplete.
struct LinkSample {
  Ps time_ps = 0;
  LinkId link = -1;
  std::int64_t queue_bytes = 0;
  double utilization = 0.0;  // fraction of line rate over the last interval
  std::int64_t drops_cum = 0;
  std::int64_t ecn_marks_cum = 0;
};

class Engine;

// A workload injects flows: the initial set in start(), and — for multi-step
// collectives like ring all-reduce — follow-on flows from on_flow_complete.
// The engine invokes on_flow_complete between events (never mid-handler), so
// implementations may call Engine::add_flow freely.
class IWorkload {
 public:
  virtual ~IWorkload() = default;
  virtual std::string name() const = 0;
  virtual void start(Engine& engine) = 0;
  virtual void on_flow_complete(Engine& engine, const Flow& flow) {
    (void)engine;
    (void)flow;
  }
};

// Single-threaded deterministic discrete-event engine. Virtual clock only —
// no wall-clock, no sockets, no threads anywhere in the model path.
class Engine {
 public:
  Engine(Topology& topo, IRouter& router, std::int64_t chunk_bytes,
         TransportParams transport);

  void add_flow(Flow flow);
  void set_workload(IWorkload* workload) { workload_ = workload; }
  // 0 disables sampling. Sampling stops once every flow has completed.
  void set_telemetry_interval(Ps interval_ps) { sample_interval_ = interval_ps; }
  EngineStats run();

  const std::vector<Flow>& flows() const { return flows_; }
  const std::vector<LinkSample>& link_samples() const { return samples_; }

 private:
  struct EventOrder {
    bool operator()(const Event& a, const Event& b) const {
      if (a.time_ps != b.time_ps) return a.time_ps > b.time_ps;
      return a.seq > b.seq;
    }
  };

  // Per-flow transport state, index-parallel with flows_.
  struct FlowState {
    double window = 1.0;
    std::int32_t inflight = 0;     // first-sent, not-yet-acked chunks
    std::int32_t next_unsent = 0;
    std::int32_t received = 0;     // distinct chunks delivered at dst
    std::vector<std::uint8_t> chunk_acked;
    std::vector<std::uint8_t> chunk_received;
    std::vector<std::int32_t> attempts;  // send attempts per chunk
    Ps ack_delay_ps = 0;   // reverse-path propagation
    Ps rtt_est_ps = 0;     // base RTT: 2*prop + per-hop serialization
    Ps last_md_ps = -1;    // last multiplicative decrease (once per RTT)
    std::int32_t snd_una = 0;   // oldest unacked chunk (first-send order)
    std::int32_t dup_acks = 0;  // acks for later chunks while snd_una waits
    Ps last_send_ps = -1;       // for flowlet-boundary reroute decisions
    Ps base_delay_ps = 0;       // uncongested one-way delay of a full chunk
    std::vector<Ps> sent_ps;    // latest send time per chunk (for feedback)
  };

  void schedule(Event ev);
  void on_telemetry_sample(const Event& ev);
  void on_flow_start(const Event& ev);
  void on_chunk_arrival(const Event& ev);
  void on_ack_arrival(const Event& ev);
  void on_timeout(const Event& ev);
  void try_send(std::int32_t flow_idx, Ps now);
  void send_chunk(std::int32_t flow_idx, std::int32_t chunk_idx, Ps now);
  Ps rto_for_attempt(std::int32_t attempt) const;
  std::int32_t num_chunks(const Flow& f) const;
  std::int64_t chunk_size(const Flow& f, std::int32_t chunk_idx) const;

  Topology& topo_;
  IRouter& router_;
  std::int64_t chunk_bytes_;
  TransportParams transport_;
  FabricView view() const { return FabricView(topo_, util_ewma_); }

  IWorkload* workload_ = nullptr;
  std::vector<std::int32_t> completed_pending_;
  std::int32_t completed_count_ = 0;
  Ps sample_interval_ = 0;
  std::vector<double> util_ewma_;
  std::vector<LinkSample> samples_;
  std::vector<std::int64_t> bytes_at_last_sample_;
  std::vector<Flow> flows_;
  std::vector<FlowState> state_;
  std::priority_queue<Event, std::vector<Event>, EventOrder> queue_;
  std::uint64_t next_seq_ = 0;
  EngineStats stats_;
};

}  // namespace gpufab
