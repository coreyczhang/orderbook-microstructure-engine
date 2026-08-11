#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "obme/MatchingEngine.hpp"
#include "obme/Order.hpp"

namespace obme {

enum class EventType { Add, Cancel, Modify, Market };

/// One row of the input event stream (see data/README.md for the CSV schema).
/// For Cancel, only `order_id` matters; for Market, `price` is ignored.
struct Event {
    Timestamp timestamp{0};
    EventType type{EventType::Add};
    OrderId order_id{0};
    Side side{Side::Buy};
    Price price{0};
    Quantity quantity{0};
};

/// Reads a tick-level event stream and replays it through a `MatchingEngine`
/// in timestamp order, reconstructing the book and emitting the resulting
/// trades and top-of-book snapshots.
class EventReplay {
public:
    struct Stats {
        std::size_t events_processed{0};
        std::size_t trades_generated{0};
        Quantity executed_volume{0};
    };

    /// Parses events from `in`. Accepts an optional header line (a first line
    /// beginning with "timestamp"). Blank lines are skipped. Throws
    /// std::runtime_error with a line number on a malformed row.
    static std::vector<Event> parse(std::istream& in);

    /// Replays `events` through `engine` in timestamp order (a stable sort by
    /// timestamp is applied, so equal-timestamp events keep input order).
    ///
    /// If `trades_out` is non-null, each generated trade is written to it as
    /// CSV. If `book_out` is non-null, a top-of-book (L1) snapshot is written
    /// after every event. Both streams receive a header row first. Returns
    /// summary stats.
    static Stats replay(std::vector<Event> events, MatchingEngine& engine,
                        std::ostream* trades_out, std::ostream* book_out);

    /// Column header lines written to each output (also handy for tests).
    static const char* trades_header() noexcept;
    static const char* book_header() noexcept;
};

}  // namespace obme
