#!/usr/bin/env python3
"""One-off benchmark: find_texture_type_collisions.py's own single-threaded
loop vs corpus_scan_framework.py's parallel driver (a few batch sizes),
all on the exact same file sample, so Luna can judge whether the speedup
is worth interrupting the real single-threaded scan already running
against the full corpus (see LUNA_NOTES.md-style session history --
that scan was started before this framework existed).

Usage:
    direnv exec . tools/venv/bin/python tools/benchmark_texture_type_collisions.py [N]

N defaults to 3000. Writes nothing to the repo -- all framework output
goes to a throwaway temp directory, deleted at exit.
"""

import shutil
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import find_texture_type_collisions as ftc  # noqa: E402
from corpus_scan_framework import discover, run_corpus_scan  # noqa: E402
from corpus_scan_tasks.texture_type_collisions_task import TextureTypeCollisionsTask  # noqa: E402

ROOT = Path("/media/luna/data/wow_export")


def bench_sequential(files: list[Path]) -> float:
    """Same shape as find_texture_type_collisions.py's own main() loop --
    one process, one file at a time, no framework involved.
    """
    start = time.monotonic()
    for path in files:
        ftc.analyze(path)
    return time.monotonic() - start


def bench_framework(files: list[Path], tmp_dir: Path, batch_size: int, parallel_mode: str) -> float:
    task = TextureTypeCollisionsTask
    original_mode = task.PARALLEL_MODE
    task.PARALLEL_MODE = parallel_mode
    try:
        start = time.monotonic()
        run_corpus_scan(
            task, root=ROOT, output_stem=f"bench_{parallel_mode}_{batch_size}",
            output_dir=tmp_dir, limit=len(files), batch_size=batch_size,
        )
        return time.monotonic() - start
    finally:
        task.PARALLEL_MODE = original_mode


def main() -> int:
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    print(f"discovering files under {ROOT} ...")
    # limit=n here uses discover()'s own short-circuiting fast path, so this
    # matches exactly what run_corpus_scan(..., limit=n) will discover internally
    # -- same file set for every configuration below, not just same count.
    files = discover(ROOT, ["*.m2"], limit=n)
    print(f"benchmarking on {len(files)} files\n")

    tmp_dir = Path(tempfile.mkdtemp(prefix="husk_corpus_bench_"))
    try:
        results = {}

        print("--- sequential (matches the currently-running script's own approach) ---")
        t = bench_sequential(files)
        results["sequential"] = t
        print(f"  {t:.2f}s ({len(files) / t:.0f} files/sec)\n")

        for batch_size in (1, 16, 64):
            print(f"--- framework, process pool, batch={batch_size} ---")
            t = bench_framework(files, tmp_dir, batch_size, "process")
            results[f"process/batch={batch_size}"] = t
            print(f"  {t:.2f}s ({len(files) / t:.0f} files/sec)\n")

        print("--- framework, thread pool, batch=1 ---")
        t = bench_framework(files, tmp_dir, 1, "thread")
        results["thread/batch=1"] = t
        print(f"  {t:.2f}s ({len(files) / t:.0f} files/sec)\n")

        baseline = results["sequential"]
        print("=== summary ===")
        for label, t in results.items():
            print(f"  {label:24s} {t:6.2f}s  {len(files) / t:7.0f} files/sec  {baseline / t:5.2f}x vs sequential")
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
