"""Full-corpus scan tagging every .m2 with its real M2 version, WoW
expansion label, and a coarse support tier -- built for
tools/live_gallery_server.py's --expansion-data overlay, so the gallery can
filter down to "files husk actually targets" instead of showing every file
in a 130k-file corpus as equally in-scope.

Tier boundaries come straight from DESIGN.md's own stated Goal ("a real
Blender import path for modern (Legion+, chunked) WoW M2 models") and
src/cmd_export.cpp's own version warnings (kMinVerifiedRecordStrideVersion
= 264, Wrath) -- not a new policy invented here:

  - version < 264 (pre-Wrath: Classic/TBC/Pre-Release): "out_of_scope" --
    husk's own parser doesn't claim correctness below this floor at all.
  - version >= 264 but not chunked (WotLK through pre-Legion MoP/WoD, flat
    MD20 body): "sketchy" -- husk parses these, but every version-gated
    feature (particles, some record strides) has real, documented gaps
    below its own verified floor.
  - chunked (Legion+, real MD21 wrapper): "supported" -- husk's actual,
    verified target.

Version/expansion-label logic is a second, independent read of the same
bytes src/m2_primitives.cpp's own expansionForVersion parses (same
"second opinion" discipline WIKI_FINDINGS.md/tools/ already use elsewhere)
-- not a shared implementation, so keep the two in sync by hand if
DESIGN.md's version table is ever revised.

Run with:
    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.expansion_task:ExpansionTask \\
        --root /media/luna/data/wow_export --output-stem expansion_data
"""
from __future__ import annotations

import struct
from pathlib import Path

_VERSION_OFFSET = 0x004
_MIN_HEADER_SIZE = 0x008

# Transcribed from src/m2_primitives.cpp's own expansionForVersion table
# (wowdev.wiki M2#Versions) -- see this file's own docstring for why this
# is a second, independent copy, not a shared implementation.
_EXPANSION_ROWS = [
    (256, 256, "Pre-Release"),
    (256, 257, "Classic"),
    (260, 263, "The Burning Crusade"),
    (264, 264, "Wrath of the Lich King"),
    (265, 272, "Cataclysm"),
    (272, 272, "Mists of Pandaria / Warlords of Draenor"),
    (272, 274, "Legion / Battle for Azeroth / Shadowlands"),
]

_MIN_VERIFIED_VERSION = 264  # src/cmd_export.cpp's kMinVerifiedRecordStrideVersion, Wrath


def _expansion_label(version: int) -> str:
    labels = [label for lo, hi, label in _EXPANSION_ROWS if lo <= version <= hi]
    return " or ".join(labels) if labels else "unknown"


def _is_chunked(data: bytes) -> bool:
    return len(data) >= 4 and data[0:4] == b"MD21"


class ExpansionTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = ["version", "chunked", "expansion", "tier"]
    PARALLEL_MODE = "process"  # plain struct-unpack CPU work, no subprocesses

    @staticmethod
    def analyze(path: Path) -> dict | None:
        try:
            data = path.read_bytes()
        except OSError:
            return None
        chunked = _is_chunked(data)
        blob = data
        if chunked:
            # MD21-wrapped: real header bytes start after the "MD21"+size
            # chunk prefix, same unwrap example_texture_count.py's
            # _extract_md20_blob does.
            if len(data) < 8:
                return None
            csize = struct.unpack_from("<I", data, 4)[0]
            blob = data[8:8 + csize]
        if len(blob) < _MIN_HEADER_SIZE or blob[0:4] != b"MD20":
            return None
        version = struct.unpack_from("<I", blob, _VERSION_OFFSET)[0]

        if chunked:
            tier = "supported"
        elif version >= _MIN_VERIFIED_VERSION:
            tier = "sketchy"
        else:
            tier = "out_of_scope"

        return {"version": version, "chunked": chunked, "expansion": _expansion_label(version), "tier": tier}

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        from collections import Counter
        tiers = Counter(r["tier"] for r in rows)
        lines = [f"{total_files} .m2 files tagged with version/expansion/tier"]
        for tier in ("supported", "sketchy", "out_of_scope"):
            n = tiers.get(tier, 0)
            lines.append(f"  {n:7d}  {tier} ({n/total_files:.1%})")
        return lines
