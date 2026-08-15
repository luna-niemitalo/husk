#!/usr/bin/env python3
"""Runs the full husk export -> Blender render pipeline over the entire
corpus (fresh directory discovery, not a stale pre-generated file list),
honoring an optional .renderignore file at the repo root: gitignore-style
glob patterns, one per line, for directories or files to skip. Default
output: corpus_reports/renders_full, this project's established render
location -- resume-safe, same as tools/corpus_scan_tasks/render_sample_
driver.py (an already-rendered file is left alone, not redone).

Usage:
    direnv exec . tools/venv/bin/python tools/full_render.py
    direnv exec . tools/venv/bin/python tools/full_render.py --dry-run

.renderignore format (a real subset of gitignore, not the full spec --
no negation, no explicit '**'; a `*` never crosses a `/`, same as real
gitignore):
    # lines starting with # are comments, blank lines are skipped
    character/                 # directory prefix -- matches at any depth
    /item/objectcomponents/    # leading '/' anchors to the corpus root
    *_test.m2                  # unanchored glob -- matched against the
                                # filename only, at any depth
    /foo/*_test.m2              # anchored glob -- matched segment-by-
                                 # segment starting at the corpus root
"""
from __future__ import annotations

import argparse
import fnmatch
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "corpus_scan_tasks"))
from render_sample_driver import CORPUS_ROOT, run_render_pipeline  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_IGNORE_FILE = REPO_ROOT / ".renderignore"
DEFAULT_OUTPUT_DIR = REPO_ROOT / "corpus_reports" / "renders_full"


def load_ignore_patterns(ignore_path: Path) -> list[str]:
    if not ignore_path.exists():
        return []
    patterns = []
    for raw_line in ignore_path.read_text().splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if line:
            patterns.append(line)
    return patterns


def is_ignored(rel_posix: str, patterns: list[str]) -> bool:
    """rel_posix: the .m2 path relative to the corpus root, forward
    slashes, no leading slash (e.g. "item/objectcomponents/foo.m2").
    Glob segments are matched one path component at a time (never a bare
    fnmatch.fnmatch on the joined path) specifically so a `*` in a
    pattern can't accidentally cross a `/` the way plain fnmatch would --
    real gitignore's `*` doesn't cross directory boundaries either, and a
    pattern matcher named after gitignore should not surprise someone who
    knows gitignore syntax.
    """
    path_segments = rel_posix.split("/")
    name = path_segments[-1]
    for pattern in patterns:
        anchored = pattern.startswith("/")
        p = pattern[1:] if anchored else pattern
        if p.endswith("/"):
            dir_pattern = p.rstrip("/")
            if anchored:
                if rel_posix == dir_pattern or rel_posix.startswith(dir_pattern + "/"):
                    return True
            elif rel_posix.startswith(dir_pattern + "/") or ("/" + rel_posix).find("/" + dir_pattern + "/") != -1:
                return True
        elif anchored:
            pattern_segments = p.split("/")
            if len(path_segments) == len(pattern_segments) and all(
                fnmatch.fnmatch(ps, pp) for ps, pp in zip(path_segments, pattern_segments)
            ):
                return True
        elif fnmatch.fnmatch(name, p):
            return True
    return False


def discover_files(root: Path, patterns: list[str]) -> list[str]:
    print(f"discovering .m2 files under {root} ...", flush=True)
    all_paths = sorted(str(p) for p in root.rglob("*.m2"))
    print(f"  {len(all_paths)} total", flush=True)
    if not patterns:
        return all_paths
    kept = []
    skipped = 0
    for p in all_paths:
        rel_posix = Path(p).resolve().relative_to(root).as_posix()
        if is_ignored(rel_posix, patterns):
            skipped += 1
        else:
            kept.append(p)
    print(f"  {skipped} excluded by {len(patterns)} .renderignore pattern(s), {len(kept)} remaining", flush=True)
    return kept


def main() -> int:
    sys.stdout.reconfigure(line_buffering=True)

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ignore-file", type=Path, default=DEFAULT_IGNORE_FILE,
                     help=f"default: {DEFAULT_IGNORE_FILE}")
    ap.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR,
                     help=f"default: {DEFAULT_OUTPUT_DIR}")
    ap.add_argument("--corpus-root", type=Path, default=CORPUS_ROOT, help=f"default: {CORPUS_ROOT}")
    ap.add_argument("--dry-run", action="store_true", help="print what would be rendered, don't render")
    args = ap.parse_args()

    patterns = load_ignore_patterns(args.ignore_file)
    if patterns:
        print(f"ignore file: {args.ignore_file} ({len(patterns)} pattern(s))")
    else:
        print(f"ignore file: {args.ignore_file} (not found or empty -- rendering everything)")

    paths = discover_files(args.corpus_root, patterns)
    if args.dry_run:
        print(f"\ndry run: would render {len(paths)} files to {args.output_dir}")
        return 0

    return run_render_pipeline(paths, args.output_dir)


if __name__ == "__main__":
    sys.exit(main())
