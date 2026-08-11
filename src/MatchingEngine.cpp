#include "obme/MatchingEngine.hpp"

#include <algorithm>
#include <cassert>

namespace obme {

Quantity MatchingEngine::match(const Order& order, std::vector<Trade>& out) {
    Quantity remaining = order.quantity;
    const Side resting_side = opposite(order.side);
    const bool is_market = order.type == OrderType::Market;

    // Does `resting_price` cross the aggressor's limit? Market orders always do.
    auto crosses = [&](Price resting_price) {
        if (is_market) return true;
        return order.side == Side::Buy ? resting_price <= order.price
                                       : resting_price >= order.price;
    };

    while (remaining > 0) {
        const bool has_liquidity =
            resting_side == Side::Sell ? book_.has_best_ask() : book_.has_best_bid();
        if (!has_liquidity) break;

        const Price best = resting_side == Side::Sell ? book_.best_ask() : book_.best_bid();
        if (!crosses(best)) break;

        // Copy the front resting order before we mutate the book.
        const Order resting = book_.best_order(resting_side);
        const Quantity fill = std::min(remaining, resting.quantity);

        out.push_back(Trade{order.id, resting.id, resting.price, fill, order.timestamp,
                            order.side});
        executed_volume_ += fill;
        remaining -= fill;

        if (fill == resting.quantity) {
            book_.cancel_order(resting.id);  // fully consumed
        } else {
            // Partial fill of the resting order: reduce in place, keeping its
            // time priority at the front of the queue.
            book_.modify_order(resting.id, resting.price, resting.quantity - fill);
        }
    }
    return remaining;
}

std::vector<Trade> MatchingEngine::submit(const Order& order) {
    assert(!book_.contains(order.id) && "submitting an id already resting in the book");
    assert(order.quantity > 0 && "order quantity must be positive");

    std::vector<Trade> trades;
    const Quantity remaining = match(order, trades);

    // A limit order rests its unfilled remainder; a market order does not.
    if (remaining > 0 && order.type == OrderType::Limit) {
        Order rest = order;
        rest.quantity = remaining;
        book_.add_limit_order(rest);
    }
    return trades;
}

bool MatchingEngine::cancel(OrderId id) { return book_.cancel_order(id); }

std::vector<Trade> MatchingEngine::modify(OrderId id, Price new_price,
                                          Quantity new_quantity) {
    if (new_quantity <= 0) {
        return {};  // a modify to non-positive size is a cancel; caller decides
    }
    const Order* current = book_.find(id);
    if (current == nullptr) {
        return {};  // nothing to modify
    }

    // Fast path: a same-price decrease keeps time priority and cannot cross, so
    // apply it in place with no matching.
    if (new_price == current->price && new_quantity <= current->quantity) {
        book_.modify_order(id, new_price, new_quantity);
        return {};
    }

    // Slow path: a reprice or size increase is a cancel + fresh submit, so a
    // reprice that now crosses the book will execute. Snapshot first — `current`
    // dangles once we cancel.
    Order replacement = *current;
    replacement.price = new_price;
    replacement.quantity = new_quantity;
    replacement.type = OrderType::Limit;
    book_.cancel_order(id);
    return submit(replacement);
}

}  // namespace obme
