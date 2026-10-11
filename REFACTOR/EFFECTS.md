# EFFECTS.md — fog, volumes and particles in the canonical format

**Status: in progress (2026-10-11).** Decisions marked **(Luna)** are
settled; the rest is proposal.

**Built**: the `effect` kind (`schema/effect.schema.json`; documents in
`effects/`, embedded and written as is), the input kinds, `everywhere`,
`sphere`, `box` and `planes` regions (with an optional `edge`),
`particles` content with a `quad` look, `distance_fog.linear`,
`exit_blend`, the underwater look, `ambient_color`, `particulate` and
`draw` as style (one block per element for a `for_each` source), the
shared `unresolved` shape, effect instances in placement sets
(`canon::EffectPlacement`; list, record and image inputs), `{set, id}`
instance refs, inline reference documents, and `validate-bundle`'s input
checks and placeholder warnings (`--release` fails on them). Sources on
it (Phase 2): WMO `MFOG` (`classic_local_fog`, `classic_fog_exit_blend`),
`MAVG`/`MAVD`/`MBVD` (`classic_ambient_global`/`_point`/`_box`), `MPVD`
(`classic_particulate_volume`), and the map's `VFOG`+`VFEX`+`VFE2`
(`classic_volumetric_fog`, `shape` region) and `PVMI`/`PVPD`/`PVBD`
(`classic_map_particulate_volume`, `box_tree` region) in the world
document's `fogs` and `particulate_volumes` sets. No source ships a raw
record any more: undescribed ranges are checked and kept only when set.

**Not yet**: M2's emitters (`canon::ParticleEmitter`/`RibbonEmitter`)
still carry their own shape in `resources.scene`; they need the `anchor`
field (joints) and an inline effects set.

The `anchor` field, gate, priority, provenance and the remaining content
kinds are not in the schema yet; each joins with its first source.

Source facts are in `../WIKI_FINDINGS/WORLD.md`, open items in
`../TODO/WORLD/FOG_VOLUMES_TODO.md`, and the client's fog code in
`../SHADER_FINDINGS/notes/fog.md`.

## Principles

1. **An effect never renders itself.** The format declares what is in the
   world: a medium (fog, smoke), particles, a light. The consumer's own fog
   and lighting draw it. The consumer simulates in a shared world field
   (density, heat, velocity), so everything is stated as world facts, never
   as how a client drew it.
2. **Two layers, both canonical:**
   - **World content**: physical quantities. Any renderer reads it.
   - **Style**: the classic client's look (fog record, ambient colour
     regions, exit blends, the underwater look, blend modes). Only a
     classic-style renderer reads it.

   WoW data is mostly style. Its world content is derived, or absent until
   understood.
3. **An effect is a behaviour, an instance supplies its inputs (Luna).**
   "Leaves falling" is one effect. Each tree is an instance of it that
   gives an emitter location and its own leaf texture, while the falling
   is governed by the effect. The same goes for ground fog: green, grey,
   orange and purple ground fog are one effect with a colour input.
4. **Vocabulary from the target, never from the source.** Style keys are
   named after the client's fog record roles (shader code). World-content
   keys are named after physics. No key names a chunk or a source offset.
5. **Every value says where it came from.**
   - `source`: read, meaning known.
   - `derived`: computed by a named, switchable rule.
   - `authored`: written by a person.

   Fields not yet understood climb the evidence ladder instead of
   becoming keys.
6. **Absent means "not stated".** husk invents no defaults. A missing
   style field inherits from the enclosing fog. A missing world-content
   field is the consumer's to default.

## Two tiers of representation

Every WoW effect source is represented in one of two tiers:

| Tier | What | Cost |
|---|---|---|
| **literal** | One effect per source mechanism: `classic_m2_particle_emitter`, `classic_m2_ribbon`, `classic_particulate_volume`, `classic_volumetric_fog`, `classic_local_fog`, `classic_zone_fog`, `classic_ambient_region`. Its inputs are the mechanism's whole parameter set, and each source record is an instance. | computable now for every source |
| **classified** | One effect per behaviour (`ground_fog`, `falling_leaves`, `tornado`, `embers`, `dust`, ...), with the few inputs that vary between its instances. Each source record is judged and mapped to one. | item by item; required before 1.0 |

The literal tier is the cheap, computable cop-out. It represents
everything faithfully but not *well*: a ground fog is "an M2 particle
emitter with these 60 parameters" rather than "fog over the ground, this
colour, this thick".

