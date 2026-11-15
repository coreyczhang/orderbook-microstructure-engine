#pragma once

#include <cstddef>
#include <iosfwd>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Strategy.hpp"
#include "obme/EventReplay.hpp"
#include "obme/Order.hpp"

namespace obme {
namespace sim {

struct SimConfig {
    /// Delay, in event-time units (ns), before a submitted order becomes live
    /// and before a cancellation takes effect. Default 1 ms.
    Timestamp latency{1'000'000};
    /// Horizon, in ns, at which each fill's mid is sampled for the markout /
    /// adverse-selection measure. Default 1 s.
    Timestamp markout_horizon{1'000'000'000};
};

/// One simulated fill of a strategy quote, with its adverse-selection markout.
struct Fill {
    Timestamp time{0};
    Side side{Side::Buy};  ///< side of OUR order (Buy = our bid filled)
    Price price{0};
    Quantity qty{0};
    double mid_at_fill{0.0};
    double mid_at_horizon{0.0};  ///< resolved markout_horizon later (NaN if never)
    double markout{0.0};         ///< signed, maker-favorable: + is good for us
};

struct SimStats {
    std::size_t events{0};
    std::size_t fills{0};
    Quantity filled_buy{0};
    Quantity filled_sell{0};
    double final_cash{0.0};
    Quantity final_inventory{0};
    double final_mid{0.0};
    double final_equity{0.0};  ///< cash + inventory * mid
    double total_markout{0.0}; ///< sum of per-fill markouts (adverse selection)
};

/// Event-driven market-making simulator.
///
/// Replays a market event stream through the real matching engine to get the
/// true book and executions, while the strategy's quotes live as **shadow**
/// orders that do not perturb the market (small-agent counterfactual). Shadow
/// orders obey a configurable latency and a FIFO queue-position fill model: an
/// order fills only after the volume that was ahead of it at its price has
/// traded. Each fill's mid is sampled again `markout_horizon` later to measure
/// adverse selection.
class Simulator {
public:
    Simulator(Strategy& strategy, SimConfig config = {})
        : strategy_(strategy), cfg_(config) {}

    /// Replays `events` (sorted by timestamp). If the streams are non-null,
    /// writes per-fill rows to `fills_out` and a per-event PnL series to
    /// `pnl_out`. Returns summary stats.
    SimStats run(std::vector<Event> events, std::ostream* fills_out = nullptr,
                 std::ostream* pnl_out = nullptr);

    const std::vector<Fill>& fills() const noexcept { return fills_; }
    const std::vector<std::pair<Timestamp, Decision>>& decisions() const noexcept {
        return decisions_;
    }
    /// Volume that traded at each price across the whole run (market executions).
    const std::unordered_map<Price, Quantity>& traded_at_price() const noexcept {
        return traded_at_price_;
    }
    /// Volume OUR shadow orders filled at each price. Invariant: for every
    /// price, this never exceeds `traded_at_price()`.
    const std::unordered_map<Price, Quantity>& our_fills_at_price() const noexcept {
        return our_fills_at_price_;
    }

    static const char* fills_header() noexcept;
    static const char* pnl_header() noexcept;

private:
    Strategy& strategy_;
    SimConfig cfg_;
    std::vector<Fill> fills_;
    std::vector<std::pair<Timestamp, Decision>> decisions_;
    std::unordered_map<Price, Quantity> traded_at_price_;
    std::unordered_map<Price, Quantity> our_fills_at_price_;
};

}  // namespace sim
}  // namespace obme
