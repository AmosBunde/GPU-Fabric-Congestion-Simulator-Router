#pragma once

#include <string>
#include <vector>

#include "gpufab/flow.hpp"
#include "gpufab/rng.hpp"

namespace gpufab {

// Telemetry crosses the C++/Python boundary as files, never as an FFI
// (locked Decision 2). The engine emits CSV; run.py converts it to Parquet,
// which is the canonical artifact the Python side reads.
void write_flows_csv(const std::string& path, const std::vector<Flow>& flows);

void write_seeds_txt(const std::string& path, const RngRegistry& rng);

}  // namespace gpufab
