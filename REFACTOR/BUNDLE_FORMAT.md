# BUNDLE_FORMAT.md — the native husk bundle

**Target, not current state.** See `REFACTOR/README.md` for the pipeline and the
invariants referenced by number below.

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
not an open question.

```
model.husk/
  manifest.json          <- the only entry point
  mesh.bin
  skeleton.bin
  animation.bin
  textures/<name>.png
  aux/<item>/…           <- nested bundles, same shape
```

Binary payloads are separate files rather than a packed blob so that a human can
look at a texture with an image viewer and a diff can show which payload changed.

**I3, restated concretely**: a consumer opens `manifest.json` and never looks
anywhere else. Every path in it is relative to the manifest. There is no
"textures directory" parameter, no environment variable, no sibling-directory
convention to know, and no reason to invoke husk.

The existing `GearItem::auxGlbPath` (`cmd_export.cpp:695-705`) already works
exactly this way and is the precedent being generalized — husk resolves once, at
write time, and bakes in a relative path.

## Every reference is a `Ref`, never a bare name

This is the rule that makes the bundle traceable. A resource entry is:

```json
{ "role": "base_color",
  "id":   { "kind": "file_data_id", "value": 3492879 },
  "name": "scalpupperhair00_08",
  "name_source": "listfile",
  "uri":  "textures/scalpupperhair00_08.png" }
```

`name_source` is one of `m2_embedded` / `listfile` / `db2` / `synthesized` /
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
  emitters            <- ribbon/particle
  collision
items                 <- I7: identity → slots[] → components[]
references            <- other bundles this one points at (see below)
sources               <- which sources husk could actually ask, per table
```

`schema_version` is the load-bearing addition. Today's 13 root-joint extras keys
and 34 material extras keys have no version at all, so a consumer cannot tell a
husk that predates a key from one where the key is legitimately absent.

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

## Needs a decision before stage 1 hardens

**What encoding do resolved texture bytes carry?**

Not settled here, because it is not a taste question and it constrains two
layers at once — `RESOURCE_CATALOG.md`'s `textureBytes(...) -> Resolved<bytes>`
and this file's `textures/<name>.png` payload convention. Both are being written
right now; both are cheap to state and expensive to retrofit, because the cost
lands on every catalog call site *and* every bundle already written.

- **Today, implicitly**: decoded PNG. `resolveTextureBytes` decodes BLP→PNG at
  the source layer, the Blender script caches PNG, `--slim-textures` writes PNG,
  and the shape sketch above says `textures/<name>.png`.
- **The cost**: WoW's BLP is usually DXT1/3/5 — block-compressed, i.e. already
  in a form a GPU consumes directly. Decoding to PNG in the *sources* layer
  discards that, so any future runtime backend must re-decode and re-compress
  data that arrived ready to upload. That is `POTENTIAL_PLAN`'s §10 test failing
  in the one direction it is meant to catch: "texture encoding" is named there
  as a thing that belongs *below* the boundary, and a PNG-only catalog decides it
  above.
- **The alternative**: the catalog returns bytes tagged with their encoding
  (`{bytes, encoding: Bc1|Bc2|Bc3|Bgra|Palettized|Png}`), keeps the original
  blocks when it has them, and PNG becomes a *writer-side* transcode — which is
  where it belongs anyway, since PNG exists for glTF and Blender specifically.
  The bundle then stores whichever payload the writer chose, named by the `Ref`'s
  own `uri`, and a manifest reader learns the encoding from the entry rather than
  from a file extension convention.
- **What is not being claimed**: that a runtime backend is close, or that this
  should be built now. Only that the *tag* is nearly free today and unrecoverable
  later — once PNG is the only thing that ever reached the catalog's callers, the
  original bytes are gone from the pipeline entirely.

Recommendation: tag the encoding now, keep PNG as the only transcode husk
actually performs, and let the bundle payload stay `.png` until something needs
otherwise. Flagged rather than taken, because it changes the stage-2 surface
signature and the stage-4 payload rule together.
