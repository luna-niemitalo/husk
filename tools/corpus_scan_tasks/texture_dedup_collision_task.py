"""Full-corpus scan for TODO/MULTI_TEXTURE_LAYER_TODO.md's open follow-up:
how often does a real .m2 file contain two texture slots whose *resolved
byte content* is identical, in a file that also has at least one
`textureCount > 1` batch (the shape that actually matters -- see below)?

Motivating bug (commit cd11c85, `creature/ladywaycrest/ladywaycrest.m2`):
`render_glb.py`'s `fix_multi_texture_layers()` looked up a multi-texture
batch's 2nd texture layer by its raw FileDataID as a Blender `Image`
datablock name. Blender's glTF importer merges byte-identical *embedded*
images into a single datablock and keeps only one name -- so whenever a
2nd-layer texture happens to share bytes with some other texture already
embedded elsewhere in the same file (a common shape: a recolor variant, or
the same overlay texture reused across several materials), the FDID-named
copy silently never exists as a datablock, and the combiner math got
skipped instead of applied. The fix (rebuilding the fdid->Image map from
each material's own `texture_file_data_id` extra rather than trusting the
merged datablock's name) is general and should cover the collision
regardless of frequency -- this scan quantifies that frequency, not
whether the fix works.

Restricted to files with at least one real `textureCount > 1` batch (same
`.skin` struct read `shader_id_task.py` already established) -- a
byte-identical texture pair in a file with no multi-texture batch at all
can't hit this specific bug, since there's no 2nd-layer lookup to
silently fail.

Byte-identical content is checked only for the two tiers that resolve to
one definite, per-FileDataID file on disk -- the literal `<fdid>.blp/png`
next to the model, and a `--listfile`-resolved real path (the same two
tiers `unfillable_texture_task.py` already established as husk's own
tiers 1/2, `export_materials.cpp:437-456`). Tier 3 (same-basename fuzzy
fallback) resolves every unmatched slot in a directory to the *same*
single local file regardless of FileDataID, which is a different,
already-understood collision shape (not this bug's mechanism -- there's
no distinct "2nd datablock" to lose in the first place), so it's left out
of the hash comparison here.

Run against the real corpus with:

    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.texture_dedup_collision_task:TextureDedupCollisionTask \\
        --root /media/luna/data/wow_export --output-stem texture_dedup_collision
"""
from __future__ import annotations

import functools
import hashlib
import os
import re
import struct
import subprocess
from pathlib import Path

HUSK_BIN = Path("/home/luna/dev/husk/build/husk")
TIMEOUT = 15.0
CORPUS_ROOT = Path("/media/luna/data/wow_export")
LISTFILE = Path("/media/luna/userdata/Downloads/community-listfile.csv")

TEXTURE_LINE_RE = re.compile(r"^\s*texture (\d+): type=(\d+)(?: .*?file_data_id=(\d+))?\s*$")

# src/skin.cpp's Batch field offsets (stride 0x18, batches array at 0x24) --
# only textureCount is needed here, same offset shader_id_task.py already
# transcribed and verified against real data.
_SKIN_BATCHES_OFFSET = 0x24
_SKIN_BATCH_STRIDE = 0x18
_BATCH_TEXTURE_COUNT_OFFSET = 0x0E


def _read_array(data: bytes, offset: int) -> tuple[int, int] | None:
    if offset + 8 > len(data):
        return None
    return struct.unpack_from("<II", data, offset)


def _has_multi_texture_batch(skin_data: bytes) -> bool:
    if skin_data[:4] != b"SKIN":
        return False
    arr = _read_array(skin_data, _SKIN_BATCHES_OFFSET)
    if arr is None:
        return False
    count, off = arr
    if count == 0 or off > len(skin_data) or count > (len(skin_data) - off) // _SKIN_BATCH_STRIDE:
        return False
    for i in range(count):
        base = off + i * _SKIN_BATCH_STRIDE
        texture_count = struct.unpack_from("<H", skin_data, base + _BATCH_TEXTURE_COUNT_OFFSET)[0]
        if texture_count > 1:
            return True
    return False


