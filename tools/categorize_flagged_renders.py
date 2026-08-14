#!/usr/bin/env python3
"""Sorts live_gallery/server.py's /review page's flagged renders into a
small set of failure
categories, so a 30k+-item flagged pile becomes something a human can
triage by bucket instead of one image at a time.

Two categories have a cheap, reliable pixel-level signal, both detected by
categorize_image():
  - missing_texture_or_shader: object lost its texture/shader and fell back
    to Blender's flat default material -- a large near-white area over the
    object's own silhouette.
  - black_silhouette_or_unlit: object renders as a flat, almost perfectly
    uniform near-black shape -- no shading gradient, no specular highlight,
    nothing -- distinct from a legitimately dark-colored, properly-lit
    object (a black weapon/armor piece still has real luma variance from
    its own highlights; see threshold calibration note below).

A real, visually distinct, still-undetected bucket exists too (glow/fire/
magic effect planes rendering as flat opaque quads instead of blended
translucent effects, plus a few other patterns -- partial/per-geoset
missing texture, detached sub-geometry, collapsed bind poses). Those stay
in "other" for now: no cheap pixel signal identified yet, an honest gap,
not a guess dressed up as a category. See CLAUDE_HISTORY.md for the full
investigation writeup of what's in there.

Pillow for decode, numpy for the per-pixel math -- fully vectorized, no
per-pixel Python loop.

Threshold calibration note: a "missing texture" object's fallback-material
luma tops out around 233 in real samples (Blender's normal-based shading
still darkens it), not near 255 -- a naive ">242 is white" check missed
every real case. Recalibrated against 6 known-missing-texture and 6
known-OK real flagged samples this session: near_white_luma=190 gives a
real, comfortable margin (missing-texture foreground fraction 0.63-0.92 in
the positive samples, 0.0-0.07 in the negative ones).

Black-silhouette calibration: the known real bug case (mace_1h_raidmidnight_
d_01) has foreground-luma p95=9.0, max=33.0 -- essentially flat black. 18
real OK dark/black-themed items (legit black weapons, void/shadow/fel gear)
all landed at p95>=61.8, max>=128.4 -- real shading variance a broken/unlit
material doesn't have. black_silhouette_p95=40 sits with a 1.5x margin
below the nearest real legit item and a 4.4x margin above the known bug.

Usage:
    tools/venv/bin/python tools/categorize_flagged_renders.py \\
        --review corpus_reports/renders_full_review.jsonl \\
        --root corpus_reports/renders_full \\
        --out corpus_reports/flagged_categories.csv
"""
from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image

MISSING_TEXTURE = "missing_texture_or_shader"
BLACK_SILHOUETTE = "black_silhouette_or_unlit"
EMPTY_FOREGROUND = "empty_or_tiny_foreground"
OTHER = "other"


def load_flagged_rels(review_path: Path) -> list[str]:
    """Last write per rel wins, same replay rule as ReviewLog in
    live_gallery/server.py -- this file is that server's /review page's own
    decision log (formerly the standalone tools/tag_review_server.py's log,
    same JSONL format, unchanged since the 2026-08-14 merge).
    """
    decisions: dict[str, bool] = {}
    with review_path.open() as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            decisions[rec["rel"]] = bool(rec["flagged"])
    return [rel for rel, flagged in decisions.items() if flagged]


def _background_color(arr: np.ndarray, patch: int = 8) -> np.ndarray:
    """Average color sampled from all four corners -- adapts per-image
    rather than hardcoding render_glb.py's world.color (0.12, 0.12, 0.14)
    linear, since the display color-management transform between that and
    the on-disk pixel value isn't this script's concern to reproduce.
    """
    h, w, _ = arr.shape
    patch = min(patch, w, h)
    corners = np.concatenate([
        arr[0:patch, 0:patch].reshape(-1, 3),
        arr[0:patch, w - patch:w].reshape(-1, 3),
        arr[h - patch:h, 0:patch].reshape(-1, 3),
        arr[h - patch:h, w - patch:w].reshape(-1, 3),
    ])
    return corners.mean(axis=0)


def categorize_image(
    path: Path,
    *,
    bg_diff_threshold: int = 24,
    near_white_luma: int = 190,
    min_foreground_pixels: int = 200,
    missing_texture_fraction: float = 0.35,
    black_silhouette_p95: float = 40.0,
) -> str:
    arr = np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)
    bg = _background_color(arr)
    foreground = np.abs(arr - bg).max(axis=2) > bg_diff_threshold
    fg_count = int(foreground.sum())
    if fg_count < min_foreground_pixels:
        return EMPTY_FOREGROUND

    luma = 0.299 * arr[..., 0] + 0.587 * arr[..., 1] + 0.114 * arr[..., 2]
    fg_luma = luma[foreground]

    near_white_fg_count = int((fg_luma > near_white_luma).sum())
    if near_white_fg_count / fg_count > missing_texture_fraction:
        return MISSING_TEXTURE

    if float(np.percentile(fg_luma, 95)) < black_silhouette_p95:
        return BLACK_SILHOUETTE

    return OTHER


def _categorize_one(args: tuple[Path, str]) -> tuple[str, str]:
    root, rel = args
    try:
        return rel, categorize_image(root / rel)
    except Exception as exc:  # corrupt/truncated render -- report, don't crash the batch
        return rel, f"error:{exc}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--review", type=Path, required=True, help="live_gallery/server.py /review page's JSONL decision log")
    ap.add_argument("--root", type=Path, required=True, help="rendered image root the review log's rels are relative to")
    ap.add_argument("--out", type=Path, required=True, help="CSV output path: rel,category")
    ap.add_argument("--workers", type=int, default=0, help="0 = os.cpu_count()")
    args = ap.parse_args()

    flagged = load_flagged_rels(args.review)
    print(f"{len(flagged)} flagged items to categorize")

    counts: Counter[str] = Counter()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="") as csv_f, \
         ProcessPoolExecutor(max_workers=args.workers or None) as pool:
        writer = csv.writer(csv_f)
        writer.writerow(["rel", "category"])
        work = [(args.root, rel) for rel in flagged]
        done = 0
        for rel, category in pool.map(_categorize_one, work, chunksize=64):
            writer.writerow([rel, category])
            counts[category] += 1
            done += 1
            if done % 5000 == 0:
                print(f"  {done}/{len(flagged)}...")

    print(f"\nwrote {args.out}\n")
    total = sum(counts.values())
    for category, n in counts.most_common():
        print(f"  {category:28s} {n:7d}  ({n / total * 100:5.1f}%)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
