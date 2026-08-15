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
`_Material` chain this file's own top note calls for is still not
verifiable the same way -- several of those exact tables are still 0-byte
in the current local export, a real extraction gap, not a code gap.
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

Worth noting for whoever picks up Stage 2 or the relational-schema side
project: several of the exact tables this plan needs are 0-byte files in
the current local `casc-tool` export (`chrcustomization.db2`,
`chrcustomizationcategory.db2`, `chrcustomizationchoice.db2`,
`chrcustomizationoption.db2`, `chrcustomizationreq.db2` all confirmed
0 bytes) -- a real extraction gap, not a husk bug, but it blocks Stage 3's
choice chain specifically until re-extracted. Same class of gap found again
independently while investigating `PCOL`'s bit-semantics
(`WIKI_FINDINGS_HISTORY.md` §18) -- `housedecor.db2` and five other
housing-prefixed tables are also 0 bytes in this same local export, so this
isn't a one-off, it's a recurring property of this specific local
extraction.

**A second, distinct DB2 gap found investigating a related but separate
question (2026-08-16): why can't husk fill in item/objectcomponents'
`object_skin` replaceable texture slots (`unfillable_texture_task.py`'s
`replaceable_only` bucket, 4,733 real corpus files) given `--db2-dir`
access already exists?** Checked with real tooling
(`husk db2-export --dir dbfilesclient/ out.sqlite --dbd-dir
reference/WoWDBDefs`, not assumed) rather than taken on faith --
`ItemDisplayInfo.db2`/`ItemDisplayInfoMaterialRes.db2`/
`ItemDisplayInfoModelMatRes.db2` (the chain that maps an item's own
FileDataID to its real per-race/gender texture, distinct from the
`ChrCustomizationOption` chain above) are **not** 0 bytes -- multi-
megabyte files that are genuinely present -- but every one of them is
**truncated a few dozen bytes short of a complete final record**
(`husk db2-export`'s own real error: `db2: truncated section.records:
need 135 bytes at offset 2528913, buffer is 2528913 bytes`, and
similarly 64/20 bytes short on the other two). A different failure mode
than the 0-byte tables above -- this reads as an interrupted download/
extraction, not missing/unavailable data, and is very likely fixable by
a small, targeted re-extraction of just these 3 files (same class of ask
already made once this session for the missing-texture FileDataID gap,
and far cheaper than a full re-extraction). `CharComponentTextureLayouts.
db2` is a third, harder case: it parses without error but yields only 4
real rows against a real game roster of several dozen race/gender
layouts, because `husk db2-export` reports skipping a **TACT-encrypted
section** in that specific file -- re-extraction alone won't fix this
one; it needs an up-to-date TACT decryption key covering that section,
which may or may not be available to `casc-tool`.

**Recommendation for whoever picks this up**: report the 3 truncated
`ItemDisplayInfo*` files to the casc-tool project first (likely closes
most of the 4,733-file gap on its own, low effort); re-check
`CharComponentTextureLayouts`' encrypted-section coverage after that
re-extraction (key lists get updated independently of any one
extraction run, so this may resolve on its own later); only then is
Stage 5's Blender-side picker (below) worth building specifically for
this gap -- it's the right shape for whatever residual ambiguity survives
a good-faith re-extraction (same "expose every real candidate, let a
human pick" pattern `tools/husk_blender_geoset_mask.py`'s geoset dropdown
already established for a structurally identical problem: husk knows
every real option but has no way to pick the one right one on its own),
not a substitute for fixing the underlying corrupt/incomplete local data
first.

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

### Stage 3 — the customization choice chain

`ChrModelTextureLayer` links a `TextureType`/`Layer` to a `SectionType` +
`BlendMode`, but *which* real file fills a given layer for a specific
character is a separate, longer chain:
`ChrCustomizationOption` (the player-facing choice, e.g. "Skin Color") →
`ChrCustomizationChoice` (one selectable value of that option) →
`ChrCustomizationMaterial` (the FileDataID that choice actually uses,
keyed by `ChrModelTextureTargetID`) — real DB2 tables, all present locally
per the file list above. husk has no concept of "a chosen character" today
(it processes one `.m2` + a texture directory, not a player's saved
choices) — this stage needs real design work: a new CLI input describing
which choice ID to use per option (or a sensible default/first-choice
fallback with a loud note, mirroring how `--textures` fuzzy-matching
already handles real ambiguity honestly rather than silently).

### Stage 4 — real pixel compositing

Once Stage 2+3 resolve a real ordered list of (file, placement rect,
blend mode) per compositing slot (`M2Texture::type` 1/8, per
`candidateCategoryTypes`' own doc comment in `src/export_texture_resolution.cpp`),
actually blit/blend each patch onto a base canvas sized from
`ChrModelMaterial`. `CharMaterialRenderer.js:345-372` enumerates the real
client's blend-mode branches (several are simple alpha/additive/multiply
cases; check that file directly for the full list and don't guess at
one husk hasn't seen) — needs a real software compositor (husk has no
GPU-shader dependency today and shouldn't gain one for this), operating
on the same decoded RGBA buffers `blp.hpp`/PNG decoding already produce.
Output: one real composited PNG per compositing slot, replacing today's
"one arbitrary/best-guess candidate" `baseColorImagePng` with the actual
correct look.

### Stage 5 (stretch) — Blender-side picker tooling

Luna's original framing, now buildable for real once Stage 2 exists even
without 3/4: "1 texture as default and rest which match that material as
unlinked texture nodes" — but *correctly UV-positioned* this time, not
just floating unlinked nodes with no spatial meaning. A Blender import
script (not `husk export` itself — this is Blender-side tooling this repo
doesn't have yet, same distinction `../EYES_ON_FINDINGS.md`'s finding #3/#6
already draws) that reads `alternate_textures`' real placement rects
(Stage 2) and builds a real shader node graph: each candidate wired to a
UV-mapped region matching its real section rect, toggleable/pickable by a
human without needing to understand the underlying DB2 data at all — the
practical payoff of all four stages above, in the tool a human actually
looks at.

## Why staged, not one change

Each stage is independently useful and independently risky: Stage 1 is
pure new-format-parsing risk (same shape as any other husk sidecar
parser, bounded). Stage 2 is low-risk once Stage 1 exists (read two more
tables, attach data, no new *behavior*). Stage 3 is a real design
decision (how does a CLI user express "which character") with no
established husk precedent to follow. Stage 4 is new problem *class*
entirely (image compositing, not data parsing) with its own correctness
bar (wrong blend math looks *confidently* wrong, the same failure shape
this project has hit and fixed before with texture defaults). Stage 5
lives outside `husk export` altogether. Landing them separately, in
order, means each one ships tested and useful on its own rather than one
large, hard-to-review change.
