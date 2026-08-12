#include "obme/EventReplay.hpp"

#include <algorithm>
#include <cmath>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "obme/OrderFlowImbalance.hpp"

namespace obme {

namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream ss(line);
    while (std::getline(ss, field, ',')) {
        fields.push_back(trim(field));
    }
    return fields;
}

EventType parse_event_type(const std::string& s, std::size_t line_no) {
    if (s == "ADD") return EventType::Add;
    if (s == "CANCEL") return EventType::Cancel;
    if (s == "MODIFY") return EventType::Modify;
    if (s == "MARKET") return EventType::Market;
    throw std::runtime_error("line " + std::to_string(line_no) +
                             ": unknown event_type '" + s + "'");
}

Side parse_side(const std::string& s, std::size_t line_no) {
    if (s == "B") return Side::Buy;
    if (s == "S") return Side::Sell;
    throw std::runtime_error("line " + std::to_string(line_no) +
                             ": side must be 'B' or 'S', got '" + s + "'");
}

// Parses an integer field, tolerating an empty string as `fallback` (used for
// price on MARKET / cancel rows where it is irrelevant).
long long parse_int(const std::string& s, long long fallback, const char* what,
                    std::size_t line_no) {
    if (s.empty()) return fallback;
    try {
        std::size_t consumed = 0;
        const long long value = std::stoll(s, &consumed);
        if (consumed != s.size()) throw std::invalid_argument("trailing");
        return value;
    } catch (const std::exception&) {
        throw std::runtime_error("line " + std::to_string(line_no) + ": invalid " +
                                 what + " '" + s + "'");
    }
}

char side_char(Side s) { return s == Side::Buy ? 'B' : 'S'; }

// Top-of-book snapshot extracted once per event and reused for both the book
// CSV and the OFI computation.
struct L1 {
    bool has_bid{false};
    Price bid_px{0};
    Quantity bid_qty{0};
    bool has_ask{false};
    Price ask_px{0};
    Quantity ask_qty{0};
};

L1 extract_l1(const OrderBook& book) {
    L1 l1;
    if (book.has_best_bid()) {
        l1.has_bid = true;
        l1.bid_px = book.best_bid();
        l1.bid_qty = book.best_quantity(Side::Buy);
    }
    if (book.has_best_ask()) {
        l1.has_ask = true;
        l1.ask_px = book.best_ask();
        l1.ask_qty = book.best_quantity(Side::Sell);
    }
    return l1;
}

// Extracts up to `n` price levels per side (best first) for the OFI signal.
void extract_levels(const OrderBook& book, std::size_t n, Side side,
                    std::vector<Price>& px, std::vector<Quantity>& qty) {
    px.clear();
    qty.clear();
    for (std::size_t m = 0; m < n; ++m) {
        Price p;
        if (!book.price_at_level(side, m, p)) break;  // book thinner than n
        px.push_back(p);
        qty.push_back(book.quantity_at_level(side, m));
    }
}

// Writes a top-of-book (L1) snapshot; empty fields when a side is absent.
void write_book_row(std::ostream& out, std::size_t seq, Timestamp ts, const L1& l1) {
    out << seq << ',' << ts << ',';
    if (l1.has_bid) out << l1.bid_px << ',' << l1.bid_qty;
    else out << ',';
    out << ',';
    if (l1.has_ask) out << l1.ask_px << ',' << l1.ask_qty;
    else out << ',';
    out << '\n';
}

// Emits the per-event book (L1) and OFI rows for the current book state. Shared
// by the matching replay and the book-only replay.
void emit_snapshots(std::size_t seq, Timestamp ts, const OrderBook& book,
                    OrderFlowImbalance& ofi, std::size_t levels,
                    std::ostream* book_out, std::ostream* ofi_out) {
    if (book_out != nullptr) {
        write_book_row(*book_out, seq, ts, extract_l1(book));
    }
    if (ofi_out != nullptr) {
        std::vector<Price> bid_px, ask_px;
        std::vector<Quantity> bid_qty, ask_qty;
        extract_levels(book, levels, Side::Buy, bid_px, bid_qty);
        extract_levels(book, levels, Side::Sell, ask_px, ask_qty);
        const OrderFlowImbalance::Sample s = ofi.update(bid_px, bid_qty, ask_px, ask_qty);
        *ofi_out << seq << ',' << ts << ',';
        if (std::isnan(s.mid)) *ofi_out << ',';
        else *ofi_out << s.mid << ',';
        *ofi_out << s.l1 << ',' << s.deep << ',' << (s.valid ? 1 : 0) << '\n';
    }
}

}  // namespace

const char* EventReplay::trades_header() noexcept {
    return "seq,timestamp,aggressor_id,resting_id,price,quantity,aggressor_side";
}

const char* EventReplay::book_header() noexcept {
    return "seq,timestamp,bid_px,bid_qty,ask_px,ask_qty";
}

const char* EventReplay::ofi_header() noexcept {
    return "seq,timestamp,mid,ofi,ofi_deep,valid";
}

