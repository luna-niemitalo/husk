"""corpus_scan_framework.ScanTask for TODO/CLEANUP_TODO.md's "dangling
internal reference" scan -- the deliberate counterweight to every
presence-only completeness metric this project tracks (M2_COMPLETENESS.md,
the corpus scans in this directory). Presence answers "does this file have
an animated transform/light/particle/whatever"; this answers the narrower,
harder question: of the internal cross-references husk already knows how to
resolve, how many actually resolve to something real, corpus-wide, per
reference kind?

Motivating example (TODO/CLEANUP_TODO.md's own): a real file can have a
genuinely-animated M2TextureTransform record that no batch's own
textureTransformComboIndex ever actually points at -- the data exists and
parses cleanly, but nothing in the file reaches it. A presence check counts
that file as a hit; this scan counts it as a dangling reference instead.

Two tiers, matching the real cost split TODO/CLEANUP_TODO.md itself called
out (some kinds need only the .m2, others need a matched .skin sibling
resolved first):

  M2-only (cheap, one file read):
    bone_lookup        boneLookup[i]        -> bones[.]        (0xFFFF = unused)
    sequence_lookup     sequenceLookup[i]     -> sequences[.]    (0xFFFF = unused)
    attachment_lookup   attachmentLookup[i]   -> attachments[.]  (0xFFFF = unused)
    camera_lookup       cameraLookup[i]       -> cameras[.]      (0xFFFF = unused)
    texture_lookup      textureLookup[i]      -> textures[.]     (0xFFFF = unused)

  Needs a matched same-basename .skin (every LOD sibling found, not just one):
    material_index        batch.materialIndex               -> materials[.]        (no sentinel)
    color_index            batch.colorIndex                  -> colors[.]           (0xFFFF = unused)
    texture_combo          batch.textureComboIndex.. +textureCount -> textureCombos[.] -> textures[.]
    texture_weight_combo   batch.textureWeightComboIndex     -> textureWeightCombos[.] -> textureWeights[.]
    texture_transform_combo batch.textureTransformComboIndex (0xFFFF = unused) -> textureTransformCombos[.] -> textureTransforms[.]

Deliberately excluded, not just forgotten:

  - `textureCoordComboIndex` (skin.hpp's own Batch doc comment, and
    WIKI_FINDINGS/M2/skin.md): a real corpus file already confirmed to
    carry values outside the documented -1/0/1 range, believed vestigial.
    Treating it as a must-resolve reference would manufacture false
    "dangling" hits out of an already-documented quirk, not surface real
    signal.
  - TXID `textureFileDataIds` vs. local file / --listfile presence: the
    open question TODO/CLEANUP_TODO.md itself flagged as unresolved --
    README.md's own prior scan found 99.9% of "missing" FileDataIDs were
    actually present under their real listfile name, not truly absent, so
    a naive "no local file" check would mostly measure listfile coverage,
    not real dangling references. Left for a future, listfile-aware pass
    rather than guessed at here.

Field offsets are the same fixed values husk's own C++ source uses --
src/m2_primitives.cpp's `offset::` table (M2 header arrays) and
src/skin.cpp's Batch field offsets (0x24 batches array, 0x18 stride) --
transcribed here rather than imported, matching every other find_*.py/
*_task.py tool's "small, self-contained, independent of husk's own code"
precedent (the same "second opinion" discipline corpus_checks.py's
read_m2_header_summary already uses).

Output shape: one row per real (parseable MD20/MD21) .m2 file, every
reference kind's own (checked, dangling) count as a column pair -- not
filtered to only-dangling files, since the scan's whole point is a
corpus-wide *rate* per kind, and the denominator (how many references of
that kind actually exist to check) varies by kind and needs the full
accounting, not just the hits. `dangling_examples` carries a compact,
human-readable sample of the first few real dangling hits per file, so a
CSV reader doesn't have to re-derive what specifically went wrong.

Usage:
    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.dangling_references_task:DanglingReferencesTask \\
        --root /media/luna/data/wow_export --output-stem dangling_references
"""

