# TODO: Blender-side character-texture customization switch (Stage 5)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

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
  wrong, just incomplete — flagged, not guessed). Not investigated
  whether `SwatchColor` itself is available in any DB2 table husk already
  reads. Separate follow-up, not blocking (textured choices are the
  common, valuable case).
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
