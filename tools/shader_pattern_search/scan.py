#!/usr/bin/env python3
"""CLI: scan a corpus of vkd3d-compiler d3d-asm files for known shader-math
shapes (see patterns.py), without reading every file by hand.

Usage:
    direnv exec . python3 tools/shader_pattern_search/scan.py \
        --asm-dir references/wow_shaders/asm \
        --output references/wow_shaders/pattern_scan_report.json

    # sanity-check the tool itself against files with a known answer:
    direnv exec . python3 tools/shader_pattern_search/scan.py --self-test

    # verbose look at one file's slices + matches:
    direnv exec . python3 tools/shader_pattern_search/scan.py --file <hash>.asm -v
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ir import parse_shader  # noqa: E402
import patterns as patterns_mod  # noqa: E402
from patterns import PATTERN_REGISTRY  # noqa: E402

OUTPUT_SINKS = ("o0", "oMask")

# hash -> known/expected pattern, per SHADER_SCAN_FINDINGS.md's manual reads.
# --self-test checks the tool still finds these before trusting a corpus run.
SELF_TEST_CASES = {
    "93dd1d60a5da7d7f": "combiners_mod_family",   # Combiners_Mod
    "dea0d96a0e1d054e": "combiners_mod_family",   # Combiners_Mod_Mod
    "47c35a45740c769d": "three_layer_tint_composite",  # guild-vendor capture
}


def candidate_regions(ir):
    """All slice_idxs regions patterns get tried against: full backward
    slices from each output sink (good for small combiner shapes that ARE
    the whole dependency chain) plus local windows around sample clusters
    (good for motifs embedded in a larger shader, where the full slice
    would pull in unrelated downstream math). See patterns.local_sample_windows
    for why the local-window generator exists alongside the graph slices."""
    for sink in OUTPUT_SINKS:
        yield from ir.slice_for_output(sink)
    yield from patterns_mod.local_sample_windows(ir)


def scan_file(path: Path) -> list[patterns_mod.Match]:
    ir = parse_shader(path)
    matches: list[patterns_mod.Match] = []
    seen: set[tuple[str, tuple[int, ...]]] = set()
    for slice_idxs in candidate_regions(ir):
        for pattern_fn in PATTERN_REGISTRY:
            for m in pattern_fn(ir, slice_idxs):
                key = (m.pattern, tuple(m.instructions))
                if key not in seen:
                    seen.add(key)
                    matches.append(m)
    return matches


def run_self_test(asm_dir: Path) -> bool:
    ok = True
    for h, expected in SELF_TEST_CASES.items():
        path = asm_dir / f"{h}.asm"
        if not path.exists():
            print(f"SKIP {h}: not found under {asm_dir}")
            ok = False
            continue
        matches = scan_file(path)
        found = {m.pattern for m in matches}
        status = "PASS" if expected in found else "FAIL"
        if status == "FAIL":
            ok = False
        print(f"{status} {h}: expected {expected!r}, found {sorted(found) or '(nothing)'}")
    return ok


def run_verbose(path: Path):
    ir = parse_shader(path)
    print(f"{path.name}: {len(ir.instructions)} instructions")
    for n, slice_idxs in enumerate(candidate_regions(ir)):
        found = []
        for pattern_fn in PATTERN_REGISTRY:
            found.extend(pattern_fn(ir, slice_idxs))
        if not found:
            continue
        print(f"\n-- region #{n}, {len(slice_idxs)} instructions --")
        for i in slice_idxs:
            print(f"  [{i}] {ir.by_idx(i).raw}")
        print("  matches:")
        for m in found:
            print(f"    {m.pattern} ({m.confidence}): {m.detail}")


def run_corpus_scan(asm_dir: Path, output: Path):
    report: dict[str, list[dict]] = {}
    counts: dict[str, int] = {}
    files = sorted(asm_dir.glob("*.asm"))
    for i, path in enumerate(files):
        try:
            matches = scan_file(path)
        except Exception as exc:  # noqa: BLE001 -- best-effort corpus sweep, one bad file shouldn't abort the rest
            print(f"WARN {path.name}: parse/scan failed ({exc})", file=sys.stderr)
            continue
        if matches:
            report[path.stem] = [
                {"pattern": m.pattern, "confidence": m.confidence, "detail": m.detail,
                 "instructions": m.instructions}
                for m in matches
            ]
            for m in matches:
                counts[m.pattern] = counts.get(m.pattern, 0) + 1
        if (i + 1) % 200 == 0:
            print(f"...{i + 1}/{len(files)} scanned", file=sys.stderr)

    output.write_text(json.dumps(report, indent=2, sort_keys=True))
    print(f"Scanned {len(files)} files, {len(report)} had a match. Report: {output}")
    for pattern, n in sorted(counts.items()):
        print(f"  {pattern}: {n}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--asm-dir", type=Path, default=Path("references/wow_shaders/asm"))
    ap.add_argument("--output", type=Path, default=Path("references/wow_shaders/pattern_scan_report.json"))
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--file", type=str, help="single .asm filename (relative to --asm-dir) for a verbose look")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        ok = run_self_test(args.asm_dir)
        sys.exit(0 if ok else 1)

    if args.file:
        run_verbose(args.asm_dir / args.file)
        return

    run_corpus_scan(args.asm_dir, args.output)


if __name__ == "__main__":
    main()
