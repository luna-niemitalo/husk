# BLENDER_WORLD_AUTHORING.md — editing a husk world in Blender

Asked for by Luna 2026-10-17. The sections below are the design; this one
says what is built.

## Current state (2026-10-17, MVP)

`blender/addons/husk_addon` (Blender 5.1; add `blender/` as a Script
Directory in Preferences, enable "husk world authoring", sidebar tab
"husk"). Checked by `tests/world_authoring_check.py` (headless, on
azeroth 32_49 + 32_50 from the mounted store):

- **Import**: Grid + cell boxes, the merged Terrain mesh (tiles welded,
  vertices snapped onto the bake's lattice), placements as collection
  instances, assets (level-0 mesh, first UV set, base colour), WMO doodad
  sets as child empties, the world's own sets (fogs, volumes, zone
  lights) as empties.
- **Scale world** (operator): terrain and placement positions scale about
  the scene origin or the 3D cursor; props keep their size.
- **Preview bake** of the selected cells; **Export world** with every
  cell of the current dicing baked.
- Verified: an unedited round trip gives back heights within 2.3e-5 yd,
  identical holes, and all 2,450 placements whose origin is in a loaded
  cell with their ids (position within 7e-4 yd). A 20x scale writes 911
  tiles in ~45 s with exact seams; after a subdivide-and-ripple edit the
  baked heights match direct ray casts of the mesh within 5e-4 yd. Every
  written document validates (`husk validate-bundle`, 0 problems).

Different from the design below, on purpose or not yet:

- Baked tiles never carry texture layers, ground effects, liquids or LOD,
  even on the source dicing (Luna: losing textures is fine for now).
  Liquids aren't imported; the terrain has no material.
- Export is world-only. Unloaded source tiles are kept as they are
  (`source_tiles` = Auto) only on the source dicing of an unscaled world;
  Keep/Drop override.
- A placement is listed once, in its origin's cell; the source lists it
  in every tile it overlaps. A loaded tile's placement standing in a kept
  neighbour is left to that neighbour; moved into one, export refuses.
- Effect instances (fogs, volumes, zone lights) move with a scale, but
  their sizes and radii don't.
- Area per chunk is the source area under the chunk's middle.
- Not built: cell export, the overhang count, adding an owned set back to
  a WMO instance from the UI (deleting its child empty removes one).
- The node tree's raycast is unreliable for a sample exactly on the
  mesh's outer edge; export re-casts every miss from Python
  (`bake.TerrainProbe`), the preview doesn't, so the preview can show
  such edge samples as missing where export has them.

## The idea

Open a husk `world` document in Blender, load some of its tiles, edit
terrain and placements freely, and export back to husk documents.
Blender is the editor, the bundle stays the format: the addon reads and
writes `model` (terrain tile), `placement_set` and `world` documents,
and nothing else. It never calls husk and never looks anywhere a
manifest doesn't name (`BLENDER_ADDON.md` "What the addon must not do",
I3).

| Bundle concept | In Blender |
|---|---|
| World grid (`grid`: `origin`, `cell_size`, axes) | a `Grid` empty carrying the dicing; one box empty per cell under it |
| Terrain tiles (one `model` per cell) | **one** free `Terrain` mesh, editable like any mesh |
| What each tile will contain | a live **bake preview**: the heightfield export will write, drawn per cell |
| Placement-set instances | one empty per instance, drawing its asset by collection instancing |
| Placed assets (M2, WMO model bundles) | one hidden collection per asset under `Assets` |

**The terrain mesh is free; the dicing bakes it** (Luna, 2026-10-17).
Edit mode works without restrictions: move, add, subdivide, delete,
sculpt. A tile is a heightfield sampled at fixed points, so export
samples the mesh straight down at each point of the chosen dicing and
writes those heights. The dicing (`cell_size`, origin, which cells) is
chosen at export time and may differ from the one loaded.

The rule that makes export predictable: **what a cell exports is what
its box's preview shows.** Terrain under the box, sampled at that dicing,
is the tile's heightfield; an empty whose origin is inside the box is
that tile's placement.

## Scene layout

