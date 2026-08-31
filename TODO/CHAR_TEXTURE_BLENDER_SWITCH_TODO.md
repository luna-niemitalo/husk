# TODO: Blender-side character-texture customization switch (Stage 5)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Why this lives in Blender, not husk

A real pixel compositor was built in husk (`src/char_composite.hpp`/`.cpp`,
blend math ported from `reference/wow.export`'s own char shader/renderer),
verified end to end, then deliberately reverted (2026-08-20) after Luna's
own direct pushback: husk doing pixel compositing at all breaks the
"attach real resolved data, never interpret/apply it" policy every other
DB2 feature in this project follows. It's also the wrong layer for the
actual goal here: Blender's own Mix Color node already implements
Multiply/Overlay/Screen natively, so the blend math doesn't need
reimplementing, and live shader compositing lets a user switch skin color
*and* tattoo *and* face marking independently in real time — something
husk precomputing static composited images fundamentally can't do without
one image per full cross-product combination. `CHAR_TEXTURE_COMPOSITING_
TODO.md`'s Stage 3 (the real `ChrCustomizationMaterial → TextureFileData`
FileDataID chain, `chr_enabled_materials` extras) is the actual
prerequisite this file's own switch consumes.

## What's implemented

`tools/husk_blender_geoset_mask.py` now has a real, live, switchable
character-texture-customization node graph, wired in as a new
`_run_stage(model_name, "customization texture switch", ...)` pipeline
stage:

- `read_chr_enabled_materials`/`read_chr_customization_options` (new,
  same shape as the existing `read_chr_texture_layout`/`read_enabled_geosets`).
- `apply_customization_texture_switch(options, layout, enabled_materials,
  materials, textures_dir)`: for every material whose `texture_type`
  matches one of `chr_texture_layout`'s own `materials[].texture_type` —
  builds exactly **one** combined `ShaderNodeGroup` for that material
  (`_build_material_customization_group`), real Base Color/Alpha in
  (whatever already fed the Principled BSDF, preserved), every relevant
  real `ChrCustomizationOption`'s own real `Choice` dropdown
  (`NodeSocketMenu`, one named enum item per real choice, defaulted to
  whichever choice `chr_enabled_materials` actually resolved) folded in
  internally and promoted up to *this one* group's own interface, real
  Color/Alpha out — wired directly to the Principled BSDF, no `Mix` nodes
  left in the material's own top-level tree at all. Each option's own
  switch is still built by `_build_customization_option_group` (a real
  `GeometryNodeMenuSwitch(data_type='BUNDLE')`, each choice's own
  `Color`/`Alpha` combined into one `NodeCombineBundle` first so the
  switch is genuinely exclusive), now instanced as a *nested* group inside
  the one combined per-material group rather than standalone. Options
  combine in real `texture_layers[].layer` ascending order via
  `CHR_BLEND_MODE_TO_BLEND_TYPE`. Final Alpha is alpha-clipped
  (`Math(GREATER_THAN, 0.0)`) before reaching the Principled BSDF's own
  `Alpha` input, not blended/dithered — real WoW customization textures
  (hair in particular) use cutout alpha.
- **One group per material, not one per option (real interactive use,
  third round)**: Luna ran the pipeline again and found up to 4 stacked
  group nodes on a single material, each named after a *different*
  material than the one it was sitting in — a real symptom of the
  earlier "one small group + external `Mix` per option" design, not
  reproduced or root-caused further since the whole shape it came from is
  gone now. Rebuilt so a material gets exactly one combined group
  (`_build_material_customization_group`, above) with the base texture
  folded in as the accumulator's starting value — "no mixing needed,
  ever" outside the group, per Luna's own framing. Verified end to end
  (headless Blender probe): a two-option case (`Hair Color` + `Tiara`,
  matching Luna's own hand-built prototype) produces one group node with
  both dropdowns on it, `Base Color`/`Base Alpha` linked from the
  material's pre-existing texture, `Color`/`Alpha` linked out through the
  clip node to the Principled BSDF.
- **Canonical short names, not the full exporter material name**: group
  names previously embedded the *entire* verbose glTF material name
  (`mat0_tex1_char_hair_bloodelffemale_hd_hair_color_5196729`) per Luna's
  "girthy names" finding. That name already contains a real, short,
  canonical name — husk's own C++ exporter (`export_materials.cpp`)
  already appends `m2::textureTypeName`'s own real
  wowdev.wiki-documented M2 `Texture.type` name (`documentation/
  wowdev-wiki/md/M2.md`'s "Texture Types" table) as a `_<type>` suffix —
  it was just buried inside batch/tex-index/FileDataID/embedded-filename
  cruft. New `M2_TEXTURE_TYPE_NAME` (Python, mirroring the same C++
  table, same values) names the combined group `Husk_<type>_customization`
  instead (e.g. `Husk_char_hair_customization`) — confirmed via a
  headless probe that a material with `texture_type=6` produces a group
  literally named `char_hair customization`.
- **Real dropdown, not a float index**: an earlier version of this
  session's own work found (confirmed directly against the pinned
  Blender 5.1.1) that `ShaderNodeMenuSwitch` doesn't exist and fell back
  to a `Value` node + `Math(COMPARE)`-gated `Mix` chain, with a
  console-printed legend to decode the index. Real follow-up finding,
  from Luna's own hand-built prototype (Blender's real Shader Editor,
  screenshots): `GeometryNodeMenuSwitch` (and the generic
  `NodeCombineBundle`/`NodeSeparateBundle` pair) can be inserted directly
  into a `ShaderNodeTree` and works there despite the `GeometryNode`
  idname — confirmed directly via a headless Blender probe, not assumed.
  Rebuilt on that: a real `NodeSocketMenu` group input, real named enum
  items (`choice_name`, not an index), no legend needed since the closed
  node itself shows the real names.
