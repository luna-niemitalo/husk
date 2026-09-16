# The canonical format — a semantic representation for recovered binary assets

This document explains `canon::`, the semantic data model and on-disk bundle
format under construction in this codebase (`husk`, a WoW M2 model exporter).
It is written for a reader who does not know this project: the format's core
idea is not specific to World of Warcraft, or even to game assets, and is
worth understanding on its own terms.

Design source: `POTENTIAL_PLAN/WoW RE — Canonical Data Model and
Implementation Boundary.md` (the original proposal). Migration/engineering
detail: `REFACTOR/CANONICAL_MODEL.md` and `REFACTOR/BUNDLE_FORMAT.md`. This
document synthesizes both with what has actually been built, and exists as
the one place that explains the *idea* independent of either the proposal or
the punch list.

---

## 1. The problem this solves

Reverse-engineering a legacy binary format is usually framed as a decoding
problem: figure out the byte layout, parse it, done. In practice the harder
problem shows up one layer up. A format like WoW's M2 (and its half-dozen
sidecar formats — skin, skeleton, animation, physics, texture, and several
database tables) encodes not just geometry but *meaning*: which parts of a
mesh are optional, which combinations of choices are valid, which numeric ID
means the same thing across two different files and which doesn't, which name
is a fact and which is a guess.

Every tool built against this kind of data tends to converge on the same
failure mode independently: the parser's output shape becomes whatever the
first consumer needed, then a second consumer needs something slightly
different and re-derives it its own way, then a third does too. The result is
several silently-diverging reimplementations of the same lookup, no single
place that says what a given piece of data *means*, and interchange-format
compromises (glTF's material model, glTF's joint-only hierarchy, a Blender
Python API idiom) leaking backward into how the source data itself gets
modeled. In this project that happened for real and is catalogued with
file:line evidence in `REFACTOR/AUDIT.md` — four independent, disagreeing
implementations of texture-slot resolution being the sharpest example.

The canonical format is the fix: one semantic model, built from the meaning of
the source data rather than from any target's constraints, that every writer
projects *from* rather than every parser feeding *into* a target format
directly.

## 2. Core principle

> The canonical representation is not an interchange format, GPU format,
> renderer format, or authoring-tool format. It represents the recovered
> *meaning* of the source data — how that meaning is later stored,
> transported, rendered, or executed is a separate concern.

A useful test for whether a piece of data belongs in the canonical model:
**could a completely different consumer — a different renderer, a different
authoring tool, a different target platform — use this fact unchanged?** If a
field only makes sense to one specific consumer (a GPU handle, a Python
object reference, a file path relative to one particular export), it belongs
in that consumer's own writer, not in the canonical model.

This single test is what the rest of the design falls out of.

### 2.1 Three layers, not one

Recovered domain data usually splits into three genuinely different
questions, and conflating them is the most common way a semantic model goes
wrong:

| Layer | Question | Example (character customization) |
|---|---|---|
| **Definition** | What exists, and what is valid? | "A Night Elf has faces, hair, and ears; no beard option exists for this race." |
| **Selection** | What did *this instance* choose? | "This particular character is a Night Elf who picked face 12." |
| **Resources** | What realizes that choice? | "Face option 12, for Night Elves specifically, has this diffuse image, this normal map, this corrective mesh data." |

The critical, easy-to-miss consequence: **an index is only meaningful within
the definition that scopes it.** `face = 12` means something different for a
Night Elf than for a Dwarf, even though both are the literal integer 12. A
canonical model that stores `face: 12` as a free-floating global fact has
already lost information — it has thrown away *which* definition the 12 was
chosen against. The fix is structural, not a validation rule bolted on after
the fact: a Selection always carries (or is scoped to) the Definition it was
made against, and a consumer resolves `(Definition, index) → Resource`
rather than treating the index as a global key.

This generalizes past character customization. Any format where the same
numeric or symbolic index means different things depending on context —
enum values reused across record types, per-table row IDs, a shader
"technique index" that only makes sense relative to one shader program — has
the same shape, and the same fix applies.

### 2.2 Identity is not the same fact as a display name

