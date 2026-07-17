# Architecture Design Document

**System:** GPU Fabric Congestion Simulator & Router
**Status:** Living document — reflects Phases 0–1 as built, Phases 2–5 as designed
**Companion diagram:** [`architecture.html`](architecture.html)

---

## 1. Purpose and system context

The system answers a single research question: *how much tail flow-completion
time (FCT) does congestion-aware adaptive routing recover over static ECMP in
GPU training fabrics?* Everything in the architecture exists to make the
answer **trustworthy**: deterministic simulation, an isolated routing seam,
seeded statistical evaluation, and artifacts that regenerate from one command.

It is a research instrument, not a network product. There is no production
data path, no real NICs, and no distributed deployment: the "system" is a
single-process simulator plus an offline analysis pipeline.

```
 actors                    system                          outputs
 ──────                    ──────                          ───────
 researcher ──configs──▶  gpufab_sim (C++20 DES core) ──▶ telemetry files
 CI runner  ──gates────▶  run.py / eval harness (Py)  ──▶ figures, metrics+CIs
 reviewer   ◀─README/figures/manifests──────────────────  reproducible claims
```

## 2. Architecture decision records

Decisions 1 and 2 were locked *before* engine code was written, because they
define the event types and the process boundary respectively. They are not
open for re-litigation; later ADRs record how the spine was implemented.

### ADR-001 — Simulation granularity: packet-train / chunk model
- **Status:** Accepted (locked)
- **Context:** Per-packet discrete-event simulation is faithful but collapses
  past a few hundred nodes replaying all-reduce. Pure fluid/flow-level models
  are fast but hide incast microbursts — the entire object of study.
- **Decision:** Schedule events per **chunk** of bytes (default 64 KiB) with
  periodic per-link congestion updates. Event types: `FlowStart`,
  `ChunkArrival`, and (Phase 2) `CongestionUpdate`.
- **Consequences:** Queue buildup and drops are visible at chunk resolution;
  1024 nodes replaying all-reduce is a seconds-scale run. Sub-chunk effects
  (per-packet reordering, exact RTO dynamics) are out of scope and stated as
  limitations.

### ADR-002 — Language boundary is a file format, never an FFI
- **Status:** Accepted (locked)
- **Context:** C++ for the hot simulation loop; Python for statistics and
  plotting. Embedding Python or adding pybind couples build systems, breaks
  reproducibility (environment leaks into results), and pollutes the fast path.
- **Decision:** The only thing that crosses the boundary is **telemetry
  files**. The engine writes CSV; `run.py` converts to Parquet, which is the
  canonical artifact Python reads. Configs cross in the other direction as
  files too (YAML → flattened `sim.cfg`).
- **Consequences:** Either side can be rebuilt, rerun, or replaced
  independently; a run directory is a complete, self-describing record; the
  C++ core has zero third-party dependencies.

### ADR-003 — Integer-picosecond virtual clock
- **Status:** Accepted
- **Context:** Byte-identical reproducibility requires that no floating-point
  rounding can perturb event order across platforms or compiler flags.
- **Decision:** Virtual time is `int64_t` picoseconds (`gpufab::Ps`).
  Serialization delay `bytes*8*1000/gbps` is exact for whole-Gbps link rates.
  Link rates are therefore configured as integer Gbps.
- **Consequences:** ~106 days of simulated time fits in the representation
  (vastly more than needed); all delay arithmetic is exact; fractional link
  rates are unsupported by design.

### ADR-004 — Deterministic event ordering via insertion-sequence tie-break
- **Status:** Accepted
- **Decision:** The event queue orders by `(time_ps, seq)` where `seq` is a
  monotonically increasing insertion counter. Simultaneous events therefore
  execute in a defined, reproducible order.
- **Consequences:** No dependence on `std::priority_queue` tie behavior;
  replays are exact.

### ADR-005 — One master seed, named derived streams, all logged
- **Status:** Accepted
- **Decision:** Components never construct RNGs. They request
  `RngRegistry::stream("component.name")`; the derived seed is
  `splitmix64(master ^ fnv1a(name))` and is recorded to `seeds.txt` in every
  run directory.
- **Consequences:** Adding a new randomized component cannot silently shift
  another component's stream; every random draw in a published figure is
  attributable to a logged seed.

### ADR-006 — Routing is a full-path decision behind one interface
- **Status:** Accepted (revisit at Phase 3 if per-hop decisions are needed)
- **Decision:** `IRouter::route(flow, topology) -> std::vector<NodeId>`
  returns the complete source→destination path at flow start. ECMP, flowlet,
  and RL implementations swap behind this seam with zero engine changes.
