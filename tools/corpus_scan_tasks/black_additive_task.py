"""Full-corpus scan for models likely to render as flat background color
because their only resolved texture is genuinely black and every material
is additive-family -- the class TODO/RENDER_QUALITY_TODO.md section 1
found for `creature/deathwingcorruptedjaw/deathwingcorruptedjaw.m2`
(blend_mode=4, 0 particles, texture resolves via --listfile to a real
32x32 all-black `interface/characterframe/ui-party-background.blp`).

`particle_only_task.py` catches the *particle-driven* half of this same
underlying symptom (additive-only materials, blank in practice) but only
when there's also a real M2Particle emitter to blame -- deathwingcorrupted-
jaw has none, so that task's own candidate filter can't see it. This task
is the real, scoped follow-up RENDER_QUALITY_TODO.md's section 1 named but
never built: instead of proxying through particle-presence, resolve each
additive-only file's own primary texture (same three-tier resolution
unfillable_texture_task.py already mirrors -- literal FileDataID, then
--listfile, then same-basename fuzzy) and check its *actual* decoded pixel
brightness. A `.blp` match is converted to `.png` via `husk blp-export`,
cached by FileDataID under the system temp dir (mirrors render_glb.py's
own `_convert_blp_to_png_cached`) -- decoding a raw `.blp` directly isn't
implemented in Python anywhere in this repo, and re-implementing DXT/
palette decode here would duplicate `blp/`'s own real decoder for no
reason.

Deliberately approximate, like particle_only_task.py before it: only the
*first* resolved used-texture slot is checked, not every material's own
slot -- multi-material models could have one dark and one bright texture,
which this task would miss. Good enough for a candidate list to spot-check
against corpus_reports/renders_full, not a proof.

The structural read (textures/lookup/materials/particle count) consumes
`husk info --json` (REFACTOR/CLI_AND_TOOLING.md §3) instead of scraping
prose. Texture *pixel bytes* are a separate matter -- husk has no verb
that hands back decoded pixels, so this task still shells out to `husk
blp-export` itself (see _decode_mean_brightness below), deliberately left
alone by this conversion pass; that half belongs to the resource-catalog
work `RESOURCE_CATALOG.md`'s own verdict table names for this task.

Run with:
    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.black_additive_task:BlackAdditiveTask \\
        --root /media/luna/data/wow_export --output-stem black_additive_candidates
"""
from __future__ import annotations

import functools
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import corpus_scan_framework as csf  # noqa: E402 -- see sys.path.insert above; ROOT/LISTFILE/HUSK_BIN/husk_info_json read from there, see REFACTOR/CLI_AND_TOOLING.md §3/§4

HUSK_BIN = csf.HUSK_BIN
TIMEOUT = 15.0
BLP_TIMEOUT = 10.0
BLP_CACHE_DIR = Path(tempfile.gettempdir()) / "husk_black_additive_blp_cache"

# Same additive-family threshold particle_only_task.py uses -- 0 (Opaque)/
# 1 (AlphaKey) are ordinary surfaces, 2 (Alpha) is real translucency (glass,
# cloth), not additive. See wowdev.wiki M2#Materials / export_materials.cpp.
ADDITIVE_FAMILY_THRESHOLD = 3
# Mean 0-255 RGB brightness below this counts as "genuinely black" -- same
# order of magnitude as the pixel-flatness std<1.0 threshold
# RENDER_QUALITY_TODO.md's own blank-render scan already used.
BLACK_MEAN_THRESHOLD = 4.0


@functools.lru_cache(maxsize=32)
def _texture_stems_lower(model_dir_str: str) -> tuple[str, ...]:
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
    table: dict[int, str] = {}
    if not csf.LISTFILE.exists():
        return table
    with csf.LISTFILE.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            fdid_str, _, rel_path = line.partition(";")
            if not rel_path:
                continue
            try:
                table[int(fdid_str)] = rel_path.rstrip("\n")
            except ValueError:
                continue
    return table


def _fuzzy_candidate_path(model_dir: Path, basename_lower: str) -> Path | None:
    for name in sorted(os.listdir(model_dir)) if model_dir.is_dir() else ():
        stem, _, ext = name.rpartition(".")
        if ext.lower() not in ("blp", "png") or stem.lower().isdigit():
            continue
        if stem.lower().startswith(basename_lower):
            return model_dir / name
    return None


