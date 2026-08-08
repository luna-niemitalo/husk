# TODO: WMO/ADT lighting (`MOLT`/lightset system/`MNLD`/outdoor lighting)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed (see `../TODO_correctness.md`'s own convention) —
git history is the record of what was fixed and when, not this file. One of
three sibling documents from the same investigation pass (see
`LIQUID_TODO.md`'s opening for the full framing; `FOG_VOLUMES_TODO.md`
covers the rest).

Scope: `../../WORLD_COMPLETENESS.md`'s **Lighting** section — WMO point lights
(`MOLT` + group-level `MOLR` refs), the BfA+ lightset system
(`MOLS`/`MOLP`/`MLSS`/`MLSP`/`MLSO`/`MLSK`), WMO dynamic lights (`MNLD` +
group-level `MNLR` refs), legacy v14 lightmaps (`MOLM`/`MOLD`), spotlight/
pointlight animation (`MOS2`/`MOP2`), and ADT outdoor lighting. Every row
in that table currently reads `none`/`none`; every claim below was checked
against real corpus bytes this session (`/media/luna/data/wow_export`,
read-only), not carried forward from the wiki uncritically.

**The real headline question this document had to settle** (flagged
explicitly by the investigation brief): does glTF's `KHR_lights_punctual`
extension fit `MOLT`'s shape well enough to reach `native`, not just
`extras`? Short answer, argued in full below: **partially — real, with
known gaps, but Luna confirmed 2026-08-01 that it's worth adopting anyway
(see the Verdict below) — decided, not just flagged.**

---

## Scoping correction (applies to every group-scoped item below)

Before any of the specific chunks: wowdev.wiki's own heading structure
(`documentation/wowdev-wiki/md/WMO.md`) puts `# WMO root file` at line 43
and `# WMO group file` at line 1020. **Every one of `MOLS`, `MOLP`,
`MLSS`, `MLSP`, `MLSO`, `MLSK`, `MOS2`, `MOP2`, `MPVR`, `MAVR`, `MBVR`,
`MFVR`, `MNLR`, `MOLM`, `MOLD` is documented *after* line 1020** — i.e.
all of them are nested inside `MOGP`'s own payload (a group-file
container chunk, per `## MOGP chunk`'s own explicit "this chunk contains
all other chunks!" note), not top-level chunks of either the root or
group file. Only `MOLT` and `MNLD` (the two chunks that hold real light
*records*, at lines 506 and 854 respectively — both before line 1020) are
root-scoped.

This isn't a guess — it's the wiki's own documented structure, confirmed
independently this session by direct scanning: searching for
`MOLS`/`MOLP`/`MLSS`/`MLSP`/`MPVR`/`MAVR`/`MBVR`/`MFVR`/`MNLR` as
*top-level* chunks in 4,000 real root WMO files found **zero** hits for
any of them; searching for the same tags *nested inside `MOGP`'s payload*
in 2,570 real group WMO files found real hits for all of them except
`MLSO`/`MLSK`/`MOS2`/`MOP2`/`MOLM`/`MOLD` (see each item below). A parser
written against the assumption that these are root chunks (the natural
first guess, since `MOLT`/`MOLP` look like siblings) will silently find
nothing, forever — this correction is the single most important fact in
this document for whoever implements it next.

---

## 1. WMO point lights (`MOLT` + group `MOLR` refs)

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/WMO.md`, `## MOLT chunk`
(lines 506–538, root-scoped) and `## MOLR chunk` (lines 1468–1479,
group-scoped, nested in `MOGP`).

### Struct, as documented

```
struct SMOLight {                 // 48 bytes (0x30), root-scoped, MOHD.nLights entries
    uint8_t type;                  // 0=omni, 1=spot, 2=direct, 3=ambient
    uint8_t useAtten;
    uint8_t pad[2];
    CImVector color;                // BGRA byte order
    C3Vector position;
    float intensity;
    C4Quaternion rotation;           // spot/direct only, per the wiki
    float attenStart;
    float attenEnd;
};

uint16_t lightRefList[];              // MOLR, group-scoped -- indices into root's MOLT array
```

### Real-data verification (this session)

