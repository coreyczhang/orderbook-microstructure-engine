# OFI Backtest Results

**Data:** synthetic order-flow stream (`--seed 42`, 50,000 events → 17,995 trades),
reconstructed by the C++ engine. Two OFI signals are computed per event with the
Cont–Kukanov–Stoikov formulation ([`OrderFlowImbalance`](../include/obme/OrderFlowImbalance.hpp)):

- **`ofi`** — best-level (L1) OFI.
- **`ofi_deep`** — integrated OFI over the top **5** price levels.

Observations are aggregated into **event-time bins of 50 events** (998 usable bins);
returns are mid-price changes in ticks. The train/test split is **chronological** — first
70% train (698 bins), last 30% test (300 bins) — never shuffled. The trading PnL charges
**0.5 tick per unit of position turnover**.

## Regressions

| Signal | Contemporaneous `ret_t ~ OFI_t` | | Predictive `ret_{t+1} ~ OFI_t` | | |
|--------|--------:|-----:|--------:|-----:|-----:|
| | β | R² | β | t | OOS R² |
| `ofi` (L1)   | +4.76e-3 | **0.085** | −2.36e-3 | −4.0 | 0.030 |
| `ofi_deep` (top-5) | +3.11e-5 | **0.531** | −1.51e-5 | −7.3 | 0.057 |

![OFI vs returns (L1)](ofi_scatter.png)

**What the depth extension buys us.** Integrating OFI across the top 5 levels lifts the
**contemporaneous** R² from 0.085 to **0.531** (t rises from 9.6 to 33.6) — depth beyond
the touch carries most of the price-impact information, consistent with the multi-level OFI
literature (Cont, Cucuringu & Xu, 2023). Predictive OOS R² also roughly doubles (0.030 →
0.057) but stays small.

**The predictive sign is negative for both.** This bin's OFI slightly *negatively*
forecasts next bin's move — impact is largely **transient** and partially mean-reverts (a
bounce-like artifact of the zero-intelligence generator). Significance (t = −4 to −7) comes
from ~1000 bins, not economic magnitude.

## Signal PnL vs. baseline — gross and net of costs

![Cumulative PnL](pnl.png)

Position is `sign(â + b̂·OFI_t)` from the **train** fit (a contrarian rule, since β < 0),
applied throughout. Out-of-sample (test-set) totals, in ticks:

| Signal | test gross | test net (0.5-tick cost) | test turnover |
|--------|-----------:|-------------------------:|--------------:|
| `ofi` (L1)   | +20.5 | **−123.5** | 288 |
| `ofi_deep` (top-5) | +42.5 | **−82.5** | 250 |

**The edge does not survive transaction costs.** Both signals are modestly profitable
*gross* (deep more so), but the strategy flips position almost every bin (~250–290 turns
over 300 test bins), and at 0.5 tick per turn the costs swamp the tiny per-bin edge — net
PnL is **firmly negative** for both. Deep OFI loses less (higher gross, slightly lower
turnover), but neither is tradable as-is.

## Honest caveats

1. **Frictionless synthetic, mechanical data.** The exploited reversion is a property of
   the generator, not evidence it exists (or has this sign) in real markets.
2. **Costs applied, but naively.** A real strategy would throttle turnover, use a
   dead-band, or hold longer — this is a deliberately simple sign rule to expose the
   cost sensitivity, not an optimized system.
3. **Single asset, single seed, ~50 s of simulated time.** A pipeline demonstration, not a
   robust alpha study.
4. **OFI is a signal, not a strategy.** Strong contemporaneous impact ≠ tradable forecast.

## Takeaway

The engine reconstructs the book exactly (validated to the tick against ground truth), and
the OFI signals behave sensibly and informatively: **multi-level OFI dramatically improves
contemporaneous explanatory power (R² 0.53), predictive power is weak and transient, and a
naive signal-following strategy loses money after realistic costs.** That is the honest,
expected outcome on frictionless synthetic data — reported as measured, not tuned.

## Reproduce

```bash
python python/generate_synthetic.py --out data/events.csv \
    --truth data/events_truth_l1.csv --events 50000 --seed 42
./build/engine data/events.csv --out-dir data/out --ofi-levels 5
python python/backtest.py --ofi data/out/ofi.csv --bin-events 50 \
    --train-frac 0.7 --cost-ticks 0.5
python python/plots.py
```

## Next steps

- Swap in **LOBSTER** real sample data (same event schema) — the real test of whether OFI
  predicts, and with which sign, out of sample and net of costs.
- Turnover-aware position sizing (dead-band / hold) so the gross edge isn't spent on fees.
- Per-level OFI as separate regressors (not just summed) to see where the information sits.
