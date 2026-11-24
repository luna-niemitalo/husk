# Findings — garments: body geosets, composite regions, slot geoset groups

Evidence for `REFACTOR/GARMENTS.md` ("Evidence still needed"). Build
12.1.5.70961 (`/media/luna/wow_export/live`), DB2 exported with `husk
db2-export --dir` (copy rows read), listfile 2026-11-21. Footprints from
`husk uv-footprint` (`src/garment_footprint.{hpp,cpp}`,
`src/cmd_uv_footprint.cpp`): each geoset's triangles rasterized in UV set 0
onto the layout's pixel grid, a pixel counted when its centre lies inside
or on a triangle edge. Scripts and SQL: `/media/luna/work/cache/husk_garments/`
(`bodies.sql`, `q2.sql`, `q5.sql`, `q7.sql`, `q8.sql`, `bodies.nu`,
`sample.nu`); masks: `/media/luna/work/cache/husk_garments/png/`.

---

## Region rectangles (`CharComponentTextureSections`) — data

Every playable body layout (ChrModel 1-22, 43-48, 53-74, 127-128, 195-196,
200-201) is 2048x1024 and has the same rectangles for SectionType 0-7. The
layouts differ only in the face/scalp/extra sections (8-14).

| SectionType | Region | X, Y, W, H (2048x1024) |
|---|---|---|
| 0 | ArmUpper | 0, 0, 512, 256 |
| 1 | ArmLower | 0, 256, 512, 256 |
| 2 | Hand | 0, 512, 512, 128 |
| 3 | TorsoUpper | 512, 0, 512, 256 |
| 4 | TorsoLower | 512, 256, 512, 128 |
| 5 | LegUpper | 512, 384, 512, 256 |
| 6 | LegLower | 512, 640, 512, 256 |
| 7 | Foot | 512, 896, 512, 128 |
| 8 | (Pandaren 129-130 only) | 1536, 768, 512, 256 |
| 9, 10 | ScalpUpper/ScalpLower (face) | 1024, 0, 1024, 1024 |
| 11-14 | whole-sheet extras, per layout | 0, 0, 1024 or 2048, 1024 |

The region names for 0-7 are the community names (**ref**); the footprints
below agree with them (thigh meshes land in 5, boot shafts in 6, and so on).
The non-playable layouts: 1/151/152/154 (1024x1024) and 153 (512x512) keep
the same 0-7 arrangement at their scale; 157/161/166/173/179/180 too at
1024 wide. Full table: `q8.sql`.

## Leg-region footprints on the body models — verified on 3 bodies

humanmale_hd (layout 103, skin FDID 1012983), humanfemale_hd (104, 1000778),
orcmale_hd (105, 917168), dracthyrfemale (158). Pixel rows of the
2048x1024 composite, all within x 516..1018:

| Rows | Section | Sampled by |
|---|---|---|
| 384..448 | top of LegUpper | belt group 18 (1801, 1803, 1804; 1802 reaches up into TorsoLower) |
| 448..704 | LegUpper (rest) + top of LegLower | thighs: 1104, 1105, 1301 (identical footprint, 128,512 px); 1102 only rows 448..508 |
| 640..750 | LegLower | knee group 9 (902, 903, 905) |
| 704..896 | LegLower | boot shafts group 5 (501-510) |
| 448..896 (+ Foot) | LegUpper + LegLower | skirts 1302, 1303 |

- **The skirt meshes are not a separate hem area.** 1302 and 1303 sample
  the whole of LegUpper below the belt rows and all of LegLower; their
  footprint contains the thigh footprint entirely (humanmale_hd: 1104 ∩
  1302 = 128,512 = all of 1104; same on humanfemale_hd, orcmale_hd,
  dracthyrfemale). The thigh and the skirt are alternative variants that
  read the same pixels: what a legs item paints on the thighs is what a
  robe skirt shows.
- **Hem variants are not identical but nested:** 1302 ⊂ 1303 on
  humanmale_hd (223,390 vs 248,907 px; 1303 adds 23,763 px of Foot);
  dracthyrfemale and humanfemale_hd the other way (1302 reaches 4,016 px into
  Foot, 1303 not); orcmale_hd identical (224,896 each).
- Model-space bounds agree: 1104/1105/1301 span the thighs (z 0.59..1.15
  on humanmale_hd), 1302/1303 reach the floor (z -0.005), group 9 the
  knees (z 0.36..0.73), group 5 the calves (z 0.07..0.63).
- Masks (white = sampled), and `*_overview.png` (red group 13, green group
  9/11, white section outlines):
  `/media/luna/work/cache/husk_garments/png/humanmale_hd_1104_1303/`,
  `png/humanfemale_hd/`, `png/orcmale_hd/`, `png/humanmale_legacy/`; every
  geoset of humanmale_hd in `png/humanmale_*.png`.

### Which bodies have which group-13 variants — data

`bodies.nu` over every playable body model and five pre-HD files:

- 1301, 1302, 1303: human, orc, dwarf, night elf, scourge, gnome, goblin,
  blood elf, nightborne, void elf, kul tiran, dark iron, dracthyr, earthen,
  haranir (HD remakes and post-Legion races alike).
- 1301, 1302 only: tauren, troll, draenei, worgen, pandaren, highmountain,
  lightforged, zandalari, vulpera; and the pre-HD `humanmale.m2`,
  `humanfemale.m2`, `nightelfmale.m2`, `bloodelffemale.m2`.
- 1302 only: mechagnome.
- No body model has a short hem mesh: no group-13 (or other) geoset sits
  between the waist and the knee apart from the thigh variants. Groups
  present on dracthyr and earthen (DF and TWW bodies) are the same as on
  humanmale_hd (WoD-era remake), apart from one extra belt variant (1805).
- 1303 is never selected by an item: no ItemDisplayInfo of any slot has a
  GeosetGroup value that maps to 1303 (below); ChrCustomizationGeoset has 8
  rows with GeosetType 13 (GeosetID 0-5). What shows 1303 is open.

## Slot → GeosetGroup table — verified structurally, 3 races + legacy

Rule (**ref**, WMV `WoWItem.cpp` via wow.export `DBItemGeosets.js`):
geoset id = group × 100 + 1 + value. Check: the distinct non-zero values
of each index, over every ItemDisplayInfo reachable from an Item of that
InventoryType (12.1.5), against the geoset ids that exist on humanmale_hd,
earthendwarfmale and dracthyrfemale (identical id sets for these groups),
plus model-space placement. "Verified" = every value lands on an existing
geoset of the claimed group and the value range matches the group's variant
count; nothing was rendered (structural, not visual).

| Slot (InventoryType) | Index → group | Values (displays) | Status |
|---|---|---|---|
| Head (1) | 0 → 27 helm, 1 → 21 skull | 0: 1 (2); 1: -1 (17) | unverifiable on the body (no group 21/27 geosets on any playable body) |
| Shoulder (3) | 0 → 26 | none | unverifiable (no values) |
| Shirt (4) | 0 → 8 sleeves | 1 (9), 2 (20) → 802, 803 | verified |
| Shirt (4) | 1 → 10 chest | 3 (1) → 1004 | contradicted for its one display (no 1004; bodies have 1002 only) |
| Chest (5) / Robe (20) | 0 → 8 sleeves | 1 (704), 2 (163), 3 (1) | verified for 1-2; value 3 (1 display) → 804 does not exist |
| Chest/Robe | 1 → 10 chest | 1 (5) → 1002 | verified (1002 sits on the hips, z 0.98..1.15) |
| Chest/Robe | 2 → 13 trousers/skirt | 1 (1540) → 1302 | verified |
| Chest/Robe | 3 → 22 torso | 1 (1757) → 2202 | verified |
| Chest/Robe | 4 → 28 arm upper | -1 (5) | unverifiable (no group 28 on the bodies) |
| Waist (6) | 0 → 18 belt | 1 (287), 2 (55) → 1802, 1803 | verified |
| Legs (7) | 0 → 11 pants | 1 (89), 3 (664), 4 (199) → 1102, 1104, 1105 | verified: exactly the three variants that exist (no 1103) |
| Legs (7) | 1 → 9 kneepads | 1-4 → 902-905 | verified for 1, 2, 4; value 3 (6 displays) → 904 exists only on pandaren |
| Legs (7) | 2 → 13 trousers/skirt | 1 (246) → 1302 | verified |
| Feet (8) | 0 → 5 boots | 1-9 → 502-510 | verified: exactly 501-510 |
| Feet (8) | 1 → 20 feet | 1-6 → 2002-2007 (and -1 on 429) | verified (2001-2008 exist); the "0 → 2" special case not checked |
| Wrist (9) | none | none | consistent |
| Hands (10) | 0 → 4 gloves | 1-3 → 402-404 | verified: exactly 401-404 |
| Hands (10) | 1 → 23 hand attachment | -1 (5) | unverifiable |
| Back (16) | 0 → 15 cloak | 1-9 → 1502-1510 | verified: exactly 1501-1510 |
| Tabard (19) | 0 → 12 tabard | 1 (320) → 1202 | verified (1202-1204 exist) |

The legacy `humanmale.m2` lacks 905, 1105 and 1303, so legs value 4 and
kneepad value 4 have no mesh there. `ItemDisplayInfo.GeosetGroupOverride`
is 1 on head (655), shoulder (689) and waist (175) displays and a handful
of others; not part of this table, meaning unknown. -1 values (head skull,
chest arm upper, feet 20, hands 23) are not covered by the +1 rule.

## Corpus counts — data