def _resolve_texture_path(model_dir: Path, basename_lower: str, fdid: int | None) -> Path | None:
    """Mirrors husk's own three resolution tiers in order
    (export_materials.cpp:437-456). Returns a real local path, .blp or
    .png, or None if nothing resolves.
    """
    if fdid:
        for ext in (".blp", ".png"):
            candidate = model_dir / f"{fdid}{ext}"
            if candidate.exists():
                return candidate
        rel_path = _load_listfile().get(fdid)
        if rel_path is not None:
            stem = csf.ROOT / Path(rel_path).with_suffix("")
            for ext in (".png", ".blp"):
                candidate = stem.with_suffix(ext)
                if candidate.exists():
                    return candidate
    candidate = _fuzzy_candidate_path(model_dir, basename_lower)
    if candidate is not None:
        return candidate
    return None


def _blp_cache_path(fdid_or_hash: str) -> Path:
    BLP_CACHE_DIR.mkdir(parents=True, exist_ok=True)
    return BLP_CACHE_DIR / f"{fdid_or_hash}.png"


def _decode_mean_brightness(image_path: Path) -> float | None:
    from PIL import Image

    png_path = image_path
    if image_path.suffix.lower() == ".blp":
        cache_key = f"{image_path.stat().st_size}_{image_path.name}".replace("/", "_")
        cached = _blp_cache_path(cache_key)
        if not cached.exists():
            try:
                p = subprocess.run(
                    [str(HUSK_BIN), "blp-export", str(image_path), str(cached)],
                    capture_output=True, text=True, timeout=BLP_TIMEOUT,
                )
            except subprocess.TimeoutExpired:
                return None
            if p.returncode != 0 or not cached.exists():
                return None
        png_path = cached

    try:
        with Image.open(png_path) as im:
            im = im.convert("RGB")
            hist = im.histogram()
    except Exception:
        return None

    # Mean over R/G/B channels from the histogram, cheaper than a full
    # numpy mean and this repo has no numpy dependency in tools/venv.
    total_pixels = im.size[0] * im.size[1]
    if total_pixels == 0:
        return None
    channel_means = []
    for c in range(3):
        channel_hist = hist[c * 256:(c + 1) * 256]
        channel_sum = sum(v * count for v, count in enumerate(channel_hist))
        channel_means.append(channel_sum / total_pixels)
    return sum(channel_means) / 3.0


class BlackAdditiveTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = ["material_count", "blend_modes", "particle_emitter_count", "resolved_texture", "mean_brightness"]
    PARALLEL_MODE = "process"
    BATCH_SIZE = 1  # per-file cost is dominated by an occasional blp-export + PIL decode, not IPC dispatch

    @staticmethod
    def analyze(path: Path) -> dict | None:
        info = csf.husk_info_json(path, timeout=TIMEOUT)
        if info is None:
            return None

        fdid_by_index: dict[int, int | None] = {
            t["index"]: t["file_data_id"] for t in info["textures"]["entries"]
        }
        used_indices = {e["texture_index"] for e in info["texture_lookup"].get("entries", [])}
        blend_modes = [m["blend_mode"] for m in info["materials"]["entries"]]
        particle_count = info["particle_emitters"]["count"]

        if not blend_modes or not used_indices:
            return None
        if any(b < ADDITIVE_FAMILY_THRESHOLD for b in blend_modes):
            return None  # has an ordinary opaque/alpha-tested surface -- not a candidate

        model_dir = path.parent
        basename_lower = path.stem.lower()
        first_idx = min(used_indices)
        fdid = fdid_by_index.get(first_idx)

        resolved_path = _resolve_texture_path(model_dir, basename_lower, fdid)
        if resolved_path is None:
            return None  # unfillable_texture_task.py's job, not this one

        mean_brightness = _decode_mean_brightness(resolved_path)
        if mean_brightness is None or mean_brightness >= BLACK_MEAN_THRESHOLD:
            return None

        return {
            "material_count": len(blend_modes),
            "blend_modes": ";".join(str(b) for b in blend_modes),
            "particle_emitter_count": particle_count,
            "resolved_texture": str(resolved_path.relative_to(csf.ROOT)) if resolved_path.is_relative_to(csf.ROOT) else str(resolved_path),
            "mean_brightness": f"{mean_brightness:.3f}",
        }

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        with_particles = [r for r in rows if int(r["particle_emitter_count"]) > 0]
        without_particles = [r for r in rows if int(r["particle_emitter_count"]) == 0]
        return [
            f"{len(rows)} / {total_files} files are additive-only with a resolved but genuinely-black "
            f"primary texture (mean RGB < {BLACK_MEAN_THRESHOLD}) -- likely blank renders.",
            f"  {len(with_particles)} of those also have a real particle emitter "
            "(already covered by particle_only_task.py's own candidate list).",
            f"  {len(without_particles)} have NO particle emitter -- the class particle_only_task.py's "
            "heuristic structurally cannot catch (deathwingcorruptedjaw's own shape).",
        ]
