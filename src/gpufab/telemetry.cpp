#include "gpufab/telemetry.hpp"

#include <fstream>
#include <stdexcept>

namespace gpufab {

namespace {
std::ofstream open_or_throw(const std::string& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("telemetry: cannot write " + path);
  return out;
}

std::string path_to_string(const std::vector<NodeId>& path) {
  std::string s;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i) s += '-';
    s += std::to_string(path[i]);
  }
  return s;
}
}  // namespace

void write_flows_csv(const std::string& path, const std::vector<Flow>& flows) {
  auto out = open_or_throw(path);
  out << "flow_id,src,dst,bytes,start_ps,end_ps,fct_ps,retransmits,timeouts,"
         "path\n";
  for (const Flow& f : flows) {
    out << f.id << ',' << f.src << ',' << f.dst << ',' << f.bytes << ','
        << f.start_ps << ',' << f.end_ps << ',' << f.fct_ps() << ','
        << f.retransmits << ',' << f.timeouts << ',' << path_to_string(f.path)
        << '\n';
  }
}

void write_links_csv(const std::string& path,
                     const std::vector<LinkSample>& samples,
                     const Topology& topo) {
  auto out = open_or_throw(path);
  out << "time_ps,link_id,src,dst,queue_bytes,utilization,drops_cum,"
         "ecn_marks_cum\n";
  char util[32];
  for (const LinkSample& s : samples) {
    const Link& l = topo.link(s.link);
    std::snprintf(util, sizeof(util), "%.6f", s.utilization);
    out << s.time_ps << ',' << s.link << ',' << l.src << ',' << l.dst << ','
        << s.queue_bytes << ',' << util << ',' << s.drops_cum << ','
        << s.ecn_marks_cum << '\n';
  }
}

void write_seeds_txt(const std::string& path, const RngRegistry& rng) {
  auto out = open_or_throw(path);
  out << "master " << rng.master() << '\n';
  for (const auto& [component, seed] : rng.seeds()) {
    out << component << ' ' << seed << '\n';
  }
}

}  // namespace gpufab
