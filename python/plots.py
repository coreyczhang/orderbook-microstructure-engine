"""Visualize the OFI backtest.

Produces two figures from ``bins.csv`` and ``backtest_summary.json``:

1. ``ofi_scatter.png`` — OFI vs. contemporaneous return (left) and OFI vs. the
   *next* bin's return (right) for the best-level signal, each with the fitted
   OLS line and R². Shows that the contemporaneous relationship is real while
   the predictive one is weak.
2. ``pnl.png`` — cumulative PnL of trading each OFI signal (best-level and
   deep), **gross and net of transaction costs**, versus a buy-and-hold
   baseline, with the train/test boundary marked.
"""

from __future__ import annotations

import argparse
import json
import os

import matplotlib

matplotlib.use("Agg")  # headless / file output
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402


def _scatter(
    ax: plt.Axes, x: np.ndarray, y: np.ndarray, title: str, ylabel: str
) -> None:
    ax.scatter(x, y, s=6, alpha=0.25, edgecolors="none")
    slope, intercept = np.polyfit(x, y, 1)
    xs = np.array([x.min(), x.max()])
    ax.plot(xs, intercept + slope * xs, color="crimson", lw=1.5)
    ss_res = np.sum((y - (intercept + slope * x)) ** 2)
    ss_tot = np.sum((y - y.mean()) ** 2)
    r2 = 1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan")
    ax.set_title(f"{title}\nslope={slope:.2e}, R²={r2:.3f}")
    ax.set_xlabel("OFI over bin (summed)")
    ax.set_ylabel(ylabel)
    ax.axhline(0, color="0.7", lw=0.6)
    ax.axvline(0, color="0.7", lw=0.6)


def make_scatter(bins: pd.DataFrame, out_path: str, signal: str) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))
    _scatter(
        axes[0],
        bins[signal].to_numpy(dtype=float),
        bins["ret"].to_numpy(dtype=float),
        f"Contemporaneous: ret_t vs {signal}_t",
        "same-bin mid change (ticks)",
    )
    _scatter(
        axes[1],
        bins[signal].to_numpy(dtype=float),
        bins["fwd_ret"].to_numpy(dtype=float),
        f"Predictive: ret_(t+1) vs {signal}_t",
        "next-bin mid change (ticks)",
    )
    fig.tight_layout()
    fig.savefig(out_path, dpi=120)
    plt.close(fig)


def _hysteresis_positions(pred: np.ndarray, band: float) -> np.ndarray:
    """Mirror of backtest.positions: long above +band, short below -band, else
    hold. Kept in sync so the plotted PnL matches the reported metrics."""
    pos = np.zeros(len(pred))
    cur = 0.0
    for i in range(len(pred)):
        p = pred[i]
        if p > band:
            cur = 1.0
        elif p < -band:
            cur = -1.0
        pos[i] = cur
    return pos


def _cum(pos: np.ndarray, fwd: np.ndarray, cost: float) -> np.ndarray:
    turnover = np.abs(np.diff(pos, prepend=0.0))
    return np.cumsum(pos * fwd - cost * turnover)


def make_pnl(bins: pd.DataFrame, summary: dict, out_path: str) -> None:
    cost = summary["params"]["cost_ticks"]
    split = summary["params"]["split_index"]
    fwd = bins["fwd_ret"].to_numpy(dtype=float)

    fig, ax = plt.subplots(figsize=(9, 4.8))
    ax.plot(np.cumsum(fwd), label="buy & hold", lw=1.4, color="0.5")

    colors = {"ofi": "tab:blue", "ofi_deep": "tab:green"}
    for sig, metrics in summary["signals"].items():
        if sig not in bins.columns:
            continue
        a = metrics["predictive_in_sample"]["intercept"]
        b = metrics["predictive_in_sample"]["beta"]
        pred = a + b * bins[sig].to_numpy(dtype=float)
        c = colors.get(sig, None)
        # Flip-every-bin net (band 0) vs. the train-tuned dead-band net.
        net_flip = _cum(_hysteresis_positions(pred, 0.0), fwd, cost)
        band = metrics["pnl_deadband"]["band"]
        ks = metrics["pnl_deadband"]["band_sigmas"]
        net_band = _cum(_hysteresis_positions(pred, band), fwd, cost)
        ax.plot(net_flip, label=f"{sig} net, flip", lw=1.2, color=c, alpha=0.45)
        ax.plot(net_band, label=f"{sig} net, band={ks:g}σ", lw=1.8, color=c)

    ax.axvline(split, color="crimson", ls="--", lw=1, label="train/test split")
    ax.set_title(
        f"Cumulative net PnL (ticks, cost={cost}) — flip-every-bin vs. dead-band"
    )
    ax.set_xlabel("bin index (time order)")
    ax.set_ylabel("cumulative net PnL (ticks)")
    ax.legend(loc="best", fontsize=8, ncol=2)
    fig.tight_layout()
    fig.savefig(out_path, dpi=120)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bins", default="data/out/bins.csv")
    parser.add_argument("--summary", default="data/out/backtest_summary.json")
    parser.add_argument("--out-dir", default="docs")
    parser.add_argument(
        "--scatter-signal",
        default="ofi",
        help="signal column to show in the scatter (default: ofi)",
    )
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    bins = pd.read_csv(args.bins)
    with open(args.summary) as f:
        summary = json.load(f)

    scatter_path = f"{args.out_dir}/ofi_scatter.png"
    pnl_path = f"{args.out_dir}/pnl.png"
    make_scatter(bins, scatter_path, args.scatter_signal)
    make_pnl(bins, summary, pnl_path)
    print(f"Wrote {scatter_path} and {pnl_path}")


if __name__ == "__main__":
    main()