**The literal tier is a placeholder, and it is built to be removed before
1.0 (Luna).** Nothing is more permanent than a temporary solution, so
three layers keep it from staying:
- Every `classic_*` effect document carries `status: "placeholder"`.
- `husk export` and `validate-bundle` **warn** on every instance of a
  placeholder effect: a count per effect in the run summary, plus the
  instances in the report. With `--release` they **fail**.
- The 1.0 schema has no `placeholder` status, so a 1.0 document cannot
  contain one, whatever flags were passed. The format is at 0.1.0
  (`schema_version`).

What stays after 1.0 is the classic *look*, carried as `style` on
classified instances. The literal *behaviour* goes.

A classified instance keeps its literal instance's classic look as `style`,
so the classic-style render stays exact.

**Classification is data, not code.** It is a reviewable table in husk:
a source signature (M2 path family, emitter texture, parameter cluster)
maps to an effect and an input binding. It grows one behaviour at a time,
and every classified instance names its table entry in `provenance`.
Region kinds that only a judgement can produce, like `above_ground` for
ground fog or `spline_tube` for a tornado's axis, come from here.

## Shape

### Effect documents

A fourth document kind, `effect`, with the standard contract header:

```
kind        "effect"
subject     Ref: the effect (a synthesized name: "ground_fog", "classic_local_fog")
definition  status: "placeholder" (literal tier only; absent otherwise)
            inputs: {name: input spec}
selection   {} (an effect selects nothing; its instances do)
references  textures, atlases, meshes it always uses
sources     [source, ...]
```

