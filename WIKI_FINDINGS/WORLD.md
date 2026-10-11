# wowdev.wiki findings — WMO/WDT/WDL/PM4/PD4 (world-format expansion)

Current, correct facts only. Full evidence trail: `../WIKI_FINDINGS_HISTORY.md`
§15. The full struct listings, C++ data-model sketches, and test plans live in
the eleven companion `*_TODO.md` files under `TODO/WORLD/`. This page records
what would otherwise be lost once those are implemented and deleted. Formats
split out into their own file as their parsers land: ADT is in `ADT.md`
(`src/adt.cpp`); WMO, WDT, WDL, PM4 and PD4 stay here. WMO root/group parsing
exists (`src/wmo.cpp`, `tests/test_wmo.cpp`); nothing exports it yet.

---

## Chunk-tag byte reversal is universal — verified

WMO, ADT, WDT, and WDL all reverse chunk-tag bytes on disk — same
convention `.phys` uses (`PHYS.md`), opposite of M2's own inline chunks.
None of the relevant wiki pages state this explicitly; two independent
sibling investigations each caught a real scanner bug from forgetting it
before trusting their own results.

## WDT — verified

`_occ.wdt`'s occlusion heightmap is really `int16_t[17*17]` (578 bytes), not
the documented `(17*17+16*16)` (1,090 bytes) — confirmed across all 959
real `_occ.wdt` files. The root `.wdt`'s global single-WMO `MODF` record can
omit `MWMO` entirely when `flags & 0x8` is set (35/228 real global-WMO
files), with `nameId` then holding a real FileDataID directly. `MAI2`
(wiki: "unshipped") is real in 2 files.

12.1.0 mount, all 969 root `.wdt`s (2026-10-11):
- `MAIN` is indexed `tileY * 64 + tileX`: the tile files match it that
  way (azeroth: 1,176 of 1,176), not transposed (591 mismatches).
- Every `MAIN` has-ADT tile has its root file, but 11,014 root files on
  the mount are stale: the WDT doesn't list them (`MAIN` bit clear, `MAID`
  root 0); azeroth has 1,335 root files for 1,176 tiles. The listfile
  still names these old FileDataIDs and CASC still holds them.
- `MAIN` flags are only ever 0, 1 or 2 (2 = wiki "all water", never with
  the has-ADT bit). Every map with `MAID` has a `MAID` root for exactly
  its `MAIN` tiles.
- 228 maps have no tiles and one `MODF` WMO; 13 more have neither (12
  garrison levels, `MPHD` 0xda; one 0x4e). 3 map directories have tiles
  but no `.wdt` (`islands`: 603, `islands666`: 16, `dungeonblockout`: 6);
  2 (`2641`, `2665`) have neither.

## ADT — see `ADT.md`

Split out once `src/adt.cpp` landed: `ADT.md` holds every verified ADT fact,
including the planning-pass ones that used to live here.

## WMO — verified

- `GFID` (LOD-tier group-file resolution) is **row-major**:
  `index = lodTier * nGroups + groupIndex`, zero meaning "no file" — not
  stated as a formula on the wiki.
- `MOGX` is a fixed **256-byte** (64×`uint32_t`) chunk, not the wiki's
  stated 4-byte "one single value" — only the first slot is ever non-zero,
  the rest is padding. Both `MOGX` and `MOQG` are group-level, not
  root/group as an earlier internal note stated.
- `MOMX` (wiki: "just a guess," one named example) is present in **30.5%**
  of real root WMO files (3,931/12,869), not a rare one-off — its record
  count exactly equals `MOHD.nTextures`. Per-field semantics remain
  unresolved (not FileDataID references, despite the wiki's guess).
- `MODI`'s real entry count can **exceed** `MOHD.nDoodadNames` (14.5% of
  5,000 real roots) — always ≥, never less; size the array off the chunk's
  own byte length, never trust the header field alone.
- Shadowlands+ `MWDR`/`MWDS` doodad-set-activation indirection verified
  end-to-end against two real placements activating different multi-set
  combinations into the same `MODS` table.
- `MPBV`/`MPBP`/`MPBI`/`MPBG` confirmed genuinely absent corpus-wide (0 of
  71,929 real group files) — the wiki's rarity claim holds at full scale.
- `MOVT`/`MONR` are plain **(X, Y, Z), Z up**, the same frame as M2, despite
  the wiki's "(X,Z,-Y) order" note. `guardtower_001.wmo` spans ~28 × 28 on
  the first two components and 34 on the third; `MODD` rotations of the
  same building turn about the third axis. Group vertex ranges match the
  `MOHD`/`MOGP` bounding boxes component for component.