```
husk_world:<map>                 collection; root props: world subject, source dir, provenance
  Grid                           empty; props: origin, cell_size, axis_i, axis_j, scene_offset
    cell_32_49                   empty, CUBE display, box = cell footprint x terrain height range
    cell_32_50
  Terrain                        mesh object, every loaded tile welded into one surface
  BakePreview                    mesh object, Geometry Nodes output: the heightfield per cell
  Liquids                        one object per tile liquid, carried through (see Ownership)
  Placements
    doodads                      collection per set name; each child an empty
      d_123456                   empty; instance_collection = Assets/m2_<fdid>
    map_objects
      w_789                      empty; instance_collection = Assets/wmo_<fdid>
        w_789.set_1              child empty per active doodad set; instances Assets/wmo_<fdid>.set_1
    authored                     instances made in Blender: own id space (see Export, Ids)
    fogs, particulate_volumes    world-level effect sets (off-grid), empties, SPHERE display
  Assets                         excluded from view layer
    m2_<fdid>                    the model bundle's mesh + base materials, native asset space
    wmo_<fdid>                   WMO mesh only
    wmo_<fdid>.set_<k>           that WMO's doodad set k, as empties instancing m2_ collections
```

Every husk object carries its source facts as custom properties
(`husk_*`): ids, `Ref`s with `name_source`, flags, tint, raw `source`/
`unresolved` blocks. They are deep-copied plain values, never live views
(`BLENDER_ADDON.md` "Carry forward": the ID-property segfault).

## Frame

One Blender unit is one yard; Blender's axes are WoW world space's
(+X north, +Y west, +Z up, right-handed — no conversion). The addon
reads every document's `space` header and refuses one it doesn't match,
printing expected and actual.

World coordinates reach ±17,067 yd, where a 32-bit float has ~1 mm steps.
To keep editing precise the scene is shifted by `scene_offset` (the
first loaded cell's origin, stored on `Grid`), and export adds it back.

## The dicing

- `Grid` holds the dicing: `origin`, `cell_size`, axes, and the set of
  cells. Import sets it to the loaded world's grid. Changing it is
  re-dicing: the cell boxes and the bake preview follow at once.
- A cell is always the tile shape the format defines: 16×16 chunks of
  8×8 quads, so the sample spacing is `cell_size / 128` (4.17 yd at the
  ADT's 533⅓). The schema already takes any `cell_size`/`chunk_size`/
  `quad_size`; a finer or coarser dicing just means bigger or smaller
  tiles, never a different tile structure.
- Cell boxes are transform-locked and placed from `(i, j)` by the grid
  formula (`BUNDLE_FORMAT.md` "World documents"); their height is the
  baked terrain's range in that cell.
- Which cells exist: import's cells, plus **Fill cells** (every cell the
  terrain mesh overlaps) and add/remove by hand. A cell with no terrain
  under it is a hole in the world grid, as today.
- Panel operators: **Select cell contents** (the placement empties that
  cell would export), **Export cell**, **Export world**, **Load cells…**
  (by list, rectangle, or radius around a cell), **Fill cells**.

### Scaling the world

Scaling is just an edit: scale the terrain and the placements in
Blender (select all, scale about any pivot), then dice. An instance's
scale composes with its asset's native size, so asset bundles are never
re-exported. Whether the dicing should scale with it is your choice of
`cell_size`; nothing ties them.

## The bake

The bake is one function from (terrain mesh, dicing) to per-cell
heightfields. It runs in a Geometry Nodes tree on `BakePreview`, whose
inputs are the `Terrain` object and the `Grid`'s dicing; export reads the
**evaluated** preview mesh and writes it. There is one implementation of
the bake, and the viewport is it: what you see is byte-for-byte what
exports.

Per cell, per chunk, at each of its 145 sample points (9 corners, 8
centres per row pair):

1. **Height**: a ray straight down (−Z) from above the terrain's bounds;
   the first hit's Z. A heightfield holds one height per point, so an
   overhang or cave keeps only its top surface. The panel shows how many
   samples hit more than one surface, so a lost overhang isn't silent.
   Overhangs belong in a WMO or a model.
2. **Holes**: a quad is a hole when its centre sample misses the mesh
   (there is terrain-less ground there, e.g. faces you deleted). Hole
   granularity is a whole quad.
3. **Missed corner samples on a non-hole quad** (a mesh edge running
   through the quad) take the nearest hit sample's height in that chunk,
   so the stored grid stays complete.
4. **Normals**: the mesh's interpolated smooth normal at the hit point,
   so detail finer than the sample spacing still shades.
5. **Seams**: a border point is one sample, written into both tiles, so
   baked tiles always meet exactly — none of the source cracks
   (`BUNDLE_FORMAT.md` "Seams") survive a bake.
6. **Bounds**: recomputed per cell into the world grid.

When the dicing equals the source grid and the mesh is unedited, each
sample hits a vertex, and the bake returns that vertex's height. A hit
within float tolerance of a vertex takes the vertex's Z exactly, so an
untouched round trip reproduces the source heights.

## Import

1. Read the world document. Build `Grid` with the world's dicing and
   boxes for the chosen cells.
2. **Terrain, merged.** Each tile's chunks become one mesh: corners and
   centres as vertices, each quad four triangles fanned around its
   centre, holes left out. Tiles store their border vertices twice
   (`edge_vertices: "duplicated"`); import welds them into one vertex,
   taking the first-loaded tile's height (`BUNDLE_FORMAT.md` "Seams").
   Splat layers become a preview material (the probe in
   `tests/terrain_bundle_import_check.py` already builds one).
3. **Placements.** Each instance of each tile's sets becomes an empty
   with the instance's TRS (tile frame + tile translation −
   `scene_offset`). An instance id seen in an earlier tile's set is the
   same object (uniqueIds are map-wide): one empty, with
   `husk_listed_in` recording every cell that listed it.
