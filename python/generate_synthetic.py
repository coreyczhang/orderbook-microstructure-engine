"""Synthetic order-flow generator.

Emits a tick-level event stream (ADD / CANCEL / MARKET) in the schema the C++
engine consumes (see ``data/README.md``). Arrivals are Poisson in continuous
time; limit prices are placed around a random-walking mid with an occasional
aggressive (crossing) order, so the stream produces real trades.

The generator maintains its own **shadow book** using the exact same
price-time-priority matching rules as the C++ engine. That serves two purposes:

1. Cancels only ever reference orders that are actually still resting, so the
   stream is internally coherent.
2. The shadow book's top-of-book after each event is written out as **ground
   truth** (``--truth``), which ``validate.py`` diffs against the engine's
   reconstructed ``book.csv`` to prove the reconstruction is correct.

Pure standard library; deterministic given ``--seed``.
"""

from __future__ import annotations

import argparse
import random
from collections import deque
from typing import Deque, Dict, List, Optional, TextIO, Tuple

Side = str  # "B" or "S"


class ShadowBook:
    """A minimal price-time-priority limit order book (matching mirrors the C++
    engine). Levels are FIFO queues of ``[order_id, quantity]`` entries."""

    def __init__(self) -> None:
        self.bids: Dict[int, Deque[List[int]]] = {}
        self.asks: Dict[int, Deque[List[int]]] = {}
        self.loc: Dict[int, Tuple[Side, int]] = {}

    def best_bid(self) -> Optional[int]:
        return max(self.bids) if self.bids else None

    def best_ask(self) -> Optional[int]:
        return min(self.asks) if self.asks else None

    def resting_ids(self) -> List[int]:
        return list(self.loc.keys())

    def _rest(self, side: Side, price: int, order_id: int, qty: int) -> None:
        book = self.bids if side == "B" else self.asks
        book.setdefault(price, deque()).append([order_id, qty])
        self.loc[order_id] = (side, price)

    def _consume(
        self,
        opp_side: Side,
        price: int,
        remaining: int,
        fills: Optional[List[Tuple[int, Side, int, int]]] = None,
    ) -> int:
        """Fills ``remaining`` shares against the FIFO at ``price`` on
        ``opp_side``; returns the still-unfilled remainder. When ``fills`` is
        given, appends ``(resting_order_id, opp_side, price, filled_qty)`` for
        each (partial) execution — used to emit LOBSTER-style type-4 messages."""
        book = self.bids if opp_side == "B" else self.asks
        level = book[price]
        while remaining > 0 and level:
            head = level[0]
            fill = min(remaining, head[1])
            head[1] -= fill
            remaining -= fill
            if fills is not None:
                fills.append((head[0], opp_side, price, fill))
            if head[1] == 0:
                del self.loc[head[0]]
                level.popleft()
        if not level:
            del book[price]
        return remaining

    def add_limit(self, order_id: int, side: Side, price: int, qty: int) -> None:
        remaining = qty
        if side == "B":
            while remaining > 0 and self.asks:
                best = min(self.asks)
                if best > price:
                    break
                remaining = self._consume("S", best, remaining)
        else:
            while remaining > 0 and self.bids:
                best = max(self.bids)
                if best < price:
                    break
                remaining = self._consume("B", best, remaining)
        if remaining > 0:
            self._rest(side, price, order_id, remaining)

    def market(self, side: Side, qty: int) -> List[Tuple[int, Side, int, int]]:
        """Consumes ``qty`` shares from the opposite side; returns the list of
        resting-order fills produced (see ``_consume``). Any unfilled remainder
        is dropped (a market order never rests)."""
        remaining = qty
        fills: List[Tuple[int, Side, int, int]] = []
        if side == "B":
            while remaining > 0 and self.asks:
                remaining = self._consume("S", min(self.asks), remaining, fills)
        else:
            while remaining > 0 and self.bids:
                remaining = self._consume("B", max(self.bids), remaining, fills)
        return fills

    def cancel(self, order_id: int) -> bool:
        if order_id not in self.loc:
            return False
        side, price = self.loc[order_id]
        book = self.bids if side == "B" else self.asks
        level = book[price]
        for i, entry in enumerate(level):
            if entry[0] == order_id:
                del level[i]
                break
        if not level:
            del book[price]
        del self.loc[order_id]
        return True

    def reduce(self, order_id: int, delta: int) -> int:
        """Partially reduces a resting order by ``delta`` shares (removing it if
        it hits zero). Returns the remaining size, or 0 if fully removed / absent.
        Used to model LOBSTER type-2 partial cancellations."""
        if order_id not in self.loc:
            return 0
        _, price = self.loc[order_id]
        book = self.bids if self.loc[order_id][0] == "B" else self.asks
        for entry in book[price]:
            if entry[0] == order_id:
                entry[1] -= delta
                if entry[1] <= 0:
                    self.cancel(order_id)
                    return 0
                return entry[1]
        return 0

    def l1(self) -> Tuple[Optional[int], Optional[int], Optional[int], Optional[int]]:
        bb, ba = self.best_bid(), self.best_ask()
        bbq = sum(e[1] for e in self.bids[bb]) if bb is not None else None
        baq = sum(e[1] for e in self.asks[ba]) if ba is not None else None
        return bb, bbq, ba, baq

    def mid(self, fallback: float) -> float:
        bb, ba = self.best_bid(), self.best_ask()
        if bb is not None and ba is not None:
            return (bb + ba) / 2.0
        if bb is not None:
            return float(bb)
        if ba is not None:
            return float(ba)
        return fallback


