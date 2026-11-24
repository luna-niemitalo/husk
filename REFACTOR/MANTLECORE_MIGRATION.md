# MANTLECORE_MIGRATION.md — moving MantleCore's loader to the current bundle format

For whoever updates MantleCore's world loader
(`src/world_loading/world_loading.cpp`). As of 2026-10-11 it reads the
first terrain-tile shape husk wrote (`kind: "terrain_tile"`, a flat
`terrain.placements` array). husk no longer writes that shape. This
file maps every key that loader reads to where it lives now, plus the
behaviour changes that a pure rename would miss.

This is a scratch document: delete it once MantleCore has migrated. The
format itself is defined in exactly one place:

- `schema/*.schema.json` in husk (JSON Schema draft-07, every key
  described): `model`, `placement_set`, `world`, plus `common` for the
  shared shapes.
- `REFACTOR/BUNDLE_FORMAT.md`, sections "Document kinds and the contract
  header", "Terrain tile bundles" and "World documents": what a consumer
  needs beyond the schema.
- `husk validate-bundle <dir>...`: checks each bundle and the placement
  sets it owns (not the models those sets reference: pass them too). If
  MantleCore's reading disagrees with a document that passes, MantleCore
  is wrong.

## 1. What changed, in one paragraph

Two document-wide rules first: every texture a document uses is listed
once in its `resources.textures`, and everything else (material layers,
terrain ground textures, liquid frames, effect image inputs) holds an
index into it. In an `export-world` tree a model's texture entries point
into the shared `<out>/textures/` (like tiles' already did), not its own
`textures/`. A ref without a display name is just `{id}`: `name` and
`name_source` are present together or not at all. `provenance` no longer
has `exported_at`; a stored build's `meta.json` carries the time.

v0.2 (`schema_version` "0.2.0", 2026-10-13), from MantleCore's
preliminary feedback:
- `framebuffer_blend` is one shared definition (common.schema.json)
  whose description gives each mode's formula, and gains `mod_add`
  (EGxBlend 6: the five Broken Shore moonwell/shrine WMO materials that
  had no blend).
- WMO batches of 65,536 or more indices: `MOBA` stores the count in 16
  bits, so 109158, 219651 and 219655 came out short (3,536 of 69,072).
  husk now recovers the full count; their `index_count`s are multiples of
  3.
- A placement selecting a doodad set its WMO lacks keeps only the sets
  that exist; each dropped one is an `errors.log`/`missing.tsv` row
  (phase `placement`). What the client shows for such a placement is not
  known. The f3ab7d8 build drops 2,698 (16 on azeroth); 2,586 come from
  `MWDS` set lists, whose reading as `MODS` indices is now in doubt
  (`WIKI_FINDINGS/WORLD.md`).
- Techniques (`BUNDLE_FORMAT.md` "Techniques"): each material's
  `techniques` lists reference indices to `technique` documents in the
  bundle's `techniques/` (its M2 pixel shader or WMO shader, then its
  framebuffer blend). Read the math there: `outputs` are expression trees
  over the declared `inputs`. An M2 pixel shader references
  `m2_pipeline/pipeline`, which states how its diffuse, emissive and alpha
  meet lighting and the mesh colour. The schema descriptions no longer
  carry the formulas.
- Surface types (`BUNDLE_FORMAT.md` "Surface types"):
  `resources.surface_types` lists TerrainType rows (name, footstep sound
  class, sprays). Terrain `ground_effects[].surface_type`, WMO
  `materials[].surface_type`, and a WMO's face channel `surface_type`
  (when it has per-face overrides) index it. Most terrain has none: 63%
  of quads' ground effects are absent from the build's DB2.
- WMO shaders 22 and 23 (2026-10-15, answering
  `development/husk_wmo_shaders_handoff.md`): both technique documents
  now have outputs. To evaluate them:
  - WMO layers carry `texture_unit` like M2 layers, and WMO technique
    inputs are `texture_unit` (`texture_slot` is gone). For 22 and 23 the
    layers include `color_2` (3), `flags_2` (4) and `runTimeData[0..3]`
    (17-20). Bind input `t<n>` to the layer whose `texture_unit` is n.
  - A texture input may have a `coordinate` expression: 22 offsets uv1
    and uv2 by `tangent_view`, and samples at `reflect_xy`; 23 samples at
    `sphere` and raw uv0-uv3. These are `renderer_value` and
    `vertex_value` inputs whose descriptions give the formula.
  - New ops: `div`, `max` (n-ary). A technique's terms may now use them.
  - `MOC2` is the vertex channel `wmo.moc2` (4 x u8, RGBA, x/y/z/w =
    R/G/B/A). The bytes are BGRA like `MOCV`, verified on real data
    (`WIKI_FINDINGS/WORLD.md` "WMO shaders 22 and 23"), so wow.export's
    `.bgr` swaps x and z.
  - WMO meshes keep a fourth uv set (`uv_sets[3]`; husk capped them at
    three).
  - 22's diffuse layer is `texture_3` (`t2`) and 23's is `texture_2`
    (`t1`), for a renderer that draws one texture.

