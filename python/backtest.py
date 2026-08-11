"""Backtest the OFI signal against short-horizon forward price moves.

Reads the engine's ``ofi.csv`` (per-event Order Flow Imbalance and mid price),
aggregates it into fixed-size **event-time bins**, and runs two ordinary least
squares regressions following Cont, Kukanov & Stoikov (2014):

* **Contemporaneous** — bin price change on same-bin OFI. Expected to be strong;
  it measures mechanical price *impact*, replicating the CKS result.
* **Predictive** — *next* bin's price change on this bin's OFI. This is the
  honest out-of-sample question: does OFI forecast the future, or only explain
  the present? Fit on the first ``--train-frac`` of bins (chronological, never
  shuffled) and evaluated on the held-out tail to avoid look-ahead bias.

Writes ``bins.csv`` (for plotting) and ``backtest_summary.json`` (metrics).

The regression is a plain OLS implemented on top of ``numpy`` (coefficients via
least squares, heteroskedasticity-naive standard errors, R², and a large-sample
two-sided p-value from the normal approximation) — no heavyweight stats
dependency, so the analysis runs anywhere ``numpy``/``pandas`` do.
"""

from __future__ import annotations

import argparse
import json
import math
from typing import Dict

import numpy as np
import pandas as pd


def load_bins(ofi_path: str, bin_events: int) -> pd.DataFrame:
    """Loads valid OFI observations and aggregates them into event-time bins."""
    df = pd.read_csv(ofi_path)
    df = df[(df["valid"] == 1) & df["mid"].notna()].reset_index(drop=True)
    if len(df) < 2 * bin_events:
        raise SystemExit(
            f"not enough valid observations ({len(df)}) for bin size {bin_events}"
        )

    df["bin"] = np.arange(len(df)) // bin_events
    grouped = df.groupby("bin")
    bins = pd.DataFrame(
        {
            "ofi": grouped["ofi"].sum(),
            "mid_start": grouped["mid"].first(),
            "mid_end": grouped["mid"].last(),
            "timestamp": grouped["timestamp"].first(),
        }
    )
    # Drop a possibly-partial final bin so every bin has equal weight.
    counts = grouped.size()
    bins = bins[counts == bin_events].reset_index(drop=True)

    bins["ret"] = bins["mid_end"] - bins["mid_start"]  # contemporaneous move
    bins["fwd_ret"] = bins["ret"].shift(-1)  # next bin's move
    return bins


def _fit(y: np.ndarray, x: np.ndarray) -> Dict[str, float]:
    """OLS of y on [1, x]. Returns slope, its t-stat/p-value, intercept and R².

    Standard errors are the usual homoskedastic OLS errors; the p-value uses the
    normal approximation to the t distribution, which is essentially exact at the
    thousands-of-observations sample sizes here.
    """
    design = np.column_stack([np.ones_like(x), x])
    coef, _, _, _ = np.linalg.lstsq(design, y, rcond=None)
    resid = y - design @ coef
    n, k = design.shape
    dof = n - k
    ss_res = float(resid @ resid)
    ss_tot = float(((y - y.mean()) ** 2).sum())
    sigma2 = ss_res / dof
    cov = sigma2 * np.linalg.inv(design.T @ design)
    se_slope = math.sqrt(cov[1, 1])
    t_stat = coef[1] / se_slope if se_slope > 0 else float("nan")
    p_value = math.erfc(abs(t_stat) / math.sqrt(2.0))  # two-sided, normal approx
    return {
        "n": int(n),
        "intercept": float(coef[0]),
        "beta": float(coef[1]),
        "t_stat": float(t_stat),
        "p_value": float(p_value),
        "r_squared": 1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan"),
    }


def _ols(y: pd.Series, x: pd.Series) -> Dict[str, float]:
    return _fit(y.to_numpy(dtype=float), x.to_numpy(dtype=float))


def _oos_r2(y_true: np.ndarray, y_pred: np.ndarray) -> float:
    ss_res = float(np.sum((y_true - y_pred) ** 2))
    ss_tot = float(np.sum((y_true - y_true.mean()) ** 2))
    return 1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan")


def run(ofi_path: str, bin_events: int, train_frac: float) -> Dict[str, object]:
    bins = load_bins(ofi_path, bin_events)
    usable = bins.dropna(subset=["fwd_ret"]).reset_index(drop=True)

    # Contemporaneous impact regression on the full sample.
    contemporaneous = _ols(usable["ret"], usable["ofi"])

    # Predictive regression with a chronological (time-ordered) train/test split.
    split = int(len(usable) * train_frac)
    train, test = usable.iloc[:split], usable.iloc[split:]

    predictive_is = _ols(train["fwd_ret"], train["ofi"])
    # Apply the train-fitted line to the held-out tail (no re-fitting).
    oos_pred = predictive_is["intercept"] + predictive_is["beta"] * test[
        "ofi"
    ].to_numpy(dtype=float)
    predictive_oos_r2 = _oos_r2(test["fwd_ret"].to_numpy(dtype=float), oos_pred)

    return {
        "params": {
            "bin_events": bin_events,
            "train_frac": train_frac,
            "n_bins": int(len(bins)),
            "n_usable_bins": int(len(usable)),
        },
        "contemporaneous": contemporaneous,
        "predictive_in_sample": predictive_is,
        "predictive_oos_r_squared": predictive_oos_r2,
        "bins": usable,  # returned for writing; popped before JSON
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ofi", default="data/out/ofi.csv", help="engine ofi.csv")
    parser.add_argument("--bin-events", type=int, default=50)
    parser.add_argument("--train-frac", type=float, default=0.7)
    parser.add_argument("--out-bins", default="data/out/bins.csv")
    parser.add_argument("--out-summary", default="data/out/backtest_summary.json")
    args = parser.parse_args()

    result = run(args.ofi, args.bin_events, args.train_frac)
    bins = result.pop("bins")
    bins.to_csv(args.out_bins, index=False)
    with open(args.out_summary, "w") as f:
        json.dump(result, f, indent=2)

    c = result["contemporaneous"]
    pis = result["predictive_in_sample"]
    print(
        f"Bins: {result['params']['n_usable_bins']} usable "
        f"(size {args.bin_events} events, train_frac {args.train_frac})\n"
    )
    print("Contemporaneous   ret_t ~ OFI_t")
    print(f"  beta={c['beta']:.4e}  t={c['t_stat']:.1f}  R^2={c['r_squared']:.4f}\n")
    print("Predictive        ret_(t+1) ~ OFI_t")
    print(
        f"  in-sample  beta={pis['beta']:.4e}  t={pis['t_stat']:.2f}  "
        f"R^2={pis['r_squared']:.4f}"
    )
    print(f"  out-of-sample R^2={result['predictive_oos_r_squared']:.4f}")
    print(f"\nWrote {args.out_bins} and {args.out_summary}")


if __name__ == "__main__":
    main()
