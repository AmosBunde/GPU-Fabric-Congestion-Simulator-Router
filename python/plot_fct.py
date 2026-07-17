"""Flow-completion-time CDF from a run directory's flows.parquet."""

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pyarrow.parquet as pq


def plot_fct_cdf(run_dir: Path) -> Path:
    table = pq.read_table(run_dir / "flows.parquet")
    fct_us = np.sort(table.column("fct_ps").to_numpy()) / 1e6
    cdf = np.arange(1, len(fct_us) + 1) / len(fct_us)

    fig, ax = plt.subplots(figsize=(6, 4))
    ax.step(fct_us, cdf, where="post")
    ax.set_xlabel("Flow completion time (µs)")
    ax.set_ylabel("CDF")
    ax.set_ylim(0, 1.02)
    ax.grid(True, alpha=0.3)
    ax.set_title(f"FCT CDF — {len(fct_us)} flow(s)")
    fig.tight_layout()

    out = run_dir / "fct_cdf.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    return out


if __name__ == "__main__":
    import sys

    print(plot_fct_cdf(Path(sys.argv[1])))
