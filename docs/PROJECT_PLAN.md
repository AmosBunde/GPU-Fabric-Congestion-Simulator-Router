# Project Plan

**System:** GPU Fabric Congestion Simulator & Router
**Planning stance:** Each phase ends in something that runs and produces a
real artifact — never a half-built layer. Later phases are gated on earlier
exit criteria; the validation gate (Phase 2) blocks all routing work.

---

## 1. Goals

1. A deterministic, reproducible-by-construction fabric simulator credible
   enough that a reviewer trusts its numbers (validated against a published
   result before any comparison is made).
2. A clean quantitative answer: **ECMP vs flowlet/CONGA-style vs RL adaptive
   routing**, measured as p50/p99/p999 FCT with confidence intervals across
   seeds, on stated Clos topologies and collective workloads.
3. Honest scale: headline **1024 simulated nodes**, validated at 16–128.

## 2. Non-goals

- Per-packet fidelity (RTO dynamics, packet reordering) — excluded by ADR-001.
- Real-network deployment, kernels, NICs, or lossless/PFC fabrics (stated
  limitation; PFC is possible future work, not planned work).
- Dynamic topology changes or multi-failure-domain modeling.
- "Hundreds of thousands of nodes" — that is a marketing number, not ours.
- A general-purpose simulator product; every feature exists to answer §1.2.

## 3. Phase plan

### Phase 0 — Reproducibility spine ✅ **complete**
Built before any simulation logic, because it is the difference between a toy
and something a reviewer trusts.
- CMake + CTest, C++20, warnings-as-errors; ASan/UBSan wired into CI from
  commit one.
- Seeded PRNG registry: one master seed, named derived streams, all logged.
- `run.py`: one YAML config → one output directory stamped with config hash
  and git SHA.
- **Exit criterion (met):** ctest green; one command produces a hash-stamped
  results directory.

### Phase 1 — Walking skeleton ✅ **complete**
The thinnest vertical slice through every layer of the architecture diagram.
- 16-host 2-tier Clos, one flow, static ECMP, event engine advancing chunk
  events, measured FCT → Parquet → CDF plot.
- **Exit criterion (met):** an FCT CDF produced end-to-end; measured FCT
  equals the analytic store-and-forward value exactly (103,614,720 ps), and
  two runs are byte-identical.

### Phase 2 — Congestion emerges, then **validation gate** ⟵ next
- Data-plane model: per-link FIFO queues, buffer limits, tail-drop, ECN-style
  marking; `CongestionUpdate` telemetry events; link time-series Parquet table.
- Workloads: ring all-reduce (tree later), incast, uniform permutation.
- **Gate (blocks all router work):** reproduce a known result — drive a
  deliberate incast pattern and show the classic throughput-collapse curve
  (cf. Vasudevan et al., SIGCOMM 2009), and/or match theoretical fat-tree
  bisection bandwidth on a uniform permutation (Al-Fares et al., SIGCOMM
  2008). Deliverable: our plot next to the citation.
- Also at exit: performance check — 1024-node all-reduce replay completes in
  seconds on one core.

### Phase 3 — Baseline routing family + evaluation harness
- Extend `IRouter` with link-telemetry snapshots (queue depth, ECN, EWMA
  utilization) and a flowlet-boundary re-route hook. The router still never
  sees wall-clock time.
- Implement the flowlet/CONGA-style reactive load balancer.
- Harness: N seeded repetitions per config, sweeps over topology and load,
  p50/p99/p999 FCT with bootstrap CIs.
- **Exit criterion:** ECMP-vs-flowlet CDF across many seeds with error bands
  on a stated topology and workload. Deliberate ordering: a heuristic that
  beats ECMP exists **before** RL is attempted, so the project has a positive
  headline result regardless of how Phase 4 lands.

### Phase 4 — The contribution: RL adaptive router (timeboxed)
- Behind the same `IRouter` interface: state = link telemetry, reward =
  −tail FCT. Trained across multiple topologies/loads; never overfit to one
  topology and claim generality.
