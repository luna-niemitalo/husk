"""corpus_scan_framework.ScanTask wrapper around find_texture_type_collisions.py's
own analyze()/summarize() logic -- the real check currently running
single-threaded (as of this session) against the full corpus.

Deliberately imports the existing script rather than re-deriving its
MD20/.skin parsing: find_texture_type_collisions.py is untouched and kept
runnable standalone exactly as before (this is purely a second front end
for its own logic, not a fork of it). Only the packaging differs: its
`analyze()` return dict includes its own "m2_path" key (needed for its
own plain-loop CSV writer); this task strips it, since
corpus_scan_framework.py already adds an equivalent "path" column itself.

Used to benchmark this framework's parallel driver against that script's
existing single-threaded run before deciding whether to interrupt it --
see tools/benchmark_texture_type_collisions.py.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import find_texture_type_collisions as ftc  # noqa: E402


class TextureTypeCollisionsTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = [
        "texture_count", "colliding_types", "colliding_texture_count",
        "has_texture_lookup", "skin_found", "skin_readable", "batches_total",
        "batches_on_colliding_type", "batches_matching_texture_lookup",
        "batches_diverging_from_texture_lookup",
    ]
    PARALLEL_MODE = "process"  # real per-file CPU parsing work (M2 + .skin), no subprocess calls

    @staticmethod
    def analyze(path: Path) -> dict | None:
        row = ftc.analyze(path)
        if row is None:
            return None
        row = dict(row)
        row.pop("m2_path")
        return row

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        affected_meshes = len(rows)
        affected_colliding_textures = sum(r["colliding_texture_count"] for r in rows)
        meshes_with_skin = sum(1 for r in rows if r["skin_readable"])
        batches_total = sum(r["batches_total"] for r in rows)
        batches_on_colliding = sum(r["batches_on_colliding_type"] for r in rows)
        batches_matching = sum(r["batches_matching_texture_lookup"] for r in rows)
        batches_diverging = sum(r["batches_diverging_from_texture_lookup"] for r in rows)
        meshes_with_diverging_batch = sum(1 for r in rows if r["batches_diverging_from_texture_lookup"] > 0)
        return [
            f"{affected_meshes} / {total_files} .m2 files have >=1 M2Texture type collision "
            f"({affected_meshes / total_files:.3%} of the corpus)" if total_files else "0 files scanned",
            f"{affected_colliding_textures} individual texture records are involved in some collision",
            f"{meshes_with_skin} / {affected_meshes} of those files have a resolvable same-basename "
            ".skin sibling" if affected_meshes else "0 affected files",
            f"{batches_total} total batches across those {meshes_with_skin} .skin files",
            f"{batches_on_colliding} of those batches resolve (via textureCombos) to a texture whose "
            "type collides with another texture in the same file",
            f"  -> {batches_matching} agree with what textureLookup would pick for that type",
            f"  -> {batches_diverging} DISAGREE with textureLookup's pick",
            f"{meshes_with_diverging_batch} distinct .m2 file(s) have at least one such diverging batch",
        ]
