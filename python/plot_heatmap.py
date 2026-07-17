#!/usr/bin/env python3
"""Link-utilization heatmap (time x fabric link) from a run's links.parquet.

Rows are the fabric (leaf<->spine) links — the contended tier where routing
decisions matter; host links are omitted for legibility. Color is the
per-sample utilization fraction of line rate.

Usage: plot_heatmap.py <run_dir> [--out <png>] [--all-links]
"""

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pyarrow.parquet as pq


def plot_heatmap(run_dir: Path, out: Path | None = None,
                 fabric_only: bool = True) -> Path:
    table = pq.read_table(run_dir / "links.parquet").to_pandas()
    if fabric_only:
        # Fabric links connect two switches: host links have a host endpoint,
        # and hosts are the lowest-numbered nodes. A link is fabric iff both
        # endpoints are above every host id — infer the host count as the
        # smallest endpoint ever seen on a link with a switch (max-degree
        # heuristic is overkill; the min node id of any src in the leaf tier
        # equals num_hosts). Robust approach: host links appear exactly twice
        # per host (in/out); fabric endpoints are nodes that never appear as
        # the low end. Simplest reliable filter: keep links whose src and dst
        # are both >= the minimum id that appears as a leaf, i.e. nodes that
        # appear on >= 3 distinct links.
        deg = {}
        for col in ("src", "dst"):
            for n, c in table.groupby(col)["link_id"].nunique().items():
                deg[n] = deg.get(n, 0) + c
        switches = {n for n, c in deg.items() if c > 2}
        table = table[table["src"].isin(switches) & table["dst"].isin(switches)]

    links = sorted(table["link_id"].unique())
    times = np.sort(table["time_ps"].unique())
    grid = np.zeros((len(links), len(times)))
    link_pos = {l: i for i, l in enumerate(links)}
    time_pos = {t: i for i, t in enumerate(times)}
    labels = {}
    for row in table.itertuples():
        grid[link_pos[row.link_id], time_pos[row.time_ps]] = row.utilization
        labels[row.link_id] = f"{row.src}→{row.dst}"

    fig, ax = plt.subplots(figsize=(10, max(4, len(links) * 0.14)))
    im = ax.imshow(
        grid, aspect="auto", origin="lower", cmap="inferno", vmin=0,
        vmax=max(1.0, grid.max()),
        extent=(times[0] / 1e6, times[-1] / 1e6, -0.5, len(links) - 0.5),
    )
    ax.set_xlabel("Virtual time (µs)")
    ax.set_ylabel("Fabric link" if fabric_only else "Link")
    step = max(1, len(links) // 32)
    ax.set_yticks(range(0, len(links), step))
    ax.set_yticklabels([labels[links[i]] for i in range(0, len(links), step)],
                       fontsize=6)
    fig.colorbar(im, ax=ax, label="Utilization (fraction of line rate)")
    ax.set_title(f"Link utilization — {run_dir.name}", fontsize=10)
    fig.tight_layout()
    out = out or (run_dir / "link_heatmap.png")
    fig.savefig(out, dpi=150)
    plt.close(fig)
    return out


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument("--all-links", action="store_true")
    args = parser.parse_args()
    print(plot_heatmap(args.run_dir, args.out, fabric_only=not args.all_links))
