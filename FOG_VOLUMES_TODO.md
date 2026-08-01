# TODO: WMO/ADT fog & atmospheric/particulate volumes

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed (see `TODO_correctness.md`'s own convention) —
git history is the record of what was fixed and when, not this file. One of
three sibling documents from the same investigation pass (see
`LIQUID_TODO.md`'s opening for the full framing; `LIGHTING_TODO.md` covers
the lighting slice, including the group-vs-root chunk **scoping
correction** this document also relies on for `MPVR`/`MAVR`/`MBVR`/`MFVR`).

Scope: `WORLD_COMPLETENESS.md`'s **Fog & atmospheric volumes** section —
WMO fog (`MFOG`/`MFED`/`MFOB`), WMO particulate/ambient/box volumes
(`MPVD`/`MAVG`/`MAVD`/`MBVD` + group-scoped reference lists `MPVR`/`MAVR`/
`MBVR`/`MFVR`), map-level fog volumes (`.wdt`'s `_fogs` sidecar: `VFOG`/
`VFEX`), and map-level particulate volumes (`.wdt`'s `_mpv` sidecar:
`PVPD`/`PVMI`/`PVBD`). Every row currently reads `none`/`none`; every claim
below was checked against real corpus bytes this session
(`/media/luna/data/wow_export`, read-only).

**Framing, per the investigation brief's own explicit comparison**: this
whole document is architecturally the closest sibling to M2's own
particle/ribbon emitters (`M2_COMPLETENESS.md`'s Particles/Ribbons rows,
`src/gltf.hpp`'s `Skeleton::EmitterAnchor`, `src/cmd_dump.cpp`'s
`dumpEmitters`) — procedural, non-geometric volume descriptions with no
core-glTF shape, high per-file field count, real curves/parameters worth
preserving in full somewhere, but nothing a plain glTF viewer can render.
**The M2 particle/ribbon pattern transfers directly and is the recommended
target for every item below**: a minimal placement anchor (position +
identifying id, wherever a stable owning "node" exists to hang it off —
see the per-item notes on what that anchor point actually is for a
WMO/map-scope volume, since there's no bone/joint hierarchy the way M2 has
one) as inert `extras`, full field/curve data via `husk dump-chunks`'s JSON
output. Unlike `LIQUID_TODO.md`'s `MLIQ`/`MH2O` (real geometry, reaches
`native`), **nothing in this document should ever target more than
`extras`/`node-possible` — there is no renderable shape to translate, only
metadata to preserve**, matching `WORLD_COMPLETENESS.md`'s own existing
"node-possible, unclaimed" ceiling calls for every row here.

---

## 1. WMO fog (`MFOG` + `MFED` + `MFOB`)

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/WMO.md`, `## MFOG chunk`
(656–698, root-scoped), `## MFED` (822–834, root-scoped), `## MFOB`
(1012–1019, root-scoped).

### Struct, as documented

```
struct SMOFog {                    // 48 bytes, root-scoped
    uint32_t flag_infinite_radius : 1;
    uint32_t : 3;
    uint32_t flag_0x10 : 1;
    uint32_t : 27;
    C3Vector pos;
    float smaller_radius;           // start
    float larger_radius;             // end
    struct { float end; float start_scalar; CImVector color; } fogs[2];  // [FOG, UWFOG]
};

struct MFED { uint16_t doodadSetId; char unk1[0xE]; };  // 16 bytes, SL+ (9.0.1.33978)

struct MFOB { /* no struct given -- wiki stub, version-gated Midnight+ (12.1.0.68209) */ };
```

### Real-data verification (this session)

Scanned 4,000 real WMO root files. **`MFOG`: 777/4,000 (19.4%), and
every single one is exactly 48 bytes (1 fog entry)** — no file in this
sample had more than one. Decoded the one real record in
`world/wmo/cameron.wmo`: `flags=0x0`, `pos=(-0.0, 0.0, 0.0)`,
`smaller_radius=larger_radius=0.0`, `fog.end=444.444`,
`fog.start_scalar=0.25`, `fog.color=(255,255,255,255)` white,
`uwfog.end=222.222`, `uwfog.start_scalar=-0.5`,
`uwfog.color=(255,0,0,255)` red. **This is an exact, byte-for-byte match
for the wiki's own documented "empty fog entry" default values** ("The
empty fog entry has both radiuses set to zero, 444.4445 for end, 0.25 for
start_scalar, 222.2222 for underwater end, -0.5 for underwater
start_scalar") — full confirmation of the struct, and a useful practical
fact: the overwhelming majority of real `MFOG` chunks in this sample are
just this placeholder default, not an authored fog. A future
implementation should not be surprised to find "real" `MFOG` data that's
actually just this default in most files; worth checking a wider sample
specifically for a *non-default* `MFOG` record before picking a fixture
(see Test plan).