Recovered data is frequently *named* by unreliable means: a community-
maintained lookup table, a filename convention, a heuristic the recovery tool
invented because the source format itself carries no name at all. If a
pretty name and a load-bearing identity are stored as the same plain string,
a consumer downstream cannot tell "this is a fact from the source format"
apart from "this is someone's best guess" — and once several tiers of
guessing exist, silently trusting the wrong one is easy and has already
happened in this project (a guessed name overwriting a real DB2-sourced one,
for one instance).

The fix is a small typed carrier used everywhere a name is emitted:

```
Ref {
    id:     Identity   // the thing's real, structural identity, or None
    name:   string     // a decoration — human-readable, may be wrong
    source: NameSource // where the name came from: embedded in the source
                        //   format itself, a community lookup table, a
                        //   database record, synthesized by the tool, or
                        //   simply absent
}
```

`Identity` itself has to be a tagged union, not one integer field reused for
everything, because *what counts as identity* varies by the kind of thing
being named: a texture might have a stable numeric file ID; a database-backed
record has a (table, row) pair; something with no independent identity at all
(a bone in a skeleton, an array-position-only record) only has its position
in a list. Forcing all of these into one integer type either collides two
unrelated ID spaces or invents a fake global ID where none exists.

The general rule: **identity is whatever is canonical for that specific kind
of thing** — never a single number space imposed uniformly across an entire
model — and **a name is decoration with a stated provenance**, never
interchangeable with identity.

### 2.3 The canonical model must not leak downstream storage

