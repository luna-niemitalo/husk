#!/usr/bin/env python3
"""Scans a real M2 corpus for five top-level Legion+ chunk tags -- WFV1,
WFV2, DPIV, AFRA still genuinely undocumented-or-unverified; PCOL now
implemented (husk's own dumpPcol, src/cmd_dump.cpp) -- see WIKI_FINDINGS.md
sec.10, both corrected, see below. Bundled into one corpus pass since
they're cheap to check together: one top-level chunk walk per file, five
tags to test per chunk.

CORRECTED 2026-07-31: this script previously reported a "confirmed absent"
zero-hit result for all four original targets, which was wrong -- a real bug,
not a real finding. `hits` was keyed by `str` (`tag.decode()`), but the
membership check compared the raw `bytes` tag against it (`if tag in hits`);
`bytes` and `str` never compare equal in Python 3, so the check was always
False regardless of what the real data contained. Caught by casc-tool's
independently-built `scan-chunks` command (a structural, dual-orientation
chunk walker with no Python involved) finding real hits for all five tags in
the exact same 130,576-file corpus this script scans by default -- including
one WFV1 hit landing on FileDataID 2445860, the very file wowdev.wiki names
as its own concrete first-tested example. Fixed by checking membership
against `TARGET_TAGS` (still raw bytes) directly, not the `str`-keyed dict.

Independent of husk's own code (same "second opinion" discipline
tools/find_multiroot_skeletons.py already established) -- reads the raw
top-level chunk sequence directly, not through husk's chunk.cpp.

For every hit, records path + chunk size + a hex dump of up to the first 96
bytes (enough to eyeball WFV3-prefix-alignment for WFV1/WFV2, and to spot
which DPIV bytes are non-zero across files) into a JSON report, plus a plain
newline-separated *_for_exploration.txt per tag (same format
find_multiroot_skeletons.py already uses) for anything that wants just the
file list.

Usage:
    direnv exec . uv run --python tools/venv/bin/python tools/find_m2_unknown_chunks.py [corpus_root]
"""

import json
import struct
import sys
from pathlib import Path

try:
    from tqdm import tqdm
except ImportError:
    tqdm = None

DEFAULT_ROOT = Path("/media/luna/data/wow_export")
REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET_TAGS = (b"WFV1", b"WFV2", b"DPIV", b"AFRA", b"PCOL")
MAX_HEX_BYTES = 96


def iter_top_level_chunks(data: bytes):
    """M2's own top-level chunk container: a flat (tag, u32 size, payload)
    sequence running to EOF -- MD21 wraps the flat MD20-style data as one
    such chunk, WFV1/DPIV/AFRA/etc. are siblings at the same level, not
    nested inside MD21. M2 chunk tags are NOT byte-reversed (see chunk.hpp's
    own doc comment / M2.md's explicit note) -- unlike .phys.
    """
    pos = 0
    n = len(data)
    while pos + 8 <= n:
        tag = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        payload_start = pos + 8
        if payload_start + size > n:
            return  # truncated chunk container -- stop, don't misread
        yield tag, data[payload_start:payload_start + size]
        pos = payload_start + size


def main() -> int:
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_ROOT
    print(f"scanning {root} for .m2 files")
    m2_files = sorted(root.rglob("*.m2"))
    print(f"found {len(m2_files)} .m2 files")

    hits: dict[str, list[dict]] = {tag.decode(): [] for tag in TARGET_TAGS}
    unreadable = 0
    non_chunked = 0

    iterator = tqdm(m2_files, desc="scanning chunks", unit="file") if tqdm else m2_files
    for path in iterator:
        try:
            data = path.read_bytes()
        except OSError:
            unreadable += 1
            continue
        if len(data) < 8:
            unreadable += 1
            continue
        if data[0:4] == b"MD20":
            non_chunked += 1  # old flat file, no Legion+ top-level chunks possible
            continue
        try:
            for tag, payload in iter_top_level_chunks(data):
                if tag in TARGET_TAGS:  # bug fix: `hits` is str-keyed, `tag` is bytes -- compare against TARGET_TAGS
                    hits[tag.decode()].append({
                        "path": str(path),
                        "size": len(payload),
                        "hex": payload[:MAX_HEX_BYTES].hex(),
                    })
        except Exception:
            unreadable += 1
            continue

    for tag_str, records in hits.items():
        out_txt = REPO_ROOT / f"{tag_str.lower()}_files_for_exploration.txt"
        paths = [r["path"] for r in records]
        out_txt.write_text("\n".join(paths) + ("\n" if paths else ""))
        print(f"{tag_str}: {len(records)} hit(s) -> {out_txt}")

    report_path = REPO_ROOT / "m2_unknown_chunks_report.json"
    report_path.write_text(json.dumps(hits, indent=2))
    print(f"full report -> {report_path}")
    print(f"({non_chunked} non-chunked/flat files skipped, {unreadable} unreadable/skipped)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