import functools
import os
import re
import struct
from pathlib import Path

_SENTINEL = 0xFFFF

# src/m2_primitives.cpp's offset:: table -- M2 header array offsets.
_OFF_SEQUENCES = 0x01C
_OFF_SEQUENCE_LOOKUP = 0x024
_OFF_BONES = 0x02C
_OFF_BONE_LOOKUP = 0x034
_OFF_COLORS = 0x048
_OFF_TEXTURES = 0x050
_OFF_TEXTURE_WEIGHTS = 0x058
_OFF_TEXTURE_TRANSFORMS = 0x060
_OFF_TEXTURE_LOOKUP = 0x068
_OFF_MATERIALS = 0x070
_OFF_TEXTURE_COMBOS = 0x080
_OFF_TEXTURE_WEIGHT_COMBOS = 0x090
_OFF_TEXTURE_TRANSFORM_COMBOS = 0x098
_OFF_ATTACHMENTS = 0x0F0
_OFF_ATTACHMENT_LOOKUP = 0x0F8
_OFF_CAMERAS = 0x110
_OFF_CAMERA_LOOKUP = 0x118
_MIN_HEADER_SIZE = 0x130  # offset::minHeaderSize (particleEmitters + 8)

# src/skin.cpp's Batch field offsets (stride 0x18, batches array at 0x24).
_SKIN_BATCHES_OFFSET = 0x24
_SKIN_BATCH_STRIDE = 0x18
_BATCH_COLOR_INDEX_OFFSET = 0x08
_BATCH_MATERIAL_INDEX_OFFSET = 0x0A
_BATCH_TEXTURE_COUNT_OFFSET = 0x0E
_BATCH_TEXTURE_COMBO_INDEX_OFFSET = 0x10
_BATCH_TEXTURE_WEIGHT_COMBO_INDEX_OFFSET = 0x14
_BATCH_TEXTURE_TRANSFORM_COMBO_INDEX_OFFSET = 0x16

_M2_ONLY_KINDS = [
    ("bone_lookup", _OFF_BONE_LOOKUP, _OFF_BONES),
    ("sequence_lookup", _OFF_SEQUENCE_LOOKUP, _OFF_SEQUENCES),
    ("attachment_lookup", _OFF_ATTACHMENT_LOOKUP, _OFF_ATTACHMENTS),
    ("camera_lookup", _OFF_CAMERA_LOOKUP, _OFF_CAMERAS),
    ("texture_lookup", _OFF_TEXTURE_LOOKUP, _OFF_TEXTURES),
]
_SKIN_KINDS = [
    "material_index", "color_index", "texture_combo",
    "texture_weight_combo", "texture_transform_combo",
]
_ALL_KINDS = [k for k, _, _ in _M2_ONLY_KINDS] + _SKIN_KINDS
_MAX_EXAMPLES_PER_FILE = 5


def _extract_md20_blob(data: bytes) -> bytes | None:
    """Same MD20/MD21 chunk-container walk every find_*.py tool in this
    directory duplicates rather than imports."""
    if len(data) < 8:
        return None
    if data[0:4] == b"MD20":
        return data
    pos = 0
    while pos + 8 <= len(data):
        ctag = data[pos:pos + 4]
        csize = struct.unpack_from("<I", data, pos + 4)[0]
        payload_start = pos + 8
        if payload_start + csize > len(data):
            return None
        if ctag == b"MD21":
            return data[payload_start:payload_start + csize]
        pos = payload_start + csize
    return None


def _read_array(blob: bytes, off: int) -> tuple[int, int] | None:
    if off + 8 > len(blob):
        return None
    return struct.unpack_from("<II", blob, off)


def _read_count(blob: bytes, off: int) -> int:
    arr = _read_array(blob, off)
    return arr[0] if arr is not None else 0


