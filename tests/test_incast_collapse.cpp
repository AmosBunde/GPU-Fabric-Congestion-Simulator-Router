#include <cstdio>

#include "gpufab/engine.hpp"
#include "gpufab/router/ecmp.hpp"
#include "gpufab/workloads.hpp"

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                        \
    }                                                                  \
  } while (0)

namespace {

using namespace gpufab;

// Goodput of an N-sender incast on the validation topology, as a fraction of
// the 100 Gbps bottleneck (receiver's leaf->host link). Mirrors
// python/validate_incast.py: 256 KiB buffer, no ECN, window 4, 16 KiB chunks,
// RTO 500 us.
double incast_goodput(int senders, std::vector<LinkSample>* samples_out) {
  Topology t = Topology::clos2(16, 4, 4, 100, 100, kPsPerUs);
  for (std::size_t i = 0; i < t.num_links(); ++i) {
    t.link(static_cast<LinkId>(i)).buffer_bytes = 256 * 1024;
  }
  EcmpRouter router(0x5eed);
  TransportParams tp;
  tp.window_chunks = 4;
  tp.rto_ps = 500 * kPsPerUs;
  Engine engine(t, router, 16384, tp);
  engine.set_telemetry_interval(10 * kPsPerUs);
  const std::int64_t bytes = 256 * 1024;
  IncastWorkload w(t, senders, 0, bytes);
  engine.set_workload(&w);
  w.start(engine);
  engine.run();
  Ps makespan = 0;
  for (const Flow& f : engine.flows()) {
    if (f.end_ps < 0) return -1.0;  // incomplete: hard failure
    makespan = std::max(makespan, f.end_ps);
  }
  if (samples_out) *samples_out = engine.link_samples();
  const double gbps = static_cast<double>(senders) *
                      static_cast<double>(bytes) * 8.0 * 1000.0 /
                      static_cast<double>(makespan);
  return gbps / 100.0;
}

}  // namespace

int main() {
  using namespace gpufab;

  std::vector<LinkSample> samples;
  const double few = incast_goodput(4, nullptr);
  const double many = incast_goodput(32, &samples);
  std::printf("goodput: 4 senders=%.3f  32 senders=%.3f\n", few, many);
  CHECK(few > 0 && many > 0);

  // The famous curve, coarsely: goodput collapses as synchronized senders
  // overflow the shallow buffer and RTO stalls dominate.
  CHECK(many < 0.5 * few);

  // Telemetry sanity: samples exist, utilization is a sane fraction (up to
  // one chunk of completion-quantization overshoot per interval), and the
  // bottleneck actually saturated at some point.
  CHECK(!samples.empty());
  double max_util = 0.0;
  for (const LinkSample& s : samples) {
    CHECK(s.utilization >= 0.0 && s.utilization <= 1.15);
    CHECK(s.queue_bytes >= 0);
    max_util = std::max(max_util, s.utilization);
  }
  CHECK(max_util > 0.9);
  return 0;
}