4. **Assets.** Each distinct asset `uri` is imported once into
   `Assets/` (mesh + base-colour materials, the
   `tests/bundle_import_check.py` path). An asset with no `uri` (husk
   knew it, couldn't produce it) gets a named placeholder box: a visible
   gap, never silently missing.
5. **WMO doodad sets.** A WMO instance's `active_sets` become child
   empties instancing that WMO's set collections. Turning a set on or off
   is adding or deleting a child. The sets' own contents are the WMO's
   definition, read-only here (PLACEMENT_SETS.md invariants 4–5).

### Why collection instancing, not a Geometry Nodes tree

Agreed with Luna 2026-10-17. The request was "empties that get stuff
instanced on them with geo nodes, picking the mesh from another
collection". Blender's native collection instance
(`empty.instance_collection`) is exactly that mechanism without a node
tree: each instance stays its own selectable, movable object with its
own properties, which is what export needs to read back, and the
depsgraph draws thousands of them as instances. GN stays the right tool
for the bake (above) and ground cover (`ground_effects`, rules not
instances, later).

## Export

Export writes a **new export tree**; it never overwrites the source
tree. Every exported tile is a full, schema-conformant tile document,
not an overlay of edits on the source tile (Luna, 2026-10-17): a
consumer reads it like any husk-written tile. A source document export
doesn't rewrite (an unloaded cell on an unchanged dicing, a model
bundle) is referenced back into the source tree by relative `uri`
(allowed: `BUNDLE_FORMAT.md` "Left to implementation judgment", the last
paragraph).

**Export cell (i, j)** writes that cell's tile bundle and its set
documents, and requires the dicing to be the source's (a re-diced cell
has no neighbours to sit between). **Export world** writes the world
document plus every cell of the dicing. With the source dicing,
unloaded cells keep their source tile references; with a new dicing
there are no source tiles to keep, so every cell is baked from what is
loaded, and cells outside it are holes.

1. **Terrain**: the evaluated bake preview, per cell.
2. **Placements by position.** Each instance empty belongs to the cell
   containing its origin, and is written in that tile's frame. Moving an
   empty across a border moves it to the other tile. It is also listed in
   every other cell its asset's box overlaps, which is the source
   convention import collapsed (assumption to verify, see Gate).
3. **Ids: hand-made instances get their own id space** (Luna,
   2026-10-17). Source instances keep their source id and set
   (`doodads`/`map_objects`, ADT uniqueIds) wherever they move. An
   instance made in Blender (added, or duplicated from a source one)
   goes in a third set, `authored`, whose ids are husk's own: unique
   across the whole world (so an authored instance listed in two cells
   is still one object), allocated above the highest `authored` id in
   every tile of the world, read at export time, loaded or not. One set
   holds both M2s and WMOs, since it is one id space. A duplicate is
   recognised by its copied `husk_id` on an empty whose object name
   isn't the one import gave it; it is moved to `authored` and its
   source id dropped.
