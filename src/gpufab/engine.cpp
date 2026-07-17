#include "gpufab/engine.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gpufab {

Engine::Engine(Topology& topo, IRouter& router, std::int64_t chunk_bytes,
               TransportParams transport)
    : topo_(topo),
      router_(router),
      chunk_bytes_(chunk_bytes),
      transport_(transport) {
  if (chunk_bytes <= 0) throw std::invalid_argument("chunk_bytes must be > 0");
  if (transport_.window_chunks < 1.0 || transport_.rto_ps <= 0) {
    throw std::invalid_argument("transport: window >= 1 and rto > 0 required");
  }
}

void Engine::add_flow(Flow flow) {
  const auto idx = static_cast<std::int32_t>(flows_.size());
  flows_.push_back(std::move(flow));
  state_.emplace_back();
  Event ev;
  ev.time_ps = flows_.back().start_ps;
  ev.type = EventType::FlowStart;
  ev.flow_idx = idx;
  schedule(ev);
}

EngineStats Engine::run() {
  while (!queue_.empty()) {
    const Event ev = queue_.top();
    queue_.pop();
    ++stats_.events_processed;
    switch (ev.type) {
      case EventType::FlowStart:
        on_flow_start(ev);
        break;
      case EventType::ChunkArrival:
        on_chunk_arrival(ev);
        break;
      case EventType::AckArrival:
        on_ack_arrival(ev);
        break;
      case EventType::TimeoutCheck:
        on_timeout(ev);
        break;
      case EventType::LinkDequeue:
        topo_.link(ev.link_id).queue_bytes -= ev.bytes;
        topo_.link(ev.link_id).bytes_dequeued += ev.bytes;
        break;
    }
    // Deliver completions between events: handlers hold references into
    // flows_/state_, and a workload's add_flow may reallocate both.
    while (!completed_pending_.empty()) {
      const std::int32_t idx = completed_pending_.front();
      completed_pending_.erase(completed_pending_.begin());
      if (workload_ != nullptr) {
        const Flow completed = flows_[static_cast<std::size_t>(idx)];
        workload_->on_flow_complete(*this, completed);
      }
    }
  }
  for (std::size_t i = 0; i < topo_.num_links(); ++i) {
    stats_.drops += topo_.link(static_cast<LinkId>(i)).drops;
    stats_.ecn_marks += topo_.link(static_cast<LinkId>(i)).ecn_marks;
  }
  return stats_;
}

void Engine::schedule(Event ev) {
  ev.seq = next_seq_++;
  queue_.push(ev);
}

void Engine::on_flow_start(const Event& ev) {
  Flow& f = flows_[static_cast<std::size_t>(ev.flow_idx)];
  FlowState& st = state_[static_cast<std::size_t>(ev.flow_idx)];
  f.path = router_.route(f, topo_);
  if (f.path.size() < 2) {
    throw std::runtime_error("router returned degenerate path for flow " +
                             std::to_string(f.id));
  }
  const std::int32_t n = num_chunks(f);
  st.chunk_acked.assign(static_cast<std::size_t>(n), 0);
  st.chunk_received.assign(static_cast<std::size_t>(n), 0);
  st.attempts.assign(static_cast<std::size_t>(n), 0);
  st.window = transport_.window_chunks;
  Ps prop_total = 0;
  Ps ser_total = 0;
  for (std::size_t i = 0; i + 1 < f.path.size(); ++i) {
    const Link& l = topo_.link(topo_.link_between(f.path[i], f.path[i + 1]));
    prop_total += l.prop_ps;
    ser_total += serialization_ps(chunk_bytes_, l.gbps);
  }
  st.ack_delay_ps = prop_total;
  st.rtt_est_ps = 2 * prop_total + ser_total;
  try_send(ev.flow_idx, ev.time_ps);
}

