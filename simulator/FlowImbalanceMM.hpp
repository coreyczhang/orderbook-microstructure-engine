#pragma once

#include <deque>
#include <tuple>

#include "Strategy.hpp"
#include "obme/Order.hpp"

namespace obme {
namespace sim {

/// Tunable parameters for the flow-imbalance market maker.
///
/// The defaults below are this project's **own clean-room defaults**, chosen for
/// this synthetic demonstration. They are NOT derived from, and do not
/// reproduce, any employer's, internship's, or other non-public source's
/// parameters. Every field is configurable so the strategy can be recalibrated
/// or swapped wholesale.
struct FlowImbalanceParams {
    double flow_min{0.95};        ///< neutral-flow lower bound on buy/sell volume ratio
    double flow_max{1.05};        ///< neutral-flow upper bound
    double book_threshold{1.5};   ///< top-of-book depth ratio that counts as a bias
    double mid_shift{0.25};       ///< quote-center skew, in ticks, toward the bias
    Timestamp trade_window{10'000'000'000};  ///< rolling flow window (10 s in ns)
    Quantity quote_size{10};      ///< size posted on each quoted side
};

/// A market maker that quotes **only** when recent trade flow is roughly
/// balanced (neutral) *and* the resting book shows a clear directional bias —
/// then posts a one-tick-wide quote whose center is skewed by `mid_shift` ticks
/// toward that bias. Flow is accumulated causally over a trailing
/// `trade_window`; nothing in a decision depends on the future.
class FlowImbalanceMM : public Strategy {
public:
    explicit FlowImbalanceMM(FlowImbalanceParams params = {}) : p_(params) {}

    void on_trade(const Trade& trade, Timestamp now) override;
    Decision decide(const MarketState& state) override;
    const char* name() const override { return "FlowImbalanceMM"; }

    const FlowImbalanceParams& params() const noexcept { return p_; }

private:
    void evict_before(Timestamp cutoff);

    FlowImbalanceParams p_;
    // Trailing window of (time, aggressor_side, qty) market executions.
    std::deque<std::tuple<Timestamp, Side, Quantity>> flow_;
    Quantity buy_vol_{0};
    Quantity sell_vol_{0};
};

}  // namespace sim
}  // namespace obme