**`MFED`: 265/4,000 (6.6%)**, every real size found (16, 80, 48 bytes)
divides evenly by 16 — confirms the 16-byte struct, no correction needed.
Not individually field-decoded this session (time budget went to `MFOG`'s
struct confirmation and the higher-value map-level sidecars below); a
quick follow-up decode (just `doodadSetId` + the 14 unknown trailing
bytes) is a cheap next step, not a blocker.

**`MFOB`: 0/4,000.** The wiki's own version gate for this chunk
(`≥ Midnight (12.1.0.68209)`) names a build materially newer than
anything this session's corpus (a modern retail extraction, but not
necessarily tracking an unreleased future-expansion build) would be
expected to carry — **the wiki gives no struct at all for this chunk
either**, just the version-gate box, then jumps straight to the "WMO
group file" heading. A real, clean, expected-and-explained zero, not a
scanner problem — same disposition as `LIQUID_TODO.md`'s `MCLQ` finding.
Do not implement; watch only.

### C++ data-model sketch

```cpp
// src/wmo.hpp (new, shared with LIQUID_TODO.md's own wmo.hpp sketch)
namespace husk::wmo {

struct FogLayer { float end = 0, startScalar = 0; uint8_t color[4]{}; };  // BGRA, matches CImVector convention

struct Fog {
    bool infiniteRadius = false;   // flag bit 0
    bool flag0x10 = false;         // flag bit 4, meaning undocumented
    Vec3 pos;
    float smallerRadius = 0, largerRadius = 0;
    FogLayer fog, underwaterFog;   // fogs[0], fogs[1]
};

// MFED, Shadowlands+. One entry per MFOG entry (same array length),
// matched positionally -- the wiki doesn't document unk1's meaning, kept
// raw.
struct FogExtraData {
    uint16_t doodadSetId = 0;
    std::array<uint8_t, 14> unk1{};
};

}  // namespace husk::wmo
```

### Consumption plan

