# Benchmarks

Run with a Release build (`obme_bench` target):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/obme_bench
```

Each figure is the best of 3 runs over 2,000,000 operations. Numbers below are from an
Apple M-series laptop (AppleClang, `-O3`); they are **machine-dependent** — treat them as
relative, not absolute.

## Results

| Benchmark | Throughput | Latency |
|-----------|-----------:|--------:|
| **MatchingEngine** end-to-end (submit/cancel/modify + matching) | ~3.0–3.8 M events/s | ~270–330 ns/event |
| Resting book — `OrderBook` (`std::map`), ~8k-level book | ~0.6 M ops/s | ~1.7 µs/op |
| Resting book — `FlatArrayBook` (flat array), ~8k-level book | ~0.8 M ops/s | ~1.3 µs/op |
| **Flat-array speedup** on a realistic-width book | **~1.3–1.4×** | |

## Reading these honestly

- **The matching-engine number is higher than the resting-book number** because its
  benchmark uses a tight price band with market orders, so incoming orders mostly *match
  and leave* — the book stays small and cache-resident. The resting-book benchmark never
  matches, so ~1.2 M orders accumulate across ~8k price levels; it is the harder, larger-
  working-set case.
- **The flat-array win grows with book width.** On a narrow (~200-level) book the two are
  within ~10%; at ~8k levels the flat array's O(1) index beats the tree's O(log L)
  traversal over scattered nodes (worse cache locality) by ~1.3×.
- **Both books are allocation-bound.** Each resting order allocates an intrusive-list node
  (`make_unique`) and touches two hash maps (the per-level id→node map and the book-wide
  id→location map). At ~1 M live orders those hash lookups miss cache on nearly every op,
  which dominates the ~1.4–1.8 µs/op and compresses the map-vs-array gap. The clear next
  optimization is a **pooled/slab node allocator** (and a flat id index), which would expose
  more of the data-structure difference — a deliberate follow-up, not done here.

## Design trade-off

`FlatArrayBook` pre-allocates one `PriceLevel` slot per tick over a bounded `[min, max]`
price range, trading fixed memory for O(1) level access and no per-level heap node. That is
the right shape for a liquid instrument with a known price band; `OrderBook`'s `std::map`
stays the general-purpose default for an unbounded range. The two share the same resting-
book API and are cross-checked against each other in `tests/test_flat_array_book.cpp`
(30,000 random ops, identical observable state at every step).
