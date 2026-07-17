# GPU Fabric Congestion Simulator & Router

**Thesis.** In GPU training fabrics, tail flow-completion time (p99/p999 FCT)
under collective-communication traffic is dominated by transient congestion —
incast microbursts and ECMP hash collisions — that static routing cannot see.
This project builds a deterministic discrete-event fabric simulator and asks
one question: how much tail-FCT does congestion-aware adaptive routing
(a flowlet/CONGA-style heuristic, and an RL policy trained on link telemetry)
recover over static ECMP, on Clos topologies up to 1024 nodes?

**Headline result.** *Pending — Phase 1 (walking skeleton) is complete; the
congestion model and validation gate (Phase 2) come next. No routing
comparison is claimed until the simulator reproduces a known incast
throughput-collapse curve.*

**Limitations.** Simulated fabric, packet-train (chunk) granularity — not
per-packet; virtual clock, single-threaded core; validated at 16–128 nodes,
headline scale 1024 nodes; single failure domain; no dynamic topology changes;
no PFC/lossless-fabric modeling yet.

---

## Locked design decisions

1. **Simulation granularity: packet-train / chunk model.** Events are
   scheduled per chunk of bytes (default 64 KiB) with per-link congestion
   state, not per packet. Fine enough to see queue buildup and drops, coarse
   enough to run 1024 nodes replaying all-reduce in seconds. This defines the
   engine's event types (`FlowStart`, `ChunkArrival`, later
   `CongestionUpdate`).
2. **Language boundary = files.** C++20 core, Python evaluation/visualization.
   The only thing that crosses is telemetry files (Parquet). No FFI, no
   pybind, no embedded Python — the fast path stays clean and every run is
   reproducible from its artifacts.

## Reproducibility invariants

- Single-threaded discrete-event core, integer-picosecond virtual clock. No
  wall-clock, no sockets, no threads in the model path.
- One master seed; every component draws from a named, logged derived stream
  (`seeds.txt` in each run directory). Same seed + same config = byte-identical
  results.
- Every figure regenerates from one command against a git-tracked config:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/python python/run.py configs/phase1_smoke.yaml
```

This produces `results/<utc-stamp>_<confighash8>_<gitsha8>/` containing the
config copy, manifest (git SHA + dirty flag), logged seeds, `flows.parquet`,
and `fct_cdf.png`.

Tests (warnings-as-errors; CI runs them under ASan+UBSan):

```sh
cmake -B build-san -DCMAKE_BUILD_TYPE=Debug -DGPUFAB_SANITIZE=ON
cmake --build build-san -j && ctest --test-dir build-san --output-on-failure
```

## Architecture

Deterministic discrete-event core (C++20) · pluggable routing control plane
behind a single `IRouter` interface (`src/gpufab/router/irouter.hpp`) ·
telemetry stream → Parquet results store → Python evaluation harness and
visualization. The congestion feedback loop — data-plane queue depth/ECN back
to the router's path decisions — is the intellectual core of the project.
See `docs/architecture.html` for the full diagram.

## Phase status

| Phase | Deliverable | Status |
|---|---|---|
| 0 | Reproducibility spine: CMake+CTest, warnings-as-errors, ASan/UBSan CI, seeded PRNG registry, `run.py` → hash-stamped run dirs | **Done** |
| 1 | Walking skeleton: 16-host 2-tier Clos, one flow, static ECMP, measured FCT → Parquet → CDF | **Done** |
| 2 | Data-plane model (queues, drops, ECN) + all-reduce workload; **validation gate:** reproduce incast throughput collapse | Next |
| 3 | `IRouter` baselines (ECMP control, flowlet/CONGA) + seeded sweep harness with p50/p99/p999 FCT and CIs | — |
| 4 | RL adaptive router behind the same interface; three-way CDF comparison | — |
| 5 | Link-utilization heatmap + writeup | — |
