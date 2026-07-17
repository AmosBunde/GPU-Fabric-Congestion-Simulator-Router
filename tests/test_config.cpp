#include "gpufab/config.hpp"

#include <cstdio>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                        \
    }                                                                  \
  } while (0)

int main() {
  using gpufab::Config;
  const Config cfg = Config::parse_string(
      "seed 42\n"
      "# a comment line\n"
      "topology.spines 4  # trailing comment\n"
      "router.name ecmp\n"
      "\n");
  CHECK(cfg.get_i64("seed") == 42);
  CHECK(cfg.get_i64("topology.spines") == 4);
  CHECK(cfg.get_str("router.name") == "ecmp");
  CHECK(cfg.get_i64_or("missing", 7) == 7);
  CHECK(!cfg.has("missing"));
  bool threw = false;
  try {
    (void)cfg.get_str("missing");
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);
  return 0;
}
