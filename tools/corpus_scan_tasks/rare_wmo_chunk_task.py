"""Presence scan for rare/unresolved WMO chunks, root- and group-scoped.

Targets the open items in TODO/WORLD/ docs: MFOB (wiki stub, no struct,
never yet seen), MOP2/MOS2 (light animation, 0 in every prior sample),
MOLP/MOLS (lightset lights, needed as decode fixtures). Walks top-level
chunks and, for group files, the sub-chunks inside MOGP's payload.
"""

from __future__ import annotations

import struct
from pathlib import Path

TARGETS = ("MFOB", "MOP2", "MOS2", "MOLP", "MOLS")
MOGP_HEADER_SIZE = 0x44


def _walk(data: bytes, start: int, end: int, found: dict[str, int]) -> None:
    off = start
    while off + 8 <= end:
        tag = data[off:off + 4][::-1].decode("ascii", "replace")
        size = struct.unpack_from("<I", data, off + 4)[0]
        if tag in found:
            found[tag] += size
        if tag == "MOGP":
            _walk(data, off + 8 + MOGP_HEADER_SIZE, min(off + 8 + size, end), found)
        off += 8 + size


class RareWmoChunkTask:
    GLOB_PATTERNS = ["*.wmo"]
    FIELDNAMES = [f"{t.lower()}_bytes" for t in TARGETS]
    PARALLEL_MODE = "process"
    BATCH_SIZE = 1

    @staticmethod
    def analyze(path: Path) -> dict | None:
        data = path.read_bytes()
        found = {t: 0 for t in TARGETS}
        _walk(data, 0, len(data), found)
        if not any(found.values()):
            return None
        return {f"{t.lower()}_bytes": found[t] for t in TARGETS}

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        lines = [f"{len(rows)} / {total_files} wmo files carry at least one target chunk"]
        for t in TARGETS:
            key = f"{t.lower()}_bytes"
            hits = [r for r in rows if int(r[key]) > 0]
            lines.append(f"{t}: {len(hits)} files, {sum(int(r[key]) for r in hits)} payload bytes total")
        return lines
