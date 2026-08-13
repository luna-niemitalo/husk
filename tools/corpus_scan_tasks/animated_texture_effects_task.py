"""corpus_scan_framework.ScanTask for TODO/ANIMATED_TEXTURE_EFFECTS_TODO.md's
own first open question: how many real corpus files actually have a
genuinely-animated (not merely constant) M2TextureTransform / M2Color tint
/ M2Color alpha (fade) / M2TextureWeight track -- i.e. how big is the
"spinning sigil / pulsing rune / scrolling beam" visual gap really, before
sinking time into playback infrastructure for it.

Independent of husk's own code (same "second opinion" discipline
corpus_checks.py's read_m2_header_summary and find_texture_transform_files.py
already use) -- reads the M2 header's colors/textureWeights/textureTransforms
arrays and each record's raw M2Track bytes directly, offsets transcribed
from the same constants find_texture_transform_files.py/
find_texture_type_collisions.py already established against real files:

    colors            0x048  (M2Array<M2Color>,  stride 0x28: color M2Track<C3Vector> @0x00, alpha M2Track<fixed16> @0x14)
    textureWeights    0x058  (M2Array<M2TextureWeight>, stride 0x14: weight M2Track<fixed16> @0x00)
    textureTransforms 0x060  (M2Array<M2TextureTransform>, stride 0x3C: translation @0x00, rotation @0x14, scaling @0x28)

"Genuinely animated" mirrors husk's own resolveAnimatedColorCurve/
resolveAnimatedFixed16Curve (src/export_materials.cpp): a track counts only
if it has real value data (values-outer M2Array count >= 1) AND that data
isn't reducible to husk's own "constant" case (outer count == 1 with an
inner count <= 1, i.e. exactly one value for the whole track, no per-
sequence/global-sequence variation at all) -- the same distinction
find_texture_transform_files.py's read_constant_track_value_offset makes,
just inverted (that script hunts constant transforms as fixture candidates;
this one hunts the opposite, the genuinely-animated case this TODO's
playback gap is actually about).

This does NOT try to determine whether a genuinely-animated track is
actually *reachable* at runtime (referenced by a real batch/material the
way find_texture_transform_files.py's skin-batch cross-check does for
texture transforms) -- that cross-check only exists for textureTransforms
today (via textureTransformCombos + .skin batch data) and has no equivalent
for colors/textureWeights (those are referenced by M2Material color index,
a materials-to-geoset mapping this scanner deliberately doesn't chase, since
the TODO's own scoping question is "how many files carry this kind of
curve at all", not "how many render it"). Kept as a documented gap rather
than a guess.

Usage:
    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.animated_texture_effects_task:AnimatedTextureEffectsTask \\
        --root /media/luna/data/wow_export --output-stem animated_texture_effects
"""

import struct
from pathlib import Path

_MIN_HEADER_SIZE = 0x0A0

_M2_COLORS_OFFSET = 0x048
_COLOR_STRIDE = 0x28
_COLOR_TRACK_OFFSET = 0x00
_ALPHA_TRACK_OFFSET = 0x14

_M2_TEXTURE_WEIGHTS_OFFSET = 0x058
_WEIGHT_STRIDE = 0x14
_WEIGHT_TRACK_OFFSET = 0x00

_M2_TEXTURE_TRANSFORMS_OFFSET = 0x060
_TRANSFORM_STRIDE = 0x3C
_TRANSLATION_TRACK_OFFSET = 0x00
_ROTATION_TRACK_OFFSET = 0x14
_SCALING_TRACK_OFFSET = 0x28

_TRACK_VALUES_OUTER_OFFSET = 0x0C  # M2Track<T>: values outer M2Array, after a 4-byte header + 8-byte timestamps array


def _extract_md20_blob(data: bytes) -> bytes | None:
    """Same MD20/MD21 chunk-container walk every find_*.py tool in this
    directory duplicates rather than imports (self-contained one-off
    exploration tools, per this repo's own established precedent)."""
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


def _track_is_genuinely_animated(blob: bytes, track_off: int) -> bool:
    outer = _read_array(blob, track_off + _TRACK_VALUES_OUTER_OFFSET)
    if outer is None or outer[0] == 0:
        return False  # no value data at all -- empty track
    if outer[0] == 1:
        inner = _read_array(blob, outer[1])
        if inner is None or inner[0] <= 1:
            return False  # husk's own "constant" case: one value, no variation
    return True  # >1 sequence carries data, or the single sequence has >1 keyframe


