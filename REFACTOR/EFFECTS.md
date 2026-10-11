# EFFECTS.md — fog, volumes and particles in the canonical format

**Status: proposed (2026-10-11), not built.** Nothing here exists in
`src/` or `schema/` yet. It merges two inputs:

- husk's fog-model proposal (this file's first version), which designs from
  the client's fog record;
- MantleCore's consumer requirements (`reports/husk_effects_handoff.md`,
  local and gitignored; MantleCore's side is its `design/effects.md`).
  Decisions marked **(Luna)** come from there.

Today the bundle carries each source in its own shape: `canon::LocalFog`
mirrors `MFOG`, and `canon::FogVolume` mirrors `VFOG`+`VFEX`+`VFE2`,
`unknown18` and all. M2 emitters are `canon::ParticleEmitter`/
`RibbonEmitter`. Source facts are in `../WIKI_FINDINGS/WORLD.md`, open
items in `../TODO/WORLD/FOG_VOLUMES_TODO.md`, and the client's fog code in
`../SHADER_FINDINGS/notes/fog.md`.

## Principles

1. **An effect never renders itself (MantleCore).** The format declares
   what is in the world: a medium, particles, a light. The engine's fog
   and lighting draw it.
2. **Two layers, both canonical:**
   - **World content**: physical quantities, read by every MantleCore
     renderer.
   - **Style**: how the classic client drew it (the fog record, ambient
     colour regions, exit blends, the underwater look, blend modes). Only
     the classic-style mode reads it.

   WoW data is mostly style. World content is mostly derived from it, or
   left out.
3. **Vocabulary from the target, never from the source.**
   - Style keys are named after the client's fog record roles (shader
     code, `fog.md`).
   - World-content keys are named after physics.
   - No key names a chunk or a source offset.
4. **Every value says where it came from.**
   - `source`: read, meaning known.
   - `derived`: computed by a named rule MantleCore can switch off.
   - `authored`: written by a person. husk never writes this; MantleCore's
     tools may.

   Fields not yet understood climb the evidence ladder (below) instead of
   becoming keys.
5. **Absent means "not stated".** husk invents no defaults. A missing
   style field inherits from the enclosing fog; a missing world-content
   field is MantleCore's to default.

## Shape

### An effect source

The unit is one **source**, a shared definition in
`common.schema.json`:

```
source
  id            string, unique in its list
  parent        id of another source in the same list (embers under a fire); optional
  anchor        world | instance | joint (Ref into the anchoring model's rig)
  region        where (below)
  gate          when: world facts only, a 0..1 weight
  priority      essential | ambient | decorative
  content       what is in the world: medium | particles | light_proxy (below)
  style         the classic client's look for this source; optional
  provenance    which fields are derived, and by which rule
  unresolved    fields not yet understood (evidence ladder)
  source        raw flags and record id, for provenance only
```

`content` and `style` are both optional, but a source has at least one. An
`MFOG` sphere with only a linear fog has a style and a derived medium. A
`MAVD` ambient region has a style only. `MPVD` dust has content only.

### Regions

Every region has `edge`, the soft fade width in yards (0 = hard).

| Kind | Fields | Filled by |
|---|---|---|
| `everywhere` | — | map default zone light, `MAVG` |
| `sphere` | `center`, `inner_radius`, `outer_radius` | `Light` zones, `MFOG`, `MAVD` |
| `box` | `center`, `size`, `rotation` | `MPVD`, `PVMI` |
| `planes` | outward planes; inside where all hold | `MBVD` |
| `shape` | `model` Ref, transform, `radius[3]` | `VFOG` |
| `box_tree` | `PVBD`'s hierarchy, as a stored index | `PVBD` |
| `above_ground`, `capsule`, `spline_tube`, `from_parent` | MantleCore's sketch | no WoW source |

The last row is added to the schema when the first producer (MantleCore
authoring) needs it, per "abstractions are earned". It's recorded here so
the shape leaves room. `from_parent` (spawn where the parent's heat
exceeds a threshold) is the same.

Two facts the box fields wait on: whether `MPVD`'s extent is full or half
size, and the Euler axis order. Until one is verified, the box carries
`unresolved` fields, not a `size` and `rotation` husk might have guessed
wrong.

### Gate and priority (MantleCore)

The gate takes **world facts only**, as a weight that fades.

| Gate input | WoW source |
|---|---|
| `placement_set` | `MFED`; `MAVD`/`MBVD` `doodadSetId` |
| `enabled` | `VFEX` enable (3 of 1,542 are 0; written `false`, kept) |
| `sequence` | M2 emitter `enabledIn`: the anchoring instance's animation state (new; MantleCore to confirm) |

