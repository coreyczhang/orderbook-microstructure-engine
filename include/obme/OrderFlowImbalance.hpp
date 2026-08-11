#pragma once

#include "obme/Order.hpp"

namespace obme {

/// Computes the per-event **Order Flow Imbalance (OFI)** from top-of-book
/// updates, following the general framework of Cont, Kukanov & Stoikov,
/// *"The Price Impact of Order Book Events"* (2014).
///
/// For each book update `n` with best bid (price `Pb`, size `qb`) and best ask
/// (price `Pa`, size `qa`), the OFI contribution is `e_n = e^b_n - e^a_n` where
///
///     e^b_n =  qb_n · 1{Pb_n ≥ Pb_{n-1}}  −  qb_{n-1} · 1{Pb_n ≤ Pb_{n-1}}
///     e^a_n =  qa_n · 1{Pa_n ≤ Pa_{n-1}}  −  qa_{n-1} · 1{Pa_n ≥ Pa_{n-1}}
///
/// Intuitively `e^b` is the net gain in bid-side depth and `e^a` the net gain in
/// ask-side depth, so a positive OFI reflects net buying pressure. The signal is
/// only defined when both the current and previous snapshots are two-sided; when
/// either side is missing the contribution is reported as invalid (and 0).
class OrderFlowImbalance {
public:
    struct Sample {
        double mid{0.0};    ///< (best_bid + best_ask) / 2; NaN if one-sided.
        Quantity ofi{0};    ///< per-event OFI contribution e_n (0 if !valid).
        bool valid{false};  ///< true iff both this and the prior snapshot are two-sided.
    };

    /// Feeds the top-of-book after one event and returns the OFI sample for it.
    /// `has_bid` / `has_ask` indicate whether each side currently exists; when a
    /// side is absent its price/size arguments are ignored.
    Sample update(bool has_bid, Price bid_px, Quantity bid_qty, bool has_ask,
                  Price ask_px, Quantity ask_qty);

    /// Resets to the initial (no-history) state.
    void reset() noexcept { *this = OrderFlowImbalance{}; }

private:
    bool have_prev_{false};
    bool prev_has_bid_{false};
    bool prev_has_ask_{false};
    Price prev_bid_px_{0};
    Price prev_ask_px_{0};
    Quantity prev_bid_qty_{0};
    Quantity prev_ask_qty_{0};
};

}  // namespace obme
