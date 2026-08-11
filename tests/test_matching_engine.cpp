#include "obme/MatchingEngine.hpp"

#include <gtest/gtest.h>

namespace obme {
namespace {

Order buy(OrderId id, Price price, Quantity qty, Timestamp ts = 0) {
    return Order{id, Side::Buy, price, qty, ts, OrderType::Limit};
}
Order sell(OrderId id, Price price, Quantity qty, Timestamp ts = 0) {
    return Order{id, Side::Sell, price, qty, ts, OrderType::Limit};
}
Order market(OrderId id, Side side, Quantity qty, Timestamp ts = 0) {
    return Order{id, side, 0, qty, ts, OrderType::Market};
}

TEST(Matching, NonCrossingOrderJustRests) {
    MatchingEngine eng;
    auto t1 = eng.submit(buy(1, 99, 10));
    auto t2 = eng.submit(sell(2, 101, 10));
    EXPECT_TRUE(t1.empty());
    EXPECT_TRUE(t2.empty());
    EXPECT_EQ(eng.book().best_bid(), 99);
    EXPECT_EQ(eng.book().best_ask(), 101);
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, FullFillRemovesRestingOrder) {
    MatchingEngine eng;
    eng.submit(sell(1, 100, 10));
    auto trades = eng.submit(buy(2, 100, 10));  // exactly crosses

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].aggressor_id, 2u);
    EXPECT_EQ(trades[0].resting_id, 1u);
    EXPECT_EQ(trades[0].price, 100);      // resting order's price
    EXPECT_EQ(trades[0].quantity, 10);
    EXPECT_EQ(trades[0].aggressor_side, Side::Buy);

    EXPECT_FALSE(eng.book().has_best_ask());  // resting order consumed
    EXPECT_FALSE(eng.book().has_best_bid());  // aggressor fully filled, nothing rests
    EXPECT_EQ(eng.executed_volume(), 10);
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, PartialFillOfAggressorRestsRemainder) {
    MatchingEngine eng;
    eng.submit(sell(1, 100, 4));
    auto trades = eng.submit(buy(2, 100, 10));  // takes 4, rests 6

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 4);
    EXPECT_FALSE(eng.book().has_best_ask());
    ASSERT_TRUE(eng.book().has_best_bid());
    EXPECT_EQ(eng.book().best_bid(), 100);
    EXPECT_EQ(eng.book().best_quantity(Side::Buy), 6);  // remainder rests
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, PartialFillOfRestingOrderKeepsPriority) {
    MatchingEngine eng;
    eng.submit(sell(1, 100, 10));
    auto trades = eng.submit(buy(2, 100, 4));  // takes 4 of resting 10

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 4);
    ASSERT_TRUE(eng.book().has_best_ask());
    EXPECT_EQ(eng.book().best_quantity(Side::Sell), 6);  // 6 left resting
    EXPECT_FALSE(eng.book().has_best_bid());              // buyer fully filled
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, WalksMultiplePriceLevels) {
    MatchingEngine eng;
    eng.submit(sell(1, 100, 5));
    eng.submit(sell(2, 101, 5));
    eng.submit(sell(3, 102, 5));
    // Aggressive buy up to 101 for 12 shares: fills 5@100, 5@101, rests 2@101? No,
    // 2 remaining would rest at the buy limit 101.
    auto trades = eng.submit(buy(4, 101, 12));

    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 5);
    EXPECT_EQ(trades[1].price, 101);
    EXPECT_EQ(trades[1].quantity, 5);
    // 2 shares unfilled rest as a bid at 101; 102 ask untouched.
    EXPECT_EQ(eng.book().best_bid(), 101);
    EXPECT_EQ(eng.book().best_quantity(Side::Buy), 2);
    EXPECT_EQ(eng.book().best_ask(), 102);
    EXPECT_EQ(eng.executed_volume(), 10);
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, TimePriorityAtSamePrice) {
    MatchingEngine eng;
    eng.submit(sell(1, 100, 5, /*ts=*/1));  // earlier
    eng.submit(sell(2, 100, 5, /*ts=*/2));  // later
    auto trades = eng.submit(buy(3, 100, 5));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_id, 1u);  // oldest filled first
    // Order 2 remains.
    ASSERT_TRUE(eng.book().has_best_ask());
    EXPECT_EQ(eng.book().best_quantity(Side::Sell), 5);
    EXPECT_EQ(eng.book().best_order(Side::Sell).id, 2u);
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, OutOfSequenceArrivalStillRespectsPriceTime) {
    // Resting orders arrive "out of sequence" relative to price; matching must
    // still go best-price-first, then oldest-first.
    MatchingEngine eng;
    eng.submit(sell(10, 102, 3, 5));
    eng.submit(sell(11, 100, 3, 7));  // best price, arrived later in wall time
    eng.submit(sell(12, 100, 3, 9));  // same price, even later
    auto trades = eng.submit(buy(20, 105, 7));

    ASSERT_EQ(trades.size(), 3u);
    EXPECT_EQ(trades[0].resting_id, 11u);  // 100, oldest at that price
    EXPECT_EQ(trades[0].quantity, 3);
    EXPECT_EQ(trades[1].resting_id, 12u);  // 100, next
    EXPECT_EQ(trades[1].quantity, 3);
    EXPECT_EQ(trades[2].resting_id, 10u);  // 102, last
    EXPECT_EQ(trades[2].quantity, 1);      // only 1 share of buy left
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, MarketBuySweepsUntilFilled) {
    MatchingEngine eng;
    eng.submit(sell(1, 100, 4));
    eng.submit(sell(2, 101, 4));
    auto trades = eng.submit(market(3, Side::Buy, 6));

    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 4);
    EXPECT_EQ(trades[1].price, 101);
    EXPECT_EQ(trades[1].quantity, 2);
    EXPECT_FALSE(eng.book().has_best_bid());  // market order never rests
    EXPECT_EQ(eng.book().best_quantity(Side::Sell), 2);
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, MarketOrderRemainderIsDropped) {
    MatchingEngine eng;
    eng.submit(sell(1, 100, 3));
    auto trades = eng.submit(market(2, Side::Buy, 10));  // only 3 available

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 3);
    EXPECT_FALSE(eng.book().has_best_ask());
    EXPECT_FALSE(eng.book().has_best_bid());  // 7 unfilled shares dropped
    EXPECT_EQ(eng.book().order_count(), 0u);
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, MarketOrderIntoEmptyBookDoesNothing) {
    MatchingEngine eng;
    auto trades = eng.submit(market(1, Side::Sell, 5));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(eng.book().order_count(), 0u);
}

