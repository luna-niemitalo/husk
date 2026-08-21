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

Run with:
    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.black_additive_task:BlackAdditiveTask \\
        --root /media/luna/data/wow_export --output-stem black_additive_candidates
"""
from __future__ import annotations

import functools
import os
import re
import subprocess
import tempfile
from pathlib import Path

HUSK_BIN = Path("/home/luna/dev/husk/build/husk")
TIMEOUT = 15.0
BLP_TIMEOUT = 10.0
CORPUS_ROOT = Path("/media/luna/data/wow_export")
LISTFILE = Path("/media/luna/userdata/Downloads/community-listfile.csv")
BLP_CACHE_DIR = Path(tempfile.gettempdir()) / "husk_black_additive_blp_cache"

TEXTURE_LINE_RE = re.compile(r"^\s*texture (\d+): type=(\d+)(?: .*?file_data_id=(\d+))?\s*$")
LOOKUP_LINE_RE = re.compile(r"^\s*texture type \d+(?: \(\w+\))? -> texture (\d+)\s*$")
MATERIAL_RE = re.compile(r"^\s*material \d+: flags=0x[0-9a-fA-F]+ blend_mode=(\d+)\s*$")
PARTICLE_COUNT_RE = re.compile(r"^\s*particle_emitters: (\d+) ")

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
            stem = CORPUS_ROOT / Path(rel_path).with_suffix("")
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


def _run_info(path: Path) -> str | None:
    try:
        p = subprocess.run([str(HUSK_BIN), "info", str(path)], capture_output=True, text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return None
    if p.returncode != 0:
        return None
    return p.stdout


class BlackAdditiveTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = ["material_count", "blend_modes", "particle_emitter_count", "resolved_texture", "mean_brightness"]
    PARALLEL_MODE = "process"
    BATCH_SIZE = 1  # per-file cost is dominated by an occasional blp-export + PIL decode, not IPC dispatch

    @staticmethod
    def analyze(path: Path) -> dict | None:
        out = _run_info(path)
        if out is None:
            return None

        textures: dict[int, tuple[int, int | None]] = {}
        used_indices: set[int] = set()
        blend_modes: list[int] = []
        particle_count = 0
        for line in out.splitlines():
            m = TEXTURE_LINE_RE.match(line)
            if m:
                idx, ttype, fdid = int(m.group(1)), int(m.group(2)), m.group(3)
                textures[idx] = (ttype, int(fdid) if fdid else None)
                continue
            m = LOOKUP_LINE_RE.match(line)
            if m:
                used_indices.add(int(m.group(1)))
                continue
            m = MATERIAL_RE.match(line)
            if m:
                blend_modes.append(int(m.group(1)))
                continue
            m = PARTICLE_COUNT_RE.match(line)
            if m:
                particle_count = int(m.group(1))

        if not blend_modes or not used_indices:
            return None
        if any(b < ADDITIVE_FAMILY_THRESHOLD for b in blend_modes):
            return None  # has an ordinary opaque/alpha-tested surface -- not a candidate

        model_dir = path.parent
        basename_lower = path.stem.lower()
        first_idx = min(used_indices)
        _, fdid = textures.get(first_idx, (None, None))

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
            "resolved_texture": str(resolved_path.relative_to(CORPUS_ROOT)) if resolved_path.is_relative_to(CORPUS_ROOT) else str(resolved_path),
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
