# FOG_MODEL.md — one canonical model for fog and atmosphere volumes

**Status: proposed (2026-10-11), not built.** Nothing here exists in
`src/` or `schema/` yet. The bundle today carries each source in its own
shape. `canon::LocalFog` mirrors `MFOG`, and `canon::FogVolume` mirrors
`VFOG`+`VFEX`+`VFE2`, `unknown18` and all (`src/canon_scene.hpp`,
`src/canon_map.hpp`; schema in `model.schema.json` and
`world.schema.json`). Source facts: `../WIKI_FINDINGS/WORLD.md`; open
items: `../TODO/WORLD/FOG_VOLUMES_TODO.md`; the client's fog code:
`../SHADER_FINDINGS/notes/fog.md`.

## The problem

Five sources describe atmosphere, each in its own decade's vocabulary:

| Source | What it is | Shape today |
|---|---|---|
| `Light`/`LightParams`/`LightData` DB2 | zone fog per map, keyed by time of day | not exported |
| WMO `MFOG` (+`MFED`, part `fogIds`/`MFVR`) | fog spheres a WMO places, MoP-era linear model | `LocalFog` |
| `_fogs.wdt` `VFOG`+`VFEX`+`VFE2` | volumetric fog shaped by an M2 | `FogVolume`, mostly raw |
| WMO `MAVG`/`MAVD`/`MBVD` | ambient-colour regions | `AmbientVolume` |
| WMO `MPVD`, `_mpv.wdt` `PVMI`/`PVPD`/`PVBD` | particle-filled regions | `ParticulateVolume` |

Copying each one forward means every consumer learns all five. It also
means a field whose meaning is found later changes shape in one source
only.

## Ground truth: the client renders one fog

Every forward-shaded draw evaluates the same fog record: 14 `vec4`s in
`cb5`, up to four records per draw, blended by weight (`fog.md`). On top
of that sits a froxel volume (`volumefog*` compute). The record's roles
come from shader code, so they are facts. Which source field fills each
role is mostly not known yet.

`LightData`'s column names line up with the record almost one for one.
Its newest WoWDBDefs layout has three 4-float coefficient arrays, which
match the shader's three cubics (`[9]`, `[10]`, `[11]`). This is a name
match only, not verified:

| Shader role (`fog.md`) | `LightData` column |
|---|---|
| `[0].y` fog end | `FogEnd` |
| `[0].x`/`[0].w` start offsets | `FogStartOffset`, `FogScaler` |
| `[0].z` exponential density | `FogDensity` |
| `[2]`, `[3].w` height plane, height scale | `FogHeight`, `FogHeightScaler` |
| `[4].x` density inside height fog | `FogHeightDensity` |
| `[9]` height falloff cubic | `FogHeightCoefficients[4]` |
| `[10]`, `[11]` distance cubics | `MainFogCoefficients[4]`, `HeightDensityFogCoeff[4]` |
| `[12].y`/`[12].x` curve near/far | `MainFogStartDist`, `MainFogEndDist` |
| `[8].w` vertical band | `FogZScalar` |
| `[3]`→`[4].yzw` colour near→far | `SkyFogColor`?, `EndFogColor` |
| `[6]`→`[13].rgb` height colour near→far | `FogHeightColor`, `EndFogHeightColor` |
| `[13].w`/`[6].w` gradient start, length | `EndFogColorDistance`? |
| `[5].yzw`, `[5].x`, `[7].x` sun fog | `SunFogColor`, `SunFogAngle`, `SunFogStrength` |

**Decision proposed:** the canonical vocabulary is the renderer's model,
named after these roles. It is not named after any source record, and
not after `LightData` either, though `LightData` is the source with
names. Every source becomes a partial filler of the same definitions.
When a source field's role is found, it moves into the model. The
model's shape doesn't change.

## Decomposition: region × activation × effect

Every atmosphere entry, from any source, splits into three independent
parts:

- **region**: where it applies, and how it fades at the boundary;
- **activation**: when it applies;
- **effect**: what it does inside.

