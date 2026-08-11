#pragma once

#include <vector>

#include "obme/Order.hpp"
#include "obme/OrderBook.hpp"
#include "obme/Trade.hpp"

namespace obme {

/// Processes an incoming order stream against a limit order book using
/// **price-time priority**, generating trades on crossing orders and resting
/// any unfilled remainder of a limit order.
///
/// The engine owns the book; callers inspect it through `book()`.
class MatchingEngine {
public:
    MatchingEngine() = default;

    /// Submits a new order (Limit or Market) as an aggressor.
    ///   * Limit: matches against the opposite side while the price crosses,
    ///     then rests any remainder in the book.
    ///   * Market: matches against the opposite side regardless of price until
    ///     filled or the book is exhausted; never rests (unfilled remainder is
    ///     dropped — there is no price to rest a market order at).
    /// Returns the trades generated, in execution order (best price first,
    /// oldest resting order first). Precondition: `order.id` is not currently
    /// resting in the book.
    std::vector<Trade> submit(const Order& order);

    /// Cancels a resting order by id. Returns true if it existed.
    bool cancel(OrderId id);

    /// Modifies a resting order, then returns any trades that result.
    ///   * A same-price quantity *decrease* is applied in place (keeps time
    ///     priority) and produces no trades.
    ///   * Any other change (reprice, or size increase) is a cancel followed by
    ///     a fresh `submit`, so a reprice that crosses the book will execute and
    ///     the order loses time priority.
    /// Returns the trades generated (empty for the in-place path or if the id
    /// does not exist).
    std::vector<Trade> modify(OrderId id, Price new_price, Quantity new_quantity);

    const OrderBook& book() const noexcept { return book_; }

    /// Total shares executed across every trade the engine has produced. Useful
    /// for conservation checks.
    Quantity executed_volume() const noexcept { return executed_volume_; }

private:
    /// Core matching loop shared by Limit and Market submits. Fills `order`
    /// against `book_` and appends executions to `out`; returns the unfilled
    /// remaining quantity of `order`.
    Quantity match(const Order& order, std::vector<Trade>& out);

    OrderBook book_;
    Quantity executed_volume_{0};
};

}  // namespace obme
