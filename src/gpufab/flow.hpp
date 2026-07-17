#pragma once

#include <cstdint>
#include <vector>

#include "gpufab/time.hpp"
#include "gpufab/topology.hpp"

namespace gpufab {

struct Flow {
  std::int64_t id = 0;
  NodeId src = -1;
  NodeId dst = -1;
  std::int64_t bytes = 0;
  Ps start_ps = 0;

  // Filled in by the engine.
  std::vector<NodeId> path;
  Ps end_ps = -1;  // -1 until complete

  Ps fct_ps() const { return end_ps < 0 ? -1 : end_ps - start_ps; }
};

}  // namespace gpufab
