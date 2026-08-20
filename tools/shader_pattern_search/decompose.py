#!/usr/bin/env python3
"""Layer 1: decompose ubershaders into individual candidate function blocks.

Each block is one instruction's exact backward-dependency slice (see
ir.ShaderIR.slice_from) -- a self-contained little DAG of arithmetic ending
in one temp register or output write, with pointers back to the source
shader and its own free-input list (the registers it reads but never
defines -- its parameters, from the block's point of view).

Deliberately does NOT use patterns.find_lerp_idioms (or any other
syntax-specific shape detector) to pick which instructions are worth
extracting. That would silently exclude any block whose author encoded the
same math differently -- exactly the false-negative failure mode this
whole two-layer design exists to avoid. Sink selection here is only ever a
cheap, syntax-agnostic *necessary* condition (touches a texture sample, has
more than one independent input, isn't absurdly large) -- a Bloom-filter-
style prefilter, not a decision. The actual decision belongs to Layer 2's
numeric invariant tests (invariants.py), which only need this stage to not
have thrown away a real candidate.

Usage:
    tools/venv/bin/python3 tools/shader_pattern_search/decompose.py \
        --asm-dir references/wow_shaders/asm \
        --output-dir references/wow_shaders/decomposed
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ir import ShaderIR, parse_shader  # noqa: E402

MIN_SLICE_SIZE = 2
MAX_SLICE_SIZE = 30
MIN_FREE_INPUTS = 2  # need at least 2 independent things to "combine"


def candidate_sinks(ir: ShaderIR) -> list[list[int]]:
    """All slices worth persisting as a decomposed block: every o0/oMask
    write (unconditionally -- these are always meaningful, however large),
    plus every temp-register write whose slice passes the cheap structural
    prefilter described in the module docstring."""
    slices: list[list[int]] = []
    for sink in ("o0", "oMask"):
        slices.extend(ir.slice_for_output(sink))

    for instr in ir.instructions:
        if instr.dest is None or instr.dest.cls != "temp":
            continue
        s = ir.slice_from(instr.idx)
        if not (MIN_SLICE_SIZE <= len(s) <= MAX_SLICE_SIZE):
            continue
        if not any(ir.by_idx(i).opcode.startswith("sample_indexable") for i in s):
            continue
        leaves = ir.leaves_of(s)
        if len({leaf.name for leaf in leaves}) < MIN_FREE_INPUTS:
            continue
        slices.append(s)

    return dedup_maximal(slices)


def dedup_maximal(slices: list[list[int]]) -> list[list[int]]:
    """Drop only exact-duplicate slices.

    An earlier version of this also dropped any slice that was a subset of
    an already-kept slice within a couple instructions' difference, on the
    assumption that a small size gap meant a trivial extension (an extra
    clamp/mov) not worth keeping separately. Verified wrong against real
    data: the pure 23-instruction tint-composite block in
    47c35a45740c769d got silently dropped that way, subsumed by a
    25-instruction sibling slice that tacked on the *start* of the fog
    blend that consumes it -- 2 extra instructions, but semantically a
    different, less pure computation, not a trivial variant. There's no
    cheap structural way to tell "harmless extra clamp" from "start of an
    unrelated downstream computation" without evaluating semantics, which
    is Layer 2's job, not this stage's. Given the choice between some
    redundant near-duplicate blocks and silently losing the one that
    matters, keep everything and let Layer 2's numeric invariants do the
    real filtering -- a false negative here is much worse than some wasted
    evaluation cycles later."""
    return [sorted(s) for s in {frozenset(s) for s in slices}]


def block_record(ir: ShaderIR, source_hash: str, slice_idxs: list[int]) -> dict:
    sink_idx = slice_idxs[-1]
    sink_instr = ir.by_idx(sink_idx)
    leaves = ir.leaves_of(slice_idxs)
    return {
        "id": f"{source_hash}__sink{sink_idx}",
        "source_hash": source_hash,
        "sink_idx": sink_idx,
        "sink_register": sink_instr.dest.name if sink_instr.dest else sink_instr.opcode,
        # which raw x/y/z/w slots the sink instruction actually wrote, in
        # write order -- NOT necessarily "xyz". A dest writemask of e.g.
        # ".yzw" (real, seen in 47c35a45740c769d) means the meaningful
        # 3-vector lives in state slots 1,2,3, not 0,1,2. Comparing against
        # a fixed [0:3] silently compares the wrong numbers for any sink
        # that doesn't happen to write x first.
        "sink_swizzle": (sink_instr.dest.swizzle if sink_instr.dest else "") or "xyzw",
        "instructions": slice_idxs,
        "free_inputs": [{"name": op.name, "cls": op.cls} for op in leaves],
        "raw": [ir.by_idx(i).raw for i in slice_idxs],
    }


def decompose_file(path: Path, out_dir: Path) -> list[dict]:
    ir = parse_shader(path)
    source_hash = path.stem
    records = [block_record(ir, source_hash, s) for s in candidate_sinks(ir)]
    for rec in records:
        (out_dir / f"{rec['id']}.json").write_text(json.dumps(rec, indent=2))
    return records


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--asm-dir", type=Path, default=Path("references/wow_shaders/asm"))
    ap.add_argument("--output-dir", type=Path, default=Path("references/wow_shaders/decomposed"))
    args = ap.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = args.output_dir / "manifest.jsonl"
    files = sorted(args.asm_dir.glob("*.asm"))
    total_blocks = 0
    with manifest_path.open("w") as manifest:
        for i, path in enumerate(files):
            try:
                records = decompose_file(path, args.output_dir)
            except Exception as exc:  # noqa: BLE001 -- one bad file shouldn't abort the whole corpus
                print(f"WARN {path.name}: {exc}", file=sys.stderr)
                continue
            total_blocks += len(records)
            for rec in records:
                manifest.write(json.dumps({
                    "id": rec["id"], "source_hash": rec["source_hash"],
                    "n_instructions": len(rec["instructions"]),
                    "n_free_inputs": len(rec["free_inputs"]),
                }) + "\n")
            if (i + 1) % 200 == 0:
                print(f"...{i + 1}/{len(files)} decomposed, {total_blocks} blocks so far", file=sys.stderr)

    print(f"Decomposed {len(files)} shaders into {total_blocks} candidate blocks under {args.output_dir}")
    print(f"Manifest: {manifest_path}")


if __name__ == "__main__":
    main()
