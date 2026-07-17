#include "gpufab/engine.hpp"

#include <stdexcept>

namespace gpufab {

Engine::Engine(Topology& topo, IRouter& router, std::int64_t chunk_bytes)
    : topo_(topo), router_(router), chunk_bytes_(chunk_bytes) {
  if (chunk_bytes <= 0) throw std::invalid_argument("chunk_bytes must be > 0");
}

void Engine::add_flow(Flow flow) {
  const auto idx = static_cast<std::int32_t>(flows_.size());
  flows_.push_back(std::move(flow));
  chunks_remaining_.push_back(num_chunks(flows_.back()));
  schedule(flows_.back().start_ps, EventType::FlowStart, idx, -1, -1);
}

EngineStats Engine::run() {
  while (!queue_.empty()) {
    const Event ev = queue_.top();
    queue_.pop();
    ++stats_.events_processed;
    stats_.end_time_ps = ev.time_ps;
    switch (ev.type) {
      case EventType::FlowStart:
        on_flow_start(ev);
        break;
      case EventType::ChunkArrival:
        on_chunk_arrival(ev);
        break;
    }
  }
  return stats_;
}

void Engine::schedule(Ps time_ps, EventType type, std::int32_t flow_idx,
                      std::int32_t chunk_idx, std::int32_t hop_idx) {
  queue_.push(Event{time_ps, next_seq_++, type, flow_idx, chunk_idx, hop_idx});
}

void Engine::on_flow_start(const Event& ev) {
  Flow& f = flows_[static_cast<std::size_t>(ev.flow_idx)];
  f.path = router_.route(f, topo_);
  if (f.path.size() < 2) {
    throw std::runtime_error("router returned degenerate path for flow " +
                             std::to_string(f.id));
  }
  const std::int32_t n = num_chunks(f);
  for (std::int32_t c = 0; c < n; ++c) {
    schedule(ev.time_ps, EventType::ChunkArrival, ev.flow_idx, c, 0);
  }
}

void Engine::on_chunk_arrival(const Event& ev) {
  Flow& f = flows_[static_cast<std::size_t>(ev.flow_idx)];
  const auto hop = static_cast<std::size_t>(ev.hop_idx);
  if (hop + 1 == f.path.size()) {  // chunk reached the destination host
    if (--chunks_remaining_[static_cast<std::size_t>(ev.flow_idx)] == 0) {
      f.end_ps = ev.time_ps;
    }
    return;
  }
  // Store-and-forward over the next link. busy_until serializes chunks in
  // event order — this is where queueing delay first becomes visible.
  Link& link = topo_.link(topo_.link_between(f.path[hop], f.path[hop + 1]));
  const Ps start_tx = std::max(ev.time_ps, link.busy_until_ps);
  const Ps done_tx = start_tx + serialization_ps(chunk_size(f, ev.chunk_idx),
                                                 link.gbps);
  link.busy_until_ps = done_tx;
  schedule(done_tx + link.prop_ps, EventType::ChunkArrival, ev.flow_idx,
           ev.chunk_idx, ev.hop_idx + 1);
}

std::int32_t Engine::num_chunks(const Flow& f) const {
  return static_cast<std::int32_t>((f.bytes + chunk_bytes_ - 1) / chunk_bytes_);
}

std::int64_t Engine::chunk_size(const Flow& f, std::int32_t chunk_idx) const {
  const std::int64_t full = chunk_bytes_;
  const std::int64_t last = f.bytes - full * (num_chunks(f) - 1);
  return chunk_idx == num_chunks(f) - 1 ? last : full;
}

}  // namespace gpufab
