# Data

This directory holds market-data inputs and the engine's CSV outputs. The raw/generated
files are **not** checked into git (see `.gitignore`); only this README is tracked.

## Event input schema

The engine consumes an event stream as CSV with the following columns:

```
timestamp,event_type,order_id,side,price,quantity
```

| Column       | Type    | Notes                                                        |
|--------------|---------|--------------------------------------------------------------|
| `timestamp`  | int64   | Nanoseconds since an arbitrary epoch; must be non-decreasing |
| `event_type` | string  | `ADD`, `CANCEL`, `MODIFY`, or `MARKET`                       |
| `order_id`   | uint64  | Unique per resting order                                     |
| `side`       | char    | `B` (buy/bid) or `S` (sell/ask)                              |
| `price`      | int64   | Price in integer **ticks** (ignored for `MARKET`)           |
| `quantity`   | int64   | Shares (> 0)                                                 |

## Data sources

1. **Synthetic (default, arrives in M3).** A Poisson-arrival generator
   (`python/generate_synthetic.py`) produces a realistic-ish event stream so the whole
   pipeline is testable end-to-end and fully reproducible.
2. **LOBSTER real data (book-only path).** LOBSTER (<https://lobsterdata.com>) publishes
   an already-matched message stream plus a reconstructed order-book file. `message` files
   are converted to the schema above by `python/lobster_adapter.py`, and the engine
   reconstructs them with `engine --book-only` (no re-matching). The adapter also converts
   the paired `orderbook` file into a ground-truth L1 CSV so `validate.py` can confirm the
   reconstruction matches LOBSTER's own book.

   LOBSTER now gates its free samples behind a request/approval form, so obtaining a real
   sample is a manual step. In the meantime `python/make_lobster_fixture.py` emits a
   faithful LOBSTER-format message+orderbook pair (correct by construction) that exercises
   the exact same code path — see the "Real LOBSTER data" section of the top-level README.

No proprietary or non-public data is used anywhere in this project.