The model may describe identity, relationships, meaning, valid choices,
selections, and references to semantic resources ("this face has a normal
image"). It must never describe *how* those resources are physically
represented: no memory addresses, no GPU handles, no API objects, no
authoring-tool object references, no output file paths. Those decisions
belong strictly on the far side of an "implementation boundary" — one
canonical fact should be usable by a software rasterizer, a modern GPU
backend, and an interactive authoring tool's own object model, without the
canonical fact itself changing shape for any of them.

A corollary that generalizes well: **physical layout is a separate
compilation problem.** The semantic hierarchy ("this face belongs to this
race") does not need to match how the data is eventually packed into buffers,
tables, or files. Choosing a physical layout is downstream compiler work, not
part of the model.

### 2.4 A concrete architectural consequence: don't design around one output target

Once a semantic model exists, an interchange format the project ships to
(this project's case: glTF, because Blender consumes it well) stops being
part of the architecture and becomes one writer among possible others — and
a "best-effort" one at that, since it is free to omit anything it structurally
cannot represent, encode it in whatever escape hatch it offers, or refuse
outright, without that limitation ever being allowed to shape the canonical
model itself.

This is the single biggest practical payoff observed while building this:
several ugly transport hacks existed purely because a downstream format had
no first-class slot for a real WoW concept — vertex-group-like mesh regions
represented as fake skeleton joints purely so an existing importer's bone-
weight machinery would carry them along; semantic metadata smuggled onto a
heuristically chosen "carrier" node's freeform extras field because the
target format's own schema had no home for it; a full physics joint graph
truncated because only a reduced view fit through the transport. None of
these are canonical facts about the source data — they are artifacts of one
particular output format's limitations, and once a real canonical layer
exists, every one of them becomes something the *writer* does, or stops being
necessary at all.

## 3. Generalizing beyond this project

Nothing above depends on M2, WoW, or 3D assets specifically. The pattern
applies to any project recovering meaning from a legacy, proprietary, or
otherwise opaque data source, with these preconditions:

- **Multiple format versions or variants exist that mean the same thing.**
  (This project's case: five incompatible on-disk physics chunk layouts,
  `PHYS v1`–`v5`, that all describe the same real concept — a rigid-body/
  joint graph. Version-specific decoders converge onto *one* canonical
  physics representation; the version differences never propagate past the
  decoder.) Any domain with several serialization formats for one underlying
  concept — old and new save-file versions, competing vendor formats for the
  same asset class, a schema that changed across a product's lifetime — has
  this shape.
- **Some values are contextually scoped, not globally unique.** Any format
  using small per-context indices (enum values reused per record type, row
  IDs scoped to a table, states scoped to a state machine instance) needs the
  Definition/Selection split from §2.1, or two unrelated things sharing an
  index will eventually get conflated.
- **Naming is unreliable or reconstructed.** Any recovery effort relying on
  community wikis, heuristic string matching, or best-effort naming needs the
  identity/name separation from §2.2 — otherwise confidence silently erodes
  as more naming tiers get added.
- **More than one consumer will eventually exist.** The instant a second
  renderer, exporter, or analysis tool needs the same recovered data, a
  format-specific representation (built for consumer #1) starts actively
  fighting consumer #2. Building the canonical layer *before* the second
  consumer shows up is cheaper than migrating onto it afterward — this
  project deliberately front-loaded that cost for exactly that reason.

The concrete building blocks — a `Ref`/`Identity`/`NameSource` triple, a
version-collapsing decoder-to-canonical boundary, a definition-vs-selection
split, an explicit "canonical must not leak downstream storage" test — are
reusable wholesale in any of those domains. Nothing about them is WoW-shaped.

## 4. Current implementation status

**This is a migration in progress, not a finished system.** The pure
semantic types exist and are independently tested; the old, format-specific
export pipeline they are replacing is still what runs in production. Nothing
below should be read as "how husk currently produces its output" — that is
still the pre-existing `gltf_*` pipeline. This section is what actually
exists in `src/canon_*` and `src/writers/` as of this writing.

### 4.1 What's built (`namespace husk::canon`)

All of the following are **pure value types plus pure assembly functions** —
no filesystem access, no output-format library, no authoring-tool
dependency. Each has been checked for *structural convergence* against the
existing production pipeline (same real WoW fixture files, same expected
values) without yet being wired in as the thing that pipeline actually uses:

- **`canon::Ref` / `canon::Identity` / `canon::NameSource`** (§2.2 above,
  implemented exactly as described) — a four-way tagged union for identity
  (no-identity, stable numeric file ID, database table+row, or plain array
  position) plus a decorated name with a five-way provenance tag (embedded in
  the source format, from a community lookup table, from a database record,
  synthesized by the tool, or simply absent).
- **`canon::Skeleton` / `canon::Joint`** — bind-pose hierarchy only. Bone
  corrections, physics, and customization-driven bone selection are each
  their own concern and are deliberately *not* folded in here.
- **`canon::Geoset`** — a mesh region's identity is first-class (an
  identity-scoped record-index reference, per §2.2), not encoded as a fake
  joint in the skeleton the way the current production writer does it. This
  directly retires the fake-joint transport hack named in §2.4.
- **`canon::Mesh`** assembly (`assemblePrimitiveGeosets`) — derives each
  primitive's real geoset identity from the source triangle-batch tables,
  proven to match production's own per-batch/per-primitive correspondence.
- **`canon::Material`** — texture layers keyed by *semantic role* (diffuse,
  specular, emission, ...; open-ended via a string escape hatch, since the
  source format asserts no fixed PBR vocabulary) rather than by output-format
  texture-slot index; a three-state texture reference (resolved / known to be
  unresolved / genuinely ambiguous) instead of collapsing "couldn't find it"
  and "found two plausible candidates" into one failure case.
- **`canon::Curve` / `canon::SequenceRef`** — one keyframe-curve
  representation shared by skeletal animation and by material-parameter
  animation (tint, alpha fade, UV scroll), which the current production
  writer represents as two incompatible shapes because one became a
  first-class output-format animation channel and the other became
  metadata bolted onto the side.
- **`canon::Physics`** — the full version-collapsed rigid-body/joint graph
  (all five real on-disk physics format variants converge here), keeping
  full frame-matrix and shape-geometry detail that the current production
  writer deliberately drops because its transport mechanism can't carry it.
- **`canon::Definition` / `canon::Category`** — per-model definition tree:
  real customization categories/options/choices, each choice able to name
  the geoset selection it implies. Scoped to one instance's definition
  source (a "character model" in WoW terms) rather than shared globally —
  see §4.3.
- **`canon::CharacterSelection`** — a chosen set of customization choices
  plus equipped items, each choice a `Ref` so it can be checked against a
  `Definition::Choice`'s own `Ref` by comparing like-shaped identities
  instead of unwrapping a bare integer against a struct field.
- **`canon::Item` / `Slot` / `GeometryComponent` / `SectionOverlay`** — an
  equippable item has one identity, occupies one *or more* slots, and
  contributes one or more components (standalone geometry, or a texture
  overlay on existing geometry). The current production model keys
  everything by slot instead, which structurally cannot represent one item
  spanning two slots (a garment whose hem is a separate leg-slot mesh, for
  instance) — canon's shape makes that representable without requiring that
  it be implemented yet.
- **`canon::Model`** — the composition root: one bind-pose skeleton, one
  mesh, one material per primitive, one animation clip per real sequence.
  Every fact it exposes was already independently derived and convergence-
  tested by the pieces above; `assembleModel` is purely a wiring function
  that decides how many times and with what arguments to call each builder
  — it does not re-derive any of their logic.

### 4.2 What consumes it today

Two writers exist over `canon::Model`, both new and both intentionally
independent of the legacy pipeline (no shared code, so neither can silently
inherit the legacy pipeline's own compromises):

- **The native bundle writer** (`src/writers/bundle_writer.*`) — see §5.
- **A "lean" glTF projection** (`src/writers/gltf_lean.*`) — deliberately
  writes *only* what maps onto a real core glTF field (mesh, skin, material,
  animation) and nothing else: no vendor extras of any kind. This exists
  partly as a proof that a canonical-model-driven writer is possible at all,
  and partly as a real-world demonstration that the legacy writer's
  extras-heavy, fake-joint-laden output was leaking implementation
  compromises that a spec-clean glTF file never needed in the first place.

Neither writer is the default output yet; both currently coexist alongside
the pre-existing, non-canonical export pipeline while the migration proceeds
piece by piece (see `REFACTOR/README.md`'s staged migration order — the
project's explicit policy is that a large semantic-layer migration like this
one gets steered live rather than attempted in one pass).

### 4.3 Deliberate, recorded simplifications (things the original proposal did not settle)

These are refinements made *during* implementation, not present in the
original design proposal, each recorded so a future session doesn't
re-litigate them from scratch:

- **"Absent" vs. "never asked" is a per-source fact, not a per-field
  wrapper.** A consumer legitimately needs to distinguish "this model has no
  customization data" from "no database was available to even ask" — but
  wrapping every single canonical field in a three-state optional to carry
  that distinction would be a heavy, uniform tax paid by every field to
  express a fact that is actually uniform across an entire run (which
  sources were available is a property of *how the tool was invoked*, not of
  any individual field). The resolution: canonical subtrees stay plain
  optionals; a separate, flat manifest record states which sources were
  available, at the *table* granularity — deliberately not per-run — because
  the real failure mode already observed in this project is exactly one
  table in a directory being empty/corrupt while its siblings are fine. A
  single "was the source available" boolean would have wrongly called such a
  run fully successful.
- **Definition data travels with each model rather than being deduplicated
  into a shared, globally-referenced table.** A record that references, say,
  "the wearer's race" duplicates that race's whole customization tree rather
  than pointing at one canonical shared copy. This is simpler (nothing to
  locate, version-match, or keep in sync across files) at the cost of
  redundant bytes on disk; deduplication is deliberately left available as a
  later, purely physical storage optimization that would not change the
  model's shape at all.
- **The physical bundle shape is a directory of loose files, not an archive
  or a single packed blob.** Chosen so a human or a plain text/diff tool can
  open one payload file in isolation and a version-control diff can show
  exactly which payload changed; byte-level deduplication across near-
  identical bundles is left to the filesystem layer rather than reinvented at
  the format layer.
- **A hard rule adopted mid-implementation: preserve the payload, never the
  proprietary container; human-readability is an obligation on output, not on
  storage.** Concretely: (1) nothing proprietary is stored as-is — a
  proprietary source blob is rehoused into a container a public tool can
  open, and a headerless dump of raw extracted bytes does not satisfy this,
  since "not proprietary" and "actually openable" are different bars; (2)
  stored data must be human-readable, or the format must ship a command that
  makes it so on demand — this is what allows a compressed image payload to
  stay compressed as long as a "give me a PNG" verb exists for it; (3)
  conversion happens on *output*, never on *intake* — converting the moment
  data enters the system looks convenient but permanently discards the
  ability to reproduce the original bytes, in exchange for a convenience
  that was one command away regardless. A canonical store that can't
  reproduce its own input isn't canonical.

## 5. On-disk serialization: the bundle format

The canonical model's own persisted form is a directory-shaped bundle:

```
model.bundle/
  manifest.json        <- the only entry point; describes everything else
  mesh.bin
  skeleton.bin
  animation.bin
  textures/<name>.dds  <- source pixel blocks kept verbatim, open container
  aux/<item>/…         <- nested bundles, same shape, for referenced items
```

Design goals, in order: (1) zero dependency on any interchange-format
library to read it back — not even reusing an existing format's own
buffer/accessor JSON shape; (2) any language with `fread`/`mmap` and a JSON
parser can open it; (3) large uniform numeric data (per-vertex attributes,
per-keyframe curve samples, per-joint bind data) lives as tightly packed,
headerless, native-endian raw bytes in a `.bin` file, while small or
non-uniform structural data (geoset ranges, joint names, material layer
descriptions) is plain inline JSON in the manifest.

The manifest names every raw byte range with one repeated shape, a
**BufferSlice**:

```json
{ "file": "mesh.bin", "byte_offset": 0, "byte_length": 49152,
  "component_type": "f32", "component_count": 3, "count": 4096,
  "semantic": "POSITION" }
```

`byte_length` always equals `count * component_count * sizeof(component_type)`
— a reader needs nothing beyond that arithmetic plus a byte-range read.
`semantic` borrows well-known GPU-vertex-attribute names purely because they
are widely recognized, not because the format depends on any GPU API or
interchange-format library to interpret them.

Every `canon::Ref` (§2.2) is serialized identically wherever it appears —
bone identities, geoset identities, material layer identities, texture
identities — as one tagged JSON shape per `Identity` alternative, so a reader
never has to guess which fields are meaningful for a given entry:

```json
{ "id": { "kind": "file_data_id", "value": 12345 },
  "name": "hairstyle_07",
  "name_source": "listfile" }
```

An interchange-format projection (glTF, or any future target) is explicitly
optional and explicitly best-effort: it may omit anything it cannot
represent natively, and it must never cause the canonical model or the
bundle format to bend toward its limitations. The bundle, not any projection
of it, is the source of truth.

## 6. Status summary

| Piece | State |
|---|---|
| Definition/Selection/Resources split, `Ref`/`Identity`/`NameSource` | Designed and implemented as pure types; used throughout `canon::` |
| `canon::Skeleton`, `Mesh`/`Geoset`, `Material`, `Curve`, `Physics`, `Definition`, `Selection` (`CharacterSelection`), `Item` | Implemented as pure value types + assembly functions; convergence-tested against the legacy pipeline on real fixtures; not yet the pipeline's actual source of truth |
| `canon::Model` composition root | Implemented; wires the above together for one real model |
| Bundle format (`manifest.json` + `BufferSlice` + raw `.bin` payloads) | Format settled and documented at byte-level precision; a real writer exists (`src/writers/bundle_writer.*`) |
| Lean glTF projection | A real, independent writer exists (`src/writers/gltf_lean.*`), proving canon can feed a genuinely separate output format |
| Legacy `gltf_*` pipeline | Still the pipeline actually shipping output; migration onto `canon::` is deliberately staged, not yet complete |
| `m2::Model` unification (one parsed-model aggregate feeding all consumers) | In progress — prerequisite for retiring the legacy pipeline's own duplicated parsing |

The project's own stated policy on this migration: it is being run as a
live, steered, incremental replacement rather than a wholesale rewrite,
specifically because a past experience on this codebase showed that a wide
refactor started while the target design is still moving gets thrown away.
Each piece above landed independently and was checked against real data
before the next piece began.
