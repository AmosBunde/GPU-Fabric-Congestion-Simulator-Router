#!/usr/bin/env python3
"""One command = one figure.

Takes a single YAML config and produces one run directory under results/,
stamped with the config hash and git SHA:

    results/<utc-timestamp>_<confighash8>_<gitsha8>/
        config.yaml     exact copy of the input config
        sim.cfg         flattened dotted-key config handed to the C++ binary
        manifest.json   config hash, git SHA, dirty flag, command line
        seeds.txt       master seed + every derived per-component seed
        flows.csv       raw engine telemetry
        flows.parquet   canonical artifact the Python side reads
        fct_cdf.png     flow-completion-time CDF

The C++/Python boundary is these files, never an FFI.
"""

import argparse
import datetime
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent


def flatten(node, prefix=""):
    if isinstance(node, dict):
        out = {}
        for key, value in node.items():
            out.update(flatten(value, f"{prefix}{key}."))
        return out
    return {prefix[:-1]: node}


def config_hash(cfg: dict) -> str:
    canonical = json.dumps(cfg, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode()).hexdigest()


def git_info() -> tuple[str, bool]:
    sha = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, capture_output=True, text=True
    )
    if sha.returncode != 0:
        return "nogit", False
    dirty = subprocess.run(
        ["git", "status", "--porcelain"], cwd=REPO_ROOT, capture_output=True, text=True
    )
    return sha.stdout.strip(), bool(dirty.stdout.strip())


def csv_to_parquet(csv_path: Path, parquet_path: Path) -> None:
    import pyarrow.csv
    import pyarrow.parquet

    table = pyarrow.csv.read_csv(csv_path)
    pyarrow.parquet.write_table(table, parquet_path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path, help="YAML run config")
    parser.add_argument(
        "--binary",
        type=Path,
        default=REPO_ROOT / "build" / "gpufab_sim",
        help="path to the gpufab_sim binary",
    )
    parser.add_argument(
        "--results-dir", type=Path, default=REPO_ROOT / "results"
    )
    parser.add_argument(
        "--no-plot", action="store_true", help="skip figure generation"
    )
    args = parser.parse_args()

    cfg = yaml.safe_load(args.config.read_text())
    chash = config_hash(cfg)
    sha, dirty = git_info()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")

    out_dir = args.results_dir / f"{stamp}_{chash[:8]}_{sha[:8]}"
    out_dir.mkdir(parents=True, exist_ok=False)

    shutil.copy(args.config, out_dir / "config.yaml")
    flat = flatten(cfg)
    (out_dir / "sim.cfg").write_text(
        "".join(f"{k} {v}\n" for k, v in sorted(flat.items()))
    )
    (out_dir / "manifest.json").write_text(
        json.dumps(
            {
                "config_hash": chash,
                "git_sha": sha,
                "git_dirty": dirty,
                "seed": cfg.get("seed"),
                "created_utc": stamp,
                "argv": sys.argv,
            },
            indent=2,
        )
        + "\n"
    )

    if not args.binary.exists():
        print(
            f"error: binary not found at {args.binary}\n"
            "build it first:  cmake -B build -DCMAKE_BUILD_TYPE=Release && "
            "cmake --build build -j",
            file=sys.stderr,
        )
        return 1

    proc = subprocess.run(
        [str(args.binary), str(out_dir / "sim.cfg"), str(out_dir)],
        capture_output=True,
        text=True,
    )
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    if proc.returncode != 0:
        return proc.returncode

    csv_to_parquet(out_dir / "flows.csv", out_dir / "flows.parquet")

    if not args.no_plot:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from plot_fct import plot_fct_cdf

        plot_fct_cdf(out_dir)

    print(f"run dir: {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