Exported as `effects/<name>.json`. An effect's sources may have parents
within the effect (embers spawned where the fire's heat is).

### Inputs: husk defines the shape, the consumer the storage (Luna)

An input declares a **kind**: what shape the value is, never how it is
stored (an `image` is an image, not a DDS file). Each kind has one value
shape, so a spline can't be passed where a colour is wanted.
`validate-bundle` checks every instance's inputs against its effect's
declarations.

| Kind | Value | Declaration adds |
|---|---|---|
| `number` | number | `unit` |
| `integer`, `boolean` | integer, boolean | — |
| `choice` | one of `options` | `options` |
| `position` | 3 numbers, a point | (length unit from `space`) |
| `vector` | 3 numbers | `unit` |
| `direction` | 3 numbers, unit length | — |
| `rotation` | quaternion x, y, z, w | — |
| `color`, `color_alpha` | 3 or 4 numbers, linear 0..1 | — |
| `image` | a texture entry | — |
| `mesh` | an asset reference to a model | — |
| `spline` | a list of positions | — |
| `curve` | keyed values: per-animation curves (`seconds`) or over age (`age`) | `of` (a value kind), `clock` |
| `list` | a list | `of` (an input spec) |
| `record` | an object | `fields` ({name: input spec}) |

Input names follow the schema's key rules (snake_case, no source names).
An input spec may give a `default` and must give a `description`. Every
declared input is bound by at least one source: an input nothing reads is
an `unresolved` field, not an input.

**Binding.** A source field is either a literal or `{"input": "name"}`.
A source with `for_each: "<list input>"` is repeated per element, and
`{"each": "field"}` binds a field of the element. Which fields may be
bound is the schema's choice per field (regions and content, not ids).

### Style sits on the instance

The classic look is per-record data, never a template, so it is not bound
through inputs. An instance's `style` is keyed by the effect's source ids:
`style: {fog: {distance_fog: {...}, looks: {underwater: {...}}}}`. Inputs
carry what world content needs (geometry, textures, rates); style carries
the classic look. A classified instance keeps the literal instance's style
unchanged.

### Inline documents

A `reference` holds either a `uri` (`asset.uri`) or the referenced
document itself (`document`), per "embed or reference". `validate-bundle`
checks an inline document like a file of its kind. An M2's effects set is
inline.

### Warnings

`validate-bundle` reports problems as errors or warnings, and `--release`
makes warnings errors. The one warning so far is a placeholder effect's
instance. Export commands print the placeholder instance count per effect
in their summary.

### Dependencies point one way

Effects follow the pattern the map already uses. Dependencies point down,
and every document lists what it depends on in `references`:

```
world ─▶ tile ─▶ placement set ─▶ model ─▶ textures, meshes
                       │
                       └──────────▶ effect ─▶ atlases, meshes it always uses
```

- **An effect declares what it depends on**: the assets every instance
  uses, such as an atlas the behaviour itself defines. It knows nothing of
  who places it (`PLACEMENT_SETS.md` invariant 5: an asset never knows
  where it is placed).
- **Whoever instances an effect knows the effect.** The instancer's
  `references` lists the effect document. It also lists any asset it
  supplies as an input: the tree's leaf texture is the tree's dependency,
  not the leaf effect's.

### Effect instances are placement instances

Placing an effect is the same act as placing a model. An effect instance is
therefore a **placement-set instance whose asset is an effect document**,
with two additions to the placement instance shape:

```
Instance (placement_set.schema.json), additions in bold
  id          stable within the set
  asset       Ref to a model or an effect
  anchor      **joint (Ref into the owning model's rig); absent: the set's frame**
  translation, rotation, scale   relative to the anchor
  inputs      **{name: value}; only names the effect declares**
  style       **{source id: classic look}**
  gate, priority, provenance, unresolved   **effects only**
  selection   models only (activeSets)
  source      raw flags and the source record
```

What this buys:
- **Set switching is free.** `MFED` gives a fog the doodad set it applies
  with, and `MAVD`/`MBVD` do the same. Those instances go *into* that
  set's placement set, and the set's own switching turns them on and
  off. `placement_set` disappears as a gate input.
- **One list, no content-type split.** What an instance contains (medium,
  particles, a light, or all three, like a fire) is the effect's fact,
  stated once in the effect. Per-content lists would make every instancer
  restate it, and a fire would belong in three lists.
- **`anchor` is earned twice**: M2 emitters on joints, and gear attached
  to a character's joints (attachment points), which needs the same thing
  later.

Where the instances sit:

| Instancer | Set |
|---|---|
| WMO | its doodad sets (`sets/<index>.json`): an `MFED`/`doodadSetId` volume in that set, an ungated one in set 0, which is always on |
| map | a world placement set `effects`: map fogs, particulate volumes, zone fog |
| M2 | an `effects` set it owns, its emitters anchored to joints |

Under the "embed or reference" rule (`BUNDLE_FORMAT.md`), a set may be
written inline in its owner's manifest or as its own file. An M2's effects
set is inline: it is never shared, and a file per emitter-bearing M2 would
add tens of thousands of tiny files. A WMO's doodad sets stay files.

**Per-part references use stable instance ids, not list positions.** A WMO
group's fog list (`MFVR`/`fogIds`) becomes a list of `{set, id}`. So
`MFOG` placeholder records simply aren't instances: a part that listed only
the placeholder gets an empty list, and nothing else needs renumbering.

A tree M2's leaf emitter is an instance of `falling_leaves` in the tree's
own effects set, with a joint anchor and the tree's texture as an input.
The tile's doodad set places the tree, and so its effects.

### Regions

Every region has `edge`, a soft fade width (0 = hard).

| Kind | Fields | Filled by |
|---|---|---|
| `everywhere` | — | map default zone light, `MAVG` |
| `sphere` | `center`, `inner_radius`, `outer_radius` | `Light` zones, `MFOG`, `MAVD` |
| `box` | `center`, `size`, `rotation` | `MPVD`, `PVMI`, M2 plane emitters |
| `planes` | outward planes; inside where all hold | `MBVD` |
| `shape` | `model` Ref, transform, `radius[3]` | `VFOG` |
| `box_tree` | `PVBD`'s hierarchy, as a stored index | `PVBD` |
| `spline_tube` | spline points, radius profile | M2 spline emitters (type 3, literal); ribbons, tornadoes (classified) |
| `above_ground` | height band over the terrain's height field, not an absolute height | ground fog, swamp mist (classified) |
| `capsule` | two points, radius | first producer adds it |
| `from_parent` | where a parent source's field exceeds a threshold | first producer adds it |

Two kinds are added to the schema with their first producer, per
"abstractions are earned": `capsule` and `from_parent`. The others all
have WoW producers.

Two facts the box fields wait on: whether `MPVD`'s extent is full or half
size, and the Euler axis order. Until each is verified, it is an
`unresolved` field, not a typed key husk might have guessed wrong.

### Gate and priority

The gate takes **world facts only**, as a weight, so switching is always a
fade:

| Gate input | WoW source |
|---|---|
| `enabled` | `VFEX` enable (3 of 1,542 are 0; written `false`, kept) |
| `animation` | the carrying instance's active animation (M2 emitter tracks are per animation) |
| time of day, weather, season | zone fog by `LightData.Time`; the rest first producer |

Two inputs are kept out of the gate:
- **Viewer conditions** ("camera underwater"): the world can't depend on
  the camera. `MFOG`'s underwater layer becomes a style look.
- **Quality levels**: content isn't switched by quality. `VFOG.fogLevel`
  becomes `priority` by the rule `fog_level_to_priority` (0 → essential,
  1 → ambient, 2 → decorative), and the renderer thins by priority.

### Content

| Kind | Fields |
|---|---|
| `medium` | `extinction` (1/length), `albedo` (linear RGB), `anisotropy` g, `feature_size` (length; the smallest detail worth keeping, never a grid resolution), `hold` |
| `particles` | see below |
| `light_proxy` | colour or temperature (K), intensity, `follows` (a joint, or a parent's heat centroid) |
| `heat`, `force` (wind, vortex, attractor, curl noise), `emitter` (injection of density, heat, momentum) | added with their first producer |

`hold` applies only to `medium` fields:
- `pinned`: exactly as stated, not simulated.
- `base`: the simulation relaxes back toward it.
- `seed`: the simulation starts from it, then evolves freely.

**Particles:**
- **Carried by the velocity field (Luna).** A particle's velocity relaxes
  toward the field's with a `response_time` (short for a leaf, long for
  a heavy spark). There is no "ignores wind" option.
- `motion`: initial velocity and spread, gravity, buoyancy, drag, spin,
  orbit or jitter.
- `spawn`: a density (per volume, area or spline length) and a rate, never
  a fixed count; the consumer scales counts by level of detail.
- `life`: curves over normalized age (size, colour, opacity, emission).
- `look`: one of
  - `point`;
  - `quad` (a texture Ref, an atlas `{tiles_x, tiles_y, frames}`);
  - `streak`;
  - `ribbon`;
  - `mesh_instance` (a model Ref; orientation from velocity or spin; no
    collision).
- `light`: one of
  - `emission`: radiance, or `self_temperature` with cooling, times a
    tint;
  - `albedo`.
- `deposit`: `none`, or what the particle writes back (heat, density).
  It must be explicit, since it decides where the consumer runs the
  system.

### Style

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
- **`volumetric_fog`**: the froxel pass's colour and intensity. It is a
  separate pass, so it is not folded into `distance_fog`.
- **`ambient_color`**: the three colours and the unknown-flag selector.
- **`exit_blend`**: `MFOG` flag 0x1. It scales the enclosing fog down near
  the model's exits.
- **`looks`**: alternative styles under a viewer condition. The only one
  known is `underwater` (`MFOG`'s second layer).
- **`draw`**: an emitter's classic draw state (blend mode, render flags,
  priority plane).

`LightData`'s columns line up with the fog record's roles almost one for
one. The three 4-float coefficient arrays match the shader's three cubics.
This is a name match, unverified:

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

### Time

Every animated value names its clock:
- `age`: normalized particle age.
- `seconds`.
- `day_time`: `LightData.Time`.

M2 tracks are keyed in milliseconds, so they are `seconds` curves. They
come one per animation, and which applies is the `animation` gate input,
not a separate clock.

### Provenance

There is one `provenance` per instance or source: a map from field path to
how the value was obtained. Paths not listed are `source`:

```json
"provenance": {
  "classification": "ground_fog/particleemitters_groundfog",
  "fields": {
    "content.extinction": {"kind": "derived", "rule": "linear_fog_to_extinction"},
    "priority": {"kind": "derived", "rule": "fog_level_to_priority"}
  }
}
```

A consumer switches a rule off by ignoring the fields it produced; the
style value is there to compare against.

### Unknown data: the evidence ladder

| Rung | Means | Where |
|---|---|---|
| semantic | role known | typed key |
| candidate | role proposed with evidence | `unresolved` entry with `candidate_role` |
| structural | value class known (colour, unit vector, FileDataID, count, angle) | `unresolved` entry with `kind` |
| raw | only an offset | `unresolved` entry, plus the raw record slice |

There is one `unresolved` shape everywhere:
`{fields: [{field, kind, value, candidate_role?}], records: [{field, slice}]}`.
`field` is a value, so source names are allowed in it; the lint forbids
chunk tags in keys only.

A `candidate_role` is a hypothesis carried as data. A debug view can
apply candidates and compare against an in-game screenshot. A confirmed
candidate is promoted to its typed key (a schema minor bump).

Boundary rules:
- **Always-zero fields are asserted, not carried.** This covers
  `VFEX.Unk3..8`, the `VFE2` tail, `MFED.unk1` and `MPVD`'s tail. A nonzero
  one becomes an `unresolved` field and a warning.
- **Proven duplicates are checked, then dropped.** This covers `PVMI`'s
  repeated z values, and cross-reference ids once used for pairing.
- **Flags split into named bits.** Only the unknown bits go to
  `unresolved`; the raw word goes to `source.flags`.
- **Placeholders are not instances.** A WMO's "empty" `MFOG` record means
  "use the enclosing fog". It is recognized by content, not by index:
  record 0 is the placeholder in 3,631 of 4,000 seeded roots, a later
  record never is, and in the other 369 roots record 0 is a real fog.
  Part references are instance ids, so nothing is renumbered.
- **Units convert at the boundary, for typed fields only.** Unresolved
  values stay as stored.

### Units

Declared in the contract header's `space`, with `x-unit` on every
physical field (`1/length`, `K`, `second`). husk's documents are in yards
today; the consumer converts once at load.

### Validation

`validate-bundle` rejects, naming the document, the instance or source id,
the field, and the expected and actual values:
- an unknown `kind`;
- a physical value without its unit;
- non-finite numbers;
- negative extinction, response time, density or lifetime;
- a `hold` outside `medium`;
- an instance input the effect doesn't declare, or a declared input
  without a default that the instance doesn't give;
- a viewer condition or a quality level in a gate;
- an effect reference that doesn't resolve.

## Derivation rules

Each rule is named in `provenance` and switchable. husk derives only where
a source quantity maps by a stated formula. Where WoW holds no evidence,
the field stays absent.

| Rule | Computes | From |
|---|---|---|
| `fog_density_to_extinction` | medium `extinction` = the exponential density (the shader's `exp(−d·density)`) | zone fog `exponential.density` |
| `linear_fog_to_extinction` | `extinction` = 3 / (end − start): 95 % opacity across the band | `MFOG` linear fog |
| `fog_color_to_albedo` | `albedo` = fog colour, linear | zone fog, `MFOG` |
| `fog_level_to_priority` | `priority` | `VFOG.fogLevel` |
| `additive_to_emission` | particle `light.emission` = colour × opacity as radiance | M2 additive blend modes |
| `alpha_to_albedo` | particle `light.albedo` = colour | M2 alpha blend modes |
| `count_to_spawn_density` | `spawn` density = count / box volume | `MPVD`/`PVMI` (once box size is verified) |

**Wind response time**: husk derives none in the literal tier, since no
WoW field informs it. A classified effect may state one as an `authored`
value of that effect (a leaf responds fast, a spark slowly). The judgement
then lives in one reviewable place, not on every instance.

## Where today's sources land (literal tier)

| Source | Effect | Anchor, region | Content | Style |
|---|---|---|---|---|
| Zone fog (`Light`/`LightData`) | `classic_zone_fog` | world, `everywhere`/`sphere` | medium (`base`, derived) | `distance_fog` by `day_time` |
| WMO `MFOG` | `classic_local_fog` | instance, `sphere` | medium (derived) | `distance_fog.linear`, `looks.underwater` |
| `MFOG` flag 0x1 | `classic_local_fog` | instance | — | `exit_blend` |
| `VFOG`+`VFEX`+`VFE2` | `classic_volumetric_fog` | world, `shape` | medium (`pinned`), values absent until known | `volumetric_fog` |
| `MAVG`/`MAVD`/`MBVD` | `classic_ambient_region` | instance, `everywhere`/`sphere`/`planes` | — | `ambient_color` |
| WMO `MPVD`, map `PVMI`/`PVPD`/`PVBD` | `classic_particulate_volume` | instance or world, `box`/`box_tree` | particles, `quad` per emitter slot | `draw` |
| M2 particle emitter | `classic_m2_particle_emitter` | joint; plane → `box`, sphere → `sphere`, spline → `spline_tube` | particles | `draw` |
| M2 ribbon | `classic_m2_ribbon` | joint | particles, `ribbon` look | `draw` |
| M2 model particles | `classic_m2_particle_emitter` | joint | particles, `mesh_instance` look | `draw` |

Part membership is not a region. "The parts that list this fog"
(`MFVR`/`fogIds`) stays as each part's index list, into the model's
`resources.effects`.

## First classification targets

Families the listfile already shows as one behaviour with varying inputs.
These are leads, not verified classifications:

| Effect | Region | Evidence |
|---|---|---|
| `ground_fog` | `above_ground` | `world/generic/passivedoodads/particleemitters/{green,grey,orange,purple,dustwallow}groundfog.m2`, `gilneas_groundfog_0{1,2}.m2`: colour variants of one emitter, placed as doodads; 240 `fog` and 88 `mist` M2s in all |
| `falling_leaves` | carried by the anchor | 295 `leaf` and 127 `leaves` M2 paths; tree emitters with their own textures |
| `tornado` | `spline_tube` | 22 `tornado` and 23 `cyclone` M2s (`skywall_*_tornado.m2`) |
| `dust` | `box` | `MPVD`/`PVMI` presets (+0x30 shared across unrelated files) |

## Implementation plan

Each phase ends with the full suite green, the seeded-sample corpus check,
and `validate-bundle` clean on real exports.

**Phase 1: definitions, the effect kind, placement instances.**
- Add `effect.schema.json` (with `status`) and, in `common.schema.json`,
  `region`, `gate`, `priority`, `provenance` and `unresolved`. Include
  only the region and content kinds with a producer.
- `placement_set.schema.json`: the instance's asset may be an effect;
  add `anchor` and `inputs`. A set may be inline in its owner.
- `canon::Effect` in `canon_effects.hpp`; `canon::PlacementSet`'s
  instance gains anchor and inputs.
- `validate-bundle` follows effect references and checks inputs against
  the effect's declared ones. It warns on placeholder effects, and fails
  on them with `--release`.
- Lint additions: `hold` only under `medium`; input bindings name declared
  inputs.

**Phase 2: the literal tier for everything exported today.**
- The seven `classic_*` effect documents, `status: "placeholder"`.
- Every `MFOG`, `VFOG`, ambient, particulate and M2 emitter record becomes
  a placement instance with its full input set. Each goes into the set
  it switches with (WMO doodad sets, the world's `effects` set, an M2's
  inline `effects` set). This replaces `resources.scene`'s per-source
  lists and `world.scene`'s.
- Part fog lists become `{set, id}`; `MFOG` placeholders are not
  instances; flags split; one unresolved shape; always-zero asserts at
  the parsers.
- `husk export` reports the placeholder count per effect, and fails on
  them with `--release`.
- Verified on the 4,000-root WMO sample, every `_fogs.wdt`/`_mpv.wdt`,
  and a seeded M2 sample.

**Phase 3: derived world content on the literal tier.**
- The derivation rules above, each tested on a real record.
- M2 spline emitters get `spline_tube` regions.

Blocked items stay unresolved and are named in the TODO: box size,
emitter float semantics, `VFOG` density.

**Phase 4: `LightData` zone fog.**
- A `light_db2` reader (same pattern as `liquid_db2`).
- A world instance of `classic_zone_fog` per `Light` zone, styled by
  `day_time`, plus a derived `base` medium.
- The rest of the row (sky, ambient, direct light) waits for lighting's
  own design (`LIGHTING_TODO.md`).

**Phase 5 (before 1.0): classification, one behaviour at a time.**
- The classification table and its first entries (`ground_fog` with
  `above_ground`, then `falling_leaves`, `tornado`, `dust`).
- Each entry is verified by rendering its instances against in-game
  screenshots, then switches its instances from the literal effect to the
  classified one, keeping the literal look as `style`.

**Phase 6: research, as candidates.**
- `VFE2` against `LightData` distributions.
- `MFOG` flag 0x1000 (10 sampled roots: record 0 with flag 0x1000, zero
  radii, end 16000).
- `fogIds` slot 0 as the colour record.
- `PVPD`.

Each result enters as a `candidate_role` first, then gets promoted.

## Open decisions (Luna)

Decided (Luna, 2026-10-11):
- **M2 effects sets are inline** in the M2's manifest. The placement-set
  schema allows a set inline in its owner as well as a file.
- **Classification is the 1.0 gate.** Every effect source in the corpus
  needs a classified effect before 1.0.
