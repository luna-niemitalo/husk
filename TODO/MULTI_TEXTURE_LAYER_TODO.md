# TODO: multi-texture-layer combiner rendering (Blender-side)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Background

`M2_COMPLETENESS.md`'s own row: `textureCount > 1` batches (a 2nd, and
rarely 3rd+, texture layer combined with the first via WoW's fixed-
function combiner math) are `extras-capped, permanent` at the *husk*
level — core glTF has no slot for arbitrary multi-texture blend formulas,
so this is correctly marked as a wall from `export_materials.cpp`'s own
side. That's real, but it's not the end of the story: the same shape of
wall applied to additive-blend (`blend_mode` 3-7) materials too, and this
session's own work (`render_glb.py`'s `fix_additive_materials`) proved the
fix isn't more husk parsing — it's a **companion Blender script consuming
the extras husk already exports correctly** to rebuild real shading
post-import. That's the same fix shape this file proposes for
`textureCount > 1` batches — the biggest concrete "husk-adjacent" lever
left that isn't blocked by external data or a genuine glTF wall.

Real, common case: 226,294 of 287,005 real `.skin` files (~79% of the
corpus) have `textureCount > 1` (`WIKI_FINDINGS/M2/skin.md`, "Multi-
texture-layer arithmetic" section — the index arithmetic itself is
already verified byte-exact against real data). Visually, when missing,
this reads as "flat plastic" armor/weapons that should have a detail map,
tint overlay, or metallic-looking shine layer.

## Major correction this session: the plan was aimed at the wrong mechanism

The previous version of this file staked the entire plan on `reference/
wowser`'s `batch-manager.js` "hav[ing] a full, real reverse-engineered
implementation" of shader selection, describing it as a strong-but-
unverified hypothesis that just needed a real-corpus cross-check. Both
halves of that turned out to be wrong once actually read:

