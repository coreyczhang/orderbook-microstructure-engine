"""Analyze the market-making simulator's output.

Reads the C++ simulator's ``fills.csv`` and ``pnl.csv`` (written by
``simulator/mm_sim``) and reports what the quoting strategy earned, how its
inventory evolved, and how badly it was adversely selected — the classic
market-making scorecard.

Metrics
-------
* **PnL / equity**: mark-to-market equity (cash + inventory x mid) over time.
* **Inventory**: signed position over time (risk the maker is carrying).
* **Adverse selection (markout)**: for each fill, the mid is sampled again a
  fixed horizon later; ``markout`` is the maker-favorable move (+ is good). A
  negative average means the maker tends to get filled right before the mid
  moves against it — the defining cost of passive market making.

Figures: ``mm_equity.png`` (equity + inventory) and ``mm_markout.png`` (markout
distribution). Uses only numpy/pandas/matplotlib.
"""

from __future__ import annotations

import argparse
import json
import os
from typing import Dict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402


def summarize(fills: pd.DataFrame, pnl: pd.DataFrame) -> Dict[str, object]:
    resolved = fills[fills["markout"].notna()]
    buys = fills[fills["side"] == "B"]
    sells = fills[fills["side"] == "S"]
    final = pnl.dropna(subset=["equity"]).tail(1)

    def mean_markout(df: pd.DataFrame) -> float:
        return float(df["markout"].mean()) if len(df) else float("nan")

    return {
        "n_fills": int(len(fills)),
        "buy_qty": int(buys["qty"].sum()),
        "sell_qty": int(sells["qty"].sum()),
        "final_inventory": (int(final["inventory"].iloc[0]) if len(final) else 0),
        "final_equity_ticks": (float(final["equity"].iloc[0]) if len(final) else 0.0),
        "mean_markout_ticks": mean_markout(resolved),
        "mean_markout_buy": mean_markout(buys[buys["markout"].notna()]),
        "mean_markout_sell": mean_markout(sells[sells["markout"].notna()]),
        "markout_resolved": int(len(resolved)),
    }


def make_figures(fills: pd.DataFrame, pnl: pd.DataFrame, out_dir: str) -> None:
    os.makedirs(out_dir, exist_ok=True)
    p = pnl.dropna(subset=["mid"]).copy()
    p["t_s"] = (p["time"] - p["time"].iloc[0]) / 1e9  # ns -> seconds

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
    ax1.plot(p["t_s"], p["equity"], lw=1.2, color="tab:blue")
    ax1.axhline(0, color="0.7", lw=0.6)
    ax1.set_title("Market-making simulation — mark-to-market equity")
    ax1.set_ylabel("equity (ticks)")
    ax2.plot(p["t_s"], p["inventory"], lw=1.0, color="tab:purple")
    ax2.axhline(0, color="0.7", lw=0.6)
    ax2.set_title("Inventory (position carried)")
    ax2.set_xlabel("time (s)")
    ax2.set_ylabel("inventory")
    fig.tight_layout()
    fig.savefig(f"{out_dir}/mm_equity.png", dpi=120)
    plt.close(fig)

    resolved = fills[fills["markout"].notna()]
    fig, ax = plt.subplots(figsize=(8, 4.5))
    if len(resolved):
        ax.hist(resolved["markout"], bins=41, color="tab:green", alpha=0.8)
        mean = float(resolved["markout"].mean())
        ax.axvline(mean, color="crimson", lw=1.5, label=f"mean = {mean:.3f}")
        ax.axvline(0, color="0.5", lw=0.8)
        ax.legend()
    ax.set_title("Adverse-selection markout per fill (+ favorable to the maker)")
    ax.set_xlabel("markout (ticks)")
    ax.set_ylabel("fills")
    fig.tight_layout()
    fig.savefig(f"{out_dir}/mm_markout.png", dpi=120)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fills", default="data/mm_out/fills.csv")
    parser.add_argument("--pnl", default="data/mm_out/pnl.csv")
    parser.add_argument("--out-dir", default="docs")
    parser.add_argument("--out-summary", default="data/mm_out/mm_summary.json")
    args = parser.parse_args()

    fills = pd.read_csv(args.fills)
    pnl = pd.read_csv(args.pnl)
    summary = summarize(fills, pnl)
    make_figures(fills, pnl, args.out_dir)
    with open(args.out_summary, "w") as f:
        json.dump(summary, f, indent=2)

    print(
        f"Fills: {summary['n_fills']} "
        f"(buy {summary['buy_qty']}, sell {summary['sell_qty']})"
    )
    print(
        f"Final inventory: {summary['final_inventory']}  "
        f"equity: {summary['final_equity_ticks']:.1f} ticks"
    )
    print(
        f"Mean markout: {summary['mean_markout_ticks']:.4f} ticks "
        f"(buy {summary['mean_markout_buy']:.4f}, "
        f"sell {summary['mean_markout_sell']:.4f}) over "
        f"{summary['markout_resolved']} resolved fills"
    )
    print(
        f"Wrote {args.out_dir}/mm_equity.png, {args.out_dir}/mm_markout.png, "
        f"and {args.out_summary}"
    )


if __name__ == "__main__":
    main()