v0.2.1 (2026-10-16), levels of detail (additions; nothing moved): every
mesh model has `resources.geometry.lod`, every terrain tile
`resources.terrain.lod`, both `{max_level, levels}` with level 0 the full
detail. To draw level n, read `levels[min(n, max_level)]`. M2 levels are
the `_lodNN` skins (sharing level 0's vertices), WMO levels the GFID rows
(their own vertices, interiors dropped), terrain levels the `_lod.adt`
band meshes over the tile's baked map texture. Tile placement instances
listed in `_obj1.adt` carry `distant`; leave the rest out when drawing a
tile from afar. `BUNDLE_FORMAT.md` "Levels of detail" has the details.

v0.4.0 (`schema_version` "0.4.0", 2026-11-24). Every document carries
"0.4.0"; a 0.3.0 reader rejects it by the `schema_version` const, so
re-export rather than mix. Breaking changes, each with what a reader
changes:
- **Every document names its schema.** A required top-level `$schema`
  holds the `$id` of the kind's schema,
  `https://husk.invalid/schema/<kind>.schema.json`; `validate-bundle`
  rejects one that doesn't match `kind`. A reader that checks for an
  unknown top-level key accepts `$schema`; one that dispatches on `kind`
  can keep doing so (`BUNDLE_FORMAT.md` "The contract header").
- **Particle gravity is a vector track.** `particle_system.gravity` was
  an `animated_float` (and `null` or a meaningless float for the 80% of
  emitters whose keys are packed vectors, M2Particle flag `0x800000`); it
  is now an `animated_vec3`, the acceleration in yard/s² in the
  emitter's frame (z up). A plain source value g is `[0, 0, -g]`. Apply
  it in the emitter's frame: it turns with the emitter's bone and the
  model's placement, it is not world down. A packed key with no
  direction is dropped from the curve (husk warns); none occur in a
  27,758-key sample.
- **MFOG has no medium.** `classic_local_fog` instances lost the optional
  inputs `extinction` and `albedo` (derived by `linear_fog_to_extinction`
  and `fog_color_to_albedo`) and the effect lost its `medium` content.
  Don't place an MFOG sphere as a fog volume: it is distance fog a viewer
  inside the parts that list it sees (the classic look stays in the
  instance's `style`, the radii in `inner_radius`/`outer_radius`).
- **Parallax (WMO shader 22) has no `tangent_view` input.** Its term `e`
  is `tangent_frame(view_position, view_position, view_normal, uv1) /
  dot(view_normal, view_position)` over two new `renderer_value` inputs,
  `view_position` and `view_normal`. Provide those two and evaluate the
  new `tangent_frame` op (below) in the renderer.
- **Portal `side` is resolved.** `parts[].portals[].unresolved.side` is
  now `parts[].portals[].side`, required, +1 or -1: +1 when the owning
  part lies where `dot(normal, p) + distance > 0`. The link has no
  `unresolved` object any more. Values are the source's, so the ~14
  authoring errors in 3,871 cleanly separated sample portals stay flipped.
- **`parts[].fog_volumes` is `parts[].interior_fogs`.** Same `{set, id}`
  refs, renamed for what they are: the fogs a viewer inside the part
  sees, beside `resources.scene.default_fog`.
- **Material layer alpha is two curves.** `layers[].alpha_fade` is gone;
  read `layers[].color_alpha` (M2Color.alpha) and
  `layers[].texture_weight` (M2TextureWeight). Both are written when the
  source sets both; the consumer combines them (`BUNDLE_FORMAT.md`
  "Material tracks", table in "`Model()`" below).
- **Item models and textures are the wearer's files.** A
  `definition.items[].models[]` entry is `{position, model}` or
  `{position, unresolved_reason}`, no longer an asset ref: `position` is
  the ItemDisplayInfo.ModelResourcesID index (shoulders: 0 left, 1
  right). `textures[]` entries gain a required `model_position`, the
  model they dress. Each model and texture is the file for the wearer's
  race and sex (ComponentModelFileData / ComponentTextureFileData); an
  unpicked texture is `known_unresolved` with the reason.
- **`name_source` gains `community`.** A closed enum (a 0.3 schema, a
  switch without a default) rejects it. It marks a name from a
  community-maintained table outside the client, today only clip names
  (below); treat it like `listfile`: decoration that may be wrong.
- **References are named by id.** Every document's `references[]` entry
  has a required `id` (a UUID derived from the kind and the referenced
  asset's identity, `BUNDLE_FORMAT.md` "Stable ids"), and every pointer
  into references holds that id string, not an integer index: model
  `definition.placement_sets[].reference`, `texture_slots[].composite`,
  customization choice `textures[].material`, `selection.equipped[].reference`,
  material `techniques[]`; placement_set `instances[].reference`; world
  `grid.cells[].tile` and `placements[]`. Build an id → reference map per
  document and look pointers up in it; don't assume an order of
  references (owned sets first, tiles first) any more. A set placing one
  file under two names now has one reference for it.
- **Mesh parts are named by id.** `resources.geometry.mesh.parts[]` has a
  required `id` (from the model's identity and the WMO group index); part
  `portals[].to_part`, liquid `part`, static light `part`, primitive
  `part` (mesh and every LOD level) and LOD `source.part_levels[].part`
  hold that id. Map ids to parts once per model.
- **Light conditions are named by id.** A placement set's
  `resources.light_conditions[]` (0.3 `light_params[]`, renamed below)
  entries have a required `id` (from the LightParams row); a zone light's
  `clear`, `clear_underwater`, `storm`, `storm_underwater` and `death`
  hold that id.
- **Textures are named by id.** Every `resources.textures[]` entry of a
  model, terrain tile or placement set has an `id` (from the texture's
  identity, state and what else tells two entries of one file apart), and
  every texture pointer holds it: material layer `texture`, slot
  `options[]`, item and choice `textures[].texture`, static light
  `cookie`, liquid type `textures[].frames[]`, terrain ground texture
  `diffuse`/`height`, LOD `diffuse`/`normal`, `liquid_flow`, and an
  effect instance's `image` inputs (e.g. emitter `texture`, zone light
  `color_grading`). A material document's own `resources.textures` is
  unchanged: its layers still index it.
- **Materials are named by id.** `resources.materials[]` entries have a
  required `id`; a primitive's `material_index` (mesh and every LOD level)
  and a liquid's `material_index` are replaced by `material`, holding it.
- **Static lights are named by id.** `resources.scene.static_lights[]`
  entries have a required `id` (from the model's identity and the light's
  MNLD/MOLT/MOLS/MOLP record); a part's `lights[]` and `light_sets[][]`
  hold those ids.
- **Presets point at appearances.** A `selection.presets[]` entry has a
  required `id` and keeps only `appearance` (an id), the outfit's
  `textures` (a humanoid NPC's bake; now optional), `items`, `sources`
  and the outfit's `unresolved`. `customization`, `shown_geosets`,
  `particle_colors`, `opacity`, a creature display's texture variations
  and the body's `unresolved` moved into the appearance. Find it through
  the model's one `references[]` entry of kind `appearances` (new in the
  kind enum): `document` holds the list inline (`husk export`), or
  `asset.uri` names the tree's shared `appearances.json` (`export-corpus`,
  `export-appearances`); look the id up in that document's
  `appearances[]`. Its indices are still this model's definition's. Apply
  the appearance first, then the preset's own `textures` and `garments`
  (`BUNDLE_FORMAT.md` "Appearances").
- **Items are garments** (`REFACTOR/GARMENTS.md`). What a model wears is a
  `garments` document (new kind, `schema/garments.schema.json`), reached
  through the model's one `references[]` entry of kind `garments` (new in
  the kind enum), `document` inline (`husk export`). Old → new:
  - `definition.items[]` → the document's `items[]` (`ref`, and
    `source.slot`/`slot_token`/`item_slot`) plus `garments[]` (`kind`,
    `layer`, `order`, `pieces[]`); `items[].garments` lists the
    garments wearing the item exactly means.
  - `items[].overlays[]` `{component_section, texture}` → piece
    `overlay {region, texture, mask?}`, the number in `source.section_type`;
    regions and their rectangles are the model's new `definition.body.regions`.
  - `items[].models[]` + `textures[]` → piece `attached {point, model |
    unresolved_reason, textures[]}` (head, shoulders, weapons) or
    `skinned {model | unresolved_reason, textures[], shown_geosets}`; a
    texture's `texture_type`/`model_position` → `role` plus
    `source.texture_type`/`source.model_position`.
  - New: piece `geometry {part, geoset}`, the body geosets the item
    switches on (sleeves, hem, boots), which 0.3 did not export.
  - `selection.presets[].items[]` → `selection.presets[].garments[]`
    (garment ids, each once; no slots); a bake's `covers_items` (indices)
    → `covers_garments` (garment ids).
  - `selection.equipped` is removed (nothing wrote it).
  - Texture ids in pieces index the garments document's own
    `resources.textures`, not the model's.
  A hem a legs item ships (body geoset 1102) is a piece of the tunic of its
  set, with its paint masked out of the legs texture (`overlay.mask`, a
  white-on-black texture); wearing a legs item's `items[].garments`
  still reproduces the client.
