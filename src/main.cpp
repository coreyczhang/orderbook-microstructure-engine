#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "obme/EventReplay.hpp"
#include "obme/MatchingEngine.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0
        << " <events.csv> [--out-dir DIR] [--ofi-levels N] [--book-only]\n\n"
        << "Replays a tick-level event stream and writes book.csv (top-of-book\n"
        << "snapshots) and ofi.csv to DIR (default: current directory), plus\n"
        << "trades.csv in matching mode. --ofi-levels sets the depth of the\n"
        << "integrated OFI signal (default 5).\n\n"
        << "--book-only applies ADD/CANCEL/MODIFY directly to the book without\n"
        << "matching, for already-matched message data such as LOBSTER (no\n"
        << "trades.csv is produced).\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string events_path;
    std::filesystem::path out_dir = ".";
    std::size_t ofi_levels = 5;
    bool book_only = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--book-only") {
            book_only = true;
        } else if (arg == "--out-dir") {
            if (i + 1 >= argc) {
                std::cerr << "error: --out-dir requires a directory argument\n";
                return 2;
            }
            out_dir = argv[++i];
        } else if (arg == "--ofi-levels") {
            if (i + 1 >= argc) {
                std::cerr << "error: --ofi-levels requires an integer argument\n";
                return 2;
            }
            try {
                const int n = std::stoi(argv[++i]);
                if (n < 1) throw std::invalid_argument("must be >= 1");
                ofi_levels = static_cast<std::size_t>(n);
            } catch (const std::exception&) {
                std::cerr << "error: --ofi-levels must be a positive integer\n";
                return 2;
            }
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown option '" << arg << "'\n";
            print_usage(argv[0]);
            return 2;
        } else if (events_path.empty()) {
            events_path = arg;
        } else {
            std::cerr << "error: unexpected extra argument '" << arg << "'\n";
            return 2;
        }
    }

    if (events_path.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    try {
        std::ifstream in(events_path);
        if (!in) {
            std::cerr << "error: cannot open events file '" << events_path << "'\n";
            return 1;
        }

        std::error_code ec;
        std::filesystem::create_directories(out_dir, ec);
        if (ec) {
            std::cerr << "error: cannot create output directory '" << out_dir.string()
                      << "': " << ec.message() << "\n";
            return 1;
        }

        const std::filesystem::path book_path = out_dir / "book.csv";
        const std::filesystem::path ofi_path = out_dir / "ofi.csv";
        std::ofstream book_out(book_path);
        std::ofstream ofi_out(ofi_path);
        if (!book_out || !ofi_out) {
            std::cerr << "error: cannot open output files in '" << out_dir.string()
                      << "'\n";
            return 1;
        }

        std::vector<obme::Event> events = obme::EventReplay::parse(in);

        if (book_only) {
            obme::OrderBook book;
            const obme::EventReplay::Stats stats = obme::EventReplay::replay_book_only(
                std::move(events), book, &book_out, &ofi_out, ofi_levels);
            std::cout << "Reconstructed " << stats.events_processed
                      << " messages (book-only, no matching).\n"
                      << "Wrote " << book_path.string() << " and " << ofi_path.string()
                      << "\n";
            return 0;
        }

        const std::filesystem::path trades_path = out_dir / "trades.csv";
        std::ofstream trades_out(trades_path);
        if (!trades_out) {
            std::cerr << "error: cannot open output files in '" << out_dir.string()
                      << "'\n";
            return 1;
        }
        obme::MatchingEngine engine;
        const obme::EventReplay::Stats stats = obme::EventReplay::replay(
            std::move(events), engine, &trades_out, &book_out, &ofi_out, ofi_levels);

        std::cout << "Replayed " << stats.events_processed << " events -> "
                  << stats.trades_generated << " trades, " << stats.executed_volume
                  << " shares executed.\n"
                  << "Wrote " << trades_path.string() << ", " << book_path.string()
                  << ", and " << ofi_path.string() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
