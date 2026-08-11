#pragma once

#include <cstdint>

namespace obme {

/// Order identifier, unique per resting order.
using OrderId = std::uint64_t;

/// Price in integer ticks. Signed so that price arithmetic and sentinels behave
/// predictably; real prices are non-negative.
using Price = std::int64_t;

/// Quantity in shares. Signed (rather than unsigned) so that accidental
/// over-subtraction during matching shows up as a negative value an invariant
/// check can catch, instead of silently wrapping to a huge positive number.
using Quantity = std::int64_t;

/// Timestamp in nanoseconds since an arbitrary epoch. Only ordering matters.
using Timestamp = std::int64_t;

enum class Side { Buy, Sell };

enum class OrderType { Limit, Market };

/// A single order. Value type with plain data members — cheap to copy and store.
struct Order {
    OrderId id{0};
    Side side{Side::Buy};
    Price price{0};              ///< Limit price in ticks; ignored for Market orders.
    Quantity quantity{0};        ///< Remaining shares.
    Timestamp timestamp{0};
    OrderType type{OrderType::Limit};
};

/// Returns the opposite side. Handy for matching logic.
constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

}  // namespace obme