Distinct ItemDisplayInfo per slot reachable from an Item (Item →
ItemModifiedAppearance → ItemAppearance), split by the lowest
`ItemSparse.ExpansionID` of the items that use it (0 Classic … 9
Dragonflight, 10 The War Within, 11 Midnight; -3 and blank: no regular
expansion / no ItemSparse row). Columns: LegUpper / LegLower =
ItemDisplayInfoMaterialRes rows with ComponentSection 5 / 6; skirt =
GeosetGroup[2] ≠ 0; model = ModelResourcesID[0] ≠ 0 (an item model of its
own, i.e. a collection model). `q5.sql`, `q7.sql`.

| Slot | Exp | Displays | LegUpper | LegLower | Skirt | Own model |
|---|---|---|---|---|---|---|
| chest (5, 20) | 0-5 (Classic-WoD) | 2,412 | 791 | 788 | 779 | 70 |
| chest | 6 Legion | 483 | 165 | 164 | 164 | 197 |
| chest | 7 BfA | 270 | 83 | 83 | 83 | 109 |
| chest | 8 SL | 299 | 79 | 79 | 79 | 197 |
| chest | 9 DF | 375 | 71 | 71 | 71 | 278 |
| chest | 10 TWW | 451 | 96 | 93 | 93 | 309 |
| chest | 11 Midnight | 284 | 95 | 95 | 95 | 201 |
| chest | all (incl. -3, blank) | 5,339 | 1,520 | 1,511 | 1,490 | 1,553 |
| legs (7) | 0-5 | 1,925 | 1,921 | 1,894 | 146 | 45 |
| legs | 6 Legion | 443 | 443 | 442 | 15 | 151 |
| legs | 7 BfA | 242 | 242 | 242 | 2 | 94 |
| legs | 8 SL | 275 | 275 | 275 | 6 | 153 |
| legs | 9 DF | 345 | 345 | 345 | 13 | 239 |
| legs | 10 TWW | 414 | 406 | 406 | 14 | 301 |
| legs | 11 Midnight | 276 | 276 | 258 | 2 | 185 |
| legs | all | 4,504 | 4,488 | 4,381 | 246 | 1,447 |

- **Chest leg overlays are robes.** Of 1,520 chest displays with a LegUpper
  overlay, only 30 have no skirt geoset (10 Classic, 11 at -3, 3 TWW, the
  rest scattered); no chest display has a skirt without a leg overlay. The
  stage-1 row "Chest with LegUpper overlay, no skirt geoset → tunic" covers
  30 displays.
- **Legs with a skirt geoset:** 246 of 4,504 (5%), mostly pre-Legion (146).
- **Item models took over from Legion on, not from Dragonflight:** chest
  displays with an own model go from 3% (Classic-WoD) to 41% (Legion), 40%
  (BfA), 66% (SL), 74% (DF), 69% (TWW). 1,096 chest displays have a model
  and neither a skirt nor a LegUpper overlay. Whether those models carry
  hem geometry (a tunic skirt modelled on the item, skinned to the body) is
  not read here: it is the item model's mesh, not the body's.

### Legs textures inside the skirt/thigh footprint — sampled

Sampled: 200 legs displays with a LegUpper overlay (seeded order
`idi × 2654435761 mod 2^32`, `q6.sql`; one LegUpper texture each, the
lowest FileDataID of its material resource), measured on humanmale_hd's
1104 (thigh) and 1302 (skirt) footprints inside LegUpper (`sample.nu`,
results `sample_result.csv`).

- 200 of 200 have non-transparent pixels inside both footprints; 174 are
  fully opaque there, 26 partly transparent (16 of those Classic).
- By expansion the sample holds 40 Classic, 6 TBC, 10 WotLK, 6 Cata, 11
  MoP, 11 WoD, 16 Legion, 14 BfA, 14 SL, 15 DF, 20 TWW, 11 Midnight, 26
  with no regular expansion; every group is 100% non-empty.
- So "non-empty pixels inside the hem footprint" picks every legs texture:
  the footprint is the thigh area itself and cannot tell a painted hem from
  trousers.

## What this means for `REFACTOR/GARMENTS.md` stage 2

- Its premise, "the hem is its own mesh whose UVs sample a fixed area of
  the composite", does not hold for any playable body model in 12.1.5. The
  only group-13 meshes are full-length skirts (1302, 1303) that resample
  the thigh and calf areas, plus 1301, which is a thigh variant.
- So the hem mesh's UV footprint cannot cut a hem out of a legs texture:
  it is all of LegUpper below the belt rows plus LegLower, the same pixels
  the thighs show. A hem painted by a legs item is visible on the thighs
  whatever is worn, which matches what the hem-in-trousers items look like.
- Disjointness (decision 4): the skirt and the thigh footprints coincide
  rather than being disjoint. They are never both visible in one place,
  since a skirt covers the thighs, so no texture repeats on screen; but
  "nothing needs to be painted in under a removed hem" has no footing,
  because there is no separate hem area to remove.
- The likeliest home of a modern tunic hem is the chest item's own model
  (1,096 chest displays with a model and no skirt geoset or leg overlay,
  growing from Legion on). Reading those models' geosets and UVs is the
  next evidence step.