Following the M2-particle/ribbon precedent: `dump-chunks`-only, full
records, no `.glb` presence at all — **not even a minimal anchor**, unlike
every other item in this document. A fog volume has a `pos` + two radii
(a sphere-like falloff volume, similar in *shape* to a light's attenuation
sphere but describing atmospheric density, not illumination) but no
natural "owning node" to anchor it to the way an M2 ribbon/particle
anchors to a bone joint — a WMO has no skeleton. The anchor could
reasonably become a standalone glTF node (translation = `pos`, parented
at the WMO's own root/scene node, not a joint) once WMO root/scene
structure exists at all — recommend this as a `node-possible, unclaimed`
target, matching `WORLD_COMPLETENESS.md`'s own existing call, once WMO
placement is implemented, not before.

- **Parse**: `full` for `MFOG` (confirmed); `deref` for `MFED` (struct
  confirmed by size, fields not yet individually decoded); `n/a` for
  `MFOB` (no real bytes, no struct).
- **Consumption**: `diagnostic` (`dump-chunks` JSON).
- **glTF ceiling**: `node-possible, unclaimed` — matches
  `WORLD_COMPLETENESS.md`'s existing call exactly.

---

## 2. WMO particulate/ambient/box volumes (`MPVD`/`MAVG`/`MAVD`/`MBVD` + refs)

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/WMO.md`, `## MPVD`
(737–747), `## MAVG` (749–780), `## MAVD` (781–800), `## MBVD` (801–821)
— all root-scoped (all four are documented *before* line 1020's "WMO
group file" heading, unlike the reference-list chunks below). `## MPVR`
(2328), `## MAVR` (2338), `## MBVR` (2348), `## MFVR` (2358) — all
group-scoped (nested in `MOGP`, per the Scoping correction in
`LIGHTING_TODO.md`).

### Struct, as documented

```
struct MPVD { /* Unknown -- wiki gives no fields at all */ } particulateVolumes[];

struct MAVG {                        // 48 bytes -- global ambient (pos/start/end always 0)
    C3Vector pos; float start, end;
    CImVector color1, color2, color3;
    uint32_t flags;                   // &1: use color1+color3
    uint16_t doodadSetID;
    char _0x26[10];
};

struct MAVD {                        // 48 bytes -- per-doodad-set ambient override
    C3Vector pos; float start, end;
    CImVector color1, color2, color3;  // color1 overrides MOHD.ambColor
    uint32_t flags;                     // &1: use color2+color3
    uint16_t doodadSetId;
    char _0x26[10];
};

struct MBVD {                          // 128 bytes -- box-shaped ambient volume
    C4Plane _0x00[6];                   // 6 planes (position+start?), 96 bytes
    float end;
    CImVector color1, color2, color3;
    uint32_t flags;                      // &1: use color2+color3
    uint16_t doodadSetId;
    char _0x76[10];
};

uint16_t mapobject_particulate_volume_refs[];  // MPVR, group-scoped, into MPVD
uint16_t mapobject_ambient_volume_refs[];      // MAVR, group-scoped, into MAVD
uint16_t mapobject_box_volume_refs[];          // MBVR, group-scoped, into MBVD
```

**Resolution order, per the wiki's own documented algorithm** (worth
transcribing exactly, since it's a real, non-obvious priority chain a
future implementation needs, not just "parse the struct"): a WMO's base
ambient color is `MAVG[doodadSetID]` if a matching entry exists, else
`MAVG[0]`; if no `MAVG` exists at all, fall back to `MAVD[0]`; if neither
exists, fall back to `MOHD.ambColor`. `MBVD` is documented as "only read
if a `MAVG` or `MAVD` chunk exists" — a real conditional dependency
between chunk types, not independent data.

### Real-data verification (this session)

All from the same 4,000-root-file scan as item 1:

- **`MAVG`: 232/4,000 (5.8%)**, every real size found exactly **48
  bytes** (1 record) — confirms the struct exactly, consistent with the
  "global ambient, pos/start/end always 0" framing (a single default
  record, matching `MFOG`'s own "mostly the default entry" pattern
  above).
- **`MAVD`: 45/4,000 (1.1%)**, sizes 144/96/48 all divide evenly by 48
  (3/2/1 records) — confirms the struct.
- **`MBVD`: 7/4,000 (0.2%)**, sizes 128/256/4096 all divide evenly by
  **128** exactly (1/2/32 records) — confirms the struct exactly as
  documented (the 6×`C4Plane` + trailing fields sum to precisely 128
  bytes, no discrepancy).
- **`MPVD`: 10/4,000 (0.25%)**, real sizes 25,632 / 21,360 / 4,272 bytes.
  **Tested the hypothesis that `MPVD` shares `MAVD`'s own 48-byte record
  shape** (plausible on its face: `WORLD_COMPLETENESS.md` groups it
  alongside `MAVG`/`MAVD`/`MBVD` as one conceptual family, and all three
  real sizes divide evenly by 48 — `25632/48=534`, `21360/48=445`,
  `4272/48=89`, all exact). **Decoded at that stride and found the result
  inconclusive, not confirmed**: the first record's leading `pos.x`
  decodes as a subnormal float (`~4.06e-44`) — a strong sign the assumed
  field layout is wrong (a real authored position value landing on a
  subnormal is implausible), and only 1 of 5 decoded records had any
  non-zero fields at all (the rest were entirely zero, which could mean
  "mostly padding/unused entries" or "wrong stride entirely"). **`MPVD`'s
  real struct remains genuinely unresolved** — the wiki's own "Unknown"
  is still accurate. This is flagged honestly as an open item, not
  patched over with an unconfirmed guess (same "don't guess at
  semantics, expose raw until confirmed" discipline `.bone`'s own
  reverse-engineering used before its shape was actually nailed down).
  **Recommended next step**: a from-scratch reverse-engineering pass
  (same shape as the original `.bone` investigation) — try alternate
  strides (a divisor search across all 10 real files' sizes, the same
  "which stride does every real file agree on" method `.phys`'s `SHOJ`
  ambiguity and `DETL`'s stride correction both used), decode candidate
  fields as both floats and ints, and look for a plausible `doodadSetId`-
  shaped uint16 field (small integers, matching the pattern
  `MAVG`/`MAVD`/`MBVD` all share) before committing to any layout.
- **`MPVR`/`MAVR`/`MBVR`/`MFVR`** (group-scoped reference lists,
  confirmed via the same nested-in-`MOGP` scan `LIGHTING_TODO.md`'s
  `MNLR`/`MOLR` check used): **`MPVR` 51/2,570 (2.0%), `MAVR` 86/2,570
  (3.3%), `MBVR` 87/2,570 (3.4%), `MFVR` 307/2,570 (11.9%)** — all real,
  all present, sizes consistently small even numbers (2/4/12 bytes seen
  — 1/2/6 `uint16_t` entries), matching the documented flat-array shape
  exactly. `MFVR` (fog-volume refs) is notably the most common of the
  four — worth noting for priority, even though `MFOG` itself (item 1)
  had the least additional structure to resolve.

### C++ data-model sketch

```cpp
// src/wmo.hpp (continued)
namespace husk::wmo {

struct Color3 { uint8_t color1[4]{}, color2[4]{}, color3[4]{}; };  // BGRA each

struct GlobalAmbientVolume {   // MAVG
    Vec3 pos; float start = 0, end = 0;
    Color3 colors; uint32_t flags = 0; uint16_t doodadSetId = 0;
};
struct AmbientVolume {         // MAVD -- same layout, different doodadSetId semantics (per-set override, not global)
    Vec3 pos; float start = 0, end = 0;
    Color3 colors; uint32_t flags = 0; uint16_t doodadSetId = 0;
};
struct BoxAmbientVolume {      // MBVD
    std::array<std::array<float, 4>, 6> planes{};  // 6x C4Plane
    float end = 0;
    Color3 colors; uint32_t flags = 0; uint16_t doodadSetId = 0;
};

// MPVD -- struct genuinely unresolved, exposed as raw bytes only until a
// real layout is confirmed (see this document's own "Real-data
// verification" section for why the 48-byte-stride guess was rejected).
struct ParticulateVolumeRaw {
    std::vector<uint8_t> rawBytes;
};

}  // namespace husk::wmo
```

### Consumption plan

Same as item 1: `dump-chunks`-only, full records, no `.glb` presence.
`MPVR`/`MAVR`/`MBVR`/`MFVR` resolve which of a *group's* references point
at which root-level volume — useful in the JSON output as a per-group
"which ambient/particulate/box/fog volumes actually apply here" index,
but still metadata, not geometry.

- **Parse**: `full` for `MAVG`/`MAVD`/`MBVD` (confirmed) and the four
  group-ref arrays (confirmed); `descriptor` for `MPVD` (struct
  unresolved, raw bytes only).
- **Consumption**: `diagnostic` (`dump-chunks` JSON).
- **glTF ceiling**: `node-possible, unclaimed` — matches
  `WORLD_COMPLETENESS.md`'s existing call.

---

## 3. Map-level fog volumes (`.wdt`'s `_fogs` sidecar: `VFOG`/`VFEX`)

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/WDT.md`, `# \_fogs`
(585–646): `## MVER`, `## VFOG`, `## VFEX`.

### Struct, as documented

```
struct {                              // VFOG, 104 bytes
    C3Vector color;                    // 0..1 floats
    float intensity[3];
    float _unk18;
    C3Vector position;                  // server position
    float _unk28;
    C4Vector rotation;                   // quat
    float radius[3];                      // fog start radius, per-axis min/max documented
    int animationPeriods[4];
    uint32_t flags;
    uint32_t modelFileDataId;              // client fallback: 166046 (spells/errorcube.m2)
    uint32_t fogLevel;                      // 0..2
    uint32_t id;                             // globally unique
} volumetric_fogs[];

struct {                               // VFEX, 96 bytes, WarWithin+ (11.0.0.54935), version 2 only
    uint32_t Unk0;                       // default 1
    float Unk1[16];
    uint32_t VFOG_ID;                     // -> VFOG.id
    uint32_t Unk3..Unk8;                   // all default 0
};
```

### Real-data verification (this session)

Scanned all 753 real `_fogs.wdt` files. **82/753 (10.9%) have any
non-empty content** — the wiki's own note that these files were "all
empty and not even read by the client" as of their initial Legion
introduction, only gaining real content from BfA (8.0.1.25902) onward, is
consistent with this real number: most of the corpus's `_fogs.wdt` files
are genuinely empty stubs (`MVER` only), and only a real minority carry
actual fog volumes.

**Full decode of a real file** (`world/maps/2656/2656_fogs.wdt`, 6
`VFOG` entries): **exact 104-byte stride confirmed** (`624/104=6.0`
exactly). Every field decodes plausibly: `color` real normalized RGB
floats (`(0.91, 0.45, 0.07)` orange, `(1.0, 0.6, 0.38)` lighter orange —
plausible fog tint colors), `intensity` plausible, `radius` values
(`950/200/2.5`, `1000/100/2.0`, `1000/100/1.25`) fit cleanly inside the
wiki's own documented per-axis min/max ranges (`0-10000`/`0-5000`/
`0.3-22`), `flags=0x3` consistently, `modelFileDataId` real-looking
(1728356, 1728344 — plausible FileDataIDs, same range as this project's
own M2 fixtures), `fogLevel=1` (within the documented 0-2 range), `id`
sequential-ish real values (1659, 1660, 1891). **No discrepancy found —
full confirmation.**

**`VFEX`: 6 real records in the same file, exactly 96 bytes each.**
`Unk0=1` in every record (matches the wiki's own "Default 1" note
exactly). `VFOG_ID` cross-references correctly and exactly against the
sibling `VFOG` entries' own `id` field (1659, 1660, 1891, ... — a clean,
verifiable foreign-key relationship, not just plausible-looking).
`Unk1[:3]` decodes as plausible extent-like floats (`(600,600,200)`,
`(70,70,25)`, `(225,225,225)`) — consistent with the wiki's own "first 3
floats always seem to have proper values" hedge.

