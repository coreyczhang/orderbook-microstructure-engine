#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "FlowImbalanceMM.hpp"
#include "Simulator.hpp"
#include "obme/EventReplay.hpp"

namespace {

void print_usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " <events.csv> [--out-dir DIR]\n"
        << "            [--latency-ns N] [--horizon-ns N] [--quote-size N]\n"
        << "            [--flow-min F] [--flow-max F] [--book-threshold F]\n"
        << "            [--mid-shift F] [--trade-window-ns N]\n\n"
        << "Replays a market event stream and simulates a flow-imbalance market\n"
        << "maker (shadow quotes, latency, FIFO queue fills, adverse-selection\n"
        << "markout). Writes fills.csv and pnl.csv to DIR (default: current dir).\n";
}

bool need_value(int i, int argc, const char* flag) {
    if (i + 1 >= argc) {
        std::cerr << "error: " << flag << " requires a value\n";
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace obme;

    std::string events_path;
    std::filesystem::path out_dir = ".";
    sim::SimConfig cfg;
    sim::FlowImbalanceParams params;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--out-dir") {
            if (!need_value(i, argc, "--out-dir")) return 2;
            out_dir = next();
        } else if (arg == "--latency-ns") {
            if (!need_value(i, argc, "--latency-ns")) return 2;
            cfg.latency = std::stoll(next());
        } else if (arg == "--horizon-ns") {
            if (!need_value(i, argc, "--horizon-ns")) return 2;
            cfg.markout_horizon = std::stoll(next());
        } else if (arg == "--quote-size") {
            if (!need_value(i, argc, "--quote-size")) return 2;
            params.quote_size = std::stoll(next());
        } else if (arg == "--flow-min") {
            if (!need_value(i, argc, "--flow-min")) return 2;
            params.flow_min = std::stod(next());
        } else if (arg == "--flow-max") {
            if (!need_value(i, argc, "--flow-max")) return 2;
            params.flow_max = std::stod(next());
        } else if (arg == "--book-threshold") {
            if (!need_value(i, argc, "--book-threshold")) return 2;
            params.book_threshold = std::stod(next());
        } else if (arg == "--mid-shift") {
            if (!need_value(i, argc, "--mid-shift")) return 2;
            params.mid_shift = std::stod(next());
        } else if (arg == "--trade-window-ns") {
            if (!need_value(i, argc, "--trade-window-ns")) return 2;
            params.trade_window = std::stoll(next());
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

        const std::filesystem::path fills_path = out_dir / "fills.csv";
        const std::filesystem::path pnl_path = out_dir / "pnl.csv";
        std::ofstream fills_out(fills_path);
        std::ofstream pnl_out(pnl_path);
        if (!fills_out || !pnl_out) {
            std::cerr << "error: cannot open output files in '" << out_dir.string()
                      << "'\n";
            return 1;
        }

        std::vector<Event> events = EventReplay::parse(in);
        sim::FlowImbalanceMM strategy(params);
        sim::Simulator simulator(strategy, cfg);
        const sim::SimStats s = simulator.run(std::move(events), &fills_out, &pnl_out);

        std::cout << "Simulated " << s.events << " events with " << strategy.name()
                  << ".\n"
                  << "  fills: " << s.fills << " (buy " << s.filled_buy << ", sell "
                  << s.filled_sell << ")\n"
                  << "  final inventory: " << s.final_inventory
                  << ", cash: " << s.final_cash << ", equity: " << s.final_equity << "\n"
                  << "  total markout (adverse selection): " << s.total_markout << "\n"
                  << "Wrote " << fills_path.string() << " and " << pnl_path.string()
                  << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