- **Consequences:** Simple, testable contract for Phase 1–3. Flowlet routers
  re-enter the decision at flowlet boundaries (Phase 3 extends the contract
  with telemetry and a re-route hook); per-packet adaptive schemes are out of
  scope per ADR-001.

### ADR-007 — Plain-assert tests, no test-framework dependency
- **Status:** Accepted
- **Decision:** Tests are small executables using a local `CHECK` macro,
  registered with CTest. No GoogleTest/Catch2.
- **Consequences:** Zero external dependencies to fetch or pin; tests double
  as minimal usage examples. Revisit only if matcher/fixture needs outgrow it.

## 3. Component catalog

Layered view; each component names its file, responsibility, and contract.

### 3.1 Core primitives

| Component | Files | Responsibility |
|---|---|---|
| Virtual time | `src/gpufab/time.hpp` | `Ps` (int64 ps), exact `serialization_ps(bytes, gbps)` |
| RNG registry | `src/gpufab/rng.hpp` | Master seed → named `std::mt19937_64` streams, seed log |
| Config | `src/gpufab/config.{hpp,cpp}` | Flat dotted-key parser, typed getters, missing-key errors |

### 3.2 Fabric model

| Component | Files | Responsibility |
|---|---|---|
| Topology | `src/gpufab/topology.{hpp,cpp}` | 2-tier Clos builder; node kinds (Host/Leaf/Spine); directed `Link` table with O(log n) pair lookup |
| Link state | `Link` in `topology.hpp` | Per-link `gbps`, `prop_ps`, and `busy_until_ps` — the transmitter-occupancy field that Phase 2 grows into real queues, buffer limits, drops, and ECN marking |
| Flow | `src/gpufab/flow.hpp` | Flow record: endpoints, bytes, start/end virtual times, chosen path |

Node numbering is positional (hosts, then leaves, then spines), which keeps
IDs stable for a given topology config — a requirement for comparing paths
across runs.

### 3.3 Simulation engine

`src/gpufab/engine.{hpp,cpp}` — the deterministic DES core.

- **Event model** (per ADR-001):
  - `FlowStart` — asks the router for a path, injects the flow's chunks at
    the source.
  - `ChunkArrival` — a chunk reached `path[hop]`; either forwards it
    (store-and-forward over the next link, contending on `busy_until_ps`) or,
    at the destination, decrements the flow's outstanding-chunk count and
    stamps `end_ps` when it hits zero.
  - `CongestionUpdate` (Phase 2, reserved) — periodic per-link telemetry
    refresh feeding the routing control plane.
- **Main loop:** pop min `(time, seq)`, dispatch, repeat until empty. The
  engine exposes `EngineStats` (events processed, end time) for sanity checks.
- **Prohibitions:** no wall-clock reads, no I/O, no threads, no randomness
  except via injected `RngRegistry` streams.

### 3.4 Routing control plane

`src/gpufab/router/irouter.hpp` — **the single seam the project pivots on.**

Contract (current, Phase 1):

```cpp
class IRouter {
  virtual std::string name() const = 0;
  // Full node path src -> ... -> dst, chosen at flow start.
  // Every consecutive pair must be a link in `topo`.
  virtual std::vector<NodeId> route(const Flow&, const Topology&) = 0;
};
```

Contract invariants, all phases:
1. The router **never sees wall-clock time** — only virtual quantities.
2. The router sees topology and (from Phase 3) link telemetry snapshots; it
   never mutates engine or link state.
3. All randomness comes from a named registry stream (logged).

Planned Phase 3 extension (design, not yet built): a `LinkTelemetry` snapshot
(queue depth, ECN marks, utilization EWMA per link) passed into `route`, plus
a flowlet-boundary re-route entry point. The RL router (Phase 4) consumes the
same snapshot as its observation — deliberately, so the learned policy and the
heuristic compete on identical information.

Implementations:

| Router | Files | Role |
|---|---|---|
| ECMP | `src/gpufab/router/ecmp.{hpp,cpp}` | Control baseline: spine = `splitmix64(flow_id ^ salt) % spines`. Congestion-blind by design. |
| Flowlet/CONGA-style | Phase 3 | Reactive load balancing on telemetry; the safety-net positive result |
| RL adaptive | Phase 4 | Learned policy; state = link telemetry, reward = −tail FCT |

### 3.5 Telemetry and results store

- `src/gpufab/telemetry.{hpp,cpp}` writes `flows.csv` (one row per flow:
  id, src, dst, bytes, start/end/FCT in ps, hyphen-joined path) and
  `seeds.txt`.
- `python/run.py` owns the run-directory lifecycle: config hashing
  (SHA-256 of canonical JSON), git SHA + dirty flag capture, YAML flattening,
  binary invocation, CSV→Parquet conversion, manifest emission.
