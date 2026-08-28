"""Minimal real ScanTask, meant as a copy-paste starting point.

This is deliberately trivial (counts M2Texture records per file, flags
files with zero) -- the point isn't the check itself, it's showing exactly
how little a task-assignment module needs: GLOB_PATTERNS, FIELDNAMES, and
one pure analyze(path) function. Everything else (discovery, worker pool
sizing/parallelism, CSV/log output, per-file error isolation, throughput
reporting) is corpus_scan_framework.py's job, not this file's.

Consumes husk's own understanding of the file (`husk info --json`) rather
than a second, hand-rolled struct-unpack of the M2 header -- this is the
"structured output" tier of REFACTOR/RESOURCE_CATALOG.md's excavation
escape hatch (consuming husk's understanding, not interrogating raw bytes
behind it), and this file is the copy-paste template new tasks start from,
so it has to model that pattern rather than the raw-read one.

Run it against the real corpus with:

    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.example_texture_count:TextureCountTask \\
        --root /media/luna/data/wow_export --output-stem texture_count_demo \\
        --limit 2000   # drop --limit for a real full-corpus run
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import corpus_scan_framework as csf  # noqa: E402 -- see sys.path.insert above; husk_info_json read from there, see REFACTOR/CLI_AND_TOOLING.md §3


class TextureCountTask:
    GLOB_PATTERNS = ["*.m2"]
    FIELDNAMES = ["texture_count"]
    PARALLEL_MODE = "process"  # shells out to husk per file, real subprocess cost
    BATCH_SIZE = 1  # dominated by the husk subprocess spawn, not IPC dispatch -- see CORPUS_SCANS.md's BATCH_SIZE gotcha

    @staticmethod
    def analyze(path: Path) -> dict | None:
        info = csf.husk_info_json(path)
        if info is None:
            return None
        count = info["textures"]["count"]
        if count > 0:
            return None  # only report the interesting (zero-texture) case
        return {"texture_count": 0}

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        return [f"{len(rows)} / {total_files} .m2 files have zero M2Texture records "
                f"({len(rows) / total_files:.3%} of the corpus)"]
