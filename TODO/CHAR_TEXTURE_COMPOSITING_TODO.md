# TODO: real character texture compositing (DB2-driven)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

**Note from an earlier conversation, scope clarified directly by Luna
(2026-08-08, two passes) — SQLite is a separate side project, not part of
the real pipeline**: "that is mainly for debugging and investigation, the
real pipeline is the same as with modern blp's — read the file, transform
in memory, write to gltf." I.e.: a separate, optional DB2→SQLite exporter
(out-of-band, same "hand husk a plain local file" pattern `../DESIGN.md`'s
Non-goals already establishes for external CASC/DB2 tooling — see this
file's own Background below) is worth having for a human to inspect real
DB2 contents by hand, the same role `husk dump-chunks`/`husk-blp` already
play for M2/BLP — but Stage 1's real WDC5 parser (what `husk export`
itself actually links against at runtime) reads `.db2` bytes directly into
memory and feeds Stage 2+ straight from that, no SQLite round-trip in the
real path, same architecture as every other sidecar format this project
already has.

Its actual scope is bigger than a flat per-table dump, though, and its
usefulness reaches beyond this TODO's own compositing goal — second
clarification: "it's not gonna be just flat tables only, it's gonna have
mappings tables and stuff... the actual sqlite export is a side project to
confirm correctness, and to have data available for other relevant
targets not just the engine... I think it will become massively relevant
when the world data implementation starts." I.e.: a real relational
schema — mapping/join tables for the real foreign-key relationships
between DB2 tables (e.g. the `ChrCustomizationOption` → `_Choice` →
`_Material` chain Stage 3 below needs), not one flat SQLite table per
`.db2` file with no relationships preserved. Two real purposes, not one:
(1) a correctness cross-check for whatever WDC5 parser Stage 1 builds —
independently query the same data a human already trusts, compare against
what husk's own parser produces; (2) a general-purpose local data source
for *other* consumers of this project's WoW-format work, not just
`husk export` itself — explicitly named as likely to matter a lot once
WMO/ADT world-data work starts (`../WORLD_COMPLETENESS.md` and its companion
`*_TODO.md` files — real placement/area/lighting data for a rendered world
leans on DB2 tables at least as much as character customization does).
Worth designing the SQLite schema with that second, wider audience in mind
from the start, not just "whatever this one TODO happens to need."

One open question from that earlier conversation, relevant to the SQLite
side project specifically: whether any real DB2 row cell is itself an
array of nested arrays (which a naive flat-table export can't represent
directly, though a real mapping/join-table schema — see above — may
already handle this the same way it handles other relationships) —
investigated at the time, but no real example was ever actually found.
Worth re-checking against real data before assuming the schema needs to
account for it, rather than re-relitigating from memory.

## Background

`../EYES_ON_FINDINGS.md`'s finding #3/#6 (several sessions, most recently
2026-08-08) traced the "wrong texture matched" family of bugs as far as
possible without real DB2 data: husk can filter/rank ambiguous hardcoded
texture-slot candidates (by `M2Texture::type`, real filename category,
decoded pixel size), but it can never *pick* the one correct answer or
*composite* several real layered patches into the one final look a real
character actually has, because that's genuinely driven by data husk had
no access to — until now.

Two real, direct screenshots (Blender's image editor, a real
`bloodelffemale_hd.m2` export) proved `bloodelffemale_hd_skin_color_3500119`/
`_3500115` are non-transparent overlay **patches**, each pixel-matching one
specific rectangular region of the base atlas (`_3500123`) exactly — not
independent whole-slot alternatives, not junk. Traced the real client
mechanism in `reference/wow.export`
(`src/js/3D/renderers/CharMaterialRenderer.js:114-118`,
`src/js/db/caches/DBCharacterCustomization.js:203-215`): real compositing
is driven by three DB2 tables — `ChrModelMaterial` (base atlas
`Width`/`Height`), `CharComponentTextureSection` (`SectionType`/`X`/`Y`/
`Width`/`Height` — the literal placement rectangle a patch composites
into), `ChrModelTextureLayer` (`TextureType`/`Layer`/`BlendMode`/
`TextureSectionTypeBitMask` — which section a layer targets and how it
blends).

