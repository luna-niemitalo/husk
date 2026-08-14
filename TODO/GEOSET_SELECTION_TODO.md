# TODO: real geoset selection (DB2-driven)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Background

`ENGINE_TODO.md` item 1 used to frame this as purely an external-engine
problem ("husk has no basis to pick one, and never will, since it doesn't
touch DB2") — that framing was corrected 2026-08-14: locally-extracted
`.db2` files are in scope for husk itself (`../DESIGN.md`'s Non-goals,
clarified 2026-08-08), same tier as `--textures`/`--skin-dir`. This file is
the dedicated tracking `ENGINE_TODO.md` flagged as missing, following the
same staged-implementation shape `CHAR_TEXTURE_COMPOSITING_TODO.md` already
used for the equivalent texture-layer problem.

**The gap**: `husk export` already tags every primitive with `geoset_id`/
`geoset_group`/`geoset_variant` extras (real `M2SkinSection.skinSectionId`
data), and `tools/husk_blender_geoset_mask.py` turns each group into a real
Blender dropdown — but nothing in husk resolves *which* geoset ID a real
character-customization choice (a specific hairstyle, a specific facial-hair
style, ears on/off) actually enables. A human has to pick blind in the
dropdown; there's no way to say "give me hairstyle 7" and get the right
`geoset_id` back.

## The real mechanism — verified against real local data, not guessed

Traced in `reference/wow.export/src/js/db/caches/DBCharacterCustomization.js`
(the same reference-only third-party tool already cross-checked for the
texture-compositing chain, not a runtime dependency) and independently
confirmed against Luna's own real local `casc-tool` extraction
(`/media/luna/data/wow_export/dbfilesclient/`), not just read from source:

```
ChrCustomizationChoice (a selectable value, e.g. "Hairstyle 7")
  --ChrCustomizationChoiceID-->
ChrCustomizationElement (the join table)
  .ChrCustomizationGeosetID  -->  ChrCustomizationGeoset
                                     .GeosetType, .GeosetID
                                     geoset_id = GeosetType * 100 + GeosetID
```

`geoset_id = GeosetType * 100 + GeosetID` is byte-for-byte the same
convention husk's own `geoset_group`/`geoset_variant` extras already use
for `M2SkinSection.skinSectionId` (`id / 100`, `id % 100`) — confirmed by
`reference/wow.export`'s own line `chr_customization_geoset_row.GeosetType
* 100 + chr_customization_geoset_row.GeosetID`, not assumed from the name
similarity alone.

**Confirmed present and populated locally** (`husk db2-info`, real output,
not from a listing):

```
$ husk db2-info dbfilesclient/chrcustomizationelement.db2
  record_count: 35845   field_count: 13
  flags: 0x4 (has non-inline IDs)

$ husk db2-info dbfilesclient/chrcustomizationgeoset.db2
  record_count: 512   field_count: 4  (real: ID, GeosetType, GeosetID, Modifier)
```

**Real column names resolved via `--dbd-dir reference/WoWDBDefs` and
exported end to end** (`husk db2-export ... --dbd-dir`, real SQLite output,
inspected directly):

```
CREATE TABLE "ChrCustomizationElement" (
  "ID", "ChrCustomizationChoiceID", "RelatedChrCustomizationChoiceID",
  "ChrCustomizationGeosetID", "ChrCustomizationSkinnedModelID",
  "ChrCustomizationMaterialID", "ChrCustomizationBoneSetID",
  "ChrCustomizationCondModelID", "ChrCustomizationDisplayInfoID",
  "ChrCustItemGeoModifyID", "ChrCustomizationVoiceID", "AnimKitID",
  "ParticleColorID", "ChrCustGeoComponentLinkID");
-- 35,790 real rows (1 encrypted section skipped, 55 rows), 3,811 of them
-- with a real nonzero ChrCustomizationGeosetID.

CREATE TABLE "ChrCustomizationGeoset" (
  "ID", "GeosetType", "GeosetID", "Modifier");
-- 512 real rows, e.g. row 1: GeosetType=0, GeosetID=0 -> geoset_id 0.
```

Nothing here is guessed — every table, every row count, every column name
above came from actually running `husk db2-info`/`husk db2-export` against
the real local files.

## Real bonus finding: the same table also answers `TODO_correctness.md` #2

`ChrCustomizationElement` has a `ChrCustomizationBoneSetID` column too — 645
of its 35,790 real rows have a nonzero value (e.g. `ChrCustomizationChoiceID
102 -> ChrCustomizationBoneSetID 24`), landing squarely inside
`chrcustomizationboneset.db2`'s own confirmed id range `[24, 742]`
(`TODO_correctness.md` #2). That item's own open question was "which
table joins a `.bone` `BFID`-array slot to a `ChrCustomizationBoneSet` row
for a given customization choice" — `ChrCustomizationElement` is exactly
that join table, for both geosets and bone sets at once, from the same row.
**Implemented alongside geoset selection below, not tracked separately.**

## Real correctness bug found and fixed during implementation

`ChrCustomizationChoiceID` is a genuine **one-to-many** relationship into
`ChrCustomizationElement`, not one-to-one — a real choice (e.g. 1758) owns
15-20+ real element rows, each typically carrying a nonzero value in only
*one* of `ChrCustomizationGeosetID`/`_BoneSetID`/`_MaterialID`/etc, the rest
zero. An early version of `resolveChoice` matched only the *first* row per
choice ID and silently returned `nullopt` whenever that first row wasn't
the one carrying the real geoset/boneset value — caught by real end-to-end
verification (choices 1758/1759 resolved to real `BoneFileDataID`s
1103216/1103217 in `husk db2-export`'s own sqlite output, but the CLI
export reported "0 matched" until this was fixed), not by inspection alone.
Fixed by scanning every element row for a given choice ID and taking
whichever one(s) carry a nonzero value, matching `reference/wow.export`'s
own per-field `.set()` loop over every row. Regression-covered by
`tests/test_cli_chrcustomization.cpp`'s first test case (a choice with two
element rows, only the second carrying the real value).

## The real blocker — not a husk problem, a local-extraction gap

The other half of the chain, `ChrCustomizationOption` (the player-facing
option, e.g. "Hairstyle") → `ChrCustomizationChoice` (its selectable
values, e.g. "Hairstyle 7"), is what a human-readable UI would need to
enumerate real choices by name. **Both tables are confirmed 0 bytes in the
current local `casc-tool` export** — the same gap `CHAR_TEXTURE_
COMPOSITING_TODO.md` already documents for its own Stage 3 (`chrcustomization
.db2`/`chrcustomizationcategory.db2`/`chrcustomizationchoice.db2`/
`chrcustomizationoption.db2`/`chrcustomizationreq.db2` all confirmed 0
bytes there too — same recurring property of this one local extraction,
not re-discovered here, just re-confirmed: `ls -la` on both files shows 0
bytes). This blocks "enumerate every real hairstyle choice by name," not
"resolve a given choice ID to its geoset."

## Implemented (2026-08-14) — husk side done, Blender side is the real remaining work

Same "hand husk a plain local answer, don't make it guess" pattern
`--char-layout-id` already established for the texture-compositing chain
(`CHAR_TEXTURE_COMPOSITING_TODO.md` Stage 2) — the caller supplies real
choice IDs directly, husk doesn't need to enumerate them itself.

- **`src/chrcustomization_db2.hpp`/`.cpp`** (new): a `db2table.hpp`-based
  typed reader for `ChrCustomizationElement`/`ChrCustomizationGeoset`/
  `ChrCustomizationBoneSet`, plus `resolveChoice(choiceId)` returning both
  a resolved `geoset_id` and a resolved `boneFileDataId` (either or both
  `nullopt` when that choice has no such element — real and common).
- **`husk export --db2-dir/--dbd-dir/--customization-choice-ids
  <id,id,...>`** (new, comma-separated, `src/cmd_export.cpp`'s
  `attachCustomizationChoices`) — resolves each real choice ID:
  - a real geoset selection is attached as `{choice_id, geoset_id}` on the
    glTF skin's new `enabled_geosets` extras
    (`gltf::Skeleton::EnabledGeoset`) — `geoset_id` uses the same
    `group*100+variant` convention a primitive's own `geoset_id` extras
    already use, so a consumer can match them directly, no conversion.
  - a real bone-correction-set selection marks the matching
    `--bones-dir`-resolved `CorrectionSet`'s own `selected_by_choice_ids`
    extras (only if that FileDataID was actually resolved via
    `--bones-dir` — a choice resolving to a real `BoneFileDataID` that
    isn't among this model's own resolved sets is reported, not
    fabricated or silently dropped). Must run after `attachBoneCorrections`
    in `cmd_export.cpp`'s pipeline, since it only marks already-resolved
    sets rather than attaching new `.bone` data itself.
  - Same "inert, never applied to the render" treatment as every other
    DB2/sidecar extras this project has — husk does not filter/hide any
    primitive based on this.
- **Verified end to end against real local data**: `husk export` against
  the real `bloodelffemale_hd.m2` fixture with real choice IDs 45/1758/1759
  (read directly out of a real `husk db2-export` sqlite dump) resolves
  choice 45 to `geoset_id: 2` and choices 1758/1759 to matched
  `bone_correction_sets` entries for real FileDataIDs 1103216/1103217 —
  inspected directly in the output `.glb`'s own JSON chunk, not asserted
  from the diagnostic text alone.
- **Tests**: `tests/test_cli_chrcustomization.cpp` (4 new CLI-tier cases:
  the real one-to-many regression above, an unmatched-boneset diagnostic
  case, an unresolvable-choice-ID case, and the missing-flags skip case).
  Full suite green, 632/632.
- `--db2-dir`/`--dbd-dir` are shared with `--char-layout-id`'s existing
  texture-compositing feature (same directory, same WoWDBDefs checkout) —
  documented in `README.md`/`DESIGN.md`'s flag tables.

**Deliberately not attempted**: enumerating every real choice by name
(needs the 0-byte `ChrCustomizationOption`/`_Choice` tables, a `casc-tool`
re-extraction problem, not a husk one — see "the real blocker" above), and
picking a *default* choice per option automatically (same blocker). Once
those tables are re-extracted, `chrcustomization_db2.hpp`'s reader is
already positioned to consume them — this doesn't need revisiting, just
extending with an `Option`/`Choice` loader alongside the three that exist.

## Remaining: expose this in Blender — not started

The C++ side attaches real data; nothing yet *reads* it on the Blender
side. `tools/husk_blender_geoset_mask.py` already builds one `Menu Switch`
dropdown per geoset group (`build_geoset_switch_node_group`) and already
has a precedent for reading extras husk attaches but Blender's stock
importer can't reach on its own (`read_chr_texture_layout` re-opens the raw
glTF JSON directly, since skin extras have no supported importer target at
all — confirmed empirically, see the texture-layout-overlay section of this
project's history).

Concretely, the same re-open-the-raw-JSON approach needs to:

1. Read `enabled_geosets` the same way `read_chr_texture_layout` reads
   `chr_texture_layout` (skin extras, not reachable via `bpy` post-import
   custom properties).
2. For each `{choice_id, geoset_id}` entry, find that `geoset_id`'s owning
   group (`geoset_id // 100`) and set that group's `Menu Switch` modifier
   input to the matching `variant_<geoset_id % 100>` item — same
   int-to-item mapping already reverse-engineered for the interactive
   dropdown (`item_index + 2`, `GEOSET_MASK_TODO.md`'s historical
   investigation, now folded into `DESIGN.md`), just driven by real data
   instead of a human click.
3. `selected_by_choice_ids` on `bone_correction_sets` has no consumer at
   all yet in Blender. **Correction to an earlier version of this
   paragraph**: "husk never applies `.bone` corrections to the render" is
   true of the C++ `husk export` binary specifically (`DESIGN.md`'s Key
   design decisions), but `tools/husk_blender_geoset_mask.py` is not bound
   by that same boundary — it already applies real husk-exported extras to
   actual Blender rendering for billboard alignment, texture-transform
   animation, tint/fade curves, and now geoset switching, precisely because
   it *is* "a downstream renderer or Blender script that does have the
   slot-selection mapping to apply on top" (`DESIGN.md`'s own phrasing for
   what `bone_correction_sets` was always waiting for). Now that
   `selected_by_choice_ids` gives it exactly that mapping, there's no
   architectural reason left not to build this.
   The real blocker is narrower and still genuinely open: the correction's
   own *application semantics* were reverse-engineered only as far as "a
   small delta matrix per bone" (`src/bone.hpp`'s doc comment) — multiply
   order, local-vs-model space, and where in the parent chain it composes
   are all unconfirmed against real client behavior. Applying an unverified
   composition in Blender risks a confidently-wrong pose correction, the
   same failure shape this project has hit before (the additive-blend
   `unlit`-co-occurrence bug, the billboard-alignment math that shipped
   flagged-unverified until ground-truthed). Needs the same treatment
   billboard alignment got: a real side-by-side against the client (or WMV/
   wow.export) on a model with a visually-obvious correction before
   trusting any specific composition order, not more code archaeology.
4. Verify headlessly against a real fixture with a real resolved geoset
   choice: confirm the dropdown lands on the expected item and the
   corresponding vertex group actually shows/hides, same verification
   discipline `GEOSET_MASK_TODO.md`'s own real bug-hunting session already
   established for this script (position-matching pitfalls documented
   there apply here too — don't re-invent that investigation from scratch).
