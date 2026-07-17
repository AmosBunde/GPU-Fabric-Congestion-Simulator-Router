#pragma once

#include <cstdint>

namespace gpufab {

// Virtual time in integer picoseconds. Integer ps keeps serialization delays
// exact for whole-Gbps link rates (1 Gbps = 0.001 bit/ps), which is what makes
// same-seed runs byte-identical across platforms.
using Ps = std::int64_t;

constexpr Ps kPsPerNs = 1'000;
constexpr Ps kPsPerUs = 1'000'000;
constexpr Ps kPsPerMs = 1'000'000'000;

// Serialization delay of `bytes` on a `gbps` link, exact in ps.
constexpr Ps serialization_ps(std::int64_t bytes, std::int64_t gbps) {
  return bytes * 8 * 1000 / gbps;
}

}  // namespace gpufab
