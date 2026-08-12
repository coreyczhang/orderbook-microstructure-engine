#pragma once

#include <cstddef>
#include <limits>
#include <vector>

#include "obme/Order.hpp"

namespace obme {

/// Computes **Order Flow Imbalance (OFI)** from top-of-book updates, following
/// the framework of Cont, Kukanov & Stoikov, *"The Price Impact of Order Book
/// Events"* (2014), extended to the top `N` price levels ("multi-level" /
/// "integrated" OFI, e.g. Cont, Cucuringu & Xu 2023).
///
/// For each level `m` with best-bid-side `(Pb_m, qb_m)` and best-ask-side
/// `(Pa_m, qa_m)`, the per-level contribution is `e_m = e^b_m − e^a_m` where
///
///     e^b_m =  qb_m · 1{Pb_m ≥ Pb'_m}  −  qb'_m · 1{Pb_m ≤ Pb'_m}
///     e^a_m =  qa_m · 1{Pa_m ≤ Pa'_m}  −  qa'_m · 1{Pa_m ≥ Pa'_m}
///
/// (primed = previous update). `e^b_m` is the net change in depth at the `m`-th
/// bid level and `e^a_m` at the `m`-th ask level, so a positive value is net
/// buying pressure. **Level 0** is the classic best-level (L1) OFI. The
/// **deep** OFI integrates contributions across all tracked levels. A level
/// contributes only when it is present on both sides in both the current and
/// previous snapshot (CKS assumes a two-sided book at each level).
class OrderFlowImbalance {
public:
    explicit OrderFlowImbalance(std::size_t levels = 1) : levels_(levels == 0 ? 1 : levels) {}

    struct Sample {
        double mid{std::numeric_limits<double>::quiet_NaN()};  ///< L1 mid; NaN if one-sided.
        Quantity l1{0};     ///< level-0 (best) OFI; 0 unless `valid`.
        Quantity deep{0};   ///< sum of per-level OFI over tracked levels.
        bool valid{false};  ///< true iff level 0 is two-sided now and previously.
        std::vector<Quantity> per_level;  ///< per-level OFI (size == levels()).
    };

    /// Primary interface. Each vector is best-first and may be shorter than
    /// `levels()` when the book is thin on that side; entries beyond a side's
    /// depth are treated as absent. `*_px[m]` pairs with `*_qty[m]`.
    Sample update(const std::vector<Price>& bid_px, const std::vector<Quantity>& bid_qty,
                  const std::vector<Price>& ask_px, const std::vector<Quantity>& ask_qty);

    /// Convenience L1 overload (single best level).
    Sample update(bool has_bid, Price bid_px, Quantity bid_qty, bool has_ask,
                  Price ask_px, Quantity ask_qty);

    std::size_t levels() const noexcept { return levels_; }
    void reset() noexcept {
        have_prev_ = false;
        prev_bid_px_.clear();
        prev_bid_qty_.clear();
        prev_ask_px_.clear();
        prev_ask_qty_.clear();
    }

private:
    std::size_t levels_;
    bool have_prev_{false};
    std::vector<Price> prev_bid_px_;
    std::vector<Quantity> prev_bid_qty_;
    std::vector<Price> prev_ask_px_;
    std::vector<Quantity> prev_ask_qty_;
};

}  // namespace obme
