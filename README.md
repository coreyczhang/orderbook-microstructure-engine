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

**Post-M5 enhancements (done):** multi-level (top-*N*) integrated OFI; a
transaction-cost-aware backtest comparing L1 vs. deep OFI; and **LOBSTER-format
reconstruction** (`--book-only` mode + adapter + fixture, CI-verified). Plus **turnover-aware dead-band
sizing** in the cost-aware backtest, and a **flat-array order book + throughput
benchmark** (see [Performance](#performance)). **Still open:** running on a *real* LOBSTER
sample (download is user-gated), per-level OFI regressors, a pooled node allocator,
optional pybind11 bindings. See [docs/results.md](docs/results.md#next-steps).

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
  modifies locate their level directly. For a bounded price band, `FlatArrayBook` is the
  production-latency variant — a flat `std::vector<PriceLevel>` indexed by
  `price − min_price` for O(1) level access (see [Performance](#performance)).
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

## Real LOBSTER data (book-only reconstruction)

The engine also reconstructs **real exchange data** in [LOBSTER](https://lobsterdata.com)
format. LOBSTER's message stream is *already matched* — incoming marketable orders appear
as executions against resting orders, not as flow to re-match — so the engine reconstructs
it with `--book-only` (applying ADD/CANCEL/MODIFY directly to the book, no matching):

- **`lobster_adapter.py`** translates a LOBSTER `message` file into this project's event
  schema (one event per message; hidden/cross/halt messages become no-ops that preserve
  row alignment) and converts the paired `orderbook` file into a ground-truth L1 CSV.
- **`engine --book-only`** reconstructs the book and emits `book.csv` + `ofi.csv`.
- **`validate.py`** then confirms the reconstruction matches LOBSTER's own order book,
  row for row.

Because LOBSTER now gates its free samples behind a request/approval form, the repo ships
**`make_lobster_fixture.py`**, which emits a faithful LOBSTER-format message+orderbook pair
(driven by the shadow book, so it is correct by construction). This proves the adapter and
book-only reconstruction end-to-end — and is checked in CI — **with no proprietary data**:

```bash
# Prove the LOBSTER path on a self-contained fixture (matches on all snapshots)
python python/make_lobster_fixture.py --out-message data/lob_msg.csv \
    --out-orderbook data/lob_ob.csv --messages 3000 --seed 7
python python/lobster_adapter.py --message data/lob_msg.csv --out data/lob_events.csv \
    --orderbook data/lob_ob.csv --truth data/lob_truth.csv
./build/engine data/lob_events.csv --out-dir data/lob_out --book-only
python python/validate.py --engine-book data/lob_out/book.csv --truth data/lob_truth.csv
```

To use a **real** LOBSTER sample, request it from lobsterdata.com, then run the same
`lobster_adapter.py` → `engine --book-only` → `validate.py` steps on the downloaded
`*_message_*.csv` and `*_orderbook_*.csv` files.

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
3. **The edge does not survive costs — and a turnover throttle recovers most, not all, of
   it.** Both signals are profitable *gross*, but the flip-every-bin strategy turns over
   almost every bin; at 0.5 tick/turn **net PnL is firmly negative** (L1 −123.5, deep
   −82.5). A **train-tuned hysteresis dead-band** cuts L1 turnover ~89% and lifts net to
   −4.5 (near break-even), deep to −15.5 — still negative ([docs/pnl.png](docs/pnl.png)).
   Throttling turnover removes cost drag; it doesn't manufacture alpha.

On frictionless synthetic data that mixed result is the honest, expected outcome — a
suspiciously clean predictive edge would be a red flag, not a feature.

## Performance

A Release-build benchmark (`obme_bench`) over 2M operations, best of 3 (machine-dependent
— Apple M-series, AppleClang `-O3`):

| Benchmark | Throughput |
|-----------|-----------:|
| MatchingEngine end-to-end (submit/cancel/modify + matching) | **~3 M events/s** (~300 ns/event) |
| Resting book — `std::map` `OrderBook`, ~8k-level book | ~0.6 M ops/s |
| Resting book — `FlatArrayBook` (flat array), ~8k-level book | ~0.8 M ops/s (**~1.3–1.4×**) |

The flat-array book wins more as the book widens (O(1) index vs. `std::map`'s O(log L) over
scattered nodes); both are allocation-bound at ~1 M live orders, so a pooled node allocator
is the clear next optimization. Full methodology and an honest bottleneck analysis:
**[docs/benchmarks.md](docs/benchmarks.md)**.

## Testing

- **66 GoogleTest cases** across the order book, matching engine, replay/parse, OFI
  (including multi-level OFI), book-only reconstruction, and the flat-array book (a
  30k-op parity cross-check against the `std::map` book).
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
