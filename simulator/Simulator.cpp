#include "Simulator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ostream>

#include "obme/MatchingEngine.hpp"
#include "obme/OrderBook.hpp"
#include "obme/Trade.hpp"

namespace obme {
namespace sim {

namespace {

constexpr Timestamp kNever = std::numeric_limits<Timestamp>::max();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Our passive quote living alongside the market (does not perturb it).
struct ShadowOrder {
    Price price{0};
    Quantity remaining{0};
    Quantity ahead{0};             // FIFO volume ahead, snapshot when it goes live
    Timestamp live_time{0};        // fillable once now >= live_time
    Timestamp cancel_time{kNever}; // removed once now >= cancel_time
    bool live{false};
};

double mid_of(const OrderBook& book) {
    if (book.has_best_bid() && book.has_best_ask()) {
        return (static_cast<double>(book.best_bid()) + book.best_ask()) / 2.0;
    }
    return kNaN;
}

}  // namespace

const char* Simulator::fills_header() noexcept {
    return "fill_id,time,side,price,qty,mid_at_fill,mid_at_horizon,markout";
}
const char* Simulator::pnl_header() noexcept {
    return "time,cash,inventory,mid,equity";
}

SimStats Simulator::run(std::vector<Event> events, std::ostream* fills_out,
                        std::ostream* pnl_out) {
    std::stable_sort(events.begin(), events.end(),
                     [](const Event& a, const Event& b) {
                         return a.timestamp < b.timestamp;
                     });

    MatchingEngine engine;
    std::vector<ShadowOrder> bids;
    std::vector<ShadowOrder> asks;
    Decision target;  // quotes we have already acted on (start: none)

    double cash = 0.0;
    Quantity inventory = 0;
    double last_mid = kNaN;
    double total_markout = 0.0;

    // Pending markout samples: (fill index, time at which to sample the mid).
    std::vector<std::pair<std::size_t, Timestamp>> pending_markouts;

    if (pnl_out != nullptr) *pnl_out << pnl_header() << '\n';

    auto activate = [&](std::vector<ShadowOrder>& side_orders, Side side, Timestamp now) {
        for (ShadowOrder& o : side_orders) {
            if (!o.live && now >= o.live_time) {
                o.live = true;
                o.ahead = engine.book().quantity_at(side, o.price);  // queue snapshot
            }
        }
    };
    auto drop_cancelled = [](std::vector<ShadowOrder>& side_orders, Timestamp now) {
        side_orders.erase(
            std::remove_if(side_orders.begin(), side_orders.end(),
                           [&](const ShadowOrder& o) { return now >= o.cancel_time; }),
            side_orders.end());
    };
    auto resolve_markouts = [&](Timestamp now) {
        if (std::isnan(last_mid)) return;
        pending_markouts.erase(
            std::remove_if(
                pending_markouts.begin(), pending_markouts.end(),
                [&](const std::pair<std::size_t, Timestamp>& pm) {
                    if (pm.second > now) return false;
                    Fill& f = fills_[pm.first];
                    f.mid_at_horizon = last_mid;
                    f.markout = (f.side == Side::Buy) ? (last_mid - f.price)
                                                      : (f.price - last_mid);
                    total_markout += f.markout;
                    return true;
                }),
            pending_markouts.end());
    };

    // Fills a market trade of `qty` at `price` against our live orders on `side`.
    auto fill_side = [&](std::vector<ShadowOrder>& side_orders, Side side, Price price,
                         Quantity qty, Timestamp now, double mid_now) {
        Quantity q = qty;
        for (ShadowOrder& o : side_orders) {
            if (q <= 0) break;
            if (!o.live || o.remaining <= 0 || o.price != price) continue;
            if (now >= o.cancel_time) continue;  // cancel already effective
            const Quantity consume = std::min(q, o.ahead);  // clear queue ahead first
            o.ahead -= consume;
            q -= consume;
            if (q <= 0) break;
            const Quantity f = std::min(q, o.remaining);
            o.remaining -= f;
            q -= f;

            Fill fl;
            fl.time = now;
            fl.side = side;
            fl.price = price;
            fl.qty = f;
            fl.mid_at_fill = mid_now;
            fl.mid_at_horizon = kNaN;
            fl.markout = 0.0;
            fills_.push_back(fl);
            our_fills_at_price_[price] += f;
            pending_markouts.emplace_back(fills_.size() - 1, now + cfg_.markout_horizon);

            if (side == Side::Buy) {
                cash -= static_cast<double>(f) * price;
                inventory += f;
            } else {
                cash += static_cast<double>(f) * price;
                inventory -= f;
            }
        }
    };

    auto reconcile = [&](std::vector<ShadowOrder>& side_orders, const Quote& old_q,
                         const Quote& new_q, Timestamp now) {
        if (new_q == old_q) return;  // target unchanged -> no churn
        for (ShadowOrder& o : side_orders) {
            if (o.cancel_time == kNever) o.cancel_time = now + cfg_.latency;
        }
        if (new_q.active) {
            ShadowOrder o;
            o.price = new_q.price;
            o.remaining = new_q.qty;
            o.live_time = now + cfg_.latency;
            side_orders.push_back(o);
        }
    };

    for (const Event& ev : events) {
        const Timestamp now = ev.timestamp;

        // (1) Advance time to `now`: latency transitions and markout sampling
        // happen before this event is applied, using only past information.
        activate(bids, Side::Buy, now);
        activate(asks, Side::Sell, now);
        drop_cancelled(bids, now);
        drop_cancelled(asks, now);
        resolve_markouts(now);

        // (2) Apply the market event to the real engine.
        std::vector<Trade> trades;
        switch (ev.type) {
            case EventType::Add:
                trades = engine.submit(Order{ev.order_id, ev.side, ev.price, ev.quantity,
                                             ev.timestamp, OrderType::Limit});
                break;
            case EventType::Market:
                trades = engine.submit(Order{ev.order_id, ev.side, 0, ev.quantity,
                                             ev.timestamp, OrderType::Market});
                break;
            case EventType::Cancel:
                engine.cancel(ev.order_id);
                break;
            case EventType::Modify:
                trades = engine.modify(ev.order_id, ev.price, ev.quantity);
                break;
        }

        // (3) Process executions: tally traded volume, feed flow, fill our quotes.
        const double post_mid = mid_of(engine.book());
        for (const Trade& t : trades) {
            traded_at_price_[t.price] += t.quantity;
            strategy_.on_trade(t, now);
            if (t.aggressor_side == Side::Sell) {
                fill_side(bids, Side::Buy, t.price, t.quantity, now, post_mid);
            } else {
                fill_side(asks, Side::Sell, t.price, t.quantity, now, post_mid);
            }
        }
        if (!std::isnan(post_mid)) last_mid = post_mid;

        // (4) Strategy observes the post-event state and decides (causally).
        MarketState st;
        st.time = now;
        st.has_bid = engine.book().has_best_bid();
        st.has_ask = engine.book().has_best_ask();
        if (st.has_bid) {
            st.bid_px = engine.book().best_bid();
            st.bid_qty = engine.book().best_quantity(Side::Buy);
        }
        if (st.has_ask) {
            st.ask_px = engine.book().best_ask();
            st.ask_qty = engine.book().best_quantity(Side::Sell);
        }
        st.mid = post_mid;

        const Decision d = strategy_.decide(st);
        decisions_.emplace_back(now, d);

        // (5) Act on any change in desired quotes, subject to latency.
        reconcile(bids, target.bid, d.bid, now);
        reconcile(asks, target.ask, d.ask, now);
        target = d;

        // (6) Mark-to-market PnL point.
        if (pnl_out != nullptr) {
            *pnl_out << now << ',' << cash << ',' << inventory << ',';
            if (std::isnan(post_mid)) {
                *pnl_out << ',';
            } else {
                *pnl_out << post_mid << ',' << (cash + inventory * post_mid);
            }
            *pnl_out << '\n';
        }
    }

    // Resolve any markouts still pending at end of data with the last known mid.
    if (!std::isnan(last_mid)) {
        for (const auto& pm : pending_markouts) {
            Fill& f = fills_[pm.first];
            f.mid_at_horizon = last_mid;
            f.markout = (f.side == Side::Buy) ? (last_mid - f.price) : (f.price - last_mid);
            total_markout += f.markout;
        }
    }

    if (fills_out != nullptr) {
        *fills_out << fills_header() << '\n';
        for (std::size_t i = 0; i < fills_.size(); ++i) {
            const Fill& f = fills_[i];
            *fills_out << i << ',' << f.time << ',' << (f.side == Side::Buy ? 'B' : 'S')
                       << ',' << f.price << ',' << f.qty << ',' << f.mid_at_fill << ',';
            if (std::isnan(f.mid_at_horizon)) {
                *fills_out << ',';
            } else {
                *fills_out << f.mid_at_horizon << ',' << f.markout;
            }
            *fills_out << '\n';
        }
    }

    SimStats stats;
    stats.events = events.size();
    stats.fills = fills_.size();
    for (const Fill& f : fills_) {
        if (f.side == Side::Buy) stats.filled_buy += f.qty;
        else stats.filled_sell += f.qty;
    }
    stats.final_cash = cash;
    stats.final_inventory = inventory;
    stats.final_mid = last_mid;
    stats.final_equity =
        std::isnan(last_mid) ? cash : cash + inventory * last_mid;
    stats.total_markout = total_markout;
    return stats;
}

}  // namespace sim
}  // namespace obme
