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
  matches one of `chr_texture_layout`'s own `materials[].texture_type`,
  and for every real `ChrCustomizationOption` that resolves at least one
  textured choice onto that material — builds one closed
  `ShaderNodeGroup` per option (`_build_customization_option_group`, see
  "Node-graph findability" below for why it's a group and not raw nodes
  in the material's own tree): inside, a `Choice Index` input (0-based,
  promoted to the group's own interface, defaulted to whichever choice
  `chr_enabled_materials` actually resolved at export time) drives a
  `Math(COMPARE)`-gated chain of `Mix` nodes, one per choice, each also
  gated by its own real section rect (`_uv_rect_mask`) when the layer's
  `texture_section_type_bit_mask` picks out exactly one real section — a
  mask matching *every* real section (e.g. a real `-1`/all-bits mask,
  seen on real base-skin-tone data) or *none* is treated as "whole atlas,
  no rect restriction", not an arbitrary single-section guess. Options
  combine in real `texture_layers[].layer` ascending order via
  `CHR_BLEND_MODE_TO_BLEND_TYPE`, spliced in front of each material's own
  Principled BSDF `Base Color` (preserving whatever fed it before, same
  splice technique `apply_texture_layout_overlay` already uses).
- **No dropdown widget**: confirmed directly against the pinned Blender
  5.1.1 that `GeometryNodeMenuSwitch` exists but `ShaderNodeMenuSwitch`
  does not — material node trees have no real Menu Switch equivalent at
  all. Used the TODO's own named fallback instead (`Value` node + a
  `Math(COMPARE)`-gated `Mix` chain). Also confirmed `ShaderNodeCompare`
  doesn't exist in shader trees either (geometry/function-nodes only) —
  used `ShaderNodeMath(operation='COMPARE')` (built-in epsilon-tolerant
  equality) instead. A real, per-material console-printed legend
  (`0=<choice name>, 1=<choice name>, ...`) tells the user which index to
  type into each option's own group node.
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
  top-level nodes to 10. **To use**: open the Shader Editor, select the
  material, press Home to frame all nodes, and look for the green
  `<Option> choice index` group node(s) — their own `Choice Index` field
  is directly editable right there, no need to enter the group.
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
  GUI, confirm changing an option's own Value node index *visibly*
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
- **Multi-section masks**: a `texture_section_type_bit_mask` matching
  2+ real sections but not *all* of them is treated as "no rect
  restriction" (same accepted gap `_build_section_overlay_group` already
  has) rather than OR-combining the matching rects — not seen in this
  session's real test data, but not handled if it occurs.