**A real, previously-undocumented finding**: immediately following the
6 `VFOG`/`VFEX` pairs in this same real file, this session's scan found
**6 more chunks tagged `VFE2`, 176 bytes each, one per `VFOG` entry** —
a tag **not mentioned anywhere in `WDT.md`'s `_fogs` section at all**.
Not decoded further this session (a genuinely new discovery made late in
the investigation, flagged rather than guessed at blind) — worth a
dedicated follow-up pass (the same "check plausible field values, find
the record stride, cross-reference against `VFOG_ID` if one exists"
method this whole document already uses). Given the naming pattern
(`VFOG`→`VFEX`→`VFE2`), this is plausibly a *third* extension chunk
(War Within added `VFEX` as "version 2 of the format"; `VFE2` could be a
still-newer, even-more-recent addition the currently-mirrored wiki
snapshot simply predates) — a real, concrete lead for
`documentation/wowdev-wiki/HUSK_AMENDMENTS.md`-style follow-up once a
later pass folds this document's findings back into `WIKI_FINDINGS.md`,
per this investigation's own instructions.

### C++ data-model sketch

```cpp
// src/wdt_fogs.hpp (new) -- .wdt's own byte layout is out of husk's
// current scope (WORLD_COMPLETENESS.md's Sidecar & dependency formats
// section), but this sidecar-of-a-sidecar is small enough to sketch
// directly rather than waiting on full .wdt support.
namespace husk::wdt {

struct Vec3 { float x = 0, y = 0, z = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };

struct VolumetricFog {
    Vec3 color;                          // 0..1
    std::array<float, 3> intensity{};
    float unk18 = 0;
    Vec3 position;
    float unk28 = 0;
    Quat rotation;
    std::array<float, 3> radius{};
    std::array<int32_t, 4> animationPeriods{};
    uint32_t flags = 0;
    uint32_t modelFileDataId = 0;
    uint32_t fogLevel = 0;
    uint32_t id = 0;
};

// VFEX, optional, matched to its VolumetricFog by id, not position --
// WDT.md: "at most one per VFOG entry."
struct VolumetricFogExtra {
    uint32_t unk0 = 0;
    std::array<float, 16> unk1{};
    uint32_t vfogId = 0;
    std::array<uint32_t, 6> unk3through8{};
};

// VFE2 -- undocumented on wowdev.wiki as of this session's mirror
// (wiki_revision unknown), found this session via real corpus bytes.
// 176 bytes, one per VFOG entry in the one real file checked. Exposed
// raw until decoded.
struct VolumetricFogExtra2Raw {
    std::array<uint8_t, 176> rawBytes{};
};

}  // namespace husk::wdt
```

