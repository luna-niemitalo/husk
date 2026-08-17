"""Full-corpus scan for M2 files whose real, actually-used texture slots
resolve to nothing local -- no literal FileDataID .blp/.png, no listfile-
resolved real-name path, and no same-basename fuzzy candidate either
(husk export's own three resolution tiers, mirrored here). Cheap by
design (`husk info`, header-only, no export -- same shape as
missing_texture_task.py), NOT a full `husk export` per file: an earlier
version of this task shelled out to real `husk export` per file to get
its exact embedded-texture count, which is correct but does a full
mesh/skin build + image embed + .glb write for all 130k files -- turned
a ~10-minute scan into a multi-hour one for no accuracy this task
actually needs. Corrected after direct feedback.

Different from missing_texture_task.py (which only checks the literal
`<FileDataID>.blp/.png` path and therefore over-flags anything husk's
listfile or fuzzy fallback would actually resolve) -- this task mirrors
all three of husk's real tiers, in husk's own precedence order
(export_materials.cpp:437-456): (1) literal `<FileDataID>.blp/.png` next
to the model, (2) --listfile lookup (a real corpus export commonly keeps
files under their real name/path, e.g. "character/draenei/male/
draeneimaleskin.blp", not renamed to a bare FileDataID -- resolved
against --listfile-root, the corpus root, not the model's own directory),
(3) scanFuzzyTexturePoolForBasename: any local .blp/.png whose stem
starts with the model's own basename, case-insensitive. A first version
of this task only implemented tiers 1 and 3 (dropped tier 2 when
rewriting away from a `husk export`-based implementation that *did* pass
--listfile, and never added it back) -- caught after a real 18,742-file
CASC re-extraction landed under real listfile-resolved paths and the
scan's flagged count didn't move at all, because tier 2 was silently
never being checked. See CLAUDE_HISTORY.md's 2026-08-15/16 entries for
the full incident.

Only the *used* texture slots are checked -- parsed from `husk info`'s
`texture_lookup` section (the TXLU replaceable-texture-id reverse map),
not every slot in the texture array; a texture nothing looks up doesn't
matter for whether the render comes out blank. Root cause investigated
interactively 2026-08-15: item/objectcomponents/collections-style
body-fitted armor pieces use a `type=2` ("object_skin") replaceable slot
with `file_data_id=0`, filled in by the live client from
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

import functools
import os
import re
import subprocess
from pathlib import Path

HUSK_BIN = Path("/home/luna/dev/husk/build/husk")
TIMEOUT = 15.0
CORPUS_ROOT = Path("/media/luna/data/wow_export")
LISTFILE = Path("/media/luna/userdata/Downloads/community-listfile.csv")

TEXTURE_LINE_RE = re.compile(r"^\s*texture (\d+): type=(\d+)(?: .*?file_data_id=(\d+))?\s*$")
LOOKUP_LINE_RE = re.compile(r"^\s*texture type \d+(?: \(\w+\))? -> texture (\d+)\s*$")


@functools.lru_cache(maxsize=32)
def _texture_stems_lower(model_dir_str: str) -> tuple[str, ...]:
    """Every non-numeric .blp/.png stem (lowercased) in a directory, one
    real `os.scandir` pass. Measured directly against
    item/objectcomponents/collections (110,337 entries, the single
    largest directory in the corpus): a plain `Path.glob()` per candidate
    check costs ~70ms *per call* there (it re-lists the whole directory
    every time, no cross-call cache), and the old per-file code called it
    up to 2-6x -- 150-400ms of pure directory-listing cost stacked onto
    files that already had nothing to resolve, exactly the kind of tail-
    latency spike that knocked the AdaptiveConcurrency window down (see
    corpus_reports/unfillable_textures_full_stdout.log's real run: window
    oscillating 10-13 once the scan reached this directory). lru_cache
    means each worker process pays the ~70ms scandir cost once per
    directory, not once per file in it -- collections alone amortizes
    across tens of thousands of sibling models.
    """
    try:
        with os.scandir(model_dir_str) as it:
            return tuple(
                e.name.rsplit(".", 1)[0].lower()
                for e in it
                if e.is_file() and e.name.lower().endswith((".blp", ".png"))
            )
    except OSError:
        return ()


@functools.lru_cache(maxsize=1)
def _load_listfile() -> dict[int, str]:
    """FileDataID -> real corpus-relative path, parsed once per worker
    process (measured: ~1s for the real 141.8MB/2,206,298-line
    community-listfile.csv -- negligible against a ~30s full-corpus scan
    when it happens once per worker, not once per file). Same file/format
    husk's own --listfile consumes (src/listfile.cpp's loadListfile):
    'FileDataID;path' per line.
    """
    table: dict[int, str] = {}
    if not LISTFILE.exists():
        return table
    with LISTFILE.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            fdid_str, _, rel_path = line.partition(";")
            if not rel_path:
                continue
            try:
                table[int(fdid_str)] = rel_path.rstrip("\n")
            except ValueError:
                continue
    return table


def _listfile_resolves(fdid: int) -> bool:
    rel_path = _load_listfile().get(fdid)
    if rel_path is None:
        return False
    stem = CORPUS_ROOT / Path(rel_path).with_suffix("")
    return stem.with_suffix(".png").exists() or stem.with_suffix(".blp").exists()


def _has_fuzzy_candidate(model_dir: Path, basename_lower: str) -> bool:
    for stem_lower in _texture_stems_lower(str(model_dir)):
        if stem_lower.isdigit():
            continue  # exact-FileDataID candidates are checked separately
        if stem_lower.startswith(basename_lower):
            return True
    return False


def _run_info(path: Path) -> str | None:
    try:
        p = subprocess.run([str(HUSK_BIN), "info", str(path)], capture_output=True, text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return None
    if p.returncode != 0:
        return None
    return p.stdout


class UnfillableTextureTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = ["used_texture_count", "missing_file_data_ids", "replaceable_only"]
    PARALLEL_MODE = "process"
    # Measured, not copied from another task -- a batch >1 has a real cost
    # under AdaptiveConcurrency's design (it reacts to per-completion
    # latency; a batch's completion is gated by its single slowest member,
    # so one slow file stalls its whole batch's worth of siblings and can
    # trigger a bigger backoff than the actual per-file cost warrants).
    # That's not hypothetical here: an earlier version of _has_fuzzy_
    # candidate() called Path.glob() directly against
    # item/objectcomponents/collections (110,337 files, the corpus's
    # largest single directory) and measured ~70ms/call there -- exactly
    # the kind of spike that knocked the real full-corpus run's window
    # down to 10-13 with visible oscillation. Fixed at the root
    # (_texture_stems_lower's per-directory os.scandir cache, ~70ms once
    # per directory instead of once per file), and re-benchmarked after:
    # against real husk info calls (500-file random sample, this corpus),
    # p99=10.9ms, max=35.8ms -- nowhere near TIMEOUT (15s), so a batch's
    # worst-case stall from one slow member is tens of ms, not seconds.
    # With that bottleneck gone, batch=8 measured 1696 files/sec vs
    # batch=1's 1430 (0 backoffs either way, 6000-file run against
    # collections/ directly) -- the framework's own rationale (amortize
    # IPC dispatch once the real per-file cost is small and uniform)
    # actually applies now. Batching remains a real risk for any task
    # with high-variance per-file cost; it just isn't one for this task,
    # measured, after the actual variance source was removed.
    BATCH_SIZE = 8

    @staticmethod
    def analyze(path: Path) -> dict | None:
        out = _run_info(path)
        if out is None:
            return None

        textures: dict[int, tuple[int, int | None]] = {}
        used_indices: set[int] = set()
        for line in out.splitlines():
            m = TEXTURE_LINE_RE.match(line)
            if m:
                idx, ttype, fdid = int(m.group(1)), int(m.group(2)), m.group(3)
                textures[idx] = (ttype, int(fdid) if fdid else None)
                continue
            m = LOOKUP_LINE_RE.match(line)
            if m:
                used_indices.add(int(m.group(1)))

        if not used_indices:
            return None

        model_dir = path.parent
        basename_lower = path.stem.lower()
        # "_sdr" stand-in character models share their non-"_sdr"
        # counterpart's real texture files (export_texture_resolution.cpp's
        # kSdrSuffix comment) -- without this, every _sdr file's fuzzy pool
        # is unconditionally empty regardless of what's on disk.
        alt_basename = basename_lower[: -len("_sdr")] if basename_lower.endswith("_sdr") else None

        # Per-slot, not per-file: a model with one working texture and one
        # still-blank slot (e.g. a resolved base skin plus an unresolved
        # DB2-driven object_skin slot) must still be reported -- an earlier
        # version broke on the *first* resolved slot and returned None for
        # the whole file, silently hiding every other slot's real gap (found
        # live: helm_leather_pvpdruid_b_02_scm.m2 never appeared in this
        # scan's output at all because one of its two texture slots, an
        # unrelated placeholder, happened to resolve).
        missing_fdids: list[int] = []
        any_real_fdid = False
        all_resolved = True
        for idx in used_indices:
            ttype, fdid = textures.get(idx, (None, None))
            slot_resolved = False
            if fdid:
                any_real_fdid = True
                if (model_dir / f"{fdid}.blp").exists() or (model_dir / f"{fdid}.png").exists():
                    slot_resolved = True
                elif _listfile_resolves(fdid):
                    slot_resolved = True
            if not slot_resolved and _has_fuzzy_candidate(model_dir, basename_lower):
                slot_resolved = True
            if not slot_resolved and alt_basename and _has_fuzzy_candidate(model_dir, alt_basename):
                slot_resolved = True
            if not slot_resolved:
                all_resolved = False
                if fdid:
                    missing_fdids.append(fdid)

        if all_resolved:
            return None
        return {
            "used_texture_count": len(used_indices),
            "missing_file_data_ids": " ".join(str(f) for f in missing_fdids),
            # True when every used slot is a replaceable type (type=2/3/...,
            # no FileDataID at all) -- these have nothing for a CASC
            # re-extraction to fill in (they're not standalone files; the
            # live client composites them from DB2 data at runtime). Only
            # rows with a real missing_file_data_ids entry are an actual
            # extraction gap worth reporting upstream.
            "replaceable_only": not any_real_fdid,
        }

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        extraction_gap = [r for r in rows if not r["replaceable_only"]]
        replaceable_only = [r for r in rows if r["replaceable_only"]]
        all_missing_ids: set[int] = set()
        for r in extraction_gap:
            all_missing_ids.update(int(x) for x in r["missing_file_data_ids"].split())
        return [
            f"{len(rows)} / {total_files} .m2 files have at least one actually-used texture slot "
            f"but NONE of them resolve locally ({len(rows) / total_files:.3%} of the corpus).",
            f"  {len(extraction_gap)} of those have a real, missing FileDataID -- a genuine CASC "
            f"re-extraction gap ({len(all_missing_ids)} distinct FileDataIDs across them).",
            f"  {len(replaceable_only)} of those use only replaceable/DB2-driven texture slots "
            f"(no standalone file exists to extract -- not an extraction gap, out of scope for "
            f"a casc-tool report).",
        ]