def _read_uint16_array(blob: bytes, off: int) -> list[int] | None:
    arr = _read_array(blob, off)
    if arr is None:
        return None
    count, data_off = arr
    if count == 0:
        return []
    if data_off > len(blob) or count > (len(blob) - data_off) // 2:
        return None  # claims more than the file actually holds -- corrupt/truncated, skip
    return list(struct.unpack_from(f"<{count}H", blob, data_off))


def _check_lookup_array(blob: bytes, lookup_off: int, target_count: int,
                         kind: str) -> tuple[int, int, list[str]]:
    """One M2-only lookup array's own (checked, dangling, examples)."""
    values = _read_uint16_array(blob, lookup_off)
    if not values:
        return 0, 0, []
    checked = dangling = 0
    examples = []
    for i, v in enumerate(values):
        if v == _SENTINEL:
            continue
        checked += 1
        if v >= target_count:
            dangling += 1
            if len(examples) < _MAX_EXAMPLES_PER_FILE:
                examples.append(f"{kind}[{i}]={v}>={target_count}")
    return checked, dangling, examples


@functools.lru_cache(maxsize=32)
def _skin_names(model_dir_str: str) -> tuple[str, ...]:
    """Every .skin filename (original case) in a directory, one real
    `os.scandir` pass. Same fix as unfillable_texture_task.py's
    `_texture_stems_lower` for the same underlying cause: a plain
    `Path.glob()` per file re-lists the whole directory every call, and
    item/objectcomponents/collections (the corpus's largest directory)
    holds thousands of .m2 files itself -- an uncached glob there costs
    ~70ms per call, stacked onto every one of those files. lru_cache pays
    the scandir cost once per directory, not once per file in it.
    """
    try:
        with os.scandir(model_dir_str) as it:
            return tuple(e.name for e in it if e.is_file() and e.name.lower().endswith(".skin"))
    except OSError:
        return ()


def _find_same_basename_skins(m2_path: Path) -> list[Path]:
    """Same-basename numeric-suffix .skin lookup (every LOD sibling), same
    convention find_texture_transform_files.py/find_texture_type_collisions.py
    already establish -- prefer an exact 2-digit suffix, WoW's own real
    naming convention, falling back to any-digit-run.
    """
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