TEST(Matching, SellAggressorCrossesBids) {
    MatchingEngine eng;
    eng.submit(buy(1, 100, 5));
    eng.submit(buy(2, 99, 5));
    auto trades = eng.submit(sell(3, 99, 8));  // hits 100 then 99

    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].price, 100);  // best bid first
    EXPECT_EQ(trades[0].quantity, 5);
    EXPECT_EQ(trades[1].price, 99);
    EXPECT_EQ(trades[1].quantity, 3);
    EXPECT_EQ(eng.book().best_quantity(Side::Buy), 2);  // 2 left at 99
    EXPECT_FALSE(eng.book().has_best_ask());
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, CancelRestingOrderMidBook) {
    MatchingEngine eng;
    eng.submit(buy(1, 100, 5));
    eng.submit(buy(2, 100, 5));
    EXPECT_TRUE(eng.cancel(1));
    EXPECT_FALSE(eng.cancel(1));  // already gone
    // Order 2 is now front; a crossing sell hits it.
    auto trades = eng.submit(sell(3, 100, 5));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_id, 2u);
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, ModifyDecreaseInPlaceProducesNoTrades) {
    MatchingEngine eng;
    eng.submit(buy(1, 100, 10));
    auto trades = eng.modify(1, 100, 6);
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(eng.book().best_quantity(Side::Buy), 6);
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, ModifyRepriceThatCrossesExecutes) {
    MatchingEngine eng;
    eng.submit(sell(1, 100, 5));
    eng.submit(buy(2, 98, 5));  // resting bid, not crossing yet

    // Reprice the bid up to 100 so it now crosses the resting ask; the modify
    // becomes a cancel + re-submit and therefore executes.
    auto trades = eng.modify(2, 100, 5);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_id, 1u);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 5);
    EXPECT_EQ(eng.book().order_count(), 0u);
    EXPECT_TRUE(eng.book().check_invariants());
}

TEST(Matching, ModifyNonexistentReturnsEmpty) {
    MatchingEngine eng;
    EXPECT_TRUE(eng.modify(42, 100, 5).empty());
}

TEST(Matching, BookNeverCrossedAfterResting) {
    MatchingEngine eng;
    eng.submit(sell(1, 100, 5));
    eng.submit(buy(2, 105, 3));  // crosses, fully fills, nothing rests
    // Now submit a buy that partially fills and rests below the ask.
    eng.submit(sell(3, 101, 10));
    eng.submit(buy(4, 101, 4));  // takes 4@101, nothing rests (fully filled)
    if (eng.book().has_best_bid() && eng.book().has_best_ask()) {
        EXPECT_LT(eng.book().best_bid(), eng.book().best_ask());
    }
    EXPECT_TRUE(eng.book().check_invariants());
}

}  // namespace
}  // namespace obme
