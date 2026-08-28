# CANONICAL_MODEL.md — stage 3, the semantic model

**Target, not current state.** See `REFACTOR/README.md` for the pipeline and the
invariants referenced by number below.

## Built pure, not promoted

There is a cheaper path available: rename `gltf::Skeleton` to `canon::Skeleton`,
strip `auxGlbPath` and `geosetTags`, and call it done. **That path is rejected**,
and the reason is recorded here so a later session doesn't quietly re-argue it:

> An interim shape becomes load-bearing faster than it gets replaced.

Everything built on top of a "temporary" canon locks it in — and this particular
temporary shape would carry glTF's own compromises into the layer whose entire
job is to be free of them: geosets as fake joints, extras on a heuristically
located carrier bone, a physics joint view deliberately reduced because extras
were the transport, gear keyed by slot because that's what one exporter needed.

`canon::` is designed from the semantics. Today's structs migrate *onto* it.

## The three layers

The Canonical Data Model doc's model, mapped onto data husk already parses —
so this reads as a migration target, not a greenfield design.

| Layer | Question | Real WoW data | Where it lives today |
|---|---|---|---|
| **Definition** | What exists and what is valid? | `ChrCustomizationOption`/`Choice`/`Category`, per-race valid option sets, `CharComponentTextureLayouts`, geoset group semantics, the animation catalogue | the `chr_customization_options` extras blob |
| **Selection** | What did *this* instance choose? | race/sex, chosen choice IDs, equipped items, creature display | `--appearance`, `enabled_geosets`, `creature_enabled_geosets`, `gear_*` |
| **Resources** | What things realize those choices? | mesh, skeleton, animation, material, physics, emitters, corrections, collision | `gltf::Skeleton` + `gltf::Mesh` + `gltf::Material` |

The distinction is not cosmetic (Canonical Data Model §4): `NightElf.face[12]` and
`Dwarf.face[12]` are different resources reached by the same index. A selection
is only meaningful against the definition that scopes it. Today both live as
sibling extras keys with nothing marking which is which (I5).

Definition is also not character-only. A creature's
`CreatureDisplayInfoGeosetData` is the same definition+selection pair wearing
different table names — one model, two sources.

## What distinguishes this from today's shape

Each item below is something the current tree structurally cannot express.

### `canon::Ref` — the I6 carrier

```
struct Ref {
    Identity   id;      // FileDataId | Db2Row{table,row} | RecordIndex | None
    string     name;    // decoration
    NameSource source;  // m2_embedded | listfile | db2 | synthesized | none
};
```

Every place husk emits a bare string name today emits a `Ref` instead —
textures, materials, bones, animations, geosets, gear, customization choices.

This matters because most naming in this project is *unreliable by
construction*: it comes from the community listfile, or husk synthesizes it
(`"bone_" + index`, `gltf_skeleton.cpp:136`). Right now a listfile guess and a
DB2 fact arrive downstream as identical plain strings. A `Ref` makes the
difference machine-readable, and keeps the real identity next to the pretty name
so a consumer can always fall back to the ID it can trust.

Note this generalizes past files: a bone has no FileDataID, so `Identity` is a
tagged union, not a bare integer. Identity is *whatever is canonical for that
kind of thing*.

### Geosets are first-class

`canon::Mesh` submeshes carry their real geoset id/group/variant. This retires
both current encodings at once: the fake tag joints
(`Skeleton::GeosetTag`) and the per-primitive extras. Canonical Data Model §13 names
the fake-joint trick specifically as the thing a canonical model exists to
eliminate.

### One curve representation

Today TRS animation (`gltf::JointAnimation`) and material tint/fade/UV animation
(`gltf::Material::AnimatedColorCurve` / `AnimatedScalarCurve` /
`AnimatedQuatCurve`) are the same concept — sampled keyframes over a sequence or
a global sequence — in incompatible shapes, because one became a glTF animation
channel and the other became extras. Canon has one.

### Version-collapsed physics

