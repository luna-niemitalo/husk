# shader_pattern_search

Parses vkd3d-compiler `d3d-asm` text (`references/wow_shaders/asm/*.asm`)
into a def-use dataflow graph and searches it for known shader-math shapes,
instead of reading files by hand one at a time. Built for the
`PIXEL_SHADER_FORMULAS_TODO.md` / `SHADER_SCAN_FINDINGS.md` combiner hunt —
see that doc's "Live-capture follow-up" section for the motivating case
(the guild-tabard 3-layer composite found buried inside an 825-line
ubershader, which is what this tool exists to find automatically next time).

## Two independent approaches, same goal

**1. Textual pattern matching** (`ir.py` + `patterns.py` + `scan.py`) —
parses `d3d-asm` into a def-use dataflow graph, matches registered
instruction-shape patterns against it. Fast, but only recognizes the exact
instruction encoding each pattern was written against — a differently
scheduled/reordered but algebraically identical formula won't match. See
the "Known limitations" section below.

**2. Invariant-based matching** (`decompose.py` + `eval_engine.py` +
`invariants.py`) — built after hitting that limitation for real (see
`SHADER_SCAN_FINDINGS.md`'s "Invariant-based matching" section for the
full story). Decomposes every shader into standalone function blocks
first (Layer 1), then concretely *evaluates* each block against sampled
inputs and tests numeric properties true of the target math regardless of
how it's encoded (Layer 2) — e.g. "is the output affine in this texture's
alpha, and does alpha=1 make the output ignore some other layer entirely."
This is the one to reach for when you suspect the textual matcher is
missing real matches, not just producing false positives.

## Layout

- `ir.py` — parser + def-use graph builder. `parse_shader(path) -> ShaderIR`.
  `ShaderIR.slice_from(idx)` / `.slice_for_output(name)` do backward
  dependency slicing; `.leaves_of(slice)` finds a slice's free inputs.
- `patterns.py` — approach 1's extensible part. Each pattern is a plain
  function decorated `@register_pattern`, taking `(ShaderIR, slice_idxs)`
  and returning `list[Match]`. Add a new shape by adding a new function
  here. Shared idiom-detectors (`find_lerp_idioms`, `count_samples`,
  `local_sample_windows`) live above the patterns that use them.
- `scan.py` — approach 1's CLI.
- `decompose.py` — approach 2, Layer 1. Extracts every candidate block as
  its own JSON file under `references/wow_shaders/decomposed/`, plus a
  `manifest.jsonl` index. Sink selection is deliberately syntax-agnostic
  (see the module docstring) — it must not lean on `patterns.py`'s shape
  detectors, or it would just move their blind spot one stage earlier.
- `eval_engine.py` — approach 2, Layer 2a. A small concrete interpreter
  for a block's instruction slice, given values for its free inputs.
  Narrow, honest opcode coverage — unsupported opcodes raise
  `EvalUnsupported` rather than guessing.
- `invariants.py` — approach 2, Layer 2b. The invariant battery + CLI.
  Currently one family implemented ("N-layer tinted alpha composite":
  affine-in-alpha, then annihilation). Add a new target formula's
  invariants as a new test function here, same extensibility idea as
  `patterns.py`'s registry.

## Usage

```
# approach 1: textual pattern matching
tools/venv/bin/python3 tools/shader_pattern_search/scan.py --self-test
tools/venv/bin/python3 tools/shader_pattern_search/scan.py               # -> references/wow_shaders/pattern_scan_report.json
tools/venv/bin/python3 tools/shader_pattern_search/scan.py --file <hash>.asm -v

# approach 2: invariant-based matching
tools/venv/bin/python3 tools/shader_pattern_search/decompose.py          # -> references/wow_shaders/decomposed/
tools/venv/bin/python3 tools/shader_pattern_search/invariants.py         # -> references/wow_shaders/invariant_survivors.json
```

## Known limitations (read before trusting a match)

Shared by both approaches:

- Dataflow is tracked per register, not per component — a write to any
  component of `r2` counts as defining all of `r2`. Never loses a real
  edge, occasionally adds a spurious one.
- `loop`/`endloop` back-edges aren't modeled — irrelevant for the small
  sample→blend→output chains this hunts for, would matter for tracing the
  light-accumulation loops themselves.
- **Every match is structural/numeric, not semantic.** A survivor means
  "this code has the shape of an N-layer alpha composite," which is true
  of a tabard *and* of Fresnel water reflection blending *and* possibly
  other things nobody's checked yet. A match still needs a manual read
  before it counts as a real finding — the tool's job is cutting the
  manual-read set down, never replacing the read itself.

Approach-1-specific: only recognizes the exact instruction encoding each
pattern was written against (confirmed real: see `SHADER_SCAN_FINDINGS.md`'s
193-vs-12-survivor gap once approach 2 was built).

Approach-2-specific: `eval_engine.py`'s opcode coverage is narrow by
design — a block using an unsupported opcode is silently skipped, not
flagged, so a corpus run's survivor count is a floor, not an exact count.
The invariant battery only currently covers one formula family (alpha
composites); a different target formula needs its own invariants written
in `invariants.py` before this approach can find it at all.
