# Order Book Microstructure Engine

A limit order book (LOB) reconstruction and matching engine written in modern C++,
paired with a Python layer that computes an **Order Flow Imbalance (OFI)** signal and
backtests its short-horizon predictive power. Built as a portfolio project exploring
market-microstructure research on **public data only**.

> **Status:** work in progress. This README grows milestone by milestone.
> Currently implemented: **M1 — core data structures** and **M2 — matching engine**.

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
| M3 | Event replay + synthetic data pipeline (CSV) | ⏳ planned |
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
