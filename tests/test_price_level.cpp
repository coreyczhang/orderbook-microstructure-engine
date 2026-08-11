#include "obme/PriceLevel.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace obme {
namespace {

// Convenience: build a limit order with the fields that matter for a PriceLevel.
Order limit(OrderId id, Price price, Quantity qty, Timestamp ts = 0) {
    return Order{id, Side::Buy, price, qty, ts, OrderType::Limit};
}

std::vector<OrderId> ids_in_fifo(const PriceLevel& lvl) {
    std::vector<OrderId> ids;
    for (const Order& o : lvl.snapshot()) ids.push_back(o.id);
    return ids;
}

TEST(PriceLevel, StartsEmpty) {
    PriceLevel lvl(100);
    EXPECT_EQ(lvl.price(), 100);
    EXPECT_TRUE(lvl.empty());
    EXPECT_EQ(lvl.size(), 0u);
    EXPECT_EQ(lvl.total_quantity(), 0);
}

TEST(PriceLevel, AddAccumulatesQuantityAndSize) {
    PriceLevel lvl(100);
    lvl.add_order(limit(1, 100, 10));
    lvl.add_order(limit(2, 100, 25));

    EXPECT_FALSE(lvl.empty());
    EXPECT_EQ(lvl.size(), 2u);
    EXPECT_EQ(lvl.total_quantity(), 35);
    EXPECT_TRUE(lvl.contains(1));
    EXPECT_TRUE(lvl.contains(2));
    EXPECT_FALSE(lvl.contains(99));
}

TEST(PriceLevel, PreservesFifoOrder) {
    PriceLevel lvl(100);
    lvl.add_order(limit(1, 100, 10));
    lvl.add_order(limit(2, 100, 10));
    lvl.add_order(limit(3, 100, 10));

    EXPECT_EQ(ids_in_fifo(lvl), (std::vector<OrderId>{1, 2, 3}));
    EXPECT_EQ(lvl.front().id, 1u);
}

TEST(PriceLevel, CancelFrontUpdatesHead) {
    PriceLevel lvl(100);
    lvl.add_order(limit(1, 100, 10));
    lvl.add_order(limit(2, 100, 20));

    EXPECT_TRUE(lvl.cancel_order(1));
    EXPECT_EQ(lvl.front().id, 2u);
    EXPECT_EQ(lvl.total_quantity(), 20);
    EXPECT_EQ(ids_in_fifo(lvl), (std::vector<OrderId>{2}));
}

TEST(PriceLevel, CancelMiddlePreservesNeighbors) {
    PriceLevel lvl(100);
    lvl.add_order(limit(1, 100, 10));
    lvl.add_order(limit(2, 100, 20));
    lvl.add_order(limit(3, 100, 30));

    EXPECT_TRUE(lvl.cancel_order(2));
    EXPECT_EQ(ids_in_fifo(lvl), (std::vector<OrderId>{1, 3}));
    EXPECT_EQ(lvl.total_quantity(), 40);
}

TEST(PriceLevel, CancelTailUpdatesTail) {
    PriceLevel lvl(100);
    lvl.add_order(limit(1, 100, 10));
    lvl.add_order(limit(2, 100, 20));
    EXPECT_TRUE(lvl.cancel_order(2));
    // Adding again should append after the surviving order 1.
    lvl.add_order(limit(3, 100, 5));
    EXPECT_EQ(ids_in_fifo(lvl), (std::vector<OrderId>{1, 3}));
}

TEST(PriceLevel, CancelNonexistentIsNoOp) {
    PriceLevel lvl(100);
    lvl.add_order(limit(1, 100, 10));
    EXPECT_FALSE(lvl.cancel_order(42));
    EXPECT_EQ(lvl.size(), 1u);
    EXPECT_EQ(lvl.total_quantity(), 10);
}

TEST(PriceLevel, CancelAllEmptiesLevel) {
    PriceLevel lvl(100);
    lvl.add_order(limit(1, 100, 10));
    lvl.add_order(limit(2, 100, 20));
    EXPECT_TRUE(lvl.cancel_order(1));
    EXPECT_TRUE(lvl.cancel_order(2));
    EXPECT_TRUE(lvl.empty());
    EXPECT_EQ(lvl.total_quantity(), 0);
}

TEST(PriceLevel, ReduceKeepsPositionAndAdjustsQuantity) {
    PriceLevel lvl(100);
    lvl.add_order(limit(1, 100, 10));
    lvl.add_order(limit(2, 100, 20));

    EXPECT_TRUE(lvl.reduce_order(1, 4));
    EXPECT_EQ(lvl.total_quantity(), 24);
    EXPECT_EQ(lvl.front().id, 1u);       // priority preserved
    EXPECT_EQ(lvl.front().quantity, 4);
    EXPECT_EQ(ids_in_fifo(lvl), (std::vector<OrderId>{1, 2}));
}

TEST(PriceLevel, ReduceRejectsIncreaseOrNonPositive) {
    PriceLevel lvl(100);
    lvl.add_order(limit(1, 100, 10));
    EXPECT_FALSE(lvl.reduce_order(1, 15));  // increase not allowed in place
    EXPECT_FALSE(lvl.reduce_order(1, 0));   // non-positive not allowed
    EXPECT_FALSE(lvl.reduce_order(99, 5));  // missing id
    EXPECT_EQ(lvl.total_quantity(), 10);    // unchanged
}

TEST(PriceLevel, FindReturnsOrderOrNull) {
    PriceLevel lvl(100);
    lvl.add_order(limit(7, 100, 12, 555));
    const Order* found = lvl.find(7);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->quantity, 12);
    EXPECT_EQ(found->timestamp, 555);
    EXPECT_EQ(lvl.find(8), nullptr);
}

TEST(PriceLevel, MoveTransfersContents) {
    PriceLevel lvl(100);
    lvl.add_order(limit(1, 100, 10));
    lvl.add_order(limit(2, 100, 20));

    PriceLevel moved = std::move(lvl);
    EXPECT_EQ(moved.total_quantity(), 30);
    EXPECT_EQ(ids_in_fifo(moved), (std::vector<OrderId>{1, 2}));
    EXPECT_EQ(moved.front().id, 1u);
    // Moved-from level is empty and reusable.
    EXPECT_TRUE(lvl.empty());  // NOLINT(bugprone-use-after-move) — intentional check
    EXPECT_EQ(lvl.total_quantity(), 0);
}

}  // namespace
}  // namespace obme
