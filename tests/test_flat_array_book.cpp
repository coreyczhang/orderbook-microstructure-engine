#include "obme/FlatArrayBook.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

#include "obme/OrderBook.hpp"

namespace obme {
namespace {

// Every observable quantity should agree between the two implementations.
template <typename A, typename B>
void expect_same_state(const A& a, const B& b, int op) {
    ASSERT_EQ(a.order_count(), b.order_count()) << "order_count at op " << op;
    ASSERT_EQ(a.has_best_bid(), b.has_best_bid()) << "has_best_bid at op " << op;
    ASSERT_EQ(a.has_best_ask(), b.has_best_ask()) << "has_best_ask at op " << op;
    if (a.has_best_bid()) {
        EXPECT_EQ(a.best_bid(), b.best_bid()) << "best_bid at op " << op;
        EXPECT_EQ(a.best_quantity(Side::Buy), b.best_quantity(Side::Buy))
            << "best bid qty at op " << op;
    }
    if (a.has_best_ask()) {
        EXPECT_EQ(a.best_ask(), b.best_ask()) << "best_ask at op " << op;
        EXPECT_EQ(a.best_quantity(Side::Sell), b.best_quantity(Side::Sell))
            << "best ask qty at op " << op;
    }
    EXPECT_EQ(a.total_shares(Side::Buy), b.total_shares(Side::Buy))
        << "bid shares at op " << op;
    EXPECT_EQ(a.total_shares(Side::Sell), b.total_shares(Side::Sell))
        << "ask shares at op " << op;
}

TEST(FlatArrayBook, BasicOps) {
    FlatArrayBook book(1, 1000);
    book.add_limit_order(Order{1, Side::Buy, 100, 10, 0, OrderType::Limit});
    book.add_limit_order(Order{2, Side::Buy, 101, 5, 1, OrderType::Limit});
    book.add_limit_order(Order{3, Side::Sell, 105, 8, 2, OrderType::Limit});

    EXPECT_EQ(book.best_bid(), 101);
    EXPECT_EQ(book.best_ask(), 105);
    EXPECT_EQ(book.best_quantity(Side::Buy), 5);
    EXPECT_EQ(book.total_shares(Side::Buy), 15);

    EXPECT_TRUE(book.cancel_order(2));      // best bid collapses to 100
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.total_shares(Side::Buy), 10);
    EXPECT_FALSE(book.cancel_order(2));     // already gone
}

// Cross-check the flat-array book against the std::map book on a long random
// stream of adds / cancels / modifies. Both are non-matching resting books, so
// identical inputs must yield identical observable state at every step.
TEST(FlatArrayBook, MatchesMapOrderBookOnRandomStream) {
    constexpr Price kMin = 1;
    constexpr Price kMax = 2000;
    std::mt19937_64 rng(0xBEEF);
    std::uniform_int_distribution<int> action(0, 99);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<Price> price_dist(900, 1100);
    std::uniform_int_distribution<Quantity> qty_dist(1, 50);

    OrderBook map_book;
    FlatArrayBook arr_book(kMin, kMax);
    std::vector<OrderId> live;
    OrderId next_id = 1;

    for (int op = 0; op < 30000; ++op) {
        const int a = action(rng);
        if (a < 55 || live.empty()) {  // add
            const Order o{next_id++,
                          side_dist(rng) == 0 ? Side::Buy : Side::Sell,
                          price_dist(rng),
                          qty_dist(rng),
                          op,
                          OrderType::Limit};
            map_book.add_limit_order(o);
            arr_book.add_limit_order(o);
            live.push_back(o.id);
        } else if (a < 80) {  // cancel a random live order
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            const std::size_t k = pick(rng);
            const OrderId id = live[k];
            EXPECT_EQ(map_book.cancel_order(id), arr_book.cancel_order(id));
            live[k] = live.back();
            live.pop_back();
        } else {  // modify a random live order
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            const OrderId id = live[pick(rng)];
            const Price np = price_dist(rng);
            const Quantity nq = qty_dist(rng);
            EXPECT_EQ(map_book.modify_order(id, np, nq),
                      arr_book.modify_order(id, np, nq));
        }
        expect_same_state(arr_book, map_book, op);
    }
}

}  // namespace
}  // namespace obme
