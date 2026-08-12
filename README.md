# Order Book Microstructure Engine

[![CI](https://github.com/coreyczhang/orderbook-microstructure-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/coreyczhang/orderbook-microstructure-engine/actions/workflows/ci.yml)

A limit order book (LOB) reconstruction and matching engine written in modern C++,
paired with a Python layer that computes an **Order Flow Imbalance (OFI)** signal and
backtests its short-horizon predictive power. Built as a portfolio project exploring
market-microstructure research on **public data only**.

See **[docs/architecture.md](docs/architecture.md)** for the component and data-flow
diagram, and **[docs/results.md](docs/results.md)** for the backtest write-up.

---

## What this is (one paragraph)

Exchanges publish a stream of order events — new limit orders, cancellations,
modifications, and executions. This project reconstructs the full limit order book from
that event stream in C++, runs a price-time-priority matching engine over it, and then
measures **order flow imbalance** — the net buying vs. selling pressure at the top of
the book — to test whether it predicts the next small move in price. The C++ core is the
systems-engineering centerpiece (cache-friendly data structures, O(1) cancels, RAII, a
randomized invariant stress test); the Python layer handles the statistical backtest.

## Roadmap

| Milestone | Scope | State |
|-----------|-------|-------|
| M1 | Core data structures: `Order`, `PriceLevel`, `OrderBook` (add/cancel/modify) + tests | ✅ done |
| M2 | Matching engine: price-time priority, partial fills, market orders, trades | ✅ done |
| M3 | Event replay + synthetic data pipeline (CSV) + ground-truth validation | ✅ done |
| M4 | OFI signal (C++) + Python regression/backtest + plots | ✅ done |
| M5 | Polish: architecture diagram, results write-up, CI | ✅ done |

**Post-M5 enhancements (done):** multi-level (top-*N*) integrated OFI, and a
transaction-cost-aware backtest comparing L1 vs. deep OFI. **Still open:** real LOBSTER
data, turnover-aware position sizing, per-level OFI regressors, optional pybind11 bindings.
See [docs/results.md](docs/results.md#next-steps).

## Build & test (C++)

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires a C++17 compiler and CMake ≥ 3.14. GoogleTest is fetched automatically via
CMake `FetchContent` — no manual install needed.

## Run the pipeline

Generate a synthetic event stream, reconstruct the book, validate against ground truth,
then compute and backtest the OFI signal:

```bash
# 1. Generate 50k synthetic events + ground-truth top-of-book snapshots
python python/generate_synthetic.py --out data/events.csv \
    --truth data/events_truth_l1.csv --events 50000 --seed 42

# 2. Replay through the engine -> trades.csv, book.csv, ofi.csv (L1 + top-5 deep)
./build/engine data/events.csv --out-dir data/out --ofi-levels 5

# 3. Prove the reconstructed book matches ground truth, row for row
python python/validate.py --engine-book data/out/book.csv \
    --truth data/events_truth_l1.csv

# 4. Backtest L1 vs deep OFI (chronological split, cost-aware PnL) and plot
python python/backtest.py --ofi data/out/ofi.csv --bin-events 50 \
    --train-frac 0.7 --cost-ticks 0.5
python python/plots.py
```

The generator and validator use only the Python standard library. The backtest and plots
add `numpy`, `pandas`, and `matplotlib` (see `python/requirements.txt`); the OLS is
implemented directly on numpy, so no heavyweight stats package is required.

## Design notes (M1)

- **Integer prices & quantities.** Prices are stored in integer *ticks* and quantities
  in integer shares, so the book is exact — no floating-point comparison bugs.
- **`PriceLevel`** is a FIFO of orders at one price, implemented as an **intrusive
  doubly linked list + `order_id → node` hash map**, giving O(1) append and O(1)
  cancel-by-id (a plain queue would make cancellation O(n)).
- **`OrderBook`** keeps bids and asks as `std::map<Price, PriceLevel>` (sorted, so the
  best price is at `begin()`), plus an `order_id → (side, price)` index so cancels and
  modifies locate their level directly. A production-latency version would replace the
  tree with a flat array of price levels over a bounded price range — noted here as a
  deliberate follow-up.
- **RAII throughout:** node ownership lives in `std::unique_ptr`; no raw `new`/`delete`.

## Matching engine (M2)

`MatchingEngine` processes an incoming order stream against the book with strict
**price-time priority**: an aggressor sweeps the opposite side best-price-first, then
oldest-order-first within a price, at each resting order's price. It handles partial
fills (of both the aggressor and the resting order), market orders (which sweep
regardless of price and never rest their remainder), and a `modify` that re-injects a
repriced order through matching so a reprice that crosses the book executes. A resting
limit order's unfilled remainder joins the book only after all crossing liquidity is
exhausted, so the engine never leaves the book crossed.

Test rigor: alongside targeted unit tests (full/partial fills, multi-level sweeps, time
priority, out-of-sequence arrivals, market-order edge cases, repriced-modify crossings),
a **randomized stress test** runs 20,000 pseudo-random events through the engine and, after
*every* operation, asserts structural invariants (level totals equal the sum of their
orders, no lingering empty levels, id index consistency, positive quantities), a
never-crossed book, and a per-operation share-conservation identity — all with a fixed
seed so any failure reproduces.

## Data pipeline (M3)

- **`EventReplay`** parses a tick-level event CSV (`ADD` / `CANCEL` / `MODIFY` /
  `MARKET`), stable-sorts by timestamp, and feeds each event through the matching
  engine, streaming out every trade and a top-of-book snapshot per event.
- **`engine` CLI** wraps this: `engine <events.csv> --out-dir <dir>` writes `trades.csv`
  and `book.csv`.
- **`generate_synthetic.py`** produces the event stream from a Poisson arrival process
  with limit prices around a random-walking mid and occasional crossing orders. It runs
  its own **shadow book** with the same price-time rules, so cancels always reference live
  orders and its top-of-book is exact **ground truth**.
- **`validate.py`** diffs the engine's `book.csv` against that ground truth. On the
  default 50k-event stream the reconstruction matches on **all 50,000 snapshots** — a
  direct correctness check, not just a smoke test.

Real **LOBSTER** sample data will be adapted to the same event schema in a later pass; the
synthetic path keeps the whole pipeline reproducible and self-validating in the meantime.

## Order flow imbalance & methodology (M4)

**Why OFI?** The top of the book is where price is discovered. When buy-side depth grows
faster than sell-side depth — new bids arriving, asks being consumed or cancelled — the
mid tends to tick up, and vice-versa. **Order Flow Imbalance** turns that intuition into a
single signed number per book update. It is a natural, well-studied microstructure signal
because it summarizes the *net* pressure from every event type (adds, cancels, executions)
in one quantity.

**Definition.** For each update `n` with best bid `(Pb, qb)` and best ask `(Pa, qa)`,
[`OrderFlowImbalance`](include/obme/OrderFlowImbalance.hpp) computes `OFI_n = e^b_n − e^a_n`:

```
e^b_n =  qb_n · 1{Pb_n ≥ Pb_{n-1}}  −  qb_{n-1} · 1{Pb_n ≤ Pb_{n-1}}
e^a_n =  qa_n · 1{Pa_n ≤ Pa_{n-1}}  −  qa_{n-1} · 1{Pa_n ≥ Pa_{n-1}}
```

`e^b` is the net change in bid-side depth and `e^a` the net change in ask-side depth, so a
positive OFI reflects net buying pressure. This is the general formulation of
Cont, Kukanov & Stoikov, *"The Price Impact of Order Book Events"*, Journal of Financial
Econometrics 12(1), 47–88 (2014) — a public, well-known academic paper. This project
implements the **published general methodology only**, not any firm-specific variant. The
engine emits both the **best-level (L1)** OFI and an **integrated "deep" OFI** over the top
`N` levels (`--ofi-levels`, default 5), the multi-level extension studied by Cont,
Cucuringu & Xu (2023).

The signals are computed in C++ during replay and streamed to `ofi.csv`; the Python
[`backtest.py`](python/backtest.py) bins them in event time, regresses returns on OFI with
a **chronological** (never shuffled) train/test split, and runs a **transaction-cost-aware
PnL**; [`plots.py`](python/plots.py) renders the figures below.

## Results

On the default synthetic stream (seed 42, 50k events, 50-event bins, 0.5-tick cost), full
numbers and an honest discussion of limitations are in
[docs/results.md](docs/results.md). Headline:

| Signal | Contemp R² | Predictive OOS R² | Test PnL gross | Test PnL **net** |
|--------|-----------:|------------------:|---------------:|-----------------:|
| `ofi` (L1)        | 0.085 | 0.030 | +20.5 | **−123.5** |
| `ofi_deep` (top-5) | **0.531** | 0.057 | +42.5 | **−82.5** |

![OFI vs returns](docs/ofi_scatter.png)

Three honest findings:

1. **Multi-level OFI dramatically improves contemporaneous fit** — integrating the top 5
   levels lifts R² from 0.085 to **0.53**; depth beyond the touch carries most of the
   price-impact information.
2. **Predictive power is weak and reverses sign** (β < 0) — OFI slightly *negatively*
   forecasts the next bin (transient impact / mean reversion); OOS R² stays ≈ 3–6%,
   significant only because N is large.
3. **The edge does not survive costs.** Both signals are profitable *gross* but the
   sign-flipping strategy turns over almost every bin; at 0.5 tick/turn, **net PnL is
   firmly negative** ([docs/pnl.png](docs/pnl.png)).

On frictionless synthetic data that mixed result is the honest, expected outcome — a
suspiciously clean predictive edge would be a red flag, not a feature.

## Testing

- **62 GoogleTest cases** across the order book, matching engine, replay/parse, and OFI
  (including multi-level OFI).
- A **randomized invariant stress test**: 20,000 pseudo-random events (fixed seed), with
  structural invariants, a never-crossed book, and per-operation share conservation
  checked after *every* operation.
- A **ground-truth reconstruction check**: the engine's rebuilt book is diffed against the
  synthetic generator's shadow book and matches on all 50,000 top-of-book snapshots.
- **CI** (GitHub Actions) builds, runs the tests, runs an end-to-end pipeline smoke test,
  and enforces `black` formatting on every push.

Build is clean under `-Wall -Wextra -Wpedantic`; no raw `new`/`delete` (RAII throughout).

## Project layout

```
include/obme/   public headers (Order, PriceLevel, OrderBook, MatchingEngine,
                EventReplay, OrderFlowImbalance, Trade)
src/            implementations + engine CLI (main.cpp)
tests/          GoogleTest suites (unit + randomized stress)
python/         generator, validator, backtest, plots, requirements.txt
docs/           architecture.md, results.md, result figures
data/           inputs & engine outputs (git-ignored; regenerated)
```

## Disclaimer

This project uses publicly available data and a clean-room implementation of a published
academic methodology. It is **not affiliated with, and does not use any code, data, or
proprietary methods from, any employer.**

## License

MIT — see [LICENSE](LICENSE).
