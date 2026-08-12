"""Backtest the OFI signal(s) against short-horizon forward price moves.

Reads the engine's ``ofi.csv`` (per-event best-level ``ofi`` and integrated
``ofi_deep``, plus the mid price), aggregates into fixed-size **event-time
bins**, and for each signal runs two OLS regressions following Cont, Kukanov &
Stoikov (2014):

* **Contemporaneous** — bin price change on same-bin OFI (mechanical impact).
* **Predictive** — *next* bin's price change on this bin's OFI, fit on the first
  ``--train-frac`` of bins (chronological, never shuffled) and evaluated on the
  held-out tail.

It also runs a **transaction-cost-aware PnL**: a position ``sign(â + b̂·OFI)``
from the train fit, charged ``--cost-ticks`` per unit of turnover, so the
reported net PnL reflects whether any edge survives the spread.

The OLS is implemented directly on ``numpy`` — no heavyweight stats dependency.
Writes ``bins.csv`` (for plotting) and ``backtest_summary.json`` (metrics).
"""

from __future__ import annotations

import argparse
import json
import math
from typing import Dict, List

import numpy as np
import pandas as pd

SIGNALS = ["ofi", "ofi_deep"]


def load_bins(ofi_path: str, bin_events: int, signals: List[str]) -> pd.DataFrame:
    """Loads valid OFI observations and aggregates them into event-time bins,
    summing each signal column over the bin."""
    df = pd.read_csv(ofi_path)
    df = df[(df["valid"] == 1) & df["mid"].notna()].reset_index(drop=True)
    if len(df) < 2 * bin_events:
        raise SystemExit(
            f"not enough valid observations ({len(df)}) for bin size {bin_events}"
        )
    present = [s for s in signals if s in df.columns]

    df["bin"] = np.arange(len(df)) // bin_events
    grouped = df.groupby("bin")
    data = {s: grouped[s].sum() for s in present}
    data["mid_start"] = grouped["mid"].first()
    data["mid_end"] = grouped["mid"].last()
    data["timestamp"] = grouped["timestamp"].first()
    bins = pd.DataFrame(data)

    counts = grouped.size()
    bins = bins[counts == bin_events].reset_index(drop=True)  # equal-weight bins
    bins["ret"] = bins["mid_end"] - bins["mid_start"]
    bins["fwd_ret"] = bins["ret"].shift(-1)
    return bins


def _fit(y: np.ndarray, x: np.ndarray) -> Dict[str, float]:
    """OLS of y on [1, x] with homoskedastic SEs and a normal-approx p-value."""
    design = np.column_stack([np.ones_like(x), x])
    coef, _, _, _ = np.linalg.lstsq(design, y, rcond=None)
    resid = y - design @ coef
    n, k = design.shape
    ss_res = float(resid @ resid)
    ss_tot = float(((y - y.mean()) ** 2).sum())
    sigma2 = ss_res / (n - k)
    cov = sigma2 * np.linalg.inv(design.T @ design)
    se = math.sqrt(cov[1, 1])
    t_stat = coef[1] / se if se > 0 else float("nan")
    return {
        "n": int(n),
        "intercept": float(coef[0]),
        "beta": float(coef[1]),
        "t_stat": float(t_stat),
        "p_value": float(math.erfc(abs(t_stat) / math.sqrt(2.0))),
        "r_squared": 1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan"),
    }


def _oos_r2(y_true: np.ndarray, y_pred: np.ndarray) -> float:
    ss_res = float(np.sum((y_true - y_pred) ** 2))
    ss_tot = float(np.sum((y_true - y_true.mean()) ** 2))
    return 1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan")


