"""Router-comparison FCT CDF with across-seed error bands."""

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

COLORS = {"ecmp": "tab:gray", "flowlet": "tab:blue", "rl": "tab:red"}


def plot_cdf_comparison(per_router: dict[str, list[np.ndarray]],
                        out_path: Path, title: str = "") -> Path:
    """per_router: router -> list (over seeds) of FCT arrays in ps."""
    all_fct = np.concatenate([np.concatenate(v) for v in per_router.values()])
    grid = np.logspace(np.log10(all_fct.min() * 0.95),
                       np.log10(all_fct.max() * 1.05), 400)

    fig, ax = plt.subplots(figsize=(7.5, 5))
    for router, per_seed in per_router.items():
        # Empirical CDF of each seed evaluated on a common grid; the line is
        # the median across seeds, the band the 10-90 percentile spread.
        cdfs = np.vstack([
            np.searchsorted(np.sort(fct), grid, side="right") / fct.size
            for fct in per_seed
        ])
        med = np.percentile(cdfs, 50, axis=0)
        lo = np.percentile(cdfs, 10, axis=0)
        hi = np.percentile(cdfs, 90, axis=0)
        color = COLORS.get(router)
        ax.plot(grid / 1e6, med, label=f"{router} (n={len(per_seed)} seeds)",
                color=color)
        ax.fill_between(grid / 1e6, lo, hi, alpha=0.25, color=color, lw=0)

    ax.set_xscale("log")
    ax.set_xlabel("Flow completion time (µs)")
    ax.set_ylabel("CDF")
    ax.set_ylim(0, 1.02)
    ax.grid(True, alpha=0.3, which="both")
    ax.legend(loc="lower right")
    if title:
        ax.set_title(title, fontsize=10)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    return out_path