Two inputs are kept out of the gate:
- **Viewer conditions** ("camera underwater"): the underwater layer of
  `MFOG` becomes a style **look**, not a gate.
- **Quality levels**: `VFOG.fogLevel` becomes `priority` by the rule
  `fog_level_to_priority` (0 → essential, 1 → ambient, 2 → decorative).

### Content

| Kind | Fields | Notes |
|---|---|---|
| `medium` | `extinction` (1/yard), `albedo` (linear RGB), `anisotropy` g, `feature_size` (yard), `hold` (`pinned`/`base`/`seed`) | `hold` only here; the loader rejects it elsewhere |
| `particles` | `spawn` (density per yard³/yard²/yard of spline, plus rate), `response_time` (s), `motion` (velocity, spread, gravity, buoyancy, drag, spin), `life` (curves over normalized age, each naming its clock), `look`, `light`, `deposit` | |
| `light_proxy` | colour or temperature, intensity, `follows` | |

`look` is one of:
- `point`;
- `quad`: a texture Ref, an atlas `{tiles_x, tiles_y, frames}`;
- `streak`;
- `ribbon`;
- `mesh_instance`: a model Ref, plus orientation from velocity or spin.

`light` is either `emission` (radiance, or `self_temperature` with
cooling, times a tint) or `albedo`.

`heat`, `force` (wind, vortex, attractor, curl noise) and `emitter`
(injection rates) are in MantleCore's sketch. No WoW source fills them, so
they join the schema with their first producer, like the reserved regions.

### Style

The classic client's vocabulary, from `fog.md`:

- **`distance_fog`**: the shader fog record in blocks. Each block and field
  is optional, and absent means inherit.
  - `linear`: `start`, `end`, `color`.
  - `exponential`: `density`.
  - `height`: `plane`, `scale`, `density`, `falloff` (cubic), `color`
    (near → far).
  - `color_gradient`: `near_color`, `far_color`, `start`, `length`.
  - `sun`: `color`, `angle`, `strength`.
  - `curves`: `near`, `far`, `normal`/`height` (cubics),
    `curve_vs_exponential`.
- **`volumetric_fog`**: the froxel pass's colour and intensity. It drives a
  different pass than `distance_fog`, so it is not folded into it.
- **`ambient_color`**: today's three raw colours and their unknown-flag
  selector.
- **`exit_blend`**: `MFOG` flag 0x1. It scales the enclosing fog down near
  the model's exits, and has no medium.