Scanned 4,000 real WMO root files: **777/4,000 (19.4%) declare a `MOLT`
chunk at all, but the overwhelming majority are `size=0`** (0 real
lights). Widened the check specifically for **non-empty** `MOLT` (walking
until 5 hits or 6,000 files checked): **7/418 root files (1.7%)** carry a
real, populated `MOLT`. When present, real: decoded
`world/wmo/dungeon/9du_plaguefalldungeon_sanctum01.wmo`'s 2 real lights —
`type=0` (omni), `useAtten=1`, plausible dungeon-interior positions,
`intensity=1.0`, `attenStart=2.0`/`attenEnd=4.0` (a sane, small
start<end attenuation pair). Colors decoded low/dim (BGRA
`(37,44,5,255)`/`(48,42,3,255)`) — plausible for a dim torch/ambient
accent light, not obviously wrong.

`MOLR` (group-scoped light refs into `MOLT`): **109/2,000 real group
files (5.5%)**, sizes are always even (2-byte-per-entry array, confirmed
— e.g. size 8 → 4 refs, size 2 → 1 ref).

**Real client-usage caveat, worth stating plainly rather than
oversold**: the wiki's own `MOLT` section states outright ("The entire
MOLT and related chunks seem to be unused at least in 3.3.5a...") that
this data may only ever have been used for *baking* `MOCV` vertex-color
lighting offline, not live per-light rendering — and this session's own
cross-check of `reference/wow.export/src/js/3D/renderers/WMORendererGL.js`
found its live GL renderer uses a single hardcoded directional light
(`u_light_dir = (0.5, 1.0, 0.5)`), never `MOLT` data, for real-time WMO
rendering — independent corroboration from a different, more modern
tool that `MOLT`'s practical rendering role (if any) is historical/
baking-only, not live. This doesn't change the recommendation below (the
data is real, present, and translatable regardless of whether any known
client currently re-lights from it live) but it should temper
expectations: exporting `MOLT` as real glTF lights gives Blender
*something* to work with, not a faithful reproduction of live WoW
rendering.

### The `KHR_lights_punctual` fit — full comparison

Fetched the real extension spec (Khronos `KHR_lights_punctual` README,
bounded single web fetch, see References) rather than guessing at its
shape from memory.