def _cell(value: Optional[int]) -> str:
    return "" if value is None else str(value)


def generate(
    events_out: TextIO,
    truth_out: Optional[TextIO],
    *,
    n_events: int,
    seed: int,
    initial_mid: int,
    arrival_rate: float,
    p_cancel: float,
    p_market: float,
    p_aggressive: float,
    max_qty: int,
    depth: int,
) -> Dict[str, int]:
    """Writes ``n_events`` events; returns simple summary counts."""
    rng = random.Random(seed)
    book = ShadowBook()

    events_out.write("timestamp,event_type,order_id,side,price,quantity\n")
    if truth_out is not None:
        truth_out.write("seq,timestamp,bid_px,bid_qty,ask_px,ask_qty\n")

    ts = 0
    next_id = 1
    ref = float(initial_mid)
    counts = {"ADD": 0, "CANCEL": 0, "MARKET": 0}

    for seq in range(n_events):
        # Poisson arrivals: exponential inter-arrival gaps, in integer nanoseconds.
        ts += max(1, int(rng.expovariate(arrival_rate) * 1e9))
        ref = book.mid(ref)
        base = int(round(ref))
        roll = rng.random()
        can_cancel = bool(book.resting_ids())

        if can_cancel and roll < p_cancel:
            order_id = rng.choice(book.resting_ids())
            side, price = book.loc[order_id]
            book.cancel(order_id)
            events_out.write(f"{ts},CANCEL,{order_id},{side},{price},0\n")
            counts["CANCEL"] += 1
        elif roll < p_cancel + p_market:
            side = "B" if rng.random() < 0.5 else "S"
            qty = rng.randint(1, max_qty)
            order_id = next_id
            next_id += 1
            book.market(side, qty)
            events_out.write(f"{ts},MARKET,{order_id},{side},0,{qty}\n")
            counts["MARKET"] += 1
        else:
            side = "B" if rng.random() < 0.5 else "S"
            qty = rng.randint(1, max_qty)
            order_id = next_id
            next_id += 1
            if side == "B":
                price = base - rng.randint(1, depth)
                if rng.random() < p_aggressive:
                    price = base + rng.randint(1, 2)  # crosses into asks
            else:
                price = base + rng.randint(1, depth)
                if rng.random() < p_aggressive:
                    price = base - rng.randint(1, 2)  # crosses into bids
            price = max(1, price)
            book.add_limit(order_id, side, price, qty)
            events_out.write(f"{ts},ADD,{order_id},{side},{price},{qty}\n")
            counts["ADD"] += 1

        if truth_out is not None:
            bb, bbq, ba, baq = book.l1()
            truth_out.write(
                f"{seq},{ts},{_cell(bb)},{_cell(bbq)},{_cell(ba)},{_cell(baq)}\n"
            )

    return counts


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="output events CSV path")
    parser.add_argument(
        "--truth",
        default=None,
        help="optional path to write ground-truth L1 snapshots for validation",
    )
    parser.add_argument("--events", type=int, default=50_000, help="number of events")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--initial-mid", type=int, default=10_000, help="mid, in ticks")
    parser.add_argument(
        "--arrival-rate", type=float, default=1000.0, help="events per second (Poisson)"
    )
    parser.add_argument("--p-cancel", type=float, default=0.25)
    parser.add_argument("--p-market", type=float, default=0.10)
    parser.add_argument(
        "--p-aggressive",
        type=float,
        default=0.15,
        help="probability a limit order is placed to cross the book",
    )
    parser.add_argument("--max-qty", type=int, default=20)
    parser.add_argument(
        "--depth", type=int, default=5, help="max tick offset for passive limit prices"
    )
    args = parser.parse_args()

    truth_file = open(args.truth, "w", newline="") if args.truth else None
    try:
        with open(args.out, "w", newline="") as events_file:
            counts = generate(
                events_file,
                truth_file,
                n_events=args.events,
                seed=args.seed,
                initial_mid=args.initial_mid,
                arrival_rate=args.arrival_rate,
                p_cancel=args.p_cancel,
                p_market=args.p_market,
                p_aggressive=args.p_aggressive,
                max_qty=args.max_qty,
                depth=args.depth,
            )
    finally:
        if truth_file is not None:
            truth_file.close()

    total = sum(counts.values())
    print(
        f"Wrote {total} events to {args.out} "
        f"(ADD={counts['ADD']}, CANCEL={counts['CANCEL']}, MARKET={counts['MARKET']})"
    )
    if args.truth:
        print(f"Wrote ground-truth L1 snapshots to {args.truth}")


if __name__ == "__main__":
    main()