### Consumption plan

Same `dump-chunks`-only pattern as items 1–2 — a map-level fog volume
has a real `position` and a plausible anchor point, but no owning
node/joint of any kind exists at map scope in husk's current or planned
architecture (there's no "map root node" concept yet — that would only
exist once `.wdt`/ADT-tile-grid placement is implemented at all). Recommend
treating this the same as WMO fog (item 1): a real anchor becomes
possible once map-level scene structure exists, `node-possible,
unclaimed` until then.

- **Parse**: `full` for `VFOG`/`VFEX` (confirmed); `descriptor` for
  `VFE2` (undocumented, raw bytes only).
- **Consumption**: `diagnostic` (`dump-chunks` JSON, once `.wdt`
  sidecar-of-a-sidecar support exists at all).
- **glTF ceiling**: `node-possible, unclaimed`.

---

## 4. Map-level particulate volumes (`.wdt`'s `_mpv` sidecar: `PVPD`/`PVMI`/`PVBD`)

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/WDT.md`, `# \_mpv`
(648–703): `## MVER`, `## PVPD`, `## PVMI`, `## PVBD`.

### Struct, as documented

```
enum mpv_version : uint32_t { v0, v1, v2, v3, v4 };  // v3: 8.0.1.26567-27404

struct { C2Vector _unk00; float _unk08; float _unk0c; } particle_volume_pd[];  // PVPD, 16 bytes

struct { char _unk00[size-per-version]; } particle_volume_mi[];  // PVMI, 0xF5C/0xFE8/0x10D8 per version

struct {                              // PVBD, 64 bytes
    uint32_t num_unk1C;
    CAaBox _unk04;                     // bounding box, 24 bytes (2x C3Vector)
    uint32_t _unk1C[8];                 // indices into PVPD
    uint32_t _unk3C;                     // "entry complete" boolean
} particle_volume_bd[];
```

