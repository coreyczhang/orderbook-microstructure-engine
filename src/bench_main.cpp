// Micro/throughput benchmarks for the order book and matching engine.
//
// Build in Release for meaningful numbers:
//   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
//   ./build/obme_bench
//
// Reports:
//   1. MatchingEngine end-to-end throughput (events/sec).
//   2. Resting-book op throughput: std::map OrderBook vs. flat-array book, on an
//      identical add/cancel/modify stream, with the speedup.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "obme/FlatArrayBook.hpp"
#include "obme/MatchingEngine.hpp"
#include "obme/OrderBook.hpp"

namespace {

using namespace obme;
using Clock = std::chrono::steady_clock;

enum class Kind { Add, Cancel, Modify, Market };

struct Op {
    Kind kind;
    Order order;  // for Add/Market; id/price/qty reused for Cancel/Modify
};

// Builds a coherent op stream: adds within a price band, cancels/modifies of
// still-live ids, and (optionally) market orders. Generation is not timed.
std::vector<Op> make_stream(std::size_t n, bool with_market, std::uint64_t seed,
                            Price lo, Price hi) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> action(0, 99);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<Price> price_dist(lo, hi);
    std::uniform_int_distribution<Quantity> qty_dist(1, 50);

    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<OrderId> live;
    OrderId next_id = 1;

    for (std::size_t i = 0; i < n; ++i) {
        const int a = action(rng);
        const Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
        if (with_market && a < 10 && !live.empty()) {
            ops.push_back({Kind::Market,
                           Order{next_id++, side, 0, qty_dist(rng), 0,
                                 OrderType::Market}});
        } else if (a < 60 || live.empty()) {
            const Order o{next_id++, side, price_dist(rng), qty_dist(rng), 0,
                          OrderType::Limit};
            ops.push_back({Kind::Add, o});
            live.push_back(o.id);
        } else if (a < 80) {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            const std::size_t k = pick(rng);
            Order o{};
            o.id = live[k];
            ops.push_back({Kind::Cancel, o});
            live[k] = live.back();
            live.pop_back();
        } else {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            Order o{};
            o.id = live[pick(rng)];
            o.price = price_dist(rng);
            o.quantity = qty_dist(rng);
            ops.push_back({Kind::Modify, o});
        }
    }
    return ops;
}

double seconds(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

void report(const char* label, std::size_t n, double best_s) {
    const double mops = n / best_s / 1e6;
    const double ns = best_s / n * 1e9;
    std::printf("  %-34s %8.2f M ops/s   %6.1f ns/op\n", label, mops, ns);
}

// Runs `fn` `reps` times, returns the fastest wall-clock time.
template <typename F>
double best_of(int reps, F&& fn) {
    double best = 1e18;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = Clock::now();
        fn();
        best = std::min(best, seconds(t0, Clock::now()));
    }
    return best;
}

}  // namespace

int main() {
#ifndef NDEBUG
    std::printf("WARNING: debug build (asserts on) — numbers are not "
                "representative. Configure with -DCMAKE_BUILD_TYPE=Release.\n\n");
#endif
    constexpr std::size_t kN = 2'000'000;
    constexpr int kReps = 3;
    constexpr Price kLo = 9'900, kHi = 10'100;  // tight band: heavy matching
    constexpr Price kBookLo = 6'000, kBookHi = 14'000;  // ~8k levels: realistic width
    constexpr Price kMin = 1, kMax = 20'000;

    std::printf("Order Book Microstructure Engine — benchmarks (%zu ops, best of %d)\n\n",
                kN, kReps);

    // 1. Matching engine end-to-end.
    {
        const std::vector<Op> ops = make_stream(kN, /*with_market=*/true, 1, kLo, kHi);
        const double s = best_of(kReps, [&] {
            MatchingEngine eng;
            for (const Op& op : ops) {
                switch (op.kind) {
                    case Kind::Add:
                    case Kind::Market:
                        eng.submit(op.order);
                        break;
                    case Kind::Cancel:
                        eng.cancel(op.order.id);
                        break;
                    case Kind::Modify:
                        eng.modify(op.order.id, op.order.price, op.order.quantity);
                        break;
                }
            }
        });
        std::printf("MatchingEngine (with matching):\n");
        report("submit/cancel/modify", kN, s);
        std::printf("\n");
    }

    // 2. Resting-book ops: std::map vs. flat array, identical stream.
    {
        const std::vector<Op> ops =
            make_stream(kN, /*with_market=*/false, 2, kBookLo, kBookHi);
        const double s_map = best_of(kReps, [&] {
            OrderBook book;
            for (const Op& op : ops) {
                switch (op.kind) {
                    case Kind::Add: book.add_limit_order(op.order); break;
                    case Kind::Cancel: book.cancel_order(op.order.id); break;
                    case Kind::Modify:
                        book.modify_order(op.order.id, op.order.price, op.order.quantity);
                        break;
                    case Kind::Market: break;
                }
            }
        });
        const double s_arr = best_of(kReps, [&] {
            FlatArrayBook book(kMin, kMax);
            for (const Op& op : ops) {
                switch (op.kind) {
                    case Kind::Add: book.add_limit_order(op.order); break;
                    case Kind::Cancel: book.cancel_order(op.order.id); break;
                    case Kind::Modify:
                        book.modify_order(op.order.id, op.order.price, op.order.quantity);
                        break;
                    case Kind::Market: break;
                }
            }
        });
        std::printf("Resting book (add/cancel/modify, no matching):\n");
        report("OrderBook (std::map)", kN, s_map);
        report("FlatArrayBook (flat array)", kN, s_arr);
        std::printf("  %-34s %8.2fx\n", "flat-array speedup", s_map / s_arr);
    }
    return 0;
}