- **`looks`**: alternative styles under a viewer condition. The only one
  known is `underwater` (`MFOG`'s second layer, a `distance_fog.linear`).
- **`draw`**: per-emitter classic draw state (blend mode, render flags,
  priority plane) for the classic-style mode.

`LightData`'s columns line up with the fog record's roles almost one for
one. Its three 4-float coefficient arrays match the shader's three cubics
(`[9]`, `[10]`, `[11]`). This is a name match, unverified:

| Fog record role | `LightData` column |
|---|---|
| `[0].y` end, `[0].z` density | `FogEnd`, `FogDensity` |
| `[0].x`/`[0].w` start offsets | `FogStartOffset`, `FogScaler` |
| `[2]`, `[3].w`, `[4].x` height plane, scale, density | `FogHeight`, `FogHeightScaler`, `FogHeightDensity` |
| `[9]`, `[10]`, `[11]` cubics | `FogHeightCoefficients`, `MainFogCoefficients`, `HeightDensityFogCoeff` |
| `[12].y`/`[12].x` curve near, far | `MainFogStartDist`, `MainFogEndDist` |
| `[8].w` vertical band | `FogZScalar` |
| `[3]`→`[4]`, `[6]`→`[13]` colours | `EndFogColor`, `FogHeightColor`, `EndFogHeightColor` |
| `[5]`, `[7].x` sun | `SunFogColor`, `SunFogAngle`, `SunFogStrength` |

### Clocks

Every animated value names its clock:

- `age`: normalized particle age.
- `seconds`: `MPVD`'s envelope.
- `day_time`: `LightData.Time`.
- `sequence`: per-sequence curves of an M2 emitter. This one is new to
  MantleCore. An M2 emitter's rate, speed and gravity are keyed per
  animation sequence of the model that carries it, so they can't be
  flattened to `seconds`.

### Provenance

There is one `provenance` per source, not one per value. Per value
would double every number in the file. It maps field paths to how each
was obtained, and paths not listed are `source`:

```json
"provenance": {
  "content.extinction": {"kind": "derived", "rule": "linear_fog_to_extinction"},
  "priority": {"kind": "derived", "rule": "fog_level_to_priority"}
}
```

MantleCore switches a rule off by ignoring the fields it produced. The
style value is always there to compare against.

### Unknown data: the evidence ladder

| Rung | Means | Where |
|---|---|---|
| semantic | role known | typed key |
| candidate | role proposed with evidence | `unresolved` entry with `candidate_role` |
| structural | value class known (colour, unit vector, FileDataID, count, angle) | `unresolved` entry with `kind` |
| raw | only an offset | `unresolved` entry, plus the raw record slice |

There is one `unresolved` shape for every source:
`{fields: [{field, kind, value, candidate_role?}], records: [{field, slice}]}`.
`field` is a value, so source names are allowed in it; the lint forbids
chunk tags in keys only.

A `candidate_role` is a hypothesis carried as data. MantleCore's debug view
can apply candidates and compare against an in-game screenshot. A
confirmed candidate is promoted to its typed key (a schema minor bump).

Boundary rules:
- **Always-zero fields are asserted, not carried.** This covers
  `VFEX.Unk3..8`, the `VFE2` tail, `MFED.unk1` and `MPVD`'s tail. A nonzero
  one becomes an `unresolved` field and a warning.
- **Proven duplicates are checked, then dropped.** This covers `PVMI`'s
  repeated z values, and cross-reference ids once used for pairing.
- **Flags split into named bits.** Only the unknown bits go to
  `unresolved`; the raw word goes to `source.flags`.
- **Placeholders are not sources.** A WMO's "empty" `MFOG` record means
  "use the enclosing fog". It is recognized by content, not by index:
  record 0 is the placeholder in 3,631 of 4,000 seeded roots, a later
  record never is, and in the other 369 roots record 0 is a real fog.
- **Units convert at the boundary, for typed fields only.** Unresolved
  values stay as stored.

### Units

Keep yards, declared in the contract header's `space` like every other
document, with `x-unit` on each physical field (`1/yard`, `K`, `second`).
MantleCore converts once at load. This departs from MantleCore's sketch
(metres). The reason: one export tree with two length units invites
exactly the mixed-unit bug the header exists to prevent.

## Derivation rules

Each rule is named in `provenance` and switchable. husk derives only
where a source quantity maps by a stated formula. Where WoW holds no
evidence at all, the field stays absent.

| Rule | Computes | From |
|---|---|---|
| `fog_density_to_extinction` | medium `extinction` = the exponential density (the shader's `exp(−d·density)`, d in yards) | zone fog, `exponential.density` |
| `linear_fog_to_extinction` | `extinction` = 3 / (end − start): 95 % opacity across the band | `MFOG` linear fog |
| `fog_color_to_albedo` | `albedo` = fog colour, linear | zone fog, `MFOG` |
| `fog_level_to_priority` | `priority` | `VFOG.fogLevel` |
| `additive_to_emission` | particle `light.emission` = colour × opacity as radiance | M2 additive blend modes |
| `alpha_to_albedo` | particle `light.albedo` = colour | M2 alpha blend modes |
| `count_to_spawn_density` | `spawn` per yard³ = count / box volume | `MPVD`/`PVMI` (once box size is verified) |

**Wind response time** (MantleCore asked): husk derives none and leaves
it to MantleCore's default. No WoW field informs it, so a per-class rule
husk wrote would be an authored guess dressed up as a derivation.

## Where today's sources land

| Source | Anchor, region | Content | Style |
|---|---|---|---|
| Zone fog (`Light`/`LightData`) | world, `everywhere` or `sphere` | medium (`base`, derived) | `distance_fog` by `day_time` |
| WMO `MFOG` | instance, `sphere` | medium (derived) | `distance_fog.linear`, `looks.underwater` |
| `MFOG` flag 0x1 | instance | — | `exit_blend` |
| `VFOG`+`VFEX`+`VFE2` | world, `shape` | medium (`pinned`), values absent until known | `volumetric_fog` |
| `MAVG`/`MAVD`/`MBVD` | instance, `everywhere`/`sphere`/`planes` | — | `ambient_color` |
| WMO `MPVD` | instance, `box` | particles, `quad` look per emitter slot | `draw` |
| Map `PVMI`/`PVPD`/`PVBD` | world, `box_tree` | particles | `draw` |
| M2 particle emitter | joint | particles; curves on `sequence`/`age` | `draw` |
| M2 ribbon | joint | particles, `ribbon` look | `draw` |
| M2 model particles | joint | particles, `mesh_instance` look | `draw` |
| M2 child emitters | joint | `parent` link | — |

Part membership is not a region. "The parts that list this fog"
(`MFVR`/`fogIds`) stays as each part's index list.

## Where effects live

Luna's decision is **one file per effect**. Applied to WoW data:

- **World- and WMO-placed volumes** are defined by their map or WMO and
  never shared, so they stay sources inside their document:
  - `world.scene.effects` for the map's;
  - `model.resources.effects` for a WMO's.
- **M2 emitters**: the M2 *is* the effect's file. Its emitters anchor to
  its joints and key on its sequences, which only exist in that model. So
  they stay inside the model bundle as `resources.effects` sources. A
  separate file per emitter would have to point back into the model's rig
  and sequence table, and the corpus has tens of thousands of them.
- **The `effect` document kind** (fourth kind, contract header unchanged,
  data = `parameters` + `sources`) is for reusable definitions:
  - MantleCore-authored effects;
  - any WoW definition proven shared. `MPVD`'s preset id at +0x30 is
    shared across unrelated files and is the candidate. Check whether
    same-preset records are identical apart from placement.

  Exported as `effects/<id>.effect.bundle`. A `placement_set` instance
  places one through `reference` with an `anchor` and `overrides` (exposed
  parameters only).

This reads Luna's "one file per effect" as "an effect is defined in exactly
one file", rather than "every emitter gets its own file". It is listed as
an open decision below.

## Implementation plan

Each phase ends with the full suite green, real-corpus checks on the seeded
sample, `validate-bundle` clean, and `MANTLECORE_MIGRATION.md` updated
with what changed.

**Phase 1: shared definitions.**
- Add `effect_source`, `region`, `gate`, `priority`, `provenance` and
  `unresolved` to `common.schema.json`, only the kinds WoW fills.
- Add matching `canon::` types in a new `canon_effects.hpp`.
- Extend the schema lint: `hold` only under `medium`; every source has
  content or style; `candidate_role` names an existing typed path.
- No writer changes yet. Done when schema tests cover each definition.

**Phase 2: style migration of existing data.** A reshape with no new
semantics:
- `LocalFog`, `FogVolume`, `AmbientVolume` → sources with `style` only.
- `MFOG` placeholders dropped, part fog lists remapped.
- Flags split; one `unresolved` shape; always-zero asserts at the
  parsers.
- The `MFOG` underwater layer moves to `looks`, flag 0x1 to `exit_blend`.

Removes `canon::LocalFog`/`FogVolume` and today's per-source `unresolved`
objects. Verified on the 4,000-root WMO sample and every `_fogs.wdt`.

**Phase 3: world content from what is known.**
- `MPVD`/`PVMI` → particles (`quad` look from texture and atlas; envelope
  on `seconds`).
- M2 emitters, ribbons and model particles → particles on joints, with
  the `sequence` clock. Their full M2 parameter set moves to `style.draw`
  or `unresolved`, by what is understood.
- `VFOG` → `pinned` medium with values absent.
- The derivation rules above, each with a unit test on a real record.

Blocked items stay unresolved and are named in the TODO: box size,
emitter float semantics, `VFOG` density.

**Phase 4: `LightData` zone fog.**
- New `light_db2` reader (same pattern as `liquid_db2`).
- A world-anchored source per `Light` zone: style `distance_fog` keyed by
  `day_time`, plus a derived `base` medium.
- The rest of the row (sky, ambient, direct light) stays out until
  lighting's own design (`LIGHTING_TODO.md`).

**Phase 5: `effect` documents.**
- Add the kind, its schema, the `effects/` tree, `placement_set` instances
  with `anchor`/`overrides`, and `validate-bundle` following effect
  references.
- Then the preset-dedup check. Only if it proves sharing do `MPVD` presets
  move out to effect files.

**Phase 6: research, as candidates.**
- `VFE2` against `LightData` distributions.
- `MFOG` flag 0x1000 (10 sampled roots: record 0 with flag 0x1000, zero
  radii, end 16000).
- `fogIds` slot 0 as the colour record.
- `PVPD`.

Each result enters as a `candidate_role` first, then gets promoted.

Phases 1–3 change the bundle shape MantleCore reads. Phase 2 alone is a
breaking reshape of fields MantleCore may already consume.

## Open decisions (Luna)

- **What "one file per effect" means.** Recommended: an effect is
  defined in exactly one document, so WoW emitters and volumes stay in
  their model or world, and `effect` documents hold reusable or authored
  definitions. The alternative is a file per emitter or volume, placed by
  its model.
- **Units.** Recommended: yards plus the header, so MantleCore converts.
  The alternative is metres for effect content only.
- **Lists.** One `effects` list per document with a `content` kind is
  proposed: MantleCore reads either way, and the part index refs (fog,
  ambient, particulate) become refs into it. Per-content lists would keep
  today's three ref targets.
- **`MFOG` placeholders.** Recommended: drop them and remap. The
  alternative keeps them as `inherit` entries so indices stay
  source-stable.
- **The `sequence` gate and clock.** MantleCore needs to accept these,
  since an instance's animation state is world state, not viewer state.
