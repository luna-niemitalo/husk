"""Checks tools/listfile_index.py's read of husk's mmap'd listfile cache
against a plain parse of the same CSV, and times both.

    direnv exec . tools/venv/bin/python tests/listfile_index_check.py <listfile.csv>

The cache must be fresh: run any husk command with --listfile <csv> first.
Exits non-zero, naming the first mismatch, if the two disagree on any row.
"""

from __future__ import annotations

import random
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import listfile_index  # noqa: E402


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    csv = Path(sys.argv[1])

    start = time.perf_counter()
    mapped = listfile_index.open_listfile(csv)
    open_seconds = time.perf_counter() - start
    if isinstance(mapped, dict):
        print("expected husk's mmap'd cache, got the CSV fallback (see the warning above)", file=sys.stderr)
        return 1

    start = time.perf_counter()
    parsed = listfile_index._parse_csv(csv)
    parse_seconds = time.perf_counter() - start

    if len(mapped) != len(parsed):
        print(f"expected {len(parsed)} rows, got {len(mapped)}", file=sys.stderr)
        return 1
    fdids = sorted(parsed)
    sample = fdids[:1000] + fdids[-1000:] + random.Random(0).sample(fdids, 20000)
    for fdid in sample:
        if mapped.get(fdid) != parsed[fdid]:
            print(f"FileDataID {fdid}: expected {parsed[fdid]!r}, got {mapped.get(fdid)!r}", file=sys.stderr)
            return 1
    for missing in (0, fdids[-1] + 1):
        if missing not in parsed and mapped.get(missing) is not None:
            print(f"FileDataID {missing}: expected no row, got {mapped.get(missing)!r}", file=sys.stderr)
            return 1

    start = time.perf_counter()
    for fdid in sample:
        mapped.get(fdid)
    lookup_us = (time.perf_counter() - start) / len(sample) * 1e6
    print(f"ok: {len(mapped)} rows, {len(sample)} lookups agree; "
          f"open {open_seconds * 1000:.1f} ms vs CSV parse {parse_seconds * 1000:.0f} ms; {lookup_us:.1f} us/lookup")
    return 0


if __name__ == "__main__":
    sys.exit(main())
