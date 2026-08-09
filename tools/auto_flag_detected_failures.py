#!/usr/bin/env python3
"""Runs categorize_flagged_renders.py's cheap-signal failure detectors over
the *entire* render corpus (not just what's already been manually reviewed
in tag_review_server.py) and auto-appends a "flagged" decision for every
real hit straight into the same JSONL review log.

Rationale: most flagged categories (wrong alpha blend, detached geometry,
collapsed bind pose, ...) have no cheap pixel-level signal and still need a
human eye. Two categories do -- missing_texture_or_shader and
black_silhouette_or_unlit (see categorize_flagged_renders.py's module doc
for both threshold calibrations) -- reliable enough that a human
numpad-clicking through them is pure waste. This script exists so those two
never enter the manual review queue in the first place.

Never touches an item that already has a decision (human or otherwise) --
last-write-wins on the log's own read, but this script deliberately doesn't
exercise that: overwriting a human's own judgment based on a heuristic
would be a real regression, not a speedup, so already-decided rels are
skipped outright regardless of what this scan would have concluded about
them. A rel this scan flags does NOT mean "definitely broken" beyond what
the matched heuristic can tell -- entries get "source": "auto:<category>"
in the log so a later audit can tell an automated call apart from a human
one, and which detector made it.

The tag_review_server.py process holding this log open (if any) only reads
it once at startup -- restart it after this script finishes so its
resumable /api/next skip logic picks up the new entries.

Usage:
    tools/venv/bin/python tools/auto_flag_detected_failures.py \\
        --root corpus_reports/renders_full \\
        --review corpus_reports/renders_full_review.jsonl
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from categorize_flagged_renders import BLACK_SILHOUETTE, MISSING_TEXTURE, categorize_image  # noqa: E402

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".webp", ".gif"}

# Categories with a cheap, calibrated pixel-level signal -- see
# categorize_flagged_renders.py's module doc for each one's threshold
# derivation. Anything categorize_image() returns that isn't in here
# (currently just "other" and "empty_or_tiny_foreground") is left alone:
# no auto-decision, still needs a human.
AUTO_DETECTABLE_CATEGORIES = {MISSING_TEXTURE, BLACK_SILHOUETTE}


def load_reviewed_rels(review_path: Path) -> set[str]:
    reviewed: set[str] = set()
    if not review_path.exists():
        return reviewed
    with review_path.open() as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            reviewed.add(json.loads(line)["rel"])
    return reviewed


def walk_all_rels(root: Path) -> list[str]:
    rels = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            if os.path.splitext(name)[1].lower() in IMAGE_EXTS:
                rel = os.path.relpath(os.path.join(dirpath, name), root)
                rels.append(rel.replace(os.sep, "/"))
    return rels


def _classify_one(args: tuple[Path, str]) -> tuple[str, str | None]:
    root, rel = args
    try:
        category = categorize_image(root / rel)
    except Exception as exc:  # corrupt/truncated render -- report, don't crash the batch
        return rel, f"error:{exc}"
    return rel, (category if category in AUTO_DETECTABLE_CATEGORIES else None)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, required=True, help="rendered image corpus root")
    ap.add_argument("--review", type=Path, required=True, help="tag_review_server.py JSONL decision log to append to")
    ap.add_argument("--workers", type=int, default=0, help="0 = os.cpu_count()")
    ap.add_argument("--dry-run", action="store_true", help="scan and report, but don't write any decisions")
    args = ap.parse_args()

    reviewed = load_reviewed_rels(args.review)
    all_rels = walk_all_rels(args.root)
    todo = [rel for rel in all_rels if rel not in reviewed]
    print(f"{len(all_rels)} total renders, {len(reviewed)} already reviewed, {len(todo)} to scan")

    auto_flagged: Counter[str] = Counter()
    errors = 0
    start = time.time()
    work = [(args.root, rel) for rel in todo]
    out_f = None if args.dry_run else args.review.open("a")
    try:
        with ProcessPoolExecutor(max_workers=args.workers or None) as pool:
            for i, (rel, hit) in enumerate(pool.map(_classify_one, work, chunksize=64), 1):
                if hit and hit.startswith("error:"):
                    errors += 1
                elif hit in AUTO_DETECTABLE_CATEGORIES:
                    auto_flagged[hit] += 1
                    if out_f:
                        out_f.write(json.dumps({
                            "rel": rel, "flagged": True, "ts": time.time(),
                            "source": f"auto:{hit}",
                        }) + "\n")
                if i % 5000 == 0:
                    rate = i / (time.time() - start)
                    total_flagged = sum(auto_flagged.values())
                    print(f"  {i}/{len(todo)} scanned, {total_flagged} auto-flagged so far ({rate:.0f}/s)")
    finally:
        if out_f:
            out_f.close()

    elapsed = time.time() - start
    total_flagged = sum(auto_flagged.values())
    breakdown = ", ".join(f"{n} {cat}" for cat, n in auto_flagged.most_common())
    print(f"\ndone in {elapsed:.0f}s: {total_flagged} auto-flagged ({breakdown}), "
          f"{errors} decode errors, {len(todo) - total_flagged - errors} left for manual review")
    if not args.dry_run and total_flagged:
        print(f"wrote {total_flagged} decisions to {args.review} -- restart tag_review_server.py to pick them up")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
