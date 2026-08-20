#!/usr/bin/env python3
"""Layer 2, part C: full numeric equivalence testing against concrete
candidate formulas (formula_specs.py), not just generic shape invariants.

Stronger than invariants.py's affine/annihilation battery because we now
have an actual candidate formula for every one of the 17 undocumented
combiners (transcribed from wow.export -- see PIXEL_SHADER_FORMULAS_TODO.md
and formula_specs.py's docstring). For a given block and formula: search
over which of the block's texture-class free inputs play which of the
formula's texture roles (and cbuffer-class free inputs for any constant-
tint roles, Guild-family only), fit an unmodeled per-channel scale (stands
in for mesh_color, which isn't part of these small blocks) on one random
trial, then require that same fixed scale to reproduce the block's output
on several more independent random trials. A wrong role assignment
essentially never survives multiple independent random trials by chance --
this is a black-box equivalence oracle, not a shape heuristic.

Known gaps (not implemented, not silently guessed at):
- Combiners_Mod_Dual_Crossfade / _Masked_Dual_Crossfade need an external
  weight scalar as the blend factor itself (not a texture alpha) -- this
  tester only ever binds scalars implicitly as an already-assigned texture
  role's own alpha channel, never as an independent cbuffer component. Not
  extended to that search in this version.
- Illum's real formula is "always black" -- a constant-output property,
  not a formula this equivalence machinery is shaped to test. Skipped.
- Search cost is combinatorial in role count (permutations of the block's
  free inputs into formula roles) -- fine for the <=3 texture / <=3 const
  roles every implemented formula needs, would need pruning for anything
  bigger.
"""
from __future__ import annotations

import argparse
import json
import math
import random
import sys
import zlib
from itertools import permutations
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from eval_engine import EvalUnsupported, evaluate_block  # noqa: E402
from formula_specs import FORMULAS, Formula  # noqa: E402
from ir import parse_shader  # noqa: E402

EPS = 1e-3
TRIALS = 5
MAX_BLOCK_SIZE = 40  # real formula blocks are small; the huge o0/oMask full-slices
                      # (up to 42 free inputs -- permutation search explodes) aren't candidates
MAX_TEX_CANDIDATES = 6  # cap permutation search width per role type


def _rand_vec4(rng: random.Random) -> list[float]:
    return [rng.uniform(0.2, 0.8) for _ in range(4)]


def try_match(ir, slice_idxs: list[int], sink_name: str, sink_swizzle: str, formula: Formula,
               tex_names: list[str], cbuf_names: list[str], all_free_names: set[str],
               rng: random.Random) -> dict | None:
    if len(tex_names) < formula.n_tex or len(cbuf_names) < formula.n_const:
        return None
    if len(sink_swizzle) < 3:
        return None  # formulas here all predict a full rgb; a <3-component sink can't match

    trial_assignments = [{name: _rand_vec4(rng) for name in all_free_names} for _ in range(TRIALS)]

    for tex_perm in permutations(tex_names, formula.n_tex):
        for const_perm in permutations(cbuf_names, formula.n_const):
            # component_order handles a real compiler artifact: the sink's
            # written slots don't always correspond 1:1, in order, to the
            # formula's [r,g,b] -- try every 3-way reordering, not just
            # sink_swizzle's raw write order (see formula_specs.py /
            # SHADER_SCAN_FINDINGS.md's note on 47c35a45740c769d's final
            # instruction genuinely duplicating one component into two
            # output slots and dropping a third -- that specific case is
            # NOT fixable by reordering, this only handles "different but
            # consistent order," not "duplicated/dropped channel").
            for component_order in permutations(range(3)):
                scale = None
                ok = True
                for t_i, assignments in enumerate(trial_assignments):
                    tex_vals = [assignments[n] for n in tex_perm]
                    const_vals = [assignments[n][:3] for n in const_perm]
                    spec_out = formula.fn(tex_vals, const_vals)
                    try:
                        raw_out = evaluate_block(ir, slice_idxs, sink_name, assignments, sink_swizzle)
                    except EvalUnsupported:
                        ok = False
                        break
                    block_out = [raw_out[component_order[i]] for i in range(3)]
                    # NaN/Inf anywhere makes every subsequent "> EPS" comparison
                    # False (NaN compares False against everything in Python),
                    # so a block that goes out of a real instruction's domain
                    # under this tester's domain-agnostic random sampling
                    # (sqrt/log/div hitting a value the real shader never
                    # intended) would otherwise trivially "pass" -- same
                    # degenerate-match shape as the near-zero-scale case below,
                    # found the same way (by hand, reading a real "match").
                    if any(not math.isfinite(v) for v in block_out):
                        ok = False
                        break
                    if t_i == 0:
                        if any(abs(spec_out[i]) < 1e-3 for i in range(3)):
                            ok = False
                            break
                        # A near-zero fitted scale means the block's own
                        # output is ~constant-zero regardless of input --
                        # real case found by hand (01beee22c3c834c4__sink197,
                        # a genuinely dead/always-black sink), where 0*anything
                        # trivially "matches" every formula at once. That's a
                        # tester artifact, not evidence -- reject it here.
                        if any(abs(block_out[i]) < 1e-3 for i in range(3)):
                            ok = False
                            break
                        scale = [block_out[i] / spec_out[i] for i in range(3)]
                        if any(not math.isfinite(v) for v in scale):
                            ok = False
                            break
                        continue
                    predicted = [spec_out[i] * scale[i] for i in range(3)]
                    if any(not math.isfinite(v) for v in predicted):
                        ok = False
                        break
                    if any(abs(predicted[i] - block_out[i]) > EPS for i in range(3)):
                        ok = False
                        break
                if ok:
                    return {"tex_roles": list(tex_perm), "const_roles": list(const_perm),
                            "fitted_scale": scale, "component_order": list(component_order)}
    return None


