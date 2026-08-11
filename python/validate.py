"""Validate the engine's reconstructed book against generator ground truth.

The C++ engine writes a top-of-book (L1) snapshot after every event to
``book.csv``; ``generate_synthetic.py --truth`` writes the shadow book's L1 after
every event. Because both apply the identical price-time-priority rules to the
identical event stream, the two files must match row for row. This script diffs
them and exits non-zero on any mismatch — a direct correctness check on the
reconstruction.

Pure standard library.
"""

from __future__ import annotations

import argparse
import csv
import sys
from typing import List, Optional, Tuple

Row = Tuple[Optional[int], ...]

COLUMNS = ["seq", "timestamp", "bid_px", "bid_qty", "ask_px", "ask_qty"]


def _read(path: str) -> List[Row]:
    rows: List[Row] = []
    with open(path, newline="") as f:
        reader = csv.reader(f)
        header = next(reader, None)
        if header != COLUMNS:
            raise ValueError(f"{path}: unexpected header {header}, want {COLUMNS}")
        for rec in reader:
            if not rec:
                continue
            rows.append(tuple(int(x) if x != "" else None for x in rec))
    return rows


def compare(engine_path: str, truth_path: str, max_report: int = 10) -> int:
    engine = _read(engine_path)
    truth = _read(truth_path)

    if len(engine) != len(truth):
        print(
            f"MISMATCH: row count differs — engine={len(engine)} truth={len(truth)}",
            file=sys.stderr,
        )
        return 1

    mismatches = 0
    for i, (e, t) in enumerate(zip(engine, truth)):
        # Compare on book state (ignore seq/timestamp bookkeeping columns 0,1).
        if e[2:] != t[2:]:
            if mismatches < max_report:
                print(
                    f"MISMATCH at row {i}: engine L1={e[2:]} truth L1={t[2:]}",
                    file=sys.stderr,
                )
            mismatches += 1

    if mismatches:
        print(
            f"FAILED: {mismatches}/{len(engine)} rows differ.",
            file=sys.stderr,
        )
        return 1

    print(f"OK: reconstruction matches ground truth on all {len(engine)} snapshots.")
    return 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-book", required=True, help="engine's book.csv")
    parser.add_argument("--truth", required=True, help="ground-truth L1 CSV")
    args = parser.parse_args()
    sys.exit(compare(args.engine_book, args.truth))


if __name__ == "__main__":
    main()