`PHYS v1..v5` decoders converge on one `canon::Physics` (Eventual Goals, "Treat
WoW File Formats as Serialization Formats" / "Build Version-Agnostic Semantic
Compilers"), carrying the **full** joint graph. `gltf::Skeleton::PhysicsJoint`
deliberately drops frame matrices and shape geometry because glTF extras
were the transport; that reduction is a writer decision and belongs in the
writer.

### Declarative population

`export_extras.hpp:70-80` documents in prose that `attachCustomizationChoices`
must run after `attachBoneCorrections`, because the former mutates entries the
latter created. `exportOneModel` (`cmd_export.cpp:1038-1054`) is nine
hand-sequenced `attach*` calls whose order is load-bearing and unenforced.

That constraint exists only because there is no model in which to state the
relationship. Canon states it — a correction set is *selected by* a choice —
and the ordering requirement disappears rather than being documented.

### Items by identity, not slot (I7)

Today gear is slot-keyed everywhere: `GearSectionOverlay::slot`,
`GearItem::slot`, and even the on-disk `aux_models/<slot>_<fdid>.glb` naming.
A tunic whose hem is a leg-slot piece therefore has no representation at all.

Canon inverts it:

```
Item { Ref identity; Slot slots[]; Component components[]; }
Component = Geometry{...} | SectionOverlay{...}
```

An item has an identity, occupies one *or more* slots, and contributes one or
more components. The current `gearItems` (standalone geometry) versus
`gearSectionOverlays` (texture overlay) split stops being two parallel top-level
lists and becomes two component *kinds* of one item — which also means an item
that is both is expressible.

**Scope discipline**: implementing outfit-level loading is explicitly *not* part
of this work. The requirement is only that the model not make it impossible.
Anything that assumes `1 item = 1 slot = 1 mesh` fails that test.

## Stage 1's missing piece: `m2::Model`

Canon is built from a coherent decoder output, not from twelve loose parse
calls. `m2::Model` is that output — the whole file, parsed once, with the three
commands consuming views of it rather than each choosing a different subset
(`AUDIT.md` §2.1).

This keeps the Eventual Goals "Build Version-Agnostic Semantic Compilers"
split honest: decoders produce a complete
format-shaped model, and canon is what interprets it.

## What canon must not contain (I1)

No filesystem paths. No `tinygltf::` types. No `bpy` concepts. No GPU or
API objects. No transport artifacts.

The architectural test, from Canonical Data Model §10: *could a software renderer,
Blender, and a GPU backend each consume this unchanged?* If a field only makes
sense to one of them, it belongs in that writer.

## Cost, measured rather than asserted

"It touches all 680 tests" is the kind of number that gets a correct decision
reversed, so here is the real one. Of 54 test files:

- **39 never mention `gltf::` at all** — the entire `test_m2_*`, `test_db2*`,
  `test_skin`, `test_skel`, `test_phys`, `test_bone`, `test_dbd`, `test_dump*`
  parse tier. Untouched by this migration. If any of them *does* need editing,
  that is the signal that stages 1 and 3 have leaked into each other.
- **15 mention `gltf::`**, and most are `test_cli_*` asserting on process output,
  which survives a writer swap.
- The genuine writer-tier rewrite is **~8 files**: `test_gltf.cpp`,
  `test_gltf_mesh.cpp`, `test_gltf_skeleton.cpp`, `test_gltf_math.cpp`,
  `test_export_skeleton.cpp`, `test_conformance.cpp`,
  `test_integration.cpp`, `test_integration_weapons.cpp`.

## Settled

- **"Absent" vs. "unasked" is recorded once per source, not per node.** The
  distinction is required — a consumer must be able to tell *"this model has no
  customization"* from *"no DB2 was available to ask"* — but it does **not**
  become a three-state wrapper on every canon field. Wrapping each subtree would
  tax every field in `canon::` to express a fact that is uniform across the whole
  run: source availability is decided by process-level flags (`--db2-dir`,
  `--dbd-dir`, `--listfile`), not per node.

  So canon subtrees stay plain optionals, and the bundle manifest carries a flat
  `sources` record stating what was available to ask — **per table, not per
  run**, because the real failure mode this project has already hit is a single
  table being a 0-byte extraction gap while the rest of the directory is fine
  (`texturefiledata.db2`, `chrcustomization*.db2`; see `CLAUDE.md`'s Hazards).
  A per-run boolean would have called those runs "DB2 available" and been wrong
  about exactly the subtree that mattered.

  `absent + source unavailable` = unknown; `absent + source available` = the
  model genuinely has none. Two facts, each recorded once, combined by the
  consumer — the same shape as I6's `NameSource`, which annotates the *name* it
  travels with rather than lifting every name into a wrapper type.

- **`canon::Definition` travels per model, and a model may reference other
  models** — a character references the item models it wears rather than
  absorbing them. Simpler than a shared per-race definition object (nothing to
  locate, version, or keep in sync), at the cost of duplicating a race's
  customization tree per model; deduplication stays available later as a
  storage optimization that does not change the model. See
  `BUNDLE_FORMAT.md` for how the reference is written and what an *unresolved*
  reference means.
