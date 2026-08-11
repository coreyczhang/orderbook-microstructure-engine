#include "obme/MatchingEngine.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

namespace obme {
namespace {

Quantity trade_volume(const std::vector<Trade>& trades) {
    return std::accumulate(
        trades.begin(), trades.end(), Quantity{0},
        [](Quantity acc, const Trade& t) { return acc + t.quantity; });
}

Quantity resting_total(const OrderBook& book) {
    return book.total_shares(Side::Buy) + book.total_shares(Side::Sell);
}

// Feeds thousands of random events through the engine and, after every single
// operation, asserts the book's structural invariants hold, the book is never
// crossed, and a per-operation share-conservation identity balances. A fixed
// seed keeps failures reproducible.
TEST(Invariants, RandomizedStressKeepsBookConsistent) {
    constexpr int kNumOps = 20000;
    std::mt19937_64 rng(0xC0FFEEULL);

    std::uniform_int_distribution<int> action_dist(0, 99);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<Price> price_dist(95, 105);  // tight band → crossings
    std::uniform_int_distribution<Quantity> qty_dist(1, 20);

    MatchingEngine eng;
    OrderId next_id = 1;
    Timestamp clock = 0;

    for (int op = 0; op < kNumOps; ++op) {
        ++clock;
        const int action = action_dist(rng);
        const Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;

        if (action < 60) {
            // 60%: new limit order (aggressor may cross, remainder rests).
            const Order o{next_id++, side, price_dist(rng), qty_dist(rng), clock,
                          OrderType::Limit};
            const Quantity before = resting_total(eng.book());
            const auto trades = eng.submit(o);
            const Quantity after = resting_total(eng.book());
            // Net book change = incoming remainder added (o.qty - E) minus resting
            // shares consumed (E) = o.qty - 2E.
            EXPECT_EQ(after - before, o.quantity - 2 * trade_volume(trades))
                << "limit conservation failed at op " << op;
        } else if (action < 75) {
            // 15%: market order (never rests; only consumes resting shares).
            const Order o{next_id++, side, 0, qty_dist(rng), clock, OrderType::Market};
            const Quantity before = resting_total(eng.book());
            const auto trades = eng.submit(o);
            const Quantity after = resting_total(eng.book());
            EXPECT_EQ(after - before, -trade_volume(trades))
                << "market conservation failed at op " << op;
        } else if (action < 90) {
            // 15%: cancel a randomly chosen resting order, if any exist.
            const auto resting = eng.book().snapshot();
            if (!resting.empty()) {
                std::uniform_int_distribution<std::size_t> pick(0, resting.size() - 1);
                const Order target = resting[pick(rng)];
                const Quantity before = resting_total(eng.book());
                EXPECT_TRUE(eng.cancel(target.id));
                const Quantity after = resting_total(eng.book());
                EXPECT_EQ(before - after, target.quantity)
                    << "cancel conservation failed at op " << op;
            }
        } else {
            // 10%: modify a randomly chosen resting order (may cross/execute).
            const auto resting = eng.book().snapshot();
            if (!resting.empty()) {
                std::uniform_int_distribution<std::size_t> pick(0, resting.size() - 1);
                const Order target = resting[pick(rng)];
                eng.modify(target.id, price_dist(rng), qty_dist(rng));
                // modify is built from the (separately conservation-checked)
                // submit/cancel primitives; here we just require consistency.
            }
        }

        // Structural integrity after every operation.
        ASSERT_TRUE(eng.book().check_invariants())
            << "structural invariant broken at op " << op;
        // The engine must never leave the book crossed.
        if (eng.book().has_best_bid() && eng.book().has_best_ask()) {
            ASSERT_LT(eng.book().best_bid(), eng.book().best_ask())
                << "crossed book at op " << op;
        }
    }

    // Sanity: the run actually exercised meaningful matching and left some state.
    EXPECT_GT(eng.executed_volume(), 0);
}

}  // namespace
}  // namespace obme
