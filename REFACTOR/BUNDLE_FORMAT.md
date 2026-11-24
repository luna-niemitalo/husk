# BUNDLE_FORMAT.md — the native husk bundle

**Target, not current state.** See `REFACTOR/README.md` for the pipeline and the
invariants referenced by number below. **`CANONICAL_FORMAT.md`** (repo root) is
now the canonical conceptual explainer of `canon::`/the bundle format,
including the general shape/BufferSlice/Ref schema; this file stays the
migration-specific engineering record — the invariant-by-invariant
rationale, the decisions made and rejected along the way, and the parts
still open. Where this file's own schema sketches would just repeat
`CANONICAL_FORMAT.md`'s, they've been trimmed to a pointer instead.

## Why a native format at all

glTF was husk's first output, and paying its impedance cost real structure:
geosets shipped as fake skeleton joints, semantic data smuggled through
`extras` onto a heuristically located carrier bone, a physics joint graph
deliberately truncated to fit, and no schema version anywhere (`AUDIT.md`
§3). That pipeline is deleted.

Per Canonical Data Model §13, glTF is a *projection* of the recovered semantics, not
the semantics. The bundle is what the canonical model actually writes; glTF is
`export --glb`'s optional lean projection, which omits what it cannot express.

## Shape

A bundle is a **directory**. No archive variant, no single-file mode — settled,
not an open question. See `CANONICAL_FORMAT.md` §5 for the directory sketch
and the `BufferSlice`/`Ref` JSON shapes — not repeated here.

Binary payloads are separate files rather than a packed blob so that a human can
open one on its own and a diff can show which payload changed. Every one of them
is subject to I8, which sets two tests: husk must have a verb that renders it
human-readable, **and** it must not be a headerless dump that only husk can make
sense of. The manifest itself is always JSON.

`mesh.bin` / `skeleton.bin` / `animation.bin` as sketched above pass the first
test and have not yet been held to the second — the names are a placeholder from
before I8 was written down. Deciding what open container carries geometry,
skeleton and curve data is a **stage-4 question**, not a blocker for stages 1–3,
and it should be answered the same way the texture case was: find the container
that holds husk's own payload without transforming it, and prefer one a public
tool already opens. It is recorded here so the placeholder is not mistaken for a
decision.

**Resolved 2026-09-06, superseding the paragraph above.** Luna's direct call:
zero glTF/tinygltf dependency for reading this format, not even reusing
glTF's own buffer/accessor JSON shape. `mesh.bin`/`skeleton.bin`/
`animation.bin` stay headerless raw bytes (native little-endian, fixed-width
arrays), and `manifest.json` alone gives them meaning via a small repeated
JSON shape — a **BufferSlice** — for large uniform numeric arrays, with
small per-item structural data (geoset ranges, joint names, billboard
modes, material layer descriptions) staying plain inline JSON instead. This
passes I8's second test without adopting any container format at all: the
"open container" is JSON-describes-byte-range, something `fread`/`mmap`
plus any JSON library already opens, in any language, with no husk binary
and no glTF library involved. Implemented in `src/writers/bundle_writer.hpp`
(`husk::writers::writeBundle`); the manifest shape itself is defined by
`schema/` (see "Document kinds and the contract header" below), not by
this document. Covered by `tests/test_writers_bundle.cpp`, which checks
every written manifest against the schema.

**I3, restated concretely**: a consumer opens `manifest.json` and never looks
anywhere else. Every path in it is relative to the manifest. There is no
"textures directory" parameter, no environment variable, no sibling-directory
convention to know, and no reason to invoke husk.

An `--appearance` item's model bundle (`items/<fdid>.bundle/`, in
`references`) works exactly this way — husk resolves once, at write time, and
bakes in a relative path.

## Physical storage shape — settled 2026-09-03

**Decision: loose files, one directory per bundle, unchanged.** ZFS (this
project's actual filesystem, `CLAUDE.md`'s Machines section) already dedups
shared payload bytes at the block level, which was the concrete cost this
question raised — a content-addressed backing store would buy the same
byte-dedup this filesystem already gives for free, at the cost of a real
extra layer (a store, an indirection from manifest reference to hash, a
second thing to keep consistent) for no corresponding win today. Revisit if
the file-count/metadata half (dnode count, snapshot bloat, traversal cost —
the half ZFS dedup does *not* address) is ever measured and found to actually
bite; not worth the friction pre-emptively. The investigation below is kept
as a record of the tradeoff, not as still-open work.

<details>
<summary>Superseded reasoning (kept for context, not current guidance)</summary>

Design-review pass, 2026-09-03. "Shape" above was marked settled, but the
settlement was reasoned without measuring the cost it trades away. Recorded
here as reopened rather than silently overwritten.