- `MOHD.nDoodadDefs` can disagree with `MODD`'s real length too
  (`guardtower.wmo`: header 173, `MODD` 170 records). Size every record array
  off its chunk.
- Multiple `MOTV` and `MOCV` chunks are common, not edge cases: 36% and 26%
  of 3,487 sampled group files (seeded 4,000-file sample of `world/wmo/`).
  All 4,000 parse cleanly with `src/wmo.cpp`, and per-triangle `MOPY`
  counts always equal `MOVI / 3`.
- `MOBA`'s `material_id_large` flag (0x02 at 0x16) must decide which field
  is the batch's material: flagged batches leave `material_id` at 0.

## WMO materials in the client shaders (12.1.0) — verified

Read from the client's compiled shaders (`../WIKI_FINDINGS_HISTORY.md`
§19), not from WMO files. This build has no `MapObj*` shaders: every
`MOMT` material is drawn by one container, `pixel/dx_5_0/uber`, which
switches on the material's shader ID (`cb2[4].x`, values 0–24; `cb2[4].y`
case 25 is an outline effect). Behaviour per ID, names from `WMO.md`'s
shader table (`uv0`/`uv1`/`uv2` are the vertex shader's generated UVs,
`v3.w` the second vertex-colour alpha, below):

| ID | Wiki name | Client behaviour |
|---|---|---|
| 0, 16 | Diffuse, DiffuseTerrain | `t0`, alpha `t0.a` |
| 1, 2, 4 | Specular, Metal, Opaque | `t0` rgb, alpha 1 |
| 3 | Env | `t0`; emissive `t0.a·t1(uv1)` |
| 5 | EnvMetal | `t0`; emissive `t0.rgb·t0.a·t1(uv1)` |
| 6 | TwoLayerDiffuse | `lerp(t0, t1, t1.a)`, then back toward `t0` by `v3.w` |
| 7 | TwoLayerEnvMetal | `lerp(t1, t0, v3.w)`; emissive `mix.rgb·mix.a·t2(uv2)` |
| 8 | TwoLayerTerrain | `lerp(t1, t0, v3.w)` |
| 9 | DiffuseEmissive | `t0`; emissive `t1.rgb·t1.a·v3.w` |
| 10, 14 | waterWindow, submarineWindow | no output (drawn by `FFXWaterWindow`/`FFXSubmarineWindow`, as the wiki says) |
| 11 | MaskedEnvMetal | `2·t0·t1` toward `t2` by `saturate(t2.a·v3.w)`, then by `t0.a` |
| 12 | EnvMetalEmissive | emissive `t0.rgb·t0.a·t1 + t2.rgb·t2.a·v3.w` |
| 13 | TwoLayerDiffuseOpaque | `lerp(t1, t0, v3.w)`, alpha 1 |
| 15 | TwoLayerDiffuseEmissive | two-layer blend; emissive `t1.rgb·t1.a·(1 − v3.w)` |
| 17 | AdditiveMaskedEnvMetal | `2·t0·t1 + t2·saturate(t2.a·v3.w)`, blended by `t0.a` |
| 18 | TwoLayerDiffuseMod2x | as 6, then `·t2(uv2)·2` |
| 19 | TwoLayerDiffuseMod2xNA | `lerp(t0, 2·t0·t1, v3.w)` |
| 20 | TwoLayerDiffuseAlpha | as 18, with `t2.a` in place of `v3.w` |
| 21 | Lod | `t0`; `2·t1` replaces the baked vertex light; `t2` emissive |
| 22 | Parallax | height `t17`, screen-derivative tangent frame, parallax-offset `t3`/`t4`, blended with `t2`, and with `t0` by `v3.w` |
| 23 | (DF+) | 4-layer height blend like terrain's: layers `t1`–`t4`, heights `t17`–`t20`; sphere-mapped `t0` emissive |
| 24 | **not on the wiki** | EnvMetal (5) with a separate opacity texture: alpha and alpha test from `t2.a` |

Against `WMO.md` / `WMO/Rendering.md`:

- `QueryLighting` doubles `MOCV` rgb ("not sure why?"): the vertex shader
  writes `2·MOCV` as the baked-light term.
- `FixColorVertexAlpha`'s 0/255 alpha is the interior/exterior light
  blend, as the wiki suspects: the shader lerps the scene light toward a
  per-draw light set by `1 − MOCV.a`.