1. **`reference/wowser` was never actually cloned** despite the file
   claiming it was ("cloned locally now... specifically so this
   validation pass has the real source to check against") — confirmed via
   `.gitignore`/`git log`, no trace of it anywhere. Cloned fresh this
   session (`git clone --depth 1 https://github.com/wowserhq/wowser
   reference/wowser`, same gitignored dev-reference tier as `reference/
   wow.export`/`reference/WoWDBDefs`).
2. **The current `batch-manager.js` doesn't contain the shader-selection
   algorithm at all** — it only resolves texture/UV-animation *indices*
   (`resolveTextureIndices`/`resolveUVAnimationIndices`). The actual
   vertex/fragment shader *mode* selection lives in a different file,
   `src/lib/pipeline/m2/material/index.js`, and it is an explicit,
   self-labeled stub: `vertexShaderModeFromID`/`fragmentShaderModeFromID`
   both carry a `// TODO: Fully expand these lookups` comment and only
   handle two cases — `opCount === 1` (mode 0) and `shaderID === 0` with
   `opCount > 1` (mode 1) — returning `-1` ("unhandled, sample texture 0
   raw") for everything else. The matching `shader.frag` confirms this:
   `fragCombinersWrath1Pass`/`fragCombinersWrath2Pass` are the *only* two
   real combiner formulas implemented in wowser's actual runtime shader,
   both simplified generic approximations (texture × vertex color × 2,
   optionally multiplying in a second texture's alpha) — none of the ~20
   named `Combiners_*` formulas from `Pixel_shader_logic_for_mixing_
   colors.wiki` are implemented per-formula in wowser at all. wowser is a
   real, working WotLK-era renderer that visibly gets away with this
   because its two generic formulas look passable for common cases, not
   because it solved the selection problem.

   The **full** algorithm — the one actually pasted into wowdev.wiki's
   `WotLK_shader_selection` page and cited by the old version of this file
   — is a historical, older snapshot of wowser's `BatchManager` that the
   live GitHub repo no longer contains (not in current history, not in a
   tag `git log --all` can find). It's still a real, useful reference (see
   below), just not literally "the wowser project's current code."

3. **More importantly: none of this is the mechanism that applies to
   modern (Cata+, and therefore Legion+) M2 files at all.** wowdev.wiki's
   main `M2/.skin.wiki` page states this directly, in the `shader_id in
   WotLK` section: "this entire section only applies to selecting
   appropriate shaders for WotLK... it definitely stops applying from Cata
   and on." The WotLK-era mechanism (`shader_id` mostly `0` on disk, real
   value computed at runtime from blend mode + op count + texture mapping
   + transparency animation, exactly what `batch-manager.js`/
   `WotLK_shader_selection.wiki` describe) is real for that one era and
   nothing else husk needs to care about — `husk`'s M2 corpus is
   Legion+/retail (`DESIGN.md`'s Goal section), which is squarely on the
   other side of that "stops applying" line.

## The mechanism that actually applies (Cata+, decompiled, on wowdev.wiki)

Same `M2/.skin.wiki` page (`===shader_id and textureCount===` section
onward) gives the real, decompiled Cata-and-later client functions
`M2GetPixelShaderID`/`M2GetVertexShaderID` (from Wow.exe build 12340, "5.0.1.15464" era per the page's own note — i.e. genuinely
post-WotLK, matching husk's own Legion+ scope) plus the real enum/name
tables they index into:

- `s_modelPixelShaders[36]` / `s_modelVertexShaders[16]` (`8.0.1`
  revision has 19) — the real string names (`"Combiners_Opaque_Mod2x"`,
  `"Diffuse_T1_Env"`, ...), matching (a superset of) the formulas already
  transcribed in `Pixel_shader_logic_for_mixing_colors.wiki`.
- `s_modelShaderEffect[NUM_M2SHADERS]` — a fixed table (30 entries in the
  pre-8.0.1 listing, 34 in the 8.0.1 one) of `{pixel, vertex, hull,
  domain}` shader-name indices, used whenever `shader_id & 0x8000` is set.
- The two functions themselves are short and exact, not heuristic:

  ```c
  unsigned int M2GetPixelShaderID(unsigned int op_count, unsigned short shader_id) {
    if (shader_id & 0x8000) {
      // table lookup
      return s_modelShaderEffect[shader_id & ~0x8000].pixel;
    } else if (op_count == 1) {
      return shader_id & 0x70 ? PS_Combiners_Mod : PS_Combiners_Opaque;
    } else {
      unsigned int lower = shader_id & 7;
      return (shader_id & 0x70)
        ? (lower==0 ? PS_Combiners_Mod_Opaque : lower==3 ? PS_Combiners_Mod_Add
           : lower==4 ? PS_Combiners_Mod_Mod2x : lower==6 ? PS_Combiners_Mod_Mod2xNA
           : lower==7 ? PS_Combiners_Mod_AddNA : PS_Combiners_Mod_Mod)
        : (lower==0 ? PS_Combiners_Opaque_Opaque : lower==3 ? PS_Combiners_Opaque_AddAlpha
           : lower==4 ? PS_Combiners_Opaque_Mod2x : lower==6 ? PS_Combiners_Opaque_Mod2xNA
           : lower==7 ? PS_Combiners_Opaque_AddAlpha : PS_Combiners_Opaque_Mod);
    }
  }
  ```
  (`M2GetVertexShaderID` is the same shape, see the wiki page directly —
  this file won't re-transcribe the whole thing to avoid a second,
  driftable copy.)

This is **not a derivation from blend mode/op count/texture-mapping the
way WotLK needed** — for Cata+, `shader_id` is (mostly) a real, meaningful
on-disk value the client reads close to directly. The op-count-based
formula branch above only kicks in for the low 15 bits when the table-
lookup high bit isn't set; it still needs `op_count` (== `M2Batch::
textureCount`, already parsed by husk) but nothing else heuristic.

### Real gap found in husk while checking this: `shader_id` isn't even parsed

`src/skin.cpp`'s `parseBatches` reads `flags`/`skinSectionIndex`/
`colorIndex`/`materialIndex`/`textureCount`/`textureComboIndex`/... from
each real on-disk `M2Batch` (0x18 bytes) but **skips `shader_id` entirely**
— it lives at offset 0x02 (right after `flags`/`priorityPlane`), and
`src/skin.cpp:176-178`'s own comment says so explicitly: "the rest
(priorityPlane/shader_id/geosetIndex/materialLayer) is skipped." `skin.hpp`'s
`Batch` struct has no field for it at all. This is the single field that
makes the whole "which formula does this batch use" question tractable
for modern content without guessing — and husk currently discards it on
every parse. Small, concrete, high-value fix; see Implementation plan
below.

### Real-corpus validation (this session, not guessed)

Wrote a standalone parser (scratchpad-only, not committed — same tier as
this project's other one-off corpus probes) reading `M2Batch::shader_id`
(u16 @ offset 0x02) and `textureCount` (u16 @ offset 0x0E) directly, run
first against a 400-file random sample, then against the **full local
corpus, all 287,005 `.skin` files** (same file count `WIKI_FINDINGS/
M2/skin.md` already cites for the `textureCount > 1` stat, confirming
this is the same corpus, not a different one):

- 400-file sample, 1,402 total batches: **40.2% have the `0x8000` high bit
  set** (the table-lookup path) — real and common, not a rare edge case.
  `textureCount` distribution: 2 (68%), 1 (29%), 3 (3%) — no 4-texture
  batches in this sample. Of 997 `textureCount > 1` batches, **996 had a
  non-zero `shader_id`** (only 1 was exactly `0`) — confirming `shader_id`
  is reliably populated on real modern multi-texture batches, not a dead
  legacy field husk could safely keep ignoring.
- Most common non-table `shader_id` values seen: `0x10` (16, single-
  texture `Diffuse_T1`/`Combiners_Mod`), `0x4014` (16404), `0x4011`
  (16401), `0x4016` (16406) — all decode cleanly under the formula above
  once `op_count` is known, nothing pathological.
- [Full-corpus run was in progress when this entry was written — see the
  session's own follow-up commit/entry for the exact 287,005-file numbers
  if this note is still here; the 400-sample figures above are the
  verified floor, not a placeholder guess.]

**What this validation does *not* yet cover**: it confirms `shader_id` is
present, populated, and decodable — it does *not* yet confirm that the
*specific* `Combiners_*`/`Diffuse_*` name each value resolves to matches
real in-game/another-tool output. That's the next real validation step
(see Implementation plan step 2) — same "strong hypothesis, not yet
independently cross-checked" caveat the old version of this file already
knew to apply, just now aimed at the right algorithm.

## The real combiner math (unaffected by the correction above)

`documentation/wowdev-wiki/wikitext/Pixel_shader_logic_for_mixing_colors.wiki`
still gives the exact GLSL-equivalent formula for every named `Combiners_*`
fragment shader, keyed by the same names the `s_modelPixelShaders` table
above uses — this part of the old plan was already correct and needs no
revision, just a note that ~7 of the ~36 named pixel shaders in the newer
`s_modelPixelShaders[36]` table (`Combiners_Opaque_Mod2xNA_Alpha_Add`,
`_3s`, `_UnshAlpha`, `_Alpha_Alpha`, `Mod_Dual_Crossfade`,
`Mod_Masked_Dual_Crossfade`, `Mod_Mod_Mod_Const`, ...) have **no formula
documented on the wiki page at all** (empty `===Name===` sections, e.g.
`Combiners_Opaque_Mod2xNA_Alpha_3s`) — a real, honest gap in wowdev.wiki
itself, not something this project can fill in by guessing. `tex1`/`tex2`
are the two layers' own sampled colors; `meshResColor` is the batch's
static tint/fade (already resolved by husk as `baseColorFactor` /
`tint_animation`/`fade_animation` extras). Each *documented* formula is a
small, direct Blender shader-node recipe (`Mix Color` in Multiply/Add
mode, a `Vector Math` node for the `* 2.0`, etc.) — nothing here needs
guessing, for the formulas that exist.

## The env-map ("shiny metal") special case — also needs re-examination

The old version of this file's rarity claim ("only 3 of 130,576 `.m2`
files") was about a *different* signal — `M2Batch::textureCoordComboIndex
== -1` (env-mapping declared via the texture-mapping lookup table). The
newly-found on-disk mechanism has its **own, separate** env-map signal:
`VS_Diffuse_Env`/`VS_Diffuse_T1_Env`/`VS_Diffuse_Env_T1`/`VS_Diffuse_Env_
Env`/etc. are real, distinct entries in `s_modelVertexShaders`, reachable
either via the table-lookup path (`s_modelShaderEffect[...].vertex`,
several of the 30 real table rows resolve to an `_Env`-bearing vertex
shader — e.g. row 0 is `VS_Diffuse_T1_Env`) or via the low-15-bit formula
path (`shader_id & 0x80` for single-op, a similar bit for multi-op —
see `M2GetVertexShaderID` on the same wiki page).

**This means the real env-map frequency is very likely much higher than
"3 files"** — the `0x8000` table-lookup path alone was already measured
at 40.2% of real batches this session, and several of its 30 real rows
route to an `_Env` vertex shader. **Not yet measured directly** — the
scan this session only recorded `shader_id`/`textureCount`, not the fully
resolved shader *name*, so the real env-map rate is still an open,
concrete follow-up question (Implementation plan step 0, new). The old
Luna Note below (env-mapping now reacts to real scene lighting in modern
retail, not just a static baked lookup) still stands as the open design
question for *how* to render it once frequency is known — just flagging
that "rare, low priority" is very likely no longer the right framing once
that number comes back.

### Luna Note (kept, still accurate as a design note):
> It looks "shiny" but is baked, angle-dependent in a specific fixed way, and has no relationship to actual scene lighting.

This has updated since, as now it *does* react to the real scene lighting, actual concrete in game proof that the shader graph has massively advanced since WotLK

---

**Concrete Blender node recipe for this** (standard, well-known technique
for exactly this kind of matcap shading — not a novel invention needed
here): `Geometry` node's `Incoming` vector → `Vector Math` `Reflect`
against the shading normal → `Vector Transform` (World/Object space →
Camera space) → remap the resulting X/Y to 0..1 (`uv = view_reflect.xy *
0.5 + 0.5`, a `Vector Math` Multiply+Add pair) → feed directly into the
env-map texture node's `Vector` input, bypassing the `UV Map` node
entirely. This reproduces the reflection-vector lookup Blender's own
viewport "MatCap" solid-shading mode already does internally, just built
explicitly in the Shader Editor so it survives into an actual render.
The exact sphere-mapping formula is also on `M2/.skin.wiki` itself
(`===Environment mapping===`): `normPos = -normalize(vertex); temp =
normPos - normal * 2.0*dot(normPos, normal); temp.z += 1.0; texCoord =
normalize(temp).xy * 0.5 + 0.5` (vertex/normal in camera space) — matches
the node recipe above, real client formula, not reconstructed by
inference. **Not started** — priority now genuinely unknown pending the
real frequency measurement above (was "lower priority" under the old,
wrong 3/130,576 number; do not keep deferring this on stale evidence).

## Current husk state (what's already exported vs. what's missing)

- `gltf::Material::AlternateTextureCandidate`/`AdditionalTextureLayer`
  (`gltf_mesh.hpp`) already exports each extra layer's `fileDataId`/
  `texCoord`/`imagePng` as `additional_texture_layers` extras.
  `blend_mode` is already on the primary material (extras, `blendMode >
  2`). Both are real, already-shipped data this plan builds on, not new
  parsing work.
- **`shader_id` (`M2Batch` offset 0x02) is parsed nowhere in husk** — see
  "Real gap found" above. This is now the single most important missing
  piece, more so than the previously-flagged `env_mapped` per-layer flag
  below (which only matters *after* `shader_id` resolution tells you a
  given op is env-mapped).
- `AdditionalTextureLayer` has no per-layer env-map flag — see the env-map
  section above for why this is now open-frequency, not confirmed-rare.
  Small, cheap fix once `shader_id` resolution exists: thread the
  per-op `_Env` vertex-shader result through as an `env_mapped: true`
  boolean on the relevant layer's extras (mirrors how `billboardMode`/
  `texture_type` already tag "this needs special handling" rather than
  guessing).
- No combiner-formula field exists yet at all. Given `shader_id` is a
  real, mostly-direct on-disk value for Legion+ content (not a derived
  heuristic), the right place to resolve it is arguably **husk itself**,
  not just the Blender-side script — a `shader_names` (or `combiner_
  pixel`/`combiner_vertex`) string-pair extras field, resolved once in
  C++ from real on-disk `shader_id`/`textureCount`, would be strictly more
  reliable than re-deriving it Python-side from raw extras every time, and
  matches this project's existing pattern of resolving raw M2 data into
  named, human-readable extras (`texture_type`, `blend_mode`,
  `billboardMode`) rather than punting resolution downstream. This is a
  genuine open design question this session didn't settle — see
  Implementation plan.

## Implementation plan (revised this session, nothing implemented yet)

0. **New, first**: extend this session's corpus scan (or redo it small-
   scope) to resolve full `shader_id`/`op_count` → `{pixel, vertex}` shader
   *names* (both the table-lookup and formula paths), and tally how many
   real batches resolve to an `_Env`-bearing vertex shader. This answers
   the open env-map-frequency question above with real numbers instead of
   the stale 3/130,576 figure, and should happen before committing to the
   env-map recipe's priority either way.
1. **Husk-side (small, now the clear first real step)**: add a `shader_id`
   field to `skin.hpp`'s `Batch` struct, read it in `src/skin.cpp`'s
   `parseBatches` (offset 0x02, u16 — the exact offset this session
   confirmed against the real `M2Batch` layout and cross-checked by
   parsing real files directly). No behavior change beyond parsing it;
   downstream consumption is step 2+.
2. **Husk-side**: implement `M2GetPixelShaderID`/`M2GetVertexShaderID`
   (both branches: `0x8000` table lookup against a transcribed
   `s_modelShaderEffect`/`s_modelPixelShaders`/`s_modelVertexShaders`, and
   the low-bits formula for the non-table case) as a pure function over
   `(shader_id, op_count)`, transcribed directly from `M2/.skin.wiki`
   (cite the exact wiki section in the doc comment, same discipline as
   every other formula this project has transcribed). Export the
   resolved `{pixel, vertex}` name pair as new material extras (exact key
   names TBD — `shader_names`, or split `combiner_pixel`/`combiner_
   vertex`). This is genuinely new parsing/export work in `src/`, not
   Blender-side.
3. **Validation pass** (do this **before** trusting step 2's resolved
   names as ground truth for step 4): cross-check a handful of real
   modern `textureCount > 1` corpus files' resolved shader names against
   either real in-game visual behavior or another independent tool's own
   shader-selection output — the wiki transcription is a strong
   hypothesis (decompiled client code, not a guess), but per this
   project's own standing discipline nothing under `reference/`/
   `documentation/` is trusted without an independent real-data check.
4. **Blender-side**: extend `render_glb.py`'s post-import material rebuild
   (same site as `fix_additive_materials`) to, for each material with
   `additional_texture_layers` extras: read the resolved `Combiners_*`
   name from step 2's extras (no re-derivation needed Blender-side if
   husk already resolved it), build the matching small node recipe
   (Mix/Multiply/Add nodes per the formula table) feeding the existing
   Base Color chain, and for an env-mapped vertex-shader result
   specifically, wire the reflection-vector UV recipe instead of a normal
   `UV Map` node.
5. Verify against a handful of real corpus files spanning different
   `Combiners_*` formulas, not just one — this is exactly the kind of
   thing that looks right on one lucky test case and wrong on the next
   (same lesson the additive-blend fix's own `unlit`-co-occurrence bug
   already taught this project once).
