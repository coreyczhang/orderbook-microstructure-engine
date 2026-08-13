#include "obme/FlatArrayBook.hpp"

#include <cassert>

namespace obme {

FlatArrayBook::FlatArrayBook(Price min_price, Price max_price) : min_price_(min_price) {
    assert(max_price >= min_price && "empty price range");
    const std::size_t n = static_cast<std::size_t>(max_price - min_price + 1);
    bids_.reserve(n);
    asks_.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Price p = min_price + static_cast<Price>(i);
        bids_.emplace_back(p);
        asks_.emplace_back(p);
    }
}

void FlatArrayBook::insert(const Order& order) {
    assert(index_of(order.price) < bids_.size() &&
           "price outside the book's bounded range");
    const std::size_t idx = index_of(order.price);
    const int iidx = static_cast<int>(idx);
    if (order.side == Side::Buy) {
        bids_[idx].add_order(order);
        bid_shares_ += order.quantity;
        if (iidx > best_bid_idx_) best_bid_idx_ = iidx;
    } else {
        asks_[idx].add_order(order);
        ask_shares_ += order.quantity;
        if (best_ask_idx_ < 0 || iidx < best_ask_idx_) best_ask_idx_ = iidx;
    }
    locations_.emplace(order.id, Location{order.side, order.price});
}

void FlatArrayBook::add_limit_order(const Order& order) {
    assert(order.type == OrderType::Limit && "add_limit_order requires a Limit order");
    assert(locations_.count(order.id) == 0 && "duplicate order id in book");
    assert(order.quantity > 0 && "order quantity must be positive");
    insert(order);
}

void FlatArrayBook::rescan_best_bid(int from_idx) {
    for (int i = from_idx; i >= 0; --i) {
        if (!bids_[static_cast<std::size_t>(i)].empty()) {
            best_bid_idx_ = i;
            return;
        }
    }
    best_bid_idx_ = -1;
}

void FlatArrayBook::rescan_best_ask(int from_idx) {
    const int n = static_cast<int>(asks_.size());
    for (int i = from_idx; i < n; ++i) {
        if (!asks_[static_cast<std::size_t>(i)].empty()) {
            best_ask_idx_ = i;
            return;
        }
    }
    best_ask_idx_ = -1;
}

bool FlatArrayBook::cancel_order(OrderId id) {
    auto it = locations_.find(id);
    if (it == locations_.end()) return false;

    const Location loc = it->second;
    const std::size_t idx = index_of(loc.price);
    const int iidx = static_cast<int>(idx);

    if (loc.side == Side::Buy) {
        const Order* o = bids_[idx].find(id);
        assert(o != nullptr && "location index out of sync");
        bid_shares_ -= o->quantity;
        bids_[idx].cancel_order(id);
        if (bids_[idx].empty() && iidx == best_bid_idx_) rescan_best_bid(iidx - 1);
    } else {
        const Order* o = asks_[idx].find(id);
        assert(o != nullptr && "location index out of sync");
        ask_shares_ -= o->quantity;
        asks_[idx].cancel_order(id);
        if (asks_[idx].empty() && iidx == best_ask_idx_) rescan_best_ask(iidx + 1);
    }
    locations_.erase(it);
    return true;
}

bool FlatArrayBook::modify_order(OrderId id, Price new_price, Quantity new_quantity) {
    auto it = locations_.find(id);
    if (it == locations_.end() || new_quantity <= 0) return false;

    const Location loc = it->second;
    const std::size_t idx = index_of(loc.price);
    auto& level = loc.side == Side::Buy ? bids_[idx] : asks_[idx];
    const Order* existing = level.find(id);
    assert(existing != nullptr && "location index out of sync");
    const Order original = *existing;

    // Same-price decrease keeps time priority (in-place reduction).
    if (new_price == loc.price && new_quantity <= original.quantity) {
        Quantity& shares = loc.side == Side::Buy ? bid_shares_ : ask_shares_;
        shares -= (original.quantity - new_quantity);
        level.reduce_order(id, new_quantity);
        return true;
    }

    // Otherwise: cancel + re-add at the back of the new level.
    cancel_order(id);
    Order replacement = original;
    replacement.price = new_price;
    replacement.quantity = new_quantity;
    insert(replacement);
    return true;
}

Price FlatArrayBook::best_bid() const {
    assert(best_bid_idx_ >= 0 && "best_bid() on empty bid side");
    return min_price_ + static_cast<Price>(best_bid_idx_);
}

Price FlatArrayBook::best_ask() const {
    assert(best_ask_idx_ >= 0 && "best_ask() on empty ask side");
    return min_price_ + static_cast<Price>(best_ask_idx_);
}

Quantity FlatArrayBook::best_quantity(Side side) const {
    if (side == Side::Buy) {
        return best_bid_idx_ < 0
                   ? 0
                   : bids_[static_cast<std::size_t>(best_bid_idx_)].total_quantity();
    }
    return best_ask_idx_ < 0
               ? 0
               : asks_[static_cast<std::size_t>(best_ask_idx_)].total_quantity();
}

const Order& FlatArrayBook::best_order(Side side) const {
    if (side == Side::Buy) {
        assert(best_bid_idx_ >= 0 && "best_order() on empty bid side");
        return bids_[static_cast<std::size_t>(best_bid_idx_)].front();
    }
    assert(best_ask_idx_ >= 0 && "best_order() on empty ask side");
    return asks_[static_cast<std::size_t>(best_ask_idx_)].front();
}

}  // namespace obme