- **Node-graph findability**: Luna ran the real pipeline (export, then
  the post-import script) and reported "I still can't find the options"
  — the geoset switch was easy to find because it's a Geometry Nodes
  *modifier*, whose promoted inputs Blender auto-surfaces in the object's
  own Modifier panel; a material's Shader Editor node tree has no such
  panel. The first fix attempt (giving every node *a* `.location`, since
  none had one and they were all piling up at the tree's own origin) was
  real but insufficient: a real option can have dozens of choices (e.g. a
  real 30-choice Skin Color option), each contributing ~6 nodes — one
  real material ended up with **364 raw nodes** where the model's own
  original materials had 2-5. Even individually positioned, that many
  small boxes reads as noise, not as a control. Fixed properly:
  `_build_customization_option_group` now builds each option's own whole
  switch as one self-contained node group, instantiated as a single
  labelled, green-colored `ShaderNodeGroup` per option directly in the
  material's own tree (same "promoted socket = directly editable field on
  the closed node" technique `_build_section_overlay_group`'s own "Show
  Overlay" toggle already uses) — the same real material dropped from 364
  top-level nodes to 10 (now further collapsed to one single group node
  per material, see "One group per material" above). **To use**: open the
  Shader Editor, select the material, press Home to frame all nodes, and
  look for the green `<type> customization` group node — every relevant
  option's own `Choice` field is a real dropdown, directly editable right
  there, no need to enter the group.
- **Ergonomics — real interactive use, second round**: Luna pushed back
  hard, correctly, on the ceremony the workflow above had grown: one
  `husk export` call with 8 explicit flags, then two separate manual
  `husk blp-export` calls, then a `blender` call needing its own
  `--textures` restating what `husk export` was already told. Checked
  `husk export --help` directly rather than assuming — most of that
  ceremony was unnecessary: `--skin`/`--skel`/`--textures`/`--output` all
  *already* default sensibly (`auto`, same-basename `.skel`, "the model's
  own directory", `<model-basename>.glb`) and didn't need to be passed at
  all; confirmed by re-running with none of them and getting an identical
  export. Two real fixes landed here: (1) this Blender script's own
  `--textures` now defaults to the `.glb`'s own directory when omitted
  (matching `husk export --textures`'s own "model's own directory"
  default, and Luna's own stated real workflow — export lands next to
  the source files; a separate output directory is a deliberate dev-only
  choice, in which case the caller already has the real source dir to
  pass as the one override `--textures` flag). (2) A `.blp`-only match
  is no longer reported-and-skipped — `_convert_blp_to_png_cached` now
  auto-shells to `husk blp-export` (the real in-binary tool, not `blp/`'s
  superseded `husk-blp`) and caches the result by FileDataID under the
  system temp dir, the same "auto-detect and convert, no separate step"
  behavior `husk export` itself already has for its own embedded
  textures. The real remaining workflow: one `husk export` call (only
  `--db2-dir`/`--dbd-dir`/`--char-layout-id` — the genuinely
  undiscoverable-by-husk part — need stating), one `blender --python
  tools/husk_blender_geoset_mask.py -- model.glb` call (add `--textures
  <dir>` only when output isn't next to the source). Verified end to end
  against the real `bloodelffemale_hd` export + a cleared cache: the
  `.blp`→`.png` conversion happened live, no manual step, cache
  populated at `<tempdir>/husk_blp_cache/<file_data_id>.png`.

**Verified this session** (structural + evaluation, not yet a real visual
pass — see "Still open" below): real end-to-end `husk export --db2-dir
--dbd-dir --char-layout-id 122` against `test_data/character/bloodelf/
female/bloodelffemale_hd.m2` (real `ChrModelID` 20, 17 real options/206
real choices attached). Ran the updated script against that export with a
placeholder `--textures` directory (synthetic PNGs at real resolved
`file_data_id`s, since the real per-choice texture bytes for this model
aren't present in the local corpus) — no exceptions, 2 materials touched
(`mat5_tex2_skin`: Eye Color + Skin Color; the hair material: Skin
Color), every built `Mix` node's `Factor`/`A`/`B` fully linked (checked
by real `identifier`, not display name — `ShaderNodeMix` collapses
different `data_type`s' sockets to the same short display name,
`bpy_prop_collection` string-indexing by that name resolved the *wrong*
socket and threw `KeyError` before this was caught and fixed to use
`_node_socket`'s own identifier-based lookup, matching
`apply_multiply_blend_compositing`'s established fix for the same node
type), both touched materials' own `Base Color` input ended up linked,
and a full Cycles depsgraph evaluation (headless render) completed with
no shader-compile errors.

## Real per-choice texture resolution: fixed and re-verified with real data

The session's first pass tested `_resolve_customization_texture_path`
against synthetic placeholder PNGs only, because an exact
`<textures_dir>/<file_data_id>.png` match found nothing for
`bloodelffemale_hd`'s own real choices. Luna pointed out (real local
data, not assumed) that the real files *are* present locally, just not
under that convention: `character/bloodelf/eyes00_00_3492879.blp` and
`character/bloodelf/bloodelf_hd_hair_color_3493000.blp` — the real
FileDataID as a `_<id>` suffix on a real content-named file, not the bare
ID, and shared at the **race-level parent directory**
(`character/bloodelf/`), one level above the specific `female`/`male`
model folder a caller would naturally pass as `--textures`. Fixed:
`_resolve_customization_texture_path` now also tries a `*_<file_data_id>
.png`/`.blp` glob match, and repeats every check in `--textures`'s own
parent directory. Real `.blp` files still can't load directly in
Blender — converted a real, small representative set (every `.blp` under
`character/bloodelf/` and `character/bloodelf/female/`, 1,079 files) via
`husk blp-export --dir <dir> <out-dir>` (the canonical in-repo BLP→PNG
tool — `blp/`'s standalone `husk-blp` Python package is its
now-superseded predecessor, kept only as an independent reference
implementation `tests/test_blp.cpp` checks the real C++ decoder against;
`husk export` itself already auto-detects and converts `.blp` in-memory
for its own embedded base-layer material textures via
`readTextureFileBytes`, no separate step needed there — `blp-export` is
strictly a debugging/manual-conversion tool, relevant here only because
this Blender-side Python script can't call husk's internal C++ BLP
decoder the way `husk export` does).

Re-verified against the same real `bloodelffemale_hd` DB2 export with
these real converted PNGs (no placeholders this time): "Skin Color" (on
the hair material) loaded 4 genuinely distinct real skin-tone images
(`bloodelffemale_hd_skin_color_350012{2,3,4,5}.png`, confirmed distinct
first-texel RGBA values); "Hair Color" (on the skin material) loaded 5
genuinely distinct real images
(`bloodelffemale_hd_hair_color_{4556603,5196728..5196731}.png`, also
confirmed distinct). One real, non-buggy finding along the way: every one
of "Hair Style"'s 24 real choices shares the *identical* FileDataID list
in `chr_customization_options` — real WoW data, not a husk/Blender bug:
hairstyle selection is a geoset switch, not a texture switch, so all 24
choices legitimately resolve to the same single shared texture.

## Still open

- **A real interactive Blender GUI pass** — same discipline this whole
  project uses for Blender-side work (no automated pixel-perfect render
  test exists for this kind of feature): open the file in Blender's own
  GUI, confirm changing an option's own `Choice` dropdown *visibly*
  changes the rendered skin color/tattoo/hair color/etc. on the actual
  character mesh, patches land in the correct UV position (not
  offset/flipped), blend modes look plausible (multiply darkens, screen
  lightens, etc.). This session confirmed the mechanism loads correct,
  distinct, real per-choice texture data and evaluates cleanly in Cycles
  (see above) but did not visually confirm the rendered *result* in
  Blender's own GUI. **Get Luna's own eyes on a real render before
  calling this fully done.**
- **Real choices with a `swatch_color` instead of a real name/texture**:
  `reference/wow.export`'s own `DBCharacterCustomization.js` (line ~168,
  `SwatchColor`) shows some real choices (e.g. plain color swatches) don't
  resolve to a real texture at all, just a flat RGB — `chr_enabled_
  materials`/`chr_customization_options` extras only carry what
  `ChrCustomizationMaterial` resolves, so a swatch-only choice has no
  `materials` entry at all and is silently absent from the switch (not
  wrong, just incomplete — flagged, not guessed). Separate follow-up, not
  blocking (textured choices are the common, valuable case).

  **Investigated 2026-08-22**: `SwatchColor` is real, and it's not a
  separate DB2 table lookup — it's a column directly on
  `ChrCustomizationChoice.db2` itself (confirmed via
  `reference/WoWDBDefs/definitions/ChrCustomizationChoice.dbd`), the same
  file `chrcustomization_db2.cpp::loadChoices` already opens for
  `Name_lang`/`OrderIndex`. So no new sidecar file is needed. What *is*
  a real, currently-missing piece: every real layout since build
  9.0.1.34081 stores it as `SwatchColor<32>[2]` (a genuine two-element
  array field, one real RGB-ish pair per choice — earlier 3.4.0-era
  layouts split it into scalar `SwatchColor1`/`SwatchColor2` columns
  instead), and `db2table::readNamedColumns` is explicitly scoped to
  scalar columns only (`db2table.hpp`'s own doc comment: "a real WDC5
  array field would need its own richer API, not silently flattened
  here") — it has no way to read an array field's individual elements
  today. Closing this needs either a small `readNamedColumns`-adjacent
  array-column API in `db2table.hpp`/`.cpp`, or a narrower one-off reader
  inside `chrcustomization_db2.cpp` itself. Not implemented this session
  (scoped, not started) — real next step, no longer blocked on "does the
  data exist at all."
- (Former "multi-section masks" gap is moot: the `MenuSwitch`/`Bundle`
  rebuild dropped per-choice UV-rect masking entirely — a switch is
  already exclusive, so there's no accumulation for a rect to protect
  against.)
- **The "tiara" case, settled and partially fixed**: real data (this
  session, `--db2-dir`/`husk db2-export` + `sqlite3` against the real
  local corpus, not guessed) confirms neither of the two guesses above —
  it's a *third* real shape. "Tiara" is one **choice** (real
  `ChrCustomizationChoiceID` 6639) of the "Hair Style" **option** (real
  option 121, `ChrModelID` 20 — the same model as `bloodelffemale_hd`),
  not a separate option and not purely a geoset switch. That one choice
  owns **10** real `ChrCustomizationElement` rows, each pairing the same
  choice with a *different* `RelatedChrCustomizationChoiceID` (one of
  "Hair Color"'s own real choices, option 122) and its own distinct
  `ChrCustomizationMaterialID` — one dedicated tiara-compatible material
  per real hair color, not one unconditional material. husk's own
  `resolveChoice` (`src/chrcustomization_db2.cpp`) never read
  `RelatedChrCustomizationChoiceID` at all and attached every one of
  those 10 materials unconditionally — the real, root-caused explanation
  for Luna's own screenshot showing several `choice_XXXXX` textures all
  loaded and blended together for what should have been a single "Tiara"
  pick.

  **Fixed at the data layer**: `Element`/`MaterialResolution` now carry
  `relatedChoiceId` (parsed from the real DB2 column, 0 = unconditional);
  `attachCustomizationChoices` (`src/export_extras.cpp`, the
  `--customization-choice-ids` explicit-resolution path) now skips a
  conditional material whose related choice isn't *also* part of the
  same export's own selection, rather than attaching it as if
  unconditional — real regression test in
  `tests/test_cli_chrcustomization.cpp`. The *full-menu* enumeration path
  (`chr_customization_options`, everything `_build_material_customization_group`
  consumes) has no such "current selection" context to filter with by
  design, so it now carries `related_choice_id` through in the JSON
  (present only when nonzero) instead, and
  `apply_customization_texture_switch` (Blender side) conservatively
  **skips** any conditional material for now rather than guessing which
  one applies, printing a count of how many it skipped.

  **Still open**: a true fix needs the Blender-side switch to pick the
  *right* one of the 10 materials live, based on whichever Hair Color
  choice is currently selected in that same group node — a real
  cross-product dependency between two dropdowns, not the independent-
  axes shape this file assumed earlier. That's a genuinely new
  interaction pattern (one dropdown's value gating another's available
  data) beyond anything built so far here, and needs Luna's own steer on
  the UX before implementing rather than guessing at one.

- **2026-08-31 investigation: this "tiara case" is the general case, not
  an edge case, and there's a second, independent bug alongside it.**
  Luna found, using a real `nightelffemale_hd` export plus a hand-built
  `references/Nightelf texture blending example.blend` stub, that the
  material customization switch only ever shows one color option for the
  face, matching Skin Color choice 801, even though the real filenames on
  disk (`nightelf_femalefaceupper06_10_hd`, i.e. Face option choice 6
  paired with Skin Color choice 10) prove many more exist. Investigated
  and confirmed against real freshly-exported data (`husk export
  nightelffemale_hd.m2` with local `--db2-dir`/`--dbd-dir`, real
  `ChrModelID` 8): `chr_customization_options`' Face choice 825 (option
  50, "Face") carries 20 real `materials[]` entries, not one — each
  tagged with a distinct `related_choice_id` naming one of Skin Color's
  own choice IDs (801-809, 815-823, 8294-8296, the last three being the
  newer skin tones added later, matching Luna's "one of the new ones"
  description). husk's own DB2 join already resolves every one of them
  correctly and attaches the real data — this is exactly the same
  `related_choice_id` cross-dependency the "tiara case" above already
  root-caused, just showing up on a far more common option pair (every
  race's Face × Skin Color, not the rarer Hair Style × Hair Color tiara
  case). The reason only choice 801 ever renders: `apply_customization_
  texture_switch` (`tools/husk_blender_geoset_mask.py`, the `for m_entry
  in choice.get("materials", [])` loop) does `break` after the first
  `materials[]` entry whose `chr_model_texture_target_id` resolves to
  this material's own `texture_type` — so only the first-listed
  skin-color pairing (DB2 row order, which happens to be 801) ever gets
  wired into that choice's image node; the other 19 are silently dropped,
  not skipped-and-logged. Confirms the "Still open" cross-product-
  dependency gap immediately above is the real, general fix needed here —
  not a new, separate bug.

  **Second, independent bug, same investigation**: the node graph never
  places an overlay texture in its correct spot on the shared UV atlas at
  all. `_build_customization_option_group`'s `ShaderNodeTexImage` nodes
  wire straight to `Color`/`Alpha` with no `ShaderNodeMapping` upstream,
  and every image node's `extension` stays Blender's own default
  `'REPEAT'` (confirmed directly: every material in the stub `.blend`,
  e.g. `skin`/`char_hair`/`object_skin`, shows `extension='REPEAT'` on
  its texture node) — so a small overlay crop (face/hair/tattoo/etc.,
  only ever meant to cover its own real `CharComponentTextureSections`
  rect within the shared base atlas) tiles across the entire mesh UV
  space instead of landing where it belongs, and its own alpha never gets
  used as a real positional mask either. husk already resolves the exact
  geometry this needs and already has a working implementation of the
  same join to prove it: `chr_texture_layout`'s `sections[]` (real pixel
  `x`/`y`/`width`/`height` rects in the shared atlas, plus each section's
  own `sectionType`) and `texture_layers[].textureSectionTypeBitMask`
  (the bitmask join key — a section applies to a texture layer when
  `(1 << section.sectionType) & layer.textureSectionTypeBitMask` is set),
  joined against `chrModelTextureTargetId` to find which texture layer(s)
  apply to a given customization choice's own `chr_model_texture_target_id`.
  `_build_section_overlay_group` (same file, used only for the separate,
  debug-only magenta section-boundary toggle) already implements this
  exact rect math, including the real WoW-atlas-Y-down-vs-Blender-UV-V-up
  flip — the data and the math are both already proven working,
  `apply_customization_texture_switch`'s own node-building path just
  never calls into either.

  **(1), the UV-placement bug — fixed 2026-08-31**, same session as the
  cross-dependency fix below. New `_uv_rect_for_layer` (real section
  bounding rect for a `texture_layer`, same bitmask join
  `_build_section_overlay_group` already uses, `None` for a full-atlas
  base layer like Skin Color's own) and `_build_placement_mapping` (one
  shared `ShaderNodeUVMap`→`ShaderNodeMapping` pair per option/driving
  group, reused across every one of its own choices since they all share
  the same real section). `apply_customization_texture_switch` computes
  each real variant's own `uv_rect` once (this material's real atlas
  width/height plus the full `sections[]` list, defensively bounds-
  checked against the atlas since `sections` is one flat list for the
  *whole* character model, not partitioned per atlas, and `section_type`
  uniqueness across atlases isn't confirmed); `_build_customization_option_group`
  and `_build_driving_with_dependents_group` both set `extension = 'CLIP'`
  and wire the shared mapping whenever a real `uv_rect` exists.

  **Found and fixed a second, related bug while verifying this against
  real data**: with every real cross-linked option now actually being
  mixed in (the cross-dependency fix below), the material's final Alpha
  came out **fully transparent** on a real render — traced to the
  existing "last-layer's-own-alpha-replaces-the-running-total-outright"
  rule (kept from before this session, originally correct for the real
  hair/tiara case: a tiara variant's own reduced alpha is the *intended*
  coverage, hair strands truly hidden behind the tiara mesh). On this
  real fixture the highest-layer option turned out to be "Eyesight," a
  small eye-only sprite -- once no longer silently dropped by the
  now-fixed `break` bug, its own alpha (correctly near-zero everywhere
  outside its own tiny cropped section, once (1) above was fixed) was
  overwriting the *entire* mesh's alpha, wiping out the fully-opaque Skin
  Color base underneath. Real, deeper fix: a stage without a real
  `uv_rect` (full-domain, e.g. plain-vs-tiara hair) still *replaces* the
  running alpha outright, preserving the tiara case; a stage *with* a
  real `uv_rect` (a genuinely cropped, spatially-confined overlay --
  Face/Markings Color/Tattoo Color/Eyesight) instead **unions** with it
  (`Math(MAXIMUM)`), since a small decal is never meant to erase coverage
  a base/earlier layer already established outside its own section.
  Verified end to end against the real `nightelffemale_hd` fixture: the
  final Alpha's own source traces to a real `Math(MAXIMUM)` node (not a
  bare sub-group output) once Eyesight — the real highest-layer, cropped
  option — is reached, and a real Cycles render of the mesh is fully
  opaque again (compare against the pre-fix render, which showed the
  character as a fully transparent silhouette).
  `tools/test_husk_blender_options_panel.py` still passes unchanged.

  **Follow-up, same day: the placement math itself was wrong.** Luna
  caught it in a live render before this was reported as done: the
  overlay landed nowhere near its real section. Root cause --
  `_build_placement_mapping` left `ShaderNodeMapping.vector_type` at
  Blender's own default, `'POINT'`, which transforms the *lookup
  coordinate* forward (`out = in*scale + location`) -- the literal
  inverse of "place this image at `location`, sized `scale`, within UV
  space." `'TEXTURE'` mode computes that actual inverse
  (`out = (in - location) / scale`), which is what placing a texture
  within a UV rect needs; the Location/Scale *values* already being
  computed (`_uv_rect_for_layer`'s own `(u0, v0, width, height)`) were
  already correct, only the mode was wrong. Confirmed with a real render
  before trusting it (not from the name alone): a marker texture placed
  with these exact Location/Scale values landed entirely outside the
  visible plane under `'POINT'`, and exactly where expected under
  `'TEXTURE'`.

  Also restructured per Luna's own ask, not just the mode fix: a Mapping
  node is no longer built freshly inside every call to
  `_build_customization_option_group` (a real dependent option gets
  called once *per driving choice* -- 26 times for Face on the real
  fixture -- so this meant 26 redundant, identical Mapping/UVMap pairs).
  That function now exposes a plain `Vector` interface socket (an
  ordinary `NodeSocketVector`, none of the `Menu`-type fan-out
  restriction applies) only when a real `uv_rect` exists; callers
  (`_build_material_customization_group`'s plain-option path,
  `_build_driving_with_dependents_group`'s dependent-submenu path) build
  ONE shared `_build_placement_mapping` per real section and link it into
  every consumer that needs it.

  **Follow-up, same day -- the dedup needed to be by real rect *value*,
  not by option identity.** Luna: "markings, scars, faces, etc are
  individually applicable layers to the base texture/UV, so each of them
  should share the same mapping (unless it's a full overlay) ... markings
  is not dependent on scars, and scars is not dependent on markings, and
  neither is dependent on the face, all 3 are additional layers on top of
  face." Checked against the real fixture's own `chr_texture_layout`
  before changing anything: `Face` (`texture_section_type_bit_mask`
  1024/section_type 10), `Scars` (512/section_type 9), and `Markings
  Color` (2048/section_type 11) all resolve to the *exact same* real
  section rect (x=1024, y=0, w=1024, h=1024 -- the right half of the
  atlas), confirming this precisely; `Hair Style`/`Eye Color`'s own base
  turned out to share it too. The per-option dedup above (7 Mapping
  nodes, one per option/driving-group scope) missed this because Face's
  own Mapping lived inside the "Skin Color" driving group's own separate
  node tree while Scars' lived in the top-level tree directly -- two
  different real node-group data-blocks, so even an identical value
  couldn't literally be the same node without a structural change.

  Fixed by moving dedup up to the one tree that's a common ancestor of
  every option a material touches (`_build_material_customization_group`):
  neither `_build_customization_option_group` nor
  `_build_driving_with_dependents_group` builds a Mapping node anymore --
  each only exposes a `Vector` interface socket (or several, for a
  driving group with multiple cropped dependents) when placement is
  needed, returning `{socket_name: uv_rect}` so the caller knows what
  value each one wants. `_build_material_customization_group` now keeps
  ONE real Mapping per distinct `uv_rect` *value* (`shared_mappings`,
  keyed by the exact `(u0, v0, w, h)` tuple -- safe as a dict key here
  since it's always derived from identical integer section data when two
  layers genuinely share a section, so the floats come out bit-identical)
  and links it into every consumer that asked for that value, regardless
  of which option/driving-group tree that consumer lives in.

  Verified against the real fixture: the whole "skin" material combined
  tree now has exactly **2** total `Mapping` nodes (one per real distinct
  section actually in play across the whole material -- the shared
  Face/Scars/Markings Color/Hair Style/Eye Color rect, and Tattoo Color's
  own separate one), both built only in the top-level tree; traced every
  `ShaderNodeGroup` instance's own `Vector`-typed inputs and confirmed all
  6 real consumers (`Group.Face Vector`, `Group.001.Vector` [Scars],
  `Group.002.Markings Color Vector`, `Group.004.Hair Style Vector`,
  `Group.005.Driving Vector` [Eye Color's own image],
  `Group.005.Eyesight Vector`) link from the same shared `Mapping` node,
  and `Group.003.Tattoo Color Vector` links from the other one.
  `tools/test_husk_blender_options_panel.py` still passes; the real
  pipeline still runs clean end to end against the real fixture, still
  fully opaque.

  **Follow-up, same day: a real 'None' choice per detail layer, and
  verifying multi-layering actually works.** Luna: "each of the scars,
  tattoos, markings, etc should have a none option" and reported that
  before this session's fixes she could only ever see *one* of
  Scars/Markings at a time, never both. Checked the real fixture's own
  `chr_customization_options` before changing anything: every one of
  Scars/Markings Color/Tattoo Color genuinely has a real DB2 choice named
  `'None'` (`order_index` 0 -- the real client-side default), with
  exactly zero `materials[]` rows -- a deliberate "paint nothing here" by
  design, not a resolution gap. `apply_customization_texture_switch`'s
  per-choice loop only ever kept a choice that resolved at least one real
  image, so `'None'` was silently absent from every one of these
  dropdowns -- not selectable at all. Fixed: a choice with a genuinely
  empty `materials[]` list (never conflated with an actual image-load
  *failure*, which still gets skipped as before) is now synthesized into
  a real, selectable menu item once the option's own placement/blend data
  is known from any already-resolved sibling choice (borrowed, since a
  real `'None'` row has no materials entry of its own to read `layer`/
  `blend_mode`/`uv_rect` from) -- a transparent `RGB(0,0,0,1)`+`Value(0)`
  constant, exactly like the existing "this driving choice resolves
  nothing" fallback already used elsewhere. Verified: `Husk_skin_
  customization_Scars`'s own `MenuSwitch` now has 6 real items
  (`['Teldrassil', 'Rip', 'Claw', 'Scratch', 'Swipe', 'None']`, matching
  the real DB2 list exactly), and setting the outer `Scars` dropdown to
  `'None'` validates cleanly.

  The "only ever one of Scars/Markings" report traces to the very same
  alpha-overwrite bug this session already root-caused and fixed above
  (a cropped stage's own near-zero-outside-its-section alpha replacing
  the *entire* running total instead of unioning with it) -- Scars and
  Markings Color share the same real section, so whichever one mixed
  *last* would previously wipe out the other's own painted pixels
  wherever its own alpha was 0, even though the color chain was already
  correctly compositing both. Verified this is genuinely fixed, not just
  inferred: traced the real fixture's own final Color/Alpha chains node
  by node. Color: `Skin Color -> Scars -> Markings -> Tattoo -> Hair
  Color -> Eye Color`, six *separate* sequential `Mix` stages, each
  building on the previous (not a single winner-takes-all switch). Alpha:
  a matching chain of six `Math(MAXIMUM)` nodes, one per stage -- confirms
  every real layer's own coverage unions into the final result rather
  than the last one overwriting the rest. Multi-layering (seeing scars
  *and* markings *and* a tattoo simultaneously, each independently
  toggleable) is real and working, not just structurally plausible.

  **(2), the cross-dependency bug — SOLVED, real fix found (2026-08-31),
  not just investigated.** Luna asked whether a live nested `MenuSwitch` —
  one shared "Skin Color" dropdown driving both its own sibling switch and
  Face's own internal per-skin-color switch, no addon needed — could
  replace `husk_blender_options_panel.py` for this. First attempt (raw
  fan-out: link `NodeGroupInput`'s Menu output straight to two separate
  `GeometryNodeMenuSwitch` nodes) **confirmed broken in Blender 5.1.1**,
  isolated minimal repro first: a promoted `NodeSocketMenu` can only
  validly drive exactly one internal `MenuSwitch` chain — feeding it to
  two leaves the interface socket with zero valid enum items
  (`default_value` throws; dropdown empty in the Shader Editor; material
  renders flat/faceless). Blender's own link-validity warning on this
  exact wire reads "Use node groups to reuse the same menu multiple
  times" — Luna pushed to actually test what that means rather than
  accept the dead end. Tried literally wrapping each consumer in its own
  node group first: **still fails** unless it's the exact same group
  *datablock* instanced twice (proven with an isolated repro: identical
  content in two separately-built-but-structurally-equal groups still
  fails; the same tree object instanced twice succeeds) — not useful here
  since Face's per-shape branches need genuinely different images.

  **First working alternative found** (superseded by the real fix below,
  kept in `example_exports/character_customization/`'s demo file for
  comparison): Skin Color's own `MenuSwitch` bundling a plain Float
  `Index` alongside Color/Alpha, fanned out (as an ordinary Float, no
  Menu-type restriction) to drive Face's own per-shape image pick via an
  old-style `Math(COMPARE)`-gated `Mix` chain. Verified working
  (`DemoSkinNestedIndexed_WORKING`, `live_sync_proof.png`), but abandoned
  once Luna found something better: her own hand-built "Working Menu"
  reference structure in the same demo file used real per-branch
  `MenuSwitch` submenus with no Float/Math machinery at all.

  **The real fix, and what's actually implemented now**: Luna's own
  structure -- one independent, unshared `NodeSocketMenu` **per (driving
  choice, dependent option) pair**, e.g. a real "Face (choice_801)",
  "Face (choice_802)", ... submenu per real Skin Color choice, rather than
  trying to reuse one Menu value. Each submenu has exactly one consumer
  (its own internal `MenuSwitch`), so none of them hit the fan-out wall.
  Confirmed directly against the real file Luna built (not assumed from
  the screenshots she first shared): all three of its promoted `Menu`
  sockets validate and are independently settable; Blender's Shader Editor
  hides every submenu except the one belonging to the currently-selected
  outer choice (**Properties tab does not** -- confirmed via
  `sock.enabled`/`hide`/`is_unavailable`, all stay `True`/`False`/`False`
  regardless of the outer value; the hiding is Shader-Editor-only UI
  behavior, not a data-level property). Acceptable since this graph is
  meant to be read/edited in the Shader Editor.

  **Implemented for real** (not just demoed) in
  `tools/husk_blender_geoset_mask.py`:
  - New `_build_driving_with_dependents_group(name, driving_option,
    driving_choice_infos, dependents)`: one outer `MenuSwitch` over a
    driving option's own choices (e.g. Skin Color); each case's bundle
    carries the driving option's own Color/Alpha (when it resolves a
    texture here) plus, per dependent option, a fresh submenu instance
    with its own independent, unshared `Menu` socket -- Luna's structure,
    generalized from her 2-choice demo to real per-model choice counts
    (26 real Skin Color choices on `nightelffemale_hd`, not 2).
  - `_build_material_customization_group` reworked to flatten every real
    mix stage -- one per plain option, one per driving option's own base
    layer, one per dependent -- into a single list sorted by real
    `chr_texture_layout` layer order, so a dependent's own layer mixes in
    its correct real position relative to plain options too, not just
    relative to its own driving group. Also now returns
    `{socket_name: default_choice_name}` for every real socket it
    creates, since a socket promoted through this function is always
    `links.new`'d and can't carry its own meaningful `default_value` --
    the caller applies all of them at once on the outermost, genuinely-
    unlinked material `group_node` instance instead (this also fixes a
    latent pre-existing gap: the old code recomputed each promoted
    socket's name independently at the call site rather than using the
    name `_unique_label` actually assigned, silently wrong on the rare
    real name collision).
  - `apply_customization_texture_switch`'s own per-choice collection loop
    no longer `break`s after the first resolved `materials[]` entry --
    every real variant is kept, split into independent options (one
    resolved choice_info per choice, original behavior) vs.
    driving-dependent ones (any choice has a variant with a real nonzero
    `related_choice_id`), grouped by whichever option owns those related
    choice ids (a new global `choice_id -> owning option` map, built once
    across every real option). A driving option with no own texture on a
    given material (a pure selector, e.g. this model's real "Markings"/
    "Tattoo"/"Hair Color") still gets a real, non-blank default -- the
    first of its own choices that resolves anything for at least one
    dependent, not Blender's own blank `''` state.

  **Verified end to end against real data**, not just headless
  construction: `husk export nightelffemale_hd.m2` (real `--db2-dir`/
  `--dbd-dir`, `ChrModelID` 8) piped through the real
  `husk_blender_geoset_mask.py` CLI entrypoint (`blender --python ... --
  model.glb --textures <dir>`, a handful of real converted skin/face
  `.blp`→`.png` textures) -- ran clean, no exceptions, "4 material(s) got
  a real live customization texture switch". The real `skin` material's
  combined group node ended up with `Skin Color` plus 26 real
  `Face (choice_NNN)` submenus (one per real Skin Color choice this model
  actually has), **and** automatically found three more real cross-links
  this session hadn't set out to fix: `Markings` → 10 `Markings Color
  (<name>)` submenus, `Tattoo` → 4 `Tattoo Color (<name>)` submenus, and
  `Hair Color` → 15 `Hair Style (choice_NNN)` submenus -- all previously
  silently collapsed to one wrong texture each by the same `break` bug.
  Every real promoted socket validated with a real, non-blank default
  (`Skin Color` → `choice_801`, `Markings` → `Bear`, `Hair Color` →
  `choice_858`, ...); changing `Skin Color` and a `Face (choice_804)`
  submenu both succeeded live; depsgraph evaluation and a real Cycles
  render both completed with no shader-compile errors.
  `tools/test_husk_blender_options_panel.py` still passes unchanged (its
  own fixture is built independently of this code, per its own doc
  comment).

  **Known gap, not fixed this session**: `husk_blender_options_panel.py`
  finds a driving-dependent option's own promoted socket by matching the
  option's real name exactly (`sub_node.inputs[option_name]`-style
  lookup, per its own doc comment). A dependent option's sockets are now
  named `f"{option_name} ({driving_choice_name})"`, never the bare option
  name, so the options panel currently can't find or sync
  Face/Markings Color/Tattoo Color/Hair Style at all on a real model with
  these cross-links -- it silently treats them as absent (its own
  documented behavior for "node graph was never built", not a crash).
  Plain independent options (Skin Color, Markings, Tattoo, Hair Color, Eye
  Color, Scars, Eyesight) are unaffected -- their own socket names didn't
  change. Fixing this needs the panel to also understand "one row per
  (dependent option, driving choice) pair, only the currently-relevant one
  meaningful" -- not attempted here, flagged for whoever picks this up
  next.
