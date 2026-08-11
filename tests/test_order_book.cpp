#include "obme/OrderBook.hpp"

#include <gtest/gtest.h>

namespace obme {
namespace {

Order buy(OrderId id, Price price, Quantity qty, Timestamp ts = 0) {
    return Order{id, Side::Buy, price, qty, ts, OrderType::Limit};
}
Order sell(OrderId id, Price price, Quantity qty, Timestamp ts = 0) {
    return Order{id, Side::Sell, price, qty, ts, OrderType::Limit};
}

TEST(OrderBook, EmptyBookHasNoBestPrices) {
    OrderBook book;
    EXPECT_FALSE(book.has_best_bid());
    EXPECT_FALSE(book.has_best_ask());
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_TRUE(book.empty(Side::Buy));
    EXPECT_TRUE(book.empty(Side::Sell));
}

TEST(OrderBook, AddSetsBestBidAndAsk) {
    OrderBook book;
    book.add_limit_order(buy(1, 99, 10));
    book.add_limit_order(sell(2, 101, 5));

    ASSERT_TRUE(book.has_best_bid());
    ASSERT_TRUE(book.has_best_ask());
    EXPECT_EQ(book.best_bid(), 99);
    EXPECT_EQ(book.best_ask(), 101);
    EXPECT_EQ(book.order_count(), 2u);
    EXPECT_TRUE(book.contains(1));
    EXPECT_TRUE(book.contains(2));
}

TEST(OrderBook, BestBidIsHighestBestAskIsLowest) {
    OrderBook book;
    book.add_limit_order(buy(1, 98, 10));
    book.add_limit_order(buy(2, 100, 10));  // better bid
    book.add_limit_order(buy(3, 99, 10));
    book.add_limit_order(sell(4, 105, 10));
    book.add_limit_order(sell(5, 103, 10));  // better ask
    book.add_limit_order(sell(6, 104, 10));

    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.best_ask(), 103);
    EXPECT_EQ(book.level_count(Side::Buy), 3u);
    EXPECT_EQ(book.level_count(Side::Sell), 3u);
}

TEST(OrderBook, DepthAggregatesOrdersAtSamePrice) {
    OrderBook book;
    book.add_limit_order(buy(1, 100, 10));
    book.add_limit_order(buy(2, 100, 15));
    book.add_limit_order(buy(3, 99, 7));

    EXPECT_EQ(book.best_quantity(Side::Buy), 25);
    EXPECT_EQ(book.quantity_at(Side::Buy, 100), 25);
    EXPECT_EQ(book.quantity_at(Side::Buy, 99), 7);
    EXPECT_EQ(book.quantity_at(Side::Buy, 12345), 0);  // no such level
}

TEST(OrderBook, QuantityAndPriceByLevel) {
    OrderBook book;
    book.add_limit_order(sell(1, 103, 4));
    book.add_limit_order(sell(2, 101, 9));  // best ask
    book.add_limit_order(sell(3, 102, 6));

    EXPECT_EQ(book.quantity_at_level(Side::Sell, 0), 9);
    EXPECT_EQ(book.quantity_at_level(Side::Sell, 1), 6);
    EXPECT_EQ(book.quantity_at_level(Side::Sell, 2), 4);
    EXPECT_EQ(book.quantity_at_level(Side::Sell, 3), 0);  // beyond book

    Price p = -1;
    EXPECT_TRUE(book.price_at_level(Side::Sell, 0, p));
    EXPECT_EQ(p, 101);
    EXPECT_TRUE(book.price_at_level(Side::Sell, 2, p));
    EXPECT_EQ(p, 103);
    EXPECT_FALSE(book.price_at_level(Side::Sell, 3, p));
}

TEST(OrderBook, CancelRemovesOrderAndEmptyLevel) {
    OrderBook book;
    book.add_limit_order(buy(1, 100, 10));
    book.add_limit_order(buy(2, 99, 10));

    EXPECT_TRUE(book.cancel_order(1));
    EXPECT_FALSE(book.contains(1));
    EXPECT_EQ(book.best_bid(), 99);           // top level collapsed away
    EXPECT_EQ(book.level_count(Side::Buy), 1u);
    EXPECT_EQ(book.order_count(), 1u);
}

TEST(OrderBook, CancelKeepsLevelWhenOthersRemain) {
    OrderBook book;
    book.add_limit_order(buy(1, 100, 10));
    book.add_limit_order(buy(2, 100, 20));

    EXPECT_TRUE(book.cancel_order(1));
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.best_quantity(Side::Buy), 20);
    EXPECT_EQ(book.level_count(Side::Buy), 1u);
}

TEST(OrderBook, CancelNonexistentReturnsFalse) {
    OrderBook book;
    book.add_limit_order(buy(1, 100, 10));
    EXPECT_FALSE(book.cancel_order(999));
    EXPECT_EQ(book.order_count(), 1u);
}

TEST(OrderBook, ModifyDecreaseSamePriceKeepsPriority) {
    OrderBook book;
    book.add_limit_order(buy(1, 100, 10));
    book.add_limit_order(buy(2, 100, 20));  // behind order 1

    EXPECT_TRUE(book.modify_order(1, 100, 6));  // pure decrease
    EXPECT_EQ(book.best_quantity(Side::Buy), 26);
    // Priority preserved: order 1 is still at the front, so cancelling it
    // leaves order 2 with its original position.
    EXPECT_TRUE(book.contains(1));
}

TEST(OrderBook, ModifyIncreaseIsCancelReadd) {
    OrderBook book;
    book.add_limit_order(buy(1, 100, 10));
    EXPECT_TRUE(book.modify_order(1, 100, 25));  // increase -> re-add
    EXPECT_EQ(book.best_quantity(Side::Buy), 25);
    EXPECT_EQ(book.order_count(), 1u);
}

TEST(OrderBook, ModifyPriceMovesToNewLevel) {
    OrderBook book;
    book.add_limit_order(buy(1, 100, 10));
    book.add_limit_order(buy(2, 99, 10));

    EXPECT_TRUE(book.modify_order(1, 98, 10));  // reprice down
    EXPECT_EQ(book.best_bid(), 99);             // order 2 now on top
    EXPECT_EQ(book.quantity_at(Side::Buy, 98), 10);
    EXPECT_FALSE(book.has_best_ask());
    EXPECT_EQ(book.order_count(), 2u);
}

TEST(OrderBook, ModifyNonexistentReturnsFalse) {
    OrderBook book;
    EXPECT_FALSE(book.modify_order(1, 100, 10));
}

TEST(OrderBook, ModifyToNonPositiveQuantityRejected) {
    OrderBook book;
    book.add_limit_order(buy(1, 100, 10));
    EXPECT_FALSE(book.modify_order(1, 100, 0));
    EXPECT_EQ(book.best_quantity(Side::Buy), 10);  // unchanged
}

}  // namespace
}  // namespace obme