**A real, load-bearing structural note the wiki states but is easy to
miss**: the file "does require the exact order of PVPD, PVMI, PVBD" and
these three tags **repeat as a group**, once per particulate-volume
complex — not one array per tag. Confirmed directly this session (see
below); a parser assuming "one `PVPD` chunk, one `PVMI` chunk, one
`PVBD` chunk per file" will silently undercount by a large factor on any
real multi-volume file.

### Real-data verification (this session)

Scanned all 753 real `_mpv.wdt` files. **48/753 (6.4%) have real
content.** All 48 report `MVER=3` (`mpv_version_3`), confirmed by direct
decode.

**The repeating-group structure, confirmed directly**: decoding
`world/maps/2656/2656_mpv.wdt` byte-for-byte found the real chunk
sequence is `MVER, PVMI, PVPD, PVBD, PVMI, PVPD, PVBD, PVMI, PVPD, PVBD,
...` — **5 full `(PVMI, PVPD, PVBD)` groups in this one file**, not a
single occurrence of each tag. This session's first-pass scanner
(treating each tag as a single per-file chunk, matching the file-presence
convention every other scan in this document uses) undercounted by
exactly this factor before being corrected — **619 total chunk
occurrences of each of `PVPD`/`PVMI`/`PVBD` across the 48 real files**,
not 48. Flagged prominently here since it's the one place this whole
document's real data required a mid-session correction to the scanning
methodology itself, not just a struct detail.

**`PVMI` size confirms the version enum directly**: `4312` bytes exactly
= `0x10D8` = `mpv_version_3`'s documented size, matching the file's own
`MVER=3` — a clean, self-consistent cross-check (not decoded field-by-
field, since the wiki itself only calls this "a huge blob... might not
actually be pure binary `WWFParticulateGroups`" — genuinely opaque, not
worth reverse-engineering without more specific evidence of its internal
shape).

**`PVPD` fully confirmed, 16 bytes exactly** (`96/16=6`, `160/16=10`,
etc., all exact across every real hit). Decoded 5 real records: `_unk00`
(the documented `C2Vector`) decodes as real unit-ish vectors within
`[-1, 1]` (`(0.976, -0.219)`, `(0.974, 0.224)`, `(-0.018, 1.0)`, ...) —
**exact confirmation of the wiki's own hedged "[-1.f, 1.f]" note**.
`_unk08` decodes as **exactly `-0.0` in every single real record
checked** — again an exact match for the wiki's own hedged "only seen:
-0.f" note. `_unk0c` decodes as plausible large-magnitude floats
(thousands range, e.g. `-4454.7`, `2717.7`) — consistent with a world-space
coordinate (likely a Z/height value, unconfirmed which axis).

**`PVBD` fully confirmed, 64 bytes exactly** (`448/64=7`, `960/64=15`,
`384/64=6`, `1536/64=24`, `320/64=5`, `576/64=9`, all exact). Decoded 3
real records: `_unk04` (`CAaBox`) decodes as a real, plausible
axis-aligned bounding box in world-space coordinates (min/max corners a
few dozen units apart on X/Y, hundreds of units on Z — a tall, narrow
volume, plausible for e.g. a chimney/vent-shaped particulate effect
region), `_unk1C[8]` all-zero in the samples checked (consistent with
`num_unk1C=0`, i.e. "this entry doesn't reference any `PVPD` records" —
the field is presumably only populated when `num_unk1C>0`, not checked
this session since no real record with a nonzero count was in the
5-record sample), `_unk3C=1` in every sample (matches the "entry
complete" boolean interpretation — no real "joined with next entry"
case observed this session).