def _count_animated_records(blob: bytes, array_off: int, stride: int,
                             track_offsets: list[int]) -> int:
    """Number of records at `array_off` where ANY of `track_offsets`
    (byte offsets of an M2Track within one record) is genuinely animated.
    Returns 0 (not None) for "array present but nothing animated" or "array
    empty" -- None-vs-0 isn't a distinction this scan needs, unlike
    find_texture_transform_files.py's fixture-hunting use case.
    """
    arr = _read_array(blob, array_off)
    if arr is None:
        return 0
    count, base_off = arr
    if count == 0:
        return 0
    if base_off > len(blob) or count > (len(blob) - base_off) // stride:
        return 0  # header claims more than the file holds -- corrupt/truncated, skip
    hits = 0
    for i in range(count):
        rec_off = base_off + i * stride
        if any(_track_is_genuinely_animated(blob, rec_off + t) for t in track_offsets):
            hits += 1
    return hits


class AnimatedTextureEffectsTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = [
        "animated_transform_count", "animated_tint_count",
        "animated_fade_count", "animated_weight_count",
    ]
    PARALLEL_MODE = "process"  # pure struct-unpack CPU work, no subprocess calls
    BATCH_SIZE = 8  # analyze() is cheap (one file read + a handful of small array scans)

    @staticmethod
    def analyze(path: Path) -> dict | None:
        try:
            data = path.read_bytes()
        except OSError:
            return None
        blob = _extract_md20_blob(data)
        if blob is None or len(blob) < _MIN_HEADER_SIZE or blob[0:4] != b"MD20":
            return None

        transform_count = _count_animated_records(
            blob, _M2_TEXTURE_TRANSFORMS_OFFSET, _TRANSFORM_STRIDE,
            [_TRANSLATION_TRACK_OFFSET, _ROTATION_TRACK_OFFSET, _SCALING_TRACK_OFFSET])
        tint_count = _count_animated_records(
            blob, _M2_COLORS_OFFSET, _COLOR_STRIDE, [_COLOR_TRACK_OFFSET])
        fade_count = _count_animated_records(
            blob, _M2_COLORS_OFFSET, _COLOR_STRIDE, [_ALPHA_TRACK_OFFSET])
        weight_count = _count_animated_records(
            blob, _M2_TEXTURE_WEIGHTS_OFFSET, _WEIGHT_STRIDE, [_WEIGHT_TRACK_OFFSET])

        if transform_count == 0 and tint_count == 0 and fade_count == 0 and weight_count == 0:
            return None  # only report files that actually have >=1 animated effect

        return {
            "animated_transform_count": transform_count,
            "animated_tint_count": tint_count,
            "animated_fade_count": fade_count,
            "animated_weight_count": weight_count,
        }

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        any_effect = len(rows)
        transform_files = sum(1 for r in rows if r["animated_transform_count"] > 0)
        tint_files = sum(1 for r in rows if r["animated_tint_count"] > 0)
        fade_files = sum(1 for r in rows if r["animated_fade_count"] > 0)
        weight_files = sum(1 for r in rows if r["animated_weight_count"] > 0)
        return [
            f"{any_effect} / {total_files} .m2 files carry >=1 genuinely-animated "
            f"transform/tint/fade/weight track ({any_effect / total_files:.3%} of the corpus)"
            if total_files else "0 files scanned",
            f"  {transform_files} have an animated M2TextureTransform "
            f"(UV scroll/rotate/scale) -- {sum(r['animated_transform_count'] for r in rows)} total records",
            f"  {tint_files} have an animated M2Color tint -- "
            f"{sum(r['animated_tint_count'] for r in rows)} total records",
            f"  {fade_files} have an animated M2Color alpha (fade) -- "
            f"{sum(r['animated_fade_count'] for r in rows)} total records",
            f"  {weight_files} have an animated M2TextureWeight -- "
            f"{sum(r['animated_weight_count'] for r in rows)} total records",
            "note: presence only -- doesn't check whether the animated record is actually "
            "referenced by a real batch/material (see this file's own module doc comment)",
        ]