def _check_one_batch(data: bytes, b_off: int, counts: dict, tallies: dict, examples: list) -> None:
    """Runs all 5 skin-dependent checks for one batch record, mutating
    `tallies` (kind -> [checked, dangling]) and appending to `examples`.
    """
    material_index = struct.unpack_from("<H", data, b_off + _BATCH_MATERIAL_INDEX_OFFSET)[0]
    tallies["material_index"][0] += 1
    if material_index >= counts["materials"]:
        tallies["material_index"][1] += 1
        if len(examples) < _MAX_EXAMPLES_PER_FILE:
            examples.append(f"material_index={material_index}>={counts['materials']}")

    color_index = struct.unpack_from("<H", data, b_off + _BATCH_COLOR_INDEX_OFFSET)[0]
    if color_index != _SENTINEL:
        tallies["color_index"][0] += 1
        if color_index >= counts["colors"]:
            tallies["color_index"][1] += 1
            if len(examples) < _MAX_EXAMPLES_PER_FILE:
                examples.append(f"color_index={color_index}>={counts['colors']}")

    texture_count = struct.unpack_from("<H", data, b_off + _BATCH_TEXTURE_COUNT_OFFSET)[0]
    texture_combo_index = struct.unpack_from("<H", data, b_off + _BATCH_TEXTURE_COMBO_INDEX_OFFSET)[0]
    for j in range(texture_count):
        combo_idx = texture_combo_index + j
        tallies["texture_combo"][0] += 1
        if combo_idx >= counts["texture_combos"]:
            tallies["texture_combo"][1] += 1
            if len(examples) < _MAX_EXAMPLES_PER_FILE:
                examples.append(f"texture_combo[{combo_idx}]>={counts['texture_combos']}(combos)")
            continue
        texture_idx = counts["texture_combos_values"][combo_idx]
        if texture_idx >= counts["textures"]:
            tallies["texture_combo"][1] += 1
            if len(examples) < _MAX_EXAMPLES_PER_FILE:
                examples.append(f"texture_combo[{combo_idx}]->{texture_idx}>={counts['textures']}(textures)")

    weight_combo_index = struct.unpack_from(
        "<H", data, b_off + _BATCH_TEXTURE_WEIGHT_COMBO_INDEX_OFFSET)[0]
    tallies["texture_weight_combo"][0] += 1
    if weight_combo_index >= counts["texture_weight_combos"]:
        tallies["texture_weight_combo"][1] += 1
        if len(examples) < _MAX_EXAMPLES_PER_FILE:
            examples.append(
                f"texture_weight_combo={weight_combo_index}>={counts['texture_weight_combos']}(combos)")
    else:
        weight_idx = counts["texture_weight_combos_values"][weight_combo_index]
        if weight_idx >= counts["texture_weights"]:
            tallies["texture_weight_combo"][1] += 1
            if len(examples) < _MAX_EXAMPLES_PER_FILE:
                examples.append(
                    f"texture_weight_combo->{weight_idx}>={counts['texture_weights']}(weights)")

    transform_combo_index = struct.unpack_from(
        "<H", data, b_off + _BATCH_TEXTURE_TRANSFORM_COMBO_INDEX_OFFSET)[0]
    if transform_combo_index != _SENTINEL:
        tallies["texture_transform_combo"][0] += 1
        if transform_combo_index >= counts["texture_transform_combos"]:
            tallies["texture_transform_combo"][1] += 1
            if len(examples) < _MAX_EXAMPLES_PER_FILE:
                examples.append(
                    f"texture_transform_combo={transform_combo_index}"
                    f">={counts['texture_transform_combos']}(combos)")
        else:
            transform_idx = counts["texture_transform_combos_values"][transform_combo_index]
            # Unlike textureCombos/textureWeightCombos (below), husk's own real code
            # (export_materials.cpp) treats an out-of-range resolved transformIndex as
            # best-effort/skip, not a hard error -- 0xFFFF here is a real, expected
            # per-slot "no transform" sentinel *inside the combo table itself*, not
            # just on the batch's own textureTransformComboIndex field above. Confirmed
            # empirically: an unfiltered first pass of this scan found a 64% "dangling"
            # rate here, which collapsed to real, low signal once this sentinel was
            # excluded -- see TODO/CLEANUP_TODO.md's own entry for this scan.
            if transform_idx != _SENTINEL and transform_idx >= counts["texture_transforms"]:
                tallies["texture_transform_combo"][1] += 1
                if len(examples) < _MAX_EXAMPLES_PER_FILE:
                    examples.append(
                        f"texture_transform_combo->{transform_idx}"
                        f">={counts['texture_transforms']}(transforms)")