**Confirmed present as real local files, not a live CASC dependency**:
`/media/luna/data/wow_export/dbfilesclient/` — Luna's own local export,
extracted via her own `casc-tool` (**not** `reference/wow.export`, the
untrustworthy third-party JS reference tool checked out elsewhere in this
repo purely as corroborating source-code reference; two unrelated things
that happen to share a similar name, don't conflate them) — has all
three tables (`chrmodelmaterial.db2`, `charcomponenttexturesections.db2`,
`chrmodeltexturelayer.db2`) plus the *entire* customization-choice chain
needed to fully resolve which file goes where for a given character:
`chrcustomizationoption.db2`, `chrcustomizationchoice.db2`,
`chrcustomizationmaterial.db2`, `chrcustomizationelement.db2`,
`chrcustomizationcategory.db2`, `chrcustomizationgeoset.db2`,
`chrcustomizationskinnedmodel.db2`, and more (full list: `ls
dbfilesclient/ | grep -iE "chrcustomization|chrmodel|charcomponent"`).
Checked the header of one directly: `chrmodelmaterial.db2` starts with
`WDC5` (`WOWSTATIC_12_0_7_67808`) — the modern WDC5 DB2 container format,
confirmed, not guessed.

**Scope clarification from Luna, direct, settles a real ambiguity in
`../DESIGN.md`'s own Non-goals wording**: "the only hard boundary is not
loading casc tool as a dependency... all data in wow_export is free for
all, to be used." `../DESIGN.md`'s existing Non-goals text ("What husk itself
never does, at runtime, under any circumstance, is talk to CASC/DB2
directly") was written about *live* CASC/DB2 queries, matching its own
"husk only reads what's already on disk" framing elsewhere in the same
paragraph — a raw `.db2` file Luna's own `casc-tool` already extracted to
a local directory is exactly that: already on disk, same tier as
`.m2`/`.skin`/`--textures` files. Parsing the WDC5 *file format* locally
is not "talking to CASC/DB2" in the sense that non-goal means. `../DESIGN.md`
needs a real wording update once this lands, not just an implicit
reinterpretation.

## Target: full compositing pipeline

The stated goal (Luna, 2026-08-08): get to a real compositing pipeline,
and — stretch goal — Blender-side tooling (a shader node graph) letting a
human pick from the *real, correctly-placed* candidate options for a slot,
rather than today's flat, unpositioned `alternate_textures` list.

### Stage 1 — WDC5 parser

**Proof of concept landed and verified by Luna** (`src/db2.hpp`/`.cpp`,
`husk db2-info`, see
../README.md's own section on it) -- header/section-header/field_structure/
field_storage_info parsing, all six `field_compression` decode paths
(None/Bitpacked/CommonData/BitpackedIndexed/BitpackedIndexedArray/
BitpackedSigned), and a best-effort string-offset heuristic, all verified
against real files under `/media/luna/data/wow_export/dbfilesclient/`
(byte-exact header match against `chrmodelmaterial.db2`; real UTF-8 strings
round-tripped from `namesreserved.db2`; plausible-looking placement/size
values decoded from `charcomponenttexturesections.db2`/
`chrmodelmaterial.db2` -- not independently cross-checked against a second
tool yet, see below). Only WDC5 is implemented (no WDB2..WDC4 fallback),
and offset-map/sparse sections still expose raw record bytes but not
decoded fields -- both unchanged from the original POC.

**Column naming closed, a separate session**: `src/dbd.hpp`/`.cpp`, an
independent parser for WoWDBDefs' own documented `.dbd` text grammar
(`github.com/wowdev/WoWDBDefs`, README.md's own grammar spec, not reverse-
engineered), resolves a real file's `table_hash`/`layout_hash` against a
local, optional WoWDBDefs checkout to get real per-field names/types --
matched by *position* (declaration order, skipping `noninline` fields) and,
closed in a later session, cross-validated per field against
`field_storage_info`'s own real shape (bit size for `field_compression_none`,
an upper bound for bitpacked, `array_count` for `bitpacked_indexed_array` --
see `dbd::resolveFieldNames`'s own doc comment for exactly what's checked
per storage type and what isn't checkable at all) -- a layout with the
right field *count* but the wrong per-field *shape* now also fails closed
(generic `field_<N>` names) instead of returning a coincidentally-sized but
wrong match. Never a hard dependency: husk doesn't clone/fetch/bundle WoWDBDefs itself
(a dev-only `reference/WoWDBDefs` checkout, gitignored, is investigation
scaffolding only, never read at runtime) -- `--dbd-dir` is a local,
optional, user-supplied directory, same "hand husk a plain local file"
pattern `--textures`/`--skin-dir` already use; no matching layout falls
back to generic `field_<N>` names, never a guess. New command, `husk
db2-export <file.db2> <out.sqlite> [--dbd-dir DIR]`
(`../README.md`'s own section on it), writes a real SQLite database via the
now-flake-provided `pkgs.sqlite` -- verified against real data:
`chrmodelmaterial.db2` exports 336 rows with real `ID`/
`CharComponentTextureLayoutsID`/`TextureType`/`Width`/`Height`/`Flags`/
`Field_9_0_1_34615_006` columns and plausible real atlas dimensions.

**This closes the "no table-name-to-struct mapping" gap for the SQLite
side project specifically, but not for Stage 2.** Per this file's own top
note, the SQLite exporter is explicitly a separate side project ("read the
file, transform in memory, write to gltf" is the real pipeline, no SQLite
round-trip) -- `db2-export --dir` now produces a real relational schema
across files, not just one flat table per file: a column with a real
WoWDBDefs foreign-key target gets a real SQLite `FOREIGN KEY` constraint
whenever the target table is also part of the same export batch (verified
end to end against the real `ChrModelMaterial` -> `CharComponentTexture
Layouts` chain, see `README.md`'s `db2-export` section and
`CLAUDE_HISTORY.md`). The fuller `ChrCustomizationOption` -> `_Choice` ->
`_Material` chain this file's own top note calls for is now verifiable
the same way -- `_Option`/`_Choice`/`_Category` were fetched via
tact-fetch and placed locally 2026-08-20 (see the Update note below);
`chrcustomization.db2`/`chrcustomizationreq.db2` remain 0-byte and
unfetched, not yet confirmed needed for this chain.
Non-inline relationship data (WDC5's alternate `relationship_mapping`
foreign-key storage, e.g. real `ChrModelTextureLayer`'s own
`CharComponentTextureLayoutsID` under some layouts) is now folded into
`db2-export`'s own output too, closed in a later session: a
`$noninline,relation$` DBD field gets a real named SQLite column (its
per-record value resolved via `db2::nonInlineRelationValuesByRecord`, not
a field-array slot) and a real `FOREIGN KEY` constraint under the same target-in-batch rule
ordinary inline relation columns already use --
verified against the real `chrmodeltexturelayer.db2` ->
`charcomponenttexturelayouts.db2` chain (100% of 922 real rows resolve a
value, real `JOIN` rows returned for every layout ID actually present in
the local export), plus synthetic regression tests
(`tests/test_cli_db2.cpp`). Stage 2 itself is also
still unstarted: it needs real per-table **C++ structs** wired into
`db2::decodeField`'s actual callers inside husk's own process (`db2::File`/
`decodeField` deliberately stay name-agnostic, per db2.hpp's own module
comment), not a SQLite table a human reads separately -- `dbd::
resolveFieldNames` proves the *name resolution* half works end to end
against real data, but nothing yet feeds those names into a typed
`ChrModelMaterial` struct `export_materials.cpp` could consume.

**Update (2026-08-19): confirmed the 0-byte `_option`/`_choice`/
`_category` tables were not a re-extraction bug.**
Tried a real re-extraction of the three name-bearing tables
(`chrcustomizationoption.db2`/`chrcustomizationchoice.db2`/
`chrcustomizationcategory.db2`, FileDataIDs 3384247/3450554/3526439) via
`casc-tool extract` against Luna's real local WoW install --
`casc-tool` itself reported the real, more specific cause: "FileDataID
<N> is known but its data isn't available in this local install (likely
optional/legacy content that was never downloaded)". Unlike the earlier,
already-fixed `texturefiledata.db2`/`ItemDisplayInfo*` cases (those were
present-but-truncated, a genuine extraction bug), these three files were
never downloaded to the local install at all -- a re-extraction of
existing bytes can't produce data that was never pulled down in the
first place. Getting real `Name_lang` choice/option names therefore
routes through `tact-fetch` (`~/dev/tact-fetch`), the sibling project
built specifically for "FileDataID exists in the manifest, bytes were
never downloaded, fetch them from Blizzard's CDN" -- its own README
confirms this is exactly its intended use case.

**Update (2026-08-19/20): unblocked.** tact-fetch's CDN-fetch step is now
implemented and live-verified (two real bugs found and fixed along the
way -- silent tail truncation, a locale flag that didn't actually reach
the fetch handle -- see `tact-fetch/CLAUDE.md`'s Resume). Used it to fetch
all three files (`--locale enUS`, FileDataIDs 3384247/3450554/3526439);
placed at `/media/luna/data/wow_export/dbfilesclient/
chrcustomization{option,choice,category}.db2` (2026-08-20, previously
0-byte placeholders there). Verified end to end: `husk db2-export
--dbd-dir reference/WoWDBDefs` reads `chrcustomizationoption.db2` as 1148
real rows with real `Name_lang` strings ("Skin Color", "Face", "Hair
Style", ...). Stage 3's choice-chain design work below is no longer
blocked on external data -- the full customization-choice chain
(`ChrCustomizationOption`/`_Choice`/`_Category`, plus the already-present
`_Material`/`_Element`/`_Geoset`/`_SkinnedModel` tables) is now real,
local, and readable.

**Same-day follow-up: the name-to-geoset-selector mapping half of this is
now implemented, in `TODO_correctness.md` #2's scope, not this stage's**
-- `chrcustomization_db2.hpp`'s `namedChoicesForModel`/
`defaultChoiceIdsForModel` and `husk export --chr-model-id` (a heuristic
default: lowest-`OrderIndex` choice per option, not a client-verified
default).

**Second same-day follow-up: Stage 3's own "which character" identity
problem is now solved for any real character model, not just ones
following WoW's filename convention.** `--chr-model-id auto`
(`src/chrrace_db2.hpp`/`.cpp`) has two paths, tried in order: (1) primary
-- the model's own real FileDataID (resolved via `--listfile`/
`--listfile-root`), chased through `CreatureModelData.FileDataID ->
CreatureDisplayInfo.ModelID -> ChrModel.DisplayID` for an exact,
never-ambiguous-for-a-real-file answer; (2) fallback, only when no
FileDataID was found -- `ChrRaces.ClientFileString` + a "male"/"female"
filename suffix, exact case-insensitive match only. Both report and skip
(never guess) rather than fabricate an answer.

The FileDataID path exists because the filename-only path turned out to
have a real gap, found by Luna directly: the `character/dracthyr/`
folder has three files (`dracthyrmale.m2`/`dracthyrfemale.m2`/
`dracthyrdragon.m2`), and the earlier "ambiguous, 89 or 127" report for
Dracthyr male was husk's own bug, not real caution -- traced via the
real `ChrModel.DisplayID -> CreatureDisplayInfo.ModelID ->
CreatureModelData.FileDataID` chain that `dracthyrmale.m2`'s own
FileDataID resolves to exactly `ChrModelID` 127, not 89 (the shared
dragon form); the two were only ambiguous because race+sex alone is a
broader question than a specific input file answers. Verified against
all three real Dracthyr files: `dracthyrmale.m2` -> 127,
`dracthyrfemale.m2` -> 128, `dracthyrdragon.m2` -> 89, each cross-checked
directly against `chrmodel.db2`/`creaturedisplayinfo.db2`/
`creaturemodeldata.db2`. The filename-only fallback (no `--listfile`)
still correctly reports genuine ambiguity when the FileDataID path isn't
available.

What's still open: per-choice selection for a caller wanting a *specific*
character rather than a default (this only derives model identity, not
which hairstyle/skin-tone a specific character has); and a model with no
real FileDataID resolvable via `--listfile` and a filename that doesn't
follow the naming convention still needs an explicit `--chr-model-id
<id>`.

Same class of gap found again independently while investigating `PCOL`'s
bit-semantics (`WIKI_FINDINGS_HISTORY.md` §18) -- `housedecor.db2` and
five other housing-prefixed tables are also 0 bytes in this same local
export, so this
isn't a one-off, it's a recurring property of this specific local
extraction.

**A second, distinct DB2 gap found investigating a related but separate
question (2026-08-16, now fully resolved): why can't husk fill in
item/objectcomponents' `object_skin` replaceable texture slots
(`unfillable_texture_task.py`'s `replaceable_only` bucket, 4,733 real
corpus files) given `--db2-dir` access already exists?** Checked with
real tooling (`husk db2-export --dir dbfilesclient/ out.sqlite --dbd-dir
reference/WoWDBDefs`, not assumed) rather than taken on faith. Two real,
independent gaps, both closed this session:

1. `ItemDisplayInfo.db2`/`ItemDisplayInfoMaterialRes.db2`/
   `ItemDisplayInfoModelMatRes.db2` (the chain that maps an item's own
   FileDataID to its real per-race/gender texture) were each truncated a
   few dozen bytes short of a complete final record -- a real casc-tool
   extraction bug (an interrupted download), not missing data. Reported
   and fixed upstream: casc-tool re-extracted all three to their full
   real size (byte-for-byte match against `CascGetFileInfo`'s own
   `ContentSize`), zero-padding only the genuinely-still-encrypted
   ~0.1-0.2% and warning about it explicitly.
2. `CharComponentTextureLayouts.db2` looked like a *third*, harder case
   at first -- `husk db2-export` reported skipping a TACT-encrypted
   section, and the obvious read was "needs an up-to-date decryption
   key." **That diagnosis was wrong.** Direct investigation (a real
   Salsa20 port cross-verified against pycryptodome, several IV-
   derivation hypotheses tried and ruled out against a large, human-
   readable validation payload, then a check against `reference/wow.export`'s
   own `WDCReader.js` and `reference/DBCD`'s `WDC5Reader.cs`) found
   neither community reference tool implements DB2-internal Salsa20
   decryption *at all* -- both just check whether a `tact_key_hash`-
   bearing section's bytes are all-zero, and skip only if so. Checking
   husk's own local corpus confirmed why: of 126 real encrypted-flagged
   sections found, **111 were already non-zero, readable plaintext** --
   a real CASC extraction (via CascLib, given the community TACT key)
   already decrypts these sections before the file is written to disk;
   only 15 (all inside the one already-truncated `ItemDisplayInfo.db2`
   above) were genuinely still all-zero. husk's own `db2.cpp` had a real
   bug: it treated `tact_key_hash != 0` as "opaque, unreadable" *always*,
   never checking whether the bytes it actually had were still
   ciphertext. Fixed (`db2::Section::recordsAvailable()`, `db2.hpp`) --
   no TACT key store or Salsa20 implementation needed in husk at all.
   Verified end to end: `CharComponentTextureLayouts.db2` now exports all
   5 real rows (was 4), the full corpus DB2 batch export gained 446,719
   rows it was silently dropping before. A second, same-shape bug found
   and fixed alongside it: a `relationshipMap` region that reads as
   all-zero (the same "genuinely missing, not corrupt" signal) used to
   throw `ParseError` and abort the whole file's read; now degrades to
   "no relationship data for this section" instead, matching
   `recordsAvailable()`'s own reasoning. Both covered by real synthetic-
   buffer regression tests in `tests/test_db2.cpp`.

**Net effect**: both real blockers behind the `replaceable_only` gap
this TODO entry started from are now fixed. Re-scanning with
`unfillable_texture_task.py` (which does *not* consult `--db2-dir` at
all -- it only checks local `.blp`/`.png` files) will still show the
same 4,733 files, since that scan measures a different thing (whether a
standalone texture file exists locally at all, which for `object_skin`
slots it structurally never will). The real payoff of this fix is for
`husk export --db2-dir/--dbd-dir/--char-layout-id` and Stage 2 below,
which can now actually read `CharComponentTextureLayouts`/`ChrModel*`
data that used to come back empty.

A new, real file-format parser (`src/db2.hpp`/`.cpp`, matching the
existing `src/m2.cpp`/`src/skin.cpp` split-by-format convention) for the
WDC5 container: header (record count, field count, record size, string
table size, section count, ...), per-section headers, field storage info
(bitpacked/common-data/palette column encodings — WDC5's real complexity,
unlike M2's fixed-offset scheme), and row decoding into whatever concrete
struct a caller wants (`ChrModelMaterial`, `CharComponentTextureSection`,
etc.). Scope the *parser* generally (any WDC5 table, similar spirit to
`src/chunk.cpp` being generic over M2's chunk system) even though Stage 2
only consumes a handful of specific tables — the same file-format
investment pays for every table in the customization chain, not just the
first three. wowdev.wiki has the full WDC5 spec; no format investigation
needed, unlike several of this project's past reverse-engineered fields
(e.g. `skin::Submesh::Level`) — this one's fully documented already.

New CLI surface: `husk export --db2-dir <dir>`, a local, user-populated
directory (same "FileDataID/name-conventioned, never CASC" pattern
`--textures`/`--skin-dir`/`--anim`/`--bones-dir` already use) holding the
relevant `.db2` files by their real lowercase filenames (matching Luna's
own `casc-tool` export's naming, already confirmed above).

### Stage 2 — real placement geometry (implemented, scoped down from the original plan)

**Landed**: `src/chrmodel_db2.hpp`/`.cpp` (real typed structs, built on the
new `src/db2table.hpp` generic named-column reader) parses `ChrModelMaterial`
(per-layout base atlas `Width`/`Height` by `TextureType`),
`CharComponentTextureSections` (per-layout `SectionType`/`X`/`Y`/`Width`/
`Height` placement rects), `ChrModelTextureLayer` (`TextureType`/`Layer`/
`BlendMode`/`TextureSectionTypeBitMask`), and `CharComponentTextureLayouts`
(the shared atlas size), all keyed by `CharComponentTextureLayoutsID`.
`husk export --db2-dir/--dbd-dir/--char-layout-id` attaches the real,
filtered data as inert `chr_texture_layout` skin `extras`
(`gltf::Skeleton::CharTextureLayout`) — verified end to end against the
real `chrmodelmaterial.db2`/`charcomponenttexturesections.db2`/
`chrmodeltexturelayer.db2`/`charcomponenttexturelayouts.db2` chain (layout
ID 1: 3 materials, 12 sections, 12 texture layers, real atlas 1024x1024).

One real byte-format gap closed along the way: `ChrModelTextureLayer`'s own
`CharComponentTextureLayoutsID` is stored as a genuinely non-inline
`$noninline,relation$` column under its current real layout — its value
lives only in the section's own `relationship_map`
(`db2::nonInlineRelationValuesByRecord`, `dbd::findNonInlineNonIdFieldNames`),
not any field-array slot, previously entirely unreachable (see this file's
own Background section on `db2.cpp`'s pre-existing relationship-map gap).

**Scoped down from the original wording above, deliberately**: this does
NOT attach placement data to individual `AlternateTextureCandidate`s the
way originally planned — doing that requires knowing *which*
`CharComponentTextureLayoutsID` applies to the model being exported, which
needs `ChrModel.db2` plus a real display-ID/race/gender identity husk has
no concept of today (Stage 3's own open problem, not solved here). Instead,
the caller supplies the layout ID directly (`--char-layout-id`), same "hand
husk a plain local answer, don't make it guess" pattern as every other
opt-in sidecar. A human/Blender script can cross-reference the attached
`sections`/`texture_layers` against `alternate_textures` by hand; husk
itself does not attempt that link.

### Stage 3 — the customization choice chain (material half: done)

`ChrModelTextureLayer` links a `TextureType`/`Layer` to a `SectionType` +
`BlendMode`, but *which* real file fills a given layer for a specific
character is a separate, longer chain:
`ChrCustomizationOption` (the player-facing choice, e.g. "Skin Color") →
`ChrCustomizationChoice` (one selectable value of that option) →
`ChrCustomizationElement.ChrCustomizationMaterialID` →
`ChrCustomizationMaterial` (`ChrModelTextureTargetID` + `MaterialResourcesID`)
→ `TextureFileData.db2` (`MaterialResourcesID` → real texture `FileDataID`,
`UsageType == 0` rows only) — now implemented end to end
(`src/chrcustomization_db2.hpp`'s `Resolution::materials`,
`src/texturefiledata_db2.hpp`, wired into `husk export`'s existing
`--customization-choice-ids`/`--chr-model-id` chain, no new CLI flag
needed). Real, resolved `(choiceId, chrModelTextureTargetId,
materialResourcesId, fileDataId)` tuples attach as inert
`chr_enabled_materials` skin extras (`gltf::Skeleton::EnabledMaterial`),
same "husk resolves, never applies" policy as `chr_enabled_geosets`.
Verified end to end (`tests/test_cli_chrcustomization.cpp`): a real
`ChrCustomizationChoiceID` resolves through every hop to a real
`FileDataID`. `chr_texture_layout`'s own `texture_layers` entries also now
carry `chr_model_texture_target_id` (`src/chrmodel_db2.hpp`'s
`ChrModelTextureLayer::chrModelTextureTargetId`, decoded from a real
current-layout array field's first element -- `db2table::readNamedColumns`
already handles that transparently, no new array-reading machinery
needed) -- the real join key a downstream consumer needs to match a
resolved `EnabledMaterial` back to its own placement rect/blend mode.

**The full real customization menu is also attached, automatically --
`auto` is the real default, not a flag you have to ask for.**
Beyond the choice(s) a given export run actually resolves,
`chr_customization_options` extras (`gltf::Skeleton::CustomizationOption`/
`CustomizationChoice`) list every real `(Option, Choice)` pair for the
model, whenever a real `ChrModelID` can be determined at all.
`--chr-model-id` follows the same `auto`|`none`|`<id>` three-state
convention `--textures`/`--skin-dir`/`--skel` already use, but **unset
means `auto`**: given only `--db2-dir`/`--dbd-dir` (no `--chr-model-id`,
no `--customization-choice-ids` at all), husk still tries real
derivation. Explicit `--customization-choice-ids` alone also triggers
the same best-effort attempt purely for this extras array; `--chr-model-id
none` explicitly opts out of all of it. Deliberately not gated behind a
flag the caller has to know to pass -- this was `TODO/
CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`'s own real prerequisite (a live
Blender switch needs every choice visible, not just today's default/
explicit pick), and Luna's own direct instruction was that it must be on
by default, "not only if the user utters the magic words." See that file
for the full schema and the `tryDeriveChrModelId` factoring this needed
in `cmd_export.cpp`.

What's still open, deliberately not solved here: per-choice selection for
a caller wanting a *specific* named choice per option (today's
`--customization-choice-ids`/`--chr-model-id` chain already covers this --
explicit IDs, or a lowest-`OrderIndex` default heuristic -- this stage
needed no further CLI design work once that existed).

### Stage 4 — real pixel compositing (reverted -- NOT husk's job, see Stage 5)

**A same-session real pixel compositor (`src/char_composite.hpp`/`.cpp`,
real blend math transcribed from `reference/wow.export`'s own char shader/
renderer) was built, verified end to end, then deliberately reverted**
after Luna's own direct pushback: husk doing pixel compositing at all
breaks the "attach real resolved data, never interpret/apply it" policy
every other DB2 feature in this file follows -- Stage 4 was the one
exception, quietly crossing from data exposure into rendering. Worse, it's
the *wrong* layer for the actual goal (Stage 5's live per-option choice
switching): Blender's own Mix Color node already implements Multiply/
Overlay/Screen natively, so the blend math doesn't need reimplementing at
all, and live shader compositing lets a user switch skin color *and*
tattoo *and* face marking independently in real time -- something
husk precomputing static composited images fundamentally can't do without
one image per full cross-product combination. `Stage 3` above (the real
`ChrCustomizationMaterial → TextureFileData` FileDataID chain,
`chr_enabled_materials` extras) is the actual prerequisite Stage 5 needs;
this stage is now folded into it -- no separate pixel-compositing work
remains for husk to do.

### Stage 5 — Blender-side picker tooling (now the real next step, fully scoped in its own file)

**Full implementation plan, self-contained, ready to hand to a fresh
session: `TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`.** Summary only below
— that file is the source of truth for this stage, not this section.

Luna's original framing: "1 texture as default and rest which match that
material as unlinked texture nodes" — but *correctly UV-positioned* this
time, not just floating unlinked nodes with no spatial meaning, **and**
switchable per real customization option, not just a static default. A
Blender import script (not `husk export` itself — this is Blender-side
tooling this repo doesn't have yet, same distinction
`../EYES_ON_FINDINGS.md`'s finding #3/#6 already draws, same "reads raw
skin extras JSON, builds real node graph" pattern
`tools/husk_blender_geoset_mask.py` already established for geosets).
Not started — see `TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md` for the full
plan (exact extras schema, the real blend-mode-to-Blender-node mapping,
step-by-step implementation, and the open design questions).

### Stage 6 — equipped-gear appearance resolution (`husk-appearance/1`'s `gear` field)

Separate chain from Stage 3's body/skin customization materials: given an
`ItemModifiedAppearanceID` (what `husk appearance-string`'s `gear=SLOT:id`
entries carry — see `src/appearance_string.hpp`, `src/cmd_appearance.cpp`),
resolve it to the real equipped-item geometry/texture to render on the base
character mesh. Not yet started, and not yet resolved which real DB2 chain
is the right one to reuse: `EXPLORATION_TODO.md`'s already-mapped
`ModelFileData → ItemDisplayInfo → TextureFileData` path (CLAUDE.md's
Resume, "`EXPLORATION_TODO.md` follow-up") resolves an *equipped item's own*
`.m2` to its texture for standalone item rendering — starting point, not a
confirmed match, since that chain keys off an item/model FileDataID, not an
`ItemModifiedAppearanceID` directly; the `ItemModifiedAppearance.db2` →
`ItemAppearance.db2` hop needed to bridge the two hasn't been walked by hand
yet the way the rest of this chain was. `cmd_appearance.cpp`'s
`--validate` deliberately only carries `gear` entries through as opaque
IDs and says so out loud, rather than silently pretending they're resolved.

## Why staged, not one change

Each stage is independently useful and independently risky: Stage 1 is
pure new-format-parsing risk (same shape as any other husk sidecar
parser, bounded). Stage 2 is low-risk once Stage 1 exists (read two more
tables, attach data, no new *behavior*). Stage 3 is a real design
decision (how does a CLI user express "which character") with no
established husk precedent to follow. Stage 4 turned out not to be
husk's job at all -- image compositing is a new problem *class* entirely
(not data parsing), and doing it in husk broke this project's own "attach
real data, never interpret/apply it" policy; reverted in favor of Stage 5
doing the actual compositing live, in Blender, where it belongs. Stage 5
lives outside `husk export` altogether. Stage 6 is scoped but unstarted --
the DB2 bridge it needs (`ItemModifiedAppearance` → `ItemAppearance`) is
believed to exist but hasn't been confirmed against real local data the way
every other chain in this file has. Landing them separately, in
order, means each one ships tested and useful on its own rather than one
large, hard-to-review change.
