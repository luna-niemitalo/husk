# PLACEMENT_SETS.md — placement sets, and the WMO bundle as their first user

Agreed with Luna 2026-10-09; the export target is the canonical bundle
(`BUNDLE_FORMAT.md`), consumed by MantleCore. **Built**: `canon::
PlacementSet` (`src/canon_placement.hpp`), WMOs as ordinary object bundles
that own their doodad sets (`husk export-wmo`, `src/wmo_canon_input.cpp`).
The byte-level schema is in `src/writers/bundle_writer.hpp`. **Not yet**:
terrain onto sets, WMOs in `export-world`, liquids/lights/fog inside WMOs.

## The concept

A **placement set** is a thin list of "put this asset here". It is not
building-specific: a WMO's furniture, a terrain tile's doodads and
buildings, a housing layout, or a hand-made scene are all sets. Anything
may own or reference one, and one set may be referenced from several
places.

```
PlacementSet
  ref        Ref                 name (e.g. MODS "Set_$DefaultGlobal") + source
  instances  [Instance]

Instance
  id         uint32              stable within the set; how a runtime addresses it
  asset      Ref                 identity of the placed asset (+ uri in a bundle)
  translation, rotation, scale   relative to the set's parent frame
  tint       optional RGBA + multiplier   (WMO MODD color, MDDI colorMult)
  flags      uint32, raw         source-format flags, meaning per source
  activeSets optional [Ref]      for an asset that owns sets: which of them are on
```

An asset that owns sets lists them, with the ones that are always on
marked (WMO set 0, `Set_$DefaultGlobal`, is additive and always shown).
husk resolves source-format indirections at export time, so a consumer
only ever sees final `activeSets` lists. One example is ADT `MWDR`/`MWDS`,
where one placement turns on several WMO sets.

## Invariants (so runtime placement and housing stay easy)

1. **Never bake an instance into its surroundings.** No placed asset's
   geometry, collision or lighting is merged into its parent's. An instance
   is only a reference plus a transform plus per-instance parameters, so
   moving, adding or removing one at runtime touches nothing else.
2. **Instances have stable ids**, unique within their set, so a runtime can
   address, move or delete one and a saved layout can name it again later.
3. **Transforms are local TRS relative to the set's parent frame**, never a
   pre-composed world matrix. Sets nest: an instance may place an asset
   that owns sets of its own, and the consumer composes the chain at
   runtime.
4. **Per-instance overrides live on the instance**, never as edits to the
   asset. Two placements of one WMO with different active sets share one
   WMO bundle.
5. **An asset never knows where it is placed.** Nothing in an asset bundle
   refers to its placements.
6. **A set is plain data a consumer can write.** No husk-only field is
   needed to make one, so a housing editor can author sets in the same
   format husk exports.

Authoring-time data that goes stale when an instance moves is labelled as
such, not dropped. A WMO doodad's `tint` is the light sampled at its
authored spot; after a move, a runtime may recompute it or ignore it.

## Physical form

Same "embed or reference — one rule" as every other resource
(`BUNDLE_FORMAT.md`): a set is written as its own small JSON file
(`sets/<index>.json`: `schema_version`, `ref`, `instances`), and an owner
references it by relative `uri`. Asset references inside a set are `Ref`s
with a `uri` to the asset's own bundle when one was exported. A
standalone set file is a complete document. A consumer can load one with
no owner at all, which is the housing and hand-made-scene case.

## First user: the WMO bundle

A WMO is an ordinary object bundle, the same shape as a doodad's (Luna,
2026-10-09), plus references to the placement sets it owns:

- the mesh: no skeleton; each WMO group is a mesh **part**
  (`resources.mesh.parts`), its batches primitives tagged with that part;
  every group's vertices in one shared buffer, with `MOCV` as colour sets
  and up to three UV sets. Base LOD only so far.
- `resources.materials`: `MOMT`, the same `canon::Material`/layer shape as
  M2, plus the shader name, the shared unlit/unfogged/two-sided bits and a
  constant UV scroll from `MOUV`.
- `resources.placement_sets`: one set per `MODS` entry, each a
  `sets/<index>.json`, instances from `MODD` (+ `MODI`/`MDDI`), assets
  pointing at the shared model bundles. Set 0 is always on.
- Later, in the agreed order: liquid (`MLIQ`), lights (`MOLT`, lightsets,
  `MNLD`), fog and particulate volumes, then collision, portals and LOD.

Terrain bundles move onto the same format: a tile's `MDDF`/`MODF`
placements become one placement set (world frame), and a `map_object`
instance gets `activeSets` from `MODF.doodadSet` (or `MWDR`/`MWDS`). The
terrain bundle's current `placements` array is what MantleCore reads
today, so that switch is a schema change coordinated with MantleCore, not
done silently. **Decided (Luna, 2026-10-09)**: it ships in the same change
that first gives `map_object` placements a real WMO bundle `uri`, so
MantleCore adapts once.

## Rejected

- **Sets inline in the owning WMO's manifest only.** Simpler, but a set
  then can't exist without a building, which rules out housing layouts
  and shared sets.
- **Baking furniture into the WMO's mesh.** Smaller output, but it breaks
  invariant 1: nothing could be moved or toggled at runtime.
- **Exporting one fixed doodad-set selection per WMO.** There is no data
  to pick a "right" one, and per-placement selection is real (`MWDR`/
  `MWDS`), so every set is exported and placements choose.
