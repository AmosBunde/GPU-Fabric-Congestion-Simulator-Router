#include "gpufab/config.hpp"

#include <fstream>
#include <sstream>

namespace gpufab {

Config Config::parse_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("config: cannot open " + path);
  std::stringstream ss;
  ss << in.rdbuf();
  return parse_string(ss.str());
}

Config Config::parse_string(const std::string& text) {
  Config cfg;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    const auto hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    std::istringstream ls(line);
    std::string key, value;
    if (!(ls >> key >> value)) continue;
    cfg.kv_[key] = value;
  }
  return cfg;
}

const std::string& Config::get_str(const std::string& key) const {
  auto it = kv_.find(key);
  if (it == kv_.end()) throw std::runtime_error("config: missing key " + key);
  return it->second;
}

std::int64_t Config::get_i64(const std::string& key) const {
  return std::stoll(get_str(key));
}

std::int64_t Config::get_i64_or(const std::string& key,
                                std::int64_t fallback) const {
  auto it = kv_.find(key);
  return it == kv_.end() ? fallback : std::stoll(it->second);
}

double Config::get_f64_or(const std::string& key, double fallback) const {
  auto it = kv_.find(key);
  return it == kv_.end() ? fallback : std::stod(it->second);
}

}  // namespace gpufab
