# WoW Forever (`wowf`, 1.60.1.70790) — survey findings

Current, correct facts only, checked against real bytes on the casc-tool
mount (`/media/luna/wow_export/forever`, product `wowf`) on 2026-11-09,
compared with `live` (12.1.5.70783). Diff source:
`casc-tool changes <store> live forever --cross-product`.

Forever is a modern client (same M2/WMO/ADT/DB2 container versions as
retail) with original-era content on top. Upstream WoWDBDefs tags every
forever-only DB2 below for retail **12.1.7** too, so forever is a leading
indicator for retail, not a fork husk can ignore.

## Diff summary

Per FileDataID, live → forever: 47,450 added, 729,808 removed, 188,558
changed, 1,008,636 unchanged. Of the added, 2,965 of 4,743 unnamed FDIDs
and almost every named one are readable (98,795 `.missing` rows, 96,866 of
them `unreadable:2`, i.e. not in the store).

Added, by type: BLP 22,208 · ADT 5,931 · skin 5,336 · WMO 3,728 · OGG 1,392
· M2 867 · anim 805 · **`.dat` 700** · lua 420 · WDT 195 · BLS 161 · bone
159 · WDL/`.tex` 39 · DB2 29 · pvdata 8 · phys 6. Content: many new
numeric maps (3104 "nothing to see here" 1,200 ADTs; 3065 "The Hall of
Thanes"; 3021 "Eastern Kingdoms Preserved"; 2991 "Zephras Isle"), 165 new
azeroth ADT files, new character face/skin textures and `.bone` sets.

## Nothing breaks

- Every forever-only M2 (856) passes `husk info`; all chunk tags already
  known (`MD21 SFID LDV1 TXID TXAC PGD1 EXP2 DBOC AFID TEXL DETL EDGF PFDC
  BFID`).
- 250 sampled forever-only WMOs: no tag husk lacks a parser for except
  `MOBS` (below, not forever-specific). `export-wmo` succeeds.
- `export-terrain` on forever `azeroth_32_49` succeeds with forever's own
  DB2s. ADT root/tex0/obj0 top-level and `MCNK` sub-chunk tags match live
  (forever has `MCCV` on all 256 chunks, live on 44; no `MFBO`).

## New: liquid flow maps (`MAI2` → `world/liquidflow/`)

- `MAI2` is `{u32 flowmap_fdid; u32 zero[7];}[4096]`, indexed
  `tileY * 64 + tileX` like `MAIN`. Field 0 is the FileDataID of
  `world/liquidflow/<map>/<map>_<x>_<y>.blp` (azeroth 32_49 → 7792205).
  Fields 1–7 are zero in every entry of every `MAI2` checked.
- 166 flow maps: azeroth 87, kalimdor 55, 2991 12, 3001 6, pvpzone04 4,
  3039 2; the count per map equals its nonzero `MAI2` entries.
- BLP2, uncompressed BGRA, 8-bit alpha, 256×256 per tile. RG is a signed
  2D flow vector (128,128 = still water), B = 0. Rivers carry flow.
- **Dangling in live**: live `kalimdor.wdt` already has `MAI2` with 55
  entries, but those FDIDs (e.g. 7895257) are absent from the live build.
  A reader must treat an `MAI2` FDID as optional.

## New: `_lod.adt` water as `MALP` + `MASD`

All 1,856 forever `_lod.adt` on azeroth, kalimdor, 2991, 3021, against
each root's `MH2O`/`MCVT`. `MLLN`/`MLLI`/`MLLV` (the old liquid LOD mesh)
remain in only 8 of 400 checked. `MASD` + `MLLD` appear exactly when the
tile has liquid (1,409 tiles; 447 dry tiles have neither); `MALP` only
when that liquid includes PBR water (1,337).

`MALP`: 32-byte records, rectangles covering a tile's PBR-water bodies
(only `PBRWater - …` LiquidTypes get records; Ocean 2, Slow Water 5,
Magma 7, Tar Pits 1284 never do).

| Off | Type | Meaning | Confidence |
|---|---|---|---|
| 0 | u8 ×4 | x0, y0, x1, y1 on the tile's 128×128 cell grid (cell = chunk×8 + MH2O offset), x1/y1 exclusive | verified |
| 4 | u16 | LiquidType (= the MH2O instance's) | verified |
| 6 | u16 | LiquidObject (= MH2O `liquidObjectOrLvf`) | verified |
| 8 | u8 ×8 | 0/3–7; all 7 in 1,601 of 2,780 records; all 0 only on full-tile flat Ocean-1250 rects | unknown |
| 16 | f32 | min surface height | verified |
| 20 | f32 | max surface height | verified |
| 24 | u32 ×2 | 0 | verified |

Rects never overlap and cover 14,295,190 of 14,319,222 wet cells (the
rest belong to types that get no record), plus 1.67 M dry cells: a loose
decomposition. 1,409 equal a 4-connected wet component's bounding box;
the rest split one, by a rule not found. On azeroth_32_49 both records'
(type, object, min, max) equal the MH2O instance aggregates bit for bit.

`MASD`: 132×132 samples × 4 × f16; sample (i, j) is tile cell
(i−2, j−2), the 128-cell tile with a 2-cell border, rows in `MCNK` order.
Read together, a shoreline/depth field for shading water near the coast
(interpretation).

| Off | Type | Meaning | Confidence |
|---|---|---|---|
| 0 | f16 | signed distance to the water edge in cells (≈ yd / 4.167 + 0.5), positive in water | sign verified (99.87%), scale likely |
| 2 | f16 | direction to the nearest cell of the opposite state, `atan2(dx, dy)` radians | verified (90.9% within 0.35 rad) |
| 4 | f16 | water depth (surface − ground), clamped at 0 | verified (median −1.4%) |
| 6 | f16 | water surface height; on dry cells the nearest water's, flooded outward | wet verified (99.46%), dry likely |

Open: `MALP` bytes 8–15 and its split rule; where `MASD`'s border comes
from; the exact ground point behind depth.

## New: area `.dat` tiles (`world/maps/<id>/area_<x>_<y>.dat`)

All 725 files checked. Not forever-specific: live maps 2641 (1 tile) and
2665 (24) have them in every retail build 12.0.7–12.1.5; map 2869 (700)
is forever-only. Each map exists only as these files: no WDT, ADT,
minimap, map texture or flow map in any build on the mount, and no
readable `Map.db2` row. **2665 is Elwynn Forest exactly**: its tiles sit
at azeroth's coordinates and `area_32_48` heights = `azeroth_32_48.adt`
× 36.0000 (RMS 0.00 over 16,641 vertices). 2869 fits no existing map
tested: an island of Tirisfal/Silverpine/Duskwood/Westfall assets; 670
of its 700 tiles are flat placeholders (zero heights, empty chunks).

A self-contained terrain tile in **inches** (×36 from yards), reversed
chunk tags, `MVER` = 26. Fixed prefix, then variable chunks, then `ACVT`:

| Chunk | Size | Content | Confidence |
|---|---|---|---|
| `AHDR` | 64 | u32 26, 129, 129, 16, 16, 0x00801E5F, rest 0; identical in all 725 | values verified, meaning likely |
| `ALOC` | 20 | u32 map ID, tile x, tile y, then x, y again | verified |
| `AOCH` | 2048 | 256 × 8 bytes; zero in all 725 | unknown |
| `AVTX` | 132,100 | f32 heights, inches: 129² outer block, then 128² inner | verified (seams match exactly) |
| `ANRM` | 99,075 | int8[3] per vertex, same order, Y up | verified |
| `ATEX` ×n | var. | splat texture path, no NUL | verified |
| `ADOO` ×n | var. | model path (M2s and WMOs), no NUL; a palette shared across tiles | verified |
| `ACNK` ×256 | 64 + subs | header (chunk x, y; 0xD000; words 5–8 likely a 2-bit dominant-layer map), then sub-chunks | layout verified |
| ↳ `ALYR` | 4136 | u32 `ATEX` index, u32 0x100, 24 zero bytes, `AMAP` 64×64 u8 alpha (layer 0 too) | verified |
| ↳ `ACDO` | 56 | u32 `ADOO` index, f32 pos[3] (chunk-centre-relative x/z, y an offset from the ground), f32 rot[3] degrees, f32 scale, f32 1.0, f32 ?, i32 negative id, i32 0, u32 flags | layout verified, frame likely |
| `AGRP` | 176 | a root {id, pos, rot (radians), scale, child count} and children {parent id, pos relative to parent, rot, scale}; no model | layout verified, purpose unknown |
| `AFBO` | 72 | f32[9] max 54,000, f32[9] min −18,000: flight bounds like `MFBO` | likely |
| `ADST` | 12 | u32 id, u32 FileDataID (M2s and a WMO), u32 1; forever only, 7 tiles | FileDataID verified |
| `ACVT` | 132,100 | RGBA8 per vertex, 7F7F7FFF neutral | verified |

Open: `ACNK` header words 2, 4, 9–10, 12, 14; `AOCH`; what `ACDO`'s y
offset is relative to; `AGRP`'s role; `ADST`'s ids; what reads it.

## New DB2 tables

Definitions from WoWDBDefs `fb58b5b8e` (2026-11-07; `reference/WoWDBDefs`
is at that commit since 2026-11-09).

| Table | Rows | What it is |
|---|---|---|
| `FFTTile` | 23 | FFT ocean-wave spectrum: u32 (16–512, likely grid size), 9 floats. PBR water types link 3 each (cascades, likely) |
| `LiquidTypeXFFTTile` | 65 | LiquidType → FFTTile |
| `LightDataGlobalVolumeFog` | 11,488 | global volumetric fog, up to 3 slots per `LightData` row (below) |
| `ModelProperties` | 1,495 | model FileDataID → 2 floats (1.0–2.2, 0.75–1.0; meaning unknown) |
| `ChrModelAltVariant` | 18 | ChrModel 1–18 → 257–278: the original-era bodies (DisplayID 49/50/…, texture layout 203) |
| `ChrModelAltVariantOption`/`Choice` | 93 / 1,136 | customization option/choice mapping between the two |

Shaders back these: new BLS `volumefogglobal`, `volumefogportal`,
`volumefogp-water`, `particulatec-shadowrt` (161 new BLS, 108 readable).

`LightDataGlobalVolumeFog` (field N = DBD `Field_1_60_1_69876_0NN`).
Every row joins a forever LightData row, so values are keyed by
LightParams and time of day. The file stores 027 and 028 as 2-element
arrays; the DBD declares scalars (a DBD error).

| Field | Type | Meaning | Confidence |
|---|---|---|---|
| LightDataID | int | owning LightData row | verified |
| 001–004 | int | packed 24-bit colours, changing through the day; 004 non-zero in 226 rows; none reliably equals a LightData colour | colour verified, roles unknown |
| 005 | f32 | −40..5000, often 2000: a distance | likely |
| 006, 008 | f32 | 0..80 / 0..20 | unknown |
| 007, 026 | f32 | ≈ 1: multipliers | unknown |
| 010, 012 | f32 | signed, −2000..1600: heights | likely |
| 009, 011, 013 | f32 | mostly 0 | unknown |
| 014 | f32 | 0..50, varies with time of day | unknown |
| 015 | f32 | 0..1, mode 0.9: anisotropy or albedo | unknown |
| 016–018, 019–021 | f32 ×3 ×2 | two direction vectors, set together | likely |
| 027[2], 028[2] | f32 ×2 ×2 | a scalar pair per vector, set in the same rows: two noise layers, each direction + speed/scale | likely |
| 022 | int | 0..15: 4-bit flags | likely |
| 023 | int | slot 0/1/2, unique per LightData row (3,574 of 4,000 rows have all three); slot 1 far-reaching (005 mean 1,840), slot 2 rarely uses 011/012 | verified |
| 024 | f32 | only 0 or 1: an enable flag | likely |
| 025 | f32 | 0..150; slot 2 mean 17.8, slots 0/1 0.6/1.5 | unknown |

## Pre-existing gaps found on the way (in live too)

- WMO group `MOBS`: present in every group sampled, live and forever;
  husk never mentions it.
- `environments/particulatevolumes/pvdata/*.pvdata` (85 in forever, 76
  unchanged from live): not chunked, starts `u16 4, u16 3`, then a
  serialized property blob. husk reads `_mpv.wdt` but not these.
  Forever's `azeroth_mpv.wdt` is `MVER` only.
