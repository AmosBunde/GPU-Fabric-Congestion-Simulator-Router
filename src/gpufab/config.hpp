#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

namespace gpufab {

// Flat dotted-key config ("topology.spines 4", one pair per line, '#' comments).
// run.py flattens the user-facing YAML into this format; the C++ side stays
// dependency-free and the YAML file remains the single source of truth.
class Config {
 public:
  static Config parse_file(const std::string& path);
  static Config parse_string(const std::string& text);

  const std::string& get_str(const std::string& key) const;
  std::int64_t get_i64(const std::string& key) const;
  std::int64_t get_i64_or(const std::string& key, std::int64_t fallback) const;
  bool has(const std::string& key) const { return kv_.count(key) > 0; }

  const std::map<std::string, std::string>& items() const { return kv_; }

 private:
  std::map<std::string, std::string> kv_;
};

}  // namespace gpufab