**The claim**: one directory per bundle, at this project's actual corpus scale
(130k+ models, each a manifest + several `.bin`s + N textures + nested `aux/`),
produces a many-small-files problem this project already treats as real
elsewhere (`CLAUDE.md`'s own `find /` hazard note; the corpus-scan tooling's
own directory-shape pathologies). "Shape" asserts explorability/diffability
without weighing this — unlike every other tradeoff in this file (contrast the
DDS decision's weighed-constraints table below).

**Where the discussion landed, not yet decided**:

- A single opaque archive-per-bundle (TOC + seek to one member) fixes file
  count but loses more than it gains: it forecloses dedup of the resources
  this corpus reuses constantly (shared base meshes/skeletons/textures across
  race/gender variants and item recolors) — an archive-per-bundle physically
  duplicates that content once per referencing bundle, worse than today's
  loose-file shape, which at least lets a content-aware layer dedup it. ZFS
  (this project's actual filesystem) already dedups bytes at the block level,
  independently lowering urgency on the *byte-duplication* half, but does
  nothing for the *file-count/metadata* half (dnode count, snapshot bloat,
  `find`/traversal cost — the half actually measured in this project's
  hazards).
- Stronger direction floated: separate *physical storage* from *logical
  grouping* — a content-addressed backing store (payloads keyed by hash,
  deduped, few large files) with per-bundle manifests referencing into it by
  hash, git/Nix-shaped. Bounds file count by unique-payload count rather than
  `models × files/model`, keeps per-payload diffability and dedup, and still
  gives "random access to one member" (fetch by hash) without a bundle-scoped
  TOC.
- Any container/index format here is still subject to I8: a bespoke
  husk-only seekable-archive format recreates the exact "proprietary
  container, husk-only reader" problem I8 exists to prevent for BLP, one
  layer up. Prefer something with existing public tooling (sqlite-as-blob-
  store, or zip with stored/deterministic entries) over inventing one.

**Left to investigate, not solved by the above**:

- An actual measurement — real file count and real duplicate-byte volume, at
  real corpus scale, for the directory shape as currently sketched. This
  section is missing the number that would settle it.
- Whether content-addressing lives at corpus scope (one store shared by every
  bundle) and how that interacts with "Bundles reference other bundles" /
  "Left to implementation judgment" above (nesting vs. shared `textures/`) —
  this may subsume that open question rather than sit beside it.
- Concrete container/index format, chosen against I8 and against real
  read/write-pattern needs (sequential export-time writes vs. random
  Blender-import-time reads).
- Interaction with "Embed or reference — one rule" below: does
  content-addressing change what "inline" means, since an inline payload
  would now also conceptually be a hash-addressed object?

</details>

## Every reference is a `Ref`, never a bare name

This is the rule that makes the bundle traceable — see `CANONICAL_FORMAT.md`
§5 for the JSON shape of a resource entry (`id`/`name`/`name_source`, plus
`uri` when the payload is external rather than inline).

`name_source` is one of `m2_embedded` / `adt_embedded` / `wmo_embedded` / `listfile` / `db2` / `community` / `synthesized`
(I6). `community` is a community-maintained name table outside the client
(today: clip names from `--anim-names`, wow.export's list), as opposed to
`db2`, the client's own data. A ref with no name (most DB2 rows) carries only its `id`: `name` and
`name_source` appear together or not at all (2026-10-11). It exists because most naming in this project comes from the
community listfile — unreliable at best — or is invented by husk. A consumer
reading `"scalpupperhair00_08"` must be able to tell whether that came from the
model's own bytes, a DB2 row, or a community guess that may simply be wrong.

This applies uniformly: textures, aux item bundles, animations, customization
choices, materials, geosets — **and bone names**. Physics bodies, emitters,
correction sets and attachments name joints, and every joint name is
husk-synthesized or contextual, never authoritative. The raw joint index stays
alongside for exactly that reason.

## Document kinds and the contract header — agreed 2026-10-10

Every husk document is one of eight **kinds**, decided by what the document
has to do, not by which WoW file it came from:

| Kind | What it is | Examples |
|---|---|---|
| `model` | Content in its own local frame, placed by something else: an asset. | M2 doodad, item, character, WMO building, terrain tile |
| `placement_set` | No content of its own: instances of other documents, each with a transform and the selection that instance makes. | a WMO's doodad sets, a tile's doodads and buildings |
| `world` | The root: nothing places it. Holds the tile grid, off-grid placements and the objects owned by the place as a whole. | a map |
| `effect` | A behaviour with declared inputs, placed by placement-set instances that supply them (`EFFECTS.md`). | a WMO's fog spheres (`classic_local_fog`) |
| `material` | How a surface looks: image inputs, layers combined by named functions, and textures that may be composites (`MATERIALS.md`). | a character's skin atlas canvas |
| `technique` | How the source game computes one drawing step, as data (below, "Techniques"). | `m2_pixel_shader/Combiners_Mod_Add`, `framebuffer_blend/alpha` |
| `creatures` | A corpus's catalog of creatures: what kind each is and which model and preset draws each of its looks (below, "Creature catalogs"). | `creatures.json` of an export tree |
| `appearances` | A list of bodies presets point at (above, "Appearances"): one model's, inline in its `appearances` reference, or a tree's shared list. | `appearances.json` of an export tree; a model's inline list |

A character, an item, a WMO and a doodad are all models; they differ in
content, not kind. A WMO's doodad sets are its definition (what furniture
exists, where), and which sets are visible is a selection. A terrain tile
is a model whose surface is a heightfield, in its own frame, placed by the
world by translation. Tiles carry no neighbour references: adjacency is the
world grid's fact, and a tile's definition states only how its own border
vertices relate to an adjacent tile's. Map-wide fog and particle volumes
belong to the world, since a volume can span many tiles.

### The contract header

Every document opens with the same header, then its data:

```
$schema         the kind's schema `$id`: "https://husk.invalid/schema/<kind>.schema.json"
format          "husk-bundle", constant
kind            "model" | "placement_set" | "world" | "effect" | "material" | "technique" | "creatures" | "appearances"
schema_version  version of the schema/ definitions the document conforms to
subject         Ref: what this document describes
space           length/angle units, up axis, handedness of every position in it
provenance      producer, input_format, sources[] (per table/file: available or not)
definition      what exists and is valid for the subject
selection       what this instance chose; only ever indices into definition
references      other documents and assets this one depends on: [{ref, kind, uri?}]
--- data ---
model: resources {geometry, rig, materials, scene}
placement_set: instances
world: grid, placements, scene
effect: sources
creatures: creatures, displays
appearances: appearances
```

`$schema` (schema 0.4.0) names the schema a document follows, so a
consumer holding only the document knows which definition to open. It
is the schema's `$id`, not a path, because:
- a document's place in a tree varies (bundle manifests, `sets/`,
  `effects/`, `techniques/`, store objects deduplicated across trees), so
  a relative path to a shipped `schema/` copy would differ per document
  and break whenever a document is moved or mounted elsewhere;
- exports don't ship the schemas: they are embedded in the husk binary,
  and the pair (`$schema`, `schema_version`) names one file at one version
  of husk's `schema/` exactly, and `husk schema-doc` renders that set as
  one Markdown reference (below, "One reference file: `husk schema-doc`");
- `$schema` is the key editors and JSON Schema tools already read: mapping
  `https://husk.invalid/schema/*` to a checkout's `schema/` gives
  completion and validation in an editor. The `.invalid` host never
  resolves, so the id can't be mistaken for a download location.

`validate-bundle` requires `$schema` to equal the `$id` of the schema its
`kind` selects, and reports the expected and actual id when it doesn't.

`provenance.sources` is the absent-vs-unasked record (`CANONICAL_MODEL.md`
"Settled"): per table, because the real failure is one empty table among
good ones. There is no export time: a document's bytes depend only on its
inputs and the producer, so a corpus store keeps unchanged content once
across builds; the time is the stored build's (`REFACTOR/CORPUS_EXPORT.md`).

### Structural, not described: `schema/`

The format is defined by JSON Schema files in `schema/` (one per kind plus
shared definitions), embedded into the husk binary. That is the single
source of truth; prose like this file explains it but does not define it.

- **Instance check** (`husk validate-bundle`, and every test that writes a
  bundle): the JSON Schema, then what a schema can't say. That covers
  BufferSlice arithmetic, that files exist, `x-index-into` ranges,
  `x-id-in` references (the id must belong to an entry of the named array,
  and the array's ids must be unique), and referenced placement sets,
  validated recursively.
- **Stable ids**: see "Stable ids" below.
- **Schema lint** (a test over `schema/` itself) rejects known antipatterns:
  - no storage opinions: a key, enum value or constant naming a container
    or API (`dds`, `png`, `blp`, `glb`, `gltf`, `vk`, `bpy`, ...) fails.
    Linked data is a `Payload` `{uri, media_type}`, the media type a free
    string: the format says a face has a texture, delivers the type, and
    doesn't restrict it.
  - no source-format jargon in keys: keys are lowercase snake_case, and
    none may contain a source chunk tag (`mpvd`, `mogp`, `vfe2`, ...).
    Raw source data lives only under a `source` or `unresolved` object.
  - every property has a description, every top-level property an
    `x-layer` (`contract`/`definition`/`selection`/`resource`), every
    real-valued property an `x-unit`.
  - objects are closed (`additionalProperties: false`): an undocumented key
    written by a writer fails validation.
  - `uri` appears only in the shared `Payload` and asset-reference
    definitions.
  - kind-local `definitions` and object-valued `additionalProperties`
    (maps) are held to the same rules as properties.

### Where values come from: `x-source`

`x-source` says where a value's data is found: an array of one or more
origins, each an object of exactly one form.

| Origin | Meaning |
|---|---|
| `{"table": "AreaTable", "column": "Flags[0]"}` | a DB2 table's column (`column` optional when the whole row is meant, e.g. its id) |
| `{"file": "m2", "field": "M2Particle.gravity"}` | a field of a source file; `file` is one of `m2`, `skin`, `skel`, `bone`, `anim`, `phys`, `wmo`, `wmo_group`, `adt`, `adt_obj0`, `adt_obj1`, `adt_tex0`, `adt_lod`, `wdt`, `wdt_fogs`, `wdt_mpv`, `wdt_lgt`, `wdt_occ`, `blp`; `field` optional |
| `{"derived": "<rule>"}` | computed by husk by a named rule: the same snake_case vocabulary as `provenance_fields` (rules in `EFFECTS.md`, or named in the property's description) |
| `{"listfile": true}` | the community file list (names, path lookups) |
| `{"authored": "effects/<name>.json"}` | husk's own authored data, a path in its repository (effect and technique definitions) |
| `{"stated_by": "name_source"}` | the origin differs per value, and the named sibling property states it |
| `{"format": true}` | pure structure: header, ids, indices, counts the format itself defines |

`derived` rules that are not effect rules (`EFFECTS.md` "Derivation
rules") are named here:

| Rule | Computes | From |
|---|---|---|
| `texture_resolution` | a texture entry's state, identity, unresolved reason and candidates: the literal FileDataID, then the `--listfile` path, then same-basename candidates (`src/sources/texture_resolution.cpp`) | whatever names the texture (M2, WMO, ADT, DB2 column) |
| `m2_gravity_to_vector` | particle `gravity` as a vector: a scalar `g` becomes `[0, 0, -g]`, a packed direction key is unpacked (`m2::decodeParticleGravity`) | `M2Particle.gravity` track |
| `mfog_start_scalar_to_start` | linear fog `start` = `end` × start scalar | WMO `MFOG` layer |

Several origins mean the value combines them, or comes from whichever one
the source has. The lint rejects any other form, an unknown file kind or a
rule that isn't snake_case.

Rules `x-source` names outside `EFFECTS.md`:

| Rule | Computes | From |
|---|---|---|
| `map_tile_grid` | world `grid` `origin`, `cell_size`, `axis_i`, `axis_j`: the client's 64 x 64 tile map, tiles 533.33 yd, centred on the world origin, tile x stepping -Y and y stepping -X (`canon_terrain.hpp` `terrainTileCorner`) | the map layout every ADT assumes; no file stores it |
| `tile_terrain_bounds` | a world cell's `bounds_min`/`bounds_max`: chunk footprints from their origins, heights from the vertices and liquids | the tile's `MCNK` positions and heights, its liquid heights |
| `chunk_area_coverage` | a zone cell's `coverage`: chunks naming the area / 256 | `MCNK.areaid` |
| `skin_canvas_material` | a texture-layout canvas's material: one `base` layer, texture 0 (the composite), `uv_set_0`, `replace`; lit, opaque | none: the canvas is drawn flat, as is |
| `compositor_case_by_number` | a composite layer's `blend`: the compositor case whose number is the layer's `BlendMode`, and `blend_mapping: unverified` while the client is not shown to select cases that way | `ChrModelTextureLayer.BlendMode` |
| `always_on_set` | an owned placement set is always on when it is a WMO's first doodad set (MODS[0], additive by WMO.md), an M2's effects set, or either of a terrain tile's two sets (its own content) | the set's kind |
| `m2_effect_set` | an M2's ribbon and particle emitters placed as one owned set, `effects`, on their anchor joints | M2Ribbon, M2Particle |
| `structural_bone_label` | a label for every joint from its place in the hierarchy (`canon_bone_naming.hpp`): unique, deterministic, with a mirror axis checked against real skeletons | the joint hierarchy |
| `layer_role` | M2: `env` for an environment-mapped layer, else its M2Texture.type's wiki name, else `diffuse` for a batch's first layer and `unknown` after it. WMO: `diffuse` for the shader's base unit (t0; t1 for shader 23, t2 for 22), `unknown` for the rest. A material's `diffuse_layer` is the layer this calls diffuse | M2Texture.type, M2Batch, MOMT shader |
| `wmo_layer_uv` | the uv set a WMO shader samples texture unit n with: set n, except shader 23 (t0 environment-mapped, layer i at set i - 1) and 22 (t1 environment-mapped, the others by its parallax table) (WMO.md "Shader types") | MOMT shader |
| `wmo_liquid_type` | the LiquidType row of a WMO group's MLIQ by WMO.md "how to determine LiquidTypeRec to use": MOGP groupLiquid under the root's MOHD flag 0x4 (basic types 1-20 mapped to the WMO water/ocean/magma/slime rows), legacy green lava (15) by its tiles' liquid nibbles; undecided when the rules give no row | MOGP groupLiquid, MOHD flags, MLIQ |
| `texture_slot_name` | a texture slot's name: the wiki's name for its M2 texture type (`monster_1`, `skin`, ...), `type_<n>` for a type without one, `variation_4` for CreatureDisplayInfo's fourth texture variation, which no type takes | M2Texture.type |
| `component_file_pick` | the one file of a ModelFileData/TextureFileData resource the wearer draws: by ComponentModelFileData/ComponentTextureFileData race, sex and side tags, with the race's model fallbacks (ChrRaces); unresolved with a reason when the wearer is unknown, several files fit equally, or none fits | ComponentModelFileData, ComponentTextureFileData, ChrRaces |
| `appearance_file` | values read from the `--appearance` file the export was given (chosen customization choices, gear slot tokens), not from the client's files | the `--appearance` file |
| `garment_split` | a source item's garments and their kinds (REFACTOR/GARMENTS.md stage 1): by slot, chest → `tunic` (`robe` with a skirt geoset), legs → `leggings` plus a `skirt` garment for its skirt geoset, shirt, waist, feet, wrist, hands, back, tabard, head, shoulder, weapons → one garment each; overlays, geometry and models become its pieces; head, shoulder and weapon models are `attached`, every other slot's model `skinned` | ItemDisplayInfo, ItemDisplayInfoMaterialRes, ItemDisplayInfoModelMatRes, the gear's slot |
| `garment_layer` | a garment's layer: its kind's default (`x-preset-kinds`), order 0 | the kind |
| `garment_vocabulary` | body region names for SectionType 0-7 (`arm_upper` … `foot`, else `section_<n>`), part names for the geoset groups `WIKI_FINDINGS/M2.md` reads visually (else `group_<n>`), attachment point names after wowdev.wiki's attachment table | CharComponentTextureSections, geoset groups, M2Attachment ids |
| `item_geoset` | a slot's GeosetGroup[i] value v > 0 shows body geoset group × 100 + 1 + v of the slot's i-th group (`WIKI_FINDINGS/GARMENTS.md` "Slot → GeosetGroup table", verified structurally); an item model shows group × 100 + 1 + AttachmentGeosetGroup[i] for v ≥ 0 (wow.export, unverified against the client) | ItemDisplayInfo GeosetGroup, AttachmentGeosetGroup |
| `hem_body_geoset` | the body hem geoset 1102 a legs item switches on (GeosetGroup[0] = 1) is a hem piece, not the leggings' | ItemDisplayInfo GeosetGroup |
| `hem_uv_footprint` | the hem's paint: the legs item's LegUpper overlay masked to 1102's UV footprint on the wearing body, rasterized at the layout's composite size and cut to the region's rectangle (`src/garment_footprint.hpp`); the leggings keep the whole texture | the body mesh's 1102 triangles, the layout's rectangles |
| `hem_join_set` / `hem_own_garment` | a legs item's hem pieces join the tunic of a chest item sharing a TransmogSet; with none, they are a `hem` garment of their own | TransmogSetItem |
| `item_model_hem` | an item model's shown group-13 geoset of a chest or legs item is a hem piece of its own; one shown by a set's chest and legs (same model file and geoset) is one piece, on the chest's garment | ItemDisplayInfo AttachmentGeosetGroup, the model files picked |

**Inheritance.** A property without its own `x-source` takes its `$ref`
target's (the use site's wins when both have one), else the nearest
annotated ancestor's along the document path, across `$ref`s, array
entries, map values and `oneOf` branches. One annotation on an object
therefore covers everything inside it that doesn't say otherwise. A
constant needs none: its origin is the format.

**Coverage.** Every leaf property (one with nothing below it) of every
kind must resolve an `x-source`; one that doesn't is a lint error
(`husk validate-bundle --lint-schema`, and the schema lint test).
`bundle_schema::sourceGaps()` lists them, once per schema location, with
every document path that reaches it, so a new key names where to annotate. A
shared definition's leaves usually take their origin from the use site:
`creatures[].source.id` annotates the `identity` it `$ref`s with
`Creature.ID`, and that covers each `oneOf` branch's `value`, `table` and
`row`. `bundle_schema::kSourceRequired` (`bundle_schema.hpp`) can turn the
error back into a warning for a bulk annotation pass; the command then
prints each gap and a count per schema file.

### Descriptions: what, then why

A `description`'s first sentence says what the value is; the sentences
after it say why it is so (the decision, the source behaviour it follows,
what a consumer must not assume). There is no separate `x-why` keyword:
one text keeps the what and the why together where a schema editor
writes them, and the 883 existing descriptions already mostly read that
way. A sentence ends at ". " followed by a capital letter or a backtick.
`husk schema-doc` shows the split: the first sentence, then "*Why:*" and
the rest. A description whose later sentences are more "what" rather than
"why" is rewritten into one sentence when the annotation pass touches it.

### One reference file: `husk schema-doc`

`husk schema-doc [-o FILE]` renders the embedded schemas as one Markdown
file: per kind, every document path (`$ref`s, array entries `[]`, map
values `.<name>` and `oneOf` variants `{kind=x}` expanded; a recursive
definition is listed once and marked) with its description, type, allowed
values (const, enum, bounds, pattern), unit, `x-index-into`/`x-id-in`
target, resolved `x-source` (and the path it is inherited from), and
whether it is required. Output is deterministic (keys in sorted order), so
it can be diffed between versions. It is generated, not checked in: the
schemas are the source.

## Stable ids

**Rule** (Luna, 2026-10-28; applied to every large array 2026-11-24): an
array that holds or can hold more than about 50-100 entries, or that other
documents point into, gives each entry an `id` (`common.schema.json`
`uuid`), and every pointer into it holds that id (`x-id-in`), never a
position. Positions renumber whenever an array is filtered, merged or
re-sorted; an id survives that and a re-export. Positions (`x-index-into`)
stay where position *is* the identity: intrinsic geometry and small,
fixed-scale tables.

**Ids come from source identity, never from output position.** Each is
`husk::nameUuid` (UUID v3 in husk's namespace, `src/uuid.hpp`) of a key
string. Most keys are `<space>/<identity key>` (`writers::stableId`), the
identity key being `file_data_id/<value>`, `db2_row/<table>/<row>`,
`record_index/<value>` (plus `/name/<name>` when the ref has a name: a
record index is a position in a list only the name tells apart, e.g. a
WMO's doodad set 0 and its effects set), or `name/<name>` for an entry
with no identity of its own. The Blender addon mirrors it
(`docs.identity_key`, `docs.reference_id`).

| Array | Key | Why that key |
|---|---|---|
| `references` (every kind) | `<kind>/<identity key of asset>` | The referenced document's identity: a model by file id has the same id in every document that references it, the creature catalog's included. A reference without identity (owned sets, effects, techniques) is keyed by its name. |
| world `zones` | `zone/<map>/AreaTable/<row>` | AreaTable row, per map. |
| model `selection.presets` | `preset/<identity key of the model>/<body key><outfit key>` | The source values that make the look ("Appearances"); a character model lists thousands, and the creature catalog points at them. |
| appearances `appearances` | `appearance/<identity key of the model>/<body key>` | The same, body only; the model's identity keeps ids unique in a tree's shared list. |
| garments `items` (source items), `garments`, `outfits` | `source_item/db2_row/<table>/<row>`; `garment/` + that key + `/<kind>` (`/hem`, `/skirt` for split-off garments; NPC equipment `/garment`); `outfit/db2_row/TransmogSet/<row>` | The source rows the garment was cut from: the same garment has one id in every model and tree, so a shared list dedups by id. |
| animation_fallbacks `fallbacks` | `animation_fallback/<identity key of the animation>` | The animation's AnimationData row; the table is one per client build, so the row alone is unique and the same in every tree. |
| creatures `creatures`, `displays` | `creature/Creature/<row>`, `creature_display/CreatureDisplayInfo/<row>` | Their DB2 rows. |
| mesh `parts` | `part/<identity key of the model>/<source index>` | A WMO group's index in its root file; the model's identity makes it unique across documents. |
| `light_conditions` | `light_condition/db2_row/LightParams/<row>` | The LightParams row. |
| `resources.textures` (model, terrain tile, placement set) | `texture/<identity key or none>[/named/<name>/<name_source>]/<state>` + `/payload/<uri>` (resolved with a payload), `/reason/<reason>` (known_unresolved), `/candidates/<identity key>/<category>…` (ambiguous) | The table lists each distinct entry once, and one file can have two: an own payload and the shared store's, two unresolved reasons, or one name synthesized by one producer and read from a DB2 by another. The identity alone would give both one id, so the key adds what the entry says beyond it. The writer (`TextureTable`) throws when two different entries still share a key. |
| `resources.materials` | `material/<identity key of the model>/<identity key of the material's ref>` | The ref names the material's source record: its position in the source's material list (M2 batch material, WMO MOMT). |
| `resources.scene.static_lights` | `static_light/<identity key of the model>/<record table>/<record index>`, plus `/part/<part source index>` for MOLS/MOLP | The light's source record (MNLD, MOLT, MOLS, MOLP); a group's own lights count from 0 in every group, so the group's index is part of the key. |

One reference per id: two placements of one file under different names
are one reference, and a model's item models are deduplicated the same
way. Checked-in technique bodies (`techniques/`) carry their references'
ids as text; the writer checks each against the derived one.

**Classification of every pointer** (2026-11-24):

| Pointer | Target | |
|---|---|---|
| model `definition.placement_sets[].reference`, `texture_slots[].composite`, customization choice `textures[].material`, material `techniques[]`; placement_set `instances[].reference`; world `grid.cells[].tile`, `placements[]` | `/references` | id |
| part `portals[].to_part`, liquid `part`, static light `part`, primitive `part`, LOD `part_levels[].part` | `/resources/geometry/mesh/parts` | id |
| zone light `clear` / `clear_underwater` / `storm` / `storm_underwater` / `death` | `/resources/light_conditions` | id |
| `texture_id` (material layers, slot options, item/choice textures, liquid frames, light cookies, terrain ground `diffuse`/`height`, LOD `diffuse`/`normal`, `liquid_flow`, effect image inputs) | `/resources/textures` | id: a WMO or character model can list hundreds |
| material document layer and composite layer `texture` | the material document's `/resources/textures` | index, kept: a material document lists a handful, and composites have no identity of their own |
| primitive / liquid `material` | `/resources/materials` | id: a WMO's materials reach ~100-200 |
| part `lights[]`, `light_sets[][]` | `/resources/scene/static_lights` | id: a WMO's lights reach the hundreds |
| `surface_type_index` | `/resources/surface_types` | index, kept: the face channel `surface_type` (a binary attribute) indexes the same table, and a document lists only the few surfaces it uses |
| part `portals[].portal` | `/resources/geometry/mesh/portals` | index, kept: portal polygons are geometry, paired with the parts' links |
| `joint_index` | `/resources/rig/skeleton/joints` | index, kept: vertex joint buffers index joints |
| layer `uv_map` | `/resources/uv_maps` | index, kept: a handful per model |
| choice `correction_set` | `/resources/rig/correction_sets` | index, kept: a few .bone sets per model |
| `instance_ref.set`, `selection.placement_sets[]`, light `placement_set`, instance `selection.placement_sets[]` | `/definition/placement_sets` | index, kept: a model owns a few sets (WMO doodad sets, terrain's two) |
| `slot` | `/definition/texture_slots` | index, kept: a dozen replaceable texture types at most |
| preset `particle_colors` | `/definition/particle_colors` | index, kept: a few per model |
| preset `appearance` | the entries of the document the model's `appearances` reference holds | id ("Appearances") |
| creatures display `preset` | the model document's `/selection/presets` | id, not checked: the validator doesn't open the model |
| appearance/customization `option` | `/definition/customization/options` | index, kept: options are a few dozen per model |
| preset `garments[]`, bake `covers_garments[]` | the garments of the document the model's `garments` reference holds | id, like preset `appearance` |
| garments piece `item`, source item / outfit `garments[]` | the garments document's `/items`, `/garments` | id: a shared list holds a corpus's items |
| physics `shapes[]`, joint `body_a`/`body_b` | `/resources/physics/*` | index, kept: a few bodies per model |
| terrain layer `texture_index`, `ground_effect_index` | the tile's `textures`, `ground_effects` | index, kept: per-tile tables of a few dozen |
| terrain chunk arrays (`area_index`, layers, per-chunk slices) | per-chunk position | index, kept: chunk position in the 16×16 tile is its identity |

## Attribute channels — per-element data without a dedicated key

**Current** (2026-10-10, Luna's call): `resources.mesh.attributes` is a
list of named channels, each over one element domain (`vertex`, `face` =
one per triangle of `indices`, `part`), stored as an ordinary
`BufferSlice` in `mesh.bin`. A source puts per-element facts here raw,
named `"<source>.<field>"`, instead of growing a typed key for each
format detail; a consumer reads every channel the same way, and a
Blender importer maps them onto mesh attributes directly. In-memory twin:
`canon::AttributeChannel` (`src/canon_attribute.hpp`); the channels
written today are listed in `src/writers/bundle_writer.hpp`'s header comment.
First use: a WMO's `MOPY`/`MPY2` per-face flags and material, which
replaced a separate WMO collision mesh (a consumer selects collidable
faces by flag). `resources.geometry.collision` remains for M2, whose collision
vertices are separate data.

One channel is resolved rather than raw, so it has no source prefix:
`surface_type` (face, u16), each face's index into
`resources.surface_types` (0xFFFF: none). It is written for a WMO with
per-face ground types (`MOQG`); otherwise a face's surface is its
material's `surface_type`.

## Techniques — the source game's math, once

**Current** (2026-10-13, Luna's call): a `technique` document holds one
drawing step of the source game (an M2 pixel shader, the M2 pipeline every
pixel shader feeds, a framebuffer blend, a WMO shader) as data: its inputs
(texture units, per-draw constants, vertex values), named `terms`, and
`outputs` as expression trees (`input`, `term`, `const`, or `op` over
`args`; ops add, sub, mul, div, max, min, dot, lerp, saturate, luma,
channels, lit, tangent_frame; each op's definition is in
`schema/technique.schema.json`'s `expression`). A texture input is the material layer with that `texture_unit`;
its optional `coordinate` is an expression over the other inputs and
terms (offset or generated coordinates), else it samples at the layer's
`uv_map`. Values a renderer computes (sphere-mapped coordinates, the
view-space position and normal) are `renderer_value` inputs whose
description states the formula. Two ops need the renderer too: `lit`
(its lighting) and `tangent_frame` (screen derivatives of its operands,
which a per-texel evaluator can't take). Each
states its `status` (verified from the client's shaders, inferred,
partial, or from the wiki) and its `evidence`, and what an expression
can't capture goes in `notes`. The checked-in `techniques/<family>/<name>.json`
are the source of truth; the schema's descriptions no longer hold
formulas. A model material lists the techniques it is drawn with
(`techniques`, reference indices: its shader, then its blend); the bundle
carries each one it uses under `techniques/`, plus those they reference
(a pixel shader references `m2_pipeline/pipeline`), so a bundle is
self-contained, and the content-addressed store keeps identical files
once. 72 documents: 37 M2 pixel shaders, the M2 pipeline, 9 framebuffer
blends, 25 WMO shaders (from the client's `uber` shader). A WMO
material's layers carry `texture_unit`: `texture_1`-`3` are 0-2, and for
shaders 22 and 23 `color_2`, `flags_2` are 3-4 and `runTimeData[0..3]`
17-20 (`WIKI_FINDINGS/WORLD.md` "WMO shaders 22 and 23").

## Levels of detail

**Current** (2026-10-16, Luna's shape): every mesh model has
`resources.geometry.lod = {max_level, levels}`. Levels are finest first:
level 0 is full detail, each later one a coarser stand-in the source draws
from further away. `max_level` is the last level the source authored, and
`levels` has exactly `max_level + 1` entries. A reader wanting level n reads
`levels[min(n, max_level)]` and never checks per item what exists.

Every level is a complete mesh, read like `resources.geometry.mesh` (vertex
streams, `indices`, `primitives`, `attributes`). Level 0 repeats the mesh
itself, the same slices and primitives; `validate-bundle` checks that it
does. A stream two levels share names the same bytes. A level's primitives
name the mesh's parts and the document's materials. Its `source` says
where it came from.

- **M2** (doodads, trees, anything with `_lodNN.skin`): level k is the k-th
  LOD skin, the SFID entries after the model's `numSkinProfiles` view
  skins. The views are not distance levels: on a sample of 800 world M2s,
  a model's view skins `00`..`03` were near-identical in size, while its
  `_lod01`..`_lod03` shrink each step (a tree: 11,460 → 5,448 → 1,992 → 18
  indices, the last an impostor card with its own `_lod` leaf texture). An
  LOD skin indexes the model's one vertex list, so every level shares
  level 0's vertex slices. `source.file` is the skin. 17 of the 800
  sampled M2s have LOD skins; the rest have `max_level` 0. `LDV1`'s
  `lodCount` isn't used: some models with LOD skins lack the chunk (that
  tree), and the SFID list is what names the files.
- **WMO**: level k is GFID row k (`<root>_NNN_lodK.wmo`). It draws every
  group carrying `SMOGroup::LOD` (flag 0x400), each from its highest own
  level ≤ k, and drops the other groups (interiors). On 1,380 sampled
  roots a group has lod1/lod2 files exactly when it carries the flag; at
  level 3, 39 of 505 flagged groups have no file and draw their level 2,
  the per-part version of the reader's own clamp. `source.part_levels`
  lists the parts drawn with the level each came from. A level has its
  own vertex streams (group files hold their own vertices) and no
  `wmo.poly_*` face channels unless every group it draws has them: level
  files carry no `MOPY`. Bootybay: 269,121 → 113,433 → 70,332 indices.

A terrain tile's chain is `resources.terrain.lod`, the same wrapper. Level
0 is `{"surface": "heightfield"}`: draw the chunks. Levels 1–4 are the
tile's `_lod.adt` bands (`source.band` 4, 8, 16, 32, finest first), each a
whole-tile mesh: `positions` (one slice every level shares: 129×129
corners, 128×128 centres, then skirt copies of border corners, lowered),
`indices`, and `skirt_indices` (triangles hanging below the border to hide
cracks against a neighbour at another level; the finest band has none).
Each band's range is the union of one depth of the file's quad tree
(verified on azeroth_32_49), so a band draws whole without the tree. The
grids sit where the chunks' vertices do: centre heights equal the chunks'
exactly, corners differ by up to ~4 yd (moved when the coarse meshes were
baked). `diffuse` and `normal` index the tile's baked map textures
(`world/maptextures`); the image's first stored row is at the tile corner,
rows along `row_axis`, columns along `column_axis` (matched by eye against
azeroth_32_49's heights). A tile without `_lod.adt` has `max_level` 0.

Placements carry what the source draws from afar on the instance, not as
a separate list: a tile's `_obj1.adt` repeats every `_obj0` doodad (same
uniqueId and model on 274 of 274 sampled tiles) and a subset of its WMOs
(2 tiles leave some out), re-sorted, with each placed model's box. An
instance it lists has `distant` (`bounds_min`/`bounds_max` in the set's
frame; `source.radius`, which the client's distance culling scales with
by a rule not established, and `source.band`, one of three bands whose
meaning is not established). An instance without `distant` isn't drawn
from afar; the tile's provenance (`obj1`) says whether the list was read.

## Surface types — what the ground is made of

**Current** (2026-10-13): a document lists each surface it names once in
`resources.surface_types`: a TerrainType row (`Dirt`, `Grass`, `Stone`,
`Snow`...) with its footstep sound class and sprays. Terrain ground
effects (`GroundEffectTexture.Sound`) and WMO materials (`MOMT`
ground_type) index it; WMO faces override per face (above). Coverage
caveat, measured on 2,000 seeded tiles of 12.1.0: 63% of quads' dominant
layer names a GroundEffectTexture row the build's file lacks, and 30%
have no layers, so most terrain has no surface type.

## Textures — one table per document

**Current** (2026-10-11, Luna's call): every document that uses textures
(a model; a placement set whose instances take images) lists each one
once in `resources.textures` (a `texture_entry`: state, identity,
payload). Everything else names a texture by its index there: material
layers' `texture`, texture slot `options`, terrain ground textures'
`diffuse`/`height`, liquid type frames, effect instances' image inputs.
Identical entries share an index, so a texture used by several layers,
options or instances appears once. An inline placement set is its own
document with its own table.

Where the bytes live (2026-10-11, Luna's call): a standalone export
embeds them under the bundle's own `textures/`. `export-world` writes
every texture of the export (tiles', models', WMOs') once to the shared
`<out>/textures/<fdid>.<ext>` (`writers::TextureStore`), and entries point
there by relative uri (`../../textures/…` from a model bundle). A
character model's hundreds of baked NPC skins are why: embedded, one
model would carry ~600 MB.

## Options and presets — what a model may be, and what a source used

**Current** (2026-10-11, Luna's call): a model that can be dressed more
than one way declares its options in `definition`, each valid on its own
whether or not the source ever combined it with the rest. Choosing among
them (which texture, which geosets, what size) is the renderer's call,
the same as customization choices.

- `texture_slots`: textures the model takes from outside
  (`{name, source: {texture_type}, options}`; M2: one per distinct
  nonzero texture type; `options` are ids of `resources.textures` entries).
  A layer that reads one names it as `slot`; its own `texture` is the
  default, and any of the slot's `options` may replace it. A slot no
  layer reads still lists its options; one husk tied to the model by
  inference (no texture type; the fourth `TextureVariationFileDataID`)
  has no `source` and says how in `inferred_from`.
- `particle_colors`: ways to recolour the model's particles; ramp i of an
  option recolours emitters whose `particle_color_index` is 11 + i.
- Geoset options are the mesh's own geosets.

`selection.presets` saves the combinations a source is known to use: slot
choices (`{slot, option}`), the geosets shown (`shown_geosets`, a closed
list; absent = not chosen), a particle colour option, opacity, and the
source rows that use it, each with the scale it draws the model at and
its named `users` (since 2026-11-09: the `Creature` rows listing that
display, name in the ref, plus `title`; absent when the client's partial
creature list names none).
Values the source names but husk can't place in an option go in the
preset's `unresolved`.

M2 creatures fill both from `CreatureModelData` → `CreatureDisplayInfo`
(`src/creature_options_input.cpp`, `export` and `export-world` with
`--db2-dir`/`--dbd-dir`). A humanoid NPC (a display
of a character model) is a preset too: its baked skin is an option of
the lowest-type slot (`skin`). A character model's
`definition.customization` lists its options and choices (with each
choice's geoset), one catalog per ChrModel that draws the file, each
option naming its `body` (a race's alternate form shares another's
mesh: `humanmale_hd` is ChrModel 1 and 45). A preset's `customization`
is `{option, choice}` indices into that list; a choice from a catalog
that doesn't draw the file is the preset's `unresolved`. Its equipment
indexes `definition.items` (`ItemDisplayInfo` refs, each once) as `{slot,
item, source: {item_slot}}`, standing in until item documents exist. Options never swap the
model: a creature that alternates between models is a level above the
model document, in the creature catalog, whose displays name different
models ("Creature catalogs"). Options that are only valid
together ("this texture only with these geosets") would need a
constraint form; none is known for creatures. For characters the plan
is to make such pieces separate options instead (a tunic's skirt as its
own selectable piece, CANONICAL_MODEL.md "Items by identity, not slot").

### Using presets: a library of real looks

On a character model (`humanmale_hd` etc.), `selection.presets` holds one
entry per distinct humanoid NPC look the client data defines: thousands of
complete, Blizzard-authored appearances (`humanmale_hd`, 12.1.5: 5,748
presets from 6,384 displays, 4,617 distinct customization sets, about
800 B each). None is marked default and the order means nothing. Any of
them is a real look rather than a guessed one: pick one as a starting
point, or let a user scroll through them. Creature models get the same
list from their texture-variation displays.

To apply preset `p` (indices are into this document unless stated): take
its `appearance` from the model's `appearances` reference (next section)
and apply the appearance's fields, then the preset's own `textures` and
`items`:

- `textures[]` `{slot, option}`: every layer naming `slot` draws
  `definition.texture_slots[slot].options[option]` (a `resources.textures`
  index) instead of its own texture. On a humanoid NPC this is the baked
  skin in the `skin` slot.
- `customization[]` `{option, choice}`: the choice is
  `definition.customization.options[option].choices[choice]`. Show its
  geoset and hide the geosets of the option's other choices. Bind its
  textures to the composite inputs they name (`materials/<n>.json`,
  `target_<id>`) to compose the skin instead of using the bake. Which of
  the two the client draws for an NPC is open (TODO/MATERIALS_TODO.md
  §9).
- `shown_geosets`, when present, is the complete list of visible geosets.
- `items[]` index `definition.items` (see "Items, physics and bone
  corrections" below). NPC equipment is a bare `ItemDisplayInfo` ref.
- `particle_colors`, `opacity`: as named.
- `sources[]`: the rows that use the look, with `scale`/`base_scale` and
  `users` (NPC names and titles) for labelling a list. The same look can
  have several sources and users.

Choices from another ChrModel's catalog, and values husk can't place, are
in the preset's `unresolved`. Skip or flag them; never guess them.

### Appearances: what a preset points at, inline or external (0.4.0)

**Current** (Luna 2026-11-24: "presets should be references, same as other
references so they can be either inline or external … external would
refer to a bigger appearance list table … but we also want to support
inline for self contained bundles"). A preset no longer carries the
body's look itself: it points at an **appearance**, an entry of an
`appearances` document (`schema/appearances.schema.json`).

- **An appearance is the body, without what it wears**: `customization`
  `{option, choice}` picks, and for a creature display its texture
  variations (`textures` `{slot, option}`), `shown_geosets`,
  `particle_colors` and `opacity`; values of those the source names but
  husk can't place (`ChrCustomizationChoiceID`, `ParticleColorID`) are the
  appearance's `unresolved`. These are what make two looks the same body.
- **Invariant: an appearance holds only body-chosen state** (customization
  picks, a creature display's variations). Nothing gear-derived goes into
  an appearance: geosets an item shows or hides (a robe's skirt, trousers
  hidden under it, gloves or boots hiding body parts; ItemDisplayInfo
  `GeosetGroup`/`AttachmentGeosetGroup`, `HelmetGeosetVis`) belong to the
  item's components, never to `shown_geosets`. Why: swapping one garment
  must not change the body (REFACTOR/README.md I7), and a gear-derived
  value in the body would split one body into one appearance per outfit,
  undoing the dedup. `shown_geosets` today comes only from
  `CreatureDisplayInfoGeosetData`, the display's own choice.
- **The preset keeps the outfit and the users**: `id`, `appearance` (an
  appearance id), the humanoid NPC's baked skin (`textures`, the bake slot:
  the bake draws the gear in, and each bake choice lists the garments it
  was made for, `covers_garments`, so a consumer that swaps a garment knows
  the bake no longer applies and composites instead), `garments` (garment
  ids of the model's garments document, each once; REFACTOR/GARMENTS.md),
  `sources` (rows, scales, `users`)
  and the outfit's `unresolved` (`ExtendedDisplayInfoID`,
  `BakeMaterialResourcesID`). A wolf's preset is a pointer and its
  sources; a humanoid NPC's is a pointer plus bake and gear.
  `export --appearance` puts its `cust` choices in the appearance and its
  gear on the preset. A preset whose body chooses nothing has no
  `appearance`.
- **Indices stay the model's.** An appearance's `slot`, `option`,
  `particle_colors` and customization indices index the definition of the
  model whose preset points at it, wherever the appearance is stored.
- **Ids from source identity.** Appearance: `nameUuid("appearance/<identity
  key of the model>/<body key>")`, the body key being the source values in
  source order (choice rows, texture FileDataIDs by display slot, geoset
  ids, ParticleColor row, alpha), so renumbered options or a re-export
  keep it. Preset: `preset/<identity key of the model>/<body key><outfit
  key>` (bake FileDataID, item rows by slot); an `--appearance` preset adds
  the appearance string. Presets get ids because a character model holds
  thousands, and the creature catalog points at them.
- **Dedup**: one appearance per distinct body per model. `humanmale_hd`
  (12.1.5): 5,748 presets over 4,617 distinct customization sets, so the
  body list is stated once and scrolling bodies needs no reader-side
  dedup.

**The reference.** Presets reach their appearances through the model's
one `references` entry of kind `appearances`, the same `reference` shape
as every other: `asset` names the list (`{kind: none}`, name
`appearances`), and the document is in one of the two forms every
reference already has:

- **inline** (`document`): the appearances document itself, holding this
  model's appearances only. `husk export` writes this: the bundle stays
  self-contained.
- **external** (`asset.uri`): a shared appearance list, `<tree>/
  appearances.json`, holding every model's appearances. Each entry then
  names its model as the id of one of the list's own `model` references
  (`model`, `x-id-in: /references`); inline entries leave `model` out (the
  model is the one embedding them). `export-corpus` writes this form.

The reference id is the same in both forms (`appearances/name/appearances`),
so turning a bundle from inline to external changes no pointer: only
`document` is replaced by `asset.uri`. `selection.presets[].appearance` is
a plain id; which document holds it is the model's `appearances`
reference, whichever form it takes. This is no new mechanism: `document`
vs `asset.uri` is "Embed or reference — one rule" for every reference
kind (placement sets, effects, materials and techniques already use it);
appearances are the first kind pointed into by entry id.

**Who writes the shared list.** `husk export-appearances <tree>` (and
`export-corpus` after `index-tree`): reads every model manifest under
`<tree>/models/`, moves each inline appearances document's entries into
`<tree>/appearances.json` (adding `model`), and rewrites the manifest's
reference to the external form (`uri` `../../appearances.json`; the
manifest is replaced, never written through, since `--link-store` trees
hardlink into the store). The rewrite replaces only that reference's
text, with the bytes the model writer writes for the external form
(`writers::beginListReference`, shared by both forms and by the
`animation_fallbacks` reference, REFACTOR/ANIMATION.md "Fallbacks"): store
objects are content-addressed, so the same inputs give the same bytes and
nothing else in the manifest is re-encoded. A manifest already external
keeps its entries from the existing list, so a rerun is idempotent.
Entries are ordered by model reference, then id.

**Validation.** `validate-bundle` validates the referenced appearances
document like any referenced document (inline or by `uri`, each file
once), checks that every `selection.presets[].appearance` is an id of an
entry there (across the external file too), and range-checks that entry's
indices against the pointing model's definition. An external list's
entries of other models are checked when those models are validated.

Rejected: a preset naming both the list reference and the entry
(`{reference, entry}`): a model has one appearance list, so the second
id only restates it. Appearances keyed by content hash of the indices:
indices renumber when a definition changes, source identity doesn't.
Model-independent appearances (choices by ChrCustomizationChoice row,
textures by FileDataID) would dedup across a race's models, but every
consumer reads options by index today; a later step, not this one.

### Items, physics and bone corrections (2026-11-09, schema 0.3.0)

- **Items.** `definition.items[]` is `{ref, models?, textures?, overlays?}`.
  `export --appearance` adds one preset: its customization choices and
  its gear, each gear entry an `ItemModifiedAppearance` item that carries
  `models` (one per `position`, the `ModelResourcesID` index: on
  shoulders 0 is the left model, 1 the right; `model` is the `.m2`
  exported as its own model bundle at `items/<fdid>.bundle/` and listed in
  `references`), `textures` (by the item model's texture type and the
  `model_position` it textures; type 2 also textures that item bundle)
  and `overlays` (textures for the wearer's body sections, by
  `component_section`). Every model and texture is the file the wearer's
  race and sex draw (CHARACTER_RENDERING.md §6). Where none could be
  picked (the wearer is unknown, several files fit equally, none fits) a
  model has `unresolved_reason` instead of `model`, and a texture is
  `known_unresolved` with the reason. Class-specific files (demon hunter,
  death knight) are never picked: the wearer's class isn't known. Preset
  item `source` is `item_slot` (NPC slot number) or `slot_token` (the
  appearance string's own slot name); `slot` is set when the name is a
  known one.
- **Physics.** `resources.physics`: the model's physics as `bodies`
  (joint, position, shape indices), `shapes` (capsule and polytope
  geometry; box and sphere layouts unverified, so under `source`) and
  `joints` (kind, bodies, the kind's record under `source`: its angle unit
  isn't established). Read from, first hit wins (0.4.0): `--phys`, the
  model's `PFDC` chunk (an inline `.phys`; where 12.x keeps nearly all of
  it), the `PFID` FileDataID (store `_by_fdid`, then the listfile path),
  a same-basename `.phys` beside the model. The manifest's provenance
  source `phys` says which (`detail`: `PFDC chunk`, `PFID <fdid>: <path>`,
  `--phys <path>`, `beside the model: <path>`); `available: false` when a
  `PFID` wasn't found locally or `--phys none` was given, so absent physics
  is unknown rather than "has none".
- **Bone corrections.** `resources.rig.correction_sets`: each `.bone`
  file the model or `.skel` names (`<bones-dir>/<fdid>.bone`, else the
  listfile/store), a 4x4 matrix per corrected joint. A customization
  choice names its set as `correction_set`. How a correction applies is
  unknown (TODO/BONE_CORRECTION_APPLICATION_TODO.md).

### Clips, material tracks, texture identity (schema 0.3.0)

- **Clips.** A sequence clip carries `animation` (its AnimationData row,
  named when the table has a `Name` column; current clients don't),
  `variation`, `duration`, `move_speed`, `frequency`, `blend_in`/
  `blend_out`, and the raw `source{flags, variation_next}`
  (`variation_next` −1 = last variation). `M2Sequence` bounds and replay
  range are not written.
- **Global sequences (0.4.0).** A `global_sequence` clip's `duration` is
  its loop length (`M2Loop`), the period its curves repeat with; the last
  keyframe can come before it. `resources.rig.global_sequences[i].duration`
  is the model file's own loop table, which material, scene and effect
  curves with `sequence_kind: global_sequence` index. A `.skel` model's
  joint clips index the `.skel`'s table, so the clip's own `duration` is
  the one to use for them.
- **Community clip names (0.4.0).** Where the client's AnimationData has no
  name for a clip's id (every 12.x clip), `export --anim-names <wow.export
  checkout>` names it from wow.export's list, `name_source: community`; a
  client name (`db2`) wins. The model document's provenance gains a source
  `animation names` (repo, file, commit), `available: false` without the
  flag. Detail: `REFACTOR/ANIMATION.md` "Which animation a clip is".
- **Animation fallbacks (0.4.0).** An `animation_fallbacks` document
  (`schema/animation_fallbacks.schema.json`) the model reaches through its
  one reference of that kind, inline (`husk export`) or the tree's shared
  `animation_fallbacks.json` by `asset.uri` (`export-world`,
  `export-corpus`), the same two forms as appearances ("Appearances").
  Entries `{id, animation, fallback}` name animations by a clip's
  `animation` ref; `otherwise` is what an unlisted animation falls back
  to. One table per client build: AnimationData rows have no model key.
  Detail and the grouping evidence: `REFACTOR/ANIMATION.md` "Fallbacks".
- **Joint flags (0.4.0).** `skeleton.joints[].source.flags` is
  `M2CompBone.flags` as stored. Bits with an established meaning are
  canonical fields: `inherit: {translation, rotation, scale}` (false where
  wowdev.wiki's ignore-parent bits 0x1/0x4/0x2 are set), `billboard`
  (0x8-0x40) and `driven_by: "physics"` (0x400, the wiki's
  `kinematic_bone`; in the 12.1.5 corpus it occurs exactly on models with
  physics, `WIKI_FINDINGS/M2.md` "Cloth"). Every other bit (the wiki's
  `transformed` 0x200 and `helmet_anim_scaled` 0x1000 among them, effects
  not established) is only in `source.flags`.
- **Material tracks.** Layer `tint`, `color_alpha`, `texture_weight` and
  `uv_maps[].transform`: a constant tint, alpha, texture weight or UV
  transform is a one-key `step` curve; identity values stay absent; a
  constant weight of 0 is kept. `color_alpha` (M2Color.alpha) and
  `texture_weight` (M2TextureWeight) are both written when both are set:
  how the client combines them is not established, so the consumer
  decides. A track a global sequence drives is that global loop
  (`sequence_kind: global_sequence`).
- **Texture identity.** A `known_unresolved` entry keeps the FileDataID it
  was looked for under (what a consumer would fetch). A resolved entry's
  FileDataID is the file the bytes are (the DB2 character tier answers a
  slot with no FileDataID of its own with the file the DB2 names); a file
  found by its FileDataID-named file takes the listfile's name
  (`name_source: listfile`) when the listfile knows it.
- **Option category.** `definition.customization` options' `category` is
  optional: absent when the option has none (`CategoryID` 0), never a
  reference to row 0.

### Schema 0.4.0 (breaking)

The full list with reader actions is `MANTLECORE_MIGRATION.md` "v0.4.0".

- `particle_system.gravity` is an `animated_vec3`: per keyframe the
  acceleration vector `[x, y, z]` in the emitter's frame (the model's
  axes, z up), yard/second^2; it rotates with the emitter. A plain M2
  gravity g is `[0, 0, -g]`; an emitter with particle flag `0x800000`
  stores a packed vector, which husk decodes (`WIKI_FINDINGS/M2.md`, 80 %
  of sampled emitters); a packed key with no direction is dropped with a
  warning. No scalar form and no raw bits remain. `ribbon_system.gravity`
  stays a scalar.
- WMO mesh parts: `portals[].side` (+1/-1, the part's half-space of the
  portal plane) is a resolved field; `interior_fogs` (was `fog_volumes`)
  lists the fogs a viewer inside the part sees.
- WMO mesh parts: `areas` (optional, additive; with `--db2-dir`) names
  where a player inside the part is: one entry per WMOAreaTable name set,
  because a placement picks among them (one building, two towns). Every
  map-object placement carries `selection.name_set` (from MODF
  `nameSet`), and the entry whose `name_set` equals it applies: the same
  definition/selection pair as `definition.placement_sets` and
  `selection.placement_sets`. Rows are keyed by the root's MOHD wmoID, the name set and
  the group's MOGP groupID; a part without a row of its own under a name
  set gets the model-wide row (WMOGroupID -1), marked `whole_model`.
  `ref` is the WMOAreaTable row named from `AreaName_lang`, `area` the
  AreaTable row it counts as, named. `nsabbey.wmo` (WMOID 59): part 0,
  "Main Hall", has rows under name sets 0-3, counting as Northshire Abbey
  under set 1 and Tyr's Hand under sets 2 and 3. The client's
  own lookup order for a missing part row is not checked; the
  `whole_model` flag lets a consumer choose.
- Names on refs that were written as identities only (additive, with
  `--db2-dir`): a tile's chunk `areas` (AreaTable `AreaName_lang`) and a
  zone light's `skybox.ref` (`LightSkybox.Name`, the sky model's old
  path). A tile liquid's `liquid_type` was already named; a
  `liquid_setting` stays unnamed, as LiquidObject has no name column.
- A customization option's `body` ref is named `"<race> <sex>"`
  (additive): the ChrRaces `Name_lang` of the lowest-row race using the
  model (ChrModel 1 is also used by "TBD NPC Race 1" and "2"), then the
  ChrRaceXChrModel `Sex` (0 male, 1 female) when the rows agree on it.
  ChrModel 1 is "Human male". Without the race tables the ref stays
  unnamed.
- Raw values left canonical keys for `source` objects (CANONICAL_FORMAT.md
  §2): liquid settings' flow speed, fishable and reflection, physics
  bodies' `type`, light parameters' celestial-sphere and sun-position
  overrides, the world `map`'s `directory`, `instance_type` and
  `map_type`, creatures' `classification` and displays' `size_class`. The
  schema lint rejects keys named after DB2 tables or columns and keys
  described "as stored" outside `source`/`unresolved`, with no exemption.
- Items are garments: a `garments` document (`schema/garments.schema.json`)
  the model reaches through its one reference of that kind, inline or
  external like appearances, with `garments` (kind, layer, order, pieces:
  `overlay`, `geometry`, `attached`, `skinned`, `hides`), `items` (the source items)
  and `outfits`; the body vocabulary the pieces name is the model's
  `definition.body` (regions with their rectangles, parts, attachment
  points). Presets wear `garments` (ids); bakes cover `covers_garments`;
  `definition.items` and `selection.equipped` are gone. Design and the
  split rules: REFACTOR/GARMENTS.md; derived rules in the table above.
- Keys named after the source renamed to say what they are: an option's
  `body` (was `chr_model`), a placement set's `light_conditions` (was
  `light_params`; stable-id key `light_condition/...`), a tile's
  `liquid_settings` and a liquid's `liquid_setting` (were
  `liquid_objects`/`liquid_object`). A geoset is `{group, variant}`
  (common.schema.json `geoset`) on primitives, customization choices and
  appearances' `shown_geosets`; the packed group * 100 + variant stays
  only as the source's number, a primitive's `source.skin_section_id` and
  a choice's `source.geoset`.
- Zone fog keys carry `extinction` and `height_extinction` in 1/yard,
  derived by `fog_density_to_extinction` (`EFFECTS.md` "Derivation
  rules", grade ref); the client's `density` and `height_density` moved
  to the fog's `source`.

## Embed or reference — one rule

A payload is either external (`uri`, a relative path) or inline. Either way the
consumer only reads the manifest, so it never has to care which.

Texture payloads beside the manifest and item model bundles under `items/`
are both this rule; nothing else special-cases it.

## glTF's demoted role

`husk export --glb <path>` writes a lean glTF projection of the model
(`src/writers/gltf_lean.*`): mesh, skin, animation, base-colour materials.
It stays because the Khronos validator and Blender's importer check it in
`tests/test_conformance.cpp`, and it is the interchange format for anyone
outside this project. It is **not** the schema of record: it omits what it
cannot express (options, presets, effects, physics, correction sets), never
encodes it in `extras`, and the canonical model does not bend to it.

## Bundles reference other bundles

**Settled**: `definition` travels **per model**, and a bundle may **reference
other model bundles** — a character bundle references the item bundles for what
it is wearing, rather than absorbing them.

So a bundle is self-describing but not necessarily self-*contained*. The
reference is an ordinary `Ref` whose `uri` points at another manifest:

```json
{ "role": "equipped_item",
  "id":   { "kind": "file_data_id", "value": 370361 },
  "name": "sword_1h_artifactfelomelorn_d_01",
  "name_source": "listfile",
  "uri":  "items/370361.bundle/manifest.json" }
```

**This isn't specific to whole models.** "Embed or reference — one rule" above
already says any resource is either inline or a `uri`, stated once on purpose
so it wouldn't need re-deciding per resource kind; `resources.materials` is
already `Refs + real texture layers`. So a material is independently
exportable as its own small manifest the exact same way an equipped item is —
`resources.materials[i]` is a `Ref` that may carry a `uri` to
`materials/<name>/manifest.json` instead of (or alongside) an inline
definition, with no new mechanism, just this rule applied at finer
granularity. This is what makes a single material genuinely distributable on
its own — see `CANON_DESIGN.md`'s material design section for what that manifest
actually contains.

This keeps the definition layer simple — no shared per-race object to locate,
version, or keep in sync — at the cost of duplicating a race's customization
tree across every model of that race. Accepted: correctness and locality beat
deduplication here, and dedup stays available later as a pure storage
optimization that does not change what a consumer reads.

### What this means for I3

I3 says a consumer looks only at the manifest. References do not weaken that —
the consumer follows a path the manifest *told it*, which is the opposite of
searching a directory or accepting an "extra data dir" parameter. The rule
restated for the referencing case:

> A consumer may open any file the manifest names, and no file it does not.

Two consequences worth writing into the schema:

- **A reference may be unresolved.** husk cannot always find an item's real
  model (no listfile, file not extracted locally). A reference with an
  `id` but no `uri` is a real, readable state meaning *"this exists, husk knows
  what it is, husk could not produce it"*. That is strictly more useful than
  omitting the entry, and it is I6 doing its job.
- **Don't assume acyclicity for free.** Character→item is a DAG in practice, but
  a consumer walking references should be cycle-safe rather than trusting the
  producer. Cheap to do, expensive to retrofit.

## Left to implementation judgment

Whether `items/` nests fully self-contained bundles or shares the parent's
`textures/` is a call to make while building, not to pre-decide here. The
tradeoff, recorded so it is not re-derived from scratch: nesting is simpler to
reason about and makes each referenced bundle independently openable; sharing
avoids duplicate texture payloads when a character and its gear use overlapping
textures. The cross-bundle reference decision above pushes gently toward
nesting — a referenced bundle that is independently openable is exactly what a
`uri` to another manifest implies — but real payload sizes should settle it.

The one constraint that is *not* left open, because it is what keeps the
measurement affordable: schema v1 must let a resource `uri` point outside its
own bundle directory, so whichever way it is measured, switching later is a
producer change and not a schema bump.

## Texture encoding — settled

**Canonical is the source *payload*, rehoused in an open container: DDS.
PNG is a projection husk emits on request.**

Four constraints were weighed (Luna's, 2026-08-28): the store should be readable
and explorable; it should be human-readable *or trivially transformable* to it
(I8); it must not pay avoidable transform quality losses; and **husk stores no
proprietary format** — if the input is a proprietary encoding, husk rehouses it
in something a publicly available tool can open, so husk is *the* tool with
built-in support, never the *mandatory* one.

A fifth point rules out the naive option before any of those apply: **raw decoded
pixels are not a candidate.** An uncompressed dump of a real corpus is enormous,
which is the whole reason a canonical *format* is being chosen rather than bytes
being spilled to disk. Every surviving candidate is compressed; that is the price
of entry, not a tiebreaker.

Those four constraints eliminate all three obvious answers and leave exactly one:

| Candidate | Fails on |
|---|---|
| BLP verbatim | Proprietary. Reading it requires husk or a WoW-specific tool. |
| PNG | Lossy re-encode of the blocks (see below), and discards GPU-native form. |
| Raw `.bin` of DXT blocks | Not a format. No public tool opens a headerless block dump. |
| **DDS** | **Nothing.** |

### Why DDS

- **It holds the blocks verbatim.** A DDS file is `DDS ` + a 124-byte header +
  the block data unchanged. Rehousing BLP's DXT1/3/5 payload into it is a header
  swap, not a transcode — zero loss, byte-identical blocks, still directly
  GPU-uploadable.
- **husk already does exactly this.** `blp/src/husk_blp/decode.py:62`
  (`_build_minimal_dds`) builds precisely this container today, to hand blocks to
  Pillow's own decoder, and its comment already notes it is "standard Microsoft
  DDS layout, not WoW-specific". The transform is implemented, exercised, and
  known cheap; what changes is that its output becomes a stored artifact rather
  than a throwaway intermediate.
- **Public tooling is broad and immediate** — Blender opens DDS natively, as do
  GIMP, Pillow, Compressonator and DirectXTex. That is the "husk is not
  mandatory" constraint met with tools someone already has, not with a spec they
  could theoretically implement.
- **KTX2 was the considered alternative** and is the more modern, Khronos-owned
  choice, with better headroom for mip/array/cubemap cases an engine would
  eventually want. It loses today on the one criterion that decided this: Blender
  has no native KTX2 support, and casual viewer support is thinner. The encoding
  tag makes the container swappable if that changes — this is a default, not a
  one-way door.

### Why the source payload wins over re-encoding to PNG

The readability and quality constraints do not conflict, because readability is
satisfied by its own escape clause while quality is only satisfiable in one
direction:

- **The transform is asymmetric, and only one direction is lossless.**
  DXT1/3/5 decodes to pixels deterministically — husk already does it, for all
  five BLP encodings, verified against the real corpus. The reverse is a
  *re-encode*: PNG → DXT throws away information and cannot reproduce the blocks
  it started from. Storing PNG therefore spends something irreversible at the
  moment of ingest, and spends it to buy a convenience that was one command away.
  Storing the blocks keeps both outputs available permanently.
- **A canonical store that cannot reproduce its own input is not canonical.**
  That is the general form of the point above, and it is why I8 says convert on
  output, never on intake.
- **Readability is satisfied, not waived.** A stored DDS opens in Blender or GIMP
  directly, and `husk blp-export` remains one command against a complete decoder
  husk already ships. That is the "trivially transformable" case I8 provides for
  — and with DDS it is barely even a transform, since a public tool opens the
  stored artifact as it sits.
- **The engine goal comes along free rather than being traded for.** DXT blocks
  are what a GPU consumes; keeping them means no re-decode-and-re-compress round
  trip later. Worth noting this is a *consequence* of the losslessness argument,
  not an independent reason — the decision would be the same with no engine in
  the picture.

One accuracy point that reinforces rather than decorates the above: DXT decoding
is only exact *with respect to a chosen decoder*. The 1/3 and 2/3 interpolants
have historically differed in low-bit rounding between implementations, so a PNG
does not record "the pixels" — it records one decoder's reading of the blocks.
The blocks are the actual artifact; a PNG is an interpretation of them.

### What this means concretely

- **Stage 2** (`RESOURCE_CATALOG.md`): the catalog returns bytes **tagged with
  their encoding** — `{bytes, encoding: Bc1|Bc2|Bc3|Bgra|Palettized|Png}` — and
  never pre-decodes. Its transcode cache is populated only when a caller asks for
  pixels. The tag names the *payload*, independently of whichever container the
  writer later puts it in.
- **Stage 4** (this file): a texture resource entry names its encoding and
  container, and may carry more than one variant of the same texture. Bundles
  written for archival or engine use carry the DDS-housed blocks; bundles written
  for Blender or glTF carry the PNG variant husk generated for them. The consumer
  reads that off the manifest and never guesses from a file extension.
- **The Blender addon needs no BLP decoder, and I3 stays intact.** husk writes
  the PNG variant at export time *because the target was Blender*, which is the
  same "resolve once, bake the answer in" move an item model bundle's `uri`
  makes. The addon does not shell out to husk and does not go looking; it opens
  what the manifest names. (It could open the DDS directly — Blender reads DDS —
  but being *told* which payload to use is the point, not being able to guess.)
- **The shape sketch's `textures/<name>.png` is therefore an example, not the
  rule.** The rule is that the `Ref` names the payload, its encoding, and its
  container.

The only hard commitment is the one that is expensive to reverse: **husk never
discards the source payload, never stores it in a proprietary container, and
never holds PNG as the only form.** Which variants a given bundle ships is a
writer decision that can change later without
a schema bump.

## Terrain tile bundles

**Current**: written by `husk export-terrain` (one tile) and `husk
export-world` (every tile of a map or world). A tile is a `model` document
whose content is `resources.terrain`, a heightfield; it has no mesh, rig
or scene. `schema/model.schema.json` defines it key by key; this section is
what a consumer needs on top of it. The subject is the root ADT (its
FileDataID when the listfile knows it, named `<map>_<x>_<y>`). With
`--db2-dir`, `definition.terrain.map` (optional, since 0.4.0) is the
tile's map as a `Map` row ref named from `MapName_lang`, the same row the
world document's `map` block describes, so a tile read alone knows its
place.

### Layout

```
<tile>.bundle/manifest.json
<tile>.bundle/terrain.bin            chunk heightfields, normals, masks, alpha maps
<tile>.bundle/liquid.bin             liquid surfaces
<tile>.bundle/textures/<fdid>.dds    export-terrain only: the tile's own ground textures
```

`export-world` writes no per-tile `textures/`. Ground textures are written
once to `<out>/textures/` and every tile references them by relative `uri`
(`../../../textures/<fdid>.dds`). Placed and ground-cover models live in
`<out>/models/<fdid>.canon.bundle/`, referenced the same way. A texture or
model with no `uri` was not produced (unresolved, not extracted, or failed
— see `errors.log`); its `Ref` still carries the identity.

### Frame and heightfield

The tile's own frame, yards. Its axes are WoW world space's (+X north, +Y
west, +Z up); its origin is the tile's corner vertex (chunk 0, corner
(0, 0)) at height 0. Heights stay absolute. A world places tile
(x, y) by translation alone, to world (32 − y, 32 − x, 0) · `tile_size`
(the world document's grid states it). Positions inside the tile are
otherwise not chunk- or file-relative.

A tile is 16×16 chunks (`index = row * 16 + column`). Rows step along
`row_axis` (−X), columns along `column_axis` (−Y). A chunk holds 145
absolute heights, interleaved as 9 corners then 8 centres per row pair.
Corner (r, c) sits at `origin + (r · row_axis + c · column_axis) ·
quad_size`; centre (r, c) half a quad further along both. Each quad is four
triangles fanned around its centre, counter-clockwise seen from +Z; a quad
whose `hole_rows` bit is set (bit c of byte r) has none.

### Splat layers

A chunk has **0 to 8** layers, not 1 to 4. Measured on all 341,760
`azeroth` chunks (MantleCore, 2026-10-09): 9% have none, 82% have 1–4, 4%
have 5–8. A splat shader must take up to 8 layers, i.e. 7 alpha maps.

- A chunk with **no layers is untextured**: in practice open ocean, under
  water. Its heights there are placeholders (see Seams).
- Layer 0 has no alpha map; it covers whatever the later layers don't.
- Each later layer has a 64×64 8-bit alpha map, row-major, rows stepping −X
  like the vertex rows. The weights partition exactly: base layer weight =
  1 − Σ alpha (verified to 1.000 over a whole tile).
- A texture repeats `repeats_per_chunk` times across a chunk (8 unless the
  tile's `MTXP` scales it).

`dominant_layer` (per quad, the layer whose ground effect applies) and
`ground_effect_suppressed_rows` are carried as read. Their bit order is
unverified: ground-cover scatter built on them placed nothing on plainly
grassy quads in testing (`TODO/WORLD/ADT_TERRAIN_TODO.md`).

### Ground cover

`ground_effects` are rules, not instances: a density and a weighted set of
doodad models per effect, which the client scatters at runtime. Use the
weights as ratios: a rule's weights sum to 100, 16 or 4, among others
(`WIKI_FINDINGS/ADT.md`).

### Liquid

One entry per `MH2O` instance: the chunk (`chunk_row`, `chunk_column`), a
quad rectangle inside its 8×8 grid, a per-quad `quad_exists` mask, and
`(columns + 1) · (rows + 1)` absolute vertex heights. `height_source` says
where they came from: `stored`, `min_height_level` (none stored: flat at
the instance's minimum, the open-ocean case) or `zero` (a depth-only
layout). These are `resources.terrain.liquids`, not the WMO-shaped
`resources.geometry.liquids`.

Every liquid names a `LiquidType`, and a terrain liquid also its
`liquid_setting`, a `LiquidObject` row (absent when the instance stored a
vertex format instead).
With the liquid DB2s, `resources.liquid_types` (terrain and WMO documents
alike) lists each named type once: its name, material and texture slots,
each slot's frames in animation order as ordinary texture entries. A
terrain's `resources.terrain.liquid_settings` gives each setting's flow direction,
and under `source` its raw flow speed (unit unverified), fishable and
reflection values. Without the DB2s both tables hold identities only. A liquid's own
arrays (`depth`, `uv`) say what it stores: Ocean's material claims height
and depth, but its liquids store depth only. Per chunk,
`liquid_fishable_rows`/`liquid_deep_rows` (same shape as `hole_rows`) mark
fishable and deep liquid quads.

Two optional additions, present only where the source has them (WoW Forever
1.60.1, retail 12.1.7+; `WIKI_FINDINGS/FOREVER.md`):
- `resources.terrain.liquid_flow`: a texture index to the tile's flow map
  (WDT `MAI2` → `world/liquidflow`), laid over the tile like the map
  texture. R and G are a signed flow vector around 128; data, not colour.
  Dropped when the WDT names a file the build doesn't ship.
- `liquid_types[].waves`: the type's FFT ocean-wave spectra (`FFTTile` via
  `LiquidTypeXFFTTile`), carried raw under `source` until the fields have
  names.

### Placements

Two placement sets (`PLACEMENT_SETS.md`, set files are
`placement_set` documents, `schema/placement_set.schema.json`), both always on: `doodads` (`MDDF`, M2s) and
`map_objects` (`MODF`, WMOs), references 0 and 1. Instance `translation` is
in the tile's frame, `rotation` a tile-from-asset quaternion (x, y, z, w;
the tile's axes are the world's), the asset in its own native M2/WMO space. Instance ids are ADT uniqueIds: unique within a set,
and map-wide, so the same id in two tiles' sets is one object to load once.
A map object's `active_sets` name the WMO's doodad sets (`record_index` =
`MODS` index) it turns on beyond the always-on set 0, with `MWDR`/`MWDS`
already resolved. A WMO's `uri` points at its own object bundle when one
was exported (`export-world`, or `export-terrain --models-dir`). A
placement whose tile names it by path and whose path the listfile doesn't
know has no identity, only its raw path as `name` (`name_source:
"adt_embedded"`).

### Seams between tiles

Each tile stores its own copy of its edge vertices (`definition.terrain.
edge_vertices: "duplicated"`); husk does not weld them. Verified on
`azeroth_32_49`/`azeroth_32_50` (2026-10-11): all 144 shared vertices
agree to 3e-5 yd in position and exactly in height. Each height is the tile's own `MCNK` base height plus its `MCVT`
offset, so two tiles' copies of one edge vertex agree only to float
precision — about 1–2e-4 yd at typical heights. A seam check needs a
tolerance relative to the height's magnitude, not a fixed 1e-4.

Larger mismatches seen across `azeroth` (MantleCore's
`world_loading_check`, 2026-10-09), all still open:

- untextured ocean chunks hold placeholder heights (0 next to a −515.56 yd
  floor), up to 515.6 yd apart;
- a 13.68 yd step where two flat ocean-floor tiles meet, ~500 yd under
  water (23_36/24_36, south edges);
- six cracks of 0.03–0.70 yd on south tile edges: 40_53, 41_52, 42_45,
  43_34, 44_34 (0.70), 45_34.

husk applies no transform to heights beyond that one addition, so these are
expected to be in the source tiles themselves; confirming it byte-for-byte is
`TODO/WORLD/ADT_TERRAIN_TODO.md`'s open item. Until then a consumer should
weld tile edges to one side's heights rather than rely on them matching.

## World documents

**Current**: written by `husk export-map` (one map, tiles referenced by
identity only) and `husk export-world` (`maps/<map>/<map>.world.bundle`,
every map with tiles or a map-wide WMO, linking the tile and WMO bundles
that exported). `kind` is `"world"`, `schema/world.schema.json` defines
it; the subject is the map's WDT. The WDT is the map's record of its
tiles: a tile file its `MAIN` doesn't list is stale and left out (and
`export-world` doesn't export it), and each tile's FileDataID comes from
its `MAID` (the listfile only without one).

- `grid`: cell (i, j) holds tile (x = i, y = j), its tile model placed by
  translation at `origin + (i · axis_i + j · axis_j) · cell_size`, which is
  where the tile's own frame puts its corner vertex. A missing cell is a
  hole (no tile, or one that failed to export). Neighbours of a cell are
  the cells next to it; a tile names none. `bounds_min`/`bounds_max` are
  the world-frame box of the tile's terrain surface and liquids, so a
  consumer can find the tile under a position without opening tiles.
  Verified on `azeroth` (1,176 tiles): every footprint matches its cell to
  0.002 yd.
- `placements`: placement sets the world places off the grid, world
  frame, documents under `sets/`. A map without terrain (WDT `MPHD` flag
  0x1) has one, `map_objects`, holding the single WMO its WDT's `MODF`
  places, with the doodad set it turns on; a map with terrain has none.
  The map's volumes are two more sets of effect instances
  (`EFFECTS.md`): `fogs` (`_fogs.wdt`, `classic_volumetric_fog`) and
  `particulate_volumes` (`_mpv.wdt`, `classic_map_particulate_volume`),
  each instance's id its source's own. They are two sets because the two
  id spaces overlap. Every azeroth fog and particle volume lands on one of
  its cells.
- `zones` (schema 0.2.2, Luna 2026-10-17; stable ids 0.3.0): the world's
  named regions, nested, each with a stable `id` (a UUID) that `parent`
  names, so reordering or filtering the array never breaks a reference.
  Ids come from `husk::nameUuid` (UUIDv3 of `zone/<map>/AreaTable/<row>`),
  so a re-export keeps them. The array stays an array, so reading by
  position is direct and lookup by id is a scan or a map built once. Each
  zone is its cells, the cells its surface directly covers, with a
  `coverage` fraction; a zone's whole extent is its cells and its
  descendants'. A zone is the format's own description, not a source
  table: the AreaTable row is only its `source.id`. From WoW, every area
  a chunk names plus the areas those lie in, named from AreaTable with
  `--db2-dir` (unnamed without). azeroth: 785 zones; Northshire is its
  three subzones over 31_48, 32_48, 32_49 (and slivers of 33_48/33_49).
  It exists so work can be loaded by zone: a zone is a workable size.
  With `--db2-dir`, a zone's `source` also carries its AreaTable flag
  words as stored (optional, since 0.4.0): `flags` (Flags[0],
  Flags[1]), `mount_flags` (MountFlags), and `flag_names`, the names of
  the set bits from wowdev.wiki DB/AreaTable's 10.2 tables (Area Flags,
  Area Flags2, Area Mount Flags; WoWDBDefs names columns, not bits). A set
  bit the wiki doesn't name is only in the raw words. The names are
  WoW's, so they stay under `source`.
- No graveyards. The client carries no graveyard positions:
  `WorldSafeLocs.db2` (FileDataID 1369425) is absent from every local
  build (12.0.7 through 12.1.5 and Forever 1.60.1; no WoWDBDefs layout for
  it names a build after 8.x), so graveyards are server data, like which
  zones a graveyard serves. The only client table mentioning
  WorldSafeLocs ids is `WaypointSafeLocs` (467 rows in 12.1.5,
  `ID`/`Pos`/`MapID`), and every row of it is a `WaypointNode`'s point:
  portals, boats, trams, instance entrances (`Take the portal from
  Wizard's Sanctum to Jade Forest`), not graveyards. `AreaPOI.PortLocID`
  references WorldSafeLocs ids without positions. What the client does
  say about graveyards is three AreaTable flags
  (`DontEvaluateGraveyardFromClient`, `UseMapTransferLocsForCemeteries`,
  `DontUseParentMapForCemeteries`), exported with the zone flags above.
  `export-map --tiles-from <tree>/maps/<map>` rewrites a map's world
  document linked to an existing export's tiles (seconds, not a
  re-export).
- `map` (optional, since 0.4.0; written with `--db2-dir`): the map's
  `Map` row, found by `Map.Directory` matching the WDT's directory (case
  ignored), because the WDT's file stem names a folder, not the place.
  `ref` is the row named from `MapName_lang` (azeroth: Map 0, "Eastern
  Kingdoms"); `expansion` (ExpansionID) and `max_players`;
  `instance_type_name` only for the five values WoWDBDefs' Map.dbd
  comment names (none, party, raid, pvp, arena); under `source`, the
  raw `directory`, `instance_type` and `map_type`; `parent` (ParentMapID) and `cosmetic_parent`
  (CosmeticParentMapID) as named Map refs, absent when stored as -1;
  `area` (AreaTableID) as an AreaTable ref named from AreaName_lang.
  Nothing from UiMap: the map's UI region tree is not exported yet.

## Creature catalogs

**Current** (Luna 2026-11-24, for schema 0.4.0): one `creatures` document
per export tree, `<tree>/creatures.json`, `schema/creatures.schema.json`.
It answers "what creatures exist, what kind is each, and what does each
look like" without opening a model or reading DB2. Which creatures live
where is server data, not in the client, so the catalog places nothing
and is not per zone.

```json
{ "kind": "creatures", "references": [
    { "id": "93ed63c9…", "kind": "model",
      "asset": { "id": { "kind": "file_data_id", "value": 123418 },
                 "name": "ridingdirewolf", "name_source": "listfile",
                 "uri": "models/123418.canon.bundle/manifest.json" } } ],
  "creatures": [
    { "id": "54d39895…", "name": "Kor'kron Battlewolf", "name_source": "db2",
      "type":   { "id": { "kind": "db2_row", "table": "CreatureType",   "row": 1 }, "name": "Beast", "name_source": "db2" },
      "family": { "id": { "kind": "db2_row", "table": "CreatureFamily", "row": 1 }, "name": "Wolf",  "name_source": "db2" },
      "displays": [ { "display": "d0fd2799…", "weight": 1 }, { "display": "691ce215…", "weight": 1 } ],
      "source": { "id": { "kind": "db2_row", "table": "Creature", "row": 71116 }, "classification": 0 } } ],
  "displays": [
    { "id": "d0fd2799…", "model": "93ed63c9…", "preset": "5be1c0de…", "scale": 1.2, "base_scale": 1,
      "source": { "id": { "kind": "db2_row", "table": "CreatureDisplayInfo", "row": 17408 } } },
    { "id": "691ce215…", "model": "93ed63c9…", "preset": "0a7f33e2…", "scale": 1.2, "base_scale": 1,
      "source": { "id": { "kind": "db2_row", "table": "CreatureDisplayInfo", "row": 247 } } } ] }
```

- **One document, not one per creature.** The client's `Creature` table
  holds ~20k of the game's creatures (18,639 rows in 12.1.5.70961, 5 of
  them in encrypted sections; a full catalog is 18,634 creatures, 16,807
  displays, 3,050 models, written in 0.5 s); one file
  of entries is what a consumer loads to pick from, and per-creature
  files would only be loaded together.
- **`creatures`**: every `Creature` row, ascending by row. `type` and
  `family` are the `CreatureType`/`CreatureFamily` row refs with their
  names, so a consumer maps them onto its own archetypes without DB2;
  absent when the row is 0. `title` is the line under the name.
  `source.classification` is `Creature.Classification` as stored, a plain
  integer: it is a client enum with no table to name it (community
  documentation reads 0 normal, 1 elite, 2 rare elite, 3 boss, 4 rare,
  5 trivial, 6 minus; not checked here).
  `displays` are the row's nonzero `DisplayID` slots in slot order, each
  with its `DisplayProbability` as `weight`, raw: a creature's only look
  can carry 0, and how the client picks among weights is not
  established.
- **`displays`**: every display a creature names, once, ascending by
  row; creatures share them. `model` is the `id` of a `references`
  entry: one `model` reference per distinct FileDataID, ascending, named
  from `--listfile` when given (`name_source: listfile`), `uri` when the
  tree holds that model's bundle, identity only otherwise. `preset` is the
  `id` of the entry of that model document's `selection.presets` whose
  `sources` list this display ("Options and presets"). `scale`/`base_scale` repeat that
  preset source's values (`CreatureDisplayInfo.CreatureModelScale`,
  `CreatureModelData.ModelScale`) so the catalog alone can size a
  creature. A display the client's table lacks (an encrypted section)
  keeps only its id and source. `source.size_class` is
  `CreatureDisplayInfo.SizeClass` as stored (no table names it); the
  rows the display layers on its model are unnamed refs, absent at 0:
  `object_effect_package` (ObjectEffectPackageID),
  `state_spell_visual_kit` (StateSpellVisualKitID, a standing visual),
  `anim_replacement_set` (AnimReplacementSetID), `dissolve_effect`
  (DissolveEffectID) and `mount_poof_spell_visual_kit`
  (MountPoofSpellVisualKitID). husk reads none of those tables yet, so a
  consumer resolves them or ignores them.
- **Stable ids.** Creatures, displays and model references carry
  `nameUuid` ids (`creature/Creature/<row>`,
  `creature_display/CreatureDisplayInfo/<row>`,
  `model/file_data_id/<fdid>`); a creature names its displays and a
  display its model by id (`x-id-in`), so filtering or reordering any of
  the three arrays never breaks a reference and a re-export keeps them.
  The catalog's reference items are its own (`kind` fixed to `model`),
  with the same id key as every document's references ("Stable ids"
  above), so a model has one id across the catalog and the documents that
  place it.
- **Written by `husk export-creatures <tree>`**, which `export-corpus`
  runs after `index-tree` whenever it has `--db2-dir` and `--dbd-dir`.
  Not part of `index-tree`: `index/` is derived from the tree alone, and
  the catalog needs the creature tables. Not part of `export-world`: the
  presets are what the model documents chose, so the catalog is written
  from the finished tree's `index/creature_displays.tsv`, and a row there
  whose model isn't the one the DB2 chain names fails the run (the tree
  was exported from other tables).
- **Validation**: the schema (every reference is a `model` one) and
  `x-id-in` for display and model ids. Whether `preset` is one of the
  model's presets is not checked (the validator doesn't open model
  references).

