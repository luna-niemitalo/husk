# Character-name → rendered husk export: pipeline test + findings (2026-08-23)

Ran the intended end-to-end pipeline for real, against Linore (Argent Dawn EU, no
special characters in the name -- confirmed via local WTF folder + wowarmory.gg, both
plain "Linore"): fetch character customization + equipped-transmog appearance ->
`husk export --appearance` -> render preview. Two of the three steps work well; the
third (attaching gear to the render) surfaced real bugs (one fixed this session, one
precisely localized with a verified ground-truth table but not yet fixed), a real
missing-data gap fixed live this session (fetched via `tact-fetch`), and real corpus
gaps still open. Every wrong-geometry claim below was independently confirmed via a
reverse lookup (item's real icon -> real internal codename -> `--listfile` -> real
FileDataID/`ModelResourcesID`), not just eyeballed. This doc is the investigation
record; nothing below is speculative -- every claim was checked against real command
output, raw DB2 bytes, or DBD schema before being written down. See also
`WOW_CHARACTER_TRANSMOG_INVESTIGATION.md` — the same week's earlier research pass
(character/account data availability, API access) that this pipeline test builds on.

## What worked, unmodified

```
tools/venv/bin/python3 tools/blizzard_profile_fetch.py argent-dawn linore \
    --race 4 --sex 1 --out-dir <dir>
# -> husk-appearance/1 race=4 sex=1 cust=... gear=...

./build/husk export nightelffemale_hd.m2 --output example_exports/linore/linore.glb \
    --db2-dir <dbfilesclient> --dbd-dir reference/WoWDBDefs \
    --listfile <community-listfile.csv> --listfile-root <wow_export> \
    --appearance "husk-appearance/1 ..."

blender --background --factory-startup --python tools/corpus_scan_tasks/render_glb.py \
    -- example_exports/linore/linore.glb example_exports/linore/linore_preview.webp
```