def _check_skin_dependent_kinds(blob: bytes, m2_path: Path) -> tuple[dict, list[str], bool]:
    """Returns (tallies, examples, skin_found) -- tallies maps each of
    _SKIN_KINDS to [checked, dangling]. Aggregates across every matched
    same-basename .skin sibling (every LOD), not just one.
    """
    tallies = {k: [0, 0] for k in _SKIN_KINDS}
    examples: list[str] = []
    skins = _find_same_basename_skins(m2_path)
    if not skins:
        return tallies, examples, False

    counts = {
        "materials": _read_count(blob, _OFF_MATERIALS),
        "colors": _read_count(blob, _OFF_COLORS),
        "textures": _read_count(blob, _OFF_TEXTURES),
        "texture_weights": _read_count(blob, _OFF_TEXTURE_WEIGHTS),
        "texture_transforms": _read_count(blob, _OFF_TEXTURE_TRANSFORMS),
    }
    texture_combos_values = _read_uint16_array(blob, _OFF_TEXTURE_COMBOS) or []
    weight_combos_values = _read_uint16_array(blob, _OFF_TEXTURE_WEIGHT_COMBOS) or []
    transform_combos_values = _read_uint16_array(blob, _OFF_TEXTURE_TRANSFORM_COMBOS) or []
    counts["texture_combos"] = len(texture_combos_values)
    counts["texture_combos_values"] = texture_combos_values
    counts["texture_weight_combos"] = len(weight_combos_values)
    counts["texture_weight_combos_values"] = weight_combos_values
    counts["texture_transform_combos"] = len(transform_combos_values)
    counts["texture_transform_combos_values"] = transform_combos_values

    found_any = False
    for skin_path in skins:
        try:
            data = skin_path.read_bytes()
        except OSError:
            continue
        if len(data) < 4 or data[0:4] != b"SKIN":
            continue
        arr = _read_array(data, _SKIN_BATCHES_OFFSET)
        if arr is None:
            continue
        count, off = arr
        if off > len(data) or count > (len(data) - off) // _SKIN_BATCH_STRIDE:
            continue  # claims more than the file holds -- corrupt/truncated, skip
        found_any = True
        for i in range(count):
            b_off = off + i * _SKIN_BATCH_STRIDE
            _check_one_batch(data, b_off, counts, tallies, examples)
    return tallies, examples, found_any


class DanglingReferencesTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = [f"{k}_{suffix}" for k in _ALL_KINDS for suffix in ("checked", "dangling")] + [
        "skin_found", "dangling_examples",
    ]
    PARALLEL_MODE = "process"  # struct-unpack + a handful of extra file reads, no subprocess calls

    @staticmethod
    def analyze(path: Path) -> dict | None:
        try:
            data = path.read_bytes()
        except OSError:
            return None
        blob = _extract_md20_blob(data)
        if blob is None or len(blob) < _MIN_HEADER_SIZE or blob[0:4] != b"MD20":
            return None

        row: dict = {}
        examples: list[str] = []
        for kind, lookup_off, target_off in _M2_ONLY_KINDS:
            target_count = _read_count(blob, target_off)
            checked, dangling, kind_examples = _check_lookup_array(blob, lookup_off, target_count, kind)
            row[f"{kind}_checked"] = checked
            row[f"{kind}_dangling"] = dangling
            examples.extend(kind_examples)

        skin_tallies, skin_examples, skin_found = _check_skin_dependent_kinds(blob, path)
        for kind in _SKIN_KINDS:
            row[f"{kind}_checked"] = skin_tallies[kind][0]
            row[f"{kind}_dangling"] = skin_tallies[kind][1]
        examples.extend(skin_examples)
        row["skin_found"] = int(skin_found)
        row["dangling_examples"] = "; ".join(examples[:_MAX_EXAMPLES_PER_FILE])
        return row

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        lines = [
            f"{len(rows)} / {total_files} .m2 files parsed and checked "
            f"({len(rows) / total_files:.3%} of the corpus)" if total_files else "0 files scanned",
        ]
        skin_found_count = sum(r["skin_found"] for r in rows)
        lines.append(
            f"  {skin_found_count} / {len(rows)} had >=1 resolvable same-basename .skin sibling "
            f"(gates material_index/color_index/texture_combo/texture_weight_combo/"
            f"texture_transform_combo below)")
        for kind in _ALL_KINDS:
            checked = sum(r[f"{kind}_checked"] for r in rows)
            dangling = sum(r[f"{kind}_dangling"] for r in rows)
            rate = f"{dangling / checked:.3%}" if checked else "n/a"
            lines.append(f"  {kind}: {dangling} / {checked} references dangling ({rate})")
        files_with_any_dangling = sum(
            1 for r in rows if any(r[f"{k}_dangling"] > 0 for k in _ALL_KINDS))
        lines.append(
            f"  {files_with_any_dangling} file(s) have >=1 dangling reference of any kind -- see "
            f"dangling_examples column for a sample of each")
        return lines
