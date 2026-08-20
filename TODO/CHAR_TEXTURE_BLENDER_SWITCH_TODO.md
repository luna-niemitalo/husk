# TODO: Blender-side character-texture customization switch (Stage 5)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file. This file is meant to be **fully self-contained**
— read it cold, with no other session context, and it should be enough to
implement the whole thing.

## Background: why this exists, and what already happened

`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md` tracks real DB2-driven character
texture compositing (base skin color, tattoos, scars, etc. — several
small rectangular texture patches, each belonging to one real
customization choice, layered onto a shared base atlas at specific
positions with specific blend modes). Its Stages 1-3 are done and stay in
husk (`husk export`, real DB2 parsing + data exposure — see "What husk
already exposes" below). Stage 4 was a real, working, *software* pixel
compositor built directly in husk (`src/char_composite.hpp`/`.cpp`) —
implemented, verified end to end, committed, then **deliberately reverted
the same session** after Luna's own direct pushback, for two independent
reasons, not just "the user said so":

1. Every other DB2-derived feature in this codebase attaches real resolved
   data and stops there (`chr_texture_layout`, `enabled_geosets`, bone
   corrections, physics bodies) — husk resolves, a human/Blender script
   applies. Stage 4 was the one exception: it *interpreted* the data
   (actual pixel blending) even though the result never got wired into
   any material slot, quietly crossing from data exposure into rendering.
2. It's also the wrong *layer* for the actual goal below: Blender's own
   Mix Color node already implements Multiply/Overlay/Screen blend modes
   natively, so hand-transcribing that math in C++ wasn't even buying
   anything. More importantly, **live shader compositing lets a user
   switch skin color, tattoo, and face marking independently in real
   time** — something a husk-precomputed static composited image
   fundamentally cannot do without generating one image per full
   cross-product combination of every option's every choice (combinatorial
   blowup: even 3 independent 10-choice options is already 1000 images).

Full narrative of the build-then-revert: `CLAUDE_HISTORY.md`'s two
`2026-08-20` entries titled "(character-texture compositing, Stages 3
material chain + 4 pixel compositing)" and "(character-texture
compositing, Stage 4 revert)", in that order (most-recent-first file, so
the revert entry appears *above* the build entry).

**This file is the actual next step**: build the Blender-side tooling that
does what Stage 4 was trying to do, but correctly — live, in the shader,
switchable.

## What husk already exposes (read this before writing any Blender code)

