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
(`husk::writers::writeBundle`) — that file's own doc comment is now the
canonical, byte-precise description of the manifest shape (BufferSlice
fields, `canon::Ref` serialization, per-section layout for
`resources.mesh`/`skeleton`/`animation`/`materials`); this document only
records that the question is closed and points there rather than
duplicating the schema. Covered by `tests/test_writers_bundle.cpp`.

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

`name_source` is one of `m2_embedded` / `adt_embedded` / `listfile` / `db2` / `synthesized` /
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

## Manifest structure

```
schema_version        <- the thing that does not exist today
exported_at           <- wall-clock timestamp, ISO 8601 -- see below
producer              <- husk version + build
model                 <- Ref for the model itself
definition            <- I5: what exists and is valid
selection             <- I5: what this instance chose
resources
  mesh                <- geosets as real data, not tag joints
  skeleton            <- joints, bind pose, corrections
  animation           <- clips; one curve representation
  materials           <- Refs + real texture layers
  physics             <- full joint graph, no transport-driven reduction
  attachments         <- attachment points, events, lights (written)
  emitters            <- ribbon/particle, every field and curve (written)
  collision
items                 <- I7: identity → slots[] → components[]
references            <- other bundles this one points at (see below)
sources               <- which sources husk could actually ask, per table
```

`schema_version` is the load-bearing addition. Today's 13 root-joint extras keys
and 34 material extras keys have no version at all, so a consumer cannot tell a
husk that predates a key from one where the key is legitimately absent.

`exported_at` answers the same question for someone with no husk to check
`schema_version` against at all — a real, offline consumer holding a bundle
years later, with only a vague memory of "feature X landed sometime early
2026." A version number alone can't be reasoned about without husk's own
version history in hand; a calendar date can, on its own, by anyone. Cheap to
add, and it's exactly the same "make the file explain itself" motive as
`schema_version` — just aimed at a human instead of a machine.

`sources` is the manifest half of `CANONICAL_MODEL.md`'s "absent vs. unasked"
answer: a flat record of which DB2 tables / listfile / texture directories were
actually readable at write time, so an empty subtree elsewhere in the manifest
can be read as *"the model has none"* rather than *"nobody asked"*. Per table,
not per run — a single 0-byte table beside a directory of good ones is this
project's documented real failure mode, not a hypothetical.

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

**Current, not target**: written today by `husk export-terrain` (one tile)
and `husk export-world` (every tile of a map or world). The exact manifest
schema is the doc comment in `src/writers/terrain_bundle_writer.hpp`; this
section is what a consumer needs on top of it. `kind` is `"terrain_tile"`,
`schema_version` `"0.1.0"`, slices and `Ref`s as everywhere else.

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

WoW world space, yards: +X north, +Y west, +Z up. Every position in the
manifest is already in it; nothing is chunk- or file-relative.

A tile is 16×16 chunks (`index = grid_y * 16 + grid_x`, `grid_x` stepping
−Y, `grid_y` stepping −X). A chunk holds 145 absolute heights, interleaved
as 9 corners then 8 centres per row pair. Corner (r, c) sits at
`origin − (r, c) · quad_size` along (X, Y); centre (r, c) at
`origin − (r + 0.5, c + 0.5) · quad_size`. Each quad is four triangles
fanned around its centre, counter-clockwise seen from +Z; a quad whose
`hole_rows` bit is set (bit c of byte r) has none.

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
weights only as ratios; their absolute scale is an open question
(`WIKI_FINDINGS/WORLD.md`).

### Liquid

One entry per `MH2O` instance: the chunk, a quad rectangle inside its 8×8
grid, a per-quad `quad_exists` mask, and `(width + 1) · (height + 1)`
absolute vertex heights. `height_source` says where they came from:
`heightmap` (stored), `min_height_level` (none stored: flat at the
instance's minimum, the open-ocean case) or `zero` (a depth-only layout).

### Placements

`model` placements are M2s, `map_object` placements are WMO buildings.
`position` is world-frame, `rotation` is a world-from-model quaternion
(x, y, z, w), the model in its own native M2/WMO space. **WMO geometry is
not exported**: `map_object` placements are identity and transform only,
with no `uri`. A placement whose tile names it by path and whose path the
listfile doesn't know has no identity, only its raw path as
`name` (`name_source: "adt_embedded"`).

### Seams between tiles

Each tile stores its own copy of its edge vertices; husk does not weld
them. Each height is the tile's own `MCNK` base height plus its `MCVT`
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
