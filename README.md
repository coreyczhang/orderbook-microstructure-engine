# Order Book Microstructure Engine

A limit order book (LOB) reconstruction and matching engine written in modern C++,
paired with a Python layer that computes an **Order Flow Imbalance (OFI)** signal and
backtests its short-horizon predictive power. Built as a portfolio project exploring
market-microstructure research on **public data only**.

> **Status:** work in progress. This README grows milestone by milestone.
> Currently implemented: **M1 — core data structures** (`Order`, `PriceLevel`, `OrderBook`).

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
| M2 | Matching engine: price-time priority, partial fills, trades | ⏳ planned |
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
