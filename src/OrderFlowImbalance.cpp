#include "obme/OrderFlowImbalance.hpp"

#include <cstddef>

namespace obme {

namespace {

// CKS bid-side depth change at one level: whole new queue if the price improved
// (rose), the queue delta if unchanged, minus the old queue if it worsened.
Quantity bid_e(Price cur_px, Quantity cur_q, Price prev_px, Quantity prev_q) {
    if (cur_px > prev_px) return cur_q;
    if (cur_px == prev_px) return cur_q - prev_q;
    return -prev_q;
}

// CKS ask-side depth change: symmetric, with "improved" meaning the ask fell.
Quantity ask_e(Price cur_px, Quantity cur_q, Price prev_px, Quantity prev_q) {
    if (cur_px < prev_px) return cur_q;
    if (cur_px == prev_px) return cur_q - prev_q;
    return -prev_q;
}

}  // namespace

OrderFlowImbalance::Sample OrderFlowImbalance::update(const std::vector<Price>& bid_px,
                                                     const std::vector<Quantity>& bid_qty,
                                                     const std::vector<Price>& ask_px,
                                                     const std::vector<Quantity>& ask_qty) {
    Sample s;
    s.per_level.assign(levels_, 0);

    const bool l1_now = !bid_px.empty() && !ask_px.empty();
    if (l1_now) {
        s.mid = (static_cast<double>(bid_px[0]) + ask_px[0]) / 2.0;
    }

    for (std::size_t m = 0; m < levels_; ++m) {
        const bool cur_bid = m < bid_px.size();
        const bool cur_ask = m < ask_px.size();
        const bool prev_bid = m < prev_bid_px_.size();
        const bool prev_ask = m < prev_ask_px_.size();
        if (have_prev_ && cur_bid && cur_ask && prev_bid && prev_ask) {
            const Quantity eb = bid_e(bid_px[m], bid_qty[m], prev_bid_px_[m], prev_bid_qty_[m]);
            const Quantity ea = ask_e(ask_px[m], ask_qty[m], prev_ask_px_[m], prev_ask_qty_[m]);
            s.per_level[m] = eb - ea;
            s.deep += s.per_level[m];
        }
    }

    s.l1 = s.per_level[0];
    // Level 0 must be two-sided in both snapshots for L1 OFI to be meaningful.
    s.valid = have_prev_ && l1_now && !prev_bid_px_.empty() && !prev_ask_px_.empty();

    // Roll current snapshot into "previous".
    have_prev_ = true;
    prev_bid_px_ = bid_px;
    prev_bid_qty_ = bid_qty;
    prev_ask_px_ = ask_px;
    prev_ask_qty_ = ask_qty;

    return s;
}

OrderFlowImbalance::Sample OrderFlowImbalance::update(bool has_bid, Price bid_px,
                                                      Quantity bid_qty, bool has_ask,
                                                      Price ask_px, Quantity ask_qty) {
    std::vector<Price> bpx, apx;
    std::vector<Quantity> bq, aq;
    if (has_bid) {
        bpx.push_back(bid_px);
        bq.push_back(bid_qty);
    }
    if (has_ask) {
        apx.push_back(ask_px);
        aq.push_back(ask_qty);
    }
    return update(bpx, bq, apx, aq);
}

}  // namespace obme
