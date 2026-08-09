"""Full-corpus scan for M2 texture slots that name a real FileDataID but
have no matching local .blp/.png file -- the gap this session's render job
kept silently degrading through rather than failing on (husk falls back to
an ambiguous same-basename pool or a blank texture, it never errors), so
it never showed up in any FAIL-EXPORT line. Written to hand a concrete,
reproducible file list to casc-tool for investigation: is the FileDataID
genuinely absent from the extracted corpus, or extracted under a different
name/location husk's own resolution convention doesn't check?

Uses `husk info` (header-only, no export) per file to get real per-texture
FileDataIDs -- deliberately not a from-scratch struct-unpack reimplementation
of the TXID chunk, so this matches exactly what `husk export`'s own
resolution logic sees, not a second, possibly-diverging read of the format.

Run with:
    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.missing_texture_task:MissingTextureTask \\
        --root /media/luna/data/wow_export --output-stem missing_textures
"""
from __future__ import annotations

import re
import subprocess
from pathlib import Path

HUSK_BIN = Path("/home/luna/dev/husk/build/husk")
TIMEOUT = 15.0

# "    texture 10: type=0 flags=0x2 filename=(empty) file_data_id=3536810"
TEXTURE_LINE_RE = re.compile(r"^\s*texture \d+: type=0 .*file_data_id=(\d+)\s*$")


def _referenced_file_data_ids(path: Path) -> list[int]:
    try:
        p = subprocess.run([str(HUSK_BIN), "info", str(path)], capture_output=True, text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return []
    if p.returncode != 0:
        return []
    ids = []
    for line in p.stdout.splitlines():
        m = TEXTURE_LINE_RE.match(line)
        if m:
            fdid = int(m.group(1))
            if fdid != 0:
                ids.append(fdid)
    return ids


class MissingTextureTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = ["missing_file_data_ids", "referenced_count"]
    PARALLEL_MODE = "process"  # shells out to husk per file, same cost profile as m2_full_validation_task
    BATCH_SIZE = 8

    @staticmethod
    def analyze(path: Path) -> dict | None:
        fdids = _referenced_file_data_ids(path)
        if not fdids:
            return None
        model_dir = path.parent
        missing = [fdid for fdid in fdids
                   if not (model_dir / f"{fdid}.blp").exists() and not (model_dir / f"{fdid}.png").exists()]
        if not missing:
            return None
        return {"missing_file_data_ids": " ".join(str(f) for f in missing), "referenced_count": len(fdids)}

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        all_missing_ids: set[int] = set()
        for r in rows:
            all_missing_ids.update(int(x) for x in r["missing_file_data_ids"].split())
        lines = [
            f"{len(rows)} / {total_files} .m2 files reference at least one FileDataID-named "
            f"texture with no local .blp/.png next to the model ({len(rows) / total_files:.3%} of the corpus)",
            f"{len(all_missing_ids)} distinct missing FileDataIDs across those files",
        ]
        return lines
