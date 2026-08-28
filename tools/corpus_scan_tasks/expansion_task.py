"""Full-corpus scan tagging every .m2 with its real M2 version, WoW
expansion label, and a coarse support tier -- built for
tools/live_gallery/server.py's --expansion-data overlay, so the gallery can
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

Consumes `husk info --json` per file (REFACTOR/CLI_AND_TOOLING.md §3):
`format`/`version`/`expansion`/`record_stride_version_verified` are all
real fields of that schema (`src/cmd_info_json.cpp`), including the exact
`expansionForVersion` table this file used to hand-transcribe -- so this
is no longer a second, independent implementation to keep in sync by hand,
it consumes husk's own answer directly.

Run with:
    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.expansion_task:ExpansionTask \\
        --root /media/luna/data/wow_export --output-stem expansion_data
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import corpus_scan_framework as csf  # noqa: E402 -- see sys.path.insert above; husk_info_json read from there, see REFACTOR/CLI_AND_TOOLING.md §3


class ExpansionTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = ["version", "chunked", "expansion", "tier"]
    # Shells out to husk per file *now* -- this task used to read 16 header
    # bytes in-process with zero subprocesses, so the conversion is a real
    # measured cost, not a neutral swap: 3,000 creature/ files went from
    # 1-2s to 7s (~4-5x; ~1.5min -> ~5min extrapolated over the full 132k
    # corpus). Accepted deliberately -- what it buys is deleting a
    # hand-transcribed copy of src/m2_primitives.cpp's expansionForVersion
    # table that had to be kept in sync by hand, which is the drift this
    # refactor exists to remove. Revisit only if this scan ever moves onto
    # a hot path; a few minutes per full-corpus run is not one.
    PARALLEL_MODE = "process"
    BATCH_SIZE = 1  # dominated by the husk subprocess spawn, not IPC dispatch -- see CORPUS_SCANS.md's BATCH_SIZE gotcha; not measured against a higher value

    @staticmethod
    def analyze(path: Path) -> dict | None:
        info = csf.husk_info_json(path)
        if info is None:
            return None

        chunked = info["format"] == "legion_chunked"
        version = info["version"]

        if chunked:
            tier = "supported"
        elif info["record_stride_version_verified"]:
            tier = "sketchy"
        else:
            tier = "out_of_scope"

        return {"version": version, "chunked": chunked, "expansion": info["expansion"], "tier": tier}

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        from collections import Counter
        tiers = Counter(r["tier"] for r in rows)
        lines = [f"{total_files} .m2 files tagged with version/expansion/tier"]
        for tier in ("supported", "sketchy", "out_of_scope"):
            n = tiers.get(tier, 0)
            lines.append(f"  {n:7d}  {tier} ({n/total_files:.1%})")
        return lines
