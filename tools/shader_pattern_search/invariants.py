"""Layer 2, part B: invariant-based matching over decomposed blocks.

Instead of testing "does this instruction sequence look like the one lerp
encoding we hardcoded" (patterns.py's job, and the source of the false-
negative risk that motivated this module), test necessary-but-not-
sufficient numeric properties of the *function* the block computes,
regardless of how it's encoded. A block failing an invariant is provably
not a match; a block passing every invariant is a candidate, not a
certainty -- false positives get sorted out by a human read, same as
patterns.py's output always needed.

Invariant battery, specific to the "N-layer tinted alpha composite" family
(background/border/emblem-shaped -- see SHADER_SCAN_FINDINGS.md's guild-
vendor finding, the thing this whole tool exists to re-find in other
shaders):

1. Affine-in-alpha: for a candidate "layer" texture Tk, does the block's
   output vary affinely in Tk's alpha channel (Tk.w)? f(0.5) should sit
   exactly halfway between f(0) and f(1) if the block does a true linear
   blend on that channel, whatever instructions it's built from. Checked
   away from the exact 0/0.5/1 boundary values too (0.3/0.5/0.7), since a
   compiler is more likely to special-case an exact boundary than a
   generic interior point (a real risk called out when this design was
   proposed).
2. Annihilation: with Tk.w pinned to 1, is the output insensitive to some
   other texture Tj's rgb? That's the signature of "this layer's alpha=1
   fully replaces whatever came before it" -- much sharper than affineness
   alone, and the property the known false positive (217a11d586e92c7d,
   terrain height-blending) is expected to fail: its "lerp-shaped" ops are
   gated by a height factor feeding into lighting math, not a clean
   per-layer alpha override.

A block needs >=2 texture-class free inputs to even be considered (nothing
to composite otherwise). Blocks whose evaluation hits an unsupported
opcode are skipped, not guessed at (see eval_engine.py).
"""
from __future__ import annotations

import argparse
import json
import math
import random
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from eval_engine import EvalUnsupported, evaluate_block  # noqa: E402
from ir import parse_shader  # noqa: E402

EPS = 1e-4


@dataclass
class InvariantResult:
    block_id: str
    source_hash: str
    layer_relations: list[dict]  # [{"alpha_driver": "t1", "overrides": "t0"}, ...]
    confidence: str


def _rand_vec4(rng: random.Random) -> list[float]:
    return [rng.uniform(0.05, 0.95) for _ in range(4)]


def _copy_assignments(assignments: dict[str, list[float]]) -> dict[str, list[float]]:
    return {k: v[:] for k, v in assignments.items()}


def test_block(ir, slice_idxs: list[int], sink_name: str, sink_swizzle: str, texture_names: list[str],
                assignments: dict[str, list[float]], rng: random.Random) -> list[dict]:
    """Run the affine + annihilation battery for one block. Returns the
    list of confirmed layer relations (empty if none found)."""

    def eval_at(assign, alpha_overrides: dict[str, float]) -> list[float]:
        a = _copy_assignments(assign)
        for name, alpha in alpha_overrides.items():
            a[name][3] = alpha
        return evaluate_block(ir, slice_idxs, sink_name, a, sink_swizzle)

    relations = []
    for tk in texture_names:
        # Tier 1: affine-in-alpha, checked at an interior point (not the boundary)
        f_lo = eval_at(assignments, {tk: 0.3})
        f_hi = eval_at(assignments, {tk: 0.7})
        f_mid = eval_at(assignments, {tk: 0.5})
        expected_mid = [(f_lo[i] + f_hi[i]) / 2 for i in range(3)]
        # NaN/Inf anywhere makes abs(x) > EPS evaluate False in Python (NaN
        # compares False against everything), so a block that goes out of a
        # real instruction's domain under random sampling would otherwise
        # silently pass the affine check instead of being rejected --
        # explicit isfinite guard, not relying on the comparison to fail safe.
        if not all(math.isfinite(v) for v in f_lo + f_hi + f_mid):
            continue
        if any(abs(f_mid[i] - expected_mid[i]) > EPS for i in range(3)):
            continue

        # Tier 2: annihilation -- at alpha=1, output insensitive to some other layer's rgb
        for tj in texture_names:
            if tj == tk:
                continue
            a1 = _copy_assignments(assignments)
            a1[tk][3] = 1.0
            a2 = _copy_assignments(a1)
            a2[tj][0:3] = [rng.uniform(0.05, 0.95) for _ in range(3)]
            out1 = evaluate_block(ir, slice_idxs, sink_name, a1, sink_swizzle)
            out2 = evaluate_block(ir, slice_idxs, sink_name, a2, sink_swizzle)
            if not all(math.isfinite(v) for v in out1 + out2):
                continue
            if all(abs(out1[i] - out2[i]) <= EPS for i in range(3)):
                relations.append({"alpha_driver": tk, "overrides": tj})

    return relations


def run(decomposed_dir: Path, seed: int = 0) -> list[InvariantResult]:
    rng = random.Random(seed)
    results: list[InvariantResult] = []
    ir_cache: dict[str, object] = {}
    asm_dir = decomposed_dir.parent / "asm"

    for block_path in sorted(decomposed_dir.glob("*.json")):
        rec = json.loads(block_path.read_text())
        texture_names = sorted({f["name"] for f in rec["free_inputs"] if f["cls"] == "texture"})
        if len(texture_names) < 2:
            continue

        source_hash = rec["source_hash"]
        if source_hash not in ir_cache:
            ir_cache[source_hash] = parse_shader(asm_dir / f"{source_hash}.asm")
        ir = ir_cache[source_hash]

        all_free_names = {f["name"] for f in rec["free_inputs"]}
        assignments = {name: _rand_vec4(rng) for name in all_free_names}

        try:
            relations = test_block(ir, rec["instructions"], rec["sink_register"], rec.get("sink_swizzle", "xyzw"),
                                    texture_names, assignments, rng)
        except EvalUnsupported:
            continue

        if relations:
            confidence = "likely" if len(relations) >= 2 else "weak"
            results.append(InvariantResult(rec["id"], source_hash, relations, confidence))

    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decomposed-dir", type=Path, default=Path("references/wow_shaders/decomposed"))
    ap.add_argument("--output", type=Path, default=Path("references/wow_shaders/invariant_survivors.json"))
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    results = run(args.decomposed_dir, args.seed)
    args.output.write_text(json.dumps(
        [{"id": r.block_id, "source_hash": r.source_hash, "confidence": r.confidence,
          "layer_relations": r.layer_relations} for r in results],
        indent=2,
    ))
    by_source = {}
    for r in results:
        by_source.setdefault(r.source_hash, []).append(r)
    print(f"{len(results)} surviving blocks across {len(by_source)} shaders. Report: {args.output}")
    for h, rs in sorted(by_source.items()):
        print(f"  {h}: {len(rs)} block(s), best confidence "
              f"{'likely' if any(r.confidence == 'likely' for r in rs) else 'weak'}")


if __name__ == "__main__":
    main()
