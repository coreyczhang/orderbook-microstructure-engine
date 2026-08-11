#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "obme/Order.hpp"
#include "obme/PriceLevel.hpp"

namespace obme {

/// A two-sided limit order book.
///
/// Bids are held in descending price order and asks in ascending price order,
/// so the best price on each side is always `begin()`. An `order_id -> (side,
/// price)` index lets cancel/modify locate an order's price level directly
/// instead of scanning.
///
/// M1 scope: resting-book maintenance only (add / cancel / modify, best-price
/// and depth queries). Crossing/matching is added in M2 by `MatchingEngine`.
class OrderBook {
public:
    OrderBook() = default;

    /// Adds a resting limit order. Does **not** attempt to match, even if the
    /// order would cross — matching is the engine's job (M2). Precondition:
    /// `order.id` is not already in the book and `order.type == Limit`.
    void add_limit_order(const Order& order);

    /// Cancels a resting order by id. Returns true if it existed.
    bool cancel_order(OrderId id);

    /// Modifies a resting order.
    ///   * A pure quantity *decrease* at the same price keeps time priority
    ///     (in-place reduction).
    ///   * A price change, or a quantity *increase*, is a cancel + re-add and
    ///     therefore loses time priority (order moves to the back of the new
    ///     level).
    /// Returns true if the order existed.
    bool modify_order(OrderId id, Price new_price, Quantity new_quantity);

    bool contains(OrderId id) const { return locations_.count(id) != 0; }

    /// Returns a pointer to the resting order with the given id, or nullptr if
    /// it is not in the book. Valid until that order is cancelled/modified or
    /// the book is mutated.
    const Order* find(OrderId id) const;
    std::size_t order_count() const noexcept { return locations_.size(); }
    bool empty(Side side) const { return side_map(side).is_empty; }

    bool has_best_bid() const noexcept { return !bids_.empty(); }
    bool has_best_ask() const noexcept { return !asks_.empty(); }

    /// Best (highest) bid / best (lowest) ask price. Precondition: side non-empty.
    Price best_bid() const;
    Price best_ask() const;

    /// The front-of-queue (oldest, highest-priority) order at the best price on
    /// `side` — i.e. the next order a crossing aggressor would match against.
    /// Precondition: that side is non-empty. Used by the matching engine.
    const Order& best_order(Side side) const;

    /// Structural self-check for tests: every level is non-empty and priced to
    /// its map key, each level's total_quantity equals the sum of its orders'
    /// quantities, every resting order has positive quantity and is registered
    /// in the id index with the right (side, price), and the id index size
    /// matches the number of resting orders. Does NOT check for a crossed book
    /// (a bare OrderBook is allowed to be crossed; keeping it uncrossed is the
    /// matching engine's job). Returns true if all structural invariants hold.
    bool check_invariants() const;

    /// Aggregate resting quantity at the best price on `side` (0 if empty).
    Quantity best_quantity(Side side) const;

    /// Aggregate resting quantity at an explicit price on `side` (0 if none).
    Quantity quantity_at(Side side, Price price) const;

    /// Aggregate quantity at the `level`-th price from the top (0 = best).
    /// Returns 0 if fewer than `level + 1` levels exist.
    Quantity quantity_at_level(Side side, std::size_t level) const;

    /// Price at the `level`-th level from the top. Returns false if it doesn't
    /// exist; otherwise writes the price to `out`.
    bool price_at_level(Side side, std::size_t level, Price& out) const;

    std::size_t level_count(Side side) const noexcept { return side_map(side).size; }

    /// Total resting shares across a side (sum of every level's quantity).
    Quantity total_shares(Side side) const;

    /// All resting orders, bids first (best→worst) then asks (best→worst), each
    /// level in FIFO order. For inspection, book export, and conservation tests.
    std::vector<Order> snapshot() const;

private:
    // Bids: highest price first. Asks: lowest price first.
    using BidMap = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskMap = std::map<Price, PriceLevel, std::less<Price>>;

    struct Location {
        Side side;
        Price price;
    };

    void insert_order(const Order& order);
    void remove_location(OrderId id);

    // Small helpers to treat both sides uniformly where the comparator doesn't
    // matter (membership, emp: iteration order differs, so callers that need
    // ordering go through the typed maps directly).
    template <typename MapT>
    static Quantity level_quantity_from_top(const MapT& m, std::size_t level);
    template <typename MapT>
    static bool level_price_from_top(const MapT& m, std::size_t level, Price& out);

    // Uniform read-only view for membership/emptiness checks.
    struct SideView {
        std::size_t size;
        bool is_empty;
    };
    SideView side_map(Side side) const {
        return side == Side::Buy ? SideView{bids_.size(), bids_.empty()}
                                 : SideView{asks_.size(), asks_.empty()};
    }

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<OrderId, Location> locations_;
};

}  // namespace obme
