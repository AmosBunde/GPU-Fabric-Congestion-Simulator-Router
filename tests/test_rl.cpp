#include "gpufab/router/rl.hpp"

#include <cstdio>

#include "gpufab/engine.hpp"
#include "gpufab/workloads.hpp"

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                        \
    }                                                                  \
  } while (0)

int main() {
  using namespace gpufab;

  // 1. Learning sanity, no simulator in the loop: congest spine 0's uplink,
  //    feed the router the resulting bad rewards for choosing it and good
  //    rewards otherwise; the greedy policy must learn to avoid spine 0.
  {
    Topology t = Topology::clos2(4, 4, 4, 100, 100, kPsPerUs);
    const std::vector<double> no_util(t.num_links(), 0.0);
    const FabricView view(t, no_util);
    std::mt19937_64 rng(42);
    RlRouter::Params p;
    p.epsilon0 = 1.0;  // pure exploration while we teach it
    p.epsilon_decay = 1e12;
    RlRouter router(p, rng);

    t.link(t.link_between(t.leaf_node(0), t.spine_node(0))).queue_bytes =
        600 * 1024;  // spine 0: persistently congested

    Flow f;
    f.id = 1;
    f.src = 0;  // leaf 0
    f.dst = 4;  // leaf 1
    const std::int64_t key = router.state_key(f, view);
    for (int i = 0; i < 400; ++i) {
      const auto path = router.route(f, view);
      CHECK(path.size() == 5);
      const bool chose_congested = path[2] == t.spine_node(0);
      router.on_chunk_feedback(f, chose_congested ? 50 * kPsPerUs : 0);
    }
    const std::int32_t greedy = router.greedy_action(key);
    std::printf("greedy action after training: spine %d\n", greedy);
    CHECK(greedy != 0);  // learned to avoid the congested spine
  }

  // 2. Full-sim determinism: identical seeds -> byte-identical outcomes.
  {
    auto run = [](std::vector<Ps>* ends) {
      Topology t = Topology::clos2(4, 4, 4, 100, 100, kPsPerUs);
      for (std::size_t i = 0; i < t.num_links(); ++i) {
        t.link(static_cast<LinkId>(i)).buffer_bytes = 512 * 1024;
        t.link(static_cast<LinkId>(i)).ecn_threshold_bytes = 128 * 1024;
      }
      std::mt19937_64 rl_rng(7), wl_rng(9);
      RlRouter router(RlRouter::Params{}, rl_rng);
      Engine engine(t, router, 65536, TransportParams{});
      PermutationWorkload w(t, 1 << 20, wl_rng);
      engine.set_workload(&w);
      w.start(engine);
      engine.run();
      for (const Flow& f : engine.flows()) {
        if (f.end_ps < 0) return false;
        ends->push_back(f.end_ps);
      }
      return true;
    };
    std::vector<Ps> a, b;
    CHECK(run(&a));
    CHECK(run(&b));
    CHECK(!a.empty() && a == b);
  }

  return 0;
}