4. **Per-instance data that goes stale on a move** is kept and labelled,
   not dropped (PLACEMENT_SETS.md): `tint` stays; `distant` bounds are
   recomputed from the asset's box when the instance moved.

## Ownership — what is baked, what passes through

A tile's other contents are tied to its source tile's chunks. They pass
through unchanged only while the dicing is the source's; on a new
dicing each is either baked from the scene or left out, and the tile's
`provenance.sources` says which.

| Data | Source dicing | New dicing |
|---|---|---|
| Heights, holes, normals | baked | baked |
| Placements, WMO active sets | edited, by position | edited, by position |
| Splat layers, alpha maps | passed through | **left out** in v1 (untextured); baked in v2 (below) |
| Ground effects | passed through | follow the splat layers they belong to |
| Liquids (`MH2O`) | passed through | **left out** in v1; baked in v3 |
| LOD (`_lod.adt` bands, map textures) | passed through, **stale if heights changed** — flagged | left out (`max_level` 0) |
| WMO doodad set contents, model contents | read-only | read-only |
| World off-grid sets (fogs, volumes) | edited TRS; inputs passed through | same |

Provenance of an exported document names the addon as producer and the
source documents as its input, and lists what was edited, baked and
left out.

## Stages

1. Grid, free terrain, bake preview, placements, export on any dicing
   (heights, holes, normals, placements).
2. **Splat bake.** Import paints each ground texture's weight into a
   world-space image (planar XY, at the alpha maps' 64-per-chunk
   resolution); the bake samples them at each alpha texel of the new
   dicing, keeps a chunk's 8 heaviest textures (the format's limit) and
   renormalises. The images are also the way to paint terrain.
3. **Liquid bake.** Liquids become one free mesh per liquid type,
   sampled like the terrain at each chunk's liquid vertices.
4. LOD regenerated from the baked heightfield instead of left out.

## Package

The first real part of `BLENDER_ADDON.md`'s `blender/addons/husk_addon/`,
sharing its one reader:

```
blender/addons/husk_addon/
  __init__.py      registration
  docs.py          document + BufferSlice read/write, space check, uri rebasing
  grid.py          Grid/cell empties, dicing, cell math
  terrain.py       the chunk layout and lattice; tiles -> merged mesh (weld)
  bake.py          the node tree, sample meshes, batched evaluation, Python re-cast
  placements.py    sets <-> empties, ids, cell assignment
  assets.py        model bundles -> Assets/ collections, placeholders
  world_io.py      import, scale, export: the output tree
  ops.py           operators and the sidebar panel
```

## Gate

Headless, like the existing `tests/*_import_check.py` probes, on
`azeroth_32_49` + `azeroth_32_50` (both in `test_data/`):

- **Round trip identity**: import both tiles, export the world unedited
  on its own dicing. Every exported document validates (`husk
  validate-bundle`) and equals its source: heights, holes, every set's
  instances and their per-tile listing. Normals equal within a stated
  tolerance (the mesh's smooth normal isn't the source's stored one;
  measured, not assumed). This is also where the overlap-listing
  assumption in Export step 2 is checked.
- **Edit**: raise a vertex on the shared border; both tiles get the new
  height and the seam check passes.
- **Free edit**: subdivide and displace a patch; the baked heights equal
  a direct `ray_cast` of the mesh at the sample points (an independent
  check on the node tree).
- **Re-dice**: export at `cell_size` 266⅔; four tiles per source tile,
  every one validates, seams exact.
- **Move**: move one doodad across the border; it leaves one tile's set
  and enters the other's, with its id.
- **Authored ids**: duplicate a source doodad; the copy lands in
  `authored` with a fresh id, the original keeps its uniqueId.
- Luna's own pass in the GUI is the visual gate.

## Later, not in the stages above

- Ground-cover preview scatter via Geometry Nodes from `ground_effects`.
- Editing a WMO's own doodad sets in place.

## Consumers

What MantleCore sees from an authored export is in
`MANTLECORE_MIGRATION.md` §6a.
