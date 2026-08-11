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
2. **LOBSTER sample data (M3+).** Free sample limit-order-book data for a few
   tickers/days is available at <https://lobsterdata.com>. Once obtained, its message
   files are adapted to the schema above. Download instructions will be added here.

No proprietary or non-public data is used anywhere in this project.
