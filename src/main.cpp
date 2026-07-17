#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "gpufab/config.hpp"
#include "gpufab/engine.hpp"
#include "gpufab/rng.hpp"
#include "gpufab/router/ecmp.hpp"
#include "gpufab/telemetry.hpp"
#include "gpufab/topology.hpp"

namespace {

using namespace gpufab;

std::unique_ptr<IRouter> make_router(const Config& cfg, RngRegistry& rng) {
  const std::string name = cfg.get_str("router.name");
  if (name == "ecmp") {
    return std::make_unique<EcmpRouter>(rng.stream("router.ecmp")());
  }
  throw std::runtime_error("unknown router: " + name);
}

void add_workload(const Config& cfg, Engine& engine) {
  const std::string kind = cfg.get_str("workload.kind");
  if (kind == "single_flow") {
    Flow f;
    f.id = 0;
    f.src = static_cast<NodeId>(cfg.get_i64("workload.src"));
    f.dst = static_cast<NodeId>(cfg.get_i64("workload.dst"));
    f.bytes = cfg.get_i64("workload.bytes");
    f.start_ps = 0;
    engine.add_flow(f);
    return;
  }
  throw std::runtime_error("unknown workload: " + kind);
}

int run(const std::string& config_path, const std::string& out_dir) {
  const Config cfg = Config::parse_file(config_path);

  RngRegistry rng(static_cast<std::uint64_t>(cfg.get_i64("seed")));

  if (cfg.get_str("topology.kind") != "clos2") {
    throw std::runtime_error("unknown topology: " + cfg.get_str("topology.kind"));
  }
  Topology topo = Topology::clos2(
      static_cast<std::int32_t>(cfg.get_i64("topology.hosts_per_leaf")),
      static_cast<std::int32_t>(cfg.get_i64("topology.leaves")),
      static_cast<std::int32_t>(cfg.get_i64("topology.spines")),
      cfg.get_i64("topology.host_gbps"), cfg.get_i64("topology.fabric_gbps"),
      cfg.get_i64("topology.prop_us") * kPsPerUs);

  // Uniform data-plane parameters across links; defaults keep small configs
  // congestion-free unless the config opts in.
  const std::int64_t buffer_b = cfg.get_i64_or("link.buffer_kb", 256) * 1024;
  const std::int64_t ecn_b = cfg.get_i64_or("link.ecn_threshold_kb", 64) * 1024;
  for (std::size_t i = 0; i < topo.num_links(); ++i) {
    topo.link(static_cast<LinkId>(i)).buffer_bytes = buffer_b;
    topo.link(static_cast<LinkId>(i)).ecn_threshold_bytes = ecn_b;
  }

  TransportParams transport;
  transport.window_chunks =
      static_cast<double>(cfg.get_i64_or("transport.window_chunks", 8));
  transport.rto_ps = cfg.get_i64_or("transport.rto_us", 500) * kPsPerUs;

  auto router = make_router(cfg, rng);
  Engine engine(topo, *router, cfg.get_i64("workload.chunk_bytes"), transport);
  add_workload(cfg, engine);

  const EngineStats stats = engine.run();

  std::filesystem::create_directories(out_dir);
  write_flows_csv(out_dir + "/flows.csv", engine.flows());
  write_seeds_txt(out_dir + "/seeds.txt", rng);

  std::printf(
      "gpufab_sim: router=%s events=%llu last_end_ps=%lld flows=%zu "
      "drops=%lld ecn_marks=%lld\n",
      router->name().c_str(),
      static_cast<unsigned long long>(stats.events_processed),
      static_cast<long long>(stats.last_flow_end_ps), engine.flows().size(),
      static_cast<long long>(stats.drops),
      static_cast<long long>(stats.ecn_marks));
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: gpufab_sim <flat-config> <out-dir>\n");
    return 2;
  }
  try {
    return run(argv[1], argv[2]);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "gpufab_sim: fatal: %s\n", e.what());
    return 1;
  }
}
