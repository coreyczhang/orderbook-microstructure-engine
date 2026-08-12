#include "obme/OrderBook.hpp"

#include <cassert>

namespace obme {

void OrderBook::insert_order(const Order& order) {
    if (order.side == Side::Buy) {
        auto it = bids_.try_emplace(order.price, order.price).first;
        it->second.add_order(order);
    } else {
        auto it = asks_.try_emplace(order.price, order.price).first;
        it->second.add_order(order);
    }
    locations_.emplace(order.id, Location{order.side, order.price});
}

void OrderBook::add_limit_order(const Order& order) {
    assert(order.type == OrderType::Limit && "add_limit_order requires a Limit order");
    assert(locations_.count(order.id) == 0 && "duplicate order id in book");
    assert(order.quantity > 0 && "order quantity must be positive");
    insert_order(order);
}

void OrderBook::remove_location(OrderId id) { locations_.erase(id); }

const Order* OrderBook::find(OrderId id) const {
    auto loc_it = locations_.find(id);
    if (loc_it == locations_.end()) {
        return nullptr;
    }
    const Location loc = loc_it->second;
    if (loc.side == Side::Buy) {
        auto lvl = bids_.find(loc.price);
        return lvl == bids_.end() ? nullptr : lvl->second.find(id);
    }
    auto lvl = asks_.find(loc.price);
    return lvl == asks_.end() ? nullptr : lvl->second.find(id);
}

bool OrderBook::cancel_order(OrderId id) {
    auto loc_it = locations_.find(id);
    if (loc_it == locations_.end()) {
        return false;
    }
    const Location loc = loc_it->second;

    if (loc.side == Side::Buy) {
        auto lvl = bids_.find(loc.price);
        assert(lvl != bids_.end() && "location index out of sync with book");
        lvl->second.cancel_order(id);
        if (lvl->second.empty()) bids_.erase(lvl);
    } else {
        auto lvl = asks_.find(loc.price);
        assert(lvl != asks_.end() && "location index out of sync with book");
        lvl->second.cancel_order(id);
        if (lvl->second.empty()) asks_.erase(lvl);
    }
    remove_location(id);
    return true;
}

bool OrderBook::modify_order(OrderId id, Price new_price, Quantity new_quantity) {
    auto loc_it = locations_.find(id);
    if (loc_it == locations_.end()) {
        return false;
    }
    if (new_quantity <= 0) {
        return false;  // a modify to non-positive quantity is a cancel; caller
                       // should cancel explicitly.
    }
    const Location loc = loc_it->second;

    // Fetch the existing order so we can preserve its immutable fields on a
    // priority-losing re-add.
    const Order* existing = (loc.side == Side::Buy) ? bids_.at(loc.price).find(id)
                                                    : asks_.at(loc.price).find(id);
    assert(existing != nullptr && "location index out of sync with book");
    const Order original = *existing;

    // Fast path: same price and a pure decrease keeps time priority.
    if (new_price == loc.price && new_quantity <= original.quantity) {
        if (loc.side == Side::Buy) {
            bids_.at(loc.price).reduce_order(id, new_quantity);
        } else {
            asks_.at(loc.price).reduce_order(id, new_quantity);
        }
        return true;
    }

    // Slow path: price change or size increase -> cancel + re-add at the back.
    cancel_order(id);
    Order replacement = original;
    replacement.price = new_price;
    replacement.quantity = new_quantity;
    insert_order(replacement);
    return true;
}

Price OrderBook::best_bid() const {
    assert(!bids_.empty() && "best_bid() on empty bid side");
    return bids_.begin()->first;
}

Price OrderBook::best_ask() const {
    assert(!asks_.empty() && "best_ask() on empty ask side");
    return asks_.begin()->first;
}

const Order& OrderBook::best_order(Side side) const {
    if (side == Side::Buy) {
        assert(!bids_.empty() && "best_order() on empty bid side");
        return bids_.begin()->second.front();
    }
    assert(!asks_.empty() && "best_order() on empty ask side");
    return asks_.begin()->second.front();
}

Quantity OrderBook::best_quantity(Side side) const {
    if (side == Side::Buy) {
        return bids_.empty() ? 0 : bids_.begin()->second.total_quantity();
    }
    return asks_.empty() ? 0 : asks_.begin()->second.total_quantity();
}

Quantity OrderBook::quantity_at(Side side, Price price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it == bids_.end() ? 0 : it->second.total_quantity();
    }
    auto it = asks_.find(price);
    return it == asks_.end() ? 0 : it->second.total_quantity();
}

template <typename MapT>
Quantity OrderBook::level_quantity_from_top(const MapT& m, std::size_t level) {
    if (level >= m.size()) return 0;
    auto it = m.begin();
    std::advance(it, static_cast<typename MapT::difference_type>(level));
    return it->second.total_quantity();
}

template <typename MapT>
bool OrderBook::level_price_from_top(const MapT& m, std::size_t level, Price& out) {
    if (level >= m.size()) return false;
    auto it = m.begin();
    std::advance(it, static_cast<typename MapT::difference_type>(level));
    out = it->first;
    return true;
}

Quantity OrderBook::quantity_at_level(Side side, std::size_t level) const {
    return side == Side::Buy ? level_quantity_from_top(bids_, level)
                             : level_quantity_from_top(asks_, level);
}

bool OrderBook::price_at_level(Side side, std::size_t level, Price& out) const {
    return side == Side::Buy ? level_price_from_top(bids_, level, out)
                             : level_price_from_top(asks_, level, out);
}

Quantity OrderBook::total_shares(Side side) const {
    Quantity total = 0;
    if (side == Side::Buy) {
        for (const auto& entry : bids_) total += entry.second.total_quantity();
    } else {
        for (const auto& entry : asks_) total += entry.second.total_quantity();
    }
    return total;
}

std::vector<Order> OrderBook::snapshot() const {
    std::vector<Order> out;
    out.reserve(locations_.size());
    for (const auto& entry : bids_) {
        for (const Order& o : entry.second.snapshot()) out.push_back(o);
    }
    for (const auto& entry : asks_) {
        for (const Order& o : entry.second.snapshot()) out.push_back(o);
    }
    return out;
}

bool OrderBook::check_invariants() const {
    std::size_t counted = 0;

    auto verify_side = [&](const auto& side_map, Side side) -> bool {
        for (const auto& [key, level] : side_map) {
            if (level.empty()) return false;             // no lingering empty levels
            if (level.price() != key) return false;      // level priced to its key
            Quantity sum = 0;
            for (const Order& o : level.snapshot()) {
                if (o.quantity <= 0) return false;       // positive resting size
                if (o.side != side) return false;        // parked on the right side
                if (o.price != key) return false;        // priced to its level
                auto loc = locations_.find(o.id);
                if (loc == locations_.end()) return false;       // indexed
                if (loc->second.side != side) return false;      // indexed side
                if (loc->second.price != key) return false;      // indexed price
                sum += o.quantity;
                ++counted;
            }
            if (sum != level.total_quantity()) return false;  // cached total is exact
        }
        return true;
    };

    if (!verify_side(bids_, Side::Buy)) return false;
    if (!verify_side(asks_, Side::Sell)) return false;

    // Every indexed id was seen exactly once, and no extras exist.
    return counted == locations_.size();
}

}  // namespace obme
