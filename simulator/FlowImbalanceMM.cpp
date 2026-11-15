#include "FlowImbalanceMM.hpp"

#include <cmath>

namespace obme {
namespace sim {

void FlowImbalanceMM::evict_before(Timestamp cutoff) {
    while (!flow_.empty() && std::get<0>(flow_.front()) < cutoff) {
        const auto& [t, side, qty] = flow_.front();
        (void)t;
        if (side == Side::Buy) {
            buy_vol_ -= qty;
        } else {
            sell_vol_ -= qty;
        }
        flow_.pop_front();
    }
}

void FlowImbalanceMM::on_trade(const Trade& trade, Timestamp now) {
    flow_.emplace_back(now, trade.aggressor_side, trade.quantity);
    if (trade.aggressor_side == Side::Buy) {
        buy_vol_ += trade.quantity;
    } else {
        sell_vol_ += trade.quantity;
    }
    // Keep the window bounded as trades arrive (also re-trimmed in decide()).
    if (now >= p_.trade_window) evict_before(now - p_.trade_window);
}

Decision FlowImbalanceMM::decide(const MarketState& s) {
    // Trim the trailing window to [time - window, time] as of this instant, so
    // flow reflects exactly the trades in the look-back regardless of arrivals.
    if (s.time >= p_.trade_window) evict_before(s.time - p_.trade_window);

    Decision none;  // both sides inactive
    if (!s.has_bid || !s.has_ask || std::isnan(s.mid)) return none;

    // Flow must be present on both sides and roughly balanced (neutral).
    if (buy_vol_ <= 0 || sell_vol_ <= 0) return none;
    const double flow_ratio =
        static_cast<double>(buy_vol_) / static_cast<double>(sell_vol_);
    const bool neutral = flow_ratio >= p_.flow_min && flow_ratio <= p_.flow_max;
    if (!neutral) return none;

    // Book must show a directional bias at the touch.
    if (s.ask_qty <= 0) return none;
    const double imbalance =
        static_cast<double>(s.bid_qty) / static_cast<double>(s.ask_qty);
    int bias = 0;
    if (imbalance >= p_.book_threshold) {
        bias = +1;  // bid-heavy: lean up
    } else if (imbalance <= 1.0 / p_.book_threshold) {
        bias = -1;  // ask-heavy: lean down
    }
    if (bias == 0) return none;

    // Post a one-tick-wide quote whose center is skewed toward the bias.
    const double center = s.mid + bias * p_.mid_shift;
    const Price bid_px = static_cast<Price>(std::floor(center));
    const Price ask_px = bid_px + 1;

    Decision d;
    d.bid = Quote{true, bid_px, p_.quote_size};
    d.ask = Quote{true, ask_px, p_.quote_size};
    return d;
}

}  // namespace sim
}  // namespace obme
