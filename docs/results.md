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

Position is taken from the **train** fit (a contrarian rule, since β < 0). Two rules are
compared: **flip** (`sign(â + b̂·OFI_t)`, re-decided every bin) and a **hysteresis
dead-band** that only changes position when the predicted move exceeds a threshold tuned
on the training window (here 1.5× the train predicted-return std). Out-of-sample (test-set)
net PnL, in ticks:

| Signal | net (flip) | test turnover | net (dead-band) | turnover | turnover cut |
|--------|-----------:|--------------:|----------------:|---------:|-------------:|
| `ofi` (L1)   | **−123.5** | 288 | **−4.5** | 32 | −89% |
| `ofi_deep` (top-5) | **−82.5** | 250 | **−15.5** | 152 | −39% |

**The naive edge does not survive costs, and a turnover throttle recovers most but not all
of the loss.** Trading every bin (~250–290 turns over 300 test bins) at 0.5 tick/turn swamps
the tiny per-bin gross edge, so flip net PnL is firmly negative. The train-tuned dead-band
cuts L1 turnover by ~89% and lifts net PnL from −123.5 to **−4.5** (near break-even), and
deep from −82.5 to −15.5 — but **both stay negative**. Throttling turnover removes cost
drag; it does not manufacture alpha from a signal this weak on frictionless data.

## Honest caveats

1. **Frictionless synthetic, mechanical data.** The exploited reversion is a property of
   the generator, not evidence it exists (or has this sign) in real markets.
2. **Even with a dead-band, this is not an optimized system.** One threshold grid, one
   hysteresis rule, a fixed cost — a real strategy would model queue position, latency,
   and a fuller cost curve.
3. **Single asset, single seed, ~50 s of simulated time.** A pipeline demonstration, not a
   robust alpha study.
4. **OFI is a signal, not a strategy.** Strong contemporaneous impact ≠ tradable forecast.

## Takeaway

The engine reconstructs the book exactly (validated to the tick against ground truth), and
the OFI signals behave sensibly and informatively: **multi-level OFI dramatically improves
contemporaneous explanatory power (R² 0.53); predictive power is weak and transient; a
naive signal-following strategy loses money after realistic costs; and a train-tuned
turnover throttle recovers most of that loss but still does not clear the spread.** That is
the honest, expected outcome on frictionless synthetic data — reported as measured, not
tuned.

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

- Run on **real LOBSTER data** (the `--book-only` reconstruction path and adapter are
  built and CI-verified against LOBSTER's own book; obtaining a sample is a manual,
  user-side download) — the real test of whether OFI predicts, and with which sign, out of
  sample and net of costs.
- Per-level OFI as separate regressors (not just summed) to see where the information sits.
- A fuller cost/queue model (queue position, latency) rather than a flat per-turn tick.
