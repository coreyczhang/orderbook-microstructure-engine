#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "obme/Order.hpp"
#include "obme/PriceLevel.hpp"

namespace obme {

/// A cache-friendly limit order book over a **bounded** integer price range.
///
/// This is the production-style optimization the README flags for `OrderBook`:
/// instead of a `std::map<Price, PriceLevel>` tree (O(log L) lookup, one heap
/// node and pointer-chase per price level), each side is a flat
/// `std::vector<PriceLevel>` indexed directly by `price - min_price`, giving
/// **O(1)** level access with contiguous storage. The best price is tracked as
/// an index and only re-scanned when the current best level empties (typically a
/// short local scan). The trade-off is a fixed, pre-allocated price band.
///
/// The public surface mirrors the subset of `OrderBook` used for matching and
/// tests, so the two are interchangeable and can be checked against each other.
class FlatArrayBook {
public:
    /// Supports resting orders with `min_price <= price <= max_price`.
    FlatArrayBook(Price min_price, Price max_price);

    void add_limit_order(const Order& order);
    bool cancel_order(OrderId id);
    bool modify_order(OrderId id, Price new_price, Quantity new_quantity);

    bool contains(OrderId id) const { return locations_.count(id) != 0; }
    std::size_t order_count() const noexcept { return locations_.size(); }

    bool has_best_bid() const noexcept { return best_bid_idx_ >= 0; }
    bool has_best_ask() const noexcept { return best_ask_idx_ >= 0; }
    Price best_bid() const;
    Price best_ask() const;
    Quantity best_quantity(Side side) const;
    const Order& best_order(Side side) const;

    Quantity total_shares(Side side) const noexcept {
        return side == Side::Buy ? bid_shares_ : ask_shares_;
    }

private:
    struct Location {
        Side side;
        Price price;
    };

    std::size_t index_of(Price price) const {
        return static_cast<std::size_t>(price - min_price_);
    }
    void insert(const Order& order);
    void rescan_best_bid(int from_idx);
    void rescan_best_ask(int from_idx);

    Price min_price_;
    std::vector<PriceLevel> bids_;  // indexed by price - min_price_
    std::vector<PriceLevel> asks_;
    std::unordered_map<OrderId, Location> locations_;
    int best_bid_idx_{-1};  // highest non-empty bid index, -1 if none
    int best_ask_idx_{-1};  // lowest non-empty ask index, -1 if none
    Quantity bid_shares_{0};
    Quantity ask_shares_{0};
};

}  // namespace obme