def _pnl(
    signal: np.ndarray,
    fwd: np.ndarray,
    a: float,
    b: float,
    cost_ticks: float,
    split: int,
) -> Dict[str, float]:
    """Cost-aware PnL of trading position = sign(a + b*signal). Turnover is the
    absolute position change; each unit is charged ``cost_ticks``."""
    position = np.sign(a + b * signal)
    turnover = np.abs(np.diff(position, prepend=0.0))
    gross = position * fwd
    net = gross - cost_ticks * turnover
    test = slice(split, None)
    return {
        "cost_ticks": cost_ticks,
        "full_gross_ticks": float(gross.sum()),
        "full_net_ticks": float(net.sum()),
        "test_gross_ticks": float(gross[test].sum()),
        "test_net_ticks": float(net[test].sum()),
        "test_turnover": float(turnover[test].sum()),
    }


def run(
    ofi_path: str, bin_events: int, train_frac: float, cost_ticks: float
) -> Dict[str, object]:
    bins = load_bins(ofi_path, bin_events, SIGNALS)
    usable = bins.dropna(subset=["fwd_ret"]).reset_index(drop=True)
    split = int(len(usable) * train_frac)
    present = [s for s in SIGNALS if s in usable.columns]

    signals: Dict[str, object] = {}
    for sig in present:
        x = usable[sig].to_numpy(dtype=float)
        ret = usable["ret"].to_numpy(dtype=float)
        fwd = usable["fwd_ret"].to_numpy(dtype=float)

        contemporaneous = _fit(ret, x)
        predictive_is = _fit(fwd[:split], x[:split])
        oos_pred = predictive_is["intercept"] + predictive_is["beta"] * x[split:]
        predictive_oos_r2 = _oos_r2(fwd[split:], oos_pred)
        pnl = _pnl(
            x, fwd, predictive_is["intercept"], predictive_is["beta"], cost_ticks, split
        )

        signals[sig] = {
            "contemporaneous": contemporaneous,
            "predictive_in_sample": predictive_is,
            "predictive_oos_r_squared": predictive_oos_r2,
            "pnl": pnl,
        }

    return {
        "params": {
            "bin_events": bin_events,
            "train_frac": train_frac,
            "cost_ticks": cost_ticks,
            "n_bins": int(len(bins)),
            "n_usable_bins": int(len(usable)),
            "split_index": split,
        },
        "signals": signals,
        "bins": usable,  # popped before JSON
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ofi", default="data/out/ofi.csv", help="engine ofi.csv")
    parser.add_argument("--bin-events", type=int, default=50)
    parser.add_argument("--train-frac", type=float, default=0.7)
    parser.add_argument(
        "--cost-ticks",
        type=float,
        default=0.5,
        help="transaction cost per unit of position turnover",
    )
    parser.add_argument("--out-bins", default="data/out/bins.csv")
    parser.add_argument("--out-summary", default="data/out/backtest_summary.json")
    args = parser.parse_args()

    result = run(args.ofi, args.bin_events, args.train_frac, args.cost_ticks)
    bins = result.pop("bins")
    bins.to_csv(args.out_bins, index=False)
    with open(args.out_summary, "w") as f:
        json.dump(result, f, indent=2)

    p = result["params"]
    print(
        f"Bins: {p['n_usable_bins']} usable (size {p['bin_events']} events, "
        f"train_frac {p['train_frac']}, cost {p['cost_ticks']} ticks/turn)\n"
    )
    header = (
        f"{'signal':>9} | {'contemp R²':>10} | {'pred OOS R²':>11} | "
        f"{'test gross':>10} | {'test net':>9}"
    )
    print(header)
    print("-" * len(header))
    for sig, m in result["signals"].items():
        print(
            f"{sig:>9} | {m['contemporaneous']['r_squared']:>10.4f} | "
            f"{m['predictive_oos_r_squared']:>11.4f} | "
            f"{m['pnl']['test_gross_ticks']:>10.1f} | "
            f"{m['pnl']['test_net_ticks']:>9.1f}"
        )
    print(f"\nWrote {args.out_bins} and {args.out_summary}")


if __name__ == "__main__":
    main()