void Engine::on_chunk_arrival(const Event& ev) {
  Flow& f = flows_[static_cast<std::size_t>(ev.flow_idx)];
  FlowState& st = state_[static_cast<std::size_t>(ev.flow_idx)];
  const auto hop = static_cast<std::size_t>(ev.hop_idx);

  if (hop + 1 == f.path.size()) {  // delivered at the destination host
    const auto c = static_cast<std::size_t>(ev.chunk_idx);
    if (!st.chunk_received[c]) {
      st.chunk_received[c] = 1;
      if (++st.received == num_chunks(f)) {
        f.end_ps = ev.time_ps;
        stats_.last_flow_end_ps = std::max(stats_.last_flow_end_ps, f.end_ps);
        completed_pending_.push_back(ev.flow_idx);
      }
    }
    Event ack;
    ack.time_ps = ev.time_ps + st.ack_delay_ps;
    ack.type = EventType::AckArrival;
    ack.flow_idx = ev.flow_idx;
    ack.chunk_idx = ev.chunk_idx;
    ack.ecn = ev.ecn;
    schedule(ack);
    return;
  }

  // Forward over the next link: tail-drop against the buffer, ECN-mark when
  // the standing queue is at or above threshold, then serialize FIFO.
  Link& link = topo_.link(topo_.link_between(f.path[hop], f.path[hop + 1]));
  const std::int64_t size = chunk_size(f, ev.chunk_idx);
  if (link.queue_bytes + size > link.buffer_bytes) {
    ++link.drops;  // recovery is the sender's RTO problem
    return;
  }
  const bool mark = link.queue_bytes >= link.ecn_threshold_bytes;
  if (mark) ++link.ecn_marks;
  link.queue_bytes += size;

  const Ps start_tx = std::max(ev.time_ps, link.busy_until_ps);
  const Ps done_tx = start_tx + serialization_ps(size, link.gbps);
  link.busy_until_ps = done_tx;

  Event deq;
  deq.time_ps = done_tx;
  deq.type = EventType::LinkDequeue;
  deq.link_id = topo_.link_between(f.path[hop], f.path[hop + 1]);
  deq.bytes = size;
  schedule(deq);

  Event next;
  next.time_ps = done_tx + link.prop_ps;
  next.type = EventType::ChunkArrival;
  next.flow_idx = ev.flow_idx;
  next.chunk_idx = ev.chunk_idx;
  next.hop_idx = ev.hop_idx + 1;
  next.ecn = ev.ecn || mark;
  schedule(next);
}

void Engine::on_ack_arrival(const Event& ev) {
  FlowState& st = state_[static_cast<std::size_t>(ev.flow_idx)];
  const auto c = static_cast<std::size_t>(ev.chunk_idx);
  if (st.chunk_acked[c]) return;  // duplicate (retransmit raced the ack)
  st.chunk_acked[c] = 1;
  --st.inflight;
  if (ev.ecn) {
    if (st.last_md_ps < 0 || ev.time_ps - st.last_md_ps >= st.rtt_est_ps) {
      st.window = std::max(1.0, st.window / 2.0);
      st.last_md_ps = ev.time_ps;
    }
  } else {
    st.window += 1.0 / st.window;  // additive increase, per acked chunk
  }
  try_send(ev.flow_idx, ev.time_ps);
}

void Engine::on_timeout(const Event& ev) {
  Flow& f = flows_[static_cast<std::size_t>(ev.flow_idx)];
  FlowState& st = state_[static_cast<std::size_t>(ev.flow_idx)];
  const auto c = static_cast<std::size_t>(ev.chunk_idx);
  if (st.chunk_acked[c]) return;          // arrived after all
  if (st.attempts[c] != ev.epoch) return;  // stale timer; a resend is live
  ++f.timeouts;
  ++f.retransmits;
  st.window = 1.0;  // RTO collapse
  send_chunk(ev.flow_idx, ev.chunk_idx, ev.time_ps);
}

void Engine::try_send(std::int32_t flow_idx, Ps now) {
  Flow& f = flows_[static_cast<std::size_t>(flow_idx)];
  FlowState& st = state_[static_cast<std::size_t>(flow_idx)];
  const std::int32_t n = num_chunks(f);
  while (st.next_unsent < n &&
         st.inflight < static_cast<std::int32_t>(st.window)) {
    send_chunk(flow_idx, st.next_unsent, now);
    ++st.next_unsent;
  }
}

void Engine::send_chunk(std::int32_t flow_idx, std::int32_t chunk_idx, Ps now) {
  FlowState& st = state_[static_cast<std::size_t>(flow_idx)];
  const auto c = static_cast<std::size_t>(chunk_idx);
  if (st.attempts[c] == 0) ++st.inflight;  // retransmits are already inflight
  ++st.attempts[c];

  Event arrive;
  arrive.time_ps = now;
  arrive.type = EventType::ChunkArrival;
  arrive.flow_idx = flow_idx;
  arrive.chunk_idx = chunk_idx;
  arrive.hop_idx = 0;
  schedule(arrive);

  Event timeout;
  timeout.time_ps = now + rto_for_attempt(st.attempts[c]);
  timeout.type = EventType::TimeoutCheck;
  timeout.flow_idx = flow_idx;
  timeout.chunk_idx = chunk_idx;
  timeout.epoch = st.attempts[c];
  schedule(timeout);
}

Ps Engine::rto_for_attempt(std::int32_t attempt) const {
  const std::int32_t doublings =
      std::min(attempt - 1, transport_.max_backoff_doublings);
  return transport_.rto_ps << doublings;
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
