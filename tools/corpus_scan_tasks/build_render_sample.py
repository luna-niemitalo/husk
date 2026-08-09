#!/usr/bin/env python3
"""Builds a stratified sample of .m2 files for the Blender visual-render
verification pass -- proportional-by-sqrt across top-level corpus
categories (item/world/spells/creature/models/environments/interface/
character/cameras/particles/test), so no single huge category
(item: 61,736 files) drowns out small ones (cameras: 33), plus a forced
include of every file that has a co-located .phys sidecar (rare, 96
total corpus-wide -- worth 100% coverage rather than a random chance of
missing the feature entirely).

Usage: direnv exec . tools/venv/bin/python tools/corpus_scan_tasks/build_render_sample.py
Writes corpus_reports/render_sample.txt (one absolute path per line).
"""
import math
import random
from pathlib import Path

ROOT = Path("/media/luna/data/wow_export")
OUT = Path("/home/luna/dev/husk/corpus_reports/render_sample.txt")
TARGET_TOTAL = 700
SEED = 20260809

random.seed(SEED)

CATEGORIES = ["item", "world", "spells", "creature", "models", "environments",
              "interface", "character", "cameras", "particles", "test", "spell", "interiors"]


def main() -> None:
    forced: set[Path] = {p.with_suffix("") for p in ROOT.rglob("*.phys")}
    forced_m2 = set()
    for base in forced:
        m2 = base.with_suffix(".m2")
        if m2.exists():
            forced_m2.add(m2)
    print(f"{len(forced_m2)} files forced-included (co-located .phys)")

    per_category: dict[str, list[Path]] = {}
    for cat in CATEGORIES:
        d = ROOT / cat
        if not d.is_dir():
            continue
        files = list(d.rglob("*.m2"))
        if files:
            per_category[cat] = files

    weights = {cat: math.sqrt(len(files)) for cat, files in per_category.items()}
    total_weight = sum(weights.values())

    sample: set[Path] = set(forced_m2)
    remaining_budget = max(0, TARGET_TOTAL - len(sample))
    for cat, files in per_category.items():
        quota = max(5, round(remaining_budget * weights[cat] / total_weight))
        quota = min(quota, len(files))
        sample.update(random.sample(files, quota))
        print(f"  {cat}: {len(files)} available, sampled {quota}")

    sample_list = sorted(sample)
    OUT.write_text("\n".join(str(p) for p in sample_list) + "\n")
    print(f"\n{len(sample_list)} total files written to {OUT}")


if __name__ == "__main__":
    main()