@functools.lru_cache(maxsize=32)
def _skin_names(model_dir_str: str) -> tuple[str, ...]:
    """One `os.scandir` pass per directory -- same fix as
    unfillable_texture_task.py/dangling_references_task.py for the same
    underlying cause (an uncached `Path.glob()` re-lists the whole
    directory per call, ruinous against item/objectcomponents/collections,
    the corpus's largest directory)."""
    try:
        with os.scandir(model_dir_str) as it:
            return tuple(e.name for e in it if e.is_file() and e.name.lower().endswith(".skin"))
    except OSError:
        return ()


def _find_same_basename_skins(m2_path: Path) -> list[Path]:
    basename = m2_path.stem
    model_dir = m2_path.parent
    candidates = [
        model_dir / name for name in _skin_names(str(model_dir))
        if name.startswith(basename)
    ]
    suffix_re = re.compile(r"^(\d+)\.skin$", re.IGNORECASE)
    two_digit, any_digit = [], []
    for c in candidates:
        rest = c.name[len(basename):]
        m = suffix_re.match(rest)
        if not m:
            continue
        any_digit.append(c)
        if len(m.group(1)) == 2:
            two_digit.append(c)
    return sorted(two_digit) if two_digit else sorted(any_digit)


@functools.lru_cache(maxsize=1)
def _load_listfile() -> dict[int, str]:
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


def _resolve_texture_path(model_dir: Path, fdid: int) -> Path | None:
    for suffix in (".blp", ".png"):
        p = model_dir / f"{fdid}{suffix}"
        if p.exists():
            return p
    rel_path = _load_listfile().get(fdid)
    if rel_path is not None:
        stem = CORPUS_ROOT / Path(rel_path).with_suffix("")
        for suffix in (".png", ".blp"):
            p = stem.with_suffix(suffix)
            if p.exists():
                return p
    return None


def _hash_file(p: Path) -> str | None:
    try:
        return hashlib.sha256(p.read_bytes()).hexdigest()
    except OSError:
        return None


def _run_info(path: Path) -> str | None:
    try:
        p = subprocess.run([str(HUSK_BIN), "info", str(path)], capture_output=True, text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return None
    if p.returncode != 0:
        return None
    return p.stdout


class TextureDedupCollisionTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = ["multi_texture_batches_present", "distinct_fdids_resolved", "collision_groups", "collision_fdid_count"]
    PARALLEL_MODE = "process"  # shells to husk + reads/hashes real texture files
    BATCH_SIZE = 1  # not yet measured against this task's own real per-file cost -- see CORPUS_SCANS.md

    @staticmethod
    def analyze(path: Path) -> dict | None:
        skins = _find_same_basename_skins(path)
        if not skins:
            return None
        if not any(_has_multi_texture_batch(s.read_bytes()) for s in skins if s.exists()):
            return None  # no multi-texture batch in this file -- the bug's mechanism can't apply

        out = _run_info(path)
        if out is None:
            return None

        fdids: set[int] = set()
        for line in out.splitlines():
            m = TEXTURE_LINE_RE.match(line)
            if m and m.group(3):
                fdids.add(int(m.group(3)))
        if len(fdids) < 2:
            return None  # need at least 2 distinct textures for a collision to even be possible

        model_dir = path.parent
        hash_to_fdids: dict[str, set[int]] = {}
        resolved = 0
        for fdid in fdids:
            local_path = _resolve_texture_path(model_dir, fdid)
            if local_path is None:
                continue
            digest = _hash_file(local_path)
            if digest is None:
                continue
            resolved += 1
            hash_to_fdids.setdefault(digest, set()).add(fdid)

        collision_groups = [sorted(g) for g in hash_to_fdids.values() if len(g) > 1]
        if not collision_groups:
            return None

        return {
            "multi_texture_batches_present": True,
            "distinct_fdids_resolved": resolved,
            "collision_groups": ";".join(",".join(str(f) for f in g) for g in collision_groups),
            "collision_fdid_count": sum(len(g) for g in collision_groups),
        }

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        total_collision_fdids = sum(r["collision_fdid_count"] for r in rows)
        return [
            f"{len(rows)} / {total_files} .m2 files ({len(rows) / total_files:.3%}) have both a real "
            f"textureCount > 1 batch AND at least two distinct FileDataIDs whose resolved texture bytes "
            f"are identical -- the shape that broke ladywaycrest's 2nd-layer Image lookup.",
            f"{total_collision_fdids} total FileDataIDs involved in a collision group across those files.",
        ]
