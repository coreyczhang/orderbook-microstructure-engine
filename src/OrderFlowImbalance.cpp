#include "obme/OrderFlowImbalance.hpp"

#include <limits>

namespace obme {

OrderFlowImbalance::Sample OrderFlowImbalance::update(bool has_bid, Price bid_px,
                                                      Quantity bid_qty, bool has_ask,
                                                      Price ask_px, Quantity ask_qty) {
    Sample s;
    s.mid = (has_bid && has_ask) ? (static_cast<double>(bid_px) + ask_px) / 2.0
                                 : std::numeric_limits<double>::quiet_NaN();

    const bool two_sided_now = has_bid && has_ask;
    const bool two_sided_prev = prev_has_bid_ && prev_has_ask_;
    s.valid = have_prev_ && two_sided_now && two_sided_prev;

    if (s.valid) {
        // Bid-side depth change (CKS e^b).
        Quantity eb;
        if (bid_px > prev_bid_px_) {
            eb = bid_qty;                    // bid improved: whole new queue counts
        } else if (bid_px == prev_bid_px_) {
            eb = bid_qty - prev_bid_qty_;    // same level: net queue change
        } else {
            eb = -prev_bid_qty_;             // bid fell: old queue vacated
        }

        // Ask-side depth change (CKS e^a).
        Quantity ea;
        if (ask_px < prev_ask_px_) {
            ea = ask_qty;                    // ask improved (fell): new queue counts
        } else if (ask_px == prev_ask_px_) {
            ea = ask_qty - prev_ask_qty_;
        } else {
            ea = -prev_ask_qty_;             // ask rose: old queue vacated
        }

        s.ofi = eb - ea;
    }

    // Roll the current snapshot into "previous" for the next event.
    have_prev_ = true;
    prev_has_bid_ = has_bid;
    prev_has_ask_ = has_ask;
    prev_bid_px_ = bid_px;
    prev_ask_px_ = ask_px;
    prev_bid_qty_ = bid_qty;
    prev_ask_qty_ = ask_qty;

    return s;
}

}  // namespace obme