- **Animation fallbacks are a shared document.**
  `resources.rig.animation_fallbacks` (`[animation, fallback]` AnimationData
  id pairs, copied into every model; in earlier 0.4.0 builds only) is
  gone. The table is the model's one `references[]` entry of kind
  `animation_fallbacks` (new in the kind enum, new kind
  `schema/animation_fallbacks.schema.json`): `document` inline
  (`husk export` with `--db2-dir`), or `asset.uri` naming the tree's one
  `animation_fallbacks.json` (`export-world`/`export-corpus` with
  `--db2-dir`), the same two forms as appearances. Old pair `[a, f]` is now
  a `fallbacks[]` entry `{id, animation, fallback}` whose
  `animation.id.row` is a and `fallback.id.row` is f (refs named like a
  clip's `animation`); the implicit "unlisted falls back to 0" is the
  document's `otherwise` ref. Walk it by identity until a clip exists
  (`REFACTOR/ANIMATION.md` "Fallbacks"). Load the shared file once per
  tree, not per model.
- **Creature display `preset` is an id.** `creatures.json` displays hold
  the preset's `id` string instead of its index into `selection.presets`.
- **Raw source values moved under `source`.** Values kept as the source
  stores them, with no meaning a consumer of another source could use,
  left the canonical keys:
  - terrain `resources.terrain.liquid_objects[]`: `flow_speed`,
    `fishable`, `reflection` → `liquid_settings[].source.{flow_speed,
    fishable, reflection}` (`flow_direction` stays);
  - model `resources.physics.bodies[].type` →
    `bodies[].source.type`;
  - placement set `resources.light_params[]`: `celestial_sphere_override`,
    `sun_position_override` → `light_conditions[].source.{...}`.
  Descriptions of colours and densities no longer say "as stored"; their
  values are unchanged.
- **Keys named after the source renamed.** Values unchanged unless said:
  - model `definition.customization.options[].chr_model` →
    `options[].body` (the body model the option belongs to);
  - placement set `resources.light_params[]` →
    `resources.light_conditions[]`, and its entries' ids change with it
    (key `light_condition/db2_row/LightParams/<row>`, `BUNDLE_FORMAT.md`
    "Stable ids");
  - terrain `resources.terrain.liquid_objects[]` →
    `resources.terrain.liquid_settings[]`; a liquid's `liquid_object` →
    `liquid_setting`;
  - customization choice `geoset_id` (group·100+variant) →
    `geoset: {group, variant}`, the packed number kept as
    `source.geoset`;
  - primitive (mesh and every LOD level) `geoset_id`, `geoset_group`,
    `geoset_variant` → `geoset: {group, variant}`, the packed number kept
    as `source.skin_section_id` (absent on a WMO batch). Match a choice to
    ranges on `geoset`;
  - appearance `shown_geosets[]`: packed numbers → `{group, variant}`
    objects.
- **Zone fog has an extinction; its density moved under `source`.** A
  light key's `fog.density` and `fog.height_density` (the client's
  parameters, unit `1`, never an extinction) are now
  `fog.source.density` and `fog.source.height_density`. New required
  `fog.extinction` and `fog.height_extinction`, in 1/yard: the fog keeps
  `exp(-extinction · e)` of the scene colour at `e` yards past the fog
  start, the rest is fog colour; inside the height fog blend the two by
  the height weight. Derived by `fog_density_to_extinction`
  (`0.0005 · density`, floored, missing values filled), grade ref: the
  scale comes from one reimplementation, not client code. A typical key
  (density 4.5) reaches half fog 308 yards past the start
  (`WIKI_FINDINGS/RENDERING.md` "Zone fog density").

