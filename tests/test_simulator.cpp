#include "Simulator.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "FlowImbalanceMM.hpp"
#include "Strategy.hpp"
#include "obme/EventReplay.hpp"

namespace obme {
namespace sim {
namespace {

Event mk(Timestamp t, EventType type, OrderId id, Side side, Price px, Quantity qty) {
    Event e;
    e.timestamp = t;
    e.type = type;
    e.order_id = id;
    e.side = side;
    e.price = px;
    e.quantity = qty;
    return e;
}

// Deterministic synthetic market: adds on both sides around a mid, cancels, and
// market orders (which generate executions the simulator can fill against).
std::vector<Event> make_stream(std::uint64_t seed, int n, Timestamp start_ts = 0,
                               OrderId start_id = 1) {
    std::mt19937_64 rng(seed);
    std::vector<Event> evs;
    std::vector<OrderId> live;
    OrderId next = start_id;
    Timestamp t = start_ts;
    for (int i = 0; i < n; ++i) {
        t += 1'000'000;  // 1 ms steps
        const int a = static_cast<int>(rng() % 100);
        const Side side = (rng() % 2) ? Side::Buy : Side::Sell;
        if (a < 55 || live.empty()) {
            const Price px = 1000 + static_cast<Price>(rng() % 11) - 5;  // 995..1005
            const Quantity q = 1 + static_cast<Quantity>(rng() % 10);
            evs.push_back(mk(t, EventType::Add, next, side, px, q));
            live.push_back(next++);
        } else if (a < 80) {
            const Quantity q = 1 + static_cast<Quantity>(rng() % 6);
            evs.push_back(mk(t, EventType::Market, next++, side, 0, q));
        } else {
            const std::size_t k = rng() % live.size();
            evs.push_back(mk(t, EventType::Cancel, live[k], side, 0, 0));
            live[k] = live.back();
            live.pop_back();
        }
    }
    return evs;
}

// Test-only strategy: always quote at the touch on both sides. Decoupled from
// the gating logic so it reliably rests orders and exercises the fill model.
class AlwaysAtTouch : public Strategy {
public:
    explicit AlwaysAtTouch(Quantity size) : size_(size) {}
    Decision decide(const MarketState& s) override {
        Decision d;
        if (s.has_bid) d.bid = Quote{true, s.bid_px, size_};
        if (s.has_ask) d.ask = Quote{true, s.ask_px, size_};
        return d;
    }
    const char* name() const override { return "AlwaysAtTouch"; }

private:
    Quantity size_;
};

// ---- Requirement 4: FIFO queue — fills never exceed volume traded at a price.
TEST(Simulator, FilledQtyNeverExceedsTradedVolumeAtPrice) {
    AlwaysAtTouch strat(10);
    SimConfig cfg;
    cfg.latency = 0;  // isolate the queue model from latency
    Simulator sim(strat, cfg);
    sim.run(make_stream(/*seed=*/42, /*n=*/4000));

    Quantity our_total = 0;
    for (const auto& [price, filled] : sim.our_fills_at_price()) {
        const auto it = sim.traded_at_price().find(price);
        const Quantity traded = it == sim.traded_at_price().end() ? 0 : it->second;
        EXPECT_LE(filled, traded) << "overfill at price " << price;
        our_total += filled;
    }
    EXPECT_GT(our_total, 0) << "test is vacuous if nothing ever filled";
}

// ---- Requirement 1: decisions are causal — changing the future never changes
// a decision already made.
TEST(Simulator, FutureEventsDoNotChangePastDecisions) {
    const std::vector<Event> base = make_stream(/*seed=*/7, /*n=*/500);
    const std::size_t k0 = 320;
    const Timestamp split_ts = base[k0].timestamp;

    // Modified stream: identical before split_ts, entirely different afterward.
    std::vector<Event> modified(base.begin(), base.begin() + k0);
    const std::vector<Event> tail =
        make_stream(/*seed=*/999, /*n=*/300, /*start_ts=*/split_ts - 1'000'000,
                    /*start_id=*/1'000'000);
    modified.insert(modified.end(), tail.begin(), tail.end());

    FlowImbalanceMM s1;
    Simulator sim1(s1, {});
    sim1.run(base);
    FlowImbalanceMM s2;
    Simulator sim2(s2, {});
    sim2.run(modified);

    auto prefix = [split_ts](const std::vector<std::pair<Timestamp, Decision>>& d) {
        std::vector<std::pair<Timestamp, Decision>> out;
        for (const auto& e : d) {
            if (e.first < split_ts) out.push_back(e);
        }
        return out;
    };
    const auto p1 = prefix(sim1.decisions());
    const auto p2 = prefix(sim2.decisions());

    ASSERT_EQ(p1.size(), p2.size());
    for (std::size_t i = 0; i < p1.size(); ++i) {
        EXPECT_EQ(p1[i].first, p2[i].first) << "timestamp mismatch at " << i;
        EXPECT_EQ(p1[i].second, p2[i].second) << "decision changed at index " << i;
    }
    EXPECT_GT(p1.size(), 0u);
}

// ---- Requirement 3: latency — an order is not live until its delay has passed,
// so a trade before that delay does not fill it.
TEST(Simulator, LatencyGatesLiveness) {
    // t=0 resting bid (2 @ 1000); t=2ms more depth (50 @ 1000); t=3ms big sell.
    const std::vector<Event> evs = {
        mk(0, EventType::Add, 1, Side::Buy, 1000, 2),
        mk(2'000'000, EventType::Add, 2, Side::Buy, 1000, 50),
        mk(3'000'000, EventType::Market, 3, Side::Sell, 0, 100),
    };

    // Our bid is decided at t=0, so it is live at latency ns.
    {
        AlwaysAtTouch strat(10);
        SimConfig cfg;
        cfg.latency = 1'000'000;  // live at 1 ms, before the 3 ms sell -> fills
        Simulator sim(strat, cfg);
        sim.run(evs);
        Quantity filled = 0;
        for (const auto& [p, q] : sim.our_fills_at_price()) filled += q;
        EXPECT_EQ(filled, 10);
    }
    {
        AlwaysAtTouch strat(10);
        SimConfig cfg;
        cfg.latency = 5'000'000;  // live at 5 ms, after the 3 ms sell -> no fill
        Simulator sim(strat, cfg);
        sim.run(evs);
        Quantity filled = 0;
        for (const auto& [p, q] : sim.our_fills_at_price()) filled += q;
        EXPECT_EQ(filled, 0);
    }
}

// ---- Adverse-selection markout is recorded and resolved for fills.
TEST(Simulator, FillsCarryResolvedMarkout) {
    AlwaysAtTouch strat(10);
    SimConfig cfg;
    cfg.latency = 0;
    cfg.markout_horizon = 5'000'000;  // 5 ms
    Simulator sim(strat, cfg);
    const SimStats stats = sim.run(make_stream(/*seed=*/11, /*n=*/4000));

    ASSERT_GT(sim.fills().size(), 0u);
    bool any_resolved = false;
    for (const Fill& f : sim.fills()) {
        EXPECT_GT(f.qty, 0);
        if (!std::isnan(f.mid_at_horizon)) any_resolved = true;
    }
    EXPECT_TRUE(any_resolved) << "no fill ever had its markout resolved";
    EXPECT_TRUE(std::isfinite(stats.total_markout));
}

}  // namespace
}  // namespace sim
}  // namespace obme
