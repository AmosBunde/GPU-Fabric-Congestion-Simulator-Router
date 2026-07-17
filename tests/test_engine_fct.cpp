#include "gpufab/engine.hpp"

#include <cstdio>

#include "gpufab/router/ecmp.hpp"

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                        \
    }                                                                  \
  } while (0)

namespace {

gpufab::Ps run_once() {
  using namespace gpufab;
  Topology t = Topology::clos2(4, 4, 4, 100, 100, kPsPerUs);
  EcmpRouter router(0x5eed);
  Engine engine(t, router, 65536);
  Flow f;
  f.id = 0;
  f.src = 0;
  f.dst = 12;  // cross-leaf: host -> leaf -> spine -> leaf -> host, 4 links
  f.bytes = 1 << 20;
  f.start_ps = 0;
  engine.add_flow(f);
  engine.run();
  return engine.flows()[0].fct_ps();
}

}  // namespace

int main() {
  using namespace gpufab;

  // Analytic pipeline FCT for a single uncontended flow, store-and-forward:
  // 1 MiB in 64 KiB chunks = 16 chunks; S = 65536*8/100Gbps = 5,242,880 ps;
  // P = 1 us; 4 equal-rate hops => FCT = 15*S + 4*(S+P) = 103,614,720 ps.
  constexpr Ps kS = serialization_ps(65536, 100);
  static_assert(kS == 5'242'880);
  constexpr Ps kExpected = 15 * kS + 4 * (kS + kPsPerUs);

  const Ps fct = run_once();
  std::printf("fct_ps=%lld expected=%lld\n", static_cast<long long>(fct),
              static_cast<long long>(kExpected));
  CHECK(fct == kExpected);

  // Same seed + same config = identical result, run-to-run.
  CHECK(run_once() == fct);
  return 0;
}
