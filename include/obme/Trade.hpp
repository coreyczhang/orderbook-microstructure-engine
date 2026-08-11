#pragma once

#include "obme/Order.hpp"

namespace obme {

/// A single execution produced when an aggressing order crosses a resting order.
/// The execution price is always the *resting* order's price (price-time
/// priority: the passive order set the price it was willing to trade at).
struct Trade {
    OrderId aggressor_id{0};    ///< the incoming order that crossed the book
    OrderId resting_id{0};      ///< the passive order that was matched
    Price price{0};             ///< execution price (= resting order's price)
    Quantity quantity{0};       ///< shares exchanged
    Timestamp timestamp{0};     ///< aggressor's timestamp
    Side aggressor_side{Side::Buy};
};

}  // namespace obme
