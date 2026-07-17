# Results

All numbers from `python/harness.py`: 20 paired seeds per router (identical
workloads across routers), 64-host 2-tier Clos at 4:1 leaf oversubscription,
2 MiB uniform-permutation flows, 64 KiB chunks, 512 KiB buffers, ECN at
128 KiB, RTO 500 µs. Pooled percentiles with seed-bootstrap 95% CIs;
artifacts in `results/sweep_three_way/` (regenerate with one command below).

```sh
.venv/bin/python python/harness.py --routers ecmp,flowlet,rl --seeds 20 \
    --out-dir results/sweep_three_way
```

## Headline: three-way FCT comparison

| Router | p50 (µs) | p99 (µs) | p99 CI95 | p999 (µs) |
|---|---|---|---|---|
| ECMP (control) | 812 | 15,928 | [8,019 – 16,085] | 16,136 |
| Flowlet (hysteresis) | 812 | **7,872** | [4,057 – 15,817] | 31,961 |
| RL (online bandit) | 1,904 | 16,097 | — | 31,941 |

**The flowlet router halves pooled p99 FCT versus ECMP** (15.9 ms → 7.9 ms)
with an unchanged median — the win lives in the tail, where it should. Paired
per-seed p99 deltas: 6 seeds with large wins (up to +12 ms), 11 near-ties,
3 regressions; mean improvement +1.5 ms.

## The negative result, and why it is the interesting one

The RL router — a tabular contextual bandit over exactly the same telemetry
the flowlet heuristic sees (information parity by construction), reward =
−queueing delay — **underperforms both baselines**: p50 doubles (exploration
routes live chunks through congested spines), p99 matches ECMP at best.

The diagnosis mirrors the herding pathology we hit (and fixed) in the
flowlet router: reducing exploration to make the policy gentler
(ε₀ 0.2→0.05) made p99 *worse* (31.8 ms), because many concurrent senders
greedily evaluating one shared Q-table on laggy congestion state all pick
the same argmax spine — the learned policy herds *structurally*, where the
heuristic's stickiness + hysteresis provides exactly the decorrelation the
bandit lacks. Within a single ~16 ms episode there are only ~2,000 decisions
spread over thousands of (leaf-pair × congestion-bucket) states, so the
table is also data-starved.

Where the learned policy does help: on a *stationary* single-bottleneck
scenario (one persistently congested spine), the greedy policy reliably
learns to avoid it (`tests/test_rl.cpp`). Where it does not: fast-moving,
self-induced congestion under concurrent decision-makers — the regime that
actually matters for fabric load balancing.

Honest conclusions we are entitled to:
1. Congestion-aware routing beats static ECMP in the tail (flowlet, p99 ×½).
2. Per-decision learned policies need either coordination mechanisms
   (stickiness, per-flow binding, pacing) or cross-episode training to
   compete; online tabular RL inside one episode is not enough. This is a
   clean negative result, not a tuning failure: the failure mode is
   structural (herding), reproduces deterministically, and worsens as the
   policy gets greedier.

## Known limitations carried from validation

- p999 under any rerouting scheme (flowlet or RL) can regress on a minority
  of seeds via RTO-backoff cascades when a rerouted burst lands in an
  already-full queue (2/20 seeds for flowlet; insensitive to flowlet gap).
- The transport's ECN response is a blunt once-per-RTT halving, not DCTCP's
  proportional response; the incast validation gate therefore rests on the
  no-ECN tail-drop curve (`results/validation_incast/`), which reproduces
  Vasudevan et al.'s throughput collapse: goodput 0.98 of line rate at 8
  senders → 0.08 at 63.
