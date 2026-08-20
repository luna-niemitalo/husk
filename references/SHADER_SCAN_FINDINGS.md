# WoW shader intermediary scan — 2026-08-20

## Investigation targets for Luna (flagged for personal follow-up)

- **`498e864b92fcf1b0`** — looks like a skin/subsurface-scattering wrap-light
  shader: two textures, dots an NTSC-luma weight (`0.299/0.587/0.144`)
  against a squared+tinted sample, tint constants ≈ `(0.325, 0.576, 0.659)`.
  Not one of the 36 `s_modelPixelShaders` combiners — flagged as a distinct,
  real effect worth a closer look on its own. Full disassembly:
  `references/wow_shaders/asm/498e864b92fcf1b0.asm`.
  ```
  sample_indexable(texture2d) r0.xyz, v1.zwzz, t1.xyzw, s1
  mul r0.xyz, r0.xyzx, r0.xyzx
  sample_indexable(texture2d) r1.xyz, v1.xyxx, t0.xyzw, s0
  mad r0.xyz, r0.xyzx, cb1[0].x, r1.xyzx
  dp3_sat r0.x, r0.xyzx, l(2.98999995e-01, 5.87000012e-01, 1.43999994e-01, 0.00000000e+00)
  add r0.y, -r0.x, l(1.00000000e+00)
  mul r0.y, r0.y, r0.x
  mul r0.y, r0.y, l(4.00000000e+00)
  min r0.y, r0.y, l(1.00000000e+00)
  mad o0.xyz, r0.y, l(3.24999988e-01, 5.75999975e-01, 6.58999979e-01, 0.00000000e+00), r0.x
  mov o0.w, l(1.00000000e+00)
  ret
  ```

- **`1be3de08da60a518`** (and its close relative `282249176b1de7e5`) — terrain
  texture-atlas decal shader: bilinear-filters across atlas tile seams with
  explicit seam-wrap math (`resinfo_indexable`, wrap-around UV blending via
  `bfi`/indexable temps to pick the right neighboring tile), not a simple
  texture sample. Genuinely complex, not one of the 36 combiners. Full file:
  `references/wow_shaders/asm/1be3de08da60a518.asm` (201 lines, too long to
  inline here).

- **`7244e298beaf7f3c`** / **`e3528693772b70e9`** — water-surface shaders:
  dual normal-map sampling (two texture reads of the same normal map at
  different UV offsets, each unpacked from `[0,1]` to `[-1,1]` via the
  `mad ..., 2.0, -1.0` pattern), Fresnel-style view-dependent blend, and a
  soft-edge/shoreline fade against depth (`t2` sampled against `v4.z`).
  Full files: `references/wow_shaders/asm/7244e298beaf7f3c.asm` (56 lines),
  `references/wow_shaders/asm/e3528693772b70e9.asm` (66 lines).

Goal: locate readable-but-stripped `.dxbc`/`.spv` intermediary shader files left
on disk by the Vulkan→DX12 translation pipeline (dev-flag artifact), to match
against known pixel-shader formulas. Scope was **exactly** `.dxbc`/`.spv`,
nothing else, searched across every real mount on `foxnest`
(`/media/luna/{data,games,models,userdata,work}`, `/home/luna`), skipping
pseudo-filesystems and guaranteed-irrelevant subtrees.

## Corpus (copied into `references/wow_shaders/`)

- **Primary source**: `/media/luna/work/cache/wow_shader_dump/` — 1023
  matched `.dxbc`/`.spv` pairs (2046 files, ~16M). This is the real dump,
  most complete set found.
- **Bonus**: `~/tinker/dev/wow_modding/export/shaders/00075004bca140b6.asm` —
  one shader (hash `00075004bca140b6`) already disassembled to readable
  `ps_5_0` HLSL assembly (`dcl_*` declarations, register ops). Confirms the
  format: Shader Model 5.0 pixel shader bytecode, standard D3D disassembly.
  This is the one known template for what "readable intermediary" looks like
  for the rest of the corpus — start matching here.