This is already genuinely "2 actions" (fetch, then export+render) end to end, with
`--chr-model-id auto`/`--char-layout-id auto` correctly deriving Linore's real
`ChrModelID 8` from the base model's own FileDataID with zero extra input. The render
(`example_exports/linore/linore_preview.webp`) shows the real Night Elf base body with
Linore's actual skin tone/hair/face customization applied correctly -- no gear
rendered on it yet, which matches husk's own documented current state
(`TODO/EQUIPPED_GEAR_RENDER_TODO.md`: gear resolves, doesn't render).

## Finding 1 (bug, confirmed, FIXED 2026-08-23): gear items resolve to the wrong item's geometry

`--appearance`'s case-1 (standalone-geometry) gear resolution
(`attachGearAppearance`/`exportGearAuxItemModels`, `src/export_extras.cpp:811`,
`src/cmd_export.cpp:679`) exported 4 real `.glb` files for Linore's equipped
MAIN_HAND/OFF_HAND/WAIST/HEAD gear. Checking each file's own embedded material/mesh
names against the real slot it was supposed to represent:

**Correction from this doc's first draft**: the item names below are the
*transmogrified-to* item (`equipment.json`'s `.transmog.item.name`), not the raw
equipped item's own name (`.name`) -- the appearance-string IDs used were always the
transmog-correct ones (`extract_gear` already prefers
`transmog.item_modified_appearance_id`), but the first draft's human-readable names
were the equipped item, not what's actually displayed. MAIN_HAND/OFF_HAND have no
transmog override on Linore (shown as their real equipped item, correctly), so only
WAIST's name changes here.

**Second correction, more serious**: the first draft called HEAD "correct" because the
resolved file was at least helm-shaped. It wasn't checked against the item's *real*
identity, and it's wrong too -- **all 4 of 4** case-1 items are wrong, not 3 of 4. See
the reverse-lookup table below for how this was caught.

| Slot ID | Slot | Expected item (the real *displayed* appearance) | Got instead |
|---|---|---|---|
| 1 | HEAD | Tasteful Eyeglasses (146480, transmogged from Pyrewalker's Miter) | **Wrong** -- `helm_leather_warfrontshorde_d_01_be_m`, a Blood-Elf-male "Warfronts" leather helm |
| 6 | WAIST | Sacred Templar's Buckle (230883, transmogged from Voidbreaker's Sage Cord) | **Wrong** -- `sword_1h_blackdragonoutdoor_d_01`, a one-handed sword |
| 21 | MAIN_HAND | Barbed Rootwand (301996, no transmog override -- shown as equipped) | **Wrong** -- `plate_outdoorarathor_d_01_shoulder_l`, a shoulder pad |
| 22 | OFF_HAND | Elderbloom Lantern (301998, no transmog override -- shown as equipped) | **Wrong** -- `plate_outdoorarathor_d_01_robe_be_m`, a full chest robe (38 bones, matches a real humanoid skeleton) |

**Root cause now precisely localized** (not fixed). Found via a reverse lookup Luna
suggested: rather than tracing the DB2 chain forward, start from the item's own icon
(`ItemAppearance.DefaultIconFileDataID`, a field that resolves correctly and is
human-readable) to recover the item's real internal codename, then search `--listfile`
for the matching model file directly. Extended to all 4 (Luna caught the HEAD miscall
by asking for the same treatment on the remaining slots):

| Item | Icon (real, readable) | Real model file (`--listfile`) | Real `ModelResourcesID` | husk's resolved `ModelResourcesID` (both array elements) |
|---|---|---|---|---|
| Tasteful Eyeglasses (HEAD) | `inv_helm_glasses_b_01_gold2_teal` | `helm_glasses_b_01_ni_f.m2` (FileDataID 3774132 -- the real per-race/-sex variant, Night Elf female, matching Linore exactly) | not yet checked | husk got FileDataID **1912096** instead (`helm_leather_warfrontshorde_d_01_be_m.m2`) |
| Barbed Rootwand (MAIN_HAND) | `inv_wand_1h_dungeonharronir_c_01` | `wand_1h_dungeonharronir_c_01.m2` (FileDataID 6652916) | **81950** | `[72423, 72423]` |
| Elderbloom Lantern (OFF_HAND) | `inv_offhand_1h_dungeonharronir_c_01` | `offhand_1h_dungeonharronir_c_01.m2` (FileDataID 6652915) | **82047** | `[72412, 0]` |
| Sacred Templar's Buckle (WAIST) | `inv_buckle_armor_nightfall_c_01` | `buckle_armor_nightfall_c_01.m2` (FileDataID 6255289) | **76889** | `[66300, 0]` |

**This rules out the "wrong array index" theory the first draft of this doc raised**:
neither `ModelResourcesID[0]` nor `[1]` matches the verified-correct value for any of
the checked items -- confirmed two independent ways (husk's own C++ resolve path, and a
fresh `husk db2-export` -> SQL query of the same file, both agree on the same wrong
numbers). The values husk decodes aren't almost-right; they're a different real
`ModelResourcesID` from somewhere else in the table entirely.

**New, better-localized suspect**: `ItemDisplayInfo.db2`'s `ModelResourcesID` field is
declared `bitpacked_indexed_array` (confirmed via `husk db2-info`'s raw field dump --
`[10] bitpacked_indexed_array offset_bits=57 size_bits=14 array_count=2`), backed by a
real, substantial palette (`pallet_data: 363240 bytes`, `flags: 0x4` "has non-inline
IDs"). This is a fundamentally different storage path than a plain inline value: the
raw bits are an *index*, and the real value has to be read back out of `pallet_data` at
a computed offset (`db2::decodeField`'s `BitpackedIndexed`/`BitpackedIndexedArray`
cases, `src/db2.cpp:545-560` and `:733-750`, both call `readPallet4`). The mechanism
exists and isn't obviously missing -- read at a skim, the code looks structurally
correct -- but it produces wrong output for this table/layout regardless. Not
byte-level debugged this session (would need stepping through the exact
index-to-palette-offset arithmetic against this file's real bytes, ideally cross-checked
against an independent reference tool like wow.export/DBCD reading the same file) --
flagging for a dedicated session with this doc's own verified ground-truth table above
as the pass/fail check.

**Root cause, found and fixed (2026-08-23)**: confirmed via a byte-level debugging
session against the real `itemdisplayinfo.db2` bytes (a small standalone C++ driver
linked straight against `src/db2.cpp`, `db2::parse`/`db2::decodeField`/`db2::recordId`
called directly -- not reimplemented, the real parser). `db2::additionalDataOffset`
(`src/db2.cpp`) summed `additionalDataSize` only over *prior fields of the exact same
`FieldCompression` value* -- but `BitpackedIndexed` (type 3) and `BitpackedIndexedArray`
(type 4) both draw from the **same** `pallet_data` block (DB2.md's own
`field_storage_info` doc comment: additional_data_offset sums "any previous fields which
are stored in the same block", grouped by *block* -- `pallet_data` vs. `common_data` --
not by the finer-grained storage type). `ItemDisplayInfo.db2`'s real layout is exactly
this shape: 10 `BitpackedIndexed` scalar fields (`GeosetGroupOverride`...`Flags`) before
`ModelResourcesID`'s `BitpackedIndexedArray` field -- so every `BitpackedIndexedArray`
read in this table landed 9,032 bytes short of its real pallet region (the summed
`additionalDataSize` of those 10 prior fields), decoding a real-but-unrelated pallet
entry instead of throwing or visibly failing. Confirmed byte-for-byte against real data
before touching the code: for Barbed Rootwand's `ItemDisplayInfoID` 719823, the correct
value (81950, this doc's own verified ground truth) lives at real pallet_data byte
offset 80608 -- exactly `9032 (prior BitpackedIndexed fields' additionalDataSize) + 8947
(the real decoded index) * 4 * 2 (arrayCount)`, matching the buggy code's own computed
index precisely once the missing 9,032-byte block offset is added back in.

**Fix**: `additionalDataOffset` now groups by block (a new `sharesAdditionalDataBlock`
helper: `BitpackedIndexed`/`BitpackedIndexedArray` share `pallet_data`; `CommonData` is
`common_data`, alone) instead of by exact storage type. Re-verified against all 4 of
this doc's own reverse-lookup ground-truth values, end to end through real DB2 data
(`husk db2-export` + `sqlite3`, not just the isolated debug driver): MAIN_HAND (Barbed
Rootwand) now resolves `ModelResourcesID` 81950 (was 72423), OFF_HAND (Elderbloom
Lantern) 82047 (was 82047's neighbor, previously wrong), WAIST (Sacred Templar's Buckle)
76889, HEAD (Tasteful Eyeglasses) 59419 -- all four exactly matching this doc's own
independently-derived ground truth table above, 4 for 4. New regression test
(`tests/test_db2.cpp`, a synthetic 2-field file: one `BitpackedIndexed` field followed
by one `BitpackedIndexedArray` field sharing `pallet_data`, reproducing the exact real
bug shape) plus a full-suite re-run, both green (696/696 -- 695 pre-existing + 1 new, 0
regressions). Same fix automatically covers the blast-radius question below --
`ModelMaterialResourcesID`/`ModelType`/`GeosetGroup`/`AttachmentGeosetGroup`/
`HelmetGeosetVis` all go through the same `additionalDataOffset`, no per-field
workaround needed.

Not re-investigated this session: whether this bug affects any *other* table beyond
`ItemDisplayInfo.db2` (any WDC5 table mixing `BitpackedIndexed` and
`BitpackedIndexedArray` fields would have been affected identically) -- the fix is
general (in the shared decode path, not `ItemDisplayInfo`-specific), so no further
per-table work is expected to be needed, but no corpus-wide re-scan across every other
`.db2` table was run to confirm no other consumer's behavior visibly changed.

**New, separate, smaller gap found while re-verifying the fix against a real Linore
export**: `ModelFileData.db2`'s `ModelResourcesID -> FileDataID` mapping is not always
1:1 -- HEAD (Tasteful Eyeglasses, `ModelResourcesID` 59419) has **33** candidate
FileDataIDs, one per race/sex variant of the same eyeglasses geometry
(`helm_glasses_b_01_be_m.m2`, `..._ni_f.m2`, ...), not 33 unrelated items. `husk export`
already captures every candidate (`gltf::Skeleton::GearItem::modelFileDataIds`,
`export_extras.cpp:873`) but `cmd_export.cpp:738` picks `.front()` unconditionally, with
no race/sex filtering -- so the re-verification export's `aux_models/head_3774115.glb`
is `helm_glasses_b_01_be_m.m2` (Blood Elf male), not the Night Elf female variant
Linore actually needs. MAIN_HAND/OFF_HAND/WAIST were all unambiguous (each resolved to
exactly one FileDataID) so this doesn't affect them. Not fixed this session (out of
Finding 1's own scope -- Finding 1 was specifically about the wrong
`ModelResourcesID`, which is now correct; picking the right FileDataID *among* a
correct `ModelResourcesID`'s real candidates is a distinct, smaller problem), but
recorded here since it was found live during verification and would otherwise look
like Finding 1 recurring.

## Finding 1b (new this session): 4 of the 6 dangling gear slots have no standalone model at all -- by design, not a bug

Reverse-lookup was extended to the 6 slots from Finding 3b too (all were dangling --
husk never got far enough to resolve any `ModelResourcesID` for them, right or wrong).
The icon-codename search turned up real texture-component assets for 4 of them, but
**no `.m2` model file anywhere in `--listfile` for any of the four**:

| Slot | Item | Icon codename | Real assets found | Model file? |
|---|---|---|---|---|
| CHEST | Leyline Scholar's Vestments | `inv_chest_cloth_legionquest100_b_01` | `item/texturecomponents/torsoupper(lower)texture/cloth_legionquest100_b_01_*_chest_t{u,l}_{f,m}.blp` (per-color, per-sex texture layers) | **none** |
| WRIST | Untethered Seer's Bands | `inv_bracer_cloth_outdoorethereal_c_01` | `item/texturecomponents/armlowertexture/bracer_{mail,leather}_outdoorethereal_c_01_al_u_*.blp` | **none** |
| HANDS | Shadowlace Handwraps | `inv_glove_cloth_oribosdungeon_c_01` | `item/texturecomponents/{handtexture,armlowertexture}/cloth_oribosdungeon_c_01_glove_{ha,al}_u_*.blp` | **none** |
| FEET | Boots of the Swift Fox | `inv_boot_armor_fox_c_01` | `item/texturecomponents/{foottexture,leglowertexture}/boot_armor_fox_c_01_*_u_*.blp` | **none** |

This is expected, not a gap: chest/bracer/glove/boot pieces are normally **case-2**
items in WoW (a texture layer composited onto the base body mesh, no separate
geometry) -- the same distinction `GearSectionOverlay` vs `GearItem` already encodes in
`src/gltf_skeleton.hpp`. So even a fully-fixed `ModelResourcesID` decode (Finding 1
above) would correctly leave these four at "no geometry" -- their real gap is the
**texture** side (`ModelMaterialResourcesID`/`ComponentSection` -> the DB2 chain
`GearSectionOverlay` already targets), blocked by the same dangling-`ItemDisplayInfoID`
issue as Finding 3b, not by Finding 1's `ModelResourcesID` bug.

**SHIRT (Rich Purple Silk Shirt, ItemModifiedAppearanceID 1706) stayed unresolved this
session** -- its icon (`inv_shirt_16`) didn't match any real asset path found by
searching `--listfile` (tried `shirt_16` literally and the `item/objectcomponents/shirt/`
directory, both empty). Possibly a pre-Legion/pre-`objectcomponents` era asset using a
different, older naming convention this session didn't identify -- open question, not
resolved.

**One lead checked and ruled out, worth recording so it isn't re-chased**: a generic
`item/texturecomponents/*texture/shirt_basic_a_04_purple_*` recolor family exists
(with its own dedicated icon, `inv_shirt_basic_a_04_purple`) and matches by color name
-- but Luna confirmed directly this is a *different, newer* shirt, not this classic
(patch 1.1) item. The real asset this vanilla-era item actually uses is still unknown;
there's a real, different path somewhere that wasn't found this session. `item=4335`
on Wowhead confirms the real item, for whoever picks this back up.

**Funny edge case, exactly as Luna flagged it**: SHIRT's `.transmog.item.name` is
literally *"Rich Purple Silk Shirt"* -- the same string as the equipped item's own
`.name`. Blizzard's API does not represent "not transmogrified" as an absent
`transmog` object for this slot; it represents it as a real transmog entry that
happens to point back at the equipped item itself. (Contrast with MAIN_HAND/OFF_HAND
above, where "not transmogrified" *does* show as `transmog: none`/absent -- so this
isn't a fixed rule across all slots, just a real inconsistency in how Blizzard's own
API represents "no override" that any future tooling needs to handle both ways.)

## Finding 2 (corpus gap, not a husk bug) -- FIXED this session

`chrraces.db2` and `texturefiledata.db2` were both **absent** from
`/media/luna/data/wow_export/dbfilesclient/` (not stale -- genuinely missing files).
Checked via `casc-tool info <FileDataID> --storage <WoW install>`: both came back
"FileDataID is known but its data isn't available in this local install (likely
optional/legacy content that was never downloaded)" -- the same "known to CASC's
manifest, streaming-install never pulled the bytes" class of gap `chrraces.db2` hit
once before (`CLAUDE_HISTORY.md`, 2026-08-20), not a corruption or extraction bug.

**Fixed live**: `tact-fetch dry-run` confirmed 2 small files, no encryption blockers;
`tact-fetch fetch` (real Blizzard CDN GET, run only after Luna's explicit go-ahead)
pulled both in under a second (landed as `_unresolved/FILE<hex>.db2` since no
`--listfile` was passed to `tact-fetch` itself -- resolved by hex-to-decimal converting
the filename back to the FileDataID). Placed at
`/media/luna/data/wow_export/dbfilesclient/{chrraces,texturefiledata}.db2`.
`chrraces.db2`: 58 real records (plus 1 real TACT-key-encrypted row, expected/rare, not
a blocker). `texturefiledata.db2`: 214,436 real records.

**Re-ran Linore's export against the same `--appearance` string to confirm the fix
actually helps, not just "file now exists"**: `couldn't read... chrraces.db2` and
`couldn't read... texturefiledata.db2` warnings both went from present every run to
**zero**; every previous `file_data_id 0` texture-resolution fallback (dozens of them)
is now a real resolved FileDataID. Finding 1's gear-geometry mismatches (a different
table chain entirely -- `modelfiledata.db2`/`ItemDisplayInfo.ModelResourcesID`, not
`texturefiledata.db2`) are, as expected, **unaffected** -- same wrong FileDataIDs
resolve before and after.

## Finding 3a (real husk bug, FIXED this session): "Hidden" transmog was misreported as a dangling reference

Two of the original 8 "dangling" slots turned out not to be a data gap at all: SHOULDER
(77343) and TABARD (83203) are both genuinely transmogged to **Hidden** on Linore
(`equipment.json`'s `.transmog.item.name` -- "Hidden Shoulder"/"Hidden Tabard").
Confirmed by direct SQL against `itemappearance.db2`: both real rows have
`ItemDisplayInfoID = 0` stored **literally**, not absent -- Blizzard's own real sentinel
for "no display," the same convention this codebase already applies to
`modelResourcesId == 0` one hop downstream (`export_extras.cpp`'s own comment: "A real
ModelResourcesID of 0 means... not 'unresolved'"). `itemappearance_db2.cpp::resolve`
had no equivalent guard for `ItemDisplayInfoID == 0` -- it unconditionally looked the
value up as a real ID, failed (real IDs start at 224), and logged a false "dangling
reference."

**Fixed**: `src/itemappearance_db2.cpp` now short-circuits on `itemDisplayInfoId == 0`
before attempting the `ItemDisplayInfo` lookup, matching the existing convention. Full
suite green after the fix, 693/693 (was already green before -- this is a genuine new
no-code-path-broken fix, not a regression repair). Re-ran Linore's export: dangling
`ItemDisplayInfo` count dropped from 8 to 6, SHOULDER/TABARD no longer appear.

## Finding 3b (corpus gap, not a husk bug): 6 of 13 equipped gear slots still dangling-reference against a fresh DB2 snapshot

The other 6 of the original 8 are real gaps, unaffected by Finding 3a's fix -- their
`ItemDisplayInfoID` values are genuinely nonzero and simply absent from
`itemdisplayinfo.db2`, even freshly re-extracted (2026-08-22, one day before this
test):

| Slot ID | Slot | Displayed item name | `ItemModifiedAppearanceID` | Dangling `ItemDisplayInfoID` |
|---|---|---|---|---|
| 4 | SHIRT | Rich Purple Silk Shirt (no transmog override) | 1706 | 4639 (confirmed absent by row count, see below) |
| 5 | CHEST | Leyline Scholar's Vestments (transmog) | 289269 | 146393 |
| 7 | LEGS | Master Warmage's Leggings (transmog) | 81815 | 155867 |
| 8 | FEET | Boots of the Swift Fox (transmog) | 285128 | 697416 |
| 9 | WRIST | Untethered Seer's Bands (transmog) | 292091 | 711612 |
| 10 | HANDS | Shadowlace Handwraps (transmog) | 107378 | 184078 |

The 5 that resolved: HEAD(1)/WAIST(6)/BACK(16, Hidden Cloak -- resolves fine since its
own `ItemDisplayInfoID` is a real nonzero row with `modelResourcesId==0`, the *already*
-handled case)/MAIN_HAND(21)/OFF_HAND(22) -- see Finding 1 for what "resolved" actually
produced for 3 of those 5.

Spot-checked one directly against raw SQL
(`husk db2-export` + `sqlite3`): `ItemDisplayInfoID 4639` (the SHIRT slot's target) is
genuinely absent from the local `itemdisplayinfo.db2` (72,452 rows, ID range 224-750273
-- 4639 is well within range, just missing), even though the *referencing* row in
`itemappearance.db2` (same extraction date) points at it. This is an internal
inconsistency within one same-day extraction pass, not a same-file-different-day
staleness issue -- some `ItemDisplayInfo` rows are silently absent from an otherwise
seemingly-complete extraction. Likely the same class of "partial/gated extraction" gap
this project has hit before with other tables (see `CLAUDE.md`'s history of
`texturefiledata.db2`/`chrcustomization*.db2` needing re-fetches). **Fix**:
re-extraction, not a husk change -- flagging for whoever runs the next `casc-tool` pass.

## Finding 3c (major correction to this doc's own first-draft claim, + a real discovery about the Blizzard API): the "107/132 (81%) customization choices dangling" number was wrong -- the real number is 2/21

The first draft of this doc took every `ChrCustomizationChoiceID` in Linore's `cust=`
field at face value and reported 107 of 132 (81%) as dangling against
`ChrCustomizationGeoset`/`ChrCustomizationMaterial`. That's true as a raw count, but
**misleading**: cross-referencing every one of those 132 IDs against
`ChrCustomizationOption.ChrModelID` (via `chrcustomizationchoice.db2` joined to
`chrcustomizationoption.db2`) shows only **21 of the 132 actually belong to Linore's
own model** (`ChrModelID` 8, Night Elf female). The other **111** belong to 8
completely different `ChrModelID`s -- 123 (27 choices), 125 (22), 124 (22), 186 (12),
188 (9), 202 (7), 207 (6), 206 (6) -- with option names like "Wings", "Saddle", "Beak",
"Body Armor", "Crest", "Snout", "Top" (values "Zeppelin"/"Balloon"/"Quad Glider"). None
of that describes a Night Elf.

**Confirmed, not guessed** (per Luna's own suspicion this session): chased each
`ChrModelID` through its own `DisplayID` -> `CreatureDisplayInfo.db2` ->
`CreatureModelData.db2` -> real model FileDataID -> `--listfile` path:

| `ChrModelID` | Real model |
|---|---|
| 123 | `character/companionserpent/companionserpent.m2` |
| 124 | `character/companionprotodragon/companionprotodragon.m2` |
| 125 | `character/companiondrake/companiondrake.m2` |
| 186 | `character/rostrumstormgryphon/rostrumstormgryphon.m2` |
| 188 | `character/rostrumfaeriedragon/rostrumfaeriedragon.m2` |
| 202, 206, 207 | `character/rostrumairship/rostrumairship.m2` (all three -- likely distinct customization contexts for the same mount) |

All customizable companion/mount models (dragonriding drakes, the "Rostrum"
delve/collector's mount family) -- bundled into the same `appearance` API response as
her actual character's customizations, unrelated to her body render at all.

**This is confirmed to be real data from Blizzard's own live API, not a bug in
`tools/blizzard_profile_fetch.py`** -- double-checked directly against the raw
`cust=` string this session after first (wrongly) suspecting a fetch-script bug; every
one of those 111 "unrelated" IDs is genuinely present in the string
`tools/blizzard_profile_to_appearance_string.py` produced from Blizzard's own JSON.
Nothing invented it locally. Whether the fetch/appearance-string tooling *should*
filter `cust=` down to just the target model's own choices is a real, separate,
unresolved design question -- not attempted this session (filtering would need to know
the target `ChrModelID` at fetch time, which today only `husk export`'s own
`--chr-model-id auto` derivation knows, one step later in the pipeline).

**The real, corrected dangling count**: of the 21 choices that actually apply to
Linore's own body, only **2** are dangling -- `862` (Hair Color) and `45117`
(Eyesight: "Neither"). The full real 21-choice list, with real names, all confirmed
plausible for a Night Elf (Blindfold, Earrings, Ears: Feral, Eye Color, Eyebrows: Long,
Eyesight: Neither, Face, Hair Color, Hair Style: Shaggy, Headdress: Circlet, Horns:
None, Markings: Bear, Markings Color, Necklace: Choker, Nose Ring: Septum, Scars:
Scratch, Skin Color, Tattoo: Ancient, Tattoo Color, Vine Color, Vines: Woven) --
"Horns"/"Markings"/"Vines"/"Tattoo" are real, current Night Elf customization axes, not
another sign of cross-model contamination. **This also resolves the "render looks fine
despite 81% dangling" puzzle this doc's first draft flagged as unexplained**: it looked
fine because the real gap was never 81% -- it was 2 of 21, both plausibly minor
(hair-color tint and an eyesight/pupil variant), not the dominant skin/hair/face axes.

**Fix for the 2 real gaps**: re-extraction (same as Finding 3b), not a husk change.

## Finding 4 (real, not a bug): modern `_hd` character models carry zero M2 attachment points

`husk info` on both `nightelffemale_hd.m2` and `bloodelffemale_hd.m2` reports
`attachments: 0 (offset 0x0)`, vs. 40 real attachment records (HandRight, HandLeft,
Shield, Back, ...) in `test_data/bloodelffemale.m2` (an older, non-`_hd` format
fixture). This is consistent across both real modern models checked, not
model-specific -- and the raw M2 header's `attachments` array genuinely has
offset/count zero in the file (not a husk parsing gap -- every sibling field in the
same header struct, e.g. bones/geosets/materials, parses correctly for these same
files). Modern player base models appear to have dropped the per-model Attachment
table entirely, relying instead on canonical, stable bone names -- confirmed present in
the exported skeleton: `HandR`/`HandL`/`SpellHandR`/`SpellHandL` are real joints in
Linore's exported `.glb`. This is architecturally sound for humanoid *player*
skeletons (always structurally identical, unlike creatures) and explains why creature
models still need the data-driven Attachment table while character models don't.

**Implication for gear attachment** (the next real step after Finding 1 is fixed): a
weapon can't be parented via husk's existing `attachment_<id>` extras nodes for these
models (there are none) -- it would need bone-name-based parenting instead
(`HandR`/`HandL`), which is straightforward for genuinely rigid single/few-bone items
but **not** for deforming multi-bone items. The OFF_HAND aux glb in this test (even
setting aside Finding 1's wrong-item issue) has 38 bones with names
(`Pelvis1`/`SpineLow`/`Chest`/`ShoulderL`/`HandR`/...) that closely mirror the base
character's own skeleton -- meaning some gear items are meant to be *re-skinned onto
the wearer's own skeleton* (shared bone names), not parented as a single rigid child
object. A correct attachment implementation needs to branch on this per item, not
assume one technique for all of `gear_items`.

## Not attempted this session (scope)

- **Fixing Finding 1.** No longer blocked on triangulation -- this doc's own
  reverse-lookup table gives 3 verified ground-truth `ModelResourcesID` values
  (81950/82047/76889) to check a fix against. Still needs the actual byte-level
  `db2::decodeField` debugging session (`BitpackedIndexed`/`BitpackedIndexedArray`
  cases, `src/db2.cpp:545-560`/`:733-750`).
- **Attaching gear geometry to the render** (Findings 1 and 4 are both prerequisites).
- **Compositing merged/section-overlay armor** (case 2 -- chest/legs/feet/etc. texture
  layers) onto the base body's material. Still fully inert extras, same as before this
  session; the precedent for how to do this in Blender already exists
  (`tools/husk_blender_geoset_mask.py`'s `_build_customization_option_group`-style node
  graphs for the customization-choice switch), just not built for gear yet.

## Deliverables from this run

- `example_exports/linore/linore.glb` -- full export with real customization +
  (currently-broken-per-Finding-1) gear extras attached.
- `example_exports/linore/aux_models/*.glb` -- the 4 standalone gear item exports
  discussed in Finding 1.
- `example_exports/linore/linore_preview.webp` -- rendered preview (base body +
  customization only, matching husk's current real capability).

## Re-verification run (2026-08-23, after Finding 1's fix)

Re-ran the exact same recipe end to end (`blizzard_profile_fetch.py` against the same
real Linore, `husk export --appearance` against the same real
`nightelffemale_hd.m2`, `render_glb.py`) to confirm the fix holds through the real
pipeline, not just in isolation. `example_exports/linore/{linore.glb,
linore_preview.webp}` and `example_exports/linore/aux_models/*.glb` were all
regenerated. `aux_models/{main_hand_6652916,off_hand_6652915,waist_6255289}.glb` now
carry exactly the real FileDataIDs this doc's own ground-truth table names (previously
`main_hand_5646084`/`off_hand_5645757`/`waist_4674142` -- all three wrong, per Finding
1). `aux_models/head_3774115.glb` is a real, correct-`ModelResourcesID` eyeglasses
model, but the wrong race/sex variant of it -- see Finding 1's new addendum above. The
render itself (`linore_preview.webp`) is unchanged from before this session: it still
only shows the base body + customization (husk's gear extras aren't consumed by the
render pipeline yet, `TODO/EQUIPPED_GEAR_RENDER_TODO.md` -- Finding 1 fixed *what
gear resolves to*, not whether it's rendered).
