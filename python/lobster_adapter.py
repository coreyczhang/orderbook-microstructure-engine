"""Adapt LOBSTER message data to this engine's event schema.

LOBSTER publishes an **already-matched** message stream: incoming marketable
orders appear as executions (type 4) against resting orders, not as orders to be
re-matched. So the engine reconstructs the book with ``--book-only`` (direct
ADD/CANCEL/MODIFY, no matching), and this adapter translates each LOBSTER
message into exactly one of our events.

LOBSTER message columns (no header): ``Time, Type, OrderID, Size, Price, Direction``
  Type 1 new limit / 2 partial delete / 3 total delete / 4 visible execution /
  5 hidden execution / 6 cross / 7 halt. Direction 1 = buy/bid, -1 = sell/ask.

Mapping (one event per message, preserving row alignment for validation):
  1            -> ADD
  2, 4         -> MODIFY (reduce remaining size in place) or CANCEL if it hits 0
  3            -> CANCEL
  5, 6, 7 etc. -> no-op CANCEL of id 0 (hidden/system; leaves the book unchanged
                  but still emits a snapshot so rows line up 1:1 with LOBSTER's
                  orderbook file)

Optionally converts the paired LOBSTER **orderbook** file into a ground-truth L1
CSV in this project's ``book.csv`` format, so ``validate.py`` can confirm the
reconstruction matches LOBSTER's own book row for row.

Pure standard library.
"""

from __future__ import annotations

import argparse
import csv
from typing import Dict, TextIO, Tuple

ASK_EMPTY_PRICE = 9999999999
BID_EMPTY_PRICE = -9999999999


def _side(direction: str) -> str:
    return "B" if int(direction) == 1 else "S"


def convert_messages(msg_in: TextIO, events_out: TextIO) -> Dict[str, int]:
    """Translates a LOBSTER message file to our event CSV. Returns type counts."""
    events_out.write("timestamp,event_type,order_id,side,price,quantity\n")
    sizes: Dict[int, int] = {}  # resting order id -> remaining shares
    counts = {"ADD": 0, "MODIFY": 0, "CANCEL": 0, "NOOP": 0}

    def noop() -> None:
        events_out.write(f"{ts},CANCEL,0,B,0,0\n")
        counts["NOOP"] += 1

    reader = csv.reader(msg_in)
    for row in reader:
        if not row or len(row) < 6:
            continue
        time_s, mtype_s, oid_s, size_s, price_s, dir_s = row[:6]
        ts = int(round(float(time_s) * 1e9))
        mtype = int(mtype_s)
        oid = int(oid_s)
        size = int(size_s)
        price = int(price_s)
        side = _side(dir_s)

        if mtype == 1:  # new limit order
            events_out.write(f"{ts},ADD,{oid},{side},{price},{size}\n")
            sizes[oid] = size
            counts["ADD"] += 1
        elif mtype in (2, 4):  # partial cancel / visible execution: reduce
            cur = sizes.get(oid)
            if cur is None:
                noop()  # references an order we never saw (pre-window); skip
                continue
            new = cur - size
            if new > 0:
                events_out.write(f"{ts},MODIFY,{oid},{side},{price},{new}\n")
                sizes[oid] = new
                counts["MODIFY"] += 1
            else:
                events_out.write(f"{ts},CANCEL,{oid},{side},{price},0\n")
                sizes.pop(oid, None)
                counts["CANCEL"] += 1
        elif mtype == 3:  # total deletion
            if oid in sizes:
                events_out.write(f"{ts},CANCEL,{oid},{side},{price},0\n")
                sizes.pop(oid, None)
                counts["CANCEL"] += 1
            else:
                noop()
        else:  # 5 hidden execution, 6 cross, 7 halt, ...: no visible book change
            noop()

    return counts


def _cell(price: int, size: int, empty_price: int) -> Tuple[str, str]:
    absent = size == 0 or abs(price) >= abs(empty_price)
    return ("", "") if absent else (str(price), str(size))


def convert_orderbook(ob_in: TextIO, truth_out: TextIO) -> int:
    """Converts a LOBSTER orderbook file (level-1 columns used) into ground-truth
    L1 rows in this project's book.csv format. Returns the row count."""
    truth_out.write("seq,timestamp,bid_px,bid_qty,ask_px,ask_qty\n")
    reader = csv.reader(ob_in)
    rows = 0
    for seq, row in enumerate(reader):
        if not row or len(row) < 4:
            continue
        ask_p, ask_s, bid_p, bid_s = (int(x) for x in row[:4])
        ask_px, ask_qty = _cell(ask_p, ask_s, ASK_EMPTY_PRICE)
        bid_px, bid_qty = _cell(bid_p, bid_s, BID_EMPTY_PRICE)
        truth_out.write(f"{seq},0,{bid_px},{bid_qty},{ask_px},{ask_qty}\n")
        rows += 1
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--message", required=True, help="LOBSTER message CSV")
    parser.add_argument("--out", required=True, help="output events CSV")
    parser.add_argument("--orderbook", default=None, help="LOBSTER orderbook CSV")
    parser.add_argument("--truth", default=None, help="output ground-truth L1 CSV")
    args = parser.parse_args()

    with open(args.message, newline="") as msg, open(args.out, "w", newline="") as ev:
        counts = convert_messages(msg, ev)
    total = sum(counts.values())
    print(
        f"Converted {total} messages -> {args.out} "
        f"(ADD={counts['ADD']}, MODIFY={counts['MODIFY']}, "
        f"CANCEL={counts['CANCEL']}, no-op={counts['NOOP']})"
    )

    if args.orderbook and args.truth:
        with open(args.orderbook, newline="") as ob, open(
            args.truth, "w", newline=""
        ) as tr:
            rows = convert_orderbook(ob, tr)
        print(f"Converted orderbook -> {args.truth} ({rows} L1 snapshots)")


if __name__ == "__main__":
    main()
