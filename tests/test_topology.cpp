#include "gpufab/topology.hpp"

#include <cstdio>

#include "gpufab/flow.hpp"
#include "gpufab/router/ecmp.hpp"

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                        \
    }                                                                  \
  } while (0)

int main() {
  using namespace gpufab;

  // 16 hosts: 4 leaves x 4 hosts, 4 spines.
  Topology t = Topology::clos2(4, 4, 4, 100, 100, kPsPerUs);
  CHECK(t.num_hosts() == 16);
  CHECK(t.num_nodes() == 16 + 4 + 4);
  // Directed links: 16 host-leaf pairs + 4x4 leaf-spine pairs, both directions.
  CHECK(t.num_links() == 2 * (16 + 16));

  CHECK(t.kind(0) == NodeKind::Host);
  CHECK(t.kind(t.leaf_node(0)) == NodeKind::Leaf);
  CHECK(t.kind(t.spine_node(3)) == NodeKind::Spine);
  CHECK(t.leaf_of_host(0) == t.leaf_node(0));
  CHECK(t.leaf_of_host(15) == t.leaf_node(3));

  const Link& l = t.link(t.link_between(0, t.leaf_node(0)));
  CHECK(l.src == 0 && l.dst == t.leaf_node(0));
  CHECK(l.gbps == 100 && l.prop_ps == kPsPerUs);

  // ECMP produces a valid cross-leaf path: every hop is a real link.
  EcmpRouter router(0xabcdef);
  const std::vector<double> no_util(t.num_links(), 0.0);
  const FabricView view(t, no_util);
  Flow f;
  f.id = 7;
  f.src = 0;
  f.dst = 12;
  const auto path = router.route(f, view);
  CHECK(path.size() == 5);
  CHECK(path.front() == 0 && path.back() == 12);
  for (std::size_t i = 0; i + 1 < path.size(); ++i) {
    (void)t.link_between(path[i], path[i + 1]);  // throws if not a link
  }

  // Same-leaf traffic never touches a spine.
  f.dst = 1;
  const auto short_path = router.route(f, view);
  CHECK(short_path.size() == 3);
  CHECK(t.kind(short_path[1]) == NodeKind::Leaf);
  return 0;
}