- Three honest outcomes, all publishable: (a) RL beats both baselines — lead
  with it; (b) RL matches the heuristic at lower configuration cost — still
  interesting; (c) RL underperforms and we explain why — a clean negative
  result reviewers respect more than a suspicious win.
- **Exit criterion:** the three-way CDF-with-CIs comparison plus a paragraph
  on where the learned policy helps and where it does not.
- **Timebox:** hard. The Phase 3 flowlet router is the safety net; when the
  box expires, write up whatever outcome is on the table.

### Phase 5 — Visualization and writeup
- Link-utilization heatmap (offline first; live only if time permits).
- README finalized: thesis → headline result → limitations, in that order.
  The limitations paragraph ("simulated at 1024 nodes, packet-train
  granularity, single failure domain, no dynamic topology changes") is the
  senior signal, not a weakness.
- **Exit criterion:** every figure in the writeup regenerates from one
  command against a tagged commit and a tracked config.

## 4. Dependencies between phases

```
Phase 0 ──▶ Phase 1 ──▶ Phase 2 ══▶ Phase 3 ──▶ Phase 4 ──▶ Phase 5
                          ║ validation gate:
                          ║ no router work of any kind
                          ║ until the sim reproduces a
                          ╚ published congestion result
```

## 5. Evaluation methodology (applies to Phases 3–5)

- **Metric:** flow completion time; report p50/p99/p999 and full CDFs. Tail
  percentiles are the object of study — means are never headlined.
- **Statistics:** ≥20 seeds per config point; bootstrap confidence intervals;
  identical seed sets across routers so comparisons are paired.
- **Controls:** ECMP is the control, not a feature. Every adaptive result is
  reported as a delta against it on the same topology, workload, and seeds.
- **Information parity:** heuristic and RL routers consume the same telemetry
  snapshot, so any RL gain is attributable to the policy, not the inputs.
- **Provenance:** a figure is publishable only from a Release build, clean
  git tree, tagged commit, tracked config (see DEPLOYMENT.md §5–7).

## 6. Risk register

| # | Risk | Phase | Likelihood | Impact | Mitigation / trigger |
|---|---|---|---|---|---|
| R1 | Chunk model too coarse to reproduce incast collapse | 2 | Med | Gate fails — project stalls | Chunk size is a config knob; validate at 4–16 KiB chunks; event model unchanged. Trigger: collapse curve qualitatively wrong after tuning → escalate to hybrid per-packet mode for validation runs only |
| R2 | Validation gate consumes the schedule | 2 | Med | Later phases compress | Two accepted validation targets (incast collapse OR bisection bandwidth); passing either opens Phase 3 |
| R3 | Flowlet router fails to beat ECMP | 3 | Low | No safety-net headline | Usually a telemetry-staleness bug, not a dead end: check snapshot freshness and flowlet-gap tuning before concluding |
| R4 | RL scope explosion | 4 | High | Overrun, unfinished writeup | Hard timebox; simplest viable policy first (contextual bandit / linear before deep RL); all three outcomes are publishable |
| R5 | RL overfits one topology, claims generality | 4 | Med | Reviewer distrust | Train/eval across topology + load grid; report per-topology deltas explicitly |
| R6 | Determinism silently broken | any | Low | Every figure invalid | Byte-identical two-run check in tests and phase acceptance; any parallelism must reproduce identical output or is rejected |
| R7 | Scale claim drift ("hundreds of thousands") | 5 | Low | Credibility loss | Fixed policy in README + this plan: 1024 headline, 16–128 validated |
| R8 | Results not regenerable at writeup time | 5 | Low | Rework | Tagged commits per phase; regeneration drill at each phase gate (DEPLOYMENT.md §5.3) |

## 7. Definition of done (project level)

1. Phase 2 validation plot sits next to its citation in the writeup.
2. Three-way router comparison (ECMP / flowlet / RL) with CIs on stated
   topologies and workloads, from ≥20 paired seeds.
3. Every figure regenerates from one command; every claim pins to a tag.
4. README leads with thesis, headline result, and limitations — in that order.
