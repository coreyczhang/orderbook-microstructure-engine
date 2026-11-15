# Market-Making Simulator

An event-driven simulator that replays a market event stream through the real
matching engine and measures what a passive quoting strategy would have earned —
including **fills, queue position, latency, and adverse selection**. It is built
entirely on the public `obme` API (nothing in `include/obme/` changed) and runs
on public or synthetic data only.

```
events.csv ──▶ Simulator ──▶ fills.csv, pnl.csv ──▶ python/simulate_mm.py ──▶ figures
                  │
                  ├─ MatchingEngine        the real market (true book + executions)
                  ├─ Strategy (swappable)  sees only state ≤ now  →  desired quotes
                  └─ shadow orders         our quotes, with latency + FIFO fills
```

## What it models

- **Causal replay.** The strategy sees only the book and trades up to the current
  timestamp; every decision is a pure function of the past. A test
  (`FutureEventsDoNotChangePastDecisions`) confirms that rewriting future events
  never changes a decision already made.
- **Shadow quotes (counterfactual).** Our orders live *alongside* the market — they
  do not perturb the real matching engine (small-agent assumption), which is what
  lets us measure a strategy against historical/synthetic flow without rewriting
  the counterfactual.
- **Latency** (`--latency-ns`, default 1 ms). A submitted order is not live — and a
  cancellation does not take effect — until its delay has elapsed in event time.
  While a cancel is in flight the order can still be hit.
- **Queue position (FIFO).** When an order goes live it records the volume resting
  ahead of it at its price; it fills only after that much volume has *traded*
  there. Invariant (`FilledQtyNeverExceedsTradedVolumeAtPrice`): our filled
  quantity at any price never exceeds the volume that traded at that price.
- **Adverse selection (markout).** For each fill, the mid is sampled again
  `--horizon-ns` later (default 1 s); `markout` is the maker-favorable move
  (positive is good). A negative average is the signature of adverse selection.

## Strategy

`FlowImbalanceMM` ([FlowImbalanceMM.hpp](FlowImbalanceMM.hpp)) quotes **only** when
recent trade flow is roughly balanced *and* the book shows a directional bias,
posting a one-tick-wide quote skewed toward that bias. Its constants
(`FLOW_MIN`/`FLOW_MAX`, `BOOK_THRESHOLD`, `MID_SHIFT`, `TRADE_WINDOW`) are the
project's **own clean-room defaults** — not taken from any employer, internship,
or other non-public source — and all are configurable. The `Strategy` interface
([Strategy.hpp](Strategy.hpp)) is abstract so the strategy can be swapped out.

## Run it

```bash
# build (from repo root)
cmake -S . -B build && cmake --build build

# synthetic events, then simulate, then analyze
python python/generate_synthetic.py --out data/events.csv --events 50000 --seed 42
./build/simulator/mm_sim data/events.csv --out-dir data/mm_out
python python/simulate_mm.py --fills data/mm_out/fills.csv --pnl data/mm_out/pnl.csv
```

`mm_sim` flags: `--latency-ns`, `--horizon-ns`, `--quote-size`, `--flow-min`,
`--flow-max`, `--book-threshold`, `--mid-shift`, `--trade-window-ns`.

## Honest limitations

- **Not inventory-aware.** The default strategy does not skew or flatten to manage
  position, so inventory can drift and carry directional risk — visible in
  `mm_equity.png`. Inventory control is an obvious next strategy iteration.
- **Shadow fills are optimistic in one respect:** cancellations ahead of us in the
  queue do not advance us (only trades do, per the spec), but we also assume our
  presence never deters the counterparties that generate the flow.
- **Synthetic flow.** Results on the Poisson generator are a mechanism demo, not a
  market study; the same pipeline runs on real LOBSTER-format data once reconstructed
  to an event stream.
