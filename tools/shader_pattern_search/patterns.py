"""Extensible library of known shader-math shapes to search for.

A "pattern" is a plain function decorated with @register_pattern. It takes
a parsed ShaderIR and one backward-dependency slice (a list of instruction
indices, from ir.slice_for_output), and returns zero or more Match objects.
Adding a new shape to hunt for is adding a new function here -- scan.py and
ir.py never need to change.

Shared idiom detectors (find_lerp_idioms, etc.) live here too, above the
patterns that use them, so a new pattern can compose existing idiom-finders
instead of re-deriving them.
"""
from __future__ import annotations

from dataclasses import dataclass

from ir import Instruction, ShaderIR

PATTERN_REGISTRY: list = []


def register_pattern(fn):
    PATTERN_REGISTRY.append(fn)
    return fn


@dataclass
class Match:
    pattern: str
    confidence: str  # "likely" | "weak"
    instructions: list[int]
    detail: str


# ---------------------------------------------------------------------------
# Shared idiom detectors
# ---------------------------------------------------------------------------

def find_lerp_idioms(ir: ShaderIR, slice_idxs: list[int]) -> list[dict]:
    """Find the SM5 mad/mad idiom for lerp(low, high, factor):

        mad D, blended, tint, -low     ; D = blended*tint - low
        mad D, factor,  D,    low      ; D = factor*D + low  == lerp(low, blended*tint, factor)

    Real captured example (references/wow_shaders/asm/47c35a45740c769d.asm,
    lines 73-74):

        mad r2.xyz, r2.xyzx, cb0[11].xyzx, -cb0[10].xyzx
        mad r2.xyz, r2.w,    r2.xyzx,       cb0[10].xyzx

    Returns one dict per matched pair: idx of both instructions, and the
    operands that make up low/high/blend/tint, for downstream patterns or
    for printing a human-readable summary.
    """
    mads = [ir.by_idx(i) for i in slice_idxs if ir.by_idx(i).opcode in ("mad", "mad_sat")]
    found = []
    for lo_instr in mads:
        if lo_instr.dest is None or len(lo_instr.srcs) < 3:
            continue
        low = lo_instr.srcs[2]
        if not low.neg:
            continue
        for hi_instr in mads:
            if hi_instr is lo_instr or hi_instr.dest is None or len(hi_instr.srcs) < 3:
                continue
            blend_factor, d_ref, high = hi_instr.srcs[0], hi_instr.srcs[1], hi_instr.srcs[2]
            if d_ref.name != lo_instr.dest.name or high.neg:
                continue
            if high.name != low.name:
                continue
            found.append({
                "low_idx": lo_instr.idx,
                "high_idx": hi_instr.idx,
                "low": low,
                "high": high,
                "blend_factor": blend_factor,
                "blended": lo_instr.srcs[0],
                "tint": lo_instr.srcs[1],
            })
    return found


def count_samples(ir: ShaderIR, slice_idxs: list[int]) -> list[Instruction]:
    return [ir.by_idx(i) for i in slice_idxs if ir.by_idx(i).opcode.startswith("sample_indexable")]


def local_sample_windows(ir: ShaderIR, gap: int = 15, trailing: int = 20) -> list[list[int]]:
    """Candidate regions: contiguous instruction-index ranges around each
    cluster of nearby texture samples, rather than a full backward slice
    from an output. A full o0-dependency slice pulls in everything the
    output *eventually* depends on (fog, lighting, tone mapping...); the
    combiner/composite motifs this tool hunts for are tight and local --
    a handful of samples followed shortly by their blend math -- so a local
    window around each sample cluster isolates them far better than trying
    to prune the full transitive closure down to just the relevant part.

    `gap`: samples more than this many instructions apart start a new
    cluster. `trailing`: how many instructions past the last sample in a
    cluster to include, to capture the blend math that follows the samples.
    """
    sample_idxs = [i.idx for i in ir.instructions if i.opcode.startswith("sample_indexable")]
    if not sample_idxs:
        return []
    groups: list[list[int]] = [[sample_idxs[0]]]
    for x in sample_idxs[1:]:
        if x - groups[-1][-1] <= gap:
            groups[-1].append(x)
        else:
            groups.append([x])
    last_idx = ir.instructions[-1].idx if ir.instructions else 0
    return [list(range(g[0], min(g[-1] + trailing, last_idx) + 1)) for g in groups]


# ---------------------------------------------------------------------------
# Patterns
# ---------------------------------------------------------------------------

@register_pattern
def combiners_mod_family(ir: ShaderIR, slice_idxs: list[int]) -> list[Match]:
    """Small mul-only chain: 1-2 texture samples straight into the sink, no
    lerp, no branch depth. Matches the confirmed Combiners_Mod /
    Combiners_Mod_Mod shape (93dd1d60a5da7d7f / dea0d96a0e1d054e)."""
    samples = count_samples(ir, slice_idxs)
    if not (1 <= len(samples) <= 2):
        return []
    if find_lerp_idioms(ir, slice_idxs):
        return []
    if len(slice_idxs) > 8:
        return []
    if any(ir.by_idx(i).depth > 0 for i in slice_idxs):
        return []
    return [Match(
        pattern="combiners_mod_family", confidence="likely", instructions=slice_idxs,
        detail=f"{len(samples)} texture sample(s), straight multiply chain, no lerp/branch, {len(slice_idxs)} instrs",
    )]


@register_pattern
def two_layer_alpha_composite(ir: ShaderIR, slice_idxs: list[int]) -> list[Match]:
    """2 texture samples, 1 alpha-driven lerp idiom -- the shape an
    *equipped* tabard render might collapse to if it's baked down from the
    3-layer editor-preview version (see SHADER_SCAN_FINDINGS.md's
    live-capture follow-up, hypothesis 3)."""
    samples = count_samples(ir, slice_idxs)
    lerps = find_lerp_idioms(ir, slice_idxs)
    if len(samples) == 2 and len(lerps) == 1:
        return [Match(
            pattern="two_layer_alpha_composite", confidence="likely", instructions=slice_idxs,
            detail="2 texture samples, 1 alpha-driven lerp -- equipped-tabard candidate shape",
        )]
    return []


@register_pattern
def three_layer_tint_composite(ir: ShaderIR, slice_idxs: list[int]) -> list[Match]:
    """3 texture samples, 2 chained alpha-driven lerps against distinct
    tint constants -- the exact shape traced by hand in
    47c35a45740c769d (tabard-vendor capture): base*lerp(bg, border*tint, a1)
    then lerp(that, emblem*tint, a2). See SHADER_SCAN_FINDINGS.md."""
    samples = count_samples(ir, slice_idxs)
    lerps = find_lerp_idioms(ir, slice_idxs)
    if len(samples) == 3 and len(lerps) == 2:
        return [Match(
            pattern="three_layer_tint_composite", confidence="likely", instructions=slice_idxs,
            detail="3 texture samples, 2 chained alpha-driven lerps -- editor/tabard-preview candidate shape",
        )]
    if len(samples) == 3 and len(lerps) >= 1:
        return [Match(
            pattern="three_layer_tint_composite", confidence="weak", instructions=slice_idxs,
            detail=f"3 texture samples, {len(lerps)} lerp idiom(s) found (expected 2) -- worth a manual look",
        )]
    return []
