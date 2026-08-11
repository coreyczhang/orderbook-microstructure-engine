# Order Book Microstructure Engine

A limit order book (LOB) reconstruction and matching engine written in modern C++,
paired with a Python layer that computes an **Order Flow Imbalance (OFI)** signal and
backtests its short-horizon predictive power. Built as a portfolio project exploring
market-microstructure research on **public data only**.

> **Status:** work in progress. This README grows milestone by milestone.
> Currently implemented: **M1 — core data structures**, **M2 — matching engine**,
> and **M3 — event replay + synthetic data pipeline**.

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
| M4 | OFI signal + Python regression/backtest + plots | ⏳ planned |
| M5 | Polish: full write-up, architecture diagram, CI | ⏳ planned |

## Build & test (C++)

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires a C++17 compiler and CMake ≥ 3.14. GoogleTest is fetched automatically via
CMake `FetchContent` — no manual install needed.

## Run the pipeline (M3)

Generate a synthetic event stream, reconstruct the book with the C++ engine, and verify
the reconstruction against the generator's ground truth:

```bash
# 1. Generate 50k synthetic events + ground-truth top-of-book snapshots
python python/generate_synthetic.py --out data/events.csv \
    --truth data/events_truth_l1.csv --events 50000 --seed 42

# 2. Replay through the engine -> data/out/trades.csv, data/out/book.csv
./build/engine data/events.csv --out-dir data/out

# 3. Prove the reconstructed book matches ground truth, row for row
python python/validate.py --engine-book data/out/book.csv \
    --truth data/events_truth_l1.csv
```

The generator and validator use only the Python standard library. The M4 backtest adds
`numpy`, `pandas`, `statsmodels`, and `matplotlib` (see `python/requirements.txt`).

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

## Order flow imbalance & methodology

OFI is implemented following the general framework of Cont, Kukanov & Stoikov,
*"The Price Impact of Order Book Events"* (2014) — a public, well-known academic paper.
Full explanation and citation will land with M4.

## Disclaimer

This project uses publicly available data and a clean-room implementation of a published
academic methodology. It is **not affiliated with, and does not use any code, data, or
proprietary methods from, any employer.**

## License

MIT — see [LICENSE](LICENSE).
