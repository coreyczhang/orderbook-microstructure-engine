#include "obme/EventReplay.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

namespace obme {
namespace {

TEST(EventReplay, ParsesWellFormedStreamWithHeader) {
    std::istringstream in(
        "timestamp,event_type,order_id,side,price,quantity\n"
        "1,ADD,10,B,100,5\n"
        "2,ADD,11,S,101,7\n"
        "3,CANCEL,10,B,0,0\n"
        "4,MARKET,12,S,0,3\n"
        "5,MODIFY,11,S,102,4\n");

    const std::vector<Event> events = EventReplay::parse(in);
    ASSERT_EQ(events.size(), 5u);

    EXPECT_EQ(events[0].type, EventType::Add);
    EXPECT_EQ(events[0].order_id, 10u);
    EXPECT_EQ(events[0].side, Side::Buy);
    EXPECT_EQ(events[0].price, 100);
    EXPECT_EQ(events[0].quantity, 5);

    EXPECT_EQ(events[2].type, EventType::Cancel);
    EXPECT_EQ(events[3].type, EventType::Market);
    EXPECT_EQ(events[4].type, EventType::Modify);
    EXPECT_EQ(events[4].price, 102);
}

TEST(EventReplay, ParseSkipsBlankLinesAndToleratesEmptyPrice) {
    std::istringstream in(
        "1,ADD,1,B,100,5\n"
        "\n"
        "2,MARKET,2,S,,3\n");  // empty price on a market order
    const std::vector<Event> events = EventReplay::parse(in);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[1].type, EventType::Market);
    EXPECT_EQ(events[1].price, 0);
    EXPECT_EQ(events[1].quantity, 3);
}

TEST(EventReplay, ParseRejectsBadEventType) {
    std::istringstream in("1,FOO,1,B,100,5\n");
    EXPECT_THROW(EventReplay::parse(in), std::runtime_error);
}

TEST(EventReplay, ParseRejectsBadSide) {
    std::istringstream in("1,ADD,1,X,100,5\n");
    EXPECT_THROW(EventReplay::parse(in), std::runtime_error);
}

TEST(EventReplay, ParseRejectsNonPositiveAddQuantity) {
    std::istringstream in("1,ADD,1,B,100,0\n");
    EXPECT_THROW(EventReplay::parse(in), std::runtime_error);
}

TEST(EventReplay, ParseRejectsTooFewFields) {
    std::istringstream in("1,ADD,1,B\n");
    EXPECT_THROW(EventReplay::parse(in), std::runtime_error);
}

TEST(EventReplay, ReplayReconstructsBookAndTrades) {
    std::istringstream in(
        "timestamp,event_type,order_id,side,price,quantity\n"
        "1,ADD,1,S,100,10\n"
        "2,ADD,2,S,101,10\n"
        "3,ADD,3,B,100,4\n");  // crosses: takes 4 @ 100

    std::vector<Event> events = EventReplay::parse(in);
    MatchingEngine engine;
    std::ostringstream trades, book;
    const EventReplay::Stats stats =
        EventReplay::replay(std::move(events), engine, &trades, &book);

    EXPECT_EQ(stats.events_processed, 3u);
    EXPECT_EQ(stats.trades_generated, 1u);
    EXPECT_EQ(stats.executed_volume, 4);

    // Book state: 6 left @ 100 ask, 10 @ 101 ask, no bids.
    EXPECT_EQ(engine.book().best_ask(), 100);
    EXPECT_EQ(engine.book().best_quantity(Side::Sell), 6);
    EXPECT_FALSE(engine.book().has_best_bid());
    EXPECT_TRUE(engine.book().check_invariants());

    // Output sanity: header + one trade line; header + one snapshot per event.
    const std::string trades_str = trades.str();
    EXPECT_NE(trades_str.find(EventReplay::trades_header()), std::string::npos);
    EXPECT_NE(trades_str.find("100,4,B"), std::string::npos);

    // book.csv should have a header and exactly 3 data rows.
    std::istringstream book_in(book.str());
    std::string line;
    int rows = 0;
    std::getline(book_in, line);  // header
    EXPECT_EQ(line, EventReplay::book_header());
    while (std::getline(book_in, line)) {
        if (!line.empty()) ++rows;
    }
    EXPECT_EQ(rows, 3);
}

TEST(EventReplay, ReplayStableSortsByTimestamp) {
    // Deliberately out-of-order timestamps; replay must process 1 then 2.
    std::istringstream in(
        "2,ADD,2,B,99,5\n"
        "1,ADD,1,B,100,5\n");
    std::vector<Event> events = EventReplay::parse(in);
    MatchingEngine engine;
    EventReplay::replay(std::move(events), engine, nullptr, nullptr);
    // Both rest (non-crossing bids); best bid is the higher price 100.
    EXPECT_EQ(engine.book().best_bid(), 100);
    EXPECT_EQ(engine.book().order_count(), 2u);
}

}  // namespace
}  // namespace obme
