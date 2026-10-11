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

glTF has been husk's only output, and paying its impedance has cost real
structure: geosets shipped as fake skeleton joints, semantic data smuggled
through `extras` onto a heuristically located carrier bone, a physics joint
graph deliberately truncated to fit, and no schema version anywhere
(`AUDIT.md` §3).

Per Canonical Data Model §13, glTF is a *projection* of the recovered semantics, not
the semantics. The bundle is what the canonical model actually writes; glTF stays
as an optional exporter that may omit what it cannot express.

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

The existing `GearItem::auxGlbPath` (`cmd_export.cpp:695-705`) already works
exactly this way and is the precedent being generalized — husk resolves once, at
write time, and bakes in a relative path.

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

`name_source` is one of `m2_embedded` / `adt_embedded` / `wmo_embedded` / `listfile` / `db2` / `synthesized` /
`none` (I6). It exists because most naming in this project comes from the
community listfile — unreliable at best — or is invented by husk. A consumer
reading `"scalpupperhair00_08"` must be able to tell whether that came from the
model's own bytes, a DB2 row, or a community guess that may simply be wrong.

This applies uniformly: textures, aux item bundles, animations, customization
choices, materials, geosets — **and bone names**. Physics bodies, emitter
anchors, correction sets and gear entries all carry bone names today
(`gltf_skeleton.cpp:266,309,331`), and every one of those names is
husk-synthesized or contextual, never authoritative. The raw joint index stays
alongside for exactly that reason.

## Document kinds and the contract header — agreed 2026-10-10

Every husk document is one of three **kinds**, decided by what the document
has to do, not by which WoW file it came from:

| Kind | What it is | Examples |
|---|---|---|
| `model` | Content in its own local frame, placed by something else: an asset. | M2 doodad, item, character, WMO building, terrain tile |
| `placement_set` | No content of its own: instances of other documents, each with a transform and the selection that instance makes. | a WMO's doodad sets, a tile's doodads and buildings |
| `world` | The root: nothing places it. Holds the tile grid, off-grid placements and the objects owned by the place as a whole. | a map |

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
format          "husk-bundle", constant
kind            "model" | "placement_set" | "world"
schema_version  version of the schema/ definitions the document conforms to
subject         Ref: what this document describes
space           length/angle units, up axis, handedness of every position in it
provenance      producer, exported_at, input_format, sources[] (per table/file: available or not)
definition      what exists and is valid for the subject
selection       what this instance chose; only ever indices into definition
references      other documents and assets this one depends on: [{ref, kind, uri?}]
--- data ---
model: resources {geometry, rig, materials, scene}
placement_set: instances
world: grid, placements, scene
```

`provenance.sources` is the absent-vs-unasked record (`CANONICAL_MODEL.md`
"Settled"): per table, because the real failure is one empty table among
good ones. `exported_at` lets someone without husk date a bundle.

### Structural, not described: `schema/`

The format is defined by JSON Schema files in `schema/` (one per kind plus
shared definitions), embedded into the husk binary. That is the single
source of truth; prose like this file explains it but does not define it.

- **Instance check** (`husk validate-bundle`, and every test that writes a
  bundle): the JSON Schema, then what a schema can't say. That covers
  BufferSlice arithmetic, that files exist, `x-index-into` ranges, and
  referenced placement sets, validated recursively.
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

## Embed or reference — one rule

A payload is either external (`uri`, a relative path) or inline. Either way the
consumer only reads the manifest, so it never has to care which.

This generalizes two features that are currently separate special cases:
`--slim-textures` (write textures beside the `.glb` instead of embedding) and
`aux_models/` (write gear item models beside it). Same rule, stated once.

## glTF's demoted role

`husk export --format glb` stays, and stays valuable:

- the Khronos validator and `tests/test_conformance.cpp` are real regression
  value that a new format does not have on day one;
- `tools/live_gallery`'s three.js viewer consumes it;
- it remains the interchange format for anyone outside this project.

What changes is its authority. It is explicitly **not** the schema of record. It
may omit what it cannot express, encode things in `extras`, or refuse a feature
outright — and the canonical model does not bend to accommodate it. In
particular, the fake geoset joints exist to serve glTF and should not survive
into the bundle.

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
  "uri":  "aux/mainhand_370361/manifest.json" }
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
its own — see `DESIGN.md`'s material design section for what that manifest
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
  model (no listfile, file not extracted locally) — today that surfaces as an
  empty `auxGlbPath` and a `note:` on stderr. Keep that: a reference with an
  `id` but no `uri` is a real, readable state meaning *"this exists, husk knows
  what it is, husk could not produce it"*. That is strictly more useful than
  omitting the entry, and it is I6 doing its job.
- **Don't assume acyclicity for free.** Character→item is a DAG in practice, but
  a consumer walking references should be cycle-safe rather than trusting the
  producer. Cheap to do, expensive to retrofit.

## Left to implementation judgment

Whether `aux/` nests fully self-contained bundles or shares the parent's
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
  same "resolve once, bake the answer in" move `GearItem::auxGlbPath` already
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
FileDataID when the listfile knows it, named `<map>_<x>_<y>`).

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
`LiquidObject` (absent when the instance stored a vertex format instead).
With the liquid DB2s, `resources.liquid_types` (terrain and WMO documents
alike) lists each named type once: its name, material and texture slots,
each slot's frames in animation order as ordinary texture entries. A
terrain's `resources.terrain.liquid_objects` gives each object's flow and
flags. Without the DB2s both tables hold identities only. A liquid's own
arrays (`depth`, `uv`) say what it stores: Ocean's material claims height
and depth, but its liquids store depth only.

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
- `scene`: what the map places as a whole, world frame. `fog_volumes`
  (`_fogs.wdt`) and `ambient_particle_volumes` (`_mpv.wdt`; each carries a
  model's `resources.scene.ambient_particle_volumes` entry as `volume`,
  plus world bounds, id and the box tree). Every azeroth fog and particle
  volume lands on one of its cells. Fields still uncharacterized are under
  `unresolved`, raw records as slices of `world.bin`.

