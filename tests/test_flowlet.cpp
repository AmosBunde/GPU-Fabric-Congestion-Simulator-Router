#include "gpufab/router/flowlet.hpp"

#include <cstdio>
#include <set>

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

constexpr std::uint64_t kSalt = 0x5eed;

// Two cross-leaf elephant flows from the same source leaf to different
// destination leaves. With `collide` ids, ECMP hashes both onto the same
// spine uplink; the flowlet router starts there too but moves one flow to an
// empty spine at the first congested flowlet boundary.
Ps run_pair(IRouter& router, std::int64_t id_a, std::int64_t id_b) {
  Topology t = Topology::clos2(4, 4, 4, 100, 100, kPsPerUs);
  for (std::size_t i = 0; i < t.num_links(); ++i) {
    t.link(static_cast<LinkId>(i)).buffer_bytes = 1 << 20;
    t.link(static_cast<LinkId>(i)).ecn_threshold_bytes = 256 * 1024;
  }
  Engine engine(t, router, 65536, TransportParams{});
  Flow a, b;
  a.id = id_a;
  a.src = 0;  // leaf 0
  a.dst = 4;  // leaf 1
  a.bytes = 4 << 20;
  b.id = id_b;
  b.src = 1;  // leaf 0 — shares the source leaf's uplinks with a
  b.dst = 8;  // leaf 2
  b.bytes = 4 << 20;
  engine.add_flow(a);
  engine.add_flow(b);
  engine.run();
  Ps makespan = 0;
  for (const Flow& f : engine.flows()) {
    if (f.end_ps < 0) return -1;
    makespan = std::max(makespan, f.end_ps);
  }
  return makespan;
}

}  // namespace

int main() {
  using namespace gpufab;

  Topology t = Topology::clos2(4, 4, 4, 100, 100, kPsPerUs);
  const std::vector<double> no_util(t.num_links(), 0.0);
  const FabricView view(t, no_util);

  // Find two flow ids that ECMP hashes onto the same spine.
  EcmpRouter probe(kSalt);
  Flow fa, fb;
  fa.src = 0;
  fa.dst = 4;
  fb.src = 1;
  fb.dst = 8;
  fa.id = 0;
  const NodeId spine_a = probe.route(fa, view)[2];
  std::int64_t collide_id = -1;
  for (std::int64_t id = 1; id < 64; ++id) {
    fb.id = id;
    if (probe.route(fb, view)[2] == spine_a) {
      collide_id = id;
      break;
    }
  }
  CHECK(collide_id > 0);

  // Flowlet routing spreads what ECMP collides: strictly better makespan.
  // Same salt as ECMP so the initial (all-queues-zero) choice collides
  // identically — the improvement must come from congestion-aware rerouting.
  constexpr std::int64_t kHyst = 512 * 1024;
  EcmpRouter ecmp(kSalt);
  FlowletRouter flowlet(0, kSalt, kHyst);  // re-evaluate every chunk boundary
  const Ps ecmp_makespan = run_pair(ecmp, 0, collide_id);
  const Ps flowlet_makespan = run_pair(flowlet, 0, collide_id);
  std::printf("makespan: ecmp=%lld flowlet=%lld\n",
              static_cast<long long>(ecmp_makespan),
              static_cast<long long>(flowlet_makespan));
  CHECK(ecmp_makespan > 0 && flowlet_makespan > 0);
  CHECK(flowlet_makespan * 10 < ecmp_makespan * 9);  // >10% faster

  // The flowlet router actually used more than one spine for the pair.
  {
    Topology t2 = Topology::clos2(4, 4, 4, 100, 100, kPsPerUs);
    FlowletRouter fr(0, kSalt, kHyst);
    Engine engine(t2, fr, 65536, TransportParams{});
    Flow a = fa, b = fb;
    a.id = 0;
    b.id = collide_id;
    a.bytes = b.bytes = 4 << 20;
    engine.add_flow(a);
    engine.add_flow(b);
    engine.run();
    std::set<NodeId> spines_used;
    for (const Flow& f : engine.flows()) spines_used.insert(f.path[2]);
    CHECK(spines_used.size() == 2);
  }

  // Determinism: identical makespan on a rerun.
  FlowletRouter flowlet2(0, kSalt, kHyst);
  CHECK(run_pair(flowlet2, 0, collide_id) == flowlet_makespan);
  return 0;
}
