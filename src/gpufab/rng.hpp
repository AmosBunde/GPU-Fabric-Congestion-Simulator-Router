#pragma once

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <string_view>

namespace gpufab {

constexpr std::uint64_t splitmix64(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

constexpr std::uint64_t fnv1a(std::string_view s) {
  std::uint64_t h = 0xcbf29ce484222325ULL;
  for (char c : s) {
    h ^= static_cast<std::uint8_t>(c);
    h *= 0x100000001b3ULL;
  }
  return h;
}

// One master seed, one derived stream per named component. Every derived seed
// is recorded so a run directory can log exactly what randomness it consumed.
class RngRegistry {
 public:
  explicit RngRegistry(std::uint64_t master_seed) : master_(master_seed) {}

  std::mt19937_64& stream(const std::string& component) {
    auto it = streams_.find(component);
    if (it == streams_.end()) {
      const std::uint64_t seed = splitmix64(master_ ^ fnv1a(component));
      seeds_.emplace(component, seed);
      it = streams_.emplace(component, std::mt19937_64(seed)).first;
    }
    return it->second;
  }

  std::uint64_t master() const { return master_; }
  const std::map<std::string, std::uint64_t>& seeds() const { return seeds_; }

 private:
  std::uint64_t master_;
  std::map<std::string, std::mt19937_64> streams_;
  std::map<std::string, std::uint64_t> seeds_;
};

}  // namespace gpufab