std::vector<Event> EventReplay::parse(std::istream& in) {
    std::vector<Event> events;
    std::string line;
    std::size_t line_no = 0;
    bool checked_header = false;

    while (std::getline(in, line)) {
        ++line_no;
        const std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        if (!checked_header) {
            checked_header = true;
            // Skip an optional header row.
            if (trimmed.rfind("timestamp", 0) == 0) continue;
        }

        const std::vector<std::string> f = split_csv(trimmed);
        if (f.size() < 6) {
            throw std::runtime_error("line " + std::to_string(line_no) +
                                     ": expected 6 fields, got " +
                                     std::to_string(f.size()));
        }

        Event ev;
        ev.timestamp = parse_int(f[0], 0, "timestamp", line_no);
        ev.type = parse_event_type(f[1], line_no);
        ev.order_id = static_cast<OrderId>(parse_int(f[2], 0, "order_id", line_no));
        ev.side = parse_side(f[3], line_no);
        ev.price = parse_int(f[4], 0, "price", line_no);
        ev.quantity = parse_int(f[5], 0, "quantity", line_no);

        if ((ev.type == EventType::Add || ev.type == EventType::Market ||
             ev.type == EventType::Modify) &&
            ev.quantity <= 0) {
            throw std::runtime_error("line " + std::to_string(line_no) +
                                     ": quantity must be positive for this event");
        }
        events.push_back(ev);
    }
    return events;
}

EventReplay::Stats EventReplay::replay(std::vector<Event> events, MatchingEngine& engine,
                                       std::ostream* trades_out, std::ostream* book_out,
                                       std::ostream* ofi_out, std::size_t ofi_levels) {
    // Stable sort keeps equal-timestamp events in their original arrival order.
    std::stable_sort(events.begin(), events.end(),
                     [](const Event& a, const Event& b) {
                         return a.timestamp < b.timestamp;
                     });

    if (trades_out != nullptr) *trades_out << trades_header() << '\n';
    if (book_out != nullptr) *book_out << book_header() << '\n';
    if (ofi_out != nullptr) *ofi_out << ofi_header() << '\n';

    OrderFlowImbalance ofi(ofi_levels);
    Stats stats;
    for (std::size_t seq = 0; seq < events.size(); ++seq) {
        const Event& ev = events[seq];
        std::vector<Trade> trades;

        switch (ev.type) {
            case EventType::Add:
                trades = engine.submit(
                    Order{ev.order_id, ev.side, ev.price, ev.quantity, ev.timestamp,
                          OrderType::Limit});
                break;
            case EventType::Market:
                trades = engine.submit(
                    Order{ev.order_id, ev.side, 0, ev.quantity, ev.timestamp,
                          OrderType::Market});
                break;
            case EventType::Cancel:
                engine.cancel(ev.order_id);
                break;
            case EventType::Modify:
                trades = engine.modify(ev.order_id, ev.price, ev.quantity);
                break;
        }

        if (trades_out != nullptr) {
            for (const Trade& t : trades) {
                *trades_out << seq << ',' << t.timestamp << ',' << t.aggressor_id << ','
                            << t.resting_id << ',' << t.price << ',' << t.quantity << ','
                            << side_char(t.aggressor_side) << '\n';
            }
        }

        emit_snapshots(seq, ev.timestamp, engine.book(), ofi, ofi_levels, book_out,
                       ofi_out);
        stats.trades_generated += trades.size();
    }
    stats.events_processed = events.size();
    stats.executed_volume = engine.executed_volume();
    return stats;
}

EventReplay::Stats EventReplay::replay_book_only(std::vector<Event> events,
                                                 OrderBook& book, std::ostream* book_out,
                                                 std::ostream* ofi_out,
                                                 std::size_t ofi_levels) {
    std::stable_sort(events.begin(), events.end(),
                     [](const Event& a, const Event& b) {
                         return a.timestamp < b.timestamp;
                     });

    if (book_out != nullptr) *book_out << book_header() << '\n';
    if (ofi_out != nullptr) *ofi_out << ofi_header() << '\n';

    OrderFlowImbalance ofi(ofi_levels);
    Stats stats;
    for (std::size_t seq = 0; seq < events.size(); ++seq) {
        const Event& ev = events[seq];
        // Messages are already matched upstream (e.g. LOBSTER): apply them
        // directly to the book, never crossing/matching. Missing-id cancels and
        // modifies are graceful no-ops, so a no-op event still emits a snapshot
        // and keeps row alignment with an external reference book.
        switch (ev.type) {
            case EventType::Add:
                if (ev.quantity > 0 && !book.contains(ev.order_id)) {
                    book.add_limit_order(Order{ev.order_id, ev.side, ev.price,
                                               ev.quantity, ev.timestamp,
                                               OrderType::Limit});
                }
                break;
            case EventType::Cancel:
                book.cancel_order(ev.order_id);
                break;
            case EventType::Modify:
                book.modify_order(ev.order_id, ev.price, ev.quantity);
                break;
            case EventType::Market:
                break;  // not meaningful without matching; ignored
        }

        emit_snapshots(seq, ev.timestamp, book, ofi, ofi_levels, book_out, ofi_out);
    }
    stats.events_processed = events.size();
    return stats;
}

}  // namespace obme
