# Milestones & Issue Tracker

GitHub milestones and issues are the source of truth; this file is the
in-repo record. One branch per issue; every change lands on `main` via a PR.

## Milestones

| # | Milestone | Gate | Status |
|---|---|---|---|
| 1 | Phase 2 — Congestion + validation gate | Incast throughput-collapse curve reproduced | **Closed** — gate passes, enforced in CI |
| 2 | Phase 3 — Baselines + evaluation harness | ECMP-vs-flowlet CDF, ≥20 seeds, error bands | **Closed** — p99 halved (15.9→7.9 ms) |
| 3 | Phase 4 — RL adaptive router | Three-way CDF with CIs + honest analysis | **Closed** — clean negative result (docs/RESULTS.md) |
| 4 | Phase 5 — Visualization + writeup | Every figure regenerates from one command | **Closed** |

## Issues

| Issue | Title | Milestone | Branch | Status |
|---|---|---|---|---|
| [#1](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/1) | Data-plane queues, buffer limits, tail-drop, ECN + windowed transport with RTO | Phase 2 | `issue-1-data-plane` | Closed — PR #8 |
| [#2](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/2) | Workloads: incast, ring all-reduce, permutation + completion hook | Phase 2 | `issue-2-workloads` | Closed — PR #9 |
| [#3](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/3) | Link telemetry time series + incast-collapse validation gate | Phase 2 | `issue-3-validation` | Closed — PR #10 |
| [#4](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/4) | Router telemetry snapshot + flowlet/CONGA-style router | Phase 3 | `issue-4-flowlet` | Closed — PR #11 |
| [#5](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/5) | Evaluation harness: seeded sweeps, tail percentiles + CIs, comparison CDF | Phase 3 | `issue-5-harness` | Closed — PR #12 |
| [#6](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/6) | RL adaptive router + three-way comparison | Phase 4 | `issue-6-rl-router` | Closed — PR #13 |
| [#7](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/7) | Link-utilization heatmap + writeup | Phase 5 | `issue-7-viz-writeup` | Closed — PR #14 |

Dependency order: #1 → #2 → #3 (gate) → #4 → #5 → #6 → #7. The Phase 2 gate
(#3) blocks all router work (#4 onward).
