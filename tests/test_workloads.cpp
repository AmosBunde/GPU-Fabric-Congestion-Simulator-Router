#include "gpufab/workloads.hpp"

#include <cstdio>
#include <set>

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

struct Rig {
  Topology topo = Topology::clos2(4, 4, 4, 100, 100, kPsPerUs);
  EcmpRouter router{0x5eed};
  Engine engine{topo, router, 65536, TransportParams{}};
};

}  // namespace

int main() {
  using namespace gpufab;

  // Incast: N flows, all to the receiver, all complete.
  {
    Rig r;
    IncastWorkload w(r.topo, 8, 0, 128 * 1024);
    r.engine.set_workload(&w);
    w.start(r.engine);
    r.engine.run();
    CHECK(r.engine.flows().size() == 8);
    for (const Flow& f : r.engine.flows()) {
      CHECK(f.dst == 0 && f.src != 0);
      CHECK(f.end_ps > 0);
    }
  }

  // Ring all-reduce over K=8 hosts: 2*(K-1) bulk-synchronous steps of K
  // flows each, injected via the completion hook; steps do not overlap.
  {
    Rig r;
    RingAllReduceWorkload w(8, 8 * 65536);
    r.engine.set_workload(&w);
    w.start(r.engine);
    r.engine.run();
    const auto& flows = r.engine.flows();
    CHECK(static_cast<std::int32_t>(flows.size()) == 2 * (8 - 1) * 8);
    for (const Flow& f : flows) {
      CHECK(f.bytes == 65536);
      CHECK(f.dst == (f.src + 1) % 8);
      CHECK(f.end_ps > 0);
    }
    // Barrier property: every step's flows start when the previous step's
    // slowest flow completed.
    for (std::size_t step = 1; step < 14; ++step) {
      Ps prev_max_end = 0;
      for (std::size_t i = (step - 1) * 8; i < step * 8; ++i) {
        prev_max_end = std::max(prev_max_end, flows[i].end_ps);
      }
      for (std::size_t i = step * 8; i < (step + 1) * 8; ++i) {
        CHECK(flows[i].start_ps == prev_max_end);
      }
    }
  }

  // Permutation: a derangement covering every host, deterministic per seed.
  {
    Rig r;
    std::mt19937_64 rng1(123), rng2(123), rng3(456);
    PermutationWorkload w1(r.topo, 65536, rng1);
    PermutationWorkload w2(r.topo, 65536, rng2);
    PermutationWorkload w3(r.topo, 65536, rng3);
    CHECK(w1.targets() == w2.targets());
    CHECK(w1.targets() != w3.targets());
    std::set<NodeId> seen;
    for (std::size_t h = 0; h < w1.targets().size(); ++h) {
      CHECK(w1.targets()[h] != static_cast<NodeId>(h));  // no self-send
      seen.insert(w1.targets()[h]);
    }
    CHECK(seen.size() == 16);  // a bijection over all hosts

    w1.start(r.engine);
    r.engine.run();
    CHECK(r.engine.flows().size() == 16);
    for (const Flow& f : r.engine.flows()) CHECK(f.end_ps > 0);
  }

  return 0;
}