- The two-layer materials blend by a second vertex alpha (`v3.w` above);
  that this attribute is `CVERTS2`/second `MOCV` is **inferred**.
- `MOC2` "used in math for PARALLAX and UNK_DF_SHADER_23": only 23 reads
  it (`xyz` layer weights, `w` tint toward a per-draw colour). Parallax
  (22) does not in this build.
- Material 23 takes four raw UV attributes (the DF+ fourth `MOTV`).
- `MOGP.fogIds[4]`: the fog code loops over at most 4 fog records per
  draw (same code for M2 and terrain).
- 12.1 WMOs run the full dynamic-lighting path (sun, hemispheric ambient,
  clustered lights, shadows), with `MOCV` as a diffuse multiplier and,
  in lighting mode 2, an added ambient term — not the classic "mostly
  baked vertex colour" path.
- The vertex shader generates each UV set by a per-draw mode (`cb2[1]`,
  an integer): 0 UV0 through the first texture transform, 1 UV1 through
  the second, 2 sphere map, 3 `reflect(V, N).xy`, 4 zero, 5 a planar
  projection of the position, 6 UV2 as-is, 7 UV0 as-is, 8 UV2 through the
  first transform. Material 23 skips generation and passes four raw UVs.
- The DX12 build's vertex struct `WMOVertex` is `{ float3 position,
  float3 normal, uint colours[2], float2 uv[4], uint }`: two vertex
  colours (`MOCV` and the second `MOCV` of `CVERTS2`), four UV sets
  where the wiki's client array holds three `MOTV`, and one more `uint`.
  The layout is verified; that the last `uint` is `MOC2` is **inferred**.

## WDT `_lgt.wdt` lights in the client shaders (12.1.0)

Clustered lighting reads a 192-byte `ShaderLight` record per light (the
DX12 type: `float4x4`, seven `float4`, four `int`). Offsets used by the
shaders: 64 position, 80 spot direction, 96/112 two colours with a
distance gradient between them, 128 attenuation start and culling range,
136 falloff scale and spot exponent, 160 inner/outer cone cosines and
scale, 176 type (0 point, 1 spot). Layout and offsets are verified.
Mapping to `MPL2`/`MPL3`/`MSLT` fields (`position`, `attenuationStart`,
`attenuationEnd`, `color`·`intensity`, spot `rotation`, `innerAngle`/
`outerAngle`) is **inferred**. The `float4x4` at offset 0 is read by no
shader in this build.

`MPL3`'s flag 1, "cast (raytraced?) shadows (on D3D12 + min shadowrt
level 2)", matches DX12-only shaders: light-buffer passes with a
per-light `RayQuery` shadow test (`lightbufferomnishadow`,
`lightbufferspotshadow`) and a ray-traced sun-shadow library `shadowrt`
(**inferred** link; the shaders are verified). `MPL3`'s cookie fields
match the light-buffer passes' cookie variants (cube cookie for point
lights, 2D for spots; **inferred**). `MLTA` animation is not in any
shader, so the client animates intensity.

## Liquid/lighting/fog — verified

**WMO `MAVG`/`MAVD`/`MBVD`** (seeded 4,000 roots, 2026-10-10): the wiki's
48/48/128-byte layouts hold, every reserved tail is zero, and every
`doodadSetId` is a valid `MODS` index (11 `MAVD` records use a non-zero
set; `MAVG`/`MBVD` never do).

| Claim | Evidence |
|---|---|
| `MAVG` is common and single: one record per file, position/start/end all zero | 1,702 files (not the earlier 5.8%) |
| `MAVG` color1 usually equals `MOHD.ambColor` | 1,464/1,702 |
| `MAVD` start <= end; 100/151 records have zero position/start/end | 151 records |
| `MBVD`'s six planes are unit normals in opposite pairs (0,1) (2,3) (4,5), not (0,3) (1,4) (2,5) | 54/54 |
| Inside an `MBVD` box is dot(normal, p) <= d for all six planes (outward planes, d = distance) | 54/54 non-empty that way; the opposite sign makes all 54 empty |
| `MBVD.end` is at most the box's smallest half-size, reading as a fade distance in from the faces | 52/54 (**inferred**) |

**WMO `MLIQ`** (154 of 4,000 sampled group files, 2026-10-10):

| Claim | Evidence |
|---|---|
| Size is exactly `30 + verts·8 + tiles`, with tiles = verts − 1 per axis | all 154 |
| Vertices and tiles are **row-major, x fastest** — the wiki's "read it column major" note is wrong for these files | 122 non-square grids: row-major gives the smoother surface in 95 (column-major in 2, rest ties), and the vertex-usage/tile agreement in 95 (column-major never) |
| The grid runs +X/+Y from `liquidCorner` at 25/6 yd per tile | rendered tile centres within 1 yd of their group's bounds: 151.4 of 154 files (per-file fraction, summed) vs ≤61 for any flipped axis; the grid snaps to world units and overhangs walls by under a tile (`ruinedkeep_crypt2_015`) |
| Tile low nibble `0xF` = no liquid (MCLQ's convention) | ~47% of tiles; the bounds check above counts only the others |
| Group flag `0x1000` and `MLIQ` always occur together; the "implicit whole-group liquid" case never appears | 154/154, 3,845 with neither |
| The wiki's LiquidType rules hold | 568 resolved liquids in 120 roots: every id is a real `LiquidType.db2` row, and no water type sits on a lava/slime-textured material |

`MH2O` covers 67.1% of real ADT tiles (vs. WMO's own `MLIQ` at 2.1%) — clear
implementation priority. Legacy `MCLQ` confirmed genuinely absent (0/4,000).
`MOLS`/`MOLP`/`MLSS`/`MLSP`/`MPVR`/`MAVR`/`MBVR`/`MFVR`/`MNLR` are all
**group**-file chunks (nested in `MOGP`), not root-level, despite reading as
root-level candidates from the wiki's own page layout. The wholly
undocumented `_fogs.wdt` chunk **`VFE2`** and `MPVD`'s record layout are
both now reverse-engineered — see the dedicated sections below.

## WMO lightset lights: `MOLS` resolved, `MOLP` corrected, `MOP2` verified (2026-10-11)

From a full-corpus chunk scan (**86,134 `.wmo` files**, roots + `MOGP`-nested
group chunks, `tools/corpus_scan_tasks/rare_wmo_chunk_task.py`):

**`MOLS`** (wiki: `char _1[0x38]`, all fields unknown) — resolved from all
**378 real records in 218 files**. It is a complete spotlight record:

```
/*0x00*/ uint32_t  lightIndex;      // small ints, 30 distinct
/*0x04*/ CImVector color;           // BGRA, alpha always 0xFF in corpus
/*0x08*/ C3Vector  position;
/*0x14*/ float     attenStart;      // 4.8..~; < attenEnd throughout
/*0x18*/ float     attenEnd;        // 10.5..38+
/*0x1C*/ float     intensity;       // 0.1..~
/*0x20*/ C3Vector  rotation;        // Euler radians, values reach ±π
/*0x2C*/ float     unk;             // exactly 1.0 in all 378 records
/*0x30*/ float     innerConeAngle;  // radians, 0.06..0.57
/*0x34*/ float     outerConeAngle;  // radians, 0.63..0.785 (≈ π/4 cap)
```

Field names inferred from value classes (byte-quantized color, ±π Euler
range, inner < outer angle pairing, start < end attenuation pairing); the
layout itself (offsets/types) is corpus-verified.

**`MOLP`** — two wiki corrections at scale (57,584 records in 2,736 files):
the `attenStart`(+0x18)/`attenEnd`(+0x1C) labels are **swapped** (start >
end in 97.4% of records under the wiki's reading; swap them and the
corpus is consistent), and `unk5`/`unk6` are **floats, not uint32** —
defaults `-0.0` (+0x24) and `-1.0` (+0x28), with the nondefault values all
plausible floats. (Promotes `LIGHTING_TODO.md`'s 4-record lead to
corpus-confirmed.)

**`MOP2`** — the wiki's "currently only in file 2143042" is stale: **5
real files carry 10 records** (`8nzj_nazjatar_cave01/04*.wmo` + LODs), and
every field decodes plausibly under the wiki's struct (BGRA colors, sane
attenuations, live flicker fields, `lightUnkRecord` all-zero) — its first
real-data verification.

**`MOS2`**: **0/86,134** — the wiki's "in binary, not in files" claim is
now confirmed at full-corpus strength. **`MFOB`** was 0/86,134 in that
export; build 12.1.0.69933 (2026-10-11) has it in 225 of 12,824 roots,
always 0 bytes, so it exists in the format but carries no data yet (see
`TODO/WORLD/MFOB_CASC_REPORT.md`).

## `_fogs.wdt` `VFEX` — fields characterized (2026-10-11)

All 1,542 real records (strictly 1:1 with `VFOG`, like `VFE2`): `Unk0` is
an enable flag (1 in 1,539, 0 in 3). The 16 unknown floats group as **five
float-triples plus one scalar**: +0x04 an extent/radius-like triple
(10..hundreds; the wiki's "first 3 floats" hedge), +0x10 a lone scalar
{0.01, 0.4, 1.0}, +0x14 a *negative-valued* triple (−65..−10,
offset-like), +0x20 a 0.1..1.0 triple, +0x2C a period/speed-like triple
({1, 5, 6, 10, 25, 100}), +0x38 a {0.2, 1, 3, 10} triple. `VFOG_ID` at
+0x44 cross-checks 1,542/1,542. `Unk3..Unk8` (+0x48..+0x5C) are **zero in
every real record** — "default 0" is actually "always 0".

**Correction (2026-10-10, same 1,542 records, per-offset histograms):**
the +0x14 triple is not negative-valued. Its common value is 1.0 (951,
884, 721 records per float), it is signed in a minority (60/157/40
negative), and the three floats range −75..500. Every float from +0x10
to +0x40 defaults to 1.0, which the wiki's "rest are 1?" already said.
The triple boundaries are also not crisp: +0x20 moves with neither
neighbour (non-default together with +0x1C in 454 of 822 records, with
+0x24 in 135 of 573). husk therefore types only `Unk0` and the +0x04
extent, and carries +0x10..+0x40 as a 13-float array.

## WMO `MFOG` / `MFED` (2026-10-10)

From the seeded 4,000-root sample, plus every group file under it:

- **`MFOG`** is in all 4,000 roots, 1..10 records each (1 in 3,706); 3,655
  of 4,534 records are the wiki's "empty fog entry" byte for byte. Flags
  seen: 0x0, 0x1 (F_IEBLEND, 154), 0x1000 (291), 0x11000, 0x10, 0x1010,
  0x1100; only 0x1 is documented. smaller_radius ≤ larger_radius in
  4,526/4,534. Colour alpha is always 0xFF.
- **`MFED`** is in 1,802 roots, and in 1,699 of them it is **empty**. The
  other 103 have exactly one record per `MFOG` record (the wiki's "shall
  be same as MFOG count"). Every `doodadSetId` is a valid `MODS` index (4
  of 300 non-zero). `unk1[0xE]` is zero in all 300 records.
- **`MOGP.fogIds[4]`** index `MFOG` in all 43,476 real slots.
- **Group refs `MFVR`/`MPVR`/`MAVR`/`MBVR`** (12.1.0, 3,648 seeded roots,
  2026-10-11): every ref is inside its root table (`MFOG`, `MPVD`, `MAVD`,
  `MBVD`; 868/187/236/198 refs). `MFVR` replaces `fogIds`: all 481 groups
  carrying it have `fogIds` all zero, and it never lists fog 0 (the
  default fog).
- **Record 0 is the model's default fog** (12.1.0, 4,000 seeded roots,
  2026-10-11). It is the empty entry in 3,631 roots (a -0.0 position in
  88 of them), and a later record never is. In the 255 roots whose record
  0 is a real fog, the `fogIds` lists are left-packed: (1,0,0,0),
  (2,1,0,0), (4,5,0,0); none has a 0 before a nonzero slot. So a 0 slot
  names the default fog, not a list entry. This is inferred from layout
  and frequency, not confirmed against the client.
- **Flag 0x1000** (250 records): the radii are ordinary floats in 149
  and zero in 101. In 116 the record is record 0, with a zero position.
  Record 0 of `9du_torghast_modular_straight02.wmo` has flag 0x1000, a
  NaN bit pattern (0x7F9DB1B8) as `smaller_radius` and the integer 3 as
  `larger_radius`, so under 0x1000 those fields aren't always radii.

## `_fogs.wdt` `VFE2` — reverse-engineered (2026-10-11)

Absent from `WDT.md` entirely (found in real files by two independent husk
investigations). Decoded from **all 1,542 real records across the 90
`_fogs.wdt` files with content** (all `MVER=2`): 176 bytes (0xB0), strictly
**one per `VFOG` record — 1,542 : 1,542 : 1,542 with `VFOG` and `VFEX`** —
paired positionally *and* carrying its `VFOG.id` at +0x4C (both checks
1,542/1,542, same dual pairing `VFEX` uses).

| Offset | Type | Values | Meaning |
|---|---|---|---|
| +0x00 | float×3 | byte-quantized RGB (n/255 in 1,542/1,542) | color A (nonzero in ~1,539) |
| +0x0C | float | 0.04..0.2 | density-like scalar |
| +0x10 | float | 0.5..1.1 | scalar |
| +0x14, +0x18 | float | 0.1..0.55, often 0 | scalars |
| +0x1C | float | 0.25..1.1 | scalar |
| +0x20 | float×3 | byte-quantized RGB | color B (dimmer than A) |
| +0x2C | float×3 | byte-quantized RGB | color C (dimmer than A) |
| +0x38 | float | 0.3..0.7 | scalar |
| +0x3C | float | exactly 1.0 or 2.0 | mode/type |
| +0x40..+0x48 | float×3 | 0.01..3 | scalars (exponent/curve-like) |
| +0x4C | uint32 | 460..1934 | **`VFOG.id` cross-reference** (1,542/1,542) |
| +0x50, +0x54 | float | 10..35, rare (122/1,542) | distance/angle-like pair |
| +0x58, +0x5C | float | rare | scalars |
| +0x68 | float | 0 or 1 (285/1,542) | bool |
| +0x70.. | — | zero in all 1,542 records | reserved |

The three-color + density/curve-scalar palette matches the client fog
model's per-record color roles (fog near/far color, height-fog color,
sun-fog color — `SHADER_FINDINGS/notes/fog.md`'s field-role table), so the
likely reading is `VFE2` = the volumetric fog's own color/curve override
set; **candidate roles only, not confirmed mappings** — confirming them
means tracing how the client fills `cb5` fog records from `VFOG`/`VFE2`
data, which the shader dump alone can't show.

## Particulate volumes: WMO `MPVD` and `_mpv.wdt` `PVMI` — reverse-engineered (2026-10-10/11)

The wiki gives no fields at all for `MPVD` ("Unknown") and hedges `PVMI` as
"a huge blob... might not actually be pure binary WWFParticulateGroups".
**Both are the same 4,272-byte particulate-volume record**; `PVMI` appends a
40-byte map-placement trailer (4,312 = 4,272 + 40). Reverse-engineered from
every instance in the corpus: **62 root WMOs / 193 `MPVD` records** (every
chunk size an exact multiple of 4,272 — stride settled) and **619 `PVMI`
blobs across 48 `_mpv.wdt` maps** (617/619 pass the color+texture-FileDataID
signature; the two misses have zero/odd fields, not a different layout). A
cross-check against the client shaders' `cb_particulate_volume` was
attempted first: the DXIL keeps only the type sequence of
`VS_ParticulateVolume` (944 bytes, offsets computed and verified against the
binding size), no member names — the dx_5_0 DXBC blobs are stripped of
reflection too, so field *names* came from value analysis, not the shaders.
One map-side texture path is the semantic smoking gun:
`environments/particulatevolumes/textures/8xp_ash_tumbling_02.blp`.

**Record shape** (4,272 = 0x10B0 bytes): a 720-byte header, then an array of
**four 844-byte (0x34C) emitter entries** at +0x2D0, then a 176-byte tail
whose final dword (+0x10AC) is the **emitter-entry count** — it equals the
number of nonzero entry slots in **all 193 + all 619 records** (map-side
usage: 619/194/31/2 for entries 0–3, so the four-slot capacity and the 844
stride are both directly observed at scale, including two real 4-emitter
volumes).

Header (offsets record-relative; "n/193" = records with the field nonzero):

| Offset | Type | Meaning | Evidence |
|---|---|---|---|
| +0x00 | uint32 | flags — WMO values {1, 17, 21, 29} (bit 0 always set, bits 2–4 vary); map-side (`PVMI`) uses far more bits, values 3..415 | 193/193, 619/619 |
| +0x04 | uint32 | unknown, {0, 2} | 2/193 |
| +0x08 | float×3 | position (WMO-local in `MPVD`; world-space in `PVMI`) | plausible coords, 192/193 |
| +0x14 | float×3 | extent (box size; 7..4000 per axis) | 193/193 |
| +0x20 | float×3 | rotation, radians (mostly 0; ±π seen at +0x28) | sparse |
| +0x2C | float | unknown, {0.5..10}, often 1 | 193/193 |
| +0x30 | uint32 | unknown id — **not** a FileDataID (listfile hits are nonsense: minimaps, emblems), not per-file-constant (15/62 WMOs have several), but *shared across unrelated files* (16386 very common; 66 distinct map-side) — reads as a preset/template reference | 193/193, 39 distinct |
| +0x34 | uint32 | particle count/density-like int, 25..14,000 | 193/193 |
| +0x38 | uint32 | bool-ish {0,1} | 17/193 |
| +0x3C | float | 0.09..1 (density/opacity-like) | 193/193 |
| +0x40 | float[40][3] | keyframe array, **12-byte (value, value2, time-seconds) triplets** — times strictly ascending (0 → 16.0 in the 9-key record), values pulse 0.01↔1.0: an intensity/emission envelope | 13/193; count at +0x298 matches exactly |
| +0x220 | float[20] | unknown array; 10 records set the first 5 slots, all 1.0 | 10/193 |
| +0x298 | uint32 | keyframe count {3,5,6,9} | 13/193 |
| +0x29C | float | 0.1..18.8, usually 1 | 182/193 |
| +0x2A0 | 12 dwords | unknown block, used by exactly one record | 1/193 |

Emitter entry (offsets entry-relative; entry 0 at record +0x2D0):

| Offset | Type | Meaning | Evidence |
|---|---|---|---|
| +0x00..+0x7F | float×32 | emission parameters, semantics unresolved per-field; characterized values: velocity/wind-like signed floats at +0x10..+0x1C, cone-angle-like radians (0.0873 = 5°, 0.1745 = 10°) at +0x54/+0x58, lifetime/speed-scale floats elsewhere. The renderer's parameter set (spawn box, wind, size min/max, wrap domain, clip planes, heightmap collision) is now decoded from the shader code — `SHADER_FINDINGS/notes/particulate_volumes.md` — so naming these means tracing the CPU's record→CB fill, not more corpus statistics | dense, 191/193 |
| +0x80 | float×4 | RGBA color, byte-quantized (all values n/255) | 185–193/193 |
| +0x90 | float | mostly 0 | sparse |
| +0x94 | uint32 | **particle texture FileDataID** — every listfile-resolvable value is a VFX texture (`particles/dust1.blp`, `snow1.blp`, `raindrop.blp`, `flare.blp`, `ash01.blp`, `pollen.blp`, …) | 193/193, 39 distinct, confirmed |
| +0x98 | uint32×4 | (blend mode {2,3} — alpha/additive; flipbook tilesX; tilesY; **frame count** — settled by the shader code: the vertex shader computes `frameIndex % cb[15].z` and divides UVs by `cb[15].xy` (tilesX/Y), so "mismatches" like (4,4,10) just animate 10 frames of a 16-tile atlas) | 193/193 + shader |
| +0xA8 | float | 1..35 | 181/193 |
| +0xAC.. | — | extended fields, near-always zero (one record sets a handful) | 1/193 |

Record tail (+0x1000..+0x10AF): zero except the final dword at +0x10AC —
the **emitter-entry count** (1..4), confirmed against the nonzero entry
slots in all 193 `MPVD` + all 619 `PVMI` records.

`PVMI`'s extra 40-byte map-placement trailer (offsets record-relative,
verified in all 619 blobs):

| Offset | Type | Meaning | Evidence |
|---|---|---|---|
| +0x10B0 | float×3 | world AABB min | min ≤ max componentwise, 619/619 |
| +0x10BC | float×3 | world AABB max | ditto |
| +0x10C8 | uint32 | **globally unique volume id** (185..1934; same convention as `VFOG.id`) | 619 distinct in 619 blobs |
| +0x10CC | uint32 | **count of the sibling `PVPD` chunk's records** | equal in 619/619 groups (and never equals the `PVBD` count) |
| +0x10D0 | float | AABB max.z again | byte-identical to +0x10C4, 619/619 |
| +0x10D4 | float | AABB min.z again | byte-identical to +0x10B8, 619/619 |

So a `(PVMI, PVPD, PVBD)` group is one particulate volume: `PVMI` holds the
emitter definition (shared with WMO `MPVD`) plus its world box and id, and
its trailer's count field ties the `PVPD` records to it explicitly.

**`PVBD` is a box hierarchy, and the wiki's "indices into PVPD" is wrong**
(all 619 groups, 2026-10-11): the index words name boxes in the same group's
`PVBD`. They exceed the group's `PVPD` count in 143 groups but never its
`PVBD` count. Boxes `0..P−1` (P = the group's `PVPD` count) are leaves, one
per `PVPD` record; every later box has children; exactly one box is nobody's
child (the root). In the 143 groups whose root has `complete` = 0, the
root's sub-boxes share leaves (e.g. leaf 0 under two inner boxes), so the
volume reads as a union of overlapping pieces; with `complete` = 1 it is a
strict tree. `PVPD`'s 2D vector is a unit vector in 4,467/4,467 records, but
reading `PVPD` as vertical half-planes bounding the pieces fails (neither
sign holds; leaf boxes touch their "plane" in 21/4,467) — its meaning stays
open. Positions are world-frame: every `PVMI` position lands on an existing
tile of its map under the terrain frame, and inside its own AABB.

## Collision & culling — verified

WMO's `MOBN`/`MOBR` BSP collision (48% of real group files) maps almost
directly onto husk's existing M2 collision-mesh pipeline, needing only
`MOBR`'s one indirection (triangle index into `MOVI`, not raw vertex).
Portal culling (`MOPV`/`MOPT`/`MOPR`/`MOPE`): **checked directly, not
assumed** — Blender has no cell-and-portal visibility-culling mechanism at
all (confirmed against Cycles' Holdout shader and Ray Visibility toggle,
both ruled out for specific documented reasons). `wow.export` itself has
zero code path for `MOBN`/`MOBR` — husk implementing this would be new
ground, not catching up to existing tooling.

**The BSP doesn't select collision faces (2026-10-10, seeded 1,000-root
sample, 2,759 groups, 20.9M triangles).** In every one of the 2,628
groups with `MOBN`/`MOBR` (group flag 0x1 matches `MOBN` presence in
2,752/2,759), `MOBR` references every triangle of the group, many
several times. So the wiki's "two representations, sometimes
disagreeing" doesn't show up as different face sets: the BSP is a spatial
index over all faces, and the `MOPY`/`MPY2` flags decide which collide.
The flag histogram fits `SMOPoly::isCollidable` (0x08, or 0x20 without
0x04):
- rendered detail faces (0x04 set: 0x44, 0x64, 0x04, 0x24, ...) are about
  70% of all triangles and don't collide;
- collision-only faces (material 0xFF in `MOPY`, **0xFFFF in `MPY2`**,
  1.7M) carry 0x08 (0x48, 0x08, 0x4A, ...);
- the exception: 146,103 collision-only `MPY2` faces carry only 0x100,
  which is undocumented, so the rule excludes them.

**Portals (2026-10-10, the 800 roots with portals in the seeded 4,000):**
`MOHD.nPortals` equals the `MOPT` count in all 4,000. The `MOPT` plane
holds `dot(normal, p) + distance == 0` for every vertex of all 5,069
portals (never `- distance`), with unit normals; 5,003 are quads. Every
`MOPR` ref names a valid portal and group (never its own), `side` is
only ±1, the filler is 0, and the groups' `MOGP` portalStart/portalCount
ranges partition `MOPR` exactly. A group with no portals may store
portalStart **0xFFFF** with count 0. 5,037 portals are linked from two
groups, 28 from one, 3 from none. `side` matches the sign of
`dot(normal, c) + distance` at the owning group's vertex centroid in
8,685/10,103 links; PortalCulling.md's front/back test is one
contributor's guess and reads the other way, so neither is encoded.
**`MOPE`** (74 of these roots, always one record per portal) doesn't
hold what the wiki says: its "portalIndex" field is often 0x4D4F50xx
(the portal index in the low byte under ASCII "MOP"), and its other
fields hold ASCII fragments and float-like garbage.

## Gameplay/misc metadata — verified

Four items promoted from a blanket "n/a" to real `extras` candidates after
being checked for real value rather than dismissed as invisible-therefore-
unimportant: `MDAL` (WMO ambient-color override — affects rendered
lighting), `MCSE` (ADT sound-emitter placement — same shape as M2's own
ribbon/particle anchor), `MCSH` (ADT baked shadow bitmap — real bimodal
shadow-density signal), `MCMT` (ADT per-layer material-ID override). `MOQG`
(WMO per-face ground type) was reconsidered under the same lens and
confirmed to genuinely belong at n/a (audio-only, no visual consequence).

## PM4/PD4 — verified, structural negative

Genuinely never shipped to the client — not an extraction gap like
`EXP2`/`PFDC`. Zero `.pm4`/`.pd4` files anywhere in a full 3,190,909-file
live CASC storage sweep. Both wiki pages' "not supposed to be shipped to
the client" text is literally true. `wow.export` has zero PM4 handling and
only a raw-byte PD4 pass-through — real support here would be genuinely
novel. The "hidden by default" design question (glTF's `KHR_node_visibility`
vs. Blender's lack of support for it) is left open for a human decision.