def scan(decomposed_dir: Path, seed: int = 0, only_formula: str | None = None,
          shard: tuple[int, int] | None = None) -> dict[str, list[dict]]:
    rng = random.Random(seed)
    asm_dir = decomposed_dir.parent / "asm"
    ir_cache: dict[str, object] = {}
    results: dict[str, list[dict]] = {}
    formulas = [f for f in FORMULAS if only_formula is None or only_formula.lower() in f.name.lower()]

    for block_path in sorted(decomposed_dir.glob("*.json")):
        rec = json.loads(block_path.read_text())
        if shard is not None:
            idx, n = shard
            # shard by source shader, not by block, so one shader's blocks
            # (and its parsed ShaderIR) never split across two processes.
            # zlib.crc32, not the builtin hash() -- str hashing is
            # randomized per-process (PYTHONHASHSEED) unless disabled, which
            # would make each shard process disagree on the partition.
            if (zlib.crc32(rec["source_hash"].encode()) % n) != idx:
                continue
        if len(rec["instructions"]) > MAX_BLOCK_SIZE:
            continue  # the huge o0/oMask full-slices; see MAX_BLOCK_SIZE's comment
        tex_names = [f["name"] for f in rec["free_inputs"] if f["cls"] == "texture"][:MAX_TEX_CANDIDATES]
        cbuf_names = [f["name"] for f in rec["free_inputs"] if f["cls"] == "cbuffer"][:MAX_TEX_CANDIDATES]
        if not tex_names:
            continue
        all_free_names = {f["name"] for f in rec["free_inputs"]}

        source_hash = rec["source_hash"]
        if source_hash not in ir_cache:
            ir_cache[source_hash] = parse_shader(asm_dir / f"{source_hash}.asm")
        ir = ir_cache[source_hash]

        sink_swizzle = rec.get("sink_swizzle", "xyzw")
        for formula in formulas:
            match = try_match(ir, rec["instructions"], rec["sink_register"], sink_swizzle, formula,
                               tex_names, cbuf_names, all_free_names, rng)
            if match:
                results.setdefault(formula.name, []).append({
                    "block_id": rec["id"], "source_hash": source_hash, **match,
                })

    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decomposed-dir", type=Path, default=Path("references/wow_shaders/decomposed"))
    ap.add_argument("--output", type=Path, default=Path("references/wow_shaders/combiner_hunt/equivalence_results.json"))
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--formula", type=str, default=None, help="substring filter, e.g. --formula Guild")
    ap.add_argument("--formula-index", type=int, default=None,
                     help="test exactly FORMULAS[i] -- for fanning out one process per formula "
                          "in parallel (this tool is otherwise single-threaded); see combiner_hunt/README.md")
    ap.add_argument("--list-formulas", action="store_true", help="print index: name for each formula and exit")
    ap.add_argument("--shard", type=str, default=None,
                     help="'i/n' -- process only shaders where crc32(hash) %% n == i, for fanning out "
                          "one expensive formula across many processes by source file instead of by formula")
    args = ap.parse_args()

    if args.list_formulas:
        for i, f in enumerate(FORMULAS):
            print(f"{i}: {f.name}")
        return

    shard = None
    if args.shard:
        idx_s, n_s = args.shard.split("/")
        shard = (int(idx_s), int(n_s))

    only = FORMULAS[args.formula_index].name if args.formula_index is not None else args.formula
    results = scan(args.decomposed_dir, args.seed, only, shard)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(results, indent=2, sort_keys=True))

    total = sum(len(v) for v in results.values())
    print(f"{total} matching blocks across {len(results)} formulas. Report: {args.output}")
    for name, matches in sorted(results.items(), key=lambda kv: -len(kv[1])):
        sources = sorted({m["source_hash"] for m in matches})
        print(f"  {name}: {len(matches)} block(s), {len(sources)} distinct shader(s)")


if __name__ == "__main__":
    main()