Two real, already-shipped `husk export` extras, both on the glTF **skin**
object (`skins[0].extras`, not on any node/mesh/material — see "A skin
extras gotcha" below):

### `chr_texture_layout` (`husk export --db2-dir/--dbd-dir/--char-layout-id <id>`)

```json
{
  "layout_id": 42,
  "width": 2048, "height": 1024,
  "materials": [
    {"id": 1, "texture_type": 1, "width": 1024, "height": 512, "flags": 0}
  ],
  "sections": [
    {"id": 1, "section_type": 0, "x": 0, "y": 0, "width": 256, "height": 256,
     "overlap_section_mask": 0}
  ],
  "texture_layers": [
    {"id": 1, "texture_type": 1, "layer": 0, "flags": 0, "blend_mode": 1,
     "texture_section_type_bit_mask": 1, "chr_model_texture_target_id": 7}
  ]
}
```

- `materials[]` — one real `ChrModelMaterial` row per real `texture_type`
  (WoW's texture-slot enum — **the same numbering space as a real
  material's own `texture_type` extras field**, see "The TextureType ↔
  M2Texture::type join is already assumed elsewhere" below): the base
  atlas size for that slot.
- `sections[]` — real `CharComponentTextureSection` rows: a placement
  rectangle within the atlas, `x`/`y`/`width`/`height` in atlas pixel
  space (top-down, WoW convention — Blender UV V grows upward, needs a
  flip, see `apply_texture_layout_overlay`'s own V-flip comment in
  `tools/husk_blender_geoset_mask.py` for the exact convention already
  established and used).
- `texture_layers[]` — real `ChrModelTextureLayer` rows: `blend_mode` is
  the real WoW blend-mode enum (see "Real blend modes" below).
  `texture_section_type_bit_mask` is a bitmask; the section this layer
  targets is whichever `sections[]` entry has
  `1 << section.section_type` set in this bitmask (see
  `src/cmd_export.cpp`'s now-deleted `attachCompositedTextures` — git
  history, commit before the revert — for the exact reference
  implementation of this join, `(mask & (1u << sectionType)) != 0`).
  `chr_model_texture_target_id` is the real join key against
  `chr_enabled_materials` below (**not** `texture_type` — a real, confirmed
  Blizzard naming quirk, see `src/chrmodel_db2.hpp`'s
  `ChrModelTextureLayer::chrModelTextureTargetId` doc comment).

### `chr_enabled_materials` (`husk export --customization-choice-ids <id,...>` or `--chr-model-id <id|auto>`)

```json
[
  {"choice_id": 99, "chr_model_texture_target_id": 7,
   "material_resources_id": 777, "file_data_id": 888}
]
```

One entry per real `ChrCustomizationChoiceID` that resolved a material
(via `ChrCustomizationElement.ChrCustomizationMaterialID` →
`ChrCustomizationMaterial` → `TextureFileData.db2`, see
`src/chrcustomization_db2.hpp`/`src/texturefiledata_db2.hpp`).
`file_data_id` is `0` when `TextureFileData.db2` couldn't resolve it
(real, reported, not fabricated — check for 0 before trying to load a
texture for it).

**The join**: for a resolved `chr_enabled_materials` entry, find the
`texture_layers[]` entry with the same `chr_model_texture_target_id`, then
find the `sections[]` entry its `texture_section_type_bit_mask` selects,
then find the `materials[]` entry with the same `texture_type` as that
`texture_layers[]` entry (for the base atlas size). That gives you: which
`FileDataID`'s texture, placed at which rect, blended how, into which
atlas size.

### `chr_customization_options` — the full real menu (done, see Step 1)

```json
[
  {"option_id": 1, "option_name": "Ears", "option_order_index": 0,
   "choices": [
     {"choice_id": 11, "choice_name": "Short Fin", "choice_order_index": 0,
      "geoset_id": 205,
      "materials": [{"chr_model_texture_target_id": 7, "material_resources_id": 700,
                      "file_data_id": 800}]}
   ]}
]
```

Every real `(Option, Choice)` pair for the model — not just the choice(s)
that ended up in `chr_enabled_materials`/`enabled_geosets` above.
`geoset_id` is omitted when a choice has no geoset element; `materials` is
an empty array when a choice has no material element (real and common —
most choices carry only one of geoset/material/boneset, per
`ChrCustomizationElement`'s own documented exclusivity).

**Attached automatically** whenever a real `ChrModelID` can be determined
at all. `--chr-model-id` follows the standard `auto`|`none`|`<id>`
three-state convention this project already uses for `--textures`/
`--skin-dir`/`--skel`, except **unset already means `auto`** — given only
`--db2-dir`/`--dbd-dir` (no `--chr-model-id`, no `--customization-choice-ids`
at all), husk still attempts real derivation and attaches the full menu.
An explicit `--customization-choice-ids` alone also triggers the same
best-effort attempt purely for this extras array. `--chr-model-id none`
is the real opt-out (no separate flag needed to get it, one exists to
turn it *off*). This was Luna's own explicit instruction, phrased
directly: it shouldn't need "the user uttering the magic words"
(`src/cmd_export.cpp`'s `attachCustomizationChoices`/`tryDeriveChrModelId`,
`gltf::Skeleton::CustomizationOption`/`CustomizationChoice` in
`gltf_skeleton.hpp`). A Blender script never
needs a second export run just to see what's selectable — this extras
array is already there on any real character export that has DB2 access
and a derivable identity.

## Real blend modes (already know these — reuse, don't re-derive)

From the reverted Stage 4's own transcription of
`reference/wow.export/src/shaders/char.fragment.shader` +
`CharMaterialRenderer.js` (git history has the exact reverted
`src/char_composite.hpp`'s module doc comment, or re-derive directly from
those two files — both are still in the repo, untouched):

| `blend_mode` | Real meaning | Blender native equivalent |
|---|---|---|
| 0, 1 | None / Blit — straight overwrite, no blending | Mix Color node, factor 1.0, or just Image Texture straight into the chain (no compositing math) |
| 4 | Multiply — `base * blend`, full RGBA including alpha | Mix Color node, **Multiply** mode |
| 6 | Overlay | Mix Color node, **Overlay** mode |
| 7 | Screen | Mix Color node, **Screen** mode |
| 9 | Alpha Straight — standard alpha-over, real alpha-channel accumulation differs subtly from 15 (not visually distinguishable in a static preview; treat same as 15 unless proven otherwise) | Mix Color node, default **Mix** mode, factor = source alpha |
| 15 | Infer alpha blend — standard alpha-over | Mix Color node, default **Mix** mode, factor = source alpha |
| anything else (2,3,5,8,10-14,16+) | Not used in character customization per `CharMaterialRenderer.js`'s own comment, or genuinely unknown | Don't guess — flag it (print a warning, use plain Mix as a fallback) and move on, same discipline the reverted compositor used |

Blender's Mix Color node (`ShaderNodeMixRGB` in older Blender / the
`Mix` node with `data_type='RGBA'` in 4.x+) has `blend_type` values
`'MULTIPLY'`, `'OVERLAY'`, `'SCREEN'`, `'MIX'` directly — **verify the
exact enum identifiers against the actual Blender version in this repo's
flake** (`bpy.types.ShaderNodeMixRGB.bl_rna.properties['blend_type'].enum_items`
from Blender's own Python console, or just try one and read the resulting
`.blend_type` value back) rather than assuming from memory; Blender's node
API has changed shape across major versions (`ShaderNodeMixRGB` vs. the
newer unified `ShaderNodeMix`) and this repo's own flake pins one specific
Blender version (`nix/flake.nix`, currently 5.1.1 per this session's own
shell banner — check it's still that when this work starts).

## The TextureType ↔ M2Texture::type join is already assumed elsewhere

A real concern raised earlier in this same session was "how does a
composited atlas map back to the primitive(s) that should render it —
`ChrModelMaterial::TextureType` vs. `M2Texture::type` looked like two
different numbering spaces that were never confirmed to match." **They
already are, implicitly, and it already works**:
`src/gltf_mesh.cpp:322-323` writes a real material's own
`materialExtras["texture_type"]` directly from `Material::textureType`
(sourced from `M2Texture::type`), and
`tools/husk_blender_geoset_mask.py`'s existing
`apply_texture_layout_overlay` (`_build_section_overlay_group` +
the function itself, lines ~826-969) already matches Blender materials
against `chr_texture_layout`'s own `materials[].texture_type` via
`mat.get("texture_type") in concerned_types` — the exact same numbering
space, already treated as equivalent, already shipping, already the
mechanism used to show real texture-layout debug overlays on the right
materials. **Reuse this matching directly** — don't re-derive or
re-litigate whether the join is valid, a real feature already depends on
it working.

## Implementation plan

### Step 0 — re-familiarize with the reference implementation, then delete it from memory

Read `git show <revert-commit>^:src/char_composite.cpp` (the compositor's
own commit, one before the revert commit — `git log --oneline -- 
src/char_composite.cpp` finds both) for the exact real per-pixel blend
math if the table above isn't precise enough. **Do not resurrect that
file or its approach** — it's the wrong layer (see Background). Its value
here is purely as a reference for the blend-mode semantics, which you're
now re-expressing as Blender nodes instead of C++ loops.

### Step 1 — husk: expose every real choice per option (DONE)

Implemented: `chr_customization_options` (see above) attaches
automatically, no new flag. `src/cmd_export.cpp` gained
`tryDeriveChrModelId` (the same primary-FileDataID/fallback-filename logic
`--chr-model-id auto` always used, factored out so it can also run
best-effort when only `--customization-choice-ids` was given) and
`attachCustomizationChoices` now computes `resolvedChrModelId` in every
code path, then — after its existing `enabled_geosets`/
`chr_enabled_materials` resolution loop — builds the full menu via
`chrcustomization::namedChoicesForModel` whenever `resolvedChrModelId` has
a real value and the model has real `Option`/`Choice` rows at all.
Verified end to end, including the "only `--customization-choice-ids`,
no `--chr-model-id`" case and the "no customization flags at all, just
`--db2-dir`/`--dbd-dir`" case (`tests/test_cli_chrcustomization.cpp`, real
filename-fallback derivation, no `--listfile` needed). `--chr-model-id`
also gained the real `none` opt-out state, matching this project's
existing `auto`|`none`|`<id>` convention exactly rather than inventing a
one-off "empty means auto" special case.

This was the only new husk-side work this file's whole plan needed —
everything else below is Blender-side.

### Step 2 — Blender: read the new extras

Add `read_chr_enabled_materials(filepath)` and
`read_chr_customization_options(filepath)` to
`tools/husk_blender_geoset_mask.py`, exact same shape as the existing
`read_chr_texture_layout`/`read_enabled_geosets` (both right above/below
`_read_glb_json` in that file, ~line 720-753) — parse the raw glTF JSON
chunk (Blender's own importer has no supported extras target for a glTF
*skin* at all, confirmed empirically in that file's own module docstring
— this is why every skin-extras reader in this file re-parses the raw
file instead of reading Blender custom properties), return `None`/`[]`
when absent, never guess.

### Step 3 — Blender: build the real node graph

New function, e.g. `apply_customization_texture_switch(options, layout,
materials, filepath)`, called as a new pipeline stage (same
`_run_stage(model_name, "customization texture switch", ...)` registration
pattern every other stage in `main()` already uses, ~line 2052-2058).

For each material whose `texture_type` matches one of `layout`'s
`materials[].texture_type` (exact same filter
`apply_texture_layout_overlay` already uses — reuse it, don't
reimplement):

1. For each `chr_customization_options` entry (an Option) whose choices'
   `chr_model_texture_target_id` matches a `texture_layers[]` entry
   feeding *this* material's own `texture_type` (via the join chain in
   "What husk already exposes" above):
   - Build one `Menu Switch` shader node (`bpy.data.node_groups` or
     inline — check Blender 5.1's real node type name/API for Menu
     Switch, it may be geometry-nodes-only in older Blender and a newer
     addition to shader nodes; if it's not available in shader node trees
     in this Blender version, fall back to a `Value` driver + a chain of
     `Mix Color` nodes gated by `Compare`/`Switch` nodes, same shape
     `apply_geoset_switches`' own Geometry Nodes Menu Switch already uses
     one level up — that function, same file, is real working Blender
     5.1 Menu Switch code today, so if shader nodes don't have it, that
     function is proof positive Geometry Nodes does, and a fallback
     approach exists).
   - Each switch input = one choice's own resolved texture: an Image
     Texture node loading the real file at `--textures/<file_data_id>.png`
     (same file-naming convention every other husk texture resolution
     already uses — `.png` first then `.blp`, see
     `src/export_texture_resolution.hpp`'s `resolveTextureBytes` doc
     comment for the exact convention, though this Blender script needs
     its own file-finding logic, husk's own C++ resolveTextureBytes isn't
     reachable from Python) wired through a Mapping node positioned via
     this choice's own real section rect (reuse the exact UV math
     `_build_section_overlay_group` already computes — `x0/y0/x1/y1`
     normalized against atlas `width`/`height`, V-flipped).