### C++ data-model sketch

```cpp
// src/wdt_mpv.hpp (new)
namespace husk::wdt {

struct ParticulateVolumePd {   // PVPD
    float unk00[2]{};           // C2Vector, real range [-1, 1] (confirmed)
    float unk08 = 0;            // real value always -0.0 in this session's sample
    float unk0c = 0;             // plausible world-space coordinate
};

// PVMI -- opaque per the wiki's own hedge ("might not actually be pure
// binary WWFParticulateGroups"), exposed raw, sized per mpv_version.
struct ParticulateVolumeMiRaw {
    std::vector<uint8_t> rawBytes;  // 0xF5C/0xFE8/0x10D8 per version, confirmed 0x10D8 for v3 this session
};

struct Box { float min[3]{}, max[3]{}; };  // CAaBox

struct ParticulateVolumeBd {   // PVBD
    uint32_t numUnk1C = 0;
    Box bounds;
    std::array<uint32_t, 8> unk1C{};  // indices into ParticulateVolumePd, only meaningful when numUnk1C > 0
    bool entryComplete = false;        // unk3C, per the wiki's own "boolean" hedge
};

// One (PVMI, PVPD*, PVBD*) group -- NOTE: PVMI is a single blob per
// group; PVPD/PVBD are themselves arrays within the group (their own
// chunk sizes divide evenly by 16/64 respectively) -- confirmed this
// session real files can have >1 PVPD/PVBD record inside one group's
// own chunk pair, on top of >1 group per file.
struct ParticulateVolumeGroup {
    ParticulateVolumeMiRaw mi;
    std::vector<ParticulateVolumePd> pd;
    std::vector<ParticulateVolumeBd> bd;
};

}  // namespace husk::wdt
```

### Consumption plan

Same `dump-chunks`-only pattern as every other item in this document.
`PVBD`'s bounding box gives a real, natural anchor shape (unlike `MPVD`'s
still-unresolved struct) — a box, not a point, so even the "minimal
anchor" idea would need to be a box-corner pair or center+extents, not a
single position the way M2 ribbon/particle anchors are. Recommend the
same `node-possible, unclaimed` ceiling, deferred until map-level scene
structure exists.

- **Parse**: `full` for `PVPD`/`PVBD` (confirmed); `descriptor` for
  `PVMI` (opaque blob, no internal structure resolved or attempted).
- **Consumption**: `diagnostic` (`dump-chunks` JSON, once `.wdt`
  sidecar-of-a-sidecar support exists at all).
- **glTF ceiling**: `node-possible, unclaimed`.

---

## Priority order

1. **Map-level particulate volumes (item 4)** — the most structurally
   complete real data of the four (three of four fields/structs fully
   confirmed, including two exact hedged-value matches), a real
   repeating-group gotcha now documented so it isn't rediscovered, and
   real files exist in enough volume (48) to have real fixture variety.
2. **WMO fog (item 1)** — second: simplest struct, fully confirmed,
   highest per-file prevalence (19.4%) of anything in this document,
   even though most real instances are just the placeholder default.
3. **Map-level fog volumes (item 3)** — third: fully confirmed struct
   plus a real, concrete new-chunk lead (`VFE2`) worth chasing, but
   rarer (10.9% of an already-thin 753-file universe) than item 4.