- `~/tinker/dev/wow_modding/export/shaders/` itself is a **strict subset**
  (683 of the 1023 pairs, byte-identical) of the primary dump — confirmed via
  diff, not assumed. Not copied separately; the dump already has everything
  it has, plus 340 more pairs.

## Confirmed non-findings (excluded, do not re-scan)

Real `.dxbc`/`.spv` files, confirmed unrelated to WoW by path/content:

- `/media/luna/work/cache/blender/vk-spirv-cache/`,
  `/home/luna/.cache/blender/` — Blender's own Vulkan pipeline cache (2736 +
  1407 files)
- `/home/luna/.steam/`, `/home/luna/.local/share/umu/` — Steam
  `pressure-vessel`/`steamrt` runtime compositor shaders (40 + 8 files)
- `/home/luna/tinker/CrispASR/build/ggml` — llama.cpp GPU kernel build output
  (2263 files)
- `/home/luna/dev/Vulkan/` — Sascha Willems' Vulkan examples repo (977 files,
  glsl/slang/hlsl tutorial shaders)
- `/home/luna/dev/VulkanTest`, `SFML-Oneko`, `CLionProjects/SFMLAudioViz`,
  `dev/gamescope` — toy/example Vulkan projects
- `/media/luna/data/SSD_back/VulkanDev/VulkanSDK/1.0.65.1/` — Vulkan SDK's own
  glslang test-suite fixtures
- `/media/luna/data/.../UnrealEngine/TressFXSample` — unrelated UE sample
- `/home/luna/tinker/shadPS4/externals/{vma,glslang}` — PS4 emulator's
  glslang test suite, coincidental filename overlap only
- `/home/luna/tinker/blender-git/blender/lib` — Blender source tree shaders

Skipped from the sweep entirely (reasoned, not searched):
`annas-archive-outer` (book archive), `biosflash` (firmware images),
`girlstash_media` (root-owned, unrelated personal media).

Zero matches, confirmed searched: WoW install dir itself
(`/media/luna/games/World of Warcraft`), `/media/luna/models`,
`/media/luna/userdata`.

## Loose end, not chased (out of stated scope)

`wow_shader_dump/` also contains two `.rs` files
(`809f1c585027e581.rs`, `c51c44546db71b52.rs`, 848B, `file(1)` says `data` —
binary, not actual Rust source despite the extension). Neither has a matching
`.dxbc`/`.spv` sibling hash in the dump, so they're not part of any shader
pair — left uncopied, unidentified, not investigated further since the task
scope was exactly `.dxbc`/`.spv`.

## Disassembly + matching pass (same session, continued)