2. Wire each option's switch output through a `Mix Color` node using the
   real `blend_mode` table above, chaining options in some real, defined
   order (`layer` field on `texture_layers[]`, ascending — matches the
   real client's own draw order, same as the reverted compositor's own
   `targetId` ascending sort).
3. Final chain feeds into the material's existing Base Color input (same
   "insert before the existing output, don't destroy what's there"
   technique `apply_texture_layout_overlay` already uses for its own
   overlay mix — `output_node.inputs["Surface"].links[0]`, preserve
   `original_socket`, splice in before it).

### Step 4 — verify interactively

Same discipline this whole project uses for Blender-side work (no
automated pixel-perfect render test exists for this kind of feature) —
run against a real character export with real `--textures`, confirm in
Blender's own GUI: switching a Menu Switch dropdown visibly changes the
rendered skin color/tattoo/etc., patches land in the correct UV position
(not offset/flipped), blend modes look plausible (multiply darkens,
screen lightens, etc.). Get Luna's own eyes on a real render before
calling this done, same as every other Blender-tooling feature in this
project's own history.

## Open questions, not yet resolved — flag, don't guess

- **Menu Switch node availability in shader trees**: not confirmed this
  session which Blender node types are actually available for this in the
  pinned 5.1.1 (or whatever version is current when this work starts) —
  check directly in Blender's own UI/Python console before writing code
  that assumes an API shape.
- **Real choices with a `swatch_color` instead of a real name/texture**:
  `reference/wow.export`'s own `DBCharacterCustomization.js` (line ~168,
  `SwatchColor`) shows some real choices (e.g. plain color swatches) don't
  resolve to a real texture at all, just a flat RGB — `chr_enabled_
  materials`/the new `chr_customization_options` extras only carry what
  `ChrCustomizationMaterial` resolves, so a swatch-only choice will show
  up with `file_data_id: 0` (or simply no `materials` entry on its
  `Resolution` at all). Not investigated this session whether `SwatchColor`
  itself is available in any DB2 table husk already reads — if a caller
  wants swatch-only choices to work too (a flat color node instead of a
  texture), that's separate follow-up work, not blocking this file's own
  scope (textured choices are the common, valuable case).

## When done

Update this file's own status per this repo's "open punch list, not a
historical record" convention (trim to whatever's still open, delete
outright once fully landed and written up in `CLAUDE_HISTORY.md`), and
update `TODO/README.md`'s own index entry + `TODO/
CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 5 section accordingly.
