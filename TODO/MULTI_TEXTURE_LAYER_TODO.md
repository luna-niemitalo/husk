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

## The real combiner math (not guessed — transcribed from wowdev.wiki)

`documentation/wowdev-wiki/wikitext/Pixel_shader_logic_for_mixing_colors.wiki`
gives the exact GLSL-equivalent formula for every named `Combiners_*`
fragment shader WoW's client uses, e.g.:

```
Combiners_Opaque_Mod2x:   finalColor.rgb = tex1.rgb * meshResColor.rgb * tex2.rgb * 2.0;
Combiners_Opaque_Add:     finalColor.rgb = tex2.rgb + tex1.rgb * meshResColor.rgb;
Combiners_Mod_Mod:        finalColor.rgba = tex1.rgba * tex2.rgba * meshResColor.rgba;
```
(full table: ~20 named formulas, that file has all of them). `tex1`/`tex2`
are the two layers' own sampled colors; `meshResColor` is the batch's
static tint/fade (already resolved by husk as `baseColorFactor` /
`tint_animation`/`fade_animation` extras). Each is a small, direct Blender
shader-node recipe (`Mix Color` in Multiply/Add mode, a `Vector Math`
node for the `* 2.0`, etc.) — nothing here needs guessing.

**Which formula applies to a given batch is a separate, real algorithm**
(blend mode + op count + per-layer env-map flag → a `Combiners_*` name) —
`reference/wowser/src/lib/pipeline/m2/batch-manager.js` (also transcribed
at `documentation/wowdev-wiki/wikitext/M2/.skin/WotLK_shader_selection.wiki`)
has a full, real reverse-engineered implementation. **Same standing
discipline as every `reference/` source this project already uses (`wow.export`,
WoWDBDefs) applies here, not a special exception for this one**: nothing
under `reference/` is ground truth on its own, ever — it's read for a
starting hypothesis, then checked against real bytes before being trusted,
exactly what `WIKI_FINDINGS_HISTORY.md` already documents for every
formula/offset in this project. Worth calling out explicitly for this
particular source anyway: it's the Wowser project's own WotLK-era
(~2008-2010, roughly a decade and a half old) reverse engineering, so the
gap between "what this describes" and "what a modern (Legion+/retail) M2
actually does" may be larger than usual. Treat it as a strong starting
hypothesis for the shader-selection algorithm's *shape*, not a verified
fact, until it's actually checked against real current corpus
batches (pick a handful of real `textureCount > 1` files across different
blend modes, hand-derive the expected `Combiners_*` name per this
algorithm, and cross-check against how the material actually looks
in-game or against another independent tool's own shader-selection
output) — same discipline as every other real-data cross-check this
project's `WIKI_FINDINGS_HISTORY.md` already documents. `reference/wowser`
is cloned locally now (`git clone --depth 1
https://github.com/wowserhq/wowser`, gitignored, same "dev-only reference
checkout, never a runtime dependency" tier as `reference/wow.export`/
`reference/WoWDBDefs`) specifically so this validation pass has the real
source to check against without re-fetching it.

## The env-map ("shiny metal") special case — explicitly not PBR

One real wrinkle Luna asked about directly: WoW has a look for shiny/
metallic surfaces, and it is **not** 1:1 with glTF/Blender's Principled
BSDF metallic-roughness model. `M2COMBINER_ENVMAP` (index 8,
`M2/Rendering.wiki`) and the shader-selection code's "env mapped" flag
(driven by `textureCoordCombos[...] == -1` for that texture unit — the
same field `WIKI_FINDINGS/M2/skin.md` already found real-but-rare in the
local corpus, only 3 of 130,576 `.m2` files) describe a classic
**environment-mapped ("MatCap"-style) reflection**, not a physically-based
one:

- Principled BSDF's metallic/roughness reflection is computed from real
  scene lighting, the surface normal, and a roughness value, via a
  microfacet BRDF — it reacts correctly to any camera angle or lighting
  setup because it's a real physical model.
- WoW's env-mapped layer is a **flat 2D texture sampled by UV coordinates
  generated from the view-space reflection vector**, not the mesh's
  authored UVs at all (`Diffuse_Env` vertex shader name in the
  shader-selection code) — the same fixed, precomputed-lookup technique
  real-time engines have used since before physically-based shading
  existed. It looks "shiny" but is baked, angle-dependent in a specific
  fixed way, and has no relationship to actual scene lighting.

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
**Not started** — given the real corpus rarity found so far (3/130,576),
this is lower priority than the ordinary two-UV-layer combiner case below;
worth confirming that rarity holds on a broader/different corpus slice
before investing here.

## Current husk state (what's already exported vs. what's missing)

- `gltf::Material::AlternateTextureCandidate`/`AdditionalTextureLayer`
  (`gltf_mesh.hpp`) already exports each extra layer's `fileDataId`/
  `texCoord`/`imagePng` as `additional_texture_layers` extras.
  `blend_mode` is already on the primary material (extras, `blendMode >
  2`). Both are real, already-shipped data this plan builds on, not new
  parsing work.
- **Real gap found while researching this**: `AdditionalTextureLayer` has
  no per-layer env-map flag — `baseColorTexCoord`'s own doc comment says
  envmap (`textureCoordCombos` value `-1`) "isn't attempted... callers
  fall back to 0," silently treating an env-mapped layer as an ordinary
  UV-0 layer. Small, cheap fix: thread the real `-1` case through as an
  `env_mapped: true` boolean on the relevant layer's extras (mirrors how
  `billboardMode`/`texture_type` already tag "this needs special
  handling" rather than guessing) — needed before the env-map Blender
  recipe above can actually be driven by real per-file data instead of
  assumed.
- No combiner-formula field exists yet at all — the Blender-side plan
  needs to derive it itself (blend_mode + op count + env-map flag, once
  exported, feeding the shader-selection algorithm above), not read one
  directly off the `.glb`.

## Implementation plan (not started)

1. Husk-side (small): add the `env_mapped` per-layer boolean above.
2. Validation pass (do this **before** trusting the shader-selection
   algorithm for real output): cross-check `reference/wowser`'s
   `batch-manager.js` logic against a handful of real modern
   `textureCount > 1` corpus files, per the caveat above.
3. Blender-side: extend `render_glb.py`'s post-import material rebuild
   (same site as `fix_additive_materials`) to, for each material with
   `additional_texture_layers` extras: derive the `Combiners_*` formula
   name (step 2's validated algorithm), build the matching small node
   recipe (Mix/Multiply/Add nodes per the formula table above) feeding
   the existing Base Color chain, and for an `env_mapped` layer specifically,
   wire the reflection-vector UV recipe instead of a normal `UV Map` node.
4. Verify against a handful of real corpus files spanning different
   `Combiners_*` formulas, not just one — this is exactly the kind of
   thing that looks right on one lucky test case and wrong on the next
   (same lesson the additive-blend fix's own `unlit`-co-occurrence bug
   already taught this project once).