`00075004bca140b6` turned out to be a distance/LOD-blend dither shader (per
Luna's memory of the earlier `wow_modding` session), not an M2 texture
combiner — correctly skipped, not chased further. Its 1-texture,
`discard_nz` + `-5.01960814e-01` (128/255) dither-threshold shape recurs
across several other captured shaders (`14a00ad81968b33d`,
`223833f2c89eb7fd`, `3ab5c621baccb06b`, `9fd79b03fddb66ab`, `d097239d9a1f32e0`,
`df949d3501ed2e7e`, and others) — a whole confirmed non-M2-combiner family,
same fade/dither system, not investigated individually.

Added `pkgs.vkd3d` to `nix/flake.nix`'s dev shell (permission granted) —
ships `vkd3d-compiler`/`vkd3d-dxbc`, the same tool the earlier session used
to produce that one `.asm` (`vkd3d-compiler -x dxbc-tpf -b d3d-asm <hash>.dxbc
-o <hash>.asm`, confirmed via `~/.bash_history`). All 1023 `.dxbc` files
disassembled into `references/wow_shaders/asm/`.

**Wrong lead, ruled out**: `cb5[173]` (a large per-model constant buffer,
present on the known LOD-blend shader) looked like a promising M2-shader
fingerprint but isn't — every shader binding it is a 167+ line lighting
uber-shader (light-loop branching, IBL, PBR), a different rendering path
entirely, not the small `s_modelPixelShaders[36]` combiner shaders the wiki
documents.

**Real fingerprint, confirmed**: actual combiner shaders are small (10-40
lines), branchless, 1-4 `dcl_resource_texture2d`, shape
`sample → mul/mad/mix chain → o0`. Sweeping the corpus for this shape found
**83 real candidates** (37 one-texture, 31 two-texture, 10 three-texture, 8
four-texture) out of 1023 total — everything else is UI, terrain, water,
skin/SSS, or other non-M2 rendering paths.

**Two confirmed exact matches against already-documented wiki formulas**
(calibration, per Luna's steer to verify against a known shape before
hunting the 17 undocumented ones blind):

- `93dd1d60a5da7d7f` = **`Combiners_Mod`** (`tex1.rgba * meshResColor.rgba`)
- `dea0d96a0e1d054e` = **`Combiners_Mod_Mod`**
  (`tex1.rgba * tex2.rgba * meshResColor.rgba`)

Both disassemble to exactly the math the wiki already documents, confirming
the "look" signature and the whole matching approach is sound.

**17-undocumented-formula hunt, so far**: the 3/4-texture bucket (where most
of the undocumented `Guild*`/`Combiners_Mod_Dual_Crossfade`/
`Combiners_Mod_Masked_Dual_Crossfade` formulas would live, since those need
3-4 textures) does not contain a clean match — every 3/4-texture candidate
found is either classic 4-way terrain lightmap splatting (flat 0.25/0.375
additive weights, no per-pixel alpha `mix`), another LOD-dither variant, or a
skin/subsurface-scattering shader. None has the alpha-driven `mix`/`movc`
shape those formulas need.

**Real caveat, not a dead end**: this capture is session-scoped — it only
contains shaders the game actually compiled while the dev-flag dump was
active, not the full 36-entry table. If the play session never rendered a
guild tabard or a dual-crossfade creature transition, those combiners simply
aren't in this corpus regardless of search depth. The factor-of-2
`Combiners_Opaque_Mod2xNA_Alpha` discrepancy (documented, TODO's step 2) and
the remaining 2-texture/1-texture undocumented formulas
(`Combiners_Opaque_Alpha`, `Combiners_Mod_Depth`, `Illum`, etc.) are still
open — the 1- and 2-texture candidate buckets (37 + 31 = 68 files) haven't
been read exhaustively yet, only spot-checked.

## 1-texture and 2-texture buckets read exhaustively — no further matches

Read all 37 one-texture and 31 two-texture candidates by hand. None matches
`Combiners_Mod_Depth`, `Illum`, or `Combiners_Opaque_Alpha`. Also ran two
broader, unfiltered greps across the *entire* 1023-file corpus (not just the
line-count-limited candidate list) specifically for the shape those three
formulas need (texture sample + `discard` + mesh-color multiply into `o0`) —
zero hits in either the 1-texture or 2-texture case. Confirmed absent, not
just unfound: this session's capture never triggered those combiners.

Everything else found in the 1-/2-texture buckets is one of: the LOD-dither
family (same `-5.01960814e-01` threshold), terrain-decal texture-atlas
wrapping shaders (`resinfo`/bilinear seam-blend logic — genuinely complex,
unrelated to M2), the flagged skin/SSS shader above, or other non-combiner
effects (fog, projected decals, luminance-based tone mapping).

**Conclusion for this pass**: 2 confirmed formula matches
(`Combiners_Mod`, `Combiners_Mod_Mod`) validate the whole approach, but this
particular capture skews heavily toward terrain/decal/LOD-fade shaders over
M2 model combiners — consistent with a play session spent mostly exploring
outdoor terrain rather than looking at guild tabards, alpha-blended
creatures, or other combiner-specific content. None of the 17 undocumented
formulas were found this pass.

## Targeted validation-case search: `Combiners_Opaque_Mod2xNA_Alpha` (factor-of-2 case)

Specifically hunted for this one, since it's the TODO's step-2 validation
target (resolving whether the wiki or `wow.export` has the right formula).
Searched the *entire* corpus (not the line-filtered candidate list) for its
structural signature — a `tex1*tex2` product immediately scaled by a literal
`2.0` — in both the 2-texture bucket (documented formula needs 2 textures)
and 3-texture bucket (`_Add` variant needs 3). Found 4 + 8 candidates by this
fingerprint; read every one in full. All are false positives from unrelated
uses of the same `2.0` constant:

- 2-texture hits: one `cb5[173]` lighting uber-shader (the `2.0`/`-1.0` pair
  is normal-map decompression, `[0,1]→[-1,1]`, not a combiner blend), three
  SDF font/glyph-atlas lookup shaders (loop over 9 candidate glyphs via an
  `immediateConstantBuffer` glyph table) — text rendering, unrelated.
- 3-texture hits: six were 500-900+ line terrain/lighting uber-shaders (too
  large to be a combiner, not read in full); the two short ones
  (`7244e298beaf7f3c`, `e3528693772b70e9`, 56/66 lines) are both water-surface
  shaders — dual normal-map sampling + Fresnel + specular, same `2.0`/`-1.0`
  normal-unpack pattern.

**Conclusion**: `Combiners_Opaque_Mod2xNA_Alpha` and
`Combiners_Opaque_Mod2xNA_Alpha_Add` are confirmed absent from this capture,
same as the rest of the 17. The factor-of-2 discrepancy (TODO's step 2)
remains unresolved by this corpus — needs either a fresh targeted capture or
the original visual-comparison approach the TODO already proposed.

## Live-capture follow-up: guild-tabard-vendor moment (`47c35a45740c769d`)

A fresh, deliberately-targeted live capture (guild vendor / tabard customization
interaction, Exodar) landed `47c35a45740c769d` — 825 lines, `cb5[173]`-bound.
First pass wrongly wrote this whole shader off as "just terrain/lighting,
unrelated" on the strength of its size and the `cb5[173]` uber-shader
fingerprint alone. That was a mistake: a shader being embedded in a large
uber-shader doesn't mean the combiner math isn't in there — it means the math
has to be traced through the surrounding control flow instead of pattern-matched
against a small standalone shader's shape. Corrected by actually tracing the
one code path that looks like texture layering (lines 70-77, inside the
`if_nz r0.y` branch):

```
r1 = sample(t0)          // base cloth
r2 = sample(t1)          // border layer
r3 = sample(t2)          // emblem layer

r2.rgb = lerp(cb0[10], r2.rgb * cb0[11], r2.a)      // border tint, blended against a base color by its own alpha
r1.rgb = r1.rgb * r2.rgb                             // base cloth × that blended border-tint
r1.rgb = lerp(r1.rgb, r3.rgb * cb0[12], r3.a)        // emblem tint layered on top by its own alpha
```

i.e. `final = lerp( t0.rgb * lerp(cb0[10], t1.rgb·cb0[11], t1.a), t2.rgb·cb0[12], t2.a )`
— a real 3-layer alpha composite with three distinct tint constants
(`cb0[10]`/`cb0[11]`/`cb0[12]`), structurally consistent with tabard
layering (background color, tinted border blended in by its own alpha,
tinted emblem layered on top by its alpha). The rest of the 825 lines
(the mirrored `else` branch, the full IBL light-probe loop, fog, 3D LUT)
is genuinely unrelated per-pixel world/character lighting, not combiner math —
that part of the original writeoff still stands, just not the whole-file
dismissal.

**Not yet resolved — two live hypotheses, don't collapse to one without more
evidence:**

1. This *is* the real `Guild`/`Guild_Opaque` blend, and the wiki's documented
   36-entry table is wrong/incomplete for it — same shape of gap as the
   already-tracked `Combiners_Opaque_Mod2xNA_Alpha` factor-of-2 discrepancy.
2. This is a separate, undocumented tabard-compositing pass sharing the same
   character/lighting uber-shader, not one of the 36 `s_modelPixelShaders`
   combiners at all — it just happens to run at the same moment the tabard
   UI is open.
3. **This is the tabard *editor*'s live-preview representation specifically**
   (Luna's addition) — the customization screen may need real-time 3-layer
   compositing so background/border/emblem stay independently swappable while
   browsing choices, while an *equipped* tabard rendered on a character in
   normal gameplay could be baked/composited down to fewer layers (2, not 3)
   once a choice is committed — closer to what the wiki's smaller documented
   formulas would actually produce. If true, this specific shader is
   UI-only and would never appear from equipped-tabard rendering in the
   world, however long that's captured for — the real `Guild`/`Guild_Opaque`
   combiner (if it's one of the 36 at all) would need capturing an
   *equipped* tabard on a character model, not the editor screen itself.

Textually this doesn't match any wiki `Combiners_*` entry as written — none of
the 36 documented formulas use three independent tint-color constants with a
per-layer alpha-driven lerp against a *color* (not just another texture). That
by itself doesn't decide between hypothesis 1 and 2.

**Methodology correction for future ubershader hits**: don't discard a large/
branchy shader as "unrelated" on fingerprint alone (line count, `cb5[173]`,
loop/branch presence). Uber-shaders can still contain real, isolable combiner-
shaped sub-expressions — trace any texture-sample-into-blend-chain segment
explicitly before ruling the whole file out.

## Pattern-search tool: `tools/shader_pattern_search/`

Built after the live-capture follow-up above, per Luna's question ("worth
looking for both [2-layer/3-layer] patterns in existing ubershaders... is
there a generalizable way to search for math structures"). Searched first
(nothing off-the-shelf does dataflow-graph shape search for RE purposes —
closest prior art is DXBC-to-HLSL decompilers, which are near-1:1 syntactic
translations, not structural search tools), then built a small extensible
one: parses `d3d-asm` into a def-use dataflow graph, matches registered
shape-patterns against it. Full design/usage: `tools/shader_pattern_search/
README.md`. Self-test validates the tool against the 3 already-hand-confirmed
cases (`Combiners_Mod`, `Combiners_Mod_Mod`, the guild-vendor 3-layer
composite) before trusting a corpus run — all 3 pass.

**Full corpus run** (1048 files, `references/wow_shaders/pattern_scan_report.json`):

- `combiners_mod_family` (1-2 samples, straight multiply, no lerp/branch): 106 hits
- `two_layer_alpha_composite` (2 samples, 1 alpha-lerp — the "equipped/baked
  tabard" candidate shape from hypothesis 3 above): 46 hits
- `three_layer_tint_composite` (3 samples, 2 chained alpha-lerps — the
  "editor-preview tabard" shape): 35 hits, 12 at "likely" confidence after
  excluding the already-documented terrain/water/skin/LOD-dither non-matches

**Spot-check, one "likely" three-layer hit read in full**: `217a11d586e92c7d`
is terrain height/snow-blending, not a tabard composite — same abstract
shape (3 samples → 2 chained lerps) as the real tabard math, different
feature entirely. Expected, not a tool bug: the pattern matches structure,
not semantics, same false-positive character as the earlier manual
mod2xNA hunt (SDF font atlases, water shaders sharing the `2.0`/`-1.0`
normal-unpack constant). Confirms the tool's recall is real (it re-found
the guild-vendor shader plus found the same *shape* elsewhere) but every
"likely" hit still needs a manual read before it's a real finding — the
tool cuts the search space down, it doesn't replace the read.

**Not yet done**: manually reading the other 11 "likely" `three_layer_tint_composite`
hits and the 46 `two_layer_alpha_composite` hits to sort real tabard/dye-
system candidates from more terrain/lighting false positives. List of the
12 "likely" three-layer hashes: `0a42be99623cb6bb`, `217a11d586e92c7d`
(confirmed false positive, terrain), `408b349456c74a56`, `4182dfdf795c3a19`,
`47c35a45740c769d` (the real guild-vendor find), `51c10ca2683b5599`,
`6712b5beb7f4b2d2`, `8f869f6da8675b2f`, `91628abdfa27f5c0`,
`aed7ffaa1096cf44`, `cbf382faa516126d`, `f4bb6372221a54b9`.

## Invariant-based matching: `decompose.py` + `invariants.py`

Per Luna's push on the false-negative risk in the textual pattern matcher
(one hardcoded lerp encoding can't recognize an algebraically-equivalent
but differently-scheduled/reordered version of the same math) and her
follow-up design ("invariant-based matching... same shape as a Bloom
filter... false positives allowed, filtered by hand"): built a two-layer
tool instead of tuning the textual matcher further.

**Layer 1** (`decompose.py`): extracts every temp-register write's exact
backward-dependency slice as a standalone "function block" (source shader,
sink register, instruction list, free-input list), deliberately *without*
using `patterns.find_lerp_idioms` or any other syntax-specific shape
detector to choose sinks -- that would just move the same blind spot one
stage earlier. Sink selection is a cheap, encoding-agnostic structural
prefilter only (touches a sample, bounded size, >1 free input). 1048
shaders -> 19,876 decomposed blocks in `references/wow_shaders/decomposed/`
(~6s). Dedup only drops exact-duplicate slices -- an earlier version also
collapsed near-duplicate subset slices, verified wrong (it silently
dropped the pure 23-instruction tint-composite block in
`47c35a45740c769d` in favor of a slightly-bigger sibling that had started
absorbing the downstream fog blend) and was reverted in favor of keeping
everything and letting Layer 2 do the real filtering.

**Layer 2** (`eval_engine.py` + `invariants.py`): a concrete interpreter
for a block's instruction slice (texture samples / cbuffer reads become
free symbolic vec4 inputs; unsupported opcodes raise rather than guess),
plus an invariant battery for the "N-layer tinted alpha composite" family:
affine-in-alpha (checked at an interior point, not the boundary) then
annihilation (at alpha=1, is the output insensitive to some other layer's
rgb entirely). A block failing either is provably not this shape; passing
both makes it a candidate, not a certainty.

**Validated against known cases before trusting the corpus run**:
`47c35a45740c769d`'s pure composite block (`sink73`) correctly reports
`t2 overrides both t0 and t1` -- exactly the hand-derived algebra from the
live-capture section above (emblem alpha fully replaces base+border).
`217a11d586e92c7d` (confirmed textual false positive) produces zero
surviving blocks -- its blend factor runs through a height/`exp` curve,
not a raw alpha, so it fails the affine test. `Combiners_Mod`/
`Combiners_Mod_Mod` don't qualify at all (their formula shape isn't an
alpha composite, correctly out of scope for this specific invariant
battery rather than a false negative).

**Full corpus result**: 1,188 surviving blocks across **193 distinct
shaders** (`references/wow_shaders/invariant_survivors.json`) -- far more
than the 12 "likely" textual hits, confirming the textual matcher really
did have real recall gaps. But this also surfaces the honest limit of the
approach: the known water shaders (`7244e298beaf7f3c`, `e3528693772b70e9`)
are now *in* the survivor list too, and correctly so -- Fresnel reflection/
refraction blending genuinely is `lerp(reflection, refraction, fresnel)`,
and at `fresnel=1` the output really is independent of the refraction
term. That's real annihilation; water just isn't the M2 combiner family
being hunted. Invariant testing fixed the false-negative problem (any
correctly-shaped alpha composite now gets found regardless of instruction
encoding) but doesn't and can't resolve the remaining semantic question
("which alpha-composite is a tabard vs. water vs. something else") --
that's still a manual read, same as every prior stage of this hunt.

**Not yet done**: reading through the 193-shader survivor list to sort
real M2-combiner-family candidates from the water/Fresnel/dye-system false
positives this battery structurally can't distinguish. A possible sharper
invariant for later, not built: check whether the alpha-driving texture's
*own rgb* participates in the output at all -- a pure alpha/mask channel
(tabard border/emblem alpha) vs. a geometry-derived Fresnel factor might
separate cleanly on that axis, but untested.

## Combiner hunt: results folder + full formula coverage

Per Luna's follow-up ("save results + runnable tests into a folder about
what we're looking for" and "verify the wiki's already-'known' formulas
too, since it's a wiki, we can't just trust it"): full results,
methodology, and the prune-via-code-vs-just-read-N-files verdict now live
in `references/wow_shaders/combiner_hunt/SUMMARY.md`. Runnable tooling
stays in `tools/shader_pattern_search/` per the project's usual code/data
split; `combiner_hunt/README.md` links both directions.

Headline (**revised after finding and fixing two real equivalence-tester
bugs** -- a near-zero-scale degenerate match and a NaN-comparison flaw,
both silently making some blocks "match" every formula at once; see
`SUMMARY.md` for the full story): the corpus splits into a Tier 1 (5
formulas, all diffuse-math-identical, ~2,700+ blocks/~352 shaders each --
provably unresolvable by more code) and a much smaller set of genuinely
verified/plausible fills after hand-tracing the actual survivors:
`Combiners_Opaque_Mod2xNA_Alpha_3s`, `_Alpha_Alpha`, and `Guild_NoBorder`
confirmed exact by hand; `_Add`, `Combiners_Opaque_Alpha`, and
`Mod_Add_Wgt` real but diffuse-ambiguous with a sibling formula; 2 formulas
(`ModNA_Alpha`, `UnshAlpha`) got genuine negative results (searched,
nothing found); `Guild`/`Guild_Opaque` itself stays at its original
1-example hand-verified state, since the automated tester still can't
catch it. Also found the same rgb-diffuse-only collapse in the wiki's
*already-documented* formulas -- 12 of ~36 total table entries are
mutually indistinguishable from each other by color math alone, and the
disputed `Mod2xNA_Alpha` factor-of-2 case now has its first real corpus
evidence (wiki version: 0 matches: wow.export version: 42 shaders). Full
breakdown, per-formula counts: see `SUMMARY.md`.

`equivalence.py` also gained `--formula-index`/`--shard` for fanning the
search out across many parallel processes (this machine has 32 cores;
the tool itself is single-threaded per invocation) -- the Guild/
Guild_Opaque search alone dropped from an open-ended serial run to ~2
minutes sharded 16 ways by source shader.

## Next step

The real lever left is a **fresh, deliberately-targeted capture**: rerun the
vkd3d-proton shader dump while intentionally visiting content likely to
exercise the missing combiners — a guild tabard (`Guild`/`Guild_NoBorder`/
`Guild_Opaque`), a dual-crossfade creature transition
(`Combiners_Mod_Dual_Crossfade`/`Combiners_Mod_Masked_Dual_Crossfade`), a
multi-texture-layer NPC/item that would plausibly hit
`Combiners_Opaque_Mod2xNA_Alpha[_Add]`, and an alpha-test creature/depth-only
render (`Combiners_Mod_Depth`/`Illum`). The existing candidate-sweep
(fingerprint: ≤40 lines, no `loop`/`if_nz`/`if_z`, 1-4
`dcl_resource_texture2d`) and the mod2x-specific structural search
(`tex1*tex2` scaled by literal `2.0` within 3 lines of the second sample) are
both ready to rerun against any new capture without redoing this session's
groundwork.