4. **WMO particulate/ambient/box volumes (item 2)** — fourth:
   `MAVG`/`MAVD`/`MBVD` are fully confirmed and cheap, but `MPVD` (the
   header row's own namesake chunk) is a genuine open reverse-engineering
   problem, not a quick win — don't let the easy 75% of this item block
   on the hard 25%; ship `MAVG`/`MAVD`/`MBVD`/the four ref-lists first,
   track `MPVD` as its own explicit sub-item.

---

## Test plan

**Real candidate fixtures**:

- `MFOG`/`MAVG` (default-value case): `world/wmo/cameron.wmo` (decoded
  above) — small, simple, good for a "confirms the struct, not
  necessarily interesting data" unit test.
- Non-default `MFOG`/`MFED`/`MAVD`/`MBVD`/`MPVD`: all of this session's
  real hits cluster in `world/wmo/dungeon/torghastraid/*.wmo` and
  `world/wmo/dungeon/9du_plaguefalldungeon_sanctum01.wmo` (both already
  used as fixtures in `LIGHTING_TODO.md` for `MNLD`/`MOLT` too — good
  candidates for one shared multi-purpose fixture pair covering both
  sibling documents) plus `world/wmo/dungeon/etherealraid/
  11du_etherealraid_darkheart01.wmo` (the specific `MPVD` fixture
  decoded above, needed for the still-open struct investigation).
- `_fogs.wdt`: `world/maps/2656/2656_fogs.wdt` (6 real `VFOG`/`VFEX`/
  `VFE2` groups, fully decoded above — the canonical fixture for this
  whole item) plus `world/maps/2694/2694_fogs.wdt` (the largest real
  hit, 11,232-byte `VFOG` chunk, for a stress/scale check).
- `_mpv.wdt`: `world/maps/2656/2656_mpv.wdt` (5 real groups, fully
  decoded above) plus `world/maps/2735/2735_mpv.wdt` (the largest real
  `PVBD` hit, 1,536 bytes = 24 records, for a scale check) and
  `world/maps/warsonggulch2/warsonggulch2_mpv.wdt` (a named/instanced
  map rather than a numeric zone id, for path-handling variety).

**Synthetic-vs-real split**: `MFOG`/`MAVG`/`MAVD`/`MBVD`/`VFOG`/`VFEX`/
`PVPD`/`PVBD` structs are all confirmed enough to unit-test against small
synthetic fixtures first (`tests/test_phys.cpp`-style); real-data
regression tests should assert exact record counts and spot-check the
specific decoded values this document already reports (the `MFOG`
default-value match, the `PVPD` `-0.0`/`[-1,1]` match, the `VFOG`/`VFEX`
id cross-reference) as concrete, reproducible pass/fail conditions —
**not** `MPVD` or `PVMI`, which have no confirmed struct to assert
against yet.

---

## References

- **wowdev.wiki**: `documentation/wowdev-wiki/md/WMO.md` (`## MFOG
  chunk` 656–698, `## MCVP chunk` 699–706 for context only, `## MFED`
  822–834, `## MFOB` 1012–1019, `## MPVD` 737–747, `## MAVG` 749–780,
  `## MAVD` 781–800, `## MBVD` 801–821, `## MPVR` 2328–2336, `## MAVR`
  2338–2346, `## MBVR` 2348–2356, `## MFVR` 2358–2366);
  `documentation/wowdev-wiki/md/WDT.md` (`# \_fogs` 585–646, `# \_mpv`
  648–703).
- **wow.export** (design ideas only, no code copied):
  `reference/wow.export/src/js/3D/loaders/WMOLoader.js` (its own `MFOG`
  parser — confirms the same field layout independently, decoding into
  a plain nested-object shape rather than a typed struct, same "metadata,
  not geometry" treatment this document's own consumption plan follows);
  `reference/wow.export/src/js/3D/exporters/WMOExporter.js` (folds
  `wmo.fogs` straight into its own scene-info JSON sidecar, never into
  mesh/scene geometry — the same metadata-only precedent
  `LIQUID_TODO.md`'s References section already cites for `MLIQ`, cited
  again here since it applies just as directly).
- **husk `src/`**: `M2_COMPLETENESS.md`'s Particles/Ribbons rows,
  `src/gltf.hpp`'s `Skeleton::EmitterAnchor`/`ribbonAnchors`/
  `particleAnchors`, `src/cmd_dump.cpp`'s `dumpEmitters` — the direct
  precedent this whole document's consumption plan is built on;
  `src/json_writer.hpp` (the streaming JSON writer every `dump-chunks`
  output already uses, reused unmodified for these new record types);
  `src/chunk.hpp` (reused unmodified, same as `LIQUID_TODO.md`/
  `LIGHTING_TODO.md`).
- **Real corpus counts, this session** (`/media/luna/data/wow_export`,
  scratch scripts in `/media/luna/work/cache/tmp/.../scratchpad/`, not
  committed): `MFOG` 777/4,000 root files (19.4%, always 1 default
  entry); `MFED` 265/4,000 (6.6%); `MFOB` 0/4,000; `MAVG` 232/4,000
  (5.8%); `MAVD` 45/4,000 (1.1%); `MBVD` 7/4,000 (0.2%); `MPVD` 10/4,000
  (0.25%, struct unresolved); `MPVR` 51/2,570 group files (2.0%), `MAVR`
  86/2,570 (3.3%), `MBVR` 87/2,570 (3.4%), `MFVR` 307/2,570 (11.9%);
  `_fogs.wdt` 82/753 (10.9%) non-empty; `_mpv.wdt` 48/753 (6.4%)
  non-empty, 619 total `(PVMI,PVPD,PVBD)` group occurrences across
  those 48 files.
