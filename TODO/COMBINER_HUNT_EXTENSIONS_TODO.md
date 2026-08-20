# TODO: extend the combiner-hunt equivalence tester (Dual_Crossfade + Illum)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file. This file is meant to be **fully self-contained**
— read it cold, with no other session context, and it should be enough to
implement both items below.

## Background: what already exists, and why these two are missing

`tools/shader_pattern_search/` is a corpus-wide reverse-engineering tool
built to find WoW's undocumented M2 `Combiners_*`/`Guild*` pixel-shader
formulas (`TODO/PIXEL_SHADER_FORMULAS_TODO.md`) inside a captured
vkd3d-proton DXBC shader dump. Full narrative and results:
`references/wow_shaders/combiner_hunt/SUMMARY.md` — read that first if you
want the "why," this file only covers the "what's left."

Pipeline, in order:

1. `references/wow_shaders/asm/*.asm` — 1048 real captured `ps_5_0`
   shaders, already disassembled (`vkd3d-compiler`) from the raw
   `.dxbc`/`.spv` dump. Read-only input, don't regenerate.
2. `tools/shader_pattern_search/decompose.py` — parses each `.asm` into a
   def-use dataflow graph (`ir.py`) and extracts every plausible
   "combiner function" as a standalone block: source shader, sink
   register + its actual write-order swizzle (`sink_swizzle` — **not**
   always `xyz`, a real compiled-shader oddity found this session, see
   `eval_engine.py`'s docstring), the exact instruction slice, and a
   `free_inputs` list (registers the block reads but never defines within
   itself — its parameters). Output: `references/wow_shaders/decomposed/
   *.json` (one file per block) + `manifest.jsonl`. Regenerate with
   `tools/venv/bin/python3 tools/shader_pattern_search/decompose.py` (~6s,
   ~19,900 blocks).
3. `tools/shader_pattern_search/eval_engine.py` — a small concrete
   interpreter: given a block and concrete values for its free inputs
   (each a `[x,y,z,w]` float list), evaluates the instruction slice and
   returns the sink's actual output values in write order.
4. `tools/shader_pattern_search/formula_specs.py` — one `Formula(name,
   n_tex, n_const, fn, documented, note)` entry per candidate formula.
   `fn(tex, const) -> [r,g,b]` takes `tex` as a list of `n_tex` vec4s
   (rgb+alpha) and `const` as a list of `n_const` vec3s (constant tints,
   Guild-family only), and returns the formula's predicted diffuse rgb.
5. `tools/shader_pattern_search/equivalence.py` — for every block, tries
   every formula: permute the block's texture-class free inputs into
   `tex1..texN` roles and cbuffer-class free inputs into `generic0..N`
   const roles, fit an unmodeled per-channel scale (stands in for
   `mesh_color`, which these small blocks don't carry) on one random
   trial, then require that *same fixed scale* to reproduce the block's
   output on several more independent random trials. Real matches end up
   with `fitted_scale ≈ [1,1,1]`.

**Two real bugs were found and fixed in `equivalence.py` this session —
read `equivalence.py`'s comments near `EPS`/the `fitted_scale` checks
before touching this file, so you don't reintroduce either:**
- A near-zero fitted scale (`[0,0,0]`) means the block's own output is
  constant-zero regardless of input, and `0 * anything == 0` trivially
  "matches" every formula at once. Must be rejected, not treated as a
  match. (Ironically, this exact bug is the entire lead for Illum below —
  see that section.)
- NaN/Inf anywhere makes every `> EPS` comparison evaluate `False` in
  Python, silently turning a should-fail comparison into a pass. Every
  numeric check needs an explicit `math.isfinite` guard, not just the
  comparison operator.

**Parallelization**: this tool is single-threaded per invocation but the
machine has many cores. `equivalence.py` has `--formula-index N`
(test exactly `FORMULAS[N]`, see `--list-formulas`) and `--shard i/n`
(process only shaders where `zlib.crc32(source_hash) % n == i`) for
fanning a single expensive formula out across many parallel background
processes — used this session to cut the `Guild` search from an
open-ended serial run to ~2 minutes sharded 16 ways. Use both for
anything expensive here rather than waiting on one serial run.

## Item 1: external-weight-scalar search (unlocks 2 of the 17)

**Target formulas** (from `reference/wow.export/src/shaders/
m2.fragment.shader`, transcribed in `TODO/PIXEL_SHADER_FORMULAS_TODO.md`):

```glsl
case 26: // Combiners_Mod_Dual_Crossfade
    mixed = mix(mix(tex1, tex2, vec4(clamp(u_tex_sample_alpha.g, 0.0, 1.0))), tex3, vec4(clamp(u_tex_sample_alpha.b, 0.0, 1.0)));
    mat_diffuse = mesh_color * mixed.rgb;
    discard_alpha = mixed.a;  // can_discard = true

case 28: // Combiners_Mod_Masked_Dual_Crossfade
    mixed = mix(mix(tex1, tex2, vec4(clamp(u_tex_sample_alpha.g, 0.0, 1.0))), tex3, vec4(clamp(u_tex_sample_alpha.b, 0.0, 1.0)));
    mat_diffuse = mesh_color * mixed.rgb;
    discard_alpha = mixed.a * tex4.a;  // can_discard = true, 4th texture sampled at uv2
```

**Why the current tool can't find these**: every existing formula in
`formula_specs.py` sources its blend factors from a texture's own alpha
channel (e.g. `tex1.a`), which `equivalence.py` gets "for free" — once a
texture is bound to a `tex1..texN` role, its `.w` component is just
`tex_vals[k][3]`, no separate search needed. These two formulas instead
use `u_tex_sample_alpha.g`/`.b` — the wiki's `M2TextureWeight` concept
(`documentation/wowdev-wiki/wikitext/Pixel_shader_logic_for_mixing_colors.wiki`'s
own Symbols section), a value that in real DXBC shows up as a **constant-
buffer scalar component**, not a texture alpha. There's currently no
mechanism to bind a formula parameter to an arbitrary `(cbuffer_name,
component)` pair.

**Implementation plan:**

1. Extend `Formula` (`formula_specs.py`) with a new field `n_scalar: int
   = 0`, and change every `fn` signature to `fn(tex, const, scalars)`
   where `scalars` is a `list[float]` of length `n_scalar` (update all
   existing formula functions to accept and ignore the new parameter —
   mechanical, every one currently ends in `(tex, const):`).
2. Add two new spec functions:
   ```python
   def _mod_dual_crossfade(tex, const, scalars):
       t1, t2, t3 = tex[0][:3], tex[1][:3], tex[2][:3]
       wg, wb = max(0.0, min(1.0, scalars[0])), max(0.0, min(1.0, scalars[1]))
       inner = mix3(t1, t2, wg)
       return mix3(inner, t3, wb)
   ```
   `Combiners_Mod_Masked_Dual_Crossfade`'s diffuse math is **identical**
   to the above (it only differs by an extra `tex4.a` factor in
   `discard_alpha`, which this tool doesn't test) — register it with the
   same `fn`, same convention as the existing `"Combiners_Mod_Mod / ..."`
   multi-name equivalence-class entries already in the file, and say so
   in the `note` field rather than writing a second identical function.
3. In `equivalence.py`'s `try_match`: add a scalar-role search alongside
   the existing `tex_perm`/`const_perm` loops. Candidates are `(name,
   component_index)` pairs drawn from the block's cbuffer-class free
   inputs (`rec["free_inputs"]` entries with `cls == "cbuffer"`), each
   contributing 4 candidates (its x/y/z/w components). Use
   `itertools.permutations` over this candidate list for `n_scalar`
   slots (order matters — `wg` and `wb` are different roles), nested
   inside the existing loops. Fetch each candidate's concrete value per
   trial as `assignments[cbuf_name][component_idx]`.
4. **Watch combinatorial cost**: a block with 3 cbuffer free inputs gives
   12 `(name, component)` candidates, `P(12,2) = 132` scalar orderings —
   multiplied against the existing `tex_perm × const_perm × 6
   component_order` search, this can get expensive fast. Add a cap
   (mirror the existing `MAX_TEX_CANDIDATES` pattern) on how many
   `(name, component)` candidates get tried, and lean on `--shard` to
   parallelize rather than letting one process grind serially — exactly
   the lesson from the Guild search this session (don't relaunch an
   unsharded run for an expensive formula, ask for a shard count and
   fan out immediately).
5. Validate against the corpus, read the actual survivors by hand before
   trusting them (every real finding this session was hand-traced against
   the raw instruction algebra, not just accepted on the tool's verdict —
   see `SUMMARY.md`'s "Three real bugs found" section for why that
   discipline mattered). Update `PIXEL_SHADER_FORMULAS_TODO.md`'s per-
   formula annotations and `combiner_hunt/SUMMARY.md` with the result,
   including a genuine "not found" if that's what happens.

## Item 2: constant-output test tier (unlocks 1 of the 17)

**Target formula:**

```glsl
case 34: // Illum
    discard_alpha = tex1.a;  // can_discard = true
    // mat_diffuse is never set -- stays vec3(0.0)
```

Illum's real diffuse output is **always black, unconditionally** — not a
function of any texture or constant, just a hardcoded zero. This isn't
"does this block's output equal `spec_fn(inputs)`," the shape every other
formula in `equivalence.py` tests; it's "is this block's output constant
(and specifically zero) no matter what its inputs are." A different test
shape is needed, not another `Formula` entry.

**A concrete lead, not a cold start**: this session's zero-scale bug
(see Background above) was found by hand-tracing block
`01beee22c3c834c4__sink197` (in `references/wow_shaders/decomposed/`, or
re-derivable from `references/wow_shaders/asm/01beee22c3c834c4.asm`) —
its output is genuinely constant-zero by construction (a register
explicitly zeroed two instructions before the final multiply). That block
was correctly *rejected* as a false positive for every other formula it
was spuriously matching — but it might be a real `Illum` candidate. Check
it first once this tier exists.

**Implementation plan:**

1. New function (either a new small module, e.g. `constant_output.py`,
   or added to `invariants.py` alongside its existing battery — either is
   fine, follow whichever reads more naturally once you're looking at the
   current file layout).
2. For each decomposed block: run several trials (reuse the existing
   `TRIALS`/`EPS` conventions from `equivalence.py`), each with a fresh
   **fully independent** random assignment for every one of the block's
   free inputs (not just varying one texture's alpha the way
   `invariants.py`'s affine test does — regenerate everything). Evaluate
   the block via `eval_engine.evaluate_block` as usual. A block qualifies
   if its output stays within `EPS` of `[0,0,0]` across every trial.
   Remember the `math.isfinite` guard from the Background section — a
   NaN/Inf output must not count as "close to zero."
3. This is a **weaker** signal than the other formulas' exact-equivalence
   matches — plenty of things other than `Illum` could be genuinely
   constant-zero (dead code, an unreachable branch, an unrelated always-
   off effect). Report survivors the same way every other tier does
   (JSON to `references/wow_shaders/combiner_hunt/`, block id + source
   hash), but flag explicitly in the writeup that every survivor needs a
   manual read of the surrounding shader (not just the isolated block) to
   judge whether it's plausibly `Illum` specifically, not just "some dead
   code that happens to zero out."
4. Same validation/writeup discipline as item 1: hand-check before
   trusting, update `PIXEL_SHADER_FORMULAS_TODO.md` and
   `combiner_hunt/SUMMARY.md`.

## When both are done

Update this file's own status (trim to whatever's still open, delete
outright if both land and get written up — matches this repo's own
"punch list, not a historical record" convention every other `TODO/*.md`
follows), and update `TODO/README.md`'s `PIXEL_SHADER_FORMULAS_TODO.md`
row + this file's own index entry accordingly.