| Source | region | activation | effect |
|---|---|---|---|
| `Light` zone | `everywhere` (map default) or `sphere` (`GameCoords`, falloff start/end) | time of day drives the medium; `LightParams` per area | `distance_fog` (+ sky, lighting: out of scope here) |
| `MFOG` | `sphere` (inner/outer radius) | its placement set (`MFED`); parts that list it | `distance_fog` (linear subset), plus a second medium when the camera is underwater |
| `MFOG` flag 0x1 | — | parts that list it | `exit_blend` (not a fog: scales the surrounding fog near exits) |
| `VFOG` | `shape` (an M2's volume, transform, per-axis radius) | quality level (`fogLevel`), `VFEX` enable | `volumetric_fog` |
| `MAVG`/`MAVD`/`MBVD` | `everywhere` (the model) / `sphere` / `planes` | placement set; parts that list it | `ambient_color` |
| `MPVD`/`PVMI` | `box` (+ `PVBD` box tree on maps) | placement set; parts that list it | `particles` |

Shared definitions, separate lists. `region` and `activation` are single
definitions every volume type references. Each effect keeps its own list
(`fog_volumes`, `ambient_color_volumes`, `ambient_particle_volumes`), so
the existing part index refs (`x-index-into`) stay as they are, and a
consumer that only draws fog reads one list. Model documents (model frame)
and world documents (world frame) use the same definitions. Today's
`LocalFog` and `FogVolume` become one `fog_volume` with different
regions.

### Regions

| Kind | Fields | Sources |
|---|---|---|
| `everywhere` | — | map default zone light, `MAVG` |
| `sphere` | `center`, `inner_radius`, `outer_radius` (full strength inside inner, fading to outer) | `Light`, `MFOG`, `MAVD` |
| `box` | `center`, `half_extent` or `extent` (unverified which), `rotation` | `MPVD`, `PVMI` |
| `planes` | outward planes, inside where all hold; `fade` | `MBVD` |
| `shape` | `model` ref, transform, `radius[3]` | `VFOG` |
| `box_tree` | the `PVBD` hierarchy, leaves, union-or-tree flag | `PVBD` (refines `box`) |

A region is geometry only. "The parts that list this fog" is not a region.
It's the part's own membership list (`parts[].fog_volumes`), and it
already exists.

### Activation

- `placement_set`: the owned set it switches with (`MFED`, MAVD/MBVD
  `doodadSetId`). Already exists.
- `min_quality`: drawn when the viewer's fog quality is ≥ this
  (`VFOG.fogLevel`).
- `enabled`: `VFEX.Unk0`. A disabled entry is written with
  `enabled: false`, not dropped, because 3 of 1,542 real fogs are disabled
  and dropping them hides data.
- `when`: a condition on the viewer. The only one known is
  `camera_underwater`, which selects `MFOG`'s second layer. It is a
  condition on a *medium*, not on the volume: one volume can carry
  `media: [{when: default}, {when: camera_underwater}]`.

### Effects

**`distance_fog`** is the shader record's model, in blocks. A source fills
the blocks it can. Every field is optional, and **absent means "inherit
from what encloses you"** (ultimately the zone fog), never a default husk
invented:

- `linear`: `start`, `end`, `color`. `MFOG` fills this completely:
  `start = end · startScalar`. The conversion is the wiki's
  MoP formula, applied at the boundary.
- `exponential`: `density`.
- `height`: `plane`, `scale`, `density`, `falloff` (cubic), `color`
  (near→far).
- `color_gradient`: `near_color`, `far_color`, `start`, `length`.
- `sun`: `color`, `angle`, `strength`.
- `curves`: `near`, `far`, `normal` (cubic), `height` (cubic),
  `curve_vs_exponential`.

**`volumetric_fog`** is the froxel volume's medium. It is *not* forced into
`distance_fog`: `VFOG` drives a different pass. Typed today: `color`,
`intensity[3]`. `VFE2`'s three colours are likely this pass's colour
roles, but that's a candidate only (see the ladder below).

**`exit_blend`**: `MFOG` flag 0x1. It carries no medium.

**`ambient_color`** and **`particles`** keep their current fields. They
move under `effect`, with `region`/`activation` factored out.

### Animation clocks

Animated fields name their clock instead of assuming seconds:

- `day_time`: `LightData.Time`. Every zone-fog field is keyed by it.
- `seconds`: `MPVD`'s envelope.
- `period`: a loop of known length. `VFOG.animationPeriods[4]` likely
  belongs here, but what it animates is unknown, so it stays unresolved.

## Unknown data: the evidence ladder

The point of the model is that a field can be *carried* before it is
*understood*, without the unknown leaking into the schema's shape. Each
source field sits on one rung and climbs over time:

| Rung | Means | Where it lives |
|---|---|---|
| 4 semantic | role known (shader code, or a source with names that the data confirms) | typed key in the model (`linear.end`) |
| 3 candidate | a role is proposed, with evidence | `unresolved` entry with `candidate_role` |
| 2 structural | value class known (colour, unit vector, FileDataID, count, angle) | `unresolved` entry with `kind` |
| 1 raw | only its offset is known | `unresolved` entry with `kind: scalar`/`bits`, plus the raw record slice |

One `unresolved` shape for every volume type, replacing today's
per-source `scalar_18`/`extension`/`palette` objects:

```json
"unresolved": {
  "fields": [
    {"field": "vfe2+0x00", "kind": "color", "value": [0.91, 0.45, 0.07],
     "candidate_role": "volumetric_fog.color"},
    {"field": "vfog+0x18", "kind": "scalar", "value": 0.5},
    {"field": "mfog.flags", "kind": "bits", "value": 4096}
  ],
  "records": [{"field": "vfe2", "slice": {"buffer": "world.bin", "...": "..."}}]
}
```

`field` is a *value*, so source names are allowed there. The lint forbids
chunk tags in *keys* only. A consumer never needs `unresolved` to draw;
a debugger can show it generically.

**`candidate_role` is the conversion path.** A hypothesis becomes data
without a schema change. MantleCore can offer "apply candidates" as a
debug toggle and compare against an in-game screenshot. Once a candidate
is confirmed, husk writes the value to the typed key and drops the entry
(a schema minor version bump). Rejected candidates just lose the
annotation.

Boundary rules that keep format soup out:

- **Always-zero fields are asserted, not carried.** `VFEX.Unk3..8`, the
  `VFE2` tail from +0x70, `MFED.unk1` and `MPVD`'s zero tail are zero in
  every real record. The parser checks they're zero. If one isn't, it
  appears as an `unresolved` field and a warning, so a new build's change
  shows up instead of being kept silently forever.
- **Proven duplicates are checked, then dropped.** Examples: `PVMI`'s
  repeated AABB z values, and every `*_ID` cross-reference once it's used
  to pair records.
- **Flags split.** Known bits become named fields. Only the unknown bits
  go to `unresolved` (`kind: bits`, masked). The raw word stays in
  `source.flags` for provenance.
- **Placeholders aren't volumes.** A WMO's "empty" `MFOG` record (zero
  radii, end 444.44, scalar 0.25) means "use the enclosing fog". It is
  recognized by content, not by index. Record 0 is that placeholder in
  3,631 of 4,000 seeded roots, a later record never is, and in the other
  369 roots record 0 is a real fog. A part that lists only placeholders
  gets an empty list. This needs remapping the part refs; see the open
  decisions below.
- **Units convert at the boundary** for typed fields only (degrees →
  radians, packed colours → 0..1, source frame → document frame).
  Unresolved values stay as stored, since converting them would be a
  guess.

## Investigation leads this model opens

Ordered cheapest first; all data-only unless noted:

1. **`LightData` export.** It's the one source with names, it fills
   `distance_fog` nearly completely, and it gives the other sources a
   reference distribution.
2. **`VFE2` against `LightData`.** Compare value distributions
   column by column. `VFE2`'s three colours, its 0.04..0.2 density and its
   0.01..3 triple look like `LightData`'s fog colours, density and
   coefficients. A match moves those fields to rung 3.
3. **`MFOG` flag 0x1000.** Ten roots' record 0 is flag 0x1000 with zero
   radii and end 16000. Hypothesis: "the whole model, no sphere". Count
   the radius-0 records per flag.
4. **`fogIds` slots against the shader's colour record** (`cb0[0].z`):
   whether slot 0 is the colour source. This needs a client trace or a
   MantleCore A/B test.
5. **A client trace** (last resort) for the `cb5` fill: the `VFEX`
   parameters, `animationPeriods`, `PVPD`.

## What changes in the current bundle

| Today | Becomes |
|---|---|
| `LocalFog` / `FogVolume` | one `fog_volume` with `region`, `activation`, `media` |
| `LocalFog.fog`/`underwater` layers | two `distance_fog.linear` media, the second `when: camera_underwater` |
| `interiorExteriorBlend` | an `exit_blend` effect |
| `FogVolume.unknown18`, `animationPeriods`, `extra.parameters`, `palette` | `unresolved.fields` |
| `sourceExtra`/`sourceExtra2` | `unresolved.records` |
| `AmbientVolume.shape` + position/planes | `region` |
| `ParticulateVolume` position/extent/rotation | `region` (`box`) |
| `MapParticulateVolume` bounds + `boxes` | `region` (`box_tree`) |
| default `MFOG` records | not written |

## Open decisions (Luna)

- **One list or three.** This proposes shared `region`/`activation`
  definitions with separate per-effect lists. A single `atmosphere_volumes`
  list with an `effect` discriminator is simpler to walk, but breaks the
  per-part index refs.
- **Placeholder `MFOG` records.** Option A drops them and remaps the part
  refs (`fogIds` 0 → nothing). Option B keeps them as `region: inherit`
  entries so indices stay source-stable. A is the cleaner model; B is
  simpler to verify.
- **`LightData` scope.** Zone fog comes bundled with sky, ambient and
  direct lighting in the same rows. Export it under a `zone_lights` entry
  whose fog part is a `distance_fog`, or fog only for now.