| `SMOLight` field | `KHR_lights_punctual` equivalent | Fit |
|---|---|---|
| `type` (omni/spot/direct/ambient) | `type`: `point`/`spot`/`directional` | omni→point, spot→spot, direct→directional all map cleanly. **`ambient` (type 3) has no equivalent at all** — the extension explicitly does not support ambient lighting. |
| `color` (`CImVector`, 0–255 BGRA) | `color` (linear RGB, 0.0–1.0 floats) | Clean: divide by 255, drop alpha. No gamma-correction guidance found either way; treated as a direct linear scale, same as every other color field this codebase already converts (`M2Color`, etc.). |
| `position` | node `translation` | Clean — same "position becomes a child node's translation" pattern this codebase already uses for `Attachment`/`Event`/`Light` (M2) nodes. |
| `rotation` (spot/direct only) | node `rotation` (direction = local -Z axis) | **Structural mismatch husk hasn't hit before**: `KHR_lights_punctual` has *no* rotation field on the light object itself — direction comes entirely from the *node's* rotation. Every existing husk `Attachment`/`Event`/`Light` (M2) node is translation-only, no rotation, by explicit design (`src/gltf.hpp`'s doc comment). A `MOLT` spot/direct light would need a **new** node shape (translation + rotation) to carry this — a real, small architecture change, not a blocker, but a first. |
| `intensity` | `intensity` (lux for directional, candela for point/spot) | **Unit mismatch, not just field-shape**: `MOLT.intensity` is an arbitrary WoW engine unit (this session's real samples were all exactly `1.0`), not calibrated to lux/candela. A 1:1 passthrough is defensible (both are "how bright," same "don't guess the exact physical mapping, expose the source value" precedent as `M2Sequence`'s own `sequence_metadata` extras) but is a real approximation, not a verified unit conversion. |
| `attenStart`/`attenEnd` (two-radius model) | `range` (single cutoff distance, `>0`, optional) + a **fixed** inverse-square+smoothed-cutoff falloff formula | **The single biggest real mismatch.** `KHR_lights_punctual` explicitly does *not* support a two-parameter attenuation model — only inverse-square falloff with an optional single `range` cutoff. `attenEnd` maps reasonably onto `range`; `attenStart` has **no target field at all** in the extension. |
| *(no field)* | `innerConeAngle`/`outerConeAngle` (spot only) | **`SMOLight` has no cone-angle field at all** — the documented struct has nothing analogous. A `KHR_lights_punctual` spot light would fall back to the extension's own defaults (0 / π/4 radians) for every WMO spot light, not real authored data. |

**Decision, confirmed by Luna 2026-08-01: adopt `KHR_lights_punctual`.**
`type` ∈ {omni, spot, direct} get real `native` glTF lights — position/
color/type map cleanly, and the known gaps (attenuation shape, missing
cone angles, intensity units) are accepted as the kind of "real but
bounded approximation" this codebase already ships elsewhere (see
`baseColorFactor`'s own WoW-blend-mode approximation), not a reason to
withhold a genuine native-reachable win. **`type=3` (ambient) has no
target and must stay `extras`-only** — there is no honest way to represent
an ambient light under this extension, decision or not. Implementation:
`native` via `KHR_lights_punctual` for omni/spot/direct, with
`attenStart`, the exact WoW `intensity` value, and `type=3` (ambient)
records all carried alongside as inert `extras` on the same light node
(same "native construct plus an extras sidecar for what doesn't fit"
pattern `Material::additionalTextureLayers` already establishes).

### Correction to the investigation brief's own premise

The brief describes this as potentially "the FIRST real use of a proper
glTF extension" in husk. **This is not quite right, checked directly**:
`src/gltf.cpp`/`src/gltf.hpp` already declare and use
`KHR_materials_unlit` (`model.extensionsUsed.push_back
("KHR_materials_unlit")`, gated on any material's `unlit` flag — see
`gltf.cpp` lines 558/763). `KHR_lights_punctual` would be the **second**
real extension, not the first — a smaller step than the brief implied,
since the `extensionsUsed`-declaration plumbing, at least, already
exists and is already exercised by real tests. What *would* be new: a
light-carrying node needing `rotation` for the first time (every existing
anchor/attachment node is translation-only), and a document-level
`extensions.KHR_lights_punctual.lights[]` array (`KHR_materials_unlit`
is a materials-only, no-payload extension — this would be the first
extension with a real per-document data array and a node-level
`extensions.KHR_lights_punctual.light` index reference).

- **Parse**: `full` (once implemented against the confirmed 48-byte
  struct).
- **Consumption**: `native` (`KHR_lights_punctual`, omni/spot/direct) +
  `extras` (ambient records, `attenStart`, raw `intensity`) — decided, see
  above.
- **glTF ceiling**: `native — gap remains` (ambient lights and the
  attenuation-shape/cone-angle mismatches are real, permanent gaps even
  with the extension in play) rather than `native — 100%`.

---

## 2. WMO lightset system (BfA+, `MOLS`/`MOLP`/`MLSS`/`MLSP`/`MLSO`/`MLSK`)

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/WMO.md`, `## MOLS`
(2179), `## MOLP` (2191), `## MLSS` (2211), `## MLSP` (2226), `## MLSO`
(2241), `## MLSK` (2258) — all group-scoped, per the Scoping correction
above.

### Struct, as documented

```
struct { char _1[0x38]; } map_object_spot_lights[];     // MOLS, 56 bytes, unknown fields

struct {                                                  // MOLP, 44 bytes
    uint32_t unk;
    CImVector color;
    C3Vector pos;
    float intensity;
    float attenStart;
    float attenEnd;
    float unk4;    // "only seen zeros"
    uint32_t unk5;
    uint32_t unk6; // "CArgb?"
} map_object_point_lights[];

struct { uint32_t offset; uint32_t count; } map_object_lightset_spotlights[];   // MLSS, indexes MOLS
struct { uint32_t offset; uint32_t count; } map_object_lightset_pointlights[];  // MLSP, indexes MOLP
struct { uint32_t offset; uint32_t count; } mapobject_spotlight_animsets[];     // MLSO, indexes MOS2 -- "in binary, not in files"
struct { uint32_t offset; uint32_t count; } mapobject_pointlight_animsets[];    // MLSK, indexes MOP2 -- same
```

**This genuinely does not map 1:1 onto `MOLT`/`MOLR`** — it's a
different, newer system, per the investigation brief's own caution not
to assume otherwise. `MOLS`/`MOLP` hold their *own* per-group light
records (not references into the root's `MOLT` array the way `MOLR`
does) — `MLSS`/`MLSP` are lightweight `(offset, count)` index windows
into those same group's own `MOLS`/`MOLP` arrays, most plausibly one
window per "lightset" (a named, switchable lighting configuration,
matching the wiki's own "layered/conditional" framing), not a
placement-reference list like `MOLR`.

### Real-data verification (this session)

Found in exactly one real cluster of files in the 2,570-group-file
sample: `world/wmo/dungeon/robodrome/*.wmo` (a Mechagon-Robodrome-family
BfA dungeon — consistent with `MOLS`'s own documented version floor,
8.1.0.27826). **`MOLS`: 7/2,570 (0.3%); `MOLP`: 50/2,570 (1.9%); `MLSS`:
7/2,570; `MLSP`: 15/2,570. `MLSO`/`MLSK`: 0/2,570 — matches the wiki's
own "In binary, not in files" annotation exactly**, a real, clean
negative result (the animation-set index tables genuinely don't ship in
real files, only exist compiled into the client binary — same
"documented absence, not a scanner bug" disposition this project already
gave `WFV1`/`WFV2` in an earlier session, now independently reconfirmed
for a different chunk family).

**Struct fully confirmed by real cross-referencing, not just size
divisibility** — `world/wmo/dungeon/robodrome/8du_robodrome_arena01_002.wmo`:
`MLSS` = `{offset:0, count:3}`, and that file's own `MOLS` chunk is
exactly `168 bytes = 3 × 56` — **exact match**. `MLSP` = `{offset:0,
count:35}`, and `MOLP` is exactly `1540 bytes = 35 × 44` — **exact
match**. (First attempt used a wrong 36-byte `MOLP` stride from a
miscounted field list and got `1540/36 = 42.78`, a non-integer — a real,
caught-before-writing-anything-down mistake; the wiki's own field list
sums to 44 bytes, not 36, and 44 is what the real file's arithmetic
confirms.) Decoded 4 real `MOLP` point lights: colors plausible and
varied (`(255,219,111)` warm white, `(253,161,0)` orange, `(2,203,255)`
cyan) — a real, colorful multi-light rig, unlike `MOLT`'s dim samples
above.

**One real, unresolved oddity, flagged rather than guessed past**: in
3 of 4 decoded `MOLP` records, `attenStart > attenEnd` (e.g.
`attenStart=10.16, attenEnd=1.0`) — the *opposite* of `MOLT`/`MNLD`'s own
clean start<end convention seen elsewhere in this same investigation.
Either the wiki's field order for `MOLP` is subtly wrong (start/end
swapped) or "attenuation end" means something different for lightset
point lights than it does for `MOLT`/`MNLD` — genuinely unresolved, flag
for whoever implements this rather than silently assuming either
direction. Separately, `unk5`/`unk6` decoded suspiciously as constant,
recognizable IEEE-754 bit patterns across most records (`unk5` = exactly
`0x80000000`, i.e. `-0.0` as a float; `unk6` = exactly `0xbf800000`,
i.e. `-1.0` as a float) — a real lead that these two `uint32_t` "unk"
fields might actually be floats with default/sentinel values, not
integers, worth a follow-up decode pass before implementation.

### Consumption plan

Same shape as item 1 above, once the `MOLS`/`MOLP`→`MLSS`/`MLSP` indexing
is resolved: `MOLP` point lights via `KHR_lights_punctual` (same fit
analysis as `MOLT`, since the two structs are field-compatible modulo the
`attenStart`/`attenEnd` question above); `MOLS` spotlight records stay
`extras`-only regardless of the light-representation decision, since the
struct itself is **wiki-documented as entirely unknown fields**
(`char _1[0x38]`) — there's nothing to translate, only bytes to preserve.
Given the lightset concept ("multiple switchable configurations") has no
core-glTF equivalent at all (glTF has no notion of "alternative light
sets," unlike WMO doodad sets which at least loosely resemble glTF's own
node-visibility-toggle patterns some DCC tools use), which `MLSS`/`MLSP`
"set" is active at a given moment is itself external, runtime-driven
information — recommend exposing every lightset's own light array, but
leaving "which one is currently active" as `extras` metadata a
Blender script would act on, the same "surface everything, let the
consumer choose" precedent `.bone` correction sets already established.

- **Parse**: `full` for `MOLP`/`MLSS`/`MLSP` (confirmed); `descriptor`
  only for `MOLS` (struct genuinely unknown); `n/a` for `MLSO`/`MLSK`
  (never present in real files).
- **Consumption**: `native` (`MOLP` via `KHR_lights_punctual`, same
  confirmed decision as item 1) + `extras` (`MOLS`, lightset
  selection metadata).
- **glTF ceiling**: `native — gap remains` (same ceiling as item 1, plus
  the additional "which set is active" gap).

---

## 3. WMO dynamic lights (`MNLD` + group `MNLR` refs)

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/WMO.md`, `## MNLD`
(854–905, root-scoped) and `## MNLR` (2368–2377, group-scoped).

### Struct, as documented

184 bytes (0xB8) per record — `type`/`lightIndex`/`flags`/`doodadSet`
(4 ints), `innerColor` (`CImVector`), `position`/`rotation`
(`C3Vector` each), `attenStart`/`attenEnd`/`intensity` (3 floats),
`outerColor` (`CImVector`), `blendStart`/`blendEnd` (2 floats), a 4-byte
gap, `flickerIntensity`/`flickerSpeed`/`flickerMode`, a `C3Vector`
"field_54" (unexplained), a 4-byte gap, `lightCookieFileID`, a 20-byte
gap, `falloff`/`innerAngle`/`outerAngle` (spot-only), `scale`/
`intensityMultiplier` (half-floats), and 11 trailing `unused_N` ints —
see `WMO.md` for the full field-by-field listing, transcribed exactly
into the C++ sketch below.

### Real-data verification (this session)

**265/4,000 real root files (6.6%)** carry a real `MNLD` chunk — an order
of magnitude more common than `MOLT`'s *populated* rate (1.7%), and
`MNLR` group refs at **892/2,570 (34.7%)** — genuinely common, the most
prevalent lighting-related chunk found in this whole investigation.
Struct size **184 bytes confirmed exactly** against three real files'
chunk sizes (`4968/184=27`, `42136/184=229`, `95680/184=520`, all exact).

Full field decode of 3 real records from
`world/wmo/dungeon/9du_plaguefalldungeon_sanctum01.wmo` — clean and
plausible throughout: `type=0` (point), real vivid colors (`innerColor`
`(17,102,95)` teal, `(142,152,43)` olive-yellow, `(253,180,84)` orange —
a real, colorful multi-light dungeon rig, unlike `MOLT`'s dim examples),
`attenStart`/`attenEnd` in the correct start<end order this time
(`10.16`/`15.59`, `2.81`/`10.56`, `5.96`/`12.89`), `intensity` plausible
(`1.0`–`1.26`), `falloff`/`innerAngle`/`outerAngle` all cleanly `0.0`
(consistent with these being point, not spot, lights — cone-angle fields
correctly unused). **One real bug caught and fixed in this session's own
verification script before it was written down**: an initial decode
misplaced `flickerIntensity` and everything after it by 4 bytes (skipped
the documented `gap0[4]` before it), producing a garbage, suspiciously
repeated `outerAngle` value (`2.0e-41` in every record) — re-derived the
correct offsets field-by-field from the wiki's struct order and re-ran;
the corrected decode is clean (all three point-light `outerAngle`s
genuinely `0.0`). Recorded here as a caution: this struct has enough
gap fields that an off-by-one-field offset mistake produces
plausible-looking-but-wrong output rather than an obvious crash — verify
against the exact field-by-field byte math, not just "the numbers looked
reasonable," before trusting a decode.

`MNLD` is real client-relevant per the wiki's own framing ("used for
everything from torch fires to projecting light/shadow... to make it
look like light is coming through a window") — unlike `MOLT`'s
disputed/historical status, this is presented as the *actual* modern
dynamic-lighting mechanism, and its real prevalence/data-quality in this
session's sample supports that.

### Consumption plan

Same `KHR_lights_punctual` analysis as item 1 applies to `type`∈{point,
spot} (`MNLD`'s own enum only has these two, no ambient case — simpler
than `MOLT`'s four-way type). `MNLD` additionally has real fields with no
`KHR_lights_punctual` equivalent at all beyond what `MOLT` already
lacked: `flags` (blend-between-colors / casts-shadows bits),
`innerColor`/`outerColor`/`blendStart`/`blendEnd` (a genuine two-color
gradient-by-distance model, richer than the extension's single `color`),
`flickerIntensity`/`flickerSpeed`/`flickerMode`, `lightCookieFileID` (a
real texture reference — a light cookie/projected-texture, glTF has no
punctual-light texture-projection slot at all), and `doodadSet`
(which doodad-set scope this light belongs to, mirroring `MODD`'s own
mechanism). All of these are real, further `extras`, same pattern as
`MOLT`'s leftover fields.

- **Parse**: `full` (confirmed).
- **Consumption**: `native` (`KHR_lights_punctual`, position/rotation/
  color/attenuation-as-range) + `extras` (flicker, light-cookie texture
  ref, two-color gradient, doodad-set scoping) — same pending human
  decision as item 1.
- **glTF ceiling**: `native — gap remains`.

---

## 4. WMO legacy lightmaps (v14, `MOLM`/`MOLD`)

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/WMO.md`, `## MOLM`
(2378) / `## MOLD` (2401) — both explicitly flagged "≤ 0.5.5.3494, only
used in v14," pre-1.0 alpha format.

**Real-data verification**: **0/2,570 real group files** — as expected,
this modern retail extraction has no v14-era files. `../../WORLD_COMPLETENESS.md`'s
own "deprioritized" note and README's existing precedent for other v14
alpha-only chunks both already cover this correctly; nothing new to add.

- **Parse/Consumption/ceiling**: `n/a, deprioritized` — confirmed, not
  just carried forward.

---

## 5. WMO spotlight/pointlight animation (`MOS2`/`MOP2`)

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/WMO.md`, `## MOS2`
(2271) / `## MOP2` (2287).

**`MOS2`**: wiki-documented as a genuinely **unknown 108-byte struct**
("Unknown struct layout... byte data[108]; // unknown"), explicitly
"in binary, not in files." **0/2,570 real group files** in this
session's sample — matches the "not in files" claim exactly, a real
confirmed negative, not an unchecked assumption. **Do not implement or
guess at this struct** — no real bytes exist anywhere in this corpus to
verify against, and the wiki itself has never resolved it.

**`MOP2`**: a real, fully documented struct (`lightIndex`/`color`/`pos`/
`attenuationStart`/`attenuationEnd`/`intensity`/`rotation` +
`lightTextureAnimation` (flicker fields, same shape as `MNLD`'s) +
`lightUnkRecord` (mostly unused except `lightTextureFileDataId`)) — but
the wiki itself notes it as observed in **exactly one known file**
(`world/wmo/zuldazar/orc/8or_pvp_warsongbg_main01.wmo`, FileDataID
2143042, client 8.1.5.28938). **0/2,570 in this session's own sample**
(consistent — a single-known-file rarity wouldn't be expected to turn up
in a 2,570-file random sample; the specific named file wasn't in this
session's sample set and wasn't separately pulled, since `MOS2`'s
"genuinely unknown, don't guess" disposition already made this whole
item low-priority enough not to spend a targeted `casc-tool` pull on it
this session).

- **Parse**: `n/a` (`MOS2`, genuinely unknown struct, no real bytes to
  check); `descriptor`-eligible but unverified (`MOP2`, struct is real
  but this session found zero real files to confirm it against).
- **Consumption**: `n/a` both.
- **glTF ceiling**: `n/a` (`MOS2`, matches `../../WORLD_COMPLETENESS.md`'s
  existing note); `extras-capped, permanent` (`MOP2`, matches
  `../../WORLD_COMPLETENESS.md`'s existing note — animated light *properties*
  hit the same wall M2's own animated tint/fade curves already do, no
  glTF animation-channel target for a light property exists).

---

## 6. ADT outdoor lighting

**Current state**: `❔` in `../../README.md`'s format matrix ("outdoor lighting
may be zone/`Light.dbc`-driven rather than stored per-ADT, unconfirmed")
and `none`/`none`/`❔` in `../../WORLD_COMPLETENESS.md`. The investigation brief
explicitly asked this be settled for real, not carried forward as a
hedge.

**Settled, with a confident answer: ADT outdoor lighting is entirely
DB2/DBC-driven, with zero bytes of it living in ADT or WDT chunk data.**
Read all seven of the pre-mirrored reference pages the brief named:

- `documentation/wowdev-wiki/md/DB/Light.md`: the root table — per-map,
  per-position (`m_x`/`m_y`/`m_z`, inches, ÷36 for yards) "Light" records
  with `falloffStart`/`falloffEnd` radii and up to 8 `LightParams`
  references (clear/storm/underwater/death/unknown weather-state
  variants). Its own text: "This information prior to 1.9 used to be
  stored in the `.LIT` files but in 1.9 was moved to Light.dbc" — a
  file-format-to-database migration that predates modern ADT entirely.
- `documentation/wowdev-wiki/md/DB/LightParams.md`/`LightIntBand.md`/
  `LightFloatBand.md`/`LightData.md`: the actual color/fog/sky-color/
  water-color band data referenced by a `Light` record's params —
  entirely separate DB2 tables, no ADT linkage of any kind.
- `documentation/wowdev-wiki/md/DB/LightSkybox.md`: skybox model
  selection, again DB2-only.
- `documentation/wowdev-wiki/md/DB/ZoneLight.md`/`ZoneLightPoint.md`:
  Cata+ zone-level lighting overrides (a named zone → `Light.dbc` id,
  plus an n-gon polygon boundary in a second table) — explicitly
  **hardcoded in the client exe pre-Cata** ("In 3.3.5, those values were
  instead hardcoded directly in the client"), moved to DB2 in Cata, never
  stored per-ADT at any point in this lineage.
- `documentation/wowdev-wiki/md/WMO/Rendering.md`'s own "Lighting
  Queries"/"Lighting Mode" sections: confirms the real WMO-interior
  rendering path is baked `MOCV` vertex colors combined with
  `DNInfo->lightInfo` (a day/night-cycle-driven struct populated from
  `Light.dbc`'s own band data) — again, DB2/runtime-state-driven, no
  ADT-stored per-tile lighting data referenced anywhere in this
  description either.
- `documentation/wowdev-wiki/md/Rendering/CM2Light.md`: two short
  decompiled function stubs (`SetDirection`/`SetPosition`, the latter a
  `TODO` on the wiki itself) — a runtime light-object API, not a file
  format, confirms nothing new about storage.

No `ADT`/`v18.md` chunk anywhere in that page names or references
`Light.dbc`, a zone-light polygon, or any lighting-band data — confirmed
directly by re-reading the full chunk list (already enumerated in
`LIQUID_TODO.md`/`FOG_VOLUMES_TODO.md`'s own scans of the same page for
their own chunks; no additional lighting-shaped chunk was found).

**This is now a confirmed non-goal, not an unconfirmed hedge**: ADT
outdoor lighting is 100% `Light.db2`/`LightData.db2`/`LightParams.db2`/
`LightIntBand.db2`/`LightFloatBand.db2`/`LightSkybox.db2` (plus
`ZoneLight.db2`/`ZoneLightPoint.db2` for Cata+ zone overrides) — the same
class of external, client-side-database dependency as `LiquidType.dbc`
(liquid shading) and `.bone` slot *selection* (character-customization
choice), all already established non-goals in this project by design.
Recommend `../../README.md`'s `❔` for this row change to `⬛`/`n/a` with a
one-line note pointing here, once a later consolidated doc pass picks
this up (per this investigation's own scope boundary — this document
doesn't edit `../../README.md` itself).

- **Parse/Consumption**: `n/a` — no bytes exist to parse.
- **glTF ceiling**: `n/a` — external DB2 dependency, same class as
  `LiquidType.dbc`, a hard non-goal by design, not a gap husk could ever
  close without CASC/DBC access it will never have.

---

## Priority order

1. **`MNLD` (item 3)** — most prevalent (6.6% root / 34.7% group-ref),
   cleanest real data, modern/actually-relevant per the wiki's own
   framing, and the simplest `type` enum (no ambient case to special-case
   around).
2. **`MOLT` (item 1)** — second: real, common enough (19.4% chunk
   presence, 1.7% populated), and the natural place to prototype the
   (now-confirmed) `KHR_lights_punctual` translation, since it's
   structurally the simplest of the three light-record formats.
3. **Lightset system (item 2)** — third: real and confirmed, but rare
   (0.3–1.9%) and has its own open question (`MOLP`'s `attenStart`/
   `attenEnd` ordering) worth resolving after the simpler two are done
   and the `KHR_lights_punctual` translation code already exists to
   reuse.
4. **ADT outdoor lighting (item 6)** — no implementation work at all,
   just a doc-symbol fix; listed here for completeness of the priority
   ordering, not because it's actionable code.
5. **Legacy/animation items (4, 5)** — `n/a`/watch-only; no real corpus
   evidence to build against (`MOLM`/`MOLD` expected-absent; `MOS2`
   confirmed-absent; `MOP2` a single known file, not sampled this
   session).

---

## Test plan

**Real candidate fixtures**:

- `MOLT` (populated): `world/wmo/dungeon/9du_plaguefalldungeon_sanctum01.wmo`
  (2 real lights, decoded above).
- `MNLD`: same file (27 real lights, 3 decoded above) — a good single
  fixture covering both `MOLT` and `MNLD` at once, plus its matching
  group file `..._000.wmo` for `MNLR` refs (14 refs, per this session's
  scan).
- Lightset system: `world/wmo/dungeon/robodrome/8du_robodrome_arena01_002.wmo`
  (3 `MOLS`/35 `MOLP` records, `MLSS`/`MLSP` cross-referenced exactly, all
  decoded above) — the only real cluster found, treat as the canonical
  fixture for this whole item.
- `MOP2`: not sampled this session — the wiki's one named file
  (FileDataID 2143042,
  `world/wmo/zuldazar/orc/8or_pvp_warsongbg_main01.wmo`) would need a
  direct `casc-tool` pull (storage `/media/luna/games/World of Warcraft`,
  listfile `/media/luna/work/tinker/dev/wow_modding/m2mod/mappings/listfile.csv`
  per this session's environment note) if this item is ever picked up —
  low priority, per item 5's own disposition.

**Synthetic-vs-real split**: the `KHR_lights_punctual` translation layer
itself (color/position/range conversion, the node-rotation change) is
cleanly unit-testable with small synthetic fixtures once the human design
decision is made; the raw chunk-struct parsers (`MOLT`/`MNLD`/`MOLP`/
`MLSS`/`MLSP`) should follow `tests/test_phys.cpp`'s precedent — a
synthetic fixture proving offset/stride correctness, plus a real-data
regression test (`doctest::skip()`-gated) against the fixtures above
asserting exact light counts and a couple of spot-checked field values
matching this document's own decoded examples.

---

## References

- **wowdev.wiki**: `documentation/wowdev-wiki/md/WMO.md` (`## MOLT
  chunk` 506–538, `## MOLR chunk` 1468–1479, `## MOLS` 2179–2189, `##
  MOLP` 2191–2209, `## MLSS` 2211–2224, `## MLSP` 2226–2239, `## MLSO`
  2241–2255, `## MLSK` 2258–2269, `## MOS2` 2271–2285, `## MOP2`
  2287–2326, `## MNLD` 854–905, `## MNLR` 2368–2376, `## MOLM` 2378–2399,
  `## MOLD` 2401–2420); `documentation/wowdev-wiki/md/DB/Light.md`,
  `LightData.md`, `LightParams.md`, `LightIntBand.md`, `LightFloatBand.md`,
  `LightSkybox.md`, `ZoneLight.md`, `ZoneLightPoint.md`;
  `documentation/wowdev-wiki/md/WMO/Rendering.md` ("Lighting Queries",
  "Lighting Mode" sections); `documentation/wowdev-wiki/md/Rendering/
  CM2Light.md`; `documentation/wowdev-wiki/md/M2.md` (`## Lights`,
  ~line 2110, cross-referenced for the same two-radius-attenuation/
  disputed-directional-light-usage pattern recurring in a *different*
  format, corroborating rather than contradicting the `MOLT`/`MNLD`
  findings above).
- **Khronos glTF spec** (bounded single `WebFetch`, per this session's
  research budget): `KHR_lights_punctual` extension README
  (`github.com/KhronosGroup/glTF`) — full schema (type/color/intensity/
  range/spot-cone-angles), direction/orientation semantics (node
  rotation, local -Z axis), and the explicit "NOT supported: two-parameter
  attenuation, ambient lighting" callouts that drove the fit-comparison
  table in item 1 above.
- **wow.export** (design ideas only): `reference/wow.export/src/js/3D/
  renderers/WMORendererGL.js` (its live GL renderer's single hardcoded
  directional light — corroborates the wiki's own "`MOLT` may be
  baking-only, not live-rendered" caveat, independently, from a different
  tool).
- **husk `src/`**: `src/gltf.cpp`/`src/gltf.hpp` (existing
  `KHR_materials_unlit` `extensionsUsed` plumbing — the correction to
  the investigation brief's "first extension" framing above; the
  `Attachment`/`Event`/`Light` (M2) node shape that a `MOLT`/`MNLD` light
  node would extend with a first-ever `rotation` field); `src/chunk.hpp`
  (reused unmodified, same as `LIQUID_TODO.md`).
- **Real corpus counts, this session** (`/media/luna/data/wow_export`,
  scratch scripts in `/media/luna/work/cache/tmp/.../scratchpad/`, not
  committed): `MOLT` 777/4,000 root files present (19.4%), 7/418 (1.7%)
  populated; `MOLR` 109/2,000 group files (5.5%); `MNLD` 265/4,000 root
  files (6.6%); `MNLR` 892/2,570 group files (34.7%); `MOLS`/`MLSS`
  7/2,570 (0.3%), `MOLP` 50/2,570 (1.9%), `MLSP` 15/2,570 (0.6%);
  `MLSO`/`MLSK`/`MOS2`/`MOP2`/`MOLM`/`MOLD` 0/2,570.
