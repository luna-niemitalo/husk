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
| `textures[i].diffuse.texture_state == "resolved"` | `textures[i].diffuse` is an index into `resources.textures`; that entry's `state`: `resolved`, `known_unresolved` or `ambiguous` |
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
| — | new: `liquid_type`, `liquid_object` (DB2 refs, named when the liquid DB2s were read), optional `depth` |

New beside it: `resources.liquid_types` lists each named type once (name,
material, texture slots whose frames are ordinary texture entries, e.g.
Water's 30 animated `lake_a` frames), and `resources.terrain.liquid_objects`
each object's flow. Match a liquid to its type by `identity`.

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
| `mesh.positions`, `normals`, `uv0`, `indices`, `primitives[].index_start/index_count/material_index` | unchanged |
| `materials[m].layers[].texture_state == "resolved"` | `materials[m].layers[].texture` is an index into `resources.textures`; that entry's `state` |
| `layers[].texture.uri` | the entry's `payload.uri` |

A WMO bundle also has:
- `definition.placement_sets`: its doodad sets, each a document under
  `sets/`. Set 0 is always on; an instance's selection adds others.
- `resources.geometry.mesh.parts`: groups, with bounds, portals, and the
  volumes each uses. `fog_volumes` are `{set, id}` refs to effect
  instances in the doodad sets (`REFACTOR/EFFECTS.md`; a fog is placed by
  its set, its look is the instance's `style`); `ambient_color_volumes`
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
  clear_underwater, storm, storm_underwater, death) to an index into the
  set's `resources.light_params`. An entry holds its `settings` (sky
  model, glow, water opacity, sun, hide_sun/moon/stars; absent for
  1,196 ids the build's LightParams file lacks) and `keys` by time of
  day: direct/ambient/horizon/ground colours, `sky`, `clouds`, `water`,
  `shadow_opacity`, `color_grading` (a 1024x32 LUT, index into the
  set's `resources.textures`) and `fog` (the old cycle key without
  `time`). Blend between a zone's inner and outer radius, and between
  keys by time.
- `subject.id`: the WDT's FileDataID. Every document's `subject` now
  carries its FileDataID when husk knows it, M2 bundles included.

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
