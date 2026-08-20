#!/usr/bin/env python3
"""Layer 2, part D: constant-output testing (Illum, PIXEL_SHADER_FORMULAS_TODO.md).

Every other formula tester here (equivalence.py, invariants.py) asks "is
this block's output a function of its inputs matching shape X." Illum's
real diffuse math is different in kind, not degree:

    case 34: // Illum
        discard_alpha = tex1.a;  // can_discard = true
        // mat_diffuse is never set -- stays vec3(0.0)

The output is unconditionally zero, regardless of every free input. That's
"is this block's output constant" -- a property equivalence.py's formula
matching can't express (there's no `tex`/`const`/`scalars` combination that
means "ignore all of your arguments").

This is a **weaker** signal than an equivalence.py match: equivalence.py
requires a wrong role assignment to survive several independent random
trials, which essentially never happens by chance for a *specific* formula
shape. Constant-zero output is a much broader property -- dead code, an
unreachable branch, or an always-off effect can all be genuinely
constant-zero without being Illum. Every survivor needs a manual read of
the surrounding shader (not just the isolated block) before it's trusted;
see COMBINER_HUNT_EXTENSIONS_TODO.md item 2 and this module's own
docstring for a concrete first block to check
(01beee22c3c834c4__sink197 -- a real block found and rejected as a
degenerate false-positive by equivalence.py's near-zero-scale guard, see
that module's comments).

Same isfinite discipline as equivalence.py/invariants.py: NaN/Inf must not
be silently treated as "close to zero."
"""
from __future__ import annotations

import argparse
import json
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from eval_engine import EvalUnsupported, evaluate_block  # noqa: E402
from ir import parse_shader  # noqa: E402

EPS = 1e-3
TRIALS = 5


def _rand_vec4(rng: random.Random) -> list[float]:
    return [rng.uniform(0.05, 0.95) for _ in range(4)]


def is_constant_zero(ir, slice_idxs: list[int], sink_name: str, sink_swizzle: str,
                      all_free_names: set[str], rng: random.Random) -> bool:
    """True if the block's output is within EPS of [0,0,0] (in the sink's
    own write order) across several independent, fully-random-per-trial
    assignments of every free input. False (not just "no," also "can't
    tell") on any unsupported opcode or non-finite output."""
    if len(sink_swizzle) < 3:
        return False
    for _ in range(TRIALS):
        assignments = {name: _rand_vec4(rng) for name in all_free_names}
        try:
            out = evaluate_block(ir, slice_idxs, sink_name, assignments, sink_swizzle)
        except EvalUnsupported:
            return False
        if not all(math.isfinite(v) for v in out[:3]):
            return False
        if any(abs(v) > EPS for v in out[:3]):
            return False
    return True


def scan(decomposed_dir: Path, seed: int = 0, shard: tuple[int, int] | None = None) -> list[dict]:
    import zlib

    rng = random.Random(seed)
    asm_dir = decomposed_dir.parent / "asm"
    ir_cache: dict[str, object] = {}
    results: list[dict] = []

    for block_path in sorted(decomposed_dir.glob("*.json")):
        rec = json.loads(block_path.read_text())
        if shard is not None:
            idx, n = shard
            if (zlib.crc32(rec["source_hash"].encode()) % n) != idx:
                continue

        source_hash = rec["source_hash"]
        if source_hash not in ir_cache:
            ir_cache[source_hash] = parse_shader(asm_dir / f"{source_hash}.asm")
        ir = ir_cache[source_hash]

        all_free_names = {f["name"] for f in rec["free_inputs"]}
        sink_swizzle = rec.get("sink_swizzle", "xyzw")
        if is_constant_zero(ir, rec["instructions"], rec["sink_register"], sink_swizzle, all_free_names, rng):
            results.append({"block_id": rec["id"], "source_hash": source_hash})

    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decomposed-dir", type=Path, default=Path("references/wow_shaders/decomposed"))
    ap.add_argument("--output", type=Path,
                     default=Path("references/wow_shaders/combiner_hunt/constant_output_results.json"))
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--shard", type=str, default=None,
                     help="'i/n' -- process only shaders where crc32(hash) %% n == i")
    args = ap.parse_args()

    shard = None
    if args.shard:
        idx_s, n_s = args.shard.split("/")
        shard = (int(idx_s), int(n_s))

    results = scan(args.decomposed_dir, args.seed, shard)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(results, indent=2, sort_keys=True))

    sources = sorted({r["source_hash"] for r in results})
    print(f"{len(results)} constant-zero block(s) across {len(sources)} distinct shader(s). "
          f"Report: {args.output}")
    print("NOTE: constant-zero is a weak signal (dead code / unreachable branches also qualify) -- "
          "every survivor needs a manual read of the surrounding shader before treating it as Illum.")


if __name__ == "__main__":
    main()