Additions (a 0.3 reader that ignores unknown keys loses nothing):
- **Creature catalog, a new document kind.** `husk export-creatures`
  (also run by `export-corpus`) writes `<tree>/creatures.json`: every
  client `Creature` row with name, title, type and family (refs with
  names), raw `source.classification`, and its display slots with weights; each
  display names its model by stable id, with the id of its preset in that
  model's `selection.presets` and its scale. Where creatures live is
  server data, so the catalog has no locations (`BUNDLE_FORMAT.md`
  "Creature catalogs").
- **Zone flags.** With `--db2-dir`, a world zone's `source` carries
  AreaTable `flags` (Flags[0], Flags[1]), `mount_flags` and
  `flag_names` (wowdev.wiki's 10.2 bit names), all three or none
  (`BUNDLE_FORMAT.md` "World documents", `zones`).
- **Technique ops** `min` (n-ary, per channel), `dot` (two operands, a
  scalar; `dot(x, 1)` sums x's channels) and `tangent_frame(v, p, n, uv)`
  (two channels; needs screen derivatives of `p` and `uv`, so the
  renderer evaluates it). Definitions: `schema/technique.schema.json`'s
  `expression`. UnkDFShader (23) sums its weights with `dot`.
- **Physics from the model itself.** `resources.physics` is filled from
  the M2's `PFDC` chunk or its `PFID` file, not only from a `.phys`
  beside it, so physics capes and mounts ship their rigid-body chains. The
  manifest's provenance source `phys` says which was read (`available:
  false` = not found, unknown, not "has none").
- **Joint flags.** `skeleton.joints[].inherit: {translation, rotation,
  scale}` (false: the joint ignores that part of its parent's transform)
  and `driven_by: "physics"` on the joints a dynamic physics body drives:
  drive those from the simulation, the rest from animation.
  `source.flags` keeps the raw word; bits with no established meaning are
  only there.
- **Global-sequence lengths.** A `global_sequence` clip has `duration`
  (its loop); `resources.rig.global_sequences[i].duration` is the loop
  table other curves index. Use these as the period, not the last key.
- **Map identity.** With `--db2-dir`, a world document has a `map` block
  from its `Map` row: `ref` (row + `MapName_lang`), `instance_type_name`
  (for 0-4), `expansion`, `max_players`, `parent`, `cosmetic_parent` (Map
  refs) and `area` (AreaTable ref), each named, and the raw
  `source.directory`, `source.instance_type` and `source.map_type`. A tile carries the same map ref
  as `definition.terrain.map`. Use these for the place's name, not the
  WDT file stem.
- **WMO interior names.** With `--db2-dir`, a WMO mesh part has `areas`:
  per name set, `{name_set, ref, area, whole_model}` (WMOAreaTable row
  with `AreaName_lang`, its AreaTable row named). Every map-object
  placement carries `selection.name_set`; pick the entry whose `name_set`
  equals it, the way `selection.placement_sets` picks doodad sets
  (`BUNDLE_FORMAT.md` "Schema 0.4.0").
- **More named refs.** With `--db2-dir`, a tile's
  `resources.terrain.chunks.areas[]` carry AreaTable names and a zone
  light's `skybox.ref` its LightSkybox name; identities unchanged.
- **Creature display fields.** A catalog display gains `source.size_class`
  (raw) and unnamed row refs `object_effect_package`, `state_spell_visual_kit`,
  `anim_replacement_set`, `dissolve_effect`, `mount_poof_spell_visual_kit`
  (CreatureDisplayInfo columns of the same names), each absent at 0.
- **Character model names.** A customization option's `body` ref
  is named "<race> <sex>" from ChrRaces and ChrRaceXChrModel ("Human
  male"); match on the ChrModel row, show the name.
- **Clip names on 12.x.** With `--anim-names <wow.export checkout>` (also
  on `export-world`, `export-corpus`, `snapshot`), a clip's `animation`
  ref is named from wow.export's list (`name_source: community`) where the
  client has no name; a client name (`db2`) wins. Provenance source
  `animation names` records repo, file and commit. Match clips by
  `animation.id.row`, never by name.

Output changes, not format changes: `db2-export` writes float arrays as
numbers and String/LocString arrays as text (they were raw bits and
string-table offsets, e.g. LiquidType `Float_*`, `Texture_*`), and
resolves single strings by WoWDBDefs type.

Output change, not a format change: DB2 copy-table rows are now read, so
ItemDisplayInfo, ComponentModelFileData (65 stored + 59,474 copies),
ChrCustomizationMaterial/Geoset, GroundEffectTexture and LightParams gain
rows. Bundles built before it miss that data; rebuild them.

Materials (2026-10-12): a material layer no longer has `blend`. It was
derived from the M2 framebuffer blend mode, not the texture combiner, so
it never said how layers combine. An M2 material now names its
`pixel_shader` (and `vertex_shader`), each layer says which shader texture
it is (`texture_unit`: t0..t3), and `model.schema.json`'s
`definitions.m2_pixel_shader` gave every pixel shader's formula; in v0.2
the formulas are technique documents instead (below).

UV maps (2026-10-12): `geometry.mesh.uv0`/`uv1`/`uv2` are now
`geometry.mesh.uv_sets` (a list, set 0 first). A layer's `uv`,
`uv_animation` and `uv_scroll` are now `uv_map`, an index into
`resources.uv_maps`: each entry is a `set` (a uv_sets index, or
`"environment"`), with its own `transform` curves and `scroll`. Layers
that sample alike share an entry. M2 layers past the first now carry
their own texture unit's transform (they had none).

Lights (2026-10-12): a static light's `cookie` is a `resources.textures`
index (it was a ref). Mesh parts carry `lights` (the static lights reaching
the part) and `light_sets` (switchable sets of static lights; light one at
a time). WMO light-set lights (`MOLS` spot, `MOLP` point) are static
lights with their owning `part`; a spot light's cone may lack
`falloff_exponent`.

Every document is one of five kinds. A **material** (REFACTOR/MATERIALS.md)
describes a surface as layers over textures and inputs; a character
model's skin, hair and eyes are composite materials its customization
choices bind textures into. A **world** (one per map) holds the
tile grid. A **model** is an asset in its own frame; terrain tiles, M2s
and WMOs are all models. A **placement set** places models and effects.
An **effect** is a behaviour with declared inputs (`REFACTOR/EFFECTS.md`). A tile is now
a model in its **own frame**: the origin is its corner vertex, the axes
are the world's, and heights are unchanged. The world places each tile by
translation alone. A tile's doodads and WMOs are two placement-set
documents, no longer an inline array. WMOs are full model bundles: same
mesh layout as an M2, plus their own doodad sets.

## 2. Layout `husk export-world` writes

```
<out>/maps/<map>/<map>.world.bundle/manifest.json    world: grid, off-grid placements, map volumes
<out>/maps/<map>/<map>.world.bundle/sets/<n>.json    off-grid placement sets (WMO-only maps)
<out>/maps/<map>/<tile>.bundle/manifest.json         model: resources.terrain
<out>/maps/<map>/<tile>.bundle/{terrain,liquid}.bin
<out>/maps/<map>/<tile>.bundle/sets/0.json           placement set "doodads"     (M2s)
<out>/maps/<map>/<tile>.bundle/sets/1.json           placement set "map_objects" (WMOs)
<out>/models/<fdid>.canon.bundle/manifest.json       model: M2 or WMO (a WMO has its own sets/)
<out>/textures/<fdid>.dds                            ground textures, shared
<out>/animation_fallbacks.json                       animation_fallbacks: every model's (with --db2-dir)
```

Every path inside a document is relative to that document's own file,
including set documents: a tile set's model `uri` is
`../../../../models/...`. Never build a path from a convention; follow
the `uri`s.

**Don't discover tiles by listing the map directory.** Start from the
world document. The WoW store keeps stale tiles that their map's WDT no
longer lists: 11,014 on the 12.1.0 mount, 159 of them in azeroth (1,335
files, 1,176 tiles). husk now skips them, but an output directory
exported before 2026-10-11 still holds their bundles. The world grid is
the list of tiles. The current loader iterates `maps/<map>/` and parses
names (`world_loading.cpp` ~line 1161); that has to go.

## 3. Read order

1. `maps/<map>/<map>.world.bundle/manifest.json`: check `kind == "world"`.
2. For each `grid.cells[k]`: tile x = `i`, tile y = `j`. The tile document
   is `references[cell.tile].asset.uri`. A reference without `uri` means
   the tile didn't export: a hole, logged in husk's `errors.log`.
3. The tile's world placement is `grid.origin + (i · grid.axis_i + j ·
   grid.axis_j) · grid.cell_size`. That equals the old chunk-0 origin,
   which is the north-west corner: world (32 − j, 32 − i, 0) · 533.333 yd.
4. Tile placement sets come from `references` entries with
   `kind == "placement_set"`; `definition.placement_sets[n].reference`
   says which reference is set n.
5. Each instance's model is `set.references[instance.reference].asset.uri`.

The world's `placements` list off-grid sets the same way, in the world
frame. Today only maps without terrain have one: 228 maps, each with the
single WMO its WDT places. MantleCore currently has no path for these
maps at all.

## 4. Key map, by loader function

Blob slices are unchanged: `file`, `byte_offset`, `byte_length`,
`component_type`, `component_count`, `count`. A slice may now also carry
`semantic`, and objects are closed, so an unknown key is a husk bug.
`endianness` is gone from the manifest: `schema/common.schema.json`'s
BufferSlice is defined as little-endian.

### `Tile()`

| Old | Now |
|---|---|
| `manifest.kind == "terrain_tile"` | `kind == "model"`, and `resources.terrain` present |
| `manifest.endianness == "little"` | gone (always little-endian) |
| `manifest.terrain` | `manifest.resources.terrain` |
| `terrain.tile_x` / `tile_y` | gone: the world cell's `i` / `j`. The tile's `subject.name` is `<map>_<x>_<y>`; `subject.id` is its FileDataID. |
| `terrain.frame == "wow_world_yards_x_north_y_west_z_up"` | gone: the contract header's `space` (yard, radian, `+z` up, right-handed), and the frame is tile-local (see 5) |

### `Terrain()`

| Old | Now |
|---|---|
| `chunk_vertex_layout == "interleaved_9x9_corners_8x8_centres"` | unchanged |
| `chunk_topology == "quad_centre_fan_4_triangles_holes_skip_quad"` | `"quad_centre_fan"` (same topology: 4 triangles per quad, a set `hole_rows` bit drops the quad) |
| `tile_size`, `chunk_size`, `quad_size` | unchanged |
| (implicit) | new: `chunks_per_side` 16, `quads_per_side` 8, `row_axis` (−1, 0, 0), `column_axis` (0, −1, 0) |
| `chunks.count`, `origin`, `heights`, `normals`, `hole_rows` | unchanged slices; `origin` is now tile-local (chunk 0 = (0, 0)) |

The loader's chunk-grid check (`northWest − (chunk / 16, chunk % 16) ·
chunkSize`) still holds with northWest = (0, 0). `tile.northWest` has to
come from the world grid (step 3), not from `origins[0]`.

### `Splat()`

| Old | Now |
|---|---|
| `textures[i].diffuse.texture_state == "resolved"` | `textures[i].diffuse` is the id of an entry of `resources.textures`; that entry's `state`: `resolved`, `known_unresolved` or `ambiguous` |
| `textures[i].diffuse.texture.uri` | the entry's `payload.uri` (with `payload.media_type`); its identity is the entry's `identity` |
| `repeats_per_chunk` | unchanged |
| `chunks.layers[c][k].texture_index`, `.alpha` | unchanged (`ground_effect_index` is new) |

The loader fails on any texture that isn't `resolved`. In real exports
that happens: a texture the listfile and store can't resolve is
`known_unresolved`, with an identity and no payload. Treat it like an
untextured layer rather than failing the tile.

### `Liquids()`

Renamed to row/column. The values are the same numbers: "row" steps −X,
"column" steps −Y.

| Old | Now |
|---|---|
| `chunk_grid_x` | `chunk_column` |
| `chunk_grid_y` | `chunk_row` (chunk index = `chunk_row · 16 + chunk_column`, as before) |
| `quad_rect.x` / `.y` | `quad_rect.column` / `.row` |
| `quad_rect.width` / `.height` | `quad_rect.columns` / `.rows` |
| `heights`, `quad_exists` | unchanged; heights count `(columns + 1) · (rows + 1)` |
| `height_source: "heightmap"` | `"stored"` (`min_height_level` and `zero` unchanged) |
| — | new: `liquid_type`, `liquid_setting` (DB2 refs, named when the liquid DB2s were read), optional `depth` |

New beside it: `resources.liquid_types` lists each named type once (name,
material, texture slots whose frames are ordinary texture entries, e.g.
Water's 30 animated `lake_a` frames), and `resources.terrain.liquid_settings`
each setting's flow. Match a liquid to its type by `identity`.

### `Placements()`: replaced

The old flat `terrain.placements` array, with `kind`, `unique_id`,
`position`, `rotation`, `scale` and `asset`, is gone. Now:

| Old | Now |
|---|---|
| entries with `kind == "model"` | instances of set 0 (`doodads`, M2s) |
| entries with `kind == "map_object"` (skipped today) | instances of set 1 (`map_objects`, WMOs): real model bundles now, see below |
| `unique_id` | `instance.id` (the ADT uniqueId, map-wide) |
| `position` | `instance.translation`, **tile frame**: add the tile's grid placement |
| `rotation` (x, y, z, w; tile axes are world axes) | `instance.rotation`, same convention |
| `scale` | `instance.scale` |
| `asset.uri` | `set.references[instance.reference].asset.uri` |
| — | `instance.selection.placement_sets`: for a WMO, which of its own doodad sets to show beyond its always-on ones |

**Cross-tile dedup can no longer be exact.** An object both tiles list
has the same id, rotation and scale in both (bit-identical), but its
world position is tile offset + local translation, computed from each
tile's own copy. Measured on "stormgarde keep": all 121 shared ids
differ, by up to 1.3 mm. The loader's `memcmp` pose check in
`Instances()` would reject every one. Compare positions with a tolerance
(2e-3 yd covers it), or by id alone.

### `Model()`

M2 and WMO bundles share one layout.

| Old | Now |
|---|---|
| `resources.mesh` | `resources.geometry.mesh` |
| `mesh.positions`, `normals`, `uv0`, `indices`, `primitives[].index_start/index_count` | unchanged |
| `primitives[].material_index` | `primitives[].material`: the id of an entry of `resources.materials` |
| `materials[m].layers[].texture_state == "resolved"` | `materials[m].layers[].texture` is the id of an entry of `resources.textures`; that entry's `state` |
| `layers[].texture.uri` | the entry's `payload.uri` |
| `layers[].alpha_fade` | two curves: `layers[].color_alpha` (M2Color.alpha) and `layers[].texture_weight` (M2TextureWeight); both may be set, combine them as the renderer sees fit |

A WMO bundle also has:
- `definition.placement_sets`: its doodad sets, each a document under
  `sets/`. Set 0 is always on; an instance's selection adds others.
- `resources.geometry.mesh.parts`: groups, with bounds, portals, and the
  volumes each uses. `interior_fogs` are `{set, id}` refs to effect
  instances in the doodad sets, the fogs a viewer inside the part sees
  (`REFACTOR/EFFECTS.md`; a fog is placed by its set, its look is the
  instance's `style`); `ambient_color_volumes`
  and `ambient_particle_volumes` are the same. `resources.scene.default_fog`
  is the fog a part falls back to.
- `resources.geometry.mesh.attributes`: per-face flags and material, which
  select collision.
- `resources.geometry.liquids`, and `resources.scene` lights.
An M2's particle and ribbon emitters are no longer `resources.scene` lists:
they are effect instances anchored to their bones, in an `effects`
placement set written inline in the manifest (`references[i].document`).

A doodad inside a WMO is placed in the WMO's frame, so its world
transform is tile ← WMO instance ← doodad instance. Following the
references recursively covers it; guard against cycles.

## 5. The tile frame

Heights, liquid heights, and positions inside a tile are all in the
tile's frame: the old world value minus the tile's corner (X and Y only;
Z is untouched). To reproduce the old world-frame numbers, add the
grid placement from step 3 to every terrain vertex, liquid vertex,
placement translation and tile bound. Rotations don't change: the tile's
axes are the world's.

Precision gets better rather than worse. Values inside a tile are now at
most ~600 yd instead of ~17,000, so float positions keep about 30× finer
resolution. Do the tile-offset addition in double, or in the
engine-from-tile transform, not in float on every vertex.

Seams: every tile still carries its own copy of its edge vertices
(`definition.terrain.edge_vertices: "duplicated"`). Two tiles' copies
agree to 3e-5 yd in position and exactly in height on azeroth 32_49/32_50.
The larger mismatches the seam check found on 2026-10-09 are still open
on husk's side (`BUNDLE_FORMAT.md` "Seams between tiles").

## 6. New in the world document, not read yet

- `grid.cells[].bounds_min/max`: each tile's world-frame box (terrain and
  liquids), so a tile can be found under a position without opening it.
- The map's volumes: two of `placements`' sets, `fogs` (`_fogs.wdt`) and
  `particulate_volumes` (`_mpv.wdt`), effect instances in the world frame
  (`REFACTOR/EFFECTS.md`).
- Outdoor lighting: the `zone_lights` set (was `zone_fogs`; effects
  `classic_zone_light`/`_global`, were `classic_zone_fog`/`_global`).
  Each instance's `style.light.zone_light` maps a condition (clear,
  clear_underwater, storm, storm_underwater, death) to the id of an entry
  of the set's `resources.light_conditions`. An entry holds its `settings` (sky
  model, glow, water opacity, sun, hide_sun/moon/stars; absent for
  1,196 ids the build's LightParams file lacks) and `keys` by time of
  day: direct/ambient/horizon/ground colours, `sky`, `clouds`, `water`,
  `shadow_opacity`, `color_grading` (a 1024x32 LUT, index into the
  set's `resources.textures`) and `fog` (the old cycle key without
  `time`). Blend between a zone's inner and outer radius, and between
  keys by time.
- `subject.id`: the WDT's FileDataID. Every document's `subject` now
  carries its FileDataID when husk knows it, M2 bundles included.
- `zones` (schema 0.2.2): named, nested regions as the cells each covers
  (`BUNDLE_FORMAT.md` "World documents"), for loading or streaming a
  world by zone. **0.3.0 (breaking): every zone has an `id` (UUID) and
  `parent` is the parent's `id`, no longer an array index.** Build a
  map from `id` once if you walk the nesting.

## 6a. Worlds authored in Blender

The Blender addon (`blender/addons/husk_addon`,
`BLENDER_WORLD_AUTHORING.md`) writes the same document kinds, and a
loader that reads sizes from the documents reads its output unchanged.
What can differ from a husk-written world:

- `grid.cell_size` other than 533⅓ (a re-diced or scaled world), with
  each tile's `tile_size`/`chunk_size`/`quad_size` following it: read all
  four from the documents, never a constant. Cell indices can fall
  outside 0..63.
- A third tile placement set, `authored`: instances made in Blender, ids
  in their own space (not ADT uniqueIds), unique across the world.
- Baked tiles have no texture layers, ground effects, liquids or coarser
  levels (`lod.max_level` 0); their `provenance.sources` lists those as
  unavailable. A tile subject without an id (`{"kind": "none"}`) is a
  cell that has no source tile.

## 7. Check

Before changing the loader, validate the export it reads. Each bundle
passed is checked along with its own placement sets:

```
husk validate-bundle <out>/maps/<map>/*.bundle     # the world and every tile
husk validate-bundle <out>/models/*                # M2 and WMO models
```

A small real map for a first pass: "stormgarde keep" (9 tiles, 2,931
model bundles incl. 40 WMOs, no water; `husk export-world <maps> <out>
--map "stormgarde keep" --listfile ... --listfile-root ...`, ~2.5 min
cold). It validates clean: 28 tile/world/set documents and 3,005 model
documents, 0 problems. azeroth 32_49 (`export-terrain`) has 73 liquid
instances to test liquids against.
