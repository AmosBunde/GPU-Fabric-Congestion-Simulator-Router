#include <cstdio>
#include <vector>

#include "gpufab/engine.hpp"
#include "gpufab/router/ecmp.hpp"

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                        \
    }                                                                  \
  } while (0)

namespace {

using namespace gpufab;

void set_links(Topology& t, std::int64_t buffer_b, std::int64_t ecn_b) {
  for (std::size_t i = 0; i < t.num_links(); ++i) {
    t.link(static_cast<LinkId>(i)).buffer_bytes = buffer_b;
    t.link(static_cast<LinkId>(i)).ecn_threshold_bytes = ecn_b;
  }
}

// N same-leaf senders -> one receiver: the leaf->receiver link is the shared
// bottleneck. Returns flow end times.
std::vector<Ps> run_incast(int senders, std::int64_t buffer_b,
                           std::int64_t ecn_b, EngineStats* stats_out) {
  Topology t = Topology::clos2(8, 2, 2, 100, 100, kPsPerUs);
  set_links(t, buffer_b, ecn_b);
  EcmpRouter router(0x5eed);
  TransportParams tp;  // window 8, rto 500us
  Engine engine(t, router, 65536, tp);
  for (int s = 0; s < senders; ++s) {
    Flow f;
    f.id = s;
    f.src = static_cast<NodeId>(1 + s);  // hosts 1..N on leaf 0
    f.dst = 0;                           // receiver, same leaf
    f.bytes = 512 * 1024;
    f.start_ps = 0;
    engine.add_flow(f);
  }
  EngineStats stats = engine.run();
  if (stats_out) *stats_out = stats;
  std::vector<Ps> ends;
  for (const Flow& f : engine.flows()) {
    if (f.end_ps < 0) return {};  // incomplete flow: hard failure
    ends.push_back(f.end_ps);
  }
  return ends;
}

}  // namespace

int main() {
  using namespace gpufab;

  // 1. Contention without loss: big buffer, low ECN threshold. Queues build,
  //    ECN marks appear, nothing drops, and sharing stretches completion.
  EngineStats solo_stats, shared_stats;
  const auto solo = run_incast(1, 64 << 20, 32 * 1024, &solo_stats);
  const auto shared = run_incast(4, 64 << 20, 32 * 1024, &shared_stats);
  CHECK(!solo.empty() && !shared.empty());
  CHECK(shared_stats.drops == 0);
  CHECK(shared_stats.ecn_marks > 0);
  Ps solo_end = solo[0], shared_max = 0;
  for (Ps e : shared) shared_max = std::max(shared_max, e);
  CHECK(shared_max > 2 * solo_end);  // 4 senders share one bottleneck

  // 2. Loss and recovery: tiny buffer (2 chunks), no ECN. Drops and RTO
  //    retransmissions happen, yet every flow still completes.
  EngineStats loss_stats;
  const auto lossy = run_incast(6, 2 * 65536, INT64_MAX / 4, &loss_stats);
  CHECK(!lossy.empty());
  CHECK(loss_stats.drops > 0);
  std::printf("lossy: drops=%lld\n", static_cast<long long>(loss_stats.drops));

  // 3. Determinism under congestion: byte-identical end times run-to-run.
  EngineStats again_stats;
  const auto lossy2 = run_incast(6, 2 * 65536, INT64_MAX / 4, &again_stats);
  CHECK(lossy2 == lossy);
  CHECK(again_stats.drops == loss_stats.drops);
  return 0;
}
