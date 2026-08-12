"""Generate a small **LOBSTER-format** fixture (message + orderbook files).

Real LOBSTER sample data is now gated behind a request/approval form, so this
script produces a *faithful, self-consistent* stand-in in the exact LOBSTER file
format, driven by the same shadow book used elsewhere (so the emitted orderbook
file is correct by construction). It lets us prove the LOBSTER adapter and the
engine's book-only reconstruction end-to-end — and keep proving it in CI —
without any proprietary data.

Message file columns (no header): ``Time, Type, OrderID, Size, Price, Direction``
  Type: 1 new limit, 2 partial delete, 3 total delete, 4 visible execution,
        5 hidden execution. Direction: 1 = buy/bid, -1 = sell/ask.
Orderbook file columns (no header), one row per message, level 1 only here:
  ``AskPrice, AskSize, BidPrice, BidSize`` (prices in dollars x 10000).

The two files match LOBSTER's real layout closely enough that
``lobster_adapter.py`` consumes them exactly as it would the genuine article.
"""

from __future__ import annotations

import argparse
import random
from typing import List, TextIO, Tuple

from generate_synthetic import ShadowBook

# LOBSTER sentinels for an absent book level.
ASK_EMPTY_PRICE = 9999999999
BID_EMPTY_PRICE = -9999999999
TICK = 100  # one cent, in LOBSTER's price units (dollars x 10000)


def _ob_row(book: ShadowBook) -> Tuple[int, int, int, int]:
    bb, bbq, ba, baq = book.l1()
    ask_p = ba if ba is not None else ASK_EMPTY_PRICE
    ask_s = baq if baq is not None else 0
    bid_p = bb if bb is not None else BID_EMPTY_PRICE
    bid_s = bbq if bbq is not None else 0
    return ask_p, ask_s, bid_p, bid_s


class FixtureWriter:
    def __init__(self, msg_out: TextIO, ob_out: TextIO, book: ShadowBook) -> None:
        self.msg = msg_out
        self.ob = ob_out
        self.book = book
        self.t = 34200.0  # 09:30:00, LOBSTER-style seconds after midnight

    def emit(self, mtype: int, oid: int, size: int, price: int, direction: int) -> None:
        self.t += 0.000123  # strictly increasing timestamps
        self.msg.write(f"{self.t:.6f},{mtype},{oid},{size},{price},{direction}\n")
        ask_p, ask_s, bid_p, bid_s = _ob_row(self.book)
        self.ob.write(f"{ask_p},{ask_s},{bid_p},{bid_s}\n")


def generate(
    msg_out: TextIO,
    ob_out: TextIO,
    *,
    n_messages: int,
    seed: int,
    mid: int = 1_000_000,  # $100.00
) -> None:
    rng = random.Random(seed)
    book = ShadowBook()
    writer = FixtureWriter(msg_out, ob_out, book)
    next_id = 1

    def dir_of(side: str) -> int:
        return 1 if side == "B" else -1

    # Seed a two-sided book so the very first messages are meaningful.
    for k in range(1, 4):
        bid_p, ask_p = mid - k * TICK, mid + k * TICK
        book.add_limit(next_id, "B", bid_p, 10 * k)
        writer.emit(1, next_id, 10 * k, bid_p, 1)
        next_id += 1
        book.add_limit(next_id, "S", ask_p, 10 * k)
        writer.emit(1, next_id, 10 * k, ask_p, -1)
        next_id += 1

    for _ in range(n_messages):
        roll = rng.random()
        resting = book.resting_ids()

        if roll < 0.45:  # new (non-crossing) limit order -> type 1
            side = "B" if rng.random() < 0.5 else "S"
            size = rng.randint(1, 20)
            if side == "B":
                ba = book.best_ask()
                base = (ba if ba is not None else mid) - TICK
                price = base - rng.randint(0, 3) * TICK
            else:
                bb = book.best_bid()
                base = (bb if bb is not None else mid) + TICK
                price = base + rng.randint(0, 3) * TICK
            oid = next_id
            next_id += 1
            book.add_limit(oid, side, price, size)
            writer.emit(1, oid, size, price, dir_of(side))

        elif roll < 0.60 and resting:  # partial delete -> type 2
            oid = rng.choice(resting)
            side, price = book.loc[oid]
            cur = next(
                e[1]
                for e in book.bids.get(price, book.asks.get(price, []))
                if e[0] == oid
            )
            delta = max(1, rng.randint(1, max(1, cur - 1)))
            if delta >= cur:
                continue  # leave full deletes to type 3
            book.reduce(oid, delta)
            writer.emit(2, oid, delta, price, dir_of(side))

        elif roll < 0.72 and resting:  # total delete -> type 3
            oid = rng.choice(resting)
            side, price = book.loc[oid]
            cur = next(
                e[1]
                for e in book.bids.get(price, book.asks.get(price, []))
                if e[0] == oid
            )
            book.cancel(oid)
            writer.emit(3, oid, cur, price, dir_of(side))

        elif roll < 0.92:  # marketable order -> one type-4 message per fill
            side = "B" if rng.random() < 0.5 else "S"
            remaining = rng.randint(1, 25)
            # Consume FIFO one resting order at a time, emitting a type-4 and a
            # fresh orderbook snapshot after *each* fill (as real LOBSTER does).
            while remaining > 0:
                if side == "B":
                    if not book.asks:
                        break
                    opp_price, resting_side = min(book.asks), "S"
                    front = book.asks[opp_price][0]
                else:
                    if not book.bids:
                        break
                    opp_price, resting_side = max(book.bids), "B"
                    front = book.bids[opp_price][0]
                oid, avail = front[0], front[1]
                filled = min(remaining, avail)
                book.reduce(oid, filled)
                remaining -= filled
                # LOBSTER: Direction is the side of the executed *limit* order.
                writer.emit(4, oid, filled, opp_price, dir_of(resting_side))

        else:  # hidden execution -> type 5 (no visible book change)
            writer.emit(5, 0, rng.randint(1, 10), mid, dir_of("B"))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-message", required=True)
    parser.add_argument("--out-orderbook", required=True)
    parser.add_argument("--messages", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=7)
    args = parser.parse_args()

    with open(args.out_message, "w", newline="") as msg, open(
        args.out_orderbook, "w", newline=""
    ) as ob:
        generate(msg, ob, n_messages=args.messages, seed=args.seed)
    print(f"Wrote LOBSTER-format fixture: {args.out_message} and {args.out_orderbook}")


if __name__ == "__main__":
    main()
