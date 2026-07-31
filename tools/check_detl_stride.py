#!/usr/bin/env python3
"""Resolves DETL's real byte layout (see WIKI_FINDINGS.md sec.11 for the
result this scan produced) -- wowdev.wiki's DETL struct
lists fields summing to 0x0C bytes but gives an explicit end-offset comment
of /*0x0a*/ -- a 6-byte discrepancy. Same stride-disambiguation technique
that resolved .phys's SHOJ/PLYT stride ambiguities (WIKI_FINDINGS.md sec.9):
DETL_recs is sized `[m2data.header.lights.count]`, so `chunk.size /
lights.count` must come out to a clean integer at the real stride -- try
both 0x0a and 0x0c as candidates and see which one (if either) divides
evenly across every real file.

Independent of husk's own code, same discipline as every other tool in this
directory -- reads the top-level chunk sequence and the MD21 payload's
`lights` Array descriptor (offset 0x108, per src/m2.cpp's own offset table)
directly.

Usage:
    direnv exec . uv run --python tools/venv/bin/python tools/check_detl_stride.py [corpus_root]
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
LIGHTS_OFFSET = 0x108  # M2Array<M2Light>: count (u32) + offset (u32), src/m2.cpp offset::lights


def iter_top_level_chunks(data: bytes):
    pos = 0
    n = len(data)
    while pos + 8 <= n:
        tag = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        payload_start = pos + 8
        if payload_start + size > n:
            return
        yield tag, data[payload_start:payload_start + size]
        pos = payload_start + size


def main() -> int:
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_ROOT
    print(f"scanning {root} for .m2 files with a DETL chunk")
    m2_files = sorted(root.rglob("*.m2"))
    print(f"found {len(m2_files)} .m2 files")

    records = []
    unreadable = 0

    iterator = tqdm(m2_files, desc="scanning DETL", unit="file") if tqdm else m2_files
    for path in iterator:
        try:
            data = path.read_bytes()
        except OSError:
            unreadable += 1
            continue
        if len(data) < 8 or data[0:4] == b"MD20":
            continue
        try:
            md21_payload = None
            detl_payload = None
            for tag, payload in iter_top_level_chunks(data):
                if tag == b"MD21":
                    md21_payload = payload
                elif tag == b"DETL":
                    detl_payload = payload
            if detl_payload is None:
                continue
            if md21_payload is None or len(md21_payload) < LIGHTS_OFFSET + 8:
                unreadable += 1
                continue
            lights_count, lights_off = struct.unpack_from("<II", md21_payload, LIGHTS_OFFSET)
            records.append({
                "path": str(path),
                "detl_size": len(detl_payload),
                "lights_count": lights_count,
                "hex_full": detl_payload.hex(),
            })
        except Exception:
            unreadable += 1
            continue

    print(f"{len(records)} file(s) with a real DETL chunk ({unreadable} unreadable/skipped)")

    for r in records:
        size, count = r["detl_size"], r["lights_count"]
        r["divides_0x0a"] = count > 0 and size % 0x0A == 0 and size // 0x0A == count
        r["divides_0x0c"] = count > 0 and size % 0x0C == 0 and size // 0x0C == count
        r["stride_if_0x0a"] = size / count if count else None

    out_path = REPO_ROOT / "detl_stride_report.json"
    out_path.write_text(json.dumps(records, indent=2))
    print(f"full report -> {out_path}")

    for r in records:
        print(f"{r['path']}: detl_size={r['detl_size']} lights_count={r['lights_count']} "
              f"divides_0x0a={r['divides_0x0a']} divides_0x0c={r['divides_0x0c']} "
              f"first16={r['hex_full'][:32]}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
