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
- **Luna confirms the reading (2026-11-24):** the long skirt geosets are
  almost always robes, and a robe hides the thighs, so thigh and skirt are
  never visible together. Group-13 body skirts are robe geometry, not hems.
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
- 1303 is shown by nothing in the 12.1.5 DB2 on a playable body (`c8.sql`
  to `c12.sql`):
  - Items: one ItemDisplayInfo has `GeosetGroup[2]` = 2 (→ 1303), display
    153974, reached by no ItemAppearance, Item or
    NPCModelItemSlotDisplayInfo row.
  - Customization: the 6 ChrCustomizationElement rows on GeosetType 13
    belong to ChrModel 89 (dracthyr dragon form, layout 155, option
    "Thighs": None/Chains/Ornate/Sheath/Scaleborn/Tassets); 2 more
    ChrCustomizationGeoset rows (2103, 2105) are referenced by no element.
  - ChrCustItemGeoModify 28 (GeosetType 13, 1 → 0) is referenced by no
    element.
  - CreatureDisplayInfoGeosetData index 12 (group 13) values 1-4 (458
    rows) are all on creature models (gnoll2 and the like), none on a
    character body.
  So 1303 on the body files is unused data as far as the tables go
  (**data**; a hardcoded client rule can't be excluded).
- **1302 vs 1303, structure** (`husk uv-footprint --geoset 1302 --geoset
  1303`): same model-space box on every body checked (humanmale_hd
  z -0.005..1.145, x -0.421..0.210; humanfemale_hd z -0.004..1.153;
  orcmale_hd z 0.033..1.327), so the same hem line and flare; 1303 has more
  triangles (humanmale_hd 616 vs 440, humanfemale_hd 818 vs 756, orcmale_hd
  774 vs 698). Footprints: 1302 ⊂ 1303 on humanmale_hd (1303 adds Foot
  pixels), 1303 ⊂ 1302 on humanfemale_hd, identical on orcmale_hd. A
  denser variant of the same robe, not a different length; what the extra
  triangles shape (slits, folds) needs a look, not a box.

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

## The body's own hem: geoset 1102 — measured on humanfemale_hd

Group 13 holds no hem, but group 11 does (Luna found it in Blender on an
export of transmog set 3540, Peculiar Peddler's Breeches, display 676797):

- **1102 is a short hem flare under the belt**: 148 triangles, z 0.947 to
  1.153 on humanfemale_hd, UV rows 448–508 (the top 60 rows of LegUpper),
  `husk uv-footprint --layout-id 104 --geoset 1102`. Masks:
  `/media/luna/work/cache/husk_garments/png/humanfemale_hem/`.
- **The legs item switches it on**, not the chest: legs
  `GeosetGroup[0]` = 1 → 1102, on 89 legs displays (value 0, none: 3,445;
  3 → 1104: 620; 4 → 1105: 199). So the hem mesh ships with the trousers
  and the hem's paint is the top band of the legs item's LegUpper texture;
  in set 3540 the tunic paints no leg section at all.
- **Its pixels are also the thigh mesh's top band**: 1301's footprint
  (rows 448–703) contains all of 1102's (overlap 30,120 px of 30,120). The
  thigh vertices on those rows sit at the waist, under the hem's flare, so
  the repeat never shows while the hem is worn (Luna saw none).
- 1104 (z 0.605–1.153) and 1105 resample the thigh footprint (1104 ∩ 1301 =
  all of 1301): knee-length shapes, not a hem; not yet looked at.

## What this means for `REFACTOR/GARMENTS.md` stage 2

- Its premise holds, with group 11 instead of 13: the hem is a body mesh
  (1102) whose UVs sample a fixed band of the composite, rows 448–508.
  That footprint cuts the hem out of a legs texture exactly.
- The footprints are not disjoint: the thigh mesh's top band reads the same
  rows. In practice it doesn't matter (Luna, Blender, set 3540): with 1102
  hidden the trousers still look right, and with the thighs textured
  differently the tunic and hem still look right. So the leggings keep
  their whole texture; nothing is masked out or painted in.
- The group-13 meshes are robe skirts (1302, 1303) and a thigh variant
  (1301), never hems (Luna confirms robes hide the thighs).
- Modern item-model hems (next section) are a second, separate case.

## Item models (collection models) — data, slot rule ref

Every chest and legs display with an own model (`ModelResourcesID[0]`):
1,553 chest, 1,447 legs, through 388 distinct ModelResources. One file per
ModelResources read (the human male file when there is one, else the lowest
FileDataID; `c13.sql`, `models_all.nu`), each geoset's triangles, model-space
z range and the texture its batches sample (`husk uv-footprint <item.m2>`
without `--layout-id`, which now reports `tex=<M2Texture type>` per geoset).
Tables `model_geosets`, `shown`, `disp_hem` in the scratch db (`c14.sql`,
`c15.sql`). A seeded sample of 50 displays (30 chest without skirt or leg
overlay, 20 legs; `c6.sql`, `items.nu`) with every geoset and its own
texture: `item_sample.tsv`, `item_geosets.tsv`.

**Which geosets a display shows** (**ref**, wow.export
`tab_characters.js` `SLOT_TO_GEOSET_GROUPS` + `M2RendererGL.js`
`setGeosetGroupDisplay`): hide every submesh, then per slot group show
`group × 100 + 1 + AttachmentGeosetGroup[index]`. Chest: 8, 10, 13, 22, 28
from indices 0-4; legs: 11, 9, 13 from 0-2. Not verified against the
client; the facts below that depend on it say "shown".

- **An item model is a whole outfit, one model per set, split by slot.**
  Its geosets carry body group ids: 4 (285 models), 5 (234), 11 (241), 13
  (83), 18 (102), 20 (172), 22 (242), 23 (161), 28 (41), a few 8/9/10/15/27.
  The chest and legs (and gloves, belt, boots) displays of one set name the
  same ModelResources and often the same texture: the Arathi set's chest
  697583 "Arathi Cleric's Chainmail" and legs 697588 "Arathi Cleric's
  Greaves" both use `mail_outdoorarathor_d_01_robe_hu_m.m2` (FDID 5645802)
  with texture 6390585; the chest shows 2201, the legs show 1101.
- **Item meshes never sample the body's composite skin.** Of 1,687 geosets
  in the 388 models, none has a batch on M2Texture type 1. All sample type 2
  (the display's own texture, ItemDisplayInfoModelMatRes TextureType 2),
  3, 4 or 24 (further item texture slots), or 0 (a file the model names).
  An item-model hem is its own geometry on its own texture sheet:
  separable from the body by construction.
- AttachmentGeosetGroup is 0 on 1,506 of 1,553 chest and 1,436 of 1,447
  legs displays, so nearly every display shows the X01 variant of each of
  its slot's groups.

### Chest displays with an own model — by what they show of group 13

Human-male scale: knee about z 0.5, hips about 1.0 (humanmale_hd's knee
group 9 is z 0.36..0.73). "Short" = the shown group-13 mesh ends above
z 0.5, "long" = below.

| Body skirt (GeosetGroup[2]) | LegUpper overlay | Item-model group 13 shown | Displays |
|---|---|---|---|
| no | no | none | 1,002 |
| no | no | short | 67 |
| no | no | long | 27 |
| no | yes | none | 6 |
| yes | yes | none | 110 |
| yes | yes | short | 289 |
| yes | yes | long | 52 |

By expansion, the 94 chest displays with an item-model group-13 mesh and
neither a body skirt nor a leg overlay: Classic-WoD 1, Legion 2, BfA 11,
SL 28, DF 13, TWW 12, Midnight 6, no regular expansion 21 (`-3` 18, blank
3). All chest displays showing a group-13 item mesh: Legion 64 of 197, BfA
46/109, SL 74/197, DF 58/278, TWW 66/309, Midnight 68/201.

- **A modern tunic hem is a group-13 submesh of the item model, switched
  by the chest slot's group-13 rule.** Examples (no body skirt, no leg
  overlay, short):
  - 187679 "High Torturer's Smock" (SL), `collections_cloth_revendrethraid_d_01_hu_m.m2`
    FDID 3567196, 1301 z 0.527..1.112, texture 3582606;
  - 180851 "Last Vestige of Neltharion" (BfA), FDID 2916655, 1301
    z 0.718..1.253;
  - 192700 "Anduin's Princely Vestments" (SL), FDID 3784453, 1301
    z 0.690..1.661;
  - 666106 "Heartfire Sentinel's Brigandine" (DF), FDID 4692044, 1301
    z 0.659..1.174.
  Long, same kind: 671167 "Cassock of Lunar Communion" (DF),
  `robe_cloth_raidpriestemerald_d_01_hu_m.m2` FDID 4994836, 1301
  z 0.382..1.241; 686911 "Void's Judgment Breastplate" (Midnight), FDID
  6032552, 1301 z 0.159..1.225.
- **But most chest models stop at the waist.** 1,002 chest displays with
  an own model show no group-13 mesh and set no skirt or leg overlay: they
  show 2201/2801/801 (torso, shoulder pieces, sleeves) only. Examples:
  160009 "Gravewarden Chestplate" (Legion, FDID 1549574, shows 2201),
  192847 "Cosmic Gladiator's Leather Vest" (SL, FDID 3864853), 697583
  "Arathi Cleric's Chainmail" (TWW): its model's long 1101 skirt
  (z 0.159..1.176) is hidden by the chest rule and shown by the legs
  display of the same set.
- **Robes add an item piece on top of the body skirt.** 341 chest
  displays set the body skirt 1302 and a leg overlay and also show an
  item-model group-13 mesh, mostly short (289): a tabard-like flap or
  outer layer over the robe. Examples: 154642 "Robe of Everburning
  Knowledge" (Legion, FDID 1356745, 1301 z 0.529..0.888), 699968 "Dying
  Star's Cassock" (TWW, FDID 6121911, 1301 z 0.920..1.302).

### Legs displays with an own model — the hem shipped with the trousers

- **291 legs displays show an item-model group-13 mesh**, 289 of them the
  same mesh of the same model as a chest display of the set: the client
  rule shows 1301 for either slot, so the hem appears with the chest or
  with the legs alone. By expansion: Classic-WoD 4, Legion 16, BfA 28, SL
  52, DF 51, TWW 58, Midnight 48, no regular expansion 34. Short (human
  male files) 219, long 72.
  - 699934 "Dying Star's Leggings" (TWW), `cloth_raidpriestethereal_d_01_hu_m.m2`
    FDID 6121911, 1301 z 0.920..1.302 (the same mesh as chest 699968);
  - 671222 "Elder's Volcanic Wrap" (DF), FDID 4994836, 1301 z 0.382..1.241
    (the same mesh as chest 671167);
  - 192936 "Leggings of the Fixed Stars" (SL), FDID 4035557, 1301
    z 0.476..2.013;
  - 690242 "Breeches of Reclaiming Blight" (TWW), FDID 5652950, 1301
    z 0.443..1.153;
  - 178974 "Seawrath Legwraps" (BfA), FDID 2905330: 1101 and 1301 are the
    same triangles (812, identical bounds), so the hem shows by either rule.
- **Group 11 on item models is the legs item's lower garment**: 1,172
  legs displays show an item 11xx mesh; 429 reach below the knee (z < 0.5,
  human male files; Legion 20, BfA 17, SL 24, DF 58, TWW 148, Midnight 46,
  Classic-WoD 22, no regular expansion 94). That covers long robe skirts
  sold as trousers (697588 "Arathi Cleric's Greaves", 1101 z 0.159..1.176,
  x -0.446..0.258) and greaves/shin plates alike; z alone does not tell
  them apart.
- **The legs texture still paints the body.** All 291 legs displays with
  an item-model hem have a LegUpper overlay (35 also set the body skirt),
  and 4,488 of 4,504 legs displays have one: the thighs under a hem carry
  the legs item's composite paint, the hem mesh carries its own texture.

PNGs (the shown hem mesh's footprint on its own item texture; outside the
footprint washed magenta; masks white on black):
`/media/luna/work/cache/husk_garments/png/chest_models/<case>_<display>/`,
cases `a_chest_tunic_hem_short`, `b_chest_hem_long`,
`c_chest_robe_plus_item_g13`, `d_legs_hem`, `f_legs_long_g11` (Arathi
legs 1101); every geoset of the 50 sampled models on its texture in
`png/chest_models/sample_all_geosets/`.

## What the evidence supports for the hem split

Facts it rests on: the body has no hem (group-13 body geosets are robe
skirts, which hide the thighs; Luna confirms); a modern hem is a group-13
(sometimes 11) submesh of a set's item model on its own texture; one item
model serves the set's chest and legs, and the slot rule shows group 13 for
either. Options for Luna:

1. **Hem = item-model geometry, owned by whichever garment shows it.** A
   garment carries its item-model submeshes as pieces (`hem` = its shown
   group-13 piece, `skirt` = a long one), each with its own texture. No
   composite cut, no footprint: the split is clean because the data is
   already split. Conflict: chest and legs of one set both show the same
   1301; the bundle needs one owner per piece (dedupe by model + geoset)
   or a rule such as "the chest owns group 13 when worn".
2. **As 1, plus a slot-level `hem_from` choice.** Since the same mesh is
   reachable from both slots, let a selection say which garment provides
   the hem (chest tunic hem over plain trousers, or trousers' hem under a
   short top). The data supports both pairings: 94 chest displays show a
   hem without any body skirt, 291 legs displays show one.
3. **Legacy texture-painted hems stay painted.** Pre-Legion legs/chest
   textures painted on LegUpper are not separable (footprint = thigh area,
   200/200 sampled textures non-empty there); they stay part of the legs
   or robe paint. Only item-model hems (Legion on, 385 displays: 94 chest +
   291 legs) are split. Stage 2's composite-mask mechanism is dropped.
4. **Body skirts (1302/1303) are robe geometry**, chosen by a robe's
   `GeosetGroup[2]` (chest 1,490, legs 246), never a hem; a robe hides the
   thighs, so no thigh/skirt conflict to resolve.

Open before any of these ships: the slot rule above is wow.export's (**ref**),
unverified against the client; what the 1,002 waist-length chest models pair
with in practice (which legs model gives them a lower edge) is not counted.
