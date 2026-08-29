"""Full-corpus scan for M2 files whose real, actually-used texture slots
resolve to nothing local -- no literal FileDataID .blp/.png, no listfile-
resolved real-name path, and no same-basename fuzzy candidate either
(husk export's own three resolution tiers).

Consumes `husk resolve` (REFACTOR/CLI_AND_TOOLING.md §3,
REFACTOR/RESOURCE_CATALOG.md's "excavation escape hatch" table) instead of
re-deriving husk's own literal/listfile/fuzzy tier order in Python. This
task is the reason that verb exists: an earlier version of this file hand-
mirrored all three tiers directly (export_materials.cpp:437-456's own order,
transcribed here), and that mirror broke once for real -- a rewrite
silently dropped tier 2 (--listfile), and a real 18,742-file CASC
re-extraction landed under real listfile-resolved paths without the scan's
flagged count moving at all, because the tier that would have noticed was
never running (see CLAUDE_HISTORY.md's 2026-08-15/16 entries for the full
incident). `husk resolve` (src/cmd_resolve.cpp) runs the same
`sources::Catalog::texture()` call `export` itself makes -- one
implementation, not a second copy that can silently drift again -- while
skipping the mesh/skeleton/glTF/write cost `export` would otherwise pay
per file (see REFACTOR_LOG.md's "`husk resolve`, a new verb" entry).

Supersedes and replaces the now-deleted `missing_texture_task.py` (which
only checked the literal `<FileDataID>.blp/.png` path and therefore
over-flagged anything husk's listfile or fuzzy fallback would actually
resolve, per its own module doc, see git history).

Only the *used* texture slots are checked -- one ledger entry per (skin,
texture slot) `husk resolve` actually built a batch for, not every slot in
the model's texture array; a texture nothing looks up doesn't matter for
whether the render comes out blank. This is a real, deliberate definition
change from the pre-conversion version of this task, which approximated
"used" via `husk info`'s `texture_lookup` (TXLU reverse-lookup) section
instead, since that didn't need a real .skin/batch resolution to compute
cheaply -- `husk resolve` does the real batch-driven resolution `export`
itself uses (the authoritative definition), not an approximation of it, so
this task now gets that for free. Root cause investigated interactively
2026-08-15: item/objectcomponents/collections-style body-fitted armor
pieces use a `type=2` ("object_skin") replaceable slot with
`file_data_id=0`, filled in by the live client from
CharComponentTextureLayoutsID/ItemDisplayInfo DB2 data, not a standalone
file -- of ~15 race/gender variants of the same item, only the ones whose
local CASC extraction happened to also dump the loose, non-FileDataID-named
skin-overlay .blp files next to the model resolve at all.

Run with:
    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.unfillable_texture_task:UnfillableTextureTask \\
        --root /media/luna/data/wow_export --output-stem unfillable_textures
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import corpus_scan_framework as csf  # noqa: E402 -- see sys.path.insert above; husk_resolve_json read from there, see REFACTOR/CLI_AND_TOOLING.md §3/§4

TIMEOUT = 20.0  # husk resolve does a real .skin/batch parse per file, not just a header read -- see corpus_scan_framework.husk_resolve_json


class UnfillableTextureTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = ["used_texture_count", "missing_file_data_ids", "replaceable_only"]
    PARALLEL_MODE = "process"
    # BATCH_SIZE=8 was measured against the pre-conversion `husk info`-only
    # per-file cost (CORPUS_SCANS.md's own BATCH_SIZE rule: never copy a
    # number across a real per-file cost-shape change). `husk resolve`
    # itself does real skin-batch resolution and reads matched texture
    # bytes -- a different, heavier cost shape -- so that old measurement
    # no longer applies and hasn't been re-taken at corpus scale. Back to
    # the documented default (1) until it is.
    BATCH_SIZE = 1

    @staticmethod
    def analyze(path: Path) -> dict | None:
        resolved = csf.husk_resolve_json(path, timeout=TIMEOUT)
        if resolved is None:
            return None
        slots = resolved["slots"]
        if not slots:
            return None

        # Per-slot, not per-file: a model with one working texture and one
        # still-blank slot (e.g. a resolved base skin plus an unresolved
        # DB2-driven object_skin slot) must still be reported -- an earlier
        # version broke on the *first* resolved slot and returned None for
        # the whole file, silently hiding every other slot's real gap.
        missing_fdids = [s["file_data_id"] for s in slots if not s["found"] and s["file_data_id"]]
        if all(s["found"] for s in slots):
            return None
        return {
            "used_texture_count": len(slots),
            "missing_file_data_ids": " ".join(str(f) for f in missing_fdids),
            # True when every *unresolved* slot is a replaceable type (no
            # FileDataID at all) -- these have nothing for a CASC
            # re-extraction to fill in (they're not standalone files; the
            # live client composites them from DB2 data at runtime). Keyed
            # on the unresolved slots specifically, not on whether the file
            # has *any* resolved real-fdid slot anywhere -- an earlier
            # version used the latter and wrongly bucketed any file mixing
            # one resolved real-fdid slot with one unresolved replaceable
            # slot (extremely common in item/objectcomponents) as a
            # "genuine extraction gap", inflating that count ~300x in a
            # real corpus run (2026-08-22, see CLAUDE_HISTORY.md).
            "replaceable_only": not missing_fdids,
        }

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        extraction_gap = [r for r in rows if not r["replaceable_only"]]
        replaceable_only = [r for r in rows if r["replaceable_only"]]
        all_missing_ids: set[int] = set()
        for r in extraction_gap:
            all_missing_ids.update(int(x) for x in r["missing_file_data_ids"].split())
        return [
            # "at least one unresolved slot", not "none resolve" -- the prose
            # said the latter for a long time while the code always meant the
            # former (it flags on `not all(...)`, and deliberately so: the
            # helm_leather_pvpdruid_b_02_scm.m2 case in analyze()'s own comment
            # is a file that went unreported precisely because one unrelated
            # slot happened to resolve). Corrected here rather than left to
            # mislead the next reader of a corpus report's headline number.
            f"{len(rows)} / {total_files} .m2 files have at least one actually-used texture slot "
            f"that doesn't resolve locally ({len(rows) / total_files:.3%} of the corpus).",
            f"  {len(extraction_gap)} of those have a real, missing FileDataID -- a genuine CASC "
            f"re-extraction gap ({len(all_missing_ids)} distinct FileDataIDs across them).",
            f"  {len(replaceable_only)} of those use only replaceable/DB2-driven texture slots "
            f"(no standalone file exists to extract -- not an extraction gap, out of scope for "
            f"a casc-tool report).",
        ]