- Phase 2 adds link-level time series (queue depth, drops, ECN marks,
  utilization) as a second Parquet table; Phase 3's harness aggregates
  per-seed run directories into sweep-level metrics with confidence intervals.

### 3.6 Evaluation and visualization (Python)

- `python/plot_fct.py` — FCT CDF from `flows.parquet` (matplotlib, headless).
- Phase 3 harness (planned): N seeded repetitions per config point, sweeps
  over topology/load, p50/p99/p999 FCT with bootstrap CIs, CDF overlays with
  error bands. Phase 5 adds the link-utilization heatmap (offline first).

## 4. Data contracts

### 4.1 Config schema
See README §Configuration reference. The YAML in `configs/` is the source of
truth; `run.py` flattens it to dotted keys so the C++ side needs no YAML
dependency. Unknown keys are ignored by the binary but still contribute to
the config hash — two runs with different YAML are never conflated.

### 4.2 `flows` table (canonical, Parquet)

| Column | Type | Unit |
|---|---|---|
| `flow_id` | int64 | — |
| `src`, `dst` | int32 | node id |
| `bytes` | int64 | bytes |
| `start_ps`, `end_ps`, `fct_ps` | int64 | picoseconds (`end_ps = -1` if incomplete) |
| `path` | string | hyphen-joined node ids |

### 4.3 Run manifest (`manifest.json`)
`config_hash` (SHA-256), `git_sha`, `git_dirty`, `seed`, `created_utc`,
`argv`. A figure whose manifest shows `git_dirty: true` is not publishable.

## 5. Quality attributes

| Attribute | Requirement | Mechanism / evidence |
|---|---|---|
| Determinism | Same seed + config → byte-identical output, forever | Integer-ps clock (ADR-003), seq tie-break (ADR-004), seeded streams (ADR-005); verified by `test_engine_fct` and a two-run `diff` in Phase 1 acceptance |
| Correctness | Engine matches closed-form results where they exist | Analytic pipeline-FCT equality test; Phase 2 gate reproduces published incast collapse before any router comparison |
| Performance | 1024 nodes replaying all-reduce in seconds on a workstation | Chunk granularity (ADR-001); single alloc-light event loop; profile gate at Phase 2 exit |
| Memory safety | No UB in any tested path | ASan+UBSan build in CI on every push; warnings-as-errors |
| Reproducibility of figures | One command per figure, from a git-tracked config | `run.py` + hash-stamped run dirs + manifests |
| Extensibility | New router = new class behind `IRouter`, zero engine edits | Enforced by the interface seam; ECMP is the template |

## 6. Error-handling policy

Fail fast and loud: invalid config, unknown router/workload/topology kinds,
degenerate router paths, and missing links all throw and terminate the run
with a non-zero exit — a partially valid simulation result is worse than none.
`run.py` propagates the binary's exit code and never emits a Parquet artifact
for a failed run.

## 7. Extension points and planned evolution

| Phase | Change | Where it lands |
|---|---|---|
| 2 | Per-link FIFO queue with buffer limit, tail-drop, ECN threshold marking | `Link` grows a queue model; `ChunkArrival` consults it; new `CongestionUpdate` event |
| 2 | Ring all-reduce, incast, permutation workloads | `add_workload` in `main.cpp` → dedicated `workload/` module |
| 2 | Link time-series telemetry table | `telemetry.{hpp,cpp}` + second Parquet table |
| 3 | `LinkTelemetry` snapshot into `IRouter`; flowlet router | `router/` |
| 3 | Sweep harness (N seeds × configs, CIs) | `python/harness.py` orchestrating `gpufab_sim` processes |
| 4 | RL policy (training loop in Python against exported episodes, frozen policy evaluated in C++, or a table/linear policy loaded from file — decision deferred, but the file boundary of ADR-002 holds either way) | `router/rl*` |
| 5 | Utilization heatmap | `python/plot_heatmap.py` |

## 8. Architectural risks

| Risk | Impact | Mitigation |
|---|---|---|
| Chunk granularity too coarse to reproduce incast collapse | Phase 2 gate fails | Chunk size is a config knob; drop to 4–16 KiB for validation runs; the event model does not change |
| `IRouter` full-path contract too rigid for flowlet re-routing | Phase 3 rework | ADR-006 already plans the flowlet-boundary entry point; the engine's per-chunk forwarding makes per-flowlet path changes natural |
| RL scope explosion | Phase 4 overruns | Timeboxed; flowlet router is the guaranteed headline; all three RL outcomes are publishable |
| Determinism broken by a future dependency (e.g., parallelism) | Invalidates every figure | Invariant is tested, not assumed; any parallelism proposal must reproduce byte-identical output or is rejected |
