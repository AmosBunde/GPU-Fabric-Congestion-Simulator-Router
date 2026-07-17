#!/usr/bin/env python3
"""Evaluation harness: seeded router sweeps with paired comparison.

Runs N seeded repetitions per router on the same topology and workload.
Seeds are PAIRED: seed k produces the identical permutation workload for
every router (the workload draws from its own named RNG stream), so router
deltas are attributable to routing, not traffic luck. Emits:

    <out>/metrics.json     p50/p99/p999 FCT per router with bootstrap CIs,
                           plus paired per-seed p99 deltas vs the first router
    <out>/comparison.png   FCT CDF overlay, median across seeds with a
                           10-90 percentile band
    <out>/runs/            one hash-stamped run directory per (router, seed)

Workload: uniform permutation of 2 MiB elephants on a 64-host, 4:1
oversubscribed 2-tier Clos (16 hosts/leaf, 4 leaves, 4 spines) — the regime
where ECMP hash collisions on fabric uplinks dominate tail FCT.
"""

import argparse
import concurrent.futures
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import pyarrow.parquet as pq
import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent
RUN_PY = Path(__file__).resolve().parent / "run.py"

BASE_CONFIG = {
    "topology": {
        "kind": "clos2",
        "hosts_per_leaf": 16,
        "leaves": 4,
        "spines": 4,
        "host_gbps": 100,
        "fabric_gbps": 100,  # 4:1 oversubscription at the leaf
        "prop_us": 1,
    },
    "link": {"buffer_kb": 512, "ecn_threshold_kb": 128},
    "transport": {"window_chunks": 8, "rto_us": 500},
    "telemetry": {"sample_us": 10},
    "workload": {
        "kind": "permutation",
        "bytes": 2 * 1024 * 1024,
        "chunk_bytes": 65536,
    },
}


def run_one(router: str, seed: int, extra: dict, runs_dir: Path) -> np.ndarray:
    cfg = yaml.safe_load(yaml.safe_dump(BASE_CONFIG))
    cfg.update(yaml.safe_load(yaml.safe_dump(extra)) if extra else {})
    cfg["seed"] = seed
    cfg["router"] = {**cfg.get("router", {}), "name": router}
    with tempfile.NamedTemporaryFile(
        "w", suffix=f"_{router}_{seed}.yaml", dir=runs_dir, delete=False
    ) as f:
        yaml.safe_dump(cfg, f)
        cfg_path = Path(f.name)
    proc = subprocess.run(
        [sys.executable, str(RUN_PY), str(cfg_path), "--results-dir",
         str(runs_dir), "--no-plot"],
        check=True, capture_output=True, text=True,
    )
    run_dir = Path(next(line.split("run dir: ", 1)[1]
                        for line in proc.stdout.splitlines()
                        if line.startswith("run dir: ")))
    fct = pq.read_table(run_dir / "flows.parquet").column("fct_ps").to_numpy()
    assert (fct >= 0).all(), f"incomplete flow in {run_dir}"
    return fct


def bootstrap_percentile_ci(per_seed: list[np.ndarray], q: float,
                            reps: int = 2000, seed: int = 7):
    """CI for the pooled q-th percentile, bootstrapping over seeds."""
    rng = np.random.default_rng(seed)
    n = len(per_seed)
    stats = []
    for _ in range(reps):
        idx = rng.integers(0, n, n)
        pooled = np.concatenate([per_seed[i] for i in idx])
        stats.append(np.percentile(pooled, q))
    return float(np.percentile(stats, 2.5)), float(np.percentile(stats, 97.5))


def summarize(per_seed: list[np.ndarray]) -> dict:
    pooled = np.concatenate(per_seed)
    out = {"flows": int(pooled.size)}
    for label, q in [("p50", 50), ("p99", 99), ("p999", 99.9)]:
        lo, hi = bootstrap_percentile_ci(per_seed, q)
        out[label] = {
            "us": float(np.percentile(pooled, q) / 1e6),
            "ci95_us": [lo / 1e6, hi / 1e6],
        }
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--routers", default="ecmp,flowlet")
    parser.add_argument("--seeds", type=int, default=20)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--out-dir", type=Path,
                        default=REPO_ROOT / "results" / "sweep")
    parser.add_argument("--config-overrides", type=str, default=None,
                        help="YAML string merged over the base config")
    args = parser.parse_args()

    routers = args.routers.split(",")
    extra = yaml.safe_load(args.config_overrides) if args.config_overrides else {}
    args.out_dir.mkdir(parents=True, exist_ok=True)
    runs_dir = args.out_dir / "runs"
    runs_dir.mkdir(exist_ok=True)

    results: dict[str, list[np.ndarray]] = {r: [None] * args.seeds
                                            for r in routers}
    jobs = [(r, s) for r in routers for s in range(args.seeds)]
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futs = {pool.submit(run_one, r, 1000 + s, extra, runs_dir): (r, s)
                for r, s in jobs}
        for fut in concurrent.futures.as_completed(futs):
            r, s = futs[fut]
            results[r][s] = fut.result()
            print(f"done: router={r} seed={1000 + s}")

    metrics = {r: summarize(results[r]) for r in routers}

    # Paired per-seed p99 delta vs the first (control) router.
    control = routers[0]
    for r in routers[1:]:
        deltas = [
            float((np.percentile(results[control][s], 99)
                   - np.percentile(results[r][s], 99)) / 1e6)
            for s in range(args.seeds)
        ]
        metrics[r]["paired_p99_improvement_vs_" + control] = {
            "mean_us": float(np.mean(deltas)),
            "per_seed_us": deltas,
            "wins": int(sum(d > 0 for d in deltas)),
            "seeds": args.seeds,
        }

    (args.out_dir / "metrics.json").write_text(
        json.dumps(metrics, indent=2) + "\n")

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from plot_compare import plot_cdf_comparison

    plot_cdf_comparison(
        {r: results[r] for r in routers},
        args.out_dir / "comparison.png",
        title=(f"Permutation FCT CDF — {args.seeds} paired seeds, 64-host "
               "Clos (4:1 oversubscribed), 2 MiB flows"),
    )

    for r in routers:
        m = metrics[r]
        print(f"{r:>8}: p50={m['p50']['us']:8.1f}us  "
              f"p99={m['p99']['us']:8.1f}us  p999={m['p999']['us']:8.1f}us")
    print(f"artifacts: {args.out_dir}/metrics.json, comparison.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
