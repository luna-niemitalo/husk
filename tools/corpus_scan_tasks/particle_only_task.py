"""Full-corpus scan for models whose real visible content is (or is very
likely) entirely M2Particle-driven -- the class TODO/RENDER_QUALITY_TODO.md
section 2 confirmed via `creature/cloud/cloudswampgas_white_clickable.m2`:
a model can have real, non-degenerate mesh geometry and a correctly-
resolved material, and still render as flat background color, because its
only real *visible* content in the live client is a particle-emitter
simulation husk structurally can't bake into geometry (glTF has no native
procedural-emitter representation) and no companion Blender script
reconstructs yet (a real, scoped, not-yet-built task -- deferred to
Blender-side work, same category as billboard alignment/texture-transform-
animation reconstruction, not a permanent wall the way character-texture
DB2 compositing is).

Heuristic, not a proof: flags a file when every one of its real materials
uses an additive-family blend mode (M2Material blend_mode >= 3 -- Add/
AddAlpha/Mod/Mod2x/etc, per wowdev.wiki M2#Materials) AND it has at least
one real M2Particle emitter. A model matching both is *plausibly*
particle-only (nothing in its own exported mesh reads as an ordinary
opaque/alpha-tested surface), but this is a static, per-file structural
signal -- it does not confirm the render is actually blank the way the
cloudswampgas case was confirmed by hand (real husk info + a pixel-flatness
check against the actual rendered output). Treat this scan's output as a
*candidate* list for human spot-check (or a pixel-flatness cross-check
against already-rendered corpus_reports/renders_full output) before
folding any of it into an exclude list the way full_corpus_file_list.
no_objectcomponents.txt already excludes item/objectcomponents/ wholesale
-- a false positive here would silently hide a file from review, not just
mis-sort it.

Uses `husk info` per file (matches this project's own existing
missing_texture_task.py/expansion_task.py convention: read exactly what
husk's own resolution logic sees, not a second, possibly-diverging
struct-unpack of the format).

Run with:
    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.particle_only_task:ParticleOnlyTask \\
        --root /media/luna/data/wow_export --output-stem particle_only_candidates
"""
from __future__ import annotations

import re
import subprocess
from pathlib import Path

HUSK_BIN = Path("/home/luna/dev/husk/build/husk")
TIMEOUT = 15.0

VERTICES_RE = re.compile(r"^\s*vertices: (\d+) ")
MATERIAL_RE = re.compile(r"^\s*material \d+: flags=0x[0-9a-fA-F]+ blend_mode=(\d+)\s*$")
PARTICLE_COUNT_RE = re.compile(r"^\s*particle_emitters: (\d+) ")

# M2Material blend_mode >= 3 is the additive family (Add/AddAlpha/Mod/
# Mod2x/...) -- 0 (Opaque) and 1 (AlphaKey) are real, ordinary surfaces;
# 2 (Alpha) is a real translucent surface too (glass, cloth), not additive
# -- see wowdev.wiki M2#Materials and this project's own
# alphaModeForBlend (src/export_materials.cpp).
ADDITIVE_FAMILY_THRESHOLD = 3


def _husk_info_lines(path: Path) -> list[str]:
    try:
        p = subprocess.run([str(HUSK_BIN), "info", str(path)], capture_output=True, text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return []
    if p.returncode != 0:
        return []
    return p.stdout.splitlines()


class ParticleOnlyTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = ["vertex_count", "material_count", "blend_modes", "particle_emitter_count"]
    PARALLEL_MODE = "process"  # shells out to husk per file, same cost profile as missing_texture_task
    BATCH_SIZE = 8

    @staticmethod
    def analyze(path: Path) -> dict | None:
        lines = _husk_info_lines(path)
        if not lines:
            return None

        vertex_count = 0
        blend_modes: list[int] = []
        particle_count = 0
        for line in lines:
            m = VERTICES_RE.match(line)
            if m:
                vertex_count = int(m.group(1))
                continue
            m = MATERIAL_RE.match(line)
            if m:
                blend_modes.append(int(m.group(1)))
                continue
            m = PARTICLE_COUNT_RE.match(line)
            if m:
                particle_count = int(m.group(1))

        # Not our target case: no real geometry (already handled separately
        # by husk's zero-vertex fallback path, a different TODO item), no
        # real materials to judge, or no particles to blame the blankness
        # on in the first place.
        if vertex_count == 0 or not blend_modes or particle_count == 0:
            return None
        if any(b < ADDITIVE_FAMILY_THRESHOLD for b in blend_modes):
            return None  # has at least one ordinary opaque/alpha-tested surface -- not a candidate

        return {
            "vertex_count": vertex_count,
            "material_count": len(blend_modes),
            "blend_modes": ";".join(str(b) for b in blend_modes),
            "particle_emitter_count": particle_count,
        }

    @staticmethod
    def summarize(rows, total_files) -> list[str]:
        return [
            f"{len(rows)} / {total_files} files are particle-only candidates "
            "(every material additive-family, at least one real particle emitter) -- "
            "spot-check before excluding, see this task's own module doc comment",
        ]
