#!/usr/bin/env python3
"""Phase 2 validation gate: reproduce incast throughput collapse.

Sweeps the number of synchronized senders converging on one receiver over a
fixed bottleneck (the receiver's leaf->host link) and plots goodput — total
delivered bytes over makespan, as a fraction of the bottleneck line rate —
against sender count. The expected shape (cf. Vasudevan et al., SIGCOMM 2009)
is near-line-rate goodput at small sender counts, then collapse as
synchronized windows overflow the shallow buffer and RTO stalls dominate.

An ECN-enabled sweep is also run and recorded in goodput.json, but is NOT
part of the gate figure: this transport's ECN response is a blunt
once-per-RTT halving (not DCTCP's proportional response), which pins windows
at the floor under persistent marking and does not mitigate incast here.
That is recorded as an honest model limitation, not evidence about ECN.

Usage: validate_incast.py [--quick] [--senders 1,2,4,...]
"""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pyarrow.parquet as pq
import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent
RUN_PY = Path(__file__).resolve().parent / "run.py"

BOTTLENECK_GBPS = 100
BYTES_PER_SENDER = 256 * 1024

BASE_CONFIG = {
    "seed": 42,
    "topology": {
        "kind": "clos2",
        "hosts_per_leaf": 16,
        "leaves": 4,
        "spines": 4,
        "host_gbps": BOTTLENECK_GBPS,
        "fabric_gbps": BOTTLENECK_GBPS,
        "prop_us": 1,
    },
    "link": {"buffer_kb": 256},
    "transport": {"window_chunks": 4, "rto_us": 500},
    "router": {"name": "ecmp"},
    "telemetry": {"sample_us": 10},
    "workload": {
        "kind": "incast",
        "receiver": 0,
        "bytes": BYTES_PER_SENDER,
        "chunk_bytes": 16384,  # finer granularity for validation (ADR-001 knob)
    },
}


def run_point(senders: int, ecn: bool, sweep_dir: Path) -> float:
    """Returns goodput as a fraction of the bottleneck line rate."""
    cfg = yaml.safe_load(yaml.safe_dump(BASE_CONFIG))  # deep copy
    cfg["workload"]["senders"] = senders
    # ECN off: threshold == buffer (never marks). ECN on: mark at 64 KiB.
    cfg["link"]["ecn_threshold_kb"] = 64 if ecn else 256
    with tempfile.NamedTemporaryFile(
        "w", suffix=".yaml", dir=sweep_dir, delete=False
    ) as f:
        yaml.safe_dump(cfg, f)
        cfg_path = Path(f.name)

    proc = subprocess.run(
        [
            sys.executable,
            str(RUN_PY),
            str(cfg_path),
            "--results-dir",
            str(sweep_dir),
            "--no-plot",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    run_dir = Path(next(line.split("run dir: ", 1)[1]
                        for line in proc.stdout.splitlines()
                        if line.startswith("run dir: ")))

    flows = pq.read_table(run_dir / "flows.parquet")
    end_ps = flows.column("end_ps").to_numpy()
    assert (end_ps >= 0).all(), f"incomplete flow in {run_dir}"
    total_bits = senders * BYTES_PER_SENDER * 8
    gbps = total_bits * 1000.0 / end_ps.max()
    return float(gbps / BOTTLENECK_GBPS)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quick", action="store_true", help="fewer points (CI)")
    parser.add_argument("--senders", type=str, default=None)
    parser.add_argument(
        "--out-dir", type=Path, default=REPO_ROOT / "results" / "validation_incast"
    )
    args = parser.parse_args()

    if args.senders:
        counts = [int(x) for x in args.senders.split(",")]
    elif args.quick:
        counts = [1, 4, 16, 32]
    else:
        counts = [1, 2, 4, 8, 12, 16, 24, 32, 48, 63]

    args.out_dir.mkdir(parents=True, exist_ok=True)
    sweep_dir = args.out_dir / "runs"
    sweep_dir.mkdir(exist_ok=True)

    curves = {}
    for ecn in (False, True):
        label = "ECN (mark at 64 KiB)" if ecn else "no ECN (tail-drop only)"
        pts = []
        for n in counts:
            g = run_point(n, ecn, sweep_dir)
            pts.append(g)
            print(f"senders={n:3d} ecn={ecn}: goodput = {g:.3f} of line rate")
        curves[label] = pts

    no_ecn = curves["no ECN (tail-drop only)"]
    peak = max(no_ecn)
    tail = no_ecn[-1]
    collapsed = bool(tail < 0.5 * peak)
    print(f"\npeak={peak:.3f} tail={tail:.3f} collapse={'YES' if collapsed else 'NO'}")

    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(counts, no_ecn, marker="o", color="tab:red")
    ax.set_xlabel("Synchronized senders")
    ax.set_ylabel("Goodput (fraction of bottleneck line rate)")
    ax.set_ylim(0, 1.05)
    ax.grid(True, alpha=0.3)
    ax.set_title(
        "Incast throughput collapse — 64-host Clos, 256 KiB buffer,\n"
        f"{BYTES_PER_SENDER // 1024} KiB/sender, RTO 500 µs "
        "(cf. Vasudevan et al., SIGCOMM 2009)"
    )
    fig.tight_layout()
    plot_path = args.out_dir / "goodput_vs_senders.png"
    fig.savefig(plot_path, dpi=150)

    (args.out_dir / "goodput.json").write_text(
        json.dumps({"senders": counts, "curves": curves, "collapse": collapsed},
                   indent=2) + "\n"
    )
    print(f"plot: {plot_path}")
    return 0 if collapsed else 1


if __name__ == "__main__":
    sys.exit(main())
