#include "gpufab/rng.hpp"

#include <cstdio>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                        \
    }                                                                  \
  } while (0)

int main() {
  using gpufab::RngRegistry;

  // Same master seed + same component => identical stream, forever.
  RngRegistry a(42), b(42);
  for (int i = 0; i < 100; ++i) {
    CHECK(a.stream("engine")() == b.stream("engine")());
  }

  // Different components draw from independent streams.
  RngRegistry c(42);
  CHECK(c.stream("engine")() != c.stream("router.ecmp")());

  // Different master seeds diverge.
  RngRegistry d(43);
  CHECK(RngRegistry(42).stream("engine")() != d.stream("engine")());

  // Every stream handed out is recorded with its derived seed.
  CHECK(c.seeds().size() == 2);
  CHECK(c.seeds().count("engine") == 1);
  CHECK(c.seeds().count("router.ecmp") == 1);
  CHECK(c.master() == 42);
  return 0;
}
