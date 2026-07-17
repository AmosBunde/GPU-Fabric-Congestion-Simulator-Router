# Milestones & Issue Tracker

GitHub milestones and issues are the source of truth; this file is the
in-repo record. One branch per issue; every change lands on `main` via a PR.

## Milestones

| # | Milestone | Gate | Status |
|---|---|---|---|
| 1 | Phase 2 — Congestion + validation gate | Incast throughput-collapse curve reproduced | Open |
| 2 | Phase 3 — Baselines + evaluation harness | ECMP-vs-flowlet CDF, ≥20 seeds, error bands | Open |
| 3 | Phase 4 — RL adaptive router | Three-way CDF with CIs + honest analysis | Open |
| 4 | Phase 5 — Visualization + writeup | Every figure regenerates from one command | Open |

## Issues

| Issue | Title | Milestone | Branch | Status |
|---|---|---|---|---|
| [#1](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/1) | Data-plane queues, buffer limits, tail-drop, ECN + windowed transport with RTO | Phase 2 | `issue-1-data-plane` | Open |
| [#2](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/2) | Workloads: incast, ring all-reduce, permutation + completion hook | Phase 2 | `issue-2-workloads` | Open |
| [#3](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/3) | Link telemetry time series + incast-collapse validation gate | Phase 2 | `issue-3-validation` | Open |
| [#4](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/4) | Router telemetry snapshot + flowlet/CONGA-style router | Phase 3 | `issue-4-flowlet` | Open |
| [#5](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/5) | Evaluation harness: seeded sweeps, tail percentiles + CIs, comparison CDF | Phase 3 | `issue-5-harness` | Open |
| [#6](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/6) | RL adaptive router + three-way comparison | Phase 4 | `issue-6-rl-router` | Open |
| [#7](https://github.com/AmosBunde/GPU-Fabric-Congestion-Simulator-Router/issues/7) | Link-utilization heatmap + writeup | Phase 5 | `issue-7-viz-writeup` | Open |

Dependency order: #1 → #2 → #3 (gate) → #4 → #5 → #6 → #7. The Phase 2 gate
(#3) blocks all router work (#4 onward).
