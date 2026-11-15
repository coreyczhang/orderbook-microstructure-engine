#pragma once

#include <limits>

#include "obme/Order.hpp"
#include "obme/Trade.hpp"

namespace obme {
namespace sim {

/// A single desired quote on one side. `active == false` means "no quote here".
struct Quote {
    bool active{false};
    Price price{0};
    Quantity qty{0};
};

inline bool operator==(const Quote& a, const Quote& b) {
    return a.active == b.active && a.price == b.price && a.qty == b.qty;
}
inline bool operator!=(const Quote& a, const Quote& b) { return !(a == b); }

/// The strategy's desired quotes for this instant (a target, not an order id).
/// The simulator diffs this against what is currently resting and issues the
/// necessary place/cancel actions (subject to latency).
struct Decision {
    Quote bid;
    Quote ask;
};

inline bool operator==(const Decision& a, const Decision& b) {
    return a.bid == b.bid && a.ask == b.ask;
}
inline bool operator!=(const Decision& a, const Decision& b) { return !(a == b); }

/// Everything the strategy may observe at the current timestamp. It deliberately
/// contains **only** top-of-book state as of `time`; a strategy that also needs
/// trade flow accumulates it causally through `on_trade` (trades up to `time`).
struct MarketState {
    Timestamp time{0};
    bool has_bid{false};
    bool has_ask{false};
    Price bid_px{0};
    Price ask_px{0};
    Quantity bid_qty{0};
    Quantity ask_qty{0};
    double mid{std::numeric_limits<double>::quiet_NaN()};
};

/// A market-making strategy. Implementations see only information at or before
/// the current timestamp, so decisions are a pure function of the past — which
/// is what makes the replay deterministic and free of look-ahead.
///
/// Kept abstract (in its own header) so strategies are swappable; see
/// `FlowImbalanceMM` for the default.
class Strategy {
public:
    virtual ~Strategy() = default;

    /// Observe a market execution (aggressor side + price + qty) at `now`.
    /// Default: ignore. Used by flow-based strategies to build a rolling window.
    virtual void on_trade(const Trade& trade, Timestamp now) {
        (void)trade;
        (void)now;
    }

    /// Decide the desired quotes given the current (causal) market state.
    virtual Decision decide(const MarketState& state) = 0;

    virtual const char* name() const = 0;
};

}  // namespace sim
}  // namespace obme
