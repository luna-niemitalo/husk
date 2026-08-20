# CLAUDE.md — session history

Full session-by-session narrative log for husk, most recent first (the
same entries that used to live inline in CLAUDE.md's own Resume section).
`CLAUDE.md`'s Resume section now holds only a condensed current-state
summary plus Next step/Hazards — this file is where the full story lives.

**Append new entries at the top** (right after this intro, before the
existing most-recent entry) each session — this file is an append-only
log, historical entries are never rewritten (only living cross-references
inside them get repointed if something they name is deleted/renamed, per
this project's own established convention — see e.g. how prior TODO-file
deletions handled their own back-references).

---

**2026-08-20 (character-texture compositing, Stage 4 revert) — the real
software pixel compositor from the entry directly below was built,
verified, committed, then deliberately reverted, same session, after
Luna's own direct pushback.** Her argument, verbatim in spirit: husk isn't
supposed to do pixel shading at all, that's Blender's job -- husk's job is
just to expose the resolved data in a standardized format. Agreed with,
for two independent reasons, not just "the user said so": (1) every other
DB2-derived feature in this codebase attaches real resolved data and stops
(`chr_texture_layout`, `enabled_geosets`, bone corrections, physics
bodies) -- Stage 4 was the one exception, quietly crossing from data
exposure into actually interpreting/rendering the data, even though it
dressed itself up as "inert extras" by not wiring the result into any
material slot; (2) it's also the *wrong* layer for the actual end goal
(a live per-customization-option choice switch in Blender, discussed
immediately before this revert) -- Blender's own Mix Color node already
implements Multiply/Overlay/Screen natively, so the hand-transcribed blend
math wasn't even buying anything, and live shader compositing lets a user
switch skin color/tattoo/face-marking independently in real time, which a
husk-precomputed static image fundamentally cannot do (it would need one
image per full cross-product combination).

Reverted: `src/char_composite.hpp`/`.cpp` (deleted), `blp::decodePng`
(deleted, no other consumer), `gltf::Skeleton::CompositedTexture` and its
`chr_composited_textures` extras (deleted), `cmd_export.cpp`'s
`attachCompositedTextures` (deleted), the `images`/`textures` threading
into `emitSkeletonAndSkin` this all needed (reverted, `gltf.cpp`'s
`images`/`textures` declaration moved back to its original position after
the skeleton-emission call), `tests/test_char_composite.cpp` (deleted),
`decodePng`/`encodePng` round-trip tests in `test_blp.cpp` (deleted). Kept
and slightly extended: Stage 3's real `chr_enabled_materials` resolution
(the actual valuable part -- real data, not interpretation) and a new
field, `gltf::Skeleton::CharTextureLayout::TextureLayer::
chrModelTextureTargetId` (wired from `chrmodel_db2.hpp`'s own
`ChrModelTextureLayer::chrModelTextureTargetId`, added for the reverted
compositor's own internal join but kept because a downstream Blender
script needs exactly the same join to find a resolved material's real
placement rect/blend mode). The CLI test that used to exercise the full
compositing pipeline now stops at data exposure -- asserts
`chr_enabled_materials`/the new join key land correctly, and explicitly
asserts `chr_composited_textures` is *absent* (husk no longer produces it
at all). `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s own Stage 4 section
rewritten to explain the revert and its reasoning rather than silently
deleted -- the "why" here is genuinely instructive for whoever picks up
Stage 5 next, unlike a routine implementation-detail cleanup. Stage 5
(Blender-side node-graph tooling) is scoped in detail as the real next
step: read `chr_texture_layout`/`chr_enabled_materials`, build an Image
Texture + Mapping node per real choice's texture, a Menu Switch node per
option, and wire blend modes via Blender's own native Mix Color node --
same "read raw skin-extras JSON, build a real node graph" pattern
`tools/husk_blender_geoset_mask.py` already established for geosets, not
a new approach invented from scratch. One open design question flagged,
not yet resolved: today's DB2 extras only expose the choice(s) a given
`husk export` run actually resolved, not *every* real choice per option --
Stage 5's switch needs the latter, likely a new export flag. Full suite
green, 653/653 (net -10 from the compositor entry's 663: 8 compositor unit
tests + 2 `decodePng` tests removed, 1 CLI test reshaped in place, no net
new test).

---

**2026-08-20 (character-texture compositing, Stages 3 material chain + 4
pixel compositing) — `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md` Stages 1-4
are now all done.** Picked up per Luna's own framing that per-choice
selection is "pointless without" real Stage 4 compositing, so both landed
in one pass. Stage 3's material half: `chrcustomization_db2.hpp`'s
`Element`/`Resolution` gained `materialId`/`materials` (a choice can carry
several real `ChrCustomizationMaterialID`-bearing Element rows, same
"scan every row, don't stop at the first" discipline the existing
geoset/boneset code already established), a new `Material` struct/loader
for `chrcustomizationmaterial.db2`, and a new `src/texturefiledata_db2.hpp`/
`.cpp` resolving `TextureFileData.db2`'s `MaterialResourcesID -> FileDataID`
join (`UsageType == 0` rows only, a real filter confirmed against
`reference/wow.export`'s own `DBCharacterCustomization.js`). Wired into
the existing `--customization-choice-ids`/`--chr-model-id` chain in
`cmd_export.cpp` (no new CLI flag) as `chr_enabled_materials` skin extras.
`chrmodel_db2.hpp`'s `ChrModelTextureLayer` gained
`chrModelTextureTargetId` (the real join key against
`ChrCustomizationMaterial.ChrModelTextureTargetID` — a genuinely different
field from `TextureType`, confirmed against real local
`chrmodeltexturelayer.db2` bytes, `db2-info` showed the current real
layout stores it as an array field `[2]`; `db2table::readNamedColumns`
already decodes an inline array field's first element for a scalar-style
request, so no new array-reading machinery was needed).

Stage 4: new `src/char_composite.hpp`/`.cpp`, a real software pixel
compositor -- per-pixel blend math transcribed directly from
`reference/wow.export/src/shaders/char.fragment.shader` +
`CharMaterialRenderer.js`'s own outer GL blendFunc switch (BlendMode 0/1
blit-overwrite, 4 multiply, 6 overlay, 7 screen, 9 alpha-straight with its
own real alpha-channel accumulation formula distinct from 15's, 15
infer-alpha-blend; any other real BlendMode -- 2,3,5,8,10-14 -- is refused
and reported, never guessed at, since the real client's own fallback for
those is a solid magenta debug square). Nearest-neighbor resampling
matches the real client's own `TEXTURE_MIN_FILTER: NEAREST` for the layer
texture. `src/blp.hpp` gained its first real PNG *pixel* decode
(`blp::decodePng`, `stbi_load_from_memory` via the same already-linked
tinygltf-vendored stb_image.h `encodePng` already uses, extern-declared
the same way `blp.cpp` already does for the write side -- no new
dependency) since every prior consumer of "PNG bytes" in this codebase
treated them as an opaque blob to re-embed as-is, never actual pixels.
`cmd_export.cpp`'s new `attachCompositedTextures` joins
`EnabledMaterial.chrModelTextureTargetId` against
`ChrModelTextureLayer`/`CharComponentTextureSections` (via the layout ID
already resolved by `--char-layout-id`) to get each material's real
placement rect + blend mode, decodes its texture via `resolveTextureBytes`
+ the new `blp::decodePng`, and composites one atlas per real
`ChrModelMaterial::TextureType`. Output attaches as
`gltf::Skeleton::CompositedTexture` -> `chr_composited_textures` skin
extras, with the composited PNG becoming a real, otherwise-unreferenced
glTF `images`/`textures` entry (`texture_index`) -- same established
"inert extras, real embedded image, never auto-applied to any material"
pattern `alternate_textures` already uses. Required threading
`images`/`textures` into `emitSkeletonAndSkin` (`gltf_skeleton_internal.hpp`/
`.cpp`) and moving their declaration earlier in `gltf.cpp`'s
`writeGlbMulti` so both the skeleton-emission phase and the per-mesh
phase share the same model-wide lists. Deliberately **not** wired into
replacing any primitive's own `baseColorImagePng` -- matching a
composited atlas back to the specific primitive(s) that should render it
needs a `TextureType -> M2Texture::type` mapping this session didn't
chase down; a human/Blender script has everything it needs to do that
match by hand, same "husk resolves and attaches, never applies" policy
every other DB2-derived extras feature in this codebase already follows.

Verified end to end: new unit tests (`tests/test_char_composite.cpp`,
every blend formula checked against the transcribed real math; `blp.cpp`'s
`decodePng` round-tripped exactly through `encodePng` in
`tests/test_blp.cpp`) and a new full-chain CLI test
(`tests/test_cli_chrcustomization.cpp`) exercising every real hop --
`ChrCustomizationElement` -> `_Material` -> `TextureFileData` -> a real
texture file under `--textures` -> `ChrModelTextureLayer`/
`CharComponentTextureSections` -> a real composited pixel -- passed on
the first real run against the full synthetic DB2/DBD fixture chain.
`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`/`TODO/README.md`/`README.md`
updated; `TODO/COMBINER_HUNT_EXTENSIONS_TODO.md`'s own separate 2026-08-20
entry (dual-crossfade scalar search + Illum constant-output tier, an
unrelated same-day session) is a different piece of work, not folded in
here. Full suite green, 663/663.

---

**2026-08-20 (overnight batch-export pass, foreign-data follow-up) —
a corrupted/empty `--listfile` now warns loudly instead of silently
degrading.** Same autonomous overnight `/loop` session, next item off
Luna's explicit framing for the session: "warn at recoverable situations
... [but] throw an unrecoverable error" when a foreign-data failure
genuinely can't be worked around. Investigated what `husk export`
actually does when `--listfile` is missing, corrupted, or unreadable.

Found: a bad `--listfile` *path* (the file doesn't open at all) already
threw `std::runtime_error` before this session — a direct user mistake,
correctly fails loudly. But a `--listfile` that opens fine and parses to
*zero usable entries* (an empty file, or content that's the wrong format
entirely so every line fails `loadListfile`'s own malformed-line skip)
degraded completely silently: every real consumer of the listfile map
(`findFileDataIdForModelPath`, the fuzzy-texture-resolution fallback
tier, `--chr-model-id auto`'s primary derivation path) already treats an
empty map exactly the same as "no `--listfile` given" and falls back to
local-only resolution correctly — the recovery already worked, it just
had zero visibility. Fixed with one loud `std::cerr` warning in
`exportGlb`, printed once right after the load, only when `--listfile`
was actually given and the resulting map is empty. The export itself
still succeeds via the existing local fallback (this is example 1 in
Luna's framing — recoverable, warn and continue), it's just not silent
about it anymore.

New regression test (`tests/test_cli.cpp`, right after the existing real
`--listfile` resolution test for a natural before/after contrast): a
listfile with two lines of plain prose, no `;` separator anywhere,
confirms both the warning text and that the export still completes
(`exitCode == 0`, `.glb` written). Full suite green, 652/652 (up from
651 — the new test).

Deliberately scoped narrow rather than trying to solve all of foreign-
data robustness in one pass: this closes the "silent degradation when
recovery is real" half of Luna's framing. The other half (example 2 —
an *unrecoverable* error when no fallback can produce a correct answer)
needed a concrete case to fix, and none was found this pass; flagged as
`CLAUDE.md`'s Resume's own next step rather than guessed at. Also flagged
but not investigated: a listfile that's present and parses fine but
contains *wrong* data (stale snapshot, paths that don't resolve) —
different failure shape than "corrupted," this fix doesn't see it.

---

**2026-08-20 (overnight batch-export pass) — `husk export --from-list`/
`--output-dir`, plus committing the previous entry's own uncommitted
work.** Autonomous overnight `/loop` session (Luna asleep): picked up
`TODO/README.md`'s suggested-order list. First found the entire previous
2026-08-20 session's worth of work (`ChrCustomizationOption`/`_Choice`
name data, `--chr-model-id auto`, `chrrace_db2`) sitting uncommitted in
the working tree — full test suite already green per that session's own
narrative, just never `git commit`ed. Committed it as-is (no code
changes) before starting new work, so it wasn't lost or silently bundled
into an unrelated diff.

New work: `TODO/CLEANUP_TODO.md`'s former item 3, `husk export` batch
mode. `--from-list <file> --output-dir <dir>` exports every `.m2` path
listed in `<file>` (one per line, blank/`#`-comment lines skipped),
mirroring `casc-tool`'s own `extract-batch --from-list <ids-file>
<out-dir>` shape and answering the same "opens storage/loads shared
resources once, not per file" need — `tools/export_hd_characters.nu`/
`render_sample_driver.py` each solved this externally by looping `husk
export` themselves; this solves it in-process instead. Refactored the old
monolithic `exportGlb` into `exportOneModel` (the real per-model
pipeline, `modelPath`/`outputPath`/`outputGiven` now parameters instead
of read off `opts` directly) plus a thin `exportGlb` that either calls it
once (single-file mode, unchanged behavior) or loops it over `--from-list`
(batch mode) — `--listfile` is loaded exactly once either way, addressing
the exact per-call cost `loadListfile`'s own doc comment already flagged
for `render_sample_driver.py`'s 130k-call driver loop (the map is passed
by *value* into `exportOneModel`, not by reference, since `--knowledge-db`
mutates it with one model's own resolved texture and that must not leak
into the next model in a batch). One bad entry is reported (its own real
error message) and skipped, not fatal to the rest of the batch — a real
worklist can be thousands of entries, and one truncated/missing file
shouldn't cost the rest of an overnight run — with a final `N succeeded,
M failed` summary line and a nonzero exit code whenever any entry failed.
Output-name collisions (two entries sharing a basename — common across
race/expansion subdirectories in a real corpus) fall back to
`<parent-dir-name>_<basename>.glb`, then a numeric suffix, rather than one
entry silently overwriting another. `-i`/`--input`'s CLI11 `->required()`
had to move to a manual post-parse check (`--from-list` supplies model
paths itself, so `--input` can't be unconditionally required anymore).

Verified: manually against real `test_data/` fixtures (3-entry batch with
shared `--listfile`, all three mutual-exclusion error paths, one-bad-
file-among-good-ones both aborts-nothing and reports correctly, exit code
reflects partial failure) before writing automated coverage. 5 new
CLI-tier tests, `tests/test_cli_batch.cpp` (real subprocess spawns via
`run_husk.hpp`, same convention as every other `test_cli_*.cpp` split):
multi-entry batch + comment/blank-line skipping, one-bad-entry-doesn't-
abort-the-batch, basename-collision disambiguation, `--output`/`--input`
mutual-exclusion errors, empty-worklist no-op. `README.md` (new batch-mode
prose + flag table rows), `TODO/CLEANUP_TODO.md` (item 3 removed, item 4
renumbered), completions (regenerated via `--print-completion`, never
hand-edited) all updated. Full suite green, 651/651.

Two commits: the previous session's backlog (`ChrCustomizationOption`
name data + `--chr-model-id auto`) landed separately from this session's
batch-export work, kept apart since `git diff --stat` showed a clean
file-level boundary between them (only `src/cmd_export.cpp`/
`src/commands.hpp` mixed both — those two files' worth of diff landed in
the batch-export commit, which is honest about carrying both the
`--chr-model-id` CLI wiring and the new batch flags).

**Still open, flagged by Luna for this overnight pass specifically, not
yet investigated**: what husk actually does under corrupted/foreign-data
conditions it hasn't been deliberately tested against before — a missing
`--listfile`, one that's present but corrupted/truncated, or an export
that structurally *needs* listfile/DB2 data to proceed correctly. The
worked example Luna gave: primary resolution path fails/unavailable →
warn loudly and fall back to a still-correct secondary path when one
exists (recoverable) → but throw a real, unrecoverable error rather than
silently emitting wrong output when no fallback can produce a *correct*
answer. `loadListfile` already throws on a bad *path* (file doesn't open)
but a truncated/malformed CSV *content* case, and which of husk's own
DB2/listfile-dependent features have no fallback at all, haven't been
audited against this framing yet — next session's actual next step,
ahead of `KNOWLEDGE_BASE_DESIGN.md`'s full-corpus-render follow-up.

---

**2026-08-20 (final) — the Dracthyr "ambiguity" from the previous entry
was husk's own bug, not real caution; fixed with a second, more precise
FileDataID-based derivation path.** Luna: "the dracthyr folder has 3
files, dracthyrmale dracthyrfemale and dracthyrdragon, i am guessing that
the dragon one includes both male and female dragon forms as geosets,
investigate, as it is possible that the correct context is not ambiguous
after all."

Checked the real local folder first, not assumed: `find ... -ipath
"*dracthyr*" -iname "*.m2"` confirmed exactly those three files under
`character/dracthyr/` (no `female`/`male` subdirectory the way BloodElf
has -- flat, three files at the top level). Ran `husk info` on all three:
`dracthyrmale.m2`/`dracthyrfemale.m2` have real inline bones (255/205)
and a gendered `type=1` skin texture; `dracthyrdragon.m2` has zero inline
bones and needs an external `.skel` (real SKID 4618751) -- three genuinely
separate model files, not one shared dragon file with male/female
geosets as Luna's guess suggested. The real disambiguator turned out to
be identity, not geosets: chased `ChrModel.DisplayID ->
CreatureDisplayInfo.ModelID -> CreatureModelData.FileDataID` by hand for
`ChrModelID` 89 and 127 before writing any code -- 89 resolves to
`character/dracthyr/dracthyrdragon.m2` (`ChrModel.Sex` = 3, a real
"shared/none" sentinel, matching that the dragon form is available to
either sex via in-game transformation, not tied to one), 127 resolves to
`dracthyrmale.m2` (`ChrModel.Sex` = 0), 128 resolves to
`dracthyrfemale.m2` (`ChrModel.Sex` = 1). So "Dracthyr, male" genuinely
has two real answers (89 and 127) -- but "the specific file
`dracthyrmale.m2`" only has one. The previous entry's race+sex-only path
was asking the broader question, not the wrong one, but it wasn't the
*most precise* question husk could ask given what it already had.

Presented this finding plus a design fork to Luna before coding: patch
the race+sex path's own heuristics (narrower, filename-only) vs. add the
real FileDataID-based chain (broader, matches how texture resolution
already prefers exact file identity over guessing). Luna picked the
FileDataID chain, then immediately added a constraint mid-implementation:
"this should still work without the listfile, so implement the listfile
as the primary path with fallback onto the name matching" -- confirming
the two-tier design already in progress rather than changing it.

New `chrrace::deriveChrModelIdFromFileDataId` (`src/chrrace_db2.hpp`/
`.cpp`): given a real model FileDataID, resolves
`CreatureModelData.FileDataID -> CreatureDisplayInfo.ModelID ->
ChrModel.DisplayID`, needing three more small table readers
(`ChrModelDisplay`/`CreatureDisplay`/`CreatureModel`, same thin
`db2table`-backed pattern as everything else in this file) -- same
"collapse to exactly one distinct answer or report and skip" discipline
as the race+sex path, kept for the (unlikely but not assumed impossible)
case where even a real FileDataID resolves to more than one ChrModel row.
New `findFileDataIdForModelPath` in `cmd_export.cpp`: a linear reverse
scan over the already-loaded `--listfile` map (FileDataID -> path) to
find the input `.m2`'s own FileDataID by matching its path relative to
`--listfile-root` -- deliberately not a second indexed copy of a
multi-million-row community listfile, since this only runs once per
export. `--chr-model-id auto`'s logic now tries the FileDataID path
first; the race+sex path from the previous entry only runs as a fallback
when no FileDataID was found at all (no `--listfile` given, or this path
isn't under `--listfile-root`) -- once the FileDataID path resolves
anything, including a genuine ambiguity report from it specifically,
that's trusted over the weaker fallback, never silently second-guessed.

Verified end to end against all three real Dracthyr files with a real
`--listfile community-listfile.csv --listfile-root
/media/luna/data/wow_export`, each cross-checked by hand against
`chrmodel.db2`/`creaturedisplayinfo.db2`/`creaturemodeldata.db2` before
trusting the CLI's own output: `dracthyrmale.m2` -> `ChrModelID` 127 (not
89 -- the previous entry's own worked example, "dracthyrfemale.m2
derives ChrModelID 89," was itself wrong on the same grounds; the real
answer is 128), `dracthyrfemale.m2` -> 128, `dracthyrdragon.m2` -> 89.
Re-ran the no-`--listfile` case too, to confirm the fallback still
behaves exactly as the previous entry described: `dracthyrmale.m2`
without `--listfile` still correctly reports the 89/127 ambiguity and
skips, since that path only ever had race+sex to work with. 1 new
CLI-tier test, real `ChrModel`/`CreatureDisplayInfo`/`CreatureModelData`
fixtures via the existing `buildFlatDb2` plus a `--listfile` CSV fixture
following `tests/test_cli.cpp`'s own established format
(`"<FDID>;<path>\n"`). Full suite green, 646/646 (up from 645). Corrected
the previous entry's wrong "dracthyrfemale.m2 -> 89" claim wherever it
appeared in `README.md`/`TODO/*.md`/`CLAUDE.md`'s own Resume snapshot --
per this file's own append-only convention, the historical entry below
is left as originally written, not rewritten.

---

**2026-08-20 (latest) — `--chr-model-id auto`: derive a real ChrModelID
from the .m2's own filename, closing most of the previous entry's own
open question.** Prompted directly by Luna pointing out the real workflow
isn't "I want char 89" -- it's "file dracthyrfemale.m2 -> husk -> glb",
and the filename already carries everything needed (race + gender) to
find the ID, if husk could read it.

Confirmed the real WoW naming convention first, against real data already
in the repo: `test_data/character/bloodelf/female/bloodelffemale_hd.m2`
-- basename `<race><sex>[_hd]`, matching `ChrRaces.ClientFileString`
exactly once resolved. Checked what `ClientFileString` for "Dracthyr"
actually was before assuming it'd be lowercase/spaced differently from
the path convention -- it wasn't ("Dracthyr", "BloodElf", "Human", all
matching their path-convention lowercased forms directly, no aliasing
needed). `chrraces.db2` (the table with this column) was itself a
genuine 0-byte file locally -- looked up its real FileDataID from the
local community-listfile (1305311), dry-ran a `tact-fetch` check first
(confirmed genuinely missing, not already-local), then did the real
fetch (a real network call, confirmed with Luna first, since this
project's own global rules always require asking before outbound network
writes). Verified the fetched bytes were real WDC5 containing real
strings ("Dracthyr", "Human", "Troll", ...) before placing them, same
discipline as the previous entry's three files.

New `src/chrrace_db2.hpp`/`.cpp`: `parseModelBasename` (lowercase,
strip an optional trailing `_hd`, then split on a "female"/"male" suffix
-- "female" checked first since "male" is a literal substring of it) and
`deriveChrModelId` (exact case-insensitive match of the parsed race token
against every real `ChrRaces.ClientFileString`, joined through
`ChrRaceXChrModel` for the parsed sex). Built to the letter of Luna's own
instruction: "the filename matching for race + sex should be 1 to 1
only, not a fuzzy match... if user opens file 'dracthyrfemale' match it
to a dracthyr female, if the user opens file 'dwagon_biddies_69' that's
not gonna match dracthyr female no matter how hard you try, best effort
to help user while preserving correctness."

That "preserving correctness" requirement caught a real wrinkle before it
became a real bug: naively assumed (race, sex) would always resolve to
exactly one `ChrModelID`, but a SQL join over the real fetched data
proved otherwise -- Dracthyr male resolves to two genuinely distinct real
`ChrModelID`s (89, 127), a real alternate-form case (dragon form vs.
Visage form), not a data quality issue to paper over. `deriveChrModelId`
only returns a value when every match collapses to exactly one distinct
`ChrModelID` across the whole (race, sex) match set -- a genuine
ambiguity is reported by name (both candidate IDs printed) and left
unresolved, same "report and skip, never guess" discipline as every DB2
extras feature in this codebase already has, not a new pattern invented
for this one case.

Wired into `husk export --chr-model-id`, which now accepts the literal
value `auto` alongside a real numeric ID -- when given, it runs the
derivation above and feeds the result into the exact same
`defaultChoiceIdsForModel` path the previous entry's numeric-ID mode
already used, so nothing about choice resolution itself changed, only
how the `ChrModelID` gets there. Verified end to end against real local
data three separate ways before trusting it: a synthetic
`dracthyrfemale.m2` derives `ChrModelID` 89 (cross-checked by hand
against `chrracexchrmodel.db2` first); a synthetic `dracthyrmale.m2`
correctly reports the 89/127 ambiguity and skips, rather than silently
picking one; and a *real* character model already in the repo,
`bloodelffemale_hd.m2`, derives `ChrModelID` 20 -- independently
verified via SQL (race 10 = BloodElf, sex 1 -> ChrModelID 20) before
trusting the CLI's own output. A fourth real check, `dwagon_biddies_69.m2`
(Luna's own example of a filename that shouldn't match), correctly
produces "doesn't match the real character-model naming convention" and
skips.

3 new CLI-tier tests in `tests/test_cli_chrcustomization.cpp` (exact
match, genuine ambiguity, no match at all), needing a new synthetic
fixture builder (`buildChrRacesDb2`) for `ChrRaces`' real 2-field
(ID, string) shape -- same "exercise the real `db2::resolveFieldString`
path, not a mock" convention the previous entry's `buildOptionOrChoiceDb2`
established. One real bug caught while writing these tests, not shipped:
two of the three new tests initially failed because `chrcustomization::load`
returns `nullopt` when every one of its 5 tables comes back empty, and
these tests deliberately don't populate `ChrCustomizationOption`/`_Choice`
-- fixed by giving `ChrCustomizationElement` one throwaway row instead of
leaving all three legacy tables empty, so `load` succeeds and the
`--chr-model-id auto` logic is actually reached. `README.md`/`TODO/
TODO_correctness.md`/`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`/`TODO/
README.md`/`completions/husk.{bash,zsh}` all updated to match. Full suite
green, 645/645 (up from 642).

---

**2026-08-20 (later) — recovered last session's fetched `ChrCustomization`
tables, then implemented the real-names-to-geoset-selector mapping and a
player default-choice heuristic they unblock.** Picked up from a "what's
next" check that surfaced two stale claims: `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`
still said Stage 3 was blocked on `tact-fetch`'s CDN-fetch step being
unimplemented, and `~/dev/tact-fetch/README.md` still said the same thing
about itself — but `tact-fetch/CLAUDE.md`'s own Resume showed the fetch
pipeline fully built and live-verified as of 2026-08-19 (two real bugs
found and fixed that same day: `FetchOneFile` silently truncating an
encrypted tail block without `CASC_OVERCOME_ENCRYPTED`, and a `--locale`
override that only touched the wrong CascLib storage handle).

Confirmed the three files it fetched that day (`chrcustomizationoption.db2`/
`_choice.db2`/`_category.db2`, FileDataIDs 3384247/3450554/3526439) had
never actually been placed anywhere — `/media/luna/data/wow_export/
dbfilesclient/` still held genuine 0-byte placeholders for all three.
Found them by grepping the earlier session's own Claude scratchpad dir
(`.../c57e9564-.../scratchpad/`), which turned out to hold *several*
near-duplicate copies from different fetch passes — an `en_*`-prefixed
pass and an unprefixed pass, both from before the two bugs above were
fixed, decoded as unreadable garbage; a `final_*`-prefixed pass, fetched
after both fixes, decoded as real English strings ("Skin Color", "Hair
Style", "Body", "Face", ...). Verified which was which by content
(`strings` + grep for known real customization names), not by filename or
timestamp — the wrong choice would have silently reintroduced the
pre-fix truncation/locale bugs into husk's own local data. Copied the
`final_*` versions into `dbfilesclient/`, verified end to end via `husk
db2-export --dbd-dir reference/WoWDBDefs` (1148 real `ChrCustomizationOption`
rows, real `Name_lang` strings). Fixed `tact-fetch/README.md`'s stale
Status section to match its own `CLAUDE.md`.

With the data confirmed real and in place, implemented what it unblocks
(`TODO/TODO_correctness.md` #2's "enumerate real choices by name or pick
a default automatically" gap, and the name-mapping half of `TODO/
CHAR_TEXTURE_COMPOSITING_TODO.md` Stage 3): `chrcustomization_db2.hpp`/
`.cpp` now loads `ChrCustomizationOption`/`_Choice` alongside the
existing `_Element`/`_Geoset`/`_BoneSet` tables. Loading `Name_lang`
required real string resolution, which `db2table.hpp`'s existing
`readNamedColumns` never supported (scalar ints only, by its own module
comment) — added a sibling `readNamedStringColumns`, reusing
`db2::resolveFieldString` and the multi-section offset correction from
the previous entry below. That correction (`stringOffsetSectionCorrection`)
was `cmd_db2.cpp`-local, so it got promoted to a real public
`db2::stringOffsetSectionCorrection` (`db2.hpp`/`.cpp`) first, so both
readers share one implementation rather than duplicating the formula —
`cmd_db2.cpp` updated to call the shared version, no behavior change,
full suite re-verified green before adding anything new on top.

Two new pure functions on top of the loaded data: `namedChoicesForModel`
(every real Option/Choice name for a `ChrModelID`, paired with what it
resolves to via the existing `resolveChoice` — "the mapping of names to
the geoset selector" this session's task asked for) and
`defaultChoiceIdsForModel` (the lowest-`OrderIndex` choice per option —
investigated whether a sensible player default is derivable at all before
implementing it: no DB2 table states an explicit player default the way
`CreatureDisplayInfoGeosetData` does for creatures, but `OrderIndex`
matches the character-creation UI's own real display order, confirmed by
inspecting real Dracthyr data — "Ears" options 0-5 read "Short Fin",
"Long Fin", "Notched", "Long Spikes", "Wing", "Natural" in exactly that
order — so OrderIndex-0 is a reasonable heuristic default, documented as
husk's own heuristic rather than a client-verified fact, both in code
comments and in `--chr-model-id`'s own `--help` text).

New CLI surface: `husk export --chr-model-id <id>`. Given alongside
`--db2-dir`/`--dbd-dir` and no explicit `--customization-choice-ids`
(which always wins if both are given — an explicit pick from the caller
beats a heuristic), it auto-selects and resolves a default choice per
option, printing every real `OptionName -> ChoiceName` pair actually used
so the caller can see what was picked, not just a silent count. Verified
end to end against real local data: `ChrModelID` 89 (Dracthyr) resolves
45 default choices across all its real options, 7 with real geoset
selections (e.g. "Ears -> Short Fin", `OrderIndex` 0) — cross-checked by
hand against a direct SQL query over the same fetched tables before
trusting the CLI output.

New CLI-tier tests in `tests/test_cli_chrcustomization.cpp`: a new
synthetic fixture builder, `buildOptionOrChoiceDb2`, constructs a real
single-section WDC5 file with a real string table and real computed
string offsets — exercises the actual `db2::resolveFieldString` code
path end to end, not a mock, following this project's own "test the real
thing" convention. Covers the default-selection tie-break (lowest
`OrderIndex`, not lowest ID or declaration order), the explicit-choice-
always-wins-over-default precedence, and the "no Option/Choice rows for
this model" skip path. Regenerated `completions/husk.{bash,zsh}` for the
new flag. Updated `README.md`, `TODO/README.md`, `TODO/TODO_correctness.md`,
`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md` to match. Full suite green,
642/642 (up from 641 — 3 new test cases, 79 new assertions in this file
alone).

Unrelated cleanup done along the way, while tracking down where the
fetched files had actually gone: found 397 stale Claude session
scratchpad directories (~4.6 GB) accumulated across all projects under
`/media/luna/work/cache/tmp/claude-1000/` — nothing prunes these
automatically once a session ends. Removed all of them except the
then-current session's own directory, on explicit confirmation.

---

**2026-08-20 — `db2::resolveFieldString`'s multi-section string-offset bug,
fixed and verified.** Picked up `TODO/TODO_correctness.md` #4 (found and
deliberately left open the prior session).

Root cause, found by hand-deriving the real WDC2+ string-offset formula
against `documentation/wowdev-wiki/md/DB2.md`'s own "String Block" section
(WDC2 subsection) rather than guessing further from `reference/wow.export`
alone: a WDC2+ string offset is relative to a *virtual* blob the client
assembles at load time — every section's record data back to back, then
every section's string block back to back, not the real on-disk section
layout (which interleaves each section's own records immediately followed
by that same section's own string block). The wiki page is explicit that
this exact gap once shipped broken in a real Blizzard build (Patch 8.1.0
build 27826) before the client-side mitigation was understood. husk's old
`fieldAbsPos + rawValue` formula is *exactly* that virtual-blob formula
for the single-section case (the overwhelmingly common one — every
previously-tested local fixture), which is why this went unnoticed until
this session's real, English, multi-section `ChrCustomization*` fixtures.

Verified byte-for-byte against `test_data/db2/chrcustomizationcategory.db2`
(2 sections: 59 real records + 1 TACT-key-encrypted trailing record) by
hand-parsing the raw header/section/field bytes with `od` independent of
husk's own code, cross-deriving the correction term two ways (once via
`reference/wow.export`'s `_readRecordFromSection`/`_readString` algebra
reduced to concrete numbers, once via the wiki's own prose formula) and
confirming both agree with the real string-table bytes exactly: row 0's
`CategoryName_lang` is really `"Body"`, row 1 `"Face"` — a real,
sequential, sensible category list, replacing the previous silently-wrong
`"cessories"`/`"ries"` (bytes landing mid-string, never NUL-preceded,
provably wrong on inspection alone).

Fix: new `cmd_db2.cpp::stringOffsetSectionCorrection` (sums each other
section's record-data size or string-block size, signed by whether that
section comes after or before the one being decoded), applied to
`fieldAbsPos` before it reaches `db2::resolveFieldString`.
`decodeRecordValues` gained a `sectionIndex` parameter (needed to know
which section's own correction to apply) threaded through its 3 call
sites (`printRows`, `buildColumnPlan`, `writeFileTable`'s row loop) —
`resolveFieldString` itself untouched, the bug was entirely in what
absolute position was being handed to it, not in the heuristic that reads
from that position.

Also re-verified `chrcustomizationoption.db2` (also multi-section): real
English option names now decode correctly (`"Skin Color"`, `"Face"`,
`"Hair Style"`, `"Hair Color"`, `"Facial Hair"`). Noted but not fixed:
`chrcustomizationchoice.db2`'s field 0 still occasionally decodes as
garbage on rows whose real value is a small integer, not a string — a
separate, pre-existing heuristic false-positive in `resolveFieldString`'s
permissive high-byte/UTF-8-continuation acceptance rule, not this bug (the
rows that *are* real strings all decode correctly now).

**Same-session follow-up, chasing that noted-but-not-fixed garbage
down**: turned out to be a second, direct consequence of the fix above,
not an unrelated heuristic bug. WDC2+ treats `rawValue == 0` as an
explicit "no string" sentinel — real client code special-cases it before
ever computing a position (`reference/wow.export`'s `WDCReader.js`:
`if (ofs == 0) out[prop] = ''`) — but `resolveFieldString` never did.
That was harmless before this session's section-correction fix (a zero
offset always resolved back to the field's own record bytes, which are
reliably non-printable binary), but the correction term can now be
negative, which can send a zero offset into real binary data earlier in
the file. Confirmed via `od`: `chrcustomizationchoice.db2` row 1's field
0 has `rawValue == 0` and was resolving to file offset 17332 — genuine
`pallet_data` bytes (`2e 8c 7d ff 00 ...`) that happen to pass the
printable-byte heuristic (`.`, a UTF-8-continuation-range byte, `}`,
another continuation-range byte, then a real NUL) purely by coincidence.
Fixed with an explicit `rawValue == 0` early return in
`db2::resolveFieldString` (`src/db2.cpp`, ahead of the existing
bounds-check comment). Verified: every zero-offset row in
`chrcustomizationchoice.db2` now correctly falls back to raw int `0`
instead of garbage; `chrcustomizationcategory.db2`'s real decoded names
(`"Body"`/`"Face"`/etc.) unaffected. Full suite green, 641/641.
`TODO/TODO_correctness.md` #4 removed per this project's own "punch
list, not a log" convention — this file is the permanent record.

---

**2026-08-19 — real creature default-geoset selection, plus the full HD
character roster exported for a manual bug hunt.** Two independent asks.

Creature geosets: added `husk export --db2-dir/--dbd-dir
--creature-display-id`, resolving a real `CreatureDisplayInfoID` against
`CreatureDisplayInfoGeosetData.db2` (`src/creature_geoset_db2.hpp`/`.cpp`,
new files) into real *default* geoset selections attached as
`creature_enabled_geosets` skin extras (`gltf::Skeleton::
CreatureEnabledGeoset`, deliberately a separate field from the existing
player-character `EnabledGeoset`/`enabled_geosets` — different DB2 table,
different formula, and unlike the player-character chain this one *is* a
true default, no per-choice caller input needed). Formula
`(GeosetIndex+1)*100+GeosetValue` cross-checked against
`reference/wow.export`'s own `DBCreatures.js` before implementing, not
guessed. Verified end to end against real local data: `creature/gnoll2/
gnoll2.m2`, `CreatureDisplayInfoID` 137795 (13 real rows, chained by hand
through `CreatureDisplayInfo.ModelID`/`CreatureModelData.FileDataID`/the
listfile to find a real local model to export against), every one of the
13 resulting `geoset_id` values hand-checked against the source DB2 rows,
`gltf_validator` clean (0 errors/warnings). Full suite green, 641/641 (no
regressions in the unrelated player-character path). README/completions
updated to match.

HD character roster: exported all 23 real `*_hd.m2` player-character
models (every race/gender combo plus the human-transform variant) to
`.glb`, for a manual visual pass to derive sane per-race/gender geoset
defaults and hunt for further player-character bugs (this project's own
"buggiest" area per direct feedback) — output at `/media/luna/work/cache/
husk/hd_character_export/`. 22/23 succeeded; `scourgemale_hd` hit a real
non-finite (NaN) bone-2 translation keyframe, husk correctly refusing
rather than exporting garbage — not investigated further this session,
likely a local extraction corruption rather than a husk bug, worth a
second look if it recurs after a fresh extraction.

**Same-day follow-up — real geoset/choice *name* data investigated, found
genuinely blocked, then unblocked by a sibling project, then a real husk
bug found underneath it all.** Asked whether character geoset group names
and individual customization-choice names (e.g. "Hairstyle", a specific
style's real name) could be resolved from DB2 data. The real tables exist
(`ChrCustomizationOption.Name_lang`/`ChrCustomizationChoice.Name_lang`/
`ChrCustomizationCategory`) and `cmd_db2.cpp`'s `resolveFieldString` path
can read locstring columns (`husk db2-export` already does; correction to
this entry's own first draft — `db2table.hpp`'s typed-reader path is
scalar-integer-only by design and was never involved here), but all three
files are 0 bytes locally — the same gap `CHAR_TEXTURE_COMPOSITING_TODO.md`
already flagged. Tried a real re-extraction via `casc-tool extract` (into
the session scratchpad, never Luna's real `wow_export/` tree — writing
there directly would cross this project's own write-scope hard limit)
against Luna's real local WoW install: **not a re-extraction bug this
time** — `casc-tool` itself reports these three FileDataIDs' bytes were
never downloaded to the local install at all ("known but ... likely
optional/legacy content that was never downloaded"), unlike the earlier,
already-fixed `texturefiledata.db2`/
`ItemDisplayInfo*` truncation cases. Per Luna's own redirect, this
specific class of gap (manifest-known, bytes-never-downloaded) is exactly
what the sibling `tact-fetch` project (`~/dev/tact-fetch`) exists for —
confirmed by reading its own README — but its actual CDN-fetch step isn't
implemented yet, a deliberate no-op scaffold as of this check. Corrected
`CHAR_TEXTURE_COMPOSITING_TODO.md`/`TODO/README.md`'s prior "blocked on a
casc-tool re-extraction" framing to name the real blocker
(`tact-fetch`'s CDN fetch, not implemented) instead. No husk code changed
this follow-up — investigation and doc correction only.

**Same-day follow-up #2 — tact-fetch actually tried, two real bugs found
there, then a third real bug found in husk itself underneath them.**
Corrected by Luna: tact-fetch's README claiming the CDN-fetch step wasn't
implemented was stale — its `CLAUDE.md` (the actively-maintained doc) said
the full pipeline was real and built. Confirmed by running it directly
(`~/dev/tact-fetch/build/tact-fetch dry-run`/`fetch`, output always routed
to this session's own scratchpad, never `~/dev/tact-fetch/`'s or Luna's
own directories — the write-scope hard limit applied to a sibling project
too, not just to the wow_export tree).

First fetch (my own, no locale override) got real WDC5 data for all 3
FileDataIDs, but each was **truncated by exactly one trailing record**
(husk's own `db2-export` caught it: "need N bytes at offset == buffer
size"). Reported to `tact-fetch-24` (a peer Claude session on this
machine, via `SendMessage`) with the exact repro. They root-caused it:
`fetch.cpp` wasn't passing CascLib's `CASC_OVERCOME_ENCRYPTED` flag on the
fetch handle — casc-tool had already hit and fixed the identical symptom
(its own 2026-08-16 CHANGELOG entry). Second, independent bug in the same
report: Luna's own attempt at a locale override (pointing me at
`~/dev/tact-fetch/development/locale_override/export/_unresolved/`)
produced byte-identical Korean output to the unforced fetch — tact-fetch-24
found their earlier override hack only forced locale on the *local-resolve*
handle (handle A), while the actual fetch handle (B) was hardcoded to
`CASC_LOCALE_ALL` regardless. Both fixed properly and committed (`17eb073`
on the tact-fetch side) — `CASC_OVERCOME_ENCRYPTED` now unconditional, plus
a real `--locale <code>` CLI flag threaded through to handle B. Re-fetched
clean afterward (`--locale enUS`) and confirmed real English text, no
truncation, byte-for-byte reproducible against a second independent
fetch run.

With real, complete, English data finally in hand, found a **third, real
bug — this one genuinely husk's own**, while eyeballing the decoded
names: `db2::resolveFieldString` (`src/db2.cpp`) misresolves some
string-field offsets. Confirmed at the byte level, not guessed:
`ChrCustomizationCategory`'s row 0 `CategoryName_lang` decodes as
`"cessories"`, row 1 as `"ries"` — real string-table bytes at file offset
1415 read `...Face\0Accessories\0Hair\0Mark...`, so `Accessories` genuinely
starts at 1424 (right after a real NUL), but husk's computed `stringPos`
for row 0 landed at 1426 (2 bytes in) and row 1 at 1431 (7 bytes in) — a
*different* wrong offset per row, ruling out a simple constant bug in the
shared `fieldAbsoluteFilePos` base (independently re-verified correct via
raw `od` inspection, along with the raw field values themselves). Root
cause not found this session — `db2.cpp`'s single-addition formula versus
`reference/wow.export`'s more involved `outsideDataSize + absoluteRecordOffs
+ dataPos + ofs` is the lead for whoever picks this up next. Full writeup
with the exact repro: `TODO/TODO_correctness.md` #4. Real fixture data
(the final, clean, `tact-fetch`-sourced enUS files) now lives in the repo
at `test_data/db2/chrcustomization{option,choice,category}.db2` rather
than only in session scratch, specifically so this bug stays reproducible
without needing to re-run any of the above.

Deliberately not fixed blind — `resolveFieldString` is `cmd_db2.cpp`'s
only string-decode path, shared by every locstring/string column
`husk db2-export` has ever touched, so a wrong guess here risks a much
wider regression than the one table this session actually needed. No
currently-shipped husk feature reads strings through this path today
(every typed DB2 reader in `src/` goes through `db2table.hpp`'s
scalar-integer-only `readNamedColumns` instead, unaffected) — this is a
latent bug, not (yet) a live one, which is exactly why it was safe to stop
and document rather than rush a fix.

Two real process points worth keeping: (1) `husk export`'s `--skin auto`
loses its same-basename-numbered-scan fallback when `--lod` is passed
explicitly (even `--lod 0`, the same value auto already defaults to) —
found exporting `bloodelffemale_hd.m2` (no `<FileDataID>.skin` present
locally, only the numbered `bloodelffemale_hd00.skin`); worked around by
dropping `--lod` from the export script, real gap now tracked as
`TODO/CLEANUP_TODO.md` #4. (2) Per direct instruction, stopped hand-rolling
bash loops for the batch export and wrote a real `tools/
export_hd_characters.nu` script instead (husk itself has no batch/
directory export mode — `CLEANUP_TODO.md` #3, new) — this project's
Nushell-for-tooling convention (global `CLAUDE.md`) hadn't actually been
applied anywhere in `tools/` yet before this session; this is the first
`.nu` file in the repo.

---

**2026-08-16, same-day follow-up — object-skin resolution actually fixed,
via local fallback, not DB2**: Continued the correctness investigation
after `item.db2`/`itemappearance.db2`/`itemmodifiedappearance.db2` got
re-extracted (confirmed: each had been missing exactly one trailing
record, 88/132/120 bytes, same shape as every other truncation this
project has hit). Used the real Item chain to check whether it could
filter out the bad `ItemDisplayInfoID` (113510, the "staff for a helmet"
case) — it couldn't cleanly: 113510 turned out to be **real, current
data** for an actual two-handed staff (`ItemID` 79340, confirmed
`ClassID=2/SubclassID=10/InventoryType=17`), not orphaned junk. Built a
real `Item.InventoryType` slot filter (a path→expected-slot table for
`item/objectcomponents/` directories/prefixes, `item_display_inventory_
type` DB2 join) that cut the DB2 chain's raw 50,058 candidates down to
2,204 "slot-verified" ones — eliminating cross-category errors, but
spot-checking 15 random survivors found same-slot cross-item errors
still common (a cloth Ardenweald helm resolving to an unrelated Scarlet
Crusade plate set's texture) — confirmed `ModelResourcesID` collisions
happen within one equip slot too, not just across categories. Concluded
the DB2 chain is too collision-prone to trust as primary at any
disambiguation depth tried so far; kept disabled.

Pivoted to the actual fix, per direct instruction ("do the local
fallback... i don't want this to end up... wrong capitalization... too
loose of a check"): extended husk's own existing fuzzy-basename matcher
(`export_texture_resolution.cpp`'s `scanFuzzyTexturePool`, which already
had one precedent fallback tier for `"_sdr"`-suffixed models) with a
new, real race/gender-suffix-stripping tier — derived from actual corpus
frequency counts (grepped every real `item/objectcomponents/*/*.m2`
filename, kept only codes with hundreds+ real occurrences), not guessed,
and matched case-insensitively throughout (same discipline as the
CRLF-listfile lesson from earlier this session). Verified against both
real cases that broke the DB2 chain: the helm case now finds its sole
real texture directly; the chest_mail case finds all 5 real recolor
variants and embeds them as `alternate_textures` extras (husk's existing
ambiguous-candidate mechanism, reused for free) — the "swap in Blender"
behavior asked for. New regression test in
`tests/test_integration_weapons.cpp`. All 639 tests pass.
`TODO/KNOWLEDGE_BASE_DESIGN.md` rewritten to record this as the actual
fix; the DB2 knowledge-base infrastructure stays built but inert/
diagnostic-only, not load-bearing. Next: run the real full-corpus render
(cleared to `trash/` earlier this session for a clean run).

---

**2026-08-16, same-day follow-up — object-skin DB2 resolution found
confidently wrong, disabled**: Direct request to add a `textures` table
to the knowledge base (done: `db2-build` now ingests `.blp`/`.png`
listfile paths too, `husk export --knowledge-db` resolves both the
texture ID and its path with no separate `--listfile` load needed) led
into a "what about materials" question, which led to checking whether
`ModelType_0/1` (a candidate texture-type column) meant anything — it
doesn't; WoWDBDefs itself marks it `ModelType?`, unconfirmed, and real
values (`1096109`, `1096189`, ...) don't match any small texture-type
enum. That in turn prompted checking the *existing* object-skin
resolution against `reference/wow.export`'s own real, working source
(`DBItemDisplays.js`/`DBItemDisplayInfoModelMatRes.js`) rather than
guessing further — and found the shipped chain was wrong:
`ItemDisplayInfo.ModelMaterialResourcesID_0/1` (what it used directly)
is, per wow.export's own code, only ever an existence check, never
dereferenced for the actual texture. Rewrote the join to match
wow.export exactly (`ModelResourcesID_0` only, via
`ItemDisplayInfoModelMatRes`, keeping every candidate per model instead
of collapsing to one — real recolor-variant ambiguity, not a bug).
**This did not fix it.** Re-verified against `helm_leather_pvpdruid_b_02_
scm.m2`: `ModelFileData→ModelResourcesID` (6706) confirmed solid across
all 22+ real race/gender variants of this exact helm, but every
`ItemDisplayInfoID` sharing that `ModelResourcesID` resolves — via the
wow.export-matching join — to a `staff_2h_pandariatradeskill_c_03*`
texture, a different item category entirely, while the real texture
(`helm_leather_pvpdruid_b_02.blp`) sits in the same directory under an
obvious name and isn't reachable from this chain at all. Root cause as
far as verified: `ItemDisplayInfoID` 113510 is internally
self-contradictory (mesh says helm, its own material says staff), no
flag explaining it — likely orphaned DB2 data a real item never
references, filterable via `Item`/`ItemAppearance`/
`ItemModifiedAppearance` (the chain this session's earlier
`EXPLORATION_TODO.md` work wrongly judged unnecessary), but those three
files are the ones already found truncated locally earlier this
session — can't verify further without a re-extraction.

Given the failure mode (confidently wrong, not just unresolved — worse
than doing nothing, and wrong even where a correct local-basename match
was trivially available), disabled `--knowledge-db` in
`render_sample_driver.py` rather than ship it. A full corpus render was
started, then stopped mid-run (per direct instruction, correctness takes
priority) once this was found — old renders cleared to `trash/` for a
clean re-run once fixed. `TODO/KNOWLEDGE_BASE_DESIGN.md`/
`EXPLORATION_TODO.md` rewritten to state this plainly rather than the
earlier session's overclaimed "resolves and renders correctly" (which
had only ever checked that *something* embedded, never that it was
correct — the real process failure underlying this whole detour).
Also added a default `os.nice(10)` to `render_sample_driver.py`'s
`run_render_pipeline` (background-by-design, not meant to compete for
foreground resources) and moved the knowledge-base's canonical location
from a hardcoded `/media/luna/work/cache/...` path to `$XDG_CACHE_HOME`
(falling back to `~/.cache`), grouped under its own `husk/` subdirectory
rather than loose in a large shared multi-app cache dir.

---

**2026-08-16, same-day follow-up — knowledge-base design pass and real
implementation**: Direct feedback after the small `--object-skin-texture-
id`/Python-script version (previous entry): the resolution work was
sprawling across too many independent, direction-specific pieces (a C++
DB2 reader for one feature, a Python DB2-join script for another, a
gatekeeping scan silently dropping candidates), and separately, that same
gatekeeping bug was caught live: `helm_leather_pvpdruid_b_02_scm.m2`'s
already-rendered `.webp` had no texture, traced to `unfillable_texture_
task.py`'s `analyze()` returning "nothing to report" the moment *any*
slot resolved, never checking whether *every* slot did — this model's
unrelated placeholder texture resolved fine, silently hiding its real,
still-unresolved `object_skin` slot from the whole downstream pipeline.

Wrote `TODO/KNOWLEDGE_BASE_DESIGN.md` (a real design pass, not more
patching): husk should own a single, verified SQLite "knowledge base,"
built once from local DB2 + listfile data, instead of re-deriving answers
per-feature/per-script. Confirmed four open questions with Luna directly
(location: local-only, rebuilt on demand; existing `--char-layout-id`
flags: left alone for now; staleness: stamped and detected; scope: build
the general table-driven ingestion mechanism now, not just one table).

Implemented: `husk db2-build --db2-dir --dbd-dir --listfile -o <out.
sqlite>` (`src/cmd_db2.cpp`, reusing `db2-export`'s existing `loadOneFile`/
`writeFileTable`, not reimplementing DB2 decode) ingests `ModelFileData`/
`ItemDisplayInfo`/`TextureFileData`, a `models` table (FileDataID → path
from `--listfile`), the resolved `model_object_skin_texture` join (real
SQL — an unindexed first attempt didn't finish in 2 minutes, same lesson
as the earlier manual SQL work; indexed, ~4s), and a `_meta` staleness
stamp. Real run: **54,188 resolved model→texture mappings** — broader
than the old pipeline's 4,216, precisely because it's no longer gated by
the buggy scan. `husk export --knowledge-db <path>` resolves each model's
own object-skin texture automatically via the knowledge base, no per-file
flag needed (`--object-skin-texture-id` kept as a manual override).
`resolve_object_skin_textures.py`/its stale CSV deleted (moved to
`trash/`); `render_sample_driver.py` passes `--knowledge-db`
unconditionally. `unfillable_texture_task.py`'s gatekeeping bug fixed
directly too (per-slot resolution, not per-file) even though it's no
longer load-bearing for this chain. Verified live: the exact file that
exposed the bug now resolves and renders correctly with zero per-file
wiring; all 638 existing tests still pass.

Full corpus re-render still not run to completion — `TODO/
KNOWLEDGE_BASE_DESIGN.md`/`EXPLORATION_TODO.md`'s next step.

---

**2026-08-16, same-day follow-up — small implementation, chain wired for
real render** (superseded by the knowledge-base pass above, kept for the
narrative): Implemented `EXPLORATION_TODO.md`'s remaining step: a new
`husk export --object-skin-texture-id <fdid>` flag
(`src/commands.hpp`/`cmd_export.cpp`/`export_materials.hpp`/`.cpp`) fills
an already-resolved texture FileDataID into any `type=2`/object_skin
texture slot whose own `fdid` is 0, by overriding `fdid` before the
existing deterministic `texturesDir`/`--listfile` embed logic runs —
husk itself does not walk the DB2 chain. New test in
`tests/test_integration_weapons.cpp`. The chain resolution lives outside
husk in a new `tools/corpus_scan_tasks/resolve_object_skin_textures.py`
(same `ModelFileData`/`ItemDisplayInfo`/`TextureFileData` joins as the
manual SQL verification above, via `husk db2-export`), writing
`corpus_reports/object_skin_texture_resolution.csv` — 4,216 rows,
matching the earlier quantification exactly.
`render_sample_driver.py` loads that CSV and passes
`--object-skin-texture-id` automatically per matching file. Verified
against a real file end to end
(`chest_mail_chainmailset_b_01_go_f.m2` → texture 422759 embedded, 1
material with an embedded texture where there was 0 before), then
smoke-tested through the actual export+Blender render pipeline on 3
real corpus files: 2 succeeded with real animated `.webm` output, 1
failed on an unrelated pre-existing `.skin`-resolution gap. Full
`tools/full_render.py` run still pending — not run to completion this
session.

---

**2026-08-16, same-day follow-up — `texturefiledata.db2` re-extraction
verified, chain fully closes**: casc-tool re-extracted
`texturefiledata.db2` (was 0 bytes, now 3,009,910 bytes, 214,436 rows,
parses cleanly). Re-ran the final hop of the chain mapped in this same
day's earlier `EXPLORATION_TODO.md` entry
(`MaterialResourcesID -> TextureFileData.FileDataID`) against all 4,220
files that had already resolved to an `ItemDisplayInfo` row: **4,216
(99.9%) now resolve to a real texture FileDataID** (4 stragglers share
one `MaterialResourcesID`, 77020, with no `TextureFileData` row at all —
not investigated further, likely cut content). Checked disk presence for
all 1,552 distinct resolved texture FileDataIDs **via their real
listfile path, not filename-by-ID** (a real early mistake this session —
texture files are stored under listfile paths, not `<FileDataID>.blp`,
same convention as models; caught and corrected before it produced a
wrong answer): **all 1,552 already exist locally**. No further casc-tool
ask needed for this bucket. Net: 4,216 of the original 4,733
`replaceable_only` files are now fully resolvable end to end, textures
included, entirely from data already on disk. `EXPLORATION_TODO.md`
trimmed again to drop the now-closed casc-tool-ask step, leaving only
"implement the chain in husk" and "re-render" as open work.

---

**2026-08-16, new session, `EXPLORATION_TODO.md` follow-up — real
`.m2` → texture DB2 chain mapped and quantified**: Picked up
`TODO/EXPLORATION_TODO.md`'s two open questions from earlier the same
day's `db2::Section::recordsAvailable()` fix. Did a full `husk
db2-export --dir /media/luna/data/wow_export/dbfilesclient --dbd-dir
reference/WoWDBDefs` (798 tables, ~6.5M rows) into a scratch SQLite
database for real-data exploration.

Question 1 (DB2 consumer audit): checked every DB2 consumer in `src/`
(`db2.cpp`/`db2table.cpp`/`chrmodel_db2.cpp`/`chrcustomization_db2.cpp`/
`cmd_db2.cpp`) — all already route through `db2table.cpp`'s or
`cmd_db2.cpp`'s own `recordsAvailable()` calls; no other consumer had
the bug the earlier fix corrected.

Question 2 (map and quantify the `.m2` → texture chain for the 4,733
`replaceable_only` files): the chain turned out shorter than
`EXPLORATION_TODO.md`'s original guess, and one assumption in it was
wrong. Real confirmed chain: `.m2` FileDataID → `ModelFileData.db2`
(`FileDataID` → `ModelResourcesID`, 131,086 rows, parses cleanly, not
TACT-gated) → `ItemDisplayInfo.db2` (`ModelResourcesID_0`/`_1` matches
directly, carries `ID`/`ItemDisplayInfoID` and
`ModelMaterialResourcesID_0`/`_1` in the same row) →
`TextureFileData.db2` (`MaterialResourcesID` → `FileDataID`, the real
texture). `Item.db2`/`ItemAppearance.db2`/`ItemModifiedAppearance.db2`
are not needed at all — the original plan's assumed path through them
was unnecessary once `ModelFileData.db2` was found as a direct reverse
lookup. `ComponentTextureFileData.db2` (originally guessed as the
`MaterialResourcesID` target) turned out to be a different, unrelated
table — its `ID` column is itself a `FileData::ID` for
race/class/gender-keyed character-customization textures, no
`MaterialResourcesID` column at all (confirmed via
`reference/WoWDBDefs/definitions/ComponentTextureFileData.dbd`); the
real target, `TextureFileData.MaterialResourcesID`, was confirmed via
`ChrCustomizationMaterial.dbd`'s own `int<TextureFileData::
MaterialResourcesID>` FK annotation.

Verified against a real file:
`item/objectcomponents/collections/chest_mail_chainmailset_b_01_go_f.m2`
(FileDataID 4418249, from `community-listfile.csv`) →
`ModelFileData.ModelResourcesID` = 63786 → `ItemDisplayInfo.ID` =
704236, `ModelMaterialResourcesID_0` = 74610 (no
`ItemDisplayInfoMaterialRes`/`ItemDisplayInfoModelMatRes` override rows
for this ID — checked, both empty, so the base row's own
`ModelMaterialResourcesID_N` is what applies). Final hop blocked: real,
locally-present `texturefiledata.db2` is a genuine **0-byte file** —
`db2-export` reports "truncated WDC5 magic: need 4 bytes at offset 0,
buffer is 0 bytes." Same failure category as the 2026-08-16
`ItemDisplayInfo`-family truncation fix earlier the same day, just
total rather than partial truncation.

Quantified across all 4,733 `replaceable_only` files (SQL joins against
the scratch `db2.sqlite`, `corpus_paths` ⋈ `listfile_raw` ⋈
`ModelFileData` ⋈ `ItemDisplayInfo`): all 4,733 have a listfile
FileDataID and a `ModelFileData` row (100%); 4,220 (89.2%) further
resolve to at least one real `ItemDisplayInfo` row, and every one of
those 4,220 has a nonzero `ModelMaterialResourcesID`, i.e. is
structurally resolvable all the way to the last hop. 513 (10.8%) don't
match any `ItemDisplayInfo` row at all — not investigated further this
session, may be cut/unused content or a different mechanism entirely.
**Answer to question 1: the `recordsAvailable()` fix alone does not yet
unlock any of the 4,733 — the chain is real and mostly resolvable
(4,220/4,733), but every one dead-ends at the same 0-byte
`texturefiledata.db2` wall, a fresh casc-tool re-extraction ask, not a
husk-side gap.**

Also re-checked step 6 (the 144 excluded `character/` files, `comm -23`
diff between `full_corpus_file_list.no_objectcomponents.no_particle_
only.txt` and its `.no_character.txt` variant, confirms 144):
`chrcustomization.db2`/`chrcustomizationcategory.db2`/
`chrcustomizationchoice.db2`/`chrcustomizationoption.db2`/
`chrcustomizationreq.db2` are all still genuinely 0 bytes locally,
unchanged from `CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 3 finding —
confirmed rather than assumed, per the plan's own instruction not to
assume.

`EXPLORATION_TODO.md` rewritten to drop the now-closed exploration
steps (per this project's own "punch list, not a historical record"
convention for that file) and keep only the real remaining work: the
`texturefiledata.db2` casc-tool ask, implementing the chain in
`--db2-dir`, and a final re-render once both land.

---

**2026-08-16, new session, objectcomponents white-render investigation →
full-corpus unfillable-texture scan → real CASC re-extraction → render
tooling rebuild**: Started from a direct observation ("most
objectcomponents meshes don't have textures") during a full-corpus render.
Investigated interactively: item/objectcomponents/collections-style body-
fitted armor uses a `type=2` ("object_skin") replaceable texture slot with
`file_data_id=0`, filled by the live client from CharComponentTexture
LayoutsID/ItemDisplayInfo DB2 data at runtime, not a standalone file — of
~15 race/gender variants of the same item, only the ones whose local CASC
extraction happened to also dump loose, non-FileDataID-named skin-overlay
`.blp` files resolve via husk's fuzzy same-basename fallback.

Built `tools/corpus_scan_tasks/unfillable_texture_task.py` to quantify
this across the whole corpus, with two real corrections along the way
(both from direct, sharp feedback, not self-caught):

1. First version shelled out to a real `husk export` per file for an
   exact embedded-texture count — correct but did a full mesh/skin
   build + image embed + `.glb` write for all 130k files, a ~10-minute
   job turned multi-hour. Rewritten to the cheap shape (`husk info`,
   header-only, plus a directory-listing-based fuzzy-match check
   mirroring `export_texture_resolution.cpp`'s real logic) —
   `missing_texture_task.py`'s own established pattern.
2. `BATCH_SIZE=8` (copied from `missing_texture_task.py` without
   measuring) interacted badly with `AdaptiveConcurrency`: one slow file
   in a batch stalls its 7 siblings, so the controller sees one "slow
   completion" gating 8x the real latency. Real incident, not
   hypothetical: `_has_fuzzy_candidate()` called `Path.glob()` directly
   against `item/objectcomponents/collections` (110,337 files, the
   corpus's largest directory) at ~70ms/call — real full-run window
   oscillation 10↔13, ~4x slowdown. Root-caused and fixed (a per-
   directory `os.scandir` cache via `functools.lru_cache`, ~70ms once
   per directory not once per file), then `BATCH_SIZE=8` re-benchmarked
   as genuinely faster than `1` (0 backoffs either way) once the real
   variance source was gone — the fix wasn't "always use batch=1," it
   was "measure per-task before batching, and fix the actual bottleneck
   instead of just avoiding batching." Both lessons saved to memory
   ([[feedback_batch_size_and_hard_data]]) since they're project-general,
   not scan-specific. `tools/CORPUS_SCANS.md` written up as a real how-
   to/gotchas doc for the scan framework, both incidents included with
   hard numbers.

First full scan: 107,737/130,576 files flagged, 103,004 with a real
missing FileDataID (18,747 distinct IDs), 4,733 using only replaceable
slots (no standalone file possible). Handed the 18,747-ID list to the
casc-tool project as a concrete re-extraction target; they built a new
`extract-batch --from-list` feature and landed 18,742/18,747 files (3.29
GB) under their real listfile-resolved paths (e.g. `character/draenei/
male/draeneimaleskin.blp`), 5 confirmed genuinely unrecoverable from that
storage.

Rescanning after the extraction landed came back byte-identical to
before — real bug, not a data problem: `unfillable_texture_task.py` had
silently dropped husk's real listfile-resolution tier
(`export_materials.cpp:437-456`'s three-tier order: literal FileDataID →
`--listfile` real-name lookup → fuzzy same-basename) when it was
rewritten away from the `husk export`-based version that *did* pass
`--listfile` — never added back. The extraction had genuinely worked
(confirmed directly: `armorreflect4.blp`, mtime matching the extraction
window exactly, resolves FileDataID 1360817), the scan just never checked
the tier that would have seen it. Fixed (added the listfile tier, ~1s
parse cost once per worker process, negligible against a ~30s full scan)
and reran: **4,891 flagged** (down from 107,737) — 158 real remaining
extraction-gap files (18 distinct IDs, matching the 5 unrecoverable plus
a handful more), 4,733 structurally replaceable-only (unchanged, as
expected — no amount of extraction fixes those). Verified end-to-end with
a real 3-file render smoke test: a file needing today's extraction now
renders fully textured; a replaceable-only file correctly still renders
partially white (expected, not a regression).

Rebuilt the render exclusion list and CASC report from the corrected
scan (125,545 files now includable, up from 22,708; 18 real missing
FileDataIDs, not 18,747) and built `tools/full_render.py`: fresh corpus
discovery every run (never a stale snapshot) plus a `.renderignore` file
(gitignore-style glob patterns, unit-tested, real per-segment matching
so `*` doesn't cross `/` the way plain `fnmatch` would) for exclusion,
replacing the scan-result-subtraction approach that made the listfile
bug's damage hard to see in the first place. `render_sample_driver.py`'s
render loop factored out into `run_render_pipeline(paths, render_dir)` so
both entry points share the exact same tested pipeline. Default
`.renderignore`: just `character/` (confirmed structurally DB2-dependent
for all 144 real files, same mechanism as the item object_skin gap).

Follow-up investigation, same session, directly requested ("explore why
we can't fill this given DB2 access, same as the geoset-picking
workaround if it's table-empty"): real `husk db2-export --dir
dbfilesclient/ out.sqlite --dbd-dir reference/WoWDBDefs` batch export
(795 tables, 6M rows) confirmed the item-texture DB2 chain
(`ItemDisplayInfo`/`ItemDisplayInfoMaterialRes`/`ItemDisplayInfoModelMatRes`)
is a *different* gap than `CHAR_TEXTURE_COMPOSITING_TODO.md`'s already-
documented 0-byte `ChrCustomizationOption` chain: all three files are
multi-megabyte and genuinely present, but each is truncated a few dozen
bytes short of a complete final record (husk's own real error: `db2:
truncated section.records: need 135 bytes at offset 2528913, buffer is
2528913 bytes`) — reads as an interrupted download, likely fixable by a
small targeted re-extraction, not a "data doesn't exist" dead end.
`CharComponentTextureLayouts.db2` is a third, harder case: parses clean
but yields only 4 real rows because husk reports skipping a TACT-
encrypted section in that file — needs an updated decryption key, not
just a re-download. Written up in `CHAR_TEXTURE_COMPOSITING_TODO.md`
(the file's existing Stage 5 already specs the right workaround —
`tools/husk_blender_geoset_mask.py`'s "expose every real candidate,
let a human pick" pattern, same shape as its geoset dropdown — but
recommended trying the cheap re-extraction fix first, since it likely
closes most of the gap on its own).

**Same session, continued: the "needs an updated TACT key" diagnosis
above was wrong — real root cause found and fixed, no crypto needed.**
Direct request to explore further ("the key is known and public, this
is a husk-side gap"). Cloned `reference/DBCD` (wowdev's own C# DB2
reader) to check for a reference decrypt implementation — found neither
it nor `reference/wow.export`'s `WDCReader.js` implements DB2-internal
Salsa20 decryption at all; both just check whether a `tact_key_hash`-
bearing section's bytes are all-zero and skip only if so. That was the
real clue: checked every real encrypted-flagged section in the local
corpus (126 total) and found **111 were already non-zero, readable
plaintext** — a real CASC extraction (CascLib, given the community TACT
key) already decrypts these sections before the file lands on disk; only
15 (all inside the one already-truncated `ItemDisplayInfo.db2`) were
genuinely still all-zero. husk's own `db2.cpp` had a real bug: it
treated `tact_key_hash != 0` as "opaque, unreadable" unconditionally,
never checking whether the bytes it actually had were still ciphertext.
Real Salsa20 spent before landing on this: ported CascLib's own
`CascDecrypt.cpp` implementation to Python, cross-verified byte-for-byte
against `pycryptodome` (installed into `tools/venv` via `uv pip install`,
no flake change needed) to rule out a primitive bug, tried five IV-
derivation hypotheses against a real 32KB encrypted payload — all ruled
out, which is what motivated checking the reference implementations
instead of continuing to guess blind. Fixed at the real root instead:
`db2::Section::recordsAvailable()` (`db2.hpp`) is now the one canonical
"can I actually read this section" check, replacing four separate
`tactKeyHash != 0` skip sites across `db2.cpp`/`db2table.cpp`/
`cmd_db2.cpp`. A second, same-shape bug found alongside it: a
`relationshipMap` region that reads as all-zero used to throw
`ParseError` and abort the *whole file's* read over one missing region;
now degrades to "no relationship data for this section" instead. Both
covered by new synthetic-buffer regression tests in `tests/test_db2.cpp`
(637 cases now, was 634, all passing). Verified against real data:
`CharComponentTextureLayouts.db2` now exports all 5 real rows (was 4);
the full corpus DB2 batch export gained 446,719 rows it was silently
dropping before (798 tables now populated, was 795); casc-tool
independently confirmed and fixed the separate `ItemDisplayInfo*`
truncation via a zero-pad-to-declared-size workaround the same session,
closing that half of the gap too — both real blockers behind
`CHAR_TEXTURE_COMPOSITING_TODO.md`'s DB2 investigation are now fixed.
`CHAR_TEXTURE_COMPOSITING_TODO.md` rewritten to reflect the corrected,
resolved outcome rather than the wrong initial diagnosis.

**2026-08-15, new session, Mod/Mod2x multiply-blend compositing implemented**:
Picked up `TODO/MOD_BLEND_COMPOSITING_TODO.md` where the prior session left
off (technique confirmed, formula not yet designed) and implemented
`apply_multiply_blend_compositing(scene, materials)` in
`tools/husk_blender_geoset_mask.py`, wired into both `render_glb.py`'s
`main()` and the interactive script's own `main()`.

Real design churn, two dead ends corrected along the way by direct
correction, both worth recording precisely since they contradict the
prior session's own optimistic framing:

1. First attempt tried to literally reconstruct the true occluded
   background behind a Mod material using two complementary Cryptomatte
   passes (`bg` materials vs `mult` materials), on the theory that a
   BLEND-mode surface's opaque-pass G-buffer still carries the occluded
   surface's data even though it's covered in the final beauty. Built a
   real headless test harness (`blender --background`, synthetic two-
   plane scene, Cryptomatte `Image`/`Matte` outputs sampled directly) to
   verify this before trusting it. Result: at alpha=1, Cryptomatte's
   `Matte` for the occluded material reads exactly 0 (no occlusion
   recovery at all, confirmed both EEVEE and Cycles) — informationally
   correct, since alpha=1 genuinely discards that data from the render.
   At alpha=0.5, `Matte` correctly reads ~0.5 (real per-material coverage,
   confirmed reliable), but `Image` (its color) recovers a ~50/50 blend of
   both layers' colors, not the pure occluded background. So single-pass
   Cryptomatte reconstruction of "what's behind" doesn't work, at either
   alpha value, for a different reason at each.
2. Pivoted to a two-pass render design (Holdout each layer separately,
   composite the two static plates) to sidestep the reconstruction
   problem entirely — directly overruled: "we do not add multiple
   renders period." Prompted to reconsider what the real WoW blend
   equation actually needs: checked the real GL blend factors for Mod/
   Mod2x (`(DST_COLOR, ZERO)` / `(DST_COLOR, SRC_COLOR)`) and found alpha
   isn't read by either blend equation at all — a fact neither of the
   first two attempts had accounted for.

Given screenshots of Luna's own working Cryptomatte compositor graph
(Matte-driven `Mix`/`Darken` nodes applied directly to the beauty pass, no
reconstruction, no second render), the final design: operate directly on
the scene's own existing beauty pass (Blender's default alpha-over import
already produces that for free), darkened by each material's own flat
tint color (`_material_tint_color` — Base Color texture average, or the
flat factor for untextured materials; handles both the Principled-BSDF and
KHR_materials_unlit/Emission import shapes, same dual-path
`fix_additive_materials` already established for modes 3/4), mixed onto
the beauty pixel by that material's own Cryptomatte `Matte` (real
per-material coverage, correct for partial-alpha too). Mod and Mod2x chain
(Mod first, Mod2x reading Mod's own output) so both can coexist in one
scene. Verified headlessly against a synthetic scene (both engines,
alpha=1 and alpha=0.5 — output matches the hand-computed
`mix(matte, beauty, beauty*tint*scale)` formula almost exactly), then
against two real corpus files end-to-end through the actual
`render_glb.py` pipeline: `creature/crab2alliance/crab2alliance.m2` (Mod,
mode 5, textured material) and `creature/rockflayer/rockflayercrystal.m2`
(Mod2x, mode 6, untextured material — exercises the flat-factor fallback
path), both animated renders (19 and 27 frames), no errors, "N Mod/Mod2x
material(s) multiply-composited" reported correctly.

Known, accepted limitation carried forward in the code's own doc comment:
for a fully opaque (alpha=1) Mod/Mod2x material, its own beauty pixel is
already just its own color (nothing behind it survives Blender's default
alpha-over import at alpha=1), so this darkens that color by itself rather
than whatever's really behind it. Not byte-exact for that specific case,
but the common real case this blend mode is used for (partial-alpha tint/
grime/glow overlays, not full opaque replacement) is unaffected.
`TODO/MOD_BLEND_COMPOSITING_TODO.md` updated to close out; nothing left
open there.

**Same session, continued — three more tasks run together** (one
background investigation agent in parallel with two implementation tasks
done directly): (1) `tools/live_gallery`'s standalone three.js viewer
gained real skeletal animation playback (`THREE.AnimationMixer`, clip
dropdown, play/pause/loop), a real JS port of the Blender-side texture-
transform/tint/fade curve-eval logic (closing the "no JS-side port at all"
gap `ANIMATED_TEXTURE_EFFECTS_TODO.md` used to describe — confirmed
three.js's `GLTFLoader` merges material `extras` straight into
`material.userData`, no Blender-style IDPropertyGroup unwrapping needed),
mesh/material picking (click to inspect `blend_mode`/`pixel_shader`/etc.),
and lighting-intensity/exposure sliders. Structurally verified (element-ID
wiring, extras JSON shape cross-checked against `gltf_mesh.cpp`, served
correctly through a real local server via `uv run` — this project's flake
deliberately provides no bare Python/Node interpreter, `uv run`/`uv sync`
is the sanctioned way to run any plain-Python tool here, not `python3`
directly) but **not yet visually confirmed in an actual browser** (no
headless browser available in this environment). `ANIMATED_TEXTURE_
EFFECTS_TODO.md` also trimmed back to a real punch list per direct
correction — it had accumulated a full "Background" narrative describing
already-closed work, which is exactly what `CLAUDE_HISTORY.md` is for, not
a TODO file.

(2) A background investigation agent tackled `PIXEL_SHADER_FORMULAS_TODO.md`
in parallel, explicitly instructed not to trust the wowdev.wiki text
uncritically (direct correction: "you can't know that the wiki is
correct"). Found the TODO's own previously-flagged factor-of-2 discrepancy
(wiki vs. `reference/wow.export`'s GLSL for `Combiners_Opaque_
Mod2xNA_Alpha`) resolves in wow.export's favor — a second, independent
wowdev.wiki page (`documentation/wowdev-wiki/md/M2/Rendering.md`, a
`Shader Name | RGB Logic | Alpha Logic` table separate from the file this
project already used) confirms the `*2.0` term wow.export has and the
other wiki page is missing. Checked ~16 directly-comparable formulas
line-by-line against wow.export; only that one case disagreed, and four
more undocumented shaders sharing the same corrected core formula inherit
that trust. Also found wow.export's own internal inconsistency: its
`ShaderMapper.js` (used by its live-rendering path) disagrees with
wowdev.wiki's shaderId table (which husk's own `m2_shader_names.cpp`
already trusts) at three array positions, one a real name swap that would
make wow.export's own live viewer pick the wrong pixel-shader case for
that shaderId — and a separate, real wow.export bug found by reading the
shader directly: every Add-suffixed combiner's additive term is computed
but never actually added to the output (`lit_color += specular;` is
commented out, "disabled for debugging"), meaning anyone comparing against
wow.export's own on-screen render wouldn't see that term either. Two
comparison renders dumped to `example_exports/` (gitignored, not
committed) for a real corpus file (`creature/aetherwyrm/aetherwyrm.m2`,
`Combiners_Opaque_Mod2xNA_Alpha_Add`) — real client screenshot comparison
was considered and explicitly declined (direct steer: a decade-plus of
WoW visual familiarity plus `live_gallery`'s fast pass/fail review flow
already does this job at "a few hundred [renders] a minute," a live-client
screenshot roundtrip for one model at a time isn't worth it). Full
findings: this notification's own transcript (not re-copied here in full;
the load-bearing parts — the corrected formula, the excluded/unresolved
shaders — are in `render_glb.py`'s own new formula-table module comment,
see next paragraph).

(3) `MULTI_TEXTURE_LAYER_TODO.md` step 4 (Blender-side node recipes)
implemented directly, in parallel with (2) above:
`fix_multi_texture_layers()` (`render_glb.py`, same site/pattern as
`fix_additive_materials`, called from `main()`) — a table of 19 real
`Combiners_*` formulas (transcribed from `documentation/wowdev-wiki/
wikitext/Pixel_shader_logic_for_mixing_colors.wiki`, corrected per (2)
above where it was wrong) as small Mix/VectorMath/Math node-graph builders
feeding Base Color/Alpha, plus the env-map reflection-vector UV recipe for
the unambiguous single-texture-layer case. Real formulas deliberately
excluded rather than guessed at: `Guild`/`_NoBorder`/`_Opaque` (unresolved
tint inputs), `Illum` (no formula, no corpus repros), the two
Dual_Crossfade shaders (wow.export/wiki table mismatch from (2)), a few
"moderate confidence only" `_Wgt` formulas, and `Combiners_Add_Mod` (its
own `meshResColor` role doesn't fit the "final multiply" pattern every
other formula shares). Skips any material `fix_additive_materials` already
fully rebuilt (`blend_mode` 3/4 — no live Base Color chain left to extend
there); Mod/Mod2x materials from this same session's earlier work stay
fully eligible (that fixup lives in the Compositor, never touches a
material's own node graph). One real design finding along the way:
Blender's own glTF importer does **not** apply `baseColorFactor` as a real
multiply once a texture is linked (confirmed empirically — Base Color/
Alpha link straight to the Image Texture node, `default_value` sits there
unused) — every formula's own `meshResColor` term is dropped to match that
existing baseline rather than inventing a new tint multiply this fixup
would be the only thing applying.

Verified: a synthetic two-texture material through the function directly
(confirms the combiner node graph builds and renders without error) and
the single-texture env-map path separately (confirms the reflection UV
lands on the right socket). **Not yet verified against a real corpus
render with actual combined output** — every real corpus file checked
this session either had `blend_mode` 3/4 (correctly skipped) or an
additional-texture-layer FileDataID husk couldn't actually embed (no local
`--textures` match) — real end-to-end corpus coverage for this path is
still open.

---

**2026-08-14, new session, two corpus-scan follow-ups + one design-blocker investigation
(user-directed: "implement the first 3" of a ranked TODO impact list)**:
Ranked `TODO/`'s open independent items by impact, then implemented the top
three. (1) `MULTI_TEXTURE_LAYER_TODO.md` step 0 and (2)
`PIXEL_SHADER_FORMULAS_TODO.md` step 1 turned out answerable by one new
corpus-scan task, `tools/corpus_scan_tasks/shader_names_task.py` — ports
`husk::m2::resolveShaderNames` (src/m2_shader_names.cpp) into Python (same
pattern `shader_id_task.py` already established), run against the full
local corpus (287,005 `.skin` files). Real results: env-map frequency
(`_Env`-bearing vertex shaders) is **41.33%** of real batches, not the
stale "3 files" figure that used to justify deferring the env-map node
recipe — now confirmed high-priority. Separately, **14 of the 17
wowdev.wiki-undocumented `Combiners_*` pixel shaders have real corpus
repros** (some with 15,000+ files), 3 never resolved in this corpus
(`Combiners_Opaque_Mod2xNA_Alpha_Alpha`, `Guild_NoBorder`, `Illum`). Both
TODO files and `TODO/README.md`'s index updated with the real numbers and
example file paths; no husk source changed (pure investigation/tooling).
(3) `RENDER_QUALITY_TODO.md`'s Mod/Mod2x (multiply) blend modes — first
pass queried the project's own pinned Blender (5.1.1) directly and
confirmed `Material.surface_render_method`/`blend_method` have no
`MULTIPLY` option and neither Cycles nor EEVEE Next expose a
framebuffer-read node to *material shader graphs*, so a shader-graph-only
port of the additive fix (`Transparent BSDF` + `Add Shader`) genuinely
doesn't generalize — initially written up as a real design-call blocker.
**Corrected in the same session, prompted directly** ("is a multi-render-
layer + Compositor node graph doable, isn't that what the Compositor tab
is for?"): right — the *Compositor* is a separate node system that runs
on finished per-ViewLayer renders, which is exactly a framebuffer read.
Built and rendered a minimal, real headless test (two ViewLayers, one
scene, `film_transparent=True`, combined via a Compositor graph — flat
background color under the Base layer via `AlphaOver`, then `mix(base,
base*multLayer, factor=multLayer.alpha)`) and verified the rendered
pixels directly: overlap region `(0.082, 0.094, 0.031)` against a
hand-computed `(0.084, 0.095, 0.032)`, no-overlap region matching the
base layer's own unmodified color exactly. Real `dest = mix(dest,
dest*src, srcAlpha)` — the actual WoW Mod formula — genuinely reproduced.
Hit and resolved two real Blender-5.x API traps along the way, both
written up in `RENDER_QUALITY_TODO.md` §3 for future compositor-graph
code in this project: the compositor tree is now `scene.
compositing_node_group` (a real NodeGroup with its own `interface`, no
`CompositorNodeComposite` node anymore — terminal node is
`NodeGroupOutput`), and node socket string-lookup (`node.inputs["X"]`)
indexes by display `.name`, not `.identifier` — several nodes
(`ShaderNodeMix`, `CompositorNodeAlphaOver`) have multiple sockets
sharing a display name, so bracket lookup silently grabs the wrong one
instead of erroring; always resolve by iterating `.identifier` explicitly.
`render_glb.py` pipeline integration (per-material object splitting,
building the ViewLayer/Compositor graph, confirming the animated case)
is real follow-up work, not blocked on a design call anymore.
**Follow-up same session** ("what about Cryptomatte, avoids the mesh
splitting"): investigated — real, and now fully confirmed working, not
just plausible. EEVEE Next's `CryptoMaterial` pass genuinely discriminates
materials on a single unsplit mesh Object (verified via raw pass-channel
readback: two materials on one two-triangle object produced two distinct
hash values). First matte-extraction attempt failed on a dumb bug: guessed
`matte_id = '"mult_mat"'` (quoted), which produced a wrong hash and a
zero matte. **Fixed on a direct tip from Luna**, who supplied a screenshot
of Blender's own interactive Cryptomatte picker showing the real field
format — a plain comma-separated list of material names, no quotes.
Corrected (`matte_id = "mult_mat"`) and reverified: entry hash now
matches the raw pass channel exactly, `Matte` output clean (0.0/1.0 on
the two materials' real pixels). The earlier "would need to reimplement
MurmurHash3 myself" conclusion from the first pass was wrong — a symptom
of the quoting bug, not a real API gap. **One more real follow-up, direct
from Luna** ("if opaque it'll just not be visible, but if the foreground
object is transparent/translucent the mattes respect it") plus real
screenshots showing a translucent foreground shape not blocking the
cubes behind it in a cutout that excluded it: raised the question of
whether a *single* render (two complementary Cryptomatte cutouts, `src`
and `dest`) could replace the second-ViewLayer approach entirely, not
just the object-splitting step. Tested directly with a deliberately
adversarial control (two fully opaque cubes, one 100% hiding the other)
on both EEVEE Next and Cycles: the hidden cube's cutout came back
`(0,0,0,0)`, genuinely empty — real occlusion is never recoverable from
one render, confirming Luna's own stated rule exactly, not the stronger
"never need a second render" reading. But the precondition (occluder
must be non-opaque) turns out to already hold for every real Mod/Mod2x
material husk exports: `alphaModeForBlend` (`src/export_texture_
resolution.cpp:230-236`) already collapses every `blendMode > 1` to glTF
`AlphaMode::Blend`, never `Opaque`, so Blender's importer never sets
`blend_method='OPAQUE'` for one. **Final recipe, genuinely simpler than
every earlier version**: one ordinary Combined render, two Cryptomatte
cutouts (`src` = Mod/Mod2x material names, `dest` = everything else),
combined via the already-verified `mix(dest, dest*src, coverage)`
Compositor graph — no second ViewLayer, no object hiding, no mesh
splitting or Geometry Nodes modifier anywhere, all superseded by this
single-render version and written up as such (with the superseded
approaches kept only as a one-paragraph historical note) in
`RENDER_QUALITY_TODO.md` §3. `render_glb.py` integration itself still not
built this session, but every open design/technique question is now
closed. Full suite untouched (no husk source changes this session — all
items were investigation/tooling, `tools/corpus_scan_tasks/
shader_names_task.py` new, `render_glb.py` itself not yet touched).

**Addendum, same session, prompted directly** ("you are diverging from
the task too much"): the "single render, two Cryptomatte cutouts" recipe
above was itself wrong, caught by Luna's own real interactive node graph
(two `Cryptomatte` nodes with complementary `matte_id` lists combined via
ordinary `Add`/`Multiply` compositor math, producing a correct result
with no second render). The specific error: a Cryptomatte per-material
cutout isn't the material's own unmixed color — it's the already-blended
composite masked by that material's coverage — proven with a real test
(Cycles, `alpha=0.5`: `DEST(back_mat)` cutout RGB was bit-identical to
the combined pixel's RGB, only alpha differed). Stopped trying to
algebraically reconstruct a clean `dest`; the real technique is Luna's
own demonstrated layering approach, tracked as an implementation plan
(not yet built) in new `TODO/MOD_BLEND_COMPOSITING_TODO.md`, with
`RENDER_QUALITY_TODO.md` §3 trimmed back down to a findings summary
pointing at it. **Second addendum, same session**: what looked like a
genuine EEVEE Next Cryptomatte engine bug (`Matte` output pure black for
any `blend_method='BLEND'` material, confirmed via matching Cycles vs.
EEVEE screenshots of the same scene) turned out to be narrower and fully
fixable — root-caused to `blend_method='BLEND'` silently flipping
`Material.surface_render_method` from Blender's own default (`DITHERED`)
to `BLENDED`, and the bug is specific to `BLENDED`, not to `BLEND`-mode
materials generally. Caught because Luna's own interactive test (with
`Render Method: Dithered` explicitly visible) contradicted this
session's scripted tests entirely. Fix: re-assert `surface_render_method
= 'DITHERED'` after setting `blend_method` — verified this makes EEVEE
match Cycles exactly, removing the engine restriction that would have
otherwise forced this feature onto Cycles. Both TODO files updated to
match. Real lesson banked as a memory entry
(`feedback_dont_overinvestigate_tangents.md`): stop and check for a more
direct path instead of re-deriving a whole alternate architecture when a
debugging side-quest starts looping.

---

**2026-08-14, new session, independent staging commits + a documentation-discipline pass**:
Nine independent `[UNVERIFIED/STAGING]` commits, each investigate-then-
commit, matching the "keep going through independent well-scoped TODO
items" instruction: (1) `ENGINE_TODO.md` #2 (`aliasNext`/animation names)
checked against the real local `animationdata.db2` and WoWDBDefs/
wowdev.wiki history — the client dropped the `Name` column from this
table's own schema around 7.3.5, so no local DB2 table can ever answer
this; closed as genuinely unfulfillable, not a gap husk can close. (2)
The real WoW alpha-test threshold (`reference/wow.export`'s
`M2RendererGL.js`, `u_alpha_test = 0.501960814`, i.e. `128/255`) is now
set explicitly as `alphaCutoff` on every `MASK`-mode material
(`gltf_mesh.cpp`'s `emitMaterial`) rather than relying on its
mathematically-coincidental match with glTF's own implicit `0.5` default
for byte-quantized textures. (3) Spot-checked 6 of
`RENDER_QUALITY_TODO.md`'s 68 "unexplained" blank corpus renders
individually — 0 were husk export bugs: 5 are correctly near-invisible by
design (character-select rig/stage markers with real attachment/light/
camera data but a trivial mesh, UI-icon-scale geometry meant for a 2D
screen camera, genuinely invisible/debug game entities), and 1
(`shattrath_scryerhedges`) was a real texture-resolution miss now fixed
by `--listfile`, revealing a separate, already-understood giant-doodad
auto-framing limitation underneath rather than a data bug — downgraded
from "top priority" accordingly. (4) Audited 7 files
(`db2.hpp`/`dbd.hpp`/`db2table.hpp`/`listfile.hpp`/`blp.hpp`/
`m2_animation.hpp`/`m2_header.hpp`) for stale "out of scope"/"not
parsed" claims per `CLEANUP_TODO.md`'s comment-hygiene item — found and
fixed one real stale claim: `m2_header.hpp`'s `physFileId` doc comment
said `.phys` was "a format husk doesn't parse yet," false since `--phys`/
`src/phys.cpp` has been a shipped feature for several sessions. (5) New
`tools/husk_blender_geoset_mask.py` job, `apply_emitter_markers`: places
a small, distinctly-shaped/colored, non-textured placement marker at
every real `ribbon_emitters`/`particle_emitters` skin extras anchor
(id/joint/position only — husk deliberately doesn't carry the full
per-emitter texture/blend/curve data in the `.glb`, see
`gltf_skeleton.hpp`'s `EmitterAnchor` doc comment), bone-following
through animation via the same "direct matrix construction, not native
constraint/parenting semantics" approach `apply_billboard_alignment`
already established. Not a particle simulation — closes the "100%
invisible, nothing there at all" gap those effects had (weapon glow
trails, magic auras, the `cloudswampgas_white_clickable` case from an
earlier session) with an honest placeholder. Follow-up per direct
request: markers now live in a dedicated `Husk Debug Markers` collection,
`hide_render`/`hide_viewport` both set (plus a belt-and-suspenders
`hide_render` on each object), so they're excluded from every render pass
and hidden in the viewport by default. Verified headlessly against 3 real
fixtures (particle+ribbon weapon, ribbon-only weapon, zero-emitter
model — confirmed silent no-op). (6) Investigated a real Blender-build-
specific import crash (`TypeError: object of type 'NoneType' has no
len()` in `io_scene_gltf2/blender/imp/blender_gltf.py`, on a self-built
Blender-git 5.3) on a husk export with zero images — tried patching the
serialized `.glb` to include an explicit `"images":[]`, but
`gltf_validator` itself rejects that as invalid (`/images: Entity cannot
be empty` — the glTF 2.0 schema requires `minItems:1` when the key is
present at all; omitting it, what tinygltf already does, is the *only*
spec-valid shape). Reverted rather than trade a Blender-build-specific
crash for a genuine spec violation this project's own conformance suite
gates on; the project's pinned flake Blender (5.1.1) handles the correct
absent-key case fine. Documented as a real, investigated, not-husk's-bug
finding rather than silently dropped. (7) Investigated
`RENDER_QUALITY_TODO.md`'s "ambiguous-pool tiebreak" hypothesis (two
independently-ambiguous hardcoded texture slots defaulting to different
colors, reported as textures that "switch per face") against both named
repros (`dragonspawntwilightoverlord`/`dragonspawn2caster`) — doesn't
reproduce: every ambiguous slot on both real models independently
resolves to the identical default candidate, and a rendered frame shows
no color mismatch. The underlying structural gap (alphabetical fallback
isn't a principled tiebreak) is still real but turns out self-consistent
in practice. (8) Split `src/export_materials.cpp` (1,344 lines, the
largest file in `src/`, two genuinely separate concerns bundled into one
translation unit) into `src/export_texture_resolution.hpp/.cpp` (texture-
candidate resolution: embedded-filename/FileDataID/fuzzy-basename-pool/
listfile matching, category classification, default-pick ordering, the
three animated-curve resolvers) and a much smaller `export_materials.cpp`
(695 lines, just `scanDirOrWarn` + `buildMaterialsAndPrimitives`) — same
"internal helper header, not part of the public API" pattern
`gltf_mesh_internal.hpp`/`gltf_skeleton_internal.hpp` already established.
Pure mechanical extraction, verified byte-identical output (a real
`bloodelffemale.m2` export produces identical vertex/material counts and
validates with zero `gltf_validator` errors, same as before). Full test
suite stayed green throughout all nine commits, 634/634.

**Documentation-discipline pass, prompted directly** (a fair correction —
these files should have been kept trimmed as each item closed during the
session above, not left to balloon into history documents needing one
retroactive sweep): `TODO/RENDER_QUALITY_TODO.md` cut from 791 to ~330
lines — removed two fully-resolved sections outright (the rotation-shear
investigation, both mechanisms — hemisphere-continuity quaternion-sign
fix and the geoset-tag-joint-deform-disable fix — and the animated-
texture V-scroll-direction fix), whose real design rationale already
lives in `src/gltf_math.hpp`'s `enforceHemisphereContinuity` doc comment
and `tools/husk_blender_geoset_mask.py`'s own code comments, not
duplicated in the TODO file; compressed several now-closed sub-items
(the alphaCutoff fix, the dragonspawn tiebreak investigation) down to
what's still load-bearing. `TODO/TODO_correctness.md` cut from 238 to
~45 lines — dropped a "Former item N is now resolved" narrative preamble
describing five items already deleted in prior sessions, and compressed
the `.bone` correction-set-selection item's multi-"Update" investigation
saga down to its current real state (DB2 chain resolved and wired,
pointer to `BONE_CORRECTION_APPLICATION_TODO.md` for what's still open).
`TODO/ANIMATED_TEXTURE_EFFECTS_TODO.md` cut from 319 to ~26 lines — two
sections explicitly marked "— DONE" removed outright (their design
rationale already lives in `render_glb.py`/`husk_blender_geoset_mask.py`'s
own module docstrings, e.g. the latter's "Fourth, independent job"
section), leaving only the one genuinely open item (the standalone 3D
viewer's missing animation playback). Fixed ~10 dangling cross-file
citations this pass and the `export_texture_resolution.cpp` split left
behind, pointing at functions/sections that moved or were deleted
(`DESIGN.md`, `README.md` x2, `M2_COMPLETENESS.md`,
`RENDER_QUALITY_TODO.md` x3, `CHAR_TEXTURE_COMPOSITING_TODO.md`,
`ANIMATED_TEXTURE_EFFECTS_TODO.md`, `src/gltf_math.hpp`,
`src/cmd_export.cpp`, `src/gltf_skeleton.hpp`, `tests/test_data_paths.hpp`,
`tools/husk_blender_geoset_mask.py`, `EYES_ON_FINDINGS.md`, `CLAUDE.md`,
`tests/test_integration_texture_transform.cpp`, `src/gltf_mesh.cpp`).
Added a real `README.md` line documenting the alpha-cutoff fix in the
materials section (coverage doc, not just a TODO note). Explicitly not
attempted this session, flagged rather than guessed at:
`BONE_NAME_DEDUCTION_TODO.md`'s Tier 2 (needs its own design pass — fuzzy
reference-skeleton matching, an ambiguity policy, a new Rigify
dependency) and Mod/Mod2x multiply blending (Blender 5.x's EEVEE Next has
no native multiply blend mode left, needs a design call on the
approximation, no real-corpus repro driving it yet either). **Left
undone, flagged not fixed**: `CLAUDE.md`'s own Resume section has the
identical "history document, not a living snapshot" problem the TODO
files had (~1,300 lines of accumulated "Prior state" entries, when the
file's own header says this should be a condensed snapshot with the full
narrative living here instead) — out of this session's actual scope (the
request was specifically about `TODO/*.md`), but a real, named follow-up:
migrate the historical chain out of `CLAUDE.md`'s Resume section into
this file, leaving just current-state/next-step/hazards there.

---

**Migrated from `CLAUDE.md`'s Resume section, 2026-08-14** (the named
follow-up two entries above): everything below this line down to the next
`---` was a chain of "Current state"/"Prior state"/"Next step"/"Hazards"
entries that had accumulated in `CLAUDE.md`'s Resume section instead of
being trimmed after transfer here, per this file's own stated convention.
Preserved verbatim, append-only (not rewritten/deduplicated against
entries elsewhere in this file that may cover the same sessions in
different words) — this block is itself internally most-recent-first,
same as the rest of this file, but its own chronological position
relative to the entries immediately below (already covering an
overlapping/adjacent time window) was not reconciled; both are kept
rather than risking real content loss trying to merge them by hand.

- **Prior state**: Same session, continued. Two follow-ups off the back
  of the geoset/`.bone`-correction-selection work below. (1) Split
  `GEOSET_SELECTION_TODO.md`'s Blender-side step 3 into its own file,
  `TODO/BONE_CORRECTION_APPLICATION_TODO.md` -- a real correction, prompted
  directly: an earlier draft claimed `.bone` corrections "aren't applied to
  the render... by design," true only of the `husk export` C++ binary
  (`DESIGN.md`'s Key design decisions), not of `tools/
  husk_blender_geoset_mask.py`, which already applies exported extras to
  real Blender rendering elsewhere (billboards, texture-transform
  animation, now geoset switching too). The real remaining blocker is
  narrower and still open: `.bone`'s correction-matrix *application
  semantics* (multiply order, local-vs-model space) were reverse-engineered
  only as far as byte shape, never verified against real client behavior --
  needs the same real side-by-side-against-the-client treatment billboard
  alignment eventually got before anything applies it, gated on Luna's own
  interactive comparison, not something to build blind. (2) The other half
  of that same TODO -- wiring real `enabled_geosets` extras into
  `tools/husk_blender_geoset_mask.py` itself -- is done: new
  `read_enabled_geosets`/`enabled_geosets_to_default_overrides` (same
  raw-glTF-JSON-reread pattern `read_chr_texture_layout` already
  established, since skin extras have no supported Blender importer target
  at all) feed a new `extra_default_overrides` parameter on
  `apply_geoset_switch(es)`, layered on top of and taking priority over the
  existing hand-curated `CURATED_DEFAULT_VARIANTS` table. Verified
  end-to-end, headlessly, against the real `bloodelffemale_hd.m2` fixture
  with a deliberately adversarial test case (a real resolved choice
  overriding group 0's own hardcoded `CURATED_DEFAULT_VARIANTS` default of
  `"none"`) -- confirmed via both the Menu Switch modifier's real stored
  value (matched against its own real enum item list, not assumed from the
  offset formula) and, more rigorously, the evaluated mesh's own vertex
  count actually differing (4,276 vs. 4,849) between the two states -- real
  geometry change, not just a correct-looking modifier input. An export
  with no `--customization-choice-ids` at all is confirmed byte-for-byte
  unaffected (clean `None`/`{}` fallback, no crash). C++ suite unaffected
  (Python-only change), still green, 632/632. **Third follow-up, prompted
  directly by Luna catching it live**: `GEOSET_SELECTION_TODO.md` itself
  had drifted into exactly the "historical record" shape its own header
  explicitly disclaims ("an open punch list, not a historical record...
  fixed items get removed outright") -- both halves of its own job (husk-
  side resolution, Blender-side consumption) were done, so the file was
  deleted outright rather than left as a done-item narrative, per this
  project's own established convention (`TODO_correctness.md`'s repeated
  precedent for exactly this). `ENGINE_TODO.md`'s former items 1/2 (which
  pointed at it) got the same treatment -- removed outright, not just
  repointed, since both were themselves fully resolved from that file's
  own "external data acquisition" framing; remaining items renumbered 1-5.
  Every other citation of the deleted file across `DESIGN.md`/`README.md`/
  `TODO_correctness.md`/`BONE_CORRECTION_APPLICATION_TODO.md`/`tests/
  test_cli_chrcustomization.cpp`/`tools/husk_blender_geoset_mask.py`/
  `src/chrcustomization_db2.{hpp,cpp}`/`src/cmd_export.cpp`/`src/
  gltf_skeleton.hpp` rewritten to explain inline or point at a real
  surviving source, same "no dangling citations" discipline this project
  already applied when `TRANSFORM_TRIAGE.md`/`GEOSET_MASK_TODO.md` were
  deleted. Also caught and fixed in the same pass: `TODO_correctness.md`
  #2 still carried the same "`.bone` corrections aren't applied... by
  design" overstatement the second follow-up above already corrected
  elsewhere -- fixed there too. Full suite still green, 632/632 (doc/TODO-
  only changes, no behavior touched).
- **Prior state**: New session. `TODO/ENGINE_TODO.md`'s stale premise
  corrected (it claimed husk "will never touch DB2," directly contradicting
  `DESIGN.md`'s own 2026-08-08 clarification that locally-extracted `.db2`
  files are in scope -- items 1-4/6 reframed as real husk work gated on
  local-DB2 investigation, not a hypothetical downstream engine's problem;
  items 5/7 correctly kept as-is, genuine "no data exists" cases). That
  correction surfaced a real, previously-unwritten TODO (`ENGINE_TODO.md`
  item 1, geoset selection) -- `TODO/GEOSET_SELECTION_TODO.md` (new) traced
  and verified the real DB2 chain (`ChrCustomizationChoice` ->
  `ChrCustomizationElement.ChrCustomizationGeosetID` ->
  `ChrCustomizationGeoset`, `geoset_id = GeosetType*100+GeosetID`, matching
  husk's own existing `geoset_group`/`geoset_variant` convention exactly)
  against real local `husk db2-info`/`db2-export --dbd-dir` output, then
  implemented it: `src/chrcustomization_db2.hpp`/`.cpp` (new, a
  `db2table.hpp`-based typed reader) backs new `husk export --db2-dir/
  --dbd-dir/--customization-choice-ids <id,id,...>`, attaching real geoset
  selections as `enabled_geosets` skin extras. The same table also answers
  `TODO_correctness.md` #2's long-open ".bone slot selection" question --
  `ChrCustomizationElement.ChrCustomizationBoneSetID` ->
  `ChrCustomizationBoneSet.BoneFileDataID` resolves a real customization
  choice straight to the real `.bone` FileDataID `--bones-dir` already
  looks for -- so the same flag also marks a matching `--bones-dir`-
  resolved `CorrectionSet` with a new `selected_by_choice_ids` extras
  field, closing that item too. Real bug found and fixed via actual
  end-to-end CLI verification, not inspection: `ChrCustomizationChoiceID`
  is one-to-many into `ChrCustomizationElement` (15-20+ real rows per
  choice, only one usually nonzero per field) -- an early version of the
  resolver matched only the first row per choice and silently dropped real
  geoset/boneset values whenever they weren't on that row; caught because
  real choice IDs pulled from a real `husk db2-export` sqlite dump reported
  "0 matched" against the real `bloodelffemale_hd.m2` fixture until fixed.
  Verified end to end against real local data throughout (not just
  synthetic tests): resolved real choice 45 to `geoset_id: 2` and choices
  1758/1759 to real matched `bone_correction_sets` FileDataIDs
  1103216/1103217, inspected directly in the output `.glb`'s own JSON
  chunk. 4 new CLI-tier tests (`tests/test_cli_chrcustomization.cpp`,
  including a regression case for the one-to-many bug). Completions
  regenerated (bash/zsh), `README.md`/`DESIGN.md` flag tables updated. Full
  suite green, 632/632. Follow-up correction, prompted directly: an early
  draft of `GEOSET_SELECTION_TODO.md`'s Blender-side section claimed
  `selected_by_choice_ids` "may not need a consumer" since `.bone`
  corrections aren't applied to the render -- wrong, since that "never
  applied to the render" boundary is real for the `husk export` C++ binary
  specifically (`DESIGN.md`'s Key design decisions) but does NOT bind
  `tools/husk_blender_geoset_mask.py`, which already applies exported
  extras to real Blender rendering for billboard alignment/texture-
  transform animation/geoset switching -- corrected in the TODO file. The
  real remaining blocker there is narrower: the `.bone` correction's own
  application semantics (multiply order, local-vs-model space) were never
  verified against real client behavior, only reverse-engineered as far as
  "a small delta matrix" -- needs the same real side-by-side-against-the-
  client treatment billboard alignment eventually got, not more code
  reading. Committed (`8564197`).
- **Prior state**: Same session, continued -- `TODO/ANIMATED_TEXTURE_EFFECTS_TODO.md`'s
  §1 ("a framework for exporting short animated clips") closed too, prompted
  directly. `tools/corpus_scan_tasks/render_glb.py` now renders a real short
  looping animated WebP (not just one still frame) for any file with a real
  animation source -- a skeletal action (Blender's glTF importer already
  leaves one active post-import for direct scrubbing, confirmed empirically
  against the real `wolf.m2` fixture, not assumed) or husk's own
  `texture_transform_animation`/`tint_animation`/`fade_animation` extras
  (this session's earlier work, see the entry below). Every animated file
  gets a fixed 5-second preview window regardless of its own native
  duration -- a native clip under 5s loops to fill it (prompted directly:
  don't skip short clips, which are the common case), a longer one is shown
  from its own start through the window; 120 real frames at a real 24fps,
  rendered via one native `bpy.ops.render.render(animation=True)` call, not
  a coarse manual per-frame loop -- a first version of this rendered a
  fixed 12 *total* samples (2.4fps, a visible strobe) via individual
  `write_still=True` calls and was corrected on direct pushback ("blender
  can render animations... near real-time"): confirmed empirically that
  `animation=True` fires the same `frame_change_pre` handlers manual
  `frame_set()` calls do, and that real wall time (startup + import + a
  full 120-frame animated render) is ~16s per file, dominated by fixed
  per-file overhead, not the render itself. A real skeletal action shorter
  than the window now loops via a genuine Blender `Cycles` F-curve
  modifier (`loop_action_natively`), not Python math -- verified the loop
  actually repeats (a near-loop-boundary frame measurably closer to frame
  0 than a mid-cycle frame), and hit a real Blender 5.x API surprise along
  the way (`action.fcurves` no longer exists; F-curves live under
  `action.layers[].strips[].channelbags[].fcurves` on the new layered
  Action data model). A genuinely static model still takes the old
  single-still path unchanged. Every rendered
  output also gets its real source `.glb` saved as a sibling file (same
  basename, `.glb` for `.webp`) -- what makes Luna's own new interactive
  three.js GLB viewer (`tools/live_gallery/server.py`, in progress/
  uncommitted, not this session's own work) able to find and load the real
  model next to its thumbnail. Real bug found and fixed while verifying
  this against the real texture-transform fixture: a repeating demo texture
  rendered as a flat, unchanging color across every sampled frame despite
  the underlying Mapping-node value genuinely changing -- a GPU mip-blur
  averaging artifact (mathematically expected: averaging a periodic signal
  over whole periods cancels the phase), not a real bug, fixed for the
  render path specifically via `Closest` interpolation
  (`example_exports/README.md` has the full writeup). Also filed two real,
  concrete usability findings as new TODO items rather than fixed silently:
  `TODO/TODO_correctness.md` (new item, since fixed -- see a later
  session's own entry below) -- a live terminal trace showing
  `husk export -o <dir>` and `-o <path-with-missing-parents>` both fail
  after running the *entire* export pipeline first, with no filename/
  directory inference at all; framed explicitly around a stated design
  principle ("sinne päin ja silmät kiinni" -- guess a reasonable default
  from minimal input by default, keep full precision available via flags
  for anyone who wants it, don't demand exact-shape input as the only
  path). `example_exports/` regenerated
  around the real `unk_exp11_7037014.m2` fixture with a genuinely-animated
  (not just still) preview, matching the new render_glb.py behavior.
- **Prior state**: `TODO/ANIMATED_TEXTURE_EFFECTS_TODO.md`'s corpus-scan
  question answered first (`tools/corpus_scan_tasks/animated_texture_effects_task.py`,
  new: 27.6% of the 130,576-file corpus, 36,086 files, carries a genuinely-
  animated texture-transform/tint/fade/weight curve -- a real, corpus-wide
  gap, not edge cases), then that TODO's real infrastructure item (§3,
  "making these curves actually animate in Blender") implemented end to
  end, prompted directly with a request for the same kind of simple,
  unambiguous debug/validate fixture the lightforged lamp was for billboard
  rotation. Found a real, previously-unclosed gap along the way: husk
  exported literally no data for a genuinely-*animated* M2TextureTransform
  (only the constant case got real values) -- `src/export_materials.cpp`/
  `src/gltf_mesh.cpp` now export the real translation/rotation/scaling
  keyframe curve as `texture_transform_animation` material extras, a new
  `resolveAnimatedRawQuatCurve`/`resolveRawQuatTrackSequence`/
  `resolveRawQuatGlobalSequenceTrack` (`src/m2_animation.{hpp,cpp}`) needed
  for rotation specifically since M2TextureTransform's rotation track is a
  *raw* `C4Quaternion`, not the compressed `M2CompQuat` bone rotations use.
  Test fixture (`unk_exp11_7037014.m2`, `test_data/models/spells/`) found
  the hard way -- a first-choice candidate turned out to be a dead,
  unreferenced transform array entry no `.skin` batch's
  `textureTransformComboIndex` actually resolves to, a real trap a naive
  corpus-presence scan doesn't catch; cross-checked against real batch data
  before committing. New real integration test
  (`tests/test_integration_texture_transform.cpp`) plus raw-quat-resolver
  unit tests (`tests/test_m2_animation_tracks.cpp`). Blender side
  (`tools/husk_blender_geoset_mask.py`, a 4th independent job):
  `apply_texture_transform_animation` (a shared Mapping node, direct
  per-frame computation + a `frame_change_pre` handler, same design choice
  as the earlier billboard-alignment work) verified headlessly against the
  real fixture -- Location.x reads 0.0/0.5/wraps-correctly at the curve's
  own start/midpoint/past-loop frames, matching the lerp+loop math exactly.
  `apply_tint_fade_animation` (same machinery, Principled BSDF driven
  directly) is smoke-tested against a real file with genuine tint/fade data
  (no crash, including the known "unlit materials get no Principled BSDF"
  Blender-importer quirk) but **not verified against real ground-truth
  values** -- flagged as such in its own doc comment, not overclaimed.
  Clip-length question (the session's own explicit ask: "24 frames ≈ 1
  second, or something else?") answered concretely: not a fixed convention
  -- each curve's own real duration (last keyframe timestamp) drives
  `scene.frame_end` (grown, never shrunk) at the scene's existing frame
  rate; the real fixture's 4.167s loop computes to 100 frames at 24fps, not
  a hardcoded clip length. Also closed, a hard prerequisite this TODO
  itself flagged before adding a 4th/5th stage: every stage in
  `husk_blender_geoset_mask.py`'s `main()` now runs through a shared
  `_run_stage` wrapper (loud, specific per-stage failure, never blocks
  later stages). Filed `TODO/CLEANUP_TODO.md` (new) separately, per a
  direct mid-session note: `src/export_materials.cpp` is now the largest
  file in `src/` (1,281+ lines) and needs splitting -- not investigated
  yet, flagged only. Stages 1/2 of `ANIMATED_TEXTURE_EFFECTS_TODO.md` (a
  real short-clip corpus render pipeline + live-gallery playback) remain
  open, out of this session's scope -- this session closed §3 (the
  playback mechanism itself) and the scope-measurement work, not the
  render-pipeline infrastructure. Full C++ suite green, 612/612.
- **Prior state**: Same session, continued once more -- a real
  regression caught live (both CPU and GPU usage visibly dropped mid-run)
  after `--listfile` first shipped: `src/listfile.cpp`'s `loadListfile`
  re-parses the full 148MB/2.2M-line `community-listfile.csv` from scratch
  on *every* `husk export` call (~1.1s each, no persistent process to cache
  it across the render driver's 130,576 invocations). A first fix
  (filtering the listfile down to a prior scan's known-relevant FileDataIDs)
  was built, then deliberately reverted on a direct objection: pruning to a
  snapshot risks losing coverage for anything that scan didn't flag. Fixed
  at the root instead — rewrote `loadListfile` for raw parse speed (one
  `fread`, manual digit scanning, `reserve()`d hashmap) against the *full,
  unpruned* file; confirmed by timing the overhead is now noise-level
  (~4.4s with `--listfile` vs ~4.5s without, same fixture that was 5.65s
  with the original naive parser). Full listfile kept, nothing pruned.
  Persistent (queue-fed, not one-process-per-file) Blender workers were
  measured (~25-30% of Blender-side wall time is pure process startup) and
  explicitly deferred as a separate follow-up, not implemented — Luna's own
  call, to launch the already-fixed pipeline rather than add another
  architecture change first. Full suite green, 601/601, unchanged from
  before (implementation-only fix, same test behavior).
- **Prior state (same session)**: two more real gaps closed,
  both found by actually looking at rendered corpus output, not just
  parsing correctness. (1) `_sdr` stand-in character models (lower-poly,
  self-contained-animation variants sharing textures with their non-"_sdr"
  counterpart) rendered fully untextured -- `scanFuzzyTexturePool`
  (`export_materials.cpp`) required an exact basename-prefix match, which
  the "_sdr" suffix always broke; fixed with a narrow, confirmed-by-bytes
  fallback (strip "_sdr" and retry when the exact basename finds nothing).
  (2) WoW's additive/multiply blend modes (M2Material blend 3-7) have no
  core-glTF equivalent and were collapsing to plain `BLEND`, a real visibly
  wrong answer for glow/particle effects (confirmed:
  `creature/celestialfoxwyvern/celestialfoxwyvern.m2` rendered as a solid
  dark diamond instead of glowing lines) -- `gltf::Material` gained a raw
  `blendMode` field, exported as `blend_mode` extras only when > 2, *and*
  `tools/corpus_scan_tasks/render_glb.py` (the real corpus-render pipeline
  itself, not just a future Blender-import nicety) now rebuilds a true
  additive shader for any such material post-import — found and fixed a
  real bug in the first version before it shipped: WoW's `unlit` flag
  commonly co-occurs with additive modes, and Blender's glTF importer
  builds a completely different node shape for unlit materials (no
  `Principled BSDF` at all), which the first version didn't handle and
  silently fixed nothing. Also this session: `tools/live_gallery/server.py`
  gained a `world/expansionNN` era filter (real folder convention,
  confirmed against actual zone content per folder — a file-*version*-based
  filter was tried first and found to carry zero signal on this modern
  retail extraction, see `CLAUDE_HISTORY.md` for why) and a real live-update
  smoothness fix (was fully rebuilding the grid on every new-file
  notification during an active render — now diffs and prepends only
  what's new). Render throughput: `render_sample_driver.py` now alternates
  `DRI_PRIME` per worker process to spread Blender's render step across
  this machine's two GPUs (confirmed via real `gpu_busy_percent` polling,
  not assumed) — Vulkan was tried too, no measurable speedup on this
  workload and its multi-GPU device-select env var didn't behave
  deterministically in testing, so not used. Render output is now WebP
  (quality 80) instead of PNG, real corpus examples ~2.5-4.7KB vs PNG's
  much larger size. Full suite green, 601/601. Full narrative, including
  the real debugging dead-ends (Vulkan device-select syntax, the unlit
  node-shape surprise) in `CLAUDE_HISTORY.md`.
- **Prior session (same day)**: New `husk export --listfile <path> [--listfile-root
  <dir>]` flags (real feature, not docs-only) close a real gap a
  from-scratch corpus tool found this session: a full 130,576-file render
  pass plus cross-referencing "missing" FileDataID textures against a real
  `community-listfile.csv` found 99.9% of them (81,809/81,890) were
  actually present in the corpus under their own real name/path, not a
  bare `<FileDataID>.{blp,png}` husk's exact-match resolution alone can
  find (write-up: a sibling project's own `casc-tool/FAILURES.md` item 13;
  the remaining 79 genuinely-absent FileDataIDs are casc-tool's own problem,
  out of husk's scope). Per `DESIGN.md`'s Non-goals clarification (the same
  one already covering `--dbd-dir`'s WoWDBDefs checkout), a local listfile
  snapshot is the identical "already on disk, never live CASC" tier as
  every other sidecar — `src/listfile.hpp`/`.cpp` (new) loads one;
  `export_materials.cpp`'s two FileDataID resolution sites each gained a
  new fallback tier, tried after the exact `<FileDataID>.{blp,png}` match
  fails but before the fuzzy same-basename pool (still deterministic, not
  a guess). `--listfile-root` is deliberately a *separate* directory from
  `--textures`, added after the first version of this shipped and was
  caught, before any real corpus run used it, reusing `texturesDir` for
  both roles: `--textures` drives the pre-existing directory-local
  embedded-filename/same-basename matching (a plain, non-recursive
  `directory_iterator` — confirmed by reading `scanDirOrWarn`), which would
  have silently gone blind across the entire corpus if `--textures` were
  pointed at a real corpus root just to make the listfile lookup work
  (real corpus texture files sit many directories below any single
  `--textures` value, so a flat scan of the root sees none of them).
  `--listfile-root` defaults to `--textures` for the simple case where one
  directory serves both roles. Verified against the exact real case that
  motivated this: `bloodelffemale_hd.m2` (`--textures` left at its default,
  the model's own directory; `--listfile-root` pointed at the real corpus
  root) goes from 0 embedded images (without `--listfile`) to 5, resolving
  the same 3 FileDataIDs (3536810/4530998/5210137) a much earlier session's
  entry below flagged as having "no matching local file at all" and left
  open. 8 new tests (`tests/test_listfile.cpp` plus 4 CLI-level tests in
  `tests/test_cli.cpp`, including one proving `--textures` and
  `--listfile-root` resolve genuinely independently), `DESIGN.md`/
  `README.md`/both `completions/husk.*` updated (the zsh completion label
  for `--listfile-root` originally contained an apostrophe, which breaks
  its single-quoted `_arguments` spec string per this file's own
  `zshFlagLabel` doc comment warning — caught by actually running `zsh -n`
  against the regenerated script, not just eyeballing the diff, and fixed
  before shipping). Full suite green, 597/597. The original full-corpus
  render job (`corpus_reports/renders_full`, resumed earlier this session
  after a crash) was stopped before this fix landed, specifically so it
  wouldn't run 79,000+ files against the broken `--textures`-as-corpus-root
  shape — restarted after, see the render-job entry immediately below for
  the concrete resume mechanics.
- **Prior session**: Finished `src/dbd.hpp`'s own long-documented scope gap
  (a prior autonomous-session task that hit the session's API limit
  mid-implementation and was stashed rather than left broken on `master` —
  picked back up and completed this session, real WIP recovered via
  `git stash pop`, not restarted from scratch). `dbd::resolveFieldNames`
  used to match a `.dbd` LAYOUT's inline fields to a real WDC5 file purely
  by *position*, with only a coarse field-*count* safety net — a layout
  with the right count but the wrong per-field shape (wrong layout hash
  matched, or a stale/wrong WoWDBDefs definition) could silently return
  real-looking column names misapplied to the wrong bytes, exactly the
  class of bug `~/.claude/CLAUDE.md`'s "Coding Policy: Foreign Data" rules
  exist to catch. Now cross-validates each matched field's real
  WoWDBDefs-declared `<Size>`/`[Length]` shape (parsed by a new
  `dbd::parseFieldLine` extension, previously discarded) against that
  same-position `db2::FieldStorageInfo` entry, per real WDC5 storage type:
  exact bit-size match for `field_compression_none`; upper-bound-only for
  bitpacked/bitpacked-signed (real compression legitimately uses fewer
  bits than the declared width, never more); exact `array_count` for
  `bitpacked_indexed_array`; genuinely un-checkable (not guessed at) for
  `common_data`/`bitpacked_indexed`, whose `field_size_bits` describes
  something other than the field's own logical value width. Any
  disagreement fails closed to `nullopt` (generic `field_<N>` fallback
  naming), same as the pre-existing count check. Verified against real
  data, not just synthetic cases: `chrmodelmaterial.db2` +
  `reference/WoWDBDefs`'s real `ChrModelMaterial` layout resolves
  correctly under the new check (its own real bitpacked/bitpacked-indexed-
  array fields exercise the non-trivial branches, not just the `None`
  case), and neither of the two other real DBD-resolved chains this
  project already had test coverage for regressed (`chrmodeltexturelayer.db2`
  → `charcomponenttexturelayouts.db2`'s `--dir` FK-constraint chain; the
  `--db2-dir`/`--char-layout-id` character-texture-layout extras path). Six
  new synthetic tests (`tests/test_dbd.cpp`) cover each storage-type branch
  individually, including two that had a real authoring bug when first
  written (missing a `field_storage_info` entry for the layout's own `ID`
  field, so the coarse count check masked what the shape check was
  actually meant to exercise — caught by actually running the tests, not
  assumed correct from the diff). `dbd.hpp`'s module comment,
  `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 1 paragraph, and
  `README.md`'s `db2-export` section updated to describe the new,
  narrower scope precisely — including what's still genuinely
  uncheckable, not overclaimed. Full suite green, 590/590.
- **Prior session**: Closed the small real gap the prior session's
  `TODO/ENGINE_TODO.md` audit found and deliberately left open: `m2::Event::data`
  (`src/m2_scene.hpp`, an opaque per-event `uint32_t`) is now exported into
  the event node's own glTF extras, not just parsed. Per wowdev.wiki
  M2#Events, `data` is documented to exist ("This data is passed when the
  event is fired") but not what it means per event — genuinely opaque, same
  "expose raw, don't guess" treatment this project already gives PCOL's
  `flags` (`WIKI_FINDINGS/M2.md`). Threaded through the existing pipeline
  unchanged in shape: `gltf::Skeleton::Event` (`src/gltf_skeleton.hpp`)
  gained a `data` field, populated in `attachPlacementNodes`
  (`src/cmd_export.cpp`, where events are actually built — not
  `export_skeleton.cpp`, which only handles the separate `dump-chunks` JSON
  path via `m2::eventName`; corrected that assumption while reading the
  code), and attached as a `data` extras key on the same `event_<identifier>`
  node in `writeGlbMulti` (`src/gltf_skeleton.cpp`), same one-extras-object-
  per-anchor-node pattern `animate_attached`/`light_animation` already use.
  Verified against the real `mace_2h_bolvar_d_01.m2` weapon fixture (2 real
  events, `$WTB`/`$WTT`) — `husk info` already printed `data=0` for both
  (a pre-existing, independent code path, `src/cmd_info.cpp`); the new test
  in `test_integration_weapons.cpp` re-parses the same file directly via
  `m2::parseEvents` and asserts the exported node's `data` extras matches
  that independent read exactly, not just that it's present. Also extended
  the existing synthetic `writeGlb` event test
  (`tests/test_gltf_skeleton.cpp`) with a non-zero `data` value round-trip
  check. `M2_COMPLETENESS.md`'s Events row and `TODO/ENGINE_TODO.md`
  item 6 updated to mention `data` is now exported (item 6's own
  conclusion — no sound-file reference lives in this field — is unchanged).
  Full suite green, 586/586. Scope was deliberately narrow (this one field
  only) per the task's own instruction — noticed but did not touch:
  `TODO/ENGINE_TODO.md` item 6's own text still cites `m2::eventName`/
  `src/export_skeleton.cpp` as if that were the glTF node-naming path; it
  isn't (that's the `dump-chunks` JSON path) — real node names come
  straight from `"event_" + e.identifier` in `src/cmd_export.cpp`/
  `src/gltf_skeleton.cpp`. Pre-existing doc imprecision, not introduced
  this session, flagged for a future docs pass rather than fixed here.
- **Prior session**: `TODO/ENGINE_TODO.md` (a spec for a hypothetical
  downstream engine project, not husk itself — see its own header) refreshed
  against real current source, not just the 4-item starting list this task
  was given. Confirmed stale, and corrected in place: item 3 (hardcoded
  texture resolution) claimed `husk export` doesn't surface `M2Texture::type`
  at all — it does, `texture_type` material extras
  (`gltf::Material::textureType`, `src/gltf_mesh.hpp`, set in
  `src/export_materials.cpp`), plus a real typed `alternate_textures`
  candidate pool per ambiguous slot. Item 4 (`aliasNext`) claimed it "isn't
  even parsed" — it's fully parsed and chain-resolved
  (`m2::Sequence::aliasNext`, `src/export_animation.cpp`,
  `alias_next`/`is_alias` clip extras), closing what the old text called a
  "genuinely unresolved even from the file alone" open question; only the
  id-to-name lookup against `AnimationData.dbc` remains external. Item 5
  (`blendTimeOperation`) claimed `blendTimeIn`/`blendTimeOut` "aren't
  currently parsed at all" — they are, exported as `blend_time_in`/
  `blend_time_out` clip extras; the real remaining gap (the blend-transition
  *rule*, correctly framed as pure client logic with no data to find) was
  already accurate and untouched. Item 6 (sound linking) claimed `M2Event`
  is "diagnostic-only, not exported as glTF nodes" — real `event_<identifier>`
  child nodes are exported (`gltf::Skeleton::Event`, `src/gltf_skeleton.cpp`);
  found and noted one genuine sub-gap while verifying: `m2::Event::data`
  (`src/m2_scene.hpp`, an opaque per-event `uint32_t`) is parsed but not
  currently exported into the node's extras — real, small, left as a
  possible follow-up, not fixed here (this was a docs-only task, no `src/`
  changes made). Item 7 (LOD thresholds) cited a `particleBoneLod` field
  that doesn't exist anywhere in husk's codebase (verified by repo-wide
  grep) — corrected to describe what's actually exposed (`lod_count` via
  `husk info`, not `dump-chunks`). Items 1 (geoset selection) and 2 (`.bone`
  correction selection) were re-verified and found still accurate as
  written, untouched. Priority ranking at the bottom updated only where a
  claim it depended on changed (item 4's ranking no longer hedges on
  alias sequences carrying unreachable keyframe data — they don't, confirmed
  above); the external-data-acquisition priority itself is unaffected.
  Header's `commit aa0df15` citation updated to the commit this refresh was
  done against. Full test suite green, 586/586, expected for a docs-only
  change but re-run anyway per this task's own instruction. Committed as
  `[UNVERIFIED/STAGED]` per Luna's standing convention for unsupervised
  documentation passes.
- **Prior session**: `db2.hpp`'s long-standing named gap -- "full decoding of
  offset-map ('sparse', flags & 0x01) sections ... expose their raw
  variable-length record bytes but not per-field values" -- is now closed
  for the `field_compression_none` case (the only one any real local
  offset-map file actually uses). New `db2::decodeOffsetMapRecord`
  (`src/db2.hpp`/`.cpp`) decodes a whole record in one pass with a running
  bit cursor, NOT via `field_storage_info.field_offset_bits` (that value
  only describes the table's *non-sparse* layout -- confirmed wrong against
  real `scenescripttext.db2` bytes, where an inline string shifts every
  field after it). Wired into both `husk db2-info` (row dump now works for
  offset-map sections too) and `husk db2-export` (`LoadedFile::
  skippedOffsetMap` removed entirely -- these sections get a real SQLite
  table like any other now); `cmd_db2.cpp` gained a shared `FieldValues`/
  `decodeRecordValues` abstraction so printing/column-planning/binding is
  one code path regardless of section shape, not two parallel ones. Found
  and fixed three real bugs via actual corpus data, not by inspection, all
  before this shipped: (1) a scalar 32-bit field's own value could
  coincidentally look like a short printable string (real:
  `conversationline.db2`'s `SpeechType` field, value `0x78` = ASCII `'x'`)
  -- fixed by only attempting the inline-string heuristic on fields whose
  `field_size_bits == 32` (a real string field's non-sparse counterpart is
  always a 4-byte string-table offset) plus a 4-byte minimum match length;
  (2) that same minimum length then wrongly rejected two genuinely real
  short strings at the very end of a record (`scenescripttext.db2`: a
  literal empty `Script` field, and a literal 1-character `" "` `Script`
  field) -- fixed by dropping the floor to 0 specifically when there isn't
  room left in the record for the alternative (a raw 4-byte int read)
  either, the one case where "short string" is the *only* value consistent
  with the record's own declared length; (3) the character-class check
  reused from `resolveFieldString` (permissive of high/UTF-8-continuation
  bytes, safe there since it's only applied within an already-likely
  string-table region) let a real large/negative 32-bit value
  (`conversationline.db2`'s `AdditionalDuration = -2500`, bytes starting
  `0x3c 0xf6 0xff 0xff`) "read through" its own high bytes into the *next*
  field's bytes before finding a zero terminator, silently desyncing every
  field after it -- fixed by making the offset-map heuristic strictly
  ASCII-only (reject anything >= 0x7f), a deliberate, documented divergence
  from `resolveFieldString`'s own looser rule. Verified end to end against
  all 5 real local offset-map files (found via a full `db2-info` scan of
  `/media/luna/data/wow_export/dbfilesclient/`'s 1129 files):
  `conversationline.db2` (69,312 rows, real numeric fields incl. the
  negative-duration case above, zero decode errors) and
  `scenescripttext.db2` (36,498 rows, real Lua source text, incl. the exact
  `Name`/`Script` pair used as this session's own regression test) both
  export cleanly via `db2-export --dbd-dir`; the three
  `collectablesource*sparse.db2` tables decode every record with no errors
  and their one array-typed field (`float[3]` map-position columns)
  reinterprets to plausible real in-game coordinates. Non-None storage
  types (Bitpacked/CommonData/BitpackedIndexed(Array)) are implemented the
  same way for offset-map records but **not verified** against real bytes
  -- none of the 5 found locally use them; said so explicitly in `db2.hpp`'s
  module comment and `README.md`, not overclaimed. New tests: 7 synthetic
  cases in `tests/test_db2.cpp` (plain numeric record, real inline string
  shifting a later field, the two short-trailing-string cases, the
  high-byte-value case, a genuinely truncated record throwing, an
  out-of-range record index throwing) via a new `buildOffsetMapFile` helper
  (had its own bug caught by the suite itself: forgot the trailing
  `offset_map_id_list` block `db2::parse` always reads once `flags & 0x01`
  is set, regardless of relationship data -- every offset-map fixture in
  this file must include it), plus a real-data-gated CLI test in
  `tests/test_cli_db2.cpp` against the real `scenescripttext.db2` export
  (skips cleanly when the file isn't present locally). Full suite green,
  586/586.
- **Prior session**: Closed `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 1
  "non-inline relationship data decoded structurally but not yet folded
  into the exported table itself" gap -- `husk db2-export` (both single-file
  and `--dir` modes, `src/cmd_db2.cpp`) now emits a real SQLite column and
  `FOREIGN KEY` for a `$noninline,relation$` DBD field (occupies no WDC5
  field-array slot at all -- its value lives only in the section's own
  `relationship_map`), not just an inline one. New `LoadedFile::
  nonInlineRelationColumns` (resolved via `dbd::findNonInlineNonIdFieldNames`
  + a by-name lookup into the DBD table's own `COLUMNS` block for the
  relation target, same pattern `dbdNames`/`nonInlineIdColumnName` already
  use); `writeFileTable` decodes each section's `db2::
  nonInlineRelationValuesByRecord` once per section (shared across every
  non-inline relation column in that table, since a section carries exactly
  one `relationship_map`, not one per DBD field) and binds
  `sqlite3_bind_null` for any record with no map entry rather than
  fabricating a value. Both helper functions this relied on
  (`db2::nonInlineRelationValuesByRecord`, `dbd::
  findNonInlineNonIdFieldNames`) already existed from an earlier session's
  `chrmodel_db2.cpp`/`db2table.cpp` work -- this session's actual gap was
  narrower than it first looked: `db2-export` itself never called either
  one, despite both being already-proven. Verified against real local data,
  not just synthetic: copied the real `chrmodeltexturelayer.db2` (922 rows,
  a real `$noninline,relation$` `CharComponentTextureLayoutsID` under its
  own layout) and `charcomponenttexturelayouts.db2` (4 rows) into a scratch
  `--dir` batch against the real `reference/WoWDBDefs` checkout -- the
  resulting table gets a real `"CharComponentTextureLayoutsID" INTEGER`
  column and `FOREIGN KEY ... REFERENCES "CharComponentTextureLayouts"
  ("ID")` constraint; every one of the 922 real rows resolves a non-null
  value (`COUNT(*) == COUNT(CharComponentTextureLayoutsID)`), and a real SQL
  `JOIN` returns rows for every one of the 4 layout IDs actually present in
  this (necessarily partial) local export -- the low join-hit-rate (40/922)
  is the local export's own real incompleteness (`CharComponentTexture
  Layouts.db2` only has 4 rows here), not a bug in the relation resolution
  itself. Two new tests in `tests/test_cli_db2.cpp`: a synthetic
  `buildDb2WithNonInlineRelation` fixture (one inline ID field, one field
  stored purely in the `relationship_map`, position-based since
  `header.flags & 0x02` is clear) proving the column/value/FK/join all work
  end to end, and a real-data-gated test (skips cleanly, doesn't fail, when
  `reference/WoWDBDefs` or the real local `.db2` files aren't present)
  against the exact real chain above. `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s
  Stage 1 paragraph and `README.md`'s `db2-export` section both updated to
  describe the new behavior instead of the old "diagnostic-only" gap;
  Stages 2-5 of that TODO file untouched, out of this session's scope. Full
  suite green, 578/578.
- **Prior session**: `TODO/GEOSET_MASK_TODO.md`'s two "Known bugs" (real
  interactive-Blender findings from 2026-08-08 — arms disappearing when
  switching an unrelated hairstyle group; the tabard-flap dropdown never
  toggling) are now genuinely confirmed fixed, not just assumed — the file
  itself is deleted per this project's own "punch list, not historical
  record" convention. Root cause of why the file's own two prior headless
  verification attempts kept producing confusing, self-doubting results
  (an "ordinal-vs-identifier confusion," then a "0/26 tabard vertices
  matched in any state" dead end): both used position-matching against an
  `evaluated_get`'d depsgraph mesh, which this session found is invalid for
  this pipeline — a genuinely untouched, unposed Armature modifier still
  shifts "rest pose" vertex positions by 0.1-0.3 units (confirmed via a
  calibration check against a known-always-present vertex), nowhere near
  float-noise scale, so no evaluated position ever matched its pristine
  bind-pose counterpart within any sane tolerance, real match or not. Fixed
  by building a debug Geometry Nodes modifier on a separate duplicate
  object that computes the exact same `_build_group_hidden_term` boolean
  expression the real script uses but stores it as a per-point `BOOLEAN`
  attribute with **no** `Separate Geometry` call at all — point-domain
  index order is then guaranteed identical to the pristine mesh, so the
  attribute can be read back by plain vertex index, no position-matching
  needed. Also empirically nailed down the Menu Switch modifier's own raw
  int-to-item mapping the file's own investigation left as an unconfirmed
  "leading theory" (`mod[identifier]` is a plain int; real items start at
  raw value 2, i.e. `item_index + 2`; out-of-range ints clamp to the same
  behavior as the highest-index item) — confirmed by systematically probing
  every int from 0-27 against known ground-truth vertex-group membership,
  not asserted. With the correct mapping, group 12's own real per-item
  vertex sets are exactly disjoint and exactly cover what each item name
  implies: `variant_2` ("both") shows precisely its own 104 tagged
  vertices (0 of variant_3/4's), `variant_3` ("back") its own 64,
  `variant_4` ("front") its own 40, `none` (and every out-of-range int)
  shows none of the 208-vertex union — the file's own "both evaluated to
  fewer vertices than front-only" anomaly was entirely an artifact of the
  old investigation's wrong int/item mapping, not a real bug, once the
  correct mapping is used. Bug 1 (group 0 `SKIN_OR_HAIR`'s
  `ALWAYS_VISIBLE_VARIANTS` fix, landed in the now-also-deleted
  `TODO/BLENDER_SCRIPT_TODO.md`, entry below) is independently confirmed
  the same rigorous way: the base-body vertex group (260 vertices) stays
  visible across all 28 real/synthetic states tested for group 0's own
  selector, while a sample real hairstyle variant (159 vertices) appears
  only at its own correct state and nowhere else — no cross-contamination,
  no accidental hiding. Verification scripts are scratchpad-only, not
  committed (same tier as this project's other one-off headless-Blender
  probes) — real command used: `direnv exec . blender --background
  --factory-startup --python <script> -- tools/husk_blender_geoset_mask.py
  example_exports/character/bloodelf/female/bloodelffemale_hd.glb`.
  Cleanup: ~15 now-dangling `TODO/GEOSET_MASK_TODO.md` citations across
  `README.md`/`TOOLS.md`/`DESIGN.md`, this script's own module docstring,
  and 7 C++/test source files (`src/gltf_mesh.cpp`, `src/cmd_export.cpp`,
  `src/gltf_skeleton.hpp`/`.cpp`, `src/gltf.hpp`,
  `src/gltf_skeleton_internal.hpp`, `tests/test_conformance.cpp`,
  `tests/test_gltf_skeleton.cpp`, `tests/test_integration_weapons.cpp`)
  rewritten to explain the mechanism inline instead of citing a file that
  no longer exists — same precedent as `TRANSFORM_TRIAGE.md`'s own
  deletion pass. Nothing needed folding into `DESIGN.md`'s "Anecdotal
  geoset-group semantics" table — it already existed there, independent of
  the now-deleted TODO file. Full C++ test suite unaffected and confirmed
  green (this was a Python/doc-only session) — see the entries below for
  everything the actual geoset-switch mechanism/design already covers.
- **Prior session**: fixed both real findings from `TODO/BLENDER_SCRIPT_TODO.md`
  (that file now deleted per this project's own "punch list, not historical
  record" convention — both closed and verified). (1) The texture-layout
  overlay's three new nodes (`ShaderNodeGroup`/`ShaderNodeEmission`/
  `ShaderNodeMixShader`, `apply_texture_layout_overlay`) previously got no
  `.location` at all, landing at the node tree's origin — very likely
  overlapping this material's own existing graph, effectively invisible
  without manually dragging nodes apart. Now anchored below the existing
  graph's own lowest node (`min(n.location.y for n in node_tree.nodes) -
  400`), offset left of the Material Output — guaranteed clear of whatever
  this material's own layout already occupies. (2) The "skirt/tunic
  fragment stays visible with every geoset toggled off" report traced to a
  real, previously-undocumented design gap, not a rendering bug: geoset
  groups with only *one* real M2 variant (this model's groups 10/23/33/34)
  were skipped by `build_geoset_switch_node_group`'s old `len(variants) >=
  2` gate entirely, so there was no way to tell "always shown, no
  alternative exists" from "should be toggleable but the toggle is
  missing" — every group now gets a switch as long as it has at least one
  *switchable* variant (new `switchable_variants` helper), gaining a real
  synthetic "none" choice same as any multi-variant group, per Luna's own
  direct steer that Blizzard's own runtime customization system can offer
  "none" without it needing to exist as a real geoset ID in the file
  (same precedent as the already-closed tabard "no submesh for 'no
  tabard'" gap). Separately, real headless
  investigation (`husk`'s own `.glb` extras, not guessed) confirmed
  Luna's own second suspicion — "arms is still part of hair 0, hair 0
  needs to stay enabled while ALSO enabling other hair options" — as a
  real, distinct bug with a concrete root cause: DESIGN.md's own
  "Anecdotal geoset-group semantics" table already names geoset group 0
  `SKIN_OR_HAIR` (two independent community reference tables agree, not
  just "Hair" as this project's own earlier visual read assumed) —
  variant 0 within that group is the character's own base skin body
  (torso/arms/legs, confirmed via the real `.glb`'s own material
  assignment: geoset_id 0's two primitives use `skin_color`/`blindfold`
  materials, not the `hair_color` material every other variant 1-24 in
  that group uses), not a real hairstyle option — WoW's own geoset
  numbering just co-locates them under one group ID. Treating variant 0 as
  just another mutually-exclusive hairstyle choice made the base body
  vanish whenever a real hairstyle was picked. Fixed with a new
  `ALWAYS_VISIBLE_VARIANTS` table (currently only `{0: {0}}`, explicitly
  scoped to this one confirmed case, not generalized to every group
  without more evidence) — variant 0 is now excluded from group 0's
  switch entirely (never hidden, never a dropdown item), same as an
  untagged base vertex; `CURATED_DEFAULT_VARIANTS`'s own group-0 entry
  updated from `"variant_0"` to `"none"` to match (variant 0 is no longer
  a selectable item at all). Verified headlessly against the real curated
  `bloodelffemale_hd.glb` export: group 0's dropdown now offers only
  `variant_1..24`/`none` (never `variant_0`), 23 dropdown switches built
  (was 19 — the 4 newly-switchable single-variant groups), every overlay
  node lands at a real non-origin location distinct per material's own
  existing graph, full `main()` run completes with no errors. No `src/`
  changes — this was pure `tools/husk_blender_geoset_mask.py` Python work,
  C++ test suite unaffected.
- **Prior session**: `tools/husk_blender_geoset_mask.py` (already the
  established "post-import companion script" per Luna's own direct steer,
  not a new file) got a second, independent job this session: parses
  `chr_texture_layout` (previous entry) and adds a toggleable magenta
  section-boundary overlay to every material it concerns. Real finding
  along the way, confirmed empirically (headless-Blender introspection, not
  assumed): Blender's stock glTF importer has **no supported extras target
  for a glTF skin at all** — node/mesh/material/camera/light/scene extras
  all land as real Blender custom properties post-import, skin extras land
  nowhere — so `chr_texture_layout` is invisible to a plain File > Import,
  unlike every other extras this project attaches. The script now re-opens
  the exported file's own raw glTF JSON directly (`read_chr_texture_layout`,
  manual glb chunk parsing) to get at it, independent of whatever bpy's
  importer already did; material-level `texture_type` (used to decide which
  materials the layout "concerns") comes through as a real custom property
  normally, no raw-JSON reading needed for that half. One shared shader
  node group (`_build_section_overlay_group`) computes an axis-aligned
  box-test OR-of-ANDs mask over every real `CharComponentTextureSection`
  rect (Shader Editor's `ShaderNodeMath` has no boolean-logic mode unlike
  Geometry Nodes' `FunctionNodeBooleanMath`, so MIN/MAX substitute for AND/
  OR), gated by a real `NodeSocketBool` group input — a plain checkbox on
  the node once instanced, no Properties-panel promotion needed the way the
  Geometry Nodes Menu Switch case required. Per concerned material, a new
  `Mix Shader` sits between the *existing* (untouched) shader output and
  the Material Output, so switching the overlay back off exactly reproduces
  the original look. **Flagged, not verified**: whether WoW's real atlas Y
  axis is top-down (assumed here, flipped against Blender's bottom-up UV V)
  has not been independently ground-truthed the way the M2→glTF coordinate
  fix was — a human should confirm the overlay lands in the visually
  correct spot before trusting its exact placement. Verified structurally
  (headless Blender): real end-to-end run against `bloodelffemale_hd.m2`'s
  own `chr_texture_layout` (layout 1) touches the expected 3 materials
  (two share `texture_type` 6), builds exactly one shared node group
  instance, each material gets exactly one `Mix Shader` wired into its
  Material Output, toggling the boolean input doesn't crash; a same-model
  export with no `--char-layout-id` skips cleanly with a diagnostic, no
  crash. No automated test suite exists for this script (Blender-only,
  outside `husk`'s own C++/CTest scope, same as `husk_blender_geoset_mask.py`'s
  existing geoset-switch half) — verification is headless-Blender runs,
  same tier as the rest of this file's own testing discipline for Blender
  tooling. `README.md` updated (both the geoset-mask paragraph and the
  Stage-2 paragraph now cross-reference each other).
- **Prior session**: `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 2
  ("real placement geometry") implemented — the first real consumer of
  locally-extracted DB2 data inside `husk export` itself, not just the
  separate `db2-export` side tool. New `src/db2table.hpp`/`.cpp` (generic
  named-column reader, built on `db2.hpp`/`dbd.hpp`) and `src/
  chrmodel_db2.hpp`/`.cpp` (real typed `ChrModelMaterial`/
  `CharComponentTextureSection`/`ChrModelTextureLayer`/
  `CharComponentTextureLayout` structs on top of it) back new `husk export
  --db2-dir/--dbd-dir/--char-layout-id` flags, attaching real placement
  geometry as `chr_texture_layout` glTF skin `extras`
  (`gltf::Skeleton::CharTextureLayout`) — verified end to end against the
  real `bloodelffemale_hd.m2` + its own real DB2 chain, headless-Blender-
  import-clean. Closed a real, previously-unreachable byte-format gap along
  the way: a WDC5 `$noninline,relation$` column's value (real for
  `ChrModelTextureLayer`'s own `CharComponentTextureLayoutsID`) now resolves
  via a new `db2::nonInlineRelationValuesByRecord`/`dbd::
  findNonInlineNonIdFieldNames` pair, reading the section's own
  `relationship_map` instead of a nonexistent field-array slot. Deliberately
  scoped down from the original TODO wording: does NOT attach placement
  data to individual `alternate_textures` candidates (needs `ChrModel.db2` +
  a real display-ID identity husk doesn't have, Stage 3's own open
  problem) — the caller supplies the layout ID directly instead. Full suite
  green, 575/575. See `CLAUDE_HISTORY.md`'s top entry for the full detail
  (new test files, completions regeneration, doc updates).
- **Prior session**: `TODO/DB2_SQLITE_SCHEMA_TODO.md` fully implemented and
  the file itself deleted (all four staged steps done, nothing left open in
  it) — `husk db2-export` now has a real relational schema, not just one
  flat table per `.db2` file. Step 1: `dbd::parseColumnType`'s `<Table::Col>`
  foreign-key-target suffix is captured into a new `dbd::RelationTarget`/
  `dbd::Column` (replacing the old bare `pair<name, type>`), verified
  against real `ChrModelMaterial.dbd` data. Step 2: WDC5's non-inline
  `relationship_mapping` (an alternate per-section foreign-key storage,
  `{foreignId, recordId}` pairs) is now decoded structurally
  (`db2::Section::relationshipEntries`, `db2-info` prints a summary) — found
  and fixed a real ordering bug along the way, caught only by checking
  actual bytes: DB2.md's own WDC5 struct pseudocode notes that
  `offset_map_id_list` moves *before* `relationship_map` when header flag
  0x02 is set, which the original `db2::parse` didn't implement (always
  read `relationship_map` first); verified against the real
  `collectablesourceencountersparse.db2` file (the only local fixture with
  both bits set) — 46,311 relationship entries decoded, foreign IDs
  sequential, record-ID values landing exactly in the table's own declared
  ID range, confirming both the byte layout and DB2.md's "uses record IDs
  instead of record index" semantic note for this flag combination. Step 3:
  new `husk db2-export --dir <db2-dir> <out.sqlite>` mode exports every
  `.db2` file in a directory into one SQLite database (a bad/empty file is
  skipped with a diagnostic, not fatal to the batch); a column with a real
  relation target gets a real SQLite `FOREIGN KEY` constraint whenever the
  target table is also part of the same batch, degrading silently to a
  plain column otherwise. Step 4: verified end to end against the real
  `chrmodelmaterial.db2` -> `charcomponenttexturelayouts.db2` chain — a real
  `--dir` export produces a working `FOREIGN KEY`, and a real SQL `JOIN`
  across the two tables returns correct, matching rows. That verification
  surfaced one more real gap, fixed in the same session: `Char
  ComponentTextureLayouts` has `header.flags & 0x04` ("has non-inline
  IDs") — its real ID lives in WDC5's own separate `idList` array, not in
  the field array at all, so the exporter previously emitted no `ID` column
  for it whatsoever, silently making the join impossible. Fixed with a new
  public `db2::recordId` (idList when present, else the inline field at
  `header.idIndex`) and `dbd::findIdFieldName` (resolves a `$noninline,id$`
  layout field's real name; deliberately excludes plain `$id$` fields,
  already covered by the normal by-position path) — every non-inline-ID
  table now gets a real, named ID column, only in `--dir` mode's own
  writeFileTable (single-file mode is unaffected). Full suite green,
  565/565; new tests include two `dbd::findIdFieldName` unit cases, two
  synthetic `db2::parse` relationship-map cases (one of which — the
  reorder case — was confirmed to actually fail without the fix, not just
  pass with it), a `db2::recordId` non-inline-ID case, and a real CLI test
  building two synthetic, DBD-related `.db2` files and running an actual
  `FOREIGN KEY` `JOIN` against the resulting SQLite output.
- **Prior session**: real DB2 column naming + a real DB2-to-SQLite exporter
  landed, same session as the DETL/DPIV/PCOL investigation and the root-doc
  reorg below. `src/dbd.hpp`/`.cpp` (new) is an independent parser for
  WoWDBDefs' own documented `.dbd` text grammar (`github.com/wowdev/
  WoWDBDefs`) — resolves a real `.db2` file's `table_hash`/`layout_hash`
  (already parsed by the existing `src/db2.hpp` POC) against a local,
  optional WoWDBDefs checkout to get real per-field names/types, matched by
  position (declaration order, skipping `noninline` fields) — never a hard
  dependency (husk doesn't clone/fetch/bundle WoWDBDefs; `reference/
  WoWDBDefs`, gitignored, is dev-only investigation scaffolding, matching
  `reference/wow.export`'s existing role, never read at runtime; `--dbd-dir`
  is a local, optional, user-supplied directory, same tier as `--textures`/
  `--skin-dir`). Verified against real data via a new manifest.json+.dbd
  lookup, tested against the real `ChrModelMaterial` table. New command,
  `husk db2-export <file.db2> <out.sqlite> [--dbd-dir DIR]`
  (`src/cmd_db2.cpp`), writes a real SQLite database (`pkgs.sqlite`, newly
  added to the flake with Luna's permission) — one table per file, real
  column names when resolved, generic `field_<N>` otherwise, one `<name>_<i>`
  column per real WDC5 array element (SQLite has no array column type).
  Verified end to end against real data: `chrmodelmaterial.db2` exports 336
  rows with real `ID`/`CharComponentTextureLayoutsID`/`TextureType`/`Width`/
  `Height`/`Flags`/`Field_9_0_1_34615_006` columns and plausible real atlas
  dimensions (2048x1024, 512x256, ...); `namesreserved.db2` round-trips real
  UTF-8 strings (including non-Latin text) correctly. Per Luna's own direct
  scope clarification, this SQLite exporter is an explicitly separate side
  project (human inspection/correctness-checking, and a future data source
  once world-data work starts) — `export`'s own runtime path still doesn't
  read DB2 data, and Stage 2 of `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`
  (real per-table C++ structs feeding `export_materials.cpp`) is still not
  started. Also fixed a real doc-comment inaccuracy caught by Luna directly:
  `db2.hpp`'s old comment cited DB2.md's "Determining Field Types" section
  as the reason WDC5 carries no column names — that section is actually
  about inferring a field's *type* (int/float/string), says nothing about
  naming, and doesn't exist where the comment implied; corrected in
  `db2.hpp` and `README.md`. Full suite green, 557/557, 3 new test files
  (`tests/test_dbd.cpp` unit tests including one real-data case against
  `reference/WoWDBDefs`, `tests/test_cli_db2.cpp`'s two synthetic
  CLI-boundary tests reading the real SQLite output back via the sqlite3 C
  API). **Next step**: `TODO/DB2_SQLITE_SCHEMA_TODO.md` (new) — the SQLite
  exporter's own stated ambition, a real relational schema with mapping/
  join tables preserving cross-file foreign-key relationships (e.g.
  `ChrCustomizationOption` -> `_Choice` -> `_Material`), not just one flat
  table per `.db2` file. Staged four ways: capture the `<Table::Col>`
  foreign-key target `dbd::parseColumnType` currently parses and discards;
  decode non-inline `relationshipData` (currently only skipped, byte-offset
  bookkeeping only, `db2::parse`); a multi-file export mode emitting real
  `FOREIGN KEY` constraints; real join verification against whatever chain
  is actually populated locally (the fuller `ChrCustomizationOption` chain
  can't be fully verified yet -- several of its own tables are still
  0-byte). Not started in `src/` yet; today's exporter is genuinely
  one-file-in, one-table-out.
- **Same session, earlier**: root-directory cleanup — every self-described
  "open punch list" `*_TODO.md` file moved from repo root into `TODO/`
  (17 files), with the 11 world-specific ones plus
  `TEXTURE_TYPE_COLLISIONS_REPORT.md`/`NOTE_ABOUT_WORLD_HANDLING.md` moved
  one level deeper into `TODO/WORLD/`; every cross-reference across docs and
  source comments fixed (several hundred sites). `EYES_ON_FINDINGS.md`
  pruned of 3 fully-resolved items (bone naming, root-bone weighting, the
  `Submesh::Level` bug), 3 genuinely open ones kept and renumbered.
  `HUSK_CORPUS_FINDINGS{,2}.md` deleted (fully migrated into `README.md`/
  `TOOL_COMPARISON.md`); `INLINE_COMMENT_RULES_VIOLATIONS.md` condensed into
  a pending-cleanup note at the top of `README.md`, then deleted. Also a
  short DETL/DPIV/PCOL investigation: DETL's `scale`/`diffuseColorMultiplier`
  confirmed dead constants in every real file (only `flags` bit 3 varies,
  plausibly a shadow-casting toggle); DPIV's byte structure characterized
  (new `TODO/DPIV_TODO.md`, concrete next steps, not yet solved); `PCOL`'s
  bit-semantics gap reframed from "permanent data-source gap" to "real but
  currently unfulfillable DB2 dependency" after finding `housedecor.db2`
  (the obvious candidate table) is 0 bytes in the local export — the same
  finding that led into the DB2/DBD work above.
- **Investigation-only session**: closed `PCOL`'s long-standing "`flags`'
  per-record meaning is undocumented — exposed raw" gap as far as real
  corpus data allows. A full 2,354-file scan (`pcol_files_for_exploration.txt`,
  husk's own already-verified `dump-chunks` output) found `flags` is
  structurally a real per-triangle bitmask, not an enum (every distinct
  value decomposes into a small combinable bit set; 98.4% of files use
  only bit 0; every rarer value is a singleton confined to one specific
  decorative doodad) — individual bit *semantics* remain unconfirmed, a
  DB2/client-data gap, not an M2-side one. Also found `flagsCount` isn't
  always `faceNormCount` (3/2,354 real files differ) — already handled
  correctly by the existing parser, no code change needed. Pure
  documentation update: `WIKI_FINDINGS_HISTORY.md` §16 (new), `WIKI_FINDINGS/
  M2.md`, `DESIGN.md`, `M2_COMPLETENESS.md`, `src/dump_chunks_misc.hpp`'s
  doc comment. See `CLAUDE_HISTORY.md`'s top entry for the full detail.
- **Independent, unsupervised tasks, same session, all three committed as
  `[UNVERIFIED/STAGING]` per Luna's own instruction, awaiting her review**:
  (1) `TODO/TODO_correctness.md`'s former item 2 (five unconsumed M2
  lookup-table arrays) — `husk info` dereferences `sequenceLookup`/
  `boneLookup`/`textureLookup`/`attachmentLookup`/`cameraLookup`, verified
  against real `wolf.m2`/`bloodelffemale_hd.m2` data. (2) `M2Light`'s 7
  animated `M2Track` fields (ambient/diffuse color+intensity, attenuation
  start/end, visibility) — resolved into `type`/`light_animation` node
  extras, reusing `gltf::Material::AnimatedColorCurve`/`AnimatedScalarCurve`;
  verified against a new real fixture, `ui_mainmenu_pandaria.m2`
  (`test_data/interface/glues/models/`, gitignored per convention), the
  only real fixture in this repo confirmed to actually have `M2Light` data.
  (3) `M2Attachment::animate_attached` (same M2Track<uint8_t> shape as
  Light's `visibility`, noticed while doing (2)) — resolved via the same
  (renamed, generalized) helper; real fixtures (`wolf.m2`/two weapon
  models, 23 attachments) all resolve to empty curves, a checked real
  negative result, not a bug — covered by synthetic tests instead. Full
  suite green, 546/546. See `CLAUDE_HISTORY.md`'s top three entries for
  the full detail; unrelated to every thread below, none of which this
  session touched.
- **Current state**: `TODO/GEOSET_MASK_TODO.md` implemented (new this session)
  but **not yet correct** — real bugs found via actual interactive Blender
  use, investigation started but not resolved, continuing in a fresh
  session/thread. C++ side (`src/gltf_skeleton.{hpp,cpp}`, `gltf_mesh.cpp`,
  `cmd_export.cpp`) is solid and fully tested: every export now carries one
  inert "tag" joint per distinct geoset ID, appended to the skin strictly
  after every real bone, woven into a second `JOINTS_1`/`WEIGHTS_1` set
  named `group_<n>,variant_<n>` (comma-separated, prefix-tagged fields, not
  a single combined token, specifically so a consumer can comma-split +
  prefix-strip instead of doing `id/100`/`id%100` math). Blender's *stock*
  glTF importer turns this into a real per-geoset vertex group with zero
  custom import tooling, verified empirically before any of this was
  written (Armature modifier renormalizes total weight across joint sets
  regardless of what's stored, so a second full weight set doesn't distort
  deformation; vertex groups are mesh-owned data independent of the
  armature's bones, so a fake tag bone can be deleted post-import with the
  group/anything built from it left completely intact). Full C++ test
  suite green, 532/532, unaffected by anything below.

  `tools/husk_blender_geoset_mask.py` (new) went through two real
  designs. The first built a Mask-modifier stack (verified working, but
  90 modifiers on one real export — "an insane stack of mask modifiers,"
  a real usability complaint in its own right). Replaced with a real
  Geometry Nodes graph: one `Menu Switch` dropdown per geoset group (a
  chain of `Separate Geometry` nodes peels each variant off a running
  remainder), confirmed scriptable via `bpy` after several real API
  gotchas found empirically (`GeometryNodeMenuSwitch` starts with two
  placeholder items that must be cleared; its `Menu` input only becomes a
  real modifier-panel dropdown once promoted to a `NodeSocketMenu`
  interface entry and *linked before* its default is set; the exposed
  modifier value is stored by integer index, not name). Aggregate
  correctness looked solid — the default-state evaluated mesh matched the
  superseded Mask-modifier version's own vertex count exactly (4,232), and
  switching one group's dropdown changed the count as expected.

  **Real interactive use the same day found that aggregate-count
  verification wasn't enough**: (1) picking a different hairstyle (geoset
  group 0) makes unrelated arm geometry disappear; (2) the tabard back-flap
  geometry never disappears no matter what's selected. This session's own
  follow-up investigation (see `TODO/GEOSET_MASK_TODO.md`'s "Known bugs"
  section for the full detail) ruled out two things with hard evidence —
  husk's own C++ export has zero cross-*group* vertex tagging, and the
  node graph's wiring/`Compare`-node defaults are correct — and found one
  strong, unconfirmed lead: `GeometryNodeSeparateGeometry` with `domain=
  'POINT'` (the default, never overridden) does not cleanly partition
  geometry — a synthetic repro showed a face straddling a selection
  boundary vanishes from *both* the Selection and Inverted outputs
  entirely, a real structural risk in a design that chains 109 sequential
  separations. Also found a suspicious, unresolved discrepancy between the
  modifier's raw stored default value and this session's own assumption
  about ordinal-vs-identifier indexing for Menu Switch items, which may
  mean some of tonight's own verification scripts were reading their own
  results wrong rather than exposing a second real bug.

  **Update, same night**: Luna manually ground-truthed group 12 in
  Blender's real GUI — it does control the tabard flaps
  (`variant_2`=both, `variant_3`=back, `variant_4`=front), and found a
  real, separate gap: no "none" option exists, because the M2 itself has
  no submesh for "no tabard" (geoset ID 1201 absent from this file's own
  `.skin` data — a real fact about the model, not a husk bug). Also
  proposed the real architectural fix directly: don't chain `Separate
  Geometry` against a shrinking remainder; compute one boolean-math
  expression per vertex first, apply exactly one `Separate`/`Delete
  Geometry` at the end. Implemented as a single design: a `STRING`-typed
  `Menu Switch` per group outputs the *name* of the currently-selected
  variant's vertex group (or a sentinel matching nothing, for a new
  synthetic "none" item, closing the gap above for every group at once),
  which feeds `Named Attribute`'s `Name` input as a *link, not a
  constant* — confirmed scriptable — collapsing what used to be 109
  chained geometry operations down to one boolean tree plus exactly one
  final `Separate Geometry` against the pristine input mesh.

  **Verification hit real limits a second time, not resolved.** A first
  headless check showed vertex counts frozen at one value across every
  switch tried (impossible if working) — turned out to be a real
  scripting gotcha (`mod[identifier] = ...` doesn't propagate without
  also calling `mod.node_group.interface_update(bpy.context)`), not a
  graph bug. Fixed that and counts did start responding to switches. But
  a targeted check tracking all 26 real tabard-flap vertex positions
  across all four of group 12's states found **zero of 26 present in any
  state**, and `variant_2` ("both") evaluated to *fewer* vertices than
  `variant_4` ("front only") — backwards if "both" is really their union.
  Given headless position-matching has now produced one confusing result
  on this feature already (the ordinal-vs-identifier confusion above),
  this was handed back rather than chased further blind — **needs Luna's
  own real interactive Blender GUI testing**, the same method that
  correctly found both original bugs and correctly ground-truthed group
  12's real semantics tonight. See `TODO/GEOSET_MASK_TODO.md`'s "Real bug
  ground-truthed by hand..." section for the concrete next step (click
  through group 12's dropdown by hand in the Modifier panel, watch the
  actual viewport). Full C++ suite unaffected throughout, still green,
  532/532 — everything above is pure Python/Blender-script work.
- **Current state (prior session)**: Closed `TODO/TODO_correctness.md`'s former item 4
  (texture-transform pivot-correction math) end to end. `gltf_mesh.cpp`'s
  new `textureTransformToKhr` derives a real `KHR_texture_transform`
  (offset/rotation/scale) from a constant `M2TextureTransform`'s
  texture-center-pivoted rotation (`offset = R*S*translation + R*t_S +
  t_R`), applied on `baseColorTexture` whenever the record is genuinely
  constant (every track either empty or a true single value) and the
  rotation is planar (Z-axis only) -- verified three independent ways
  against real `bloodknightcharger.m2` data (its transform index 2, a
  180-degree rotation + non-uniform (1.0, 1.5) scale): by hand, against
  20,000 randomized trials of `reference/wow.export`'s own
  translate-rotate-translate matrix composition, and via headless
  Blender's own glTF importer producing an exactly-matching Mapping node
  (location (1.0, 1.25), rotation 180 degrees, scale (1.0, 1.5)). Found a
  real, useful negative case along the way: `brewfestmount.m2`'s own
  transform index 0 looks constant under a cruder single-keyframe check
  (`tools/find_texture_transform_files.py`, this session's own discovery
  tool) but actually carries per-sequence-structured translation/scaling
  tracks whose values just happen to all be identity -- husk's own
  stricter `trackHasAnimatedData` check correctly refuses to treat that as
  constant, so it stays extras-only, not a false positive. Two new real
  fixtures committed (`test_data/creature/brewfestmount/`,
  `.../bloodknightcharger/`, `.m2`+`.skin` only -- textures resolved via a
  synthetic 1x1 PNG in tests, real texture bytes weren't needed to verify
  the transform math), six new tests (four synthetic in
  `test_gltf_mesh.cpp`, two real-fixture integration tests). Full suite
  green, 530/530. Real `gltf_validator`/headless-Blender verification
  clean for the new extension specifically (a pre-existing, unrelated
  JOINTS_0/WEIGHTS_0 duplicate-influence data quirk in `bloodknightcharger.m2`
  itself produced validator errors on that one fixture -- confirmed
  unrelated by checking a known-clean fixture still validates 0 errors
  after this change; not investigated further, out of this task's scope).
- **Current state (prior session)**: Resolved the previous entry's own open question with
  two real screenshots -- `bloodelffemale_hd_skin_color_3500119`/`_3500115`
  each pixel-match one specific rectangular region of `_3500123` (the base
  atlas) exactly, non-transparent overlay *patches*, not junk or unrelated
  assets as the "tiny decal" framing implied. Investigated the real
  mechanism directly in `reference/wow.export`
  (`CharMaterialRenderer.js`/`DBCharacterCustomization.js`): real client
  compositing is driven by `ChrModelMaterial` (base atlas size),
  `CharComponentTextureSections` (`X`/`Y`/`Width`/`Height` placement
  rects), and `ChrModelTextureLayer` (blend mode per layer) -- confirmed,
  named DB2 tables, not a guess, and squarely CASC/DB2 data husk has no
  access to by design. What husk *can* do: `AlternateTextureCandidate`
  now carries real `width`/`height` (`src/gltf_mesh.hpp`/
  `export_texture_resolution.cpp`'s new `pngDimensions`, emitted as
  `alternate_textures[].width`/`.height`) so a human/script can tell a
  full atlas apart from a small patch without decoding each candidate by
  hand -- not the real placement data, but real, useful, already-load-
  bearing metadata. Full suite green, 524/524.
- **Current state (prior, same session)**: A fourth correction, prompted by Luna trying to
  manually locate `bloodelffemale_hd_skin_color_3500121` in Blender and
  getting confused about where it fit (a real full-body atlas variant per
  her description, "just the body, with the underwear... completely
  different uv layout"). Investigating turned up a real bug beyond
  answering the question: `bloodelffemale_hd`'s twelve `skin_color`-
  category files split into two size classes when actually decoded --
  eight are 256x128 small strap/underwear-decal graphics, four
  (`3500122`-`3500125`) are the real 1024x512 full-body atlases -- and the
  previous entry's "prefer skin_color" rule picked whichever sorted
  alphabetically first among *all* of them, landing on a tiny decal, not
  an atlas. Fixed with a new signal: `pngPixelArea` (`src/export_materials.cpp`)
  reads a candidate's real width x height from its own PNG IHDR chunk
  (already-decoded bytes, no extra pass), and `orderCandidatesForDefault`
  now ranks by pixel area first (largest wins), falling back to the
  `skin_color` category preference only as a same-area tiebreak (needed
  since `body_jewelry` is a correct, same-resolution candidate for this
  slot too). A real performance regression was caught in the same pass:
  the first version re-decoded every candidate once per batch (~27 batches
  x ~60 files) just to sort them, timing out past 120s -- fixed by sharing
  the existing `ambiguousCandidateCache` into the ranking function instead
  of a fresh local one, back down to ~4.6s. Two new regression tests using
  a new `solidColorPng` fixture generator (`tests/test_cli_fixtures.hpp`
  -- every prior fixture used one fixed 1x1 PNG, insufficient for testing
  size-based ranking), each proven to fail with its own signal disabled.
  Full suite green, 524/524. Still unresolved: whether `3500121`
  specifically (decoded: a small decal) is really what Luna meant, given
  her own description sounds like a full-body-scale asset -- flagged back
  to her, not assumed reconciled.
- **Current state (prior, same session)**: Immediate refinement to the entry below's own fix --
  told directly that `body_jewelry`/`bracelets` are texture *overlays*
  composited onto the skin texture (no UV map of their own, same family
  as `skin_color`/`face`), while `jewelry_color` textures a genuinely
  separate 3D jewelry mesh with its own UV map -- excluding
  `body_jewelry`/`bracelets` from type 20 was right, but leaving them
  unclassified was incomplete. `candidateCategoryTypes`
  (`src/export_materials.cpp`) now maps them to `{1, 8}` (skin/skin_extra)
  explicitly. Verified: the `skin`-type material's candidate pool includes
  them again as real overlay candidates, `char_jewelry` still sees only
  its own two `jewelry_color` files. Existing regression tests unaffected
  (none assumed *where* these tokens mapped, only that they weren't type
  20). Full suite green, 523/523.
- **Current state (prior, same session)**: One more real correction, same session --
  `LUNA_FINDINGS.md` (not `LUNA_NOTES.md`, a misnamed pointer corrected
  directly after this session reported the wrong file had no new content)
  confirmed the material-dedup and `char_hair`/`eyereflect` fixes below by
  real Blender verification, and found `candidateCategoryTypes`
  (`src/export_materials.cpp`) had also wrongly mapped `body_jewelry`/
  `bracelets` to type 20 (`char_jewelry`) alongside `jewelry_color` on an
  unverified English-name assumption -- viewed directly (`husk-blp`),
  `body_jewelry_3602029` is a visually distinct necklace-chain item, not
  a color variant of `jewelry_color`'s gold/silver collar design, no
  confirmed type-20 evidence for it. Fixed by removing both from the
  category table entirely (no reassignment without evidence). Verified:
  `char_jewelry`'s `alternate_textures` now lists exactly the two
  `jewelry_color` files, matching `LUNA_FINDINGS.md` exactly. New
  regression test, proven to fail without the fix. Full suite green,
  523/523.
- **Current state (two sessions ago, same session)**: Two more real bugs found and fixed, same investigation
  thread, prompted directly with a reference screenshot (correctly-matched
  tan skin/blue hair/silver jewelry) and a concrete complaint ("we REALLY
  need to get ridd of the 500 materials produced by batches... only 1
  material per mat<num>_tex<num>_<id> combination", plus repeated
  `..._body_jewelry_3602029.<N>`-suffixed duplicate images in Blender).
  (1) `src/export_materials.cpp` now computes a real content signature
  (`materialDedupKey`) per fully-built material and reuses an existing one
  via `materialByKey` instead of emitting a new `gltf::Material` per batch
  -- real `bloodelffemale_hd.m2` export: 114 materials → 10. (2) The
  primary embedded image now shares the same cross-material cache
  `alternate_textures` already used, closing the one remaining duplicate-
  image case dedup alone didn't (two genuinely *different* materials
  independently resolving to the same unrecognized-fallback file). (3) A
  real correction to the previous session's own "prefer bare over face"
  default logic: viewed directly via `husk-blp`, the bare
  `bloodelffemale_hd_3255415.blp` file that kept winning the `skin` slot's
  default turned out to be a tiny sparkle icon, not a skin texture -- the
  real full-body atlas was under the *recognized* `skin_color` category
  the whole time (confirmed: default's average color went from
  transparent-black to a real tan (0.44, 0.27, 0.15) matching the
  reference screenshot). `filterCandidatesForType` now always prefers
  recognized-category candidates over bare/unrecognized ones, falling back
  to unlabeled files only when nothing recognized exists at all -- no more
  guessing what an unlabeled file *is*. Two new regression tests, both
  proven to fail without their respective fix. Full suite green, 522/522.
- **Current state (prior, same session)**: Follow-up in the same session, reported directly from
  Blender: embedded images were showing up as auto-generated
  `Image_<N>` names instead of their real, useful source filenames (e.g.
  `bloodelffemale_hd_hair_color_5196731`), because none of
  `gltf_mesh.cpp`'s three image-embedding sites ever set `tinygltf::
  Image::name`/`Texture::name`. Fixed: `Material::baseColorImageName`
  (new field, `src/gltf_mesh.hpp`) is populated at every
  `export_materials.cpp` resolution site that sets `baseColorImagePng`
  (M2's own embedded filename, a `<FileDataID>` exact match, a sole
  fuzzy match, or the chosen candidate out of an ambiguous pool) and
  used to name the emitted image/texture; the `alternate_textures`
  candidates and `additionalTextureLayers` (FileDataID only, no
  filename tracked) get the same treatment. Verified two ways: a new
  unit assertion (`tests/test_gltf_mesh.cpp`) and a real headless-Blender
  import of the actual `bloodelffemale_hd.m2` export, both before/after
  -- 99 images all named `Image_<N>` before, 0 generic names after. Full
  suite green (520/520).
- **Current state (prior, same session)**: Fixed `EYES_ON_FINDINGS.md` #3/#6's ambiguous-texture
  cross-contamination (Luna's own concrete example: a face `.blp`
  showing up as a candidate for a shoes-region `skin`-type material).
  `src/export_materials.cpp` now filters each hardcoded slot's fuzzy-pool
  candidates by a real filename category token
  (`classifyCandidateCategory`, e.g. `"skin_color"`/`"face"`/
  `"hair_color"`/`"jewelry_color"`/`"blindfold"`) matched against which
  `M2Texture::type` values that category is actually compatible with
  (`candidateCategoryTypes`, transcribed from `reference/wow.export`'s
  own character-customization code, not guessed) — a hair-color file no
  longer leaks into an eyes or jewelry slot's `alternate_textures` just
  because both are independently ambiguous. Types 1/8 (`skin`/
  `skin_extra`) are a real, separate case: `wow.export`'s own
  `apply_skinned_model_textures` shows the real client composites
  several layers together for these two, which husk still can't do (no
  DB2 blend-order data, by design) — so `"skin_color"`/`"face"` both stay
  valid candidates there, but a bare/`skin_color` file is now preferred
  as the wired default over a narrower `face` overlay
  (`preferBaseLayerCandidate`), and every candidate's parsed category is
  now attached to its own `alternate_textures` extras entry so a human/
  Blender script can tell what each one actually is. Verified against
  the real `bloodelffemale_hd.m2` + its real CASC texture directory: the
  `skin` slot's pool went from 94 undifferentiated candidates to 57
  correctly-typed ones, `char_eyes`/`char_jewelry`/`ui_skin` slots each
  now see only their own real candidates (9/19/2 respectively), zero
  cross-category leakage. New synthetic regression test
  (`tests/test_cli.cpp`, two hardcoded slots of genuinely different
  `M2Texture::type`s sharing one pool) proven to actually fail without
  the fix before being confirmed green. Full suite 520/520
  (`./build/husk-tests`). See `EYES_ON_FINDINGS.md`'s finding #3/#6 for
  the full writeup, including what's still genuinely unresolvable
  (*which* composited skin/face layer is correct for a given character's
  real customization choices — needs DB2 data husk doesn't have) versus
  what this fix actually closes (structurally-impossible cross-category
  offers).
- **Current state (prior session)**: Fixed the M2→glTF "upside down" export bug for real
  (`TRANSFORM_TRIAGE.md`) — the historical three hand-typed position/
  rotation/scale conversion formulas are now one mechanically-derived
  system (`src/gltf.cpp`'s `kWowToGltf` matrix, corrected from `(x,-z,y)`
  to `(x,z,-y)`, with position/rotation/scale all derived from it rather
  than hand-typed separately). Corroborated three independent ways: the
  change-of-basis math, a real headless-Blender round-trip, and
  `reference/wow.export`'s own independently-written conversion code.
  Covered by a new asset-agnostic synthetic coordinate-frame probe test
  tier (proven to actually catch the bug, not just pass), a property-based
  rotation-matrix unit test, a real humanoid-landmark sanity check, and a
  new quadruped fixture (`test_data/creature/wolf/wolf.m2`). Full suite
  green, 484/484 (`./build/husk-tests`), zero hand-updated literals needed
  anywhere else in the suite. `DESIGN.md`/`README.md`/`TRANSFORM_TRIAGE.md`
  all updated to match. See `CLAUDE_HISTORY.md`'s top entry for the full
  narrative, including the two real corrections Luna made to the plan
  before any code was written.
- **Next step**: `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md` (new this session) --
  the real, staged plan for full DB2-driven character texture compositing.
  Real WDC5 DB2 tables (`ChrModelMaterial`/`CharComponentTextureSection`/
  `ChrModelTextureLayer`, plus the full `ChrCustomization*` choice chain)
  confirmed present as local files in Luna's own real local `casc-tool`
  export (**not** `reference/wow.export`, an unrelated third-party JS tool
  checked out for source-code reference only -- don't conflate the two)
  -- in scope per Luna's own direct clarification ("the only hard boundary
  is not loading casc tool as a dependency," not "no DB2 data ever";
  `DESIGN.md`'s existing Non-goals wording needs a real update once this
  lands, see that TODO's own Background section). Not started in `src/`
  yet -- five stages (WDC5 parser, placement geometry, the customization-
  choice chain, real pixel compositing, Blender-side picker tooling),
  each independently useful, see the TODO file for why staged rather than
  one large change.
- **Next step (also open, from an earlier session)**: a genuinely open, freshly-found gap from this session --
  `bloodelffemale_hd.m2`'s three real (`textureType == 0`) FileDataID-based
  slots (`3536810`/`4530998`/`5210137`) have no matching local file at all
  in the real `/media/luna/data/wow_export` texture directory, so they
  fall back to the same ambiguous same-basename pool as the hardcoded
  slots and land on the fallback tier's arbitrary pick -- not a resolution
  bug (`EYES_ON_FINDINGS.md`'s newest addendum has the full detail), but
  whether these FileDataIDs are just absent from this export or live under
  a different naming convention entirely is unconfirmed, not guessed at.
  `M2_GAPS_TODO.md` and, as of an earlier session, `RO_COMPLETENESS_TODO.md`
  are both fully implemented and deleted (see Last state above and
  further above) — every item either ever bundled (`M2Sequence`
  fields/`aliasNext`, `PFDC`, `EXP2`, `Texture.type`, Attachments/Events/
  Lights, animated tint/fade, `DETL`, `PCOL`, header-metadata decode,
  `WFV1`/`WFV2`/`DPIV`/`AFRA`, `blp/` DXT3 verification, `resolveSkin`
  diagnostics, plus regression-test follow-ups) is done, tested,
  documented. There is no active TODO file for M2/`blp/` in this repo
  anymore — the only remaining tracked, undone work in that scope lives in
  `TODO/TODO_correctness.md` (`M2Camera`, `.bone` slot *selection* — both
  low-priority by design, not oversight) and the two carryover threads
  below. Both `ANIM_TODO.md`'s
  `--anim` same-basename fallback and `PHYS_TODO.md`'s full `.phys`
  physics/collision support are implemented, tested, and documented. The M2→glTF
  multi-root-bone-forest representation gap is real, tested code now, not
  a survey — see `DESIGN.md`'s Key design decisions (the
  synthesized-non-joint-parent-node entry). Genuinely open threads, all carried over from earlier
  sessions and untouched by this one: (a) the ~7
  `textureComboIndex`-out-of-range cases `CORPUS_TODO.md` #5 couldn't
  re-verify (`failures_unique.txt` strips paths) — almost certainly the
  same "mismatched shared batch data" root cause as the now-16x-confirmed
  `materialIndex` case, but genuinely unconfirmed; (b) `tools/
  corpus_checks.py` keeping at least one real example path per distinct
  failure *message shape*, not just the top-N codes by count, so a case
  like (a) doesn't stay unverifiable next time. `resolveSkin`'s failure
  messages now do name the specific candidate path/FileDataID they tried
  (this session, see Last state) — that specific gap is closed. Optional
  scope expansion (WMO/M3, Blender-side tooling for the various `extras`
  this project already exports) is still nominally open from this file's
  own perspective, but see Last state's own note: Luna appears to have
  already started a WMO/ADT/world-geometry scaffolding pass in a
  concurrent session (`WORLD_COMPLETENESS.md` and several new
  `*_TODO.md` files landed in the work dir mid-session, untouched by this
  one) — worth checking her intent directly before assuming this is still
  unclaimed, rather than duplicating or stepping on it.
- **Hazards**: geoset tag joints (`Skeleton::geosetTags`, new this session,
  see Last state) are the one deliberate *exception* to the very next rule
  below — they **are** added to `skin.joints` (unlike Attachment/Event/
  Light), always appended strictly after every real bone so real joint
  indices 0..N-1 are never renumbered, and always parented under whatever
  node is/would be the skin's own closest common root (the single real
  root joint, or the synthesized multi-root parent) so `gltf_validator`'s
  "closest common root" check keeps passing. If `emitSkeletonAndSkin`
  (`gltf_skeleton.cpp`) is touched again: the synthesized multi-root
  parent node's own index formula must account for
  `skeleton->geosetTags.size()` (it sits *past* the tag-node range now,
  not right after the real joints) — this exact class of stale-index bug
  shipped once this session (caught by the existing `gltf_validator`-
  backed multi-root weapon test, not by inspection) before being fixed.
  Also: a tagged vertex's `WEIGHTS_0` and `WEIGHTS_1` must be rescaled
  *together* so their combined sum stays 1.0 (`gltf_mesh.cpp`'s
  `emitMeshNode`) — leaving `WEIGHTS_0` at its original full sum while
  adding a second full-summing `WEIGHTS_1` produces a real
  `gltf_validator` `ACCESSOR_WEIGHTS_NON_NORMALIZED` error, even though
  Blender's own Armature modifier renormalizes at runtime regardless (this
  project's test suite gates on zero `gltf_validator` errors, so this
  matters even though Blender itself wouldn't visibly misrender). Separate,
  pre-existing, NOT caused by this session's work, flagged but not fixed:
  the real `bloodelffemale_hd.m2` fixture has 6,879 vertices with a
  duplicate joint index within their own raw `JOINTS_0` slots (husk only
  ever copies `m2::Vertex::boneIndices` through, never modifies it) —
  confirmed via a standalone tinygltf-linked scan tool (scratchpad only)
  that a clean fixture (`wolf.m2`) has zero such duplicates, so this is a
  real data property of that one specific model, not a systemic bug;
  whoever next touches raw M2 bone-index handling should know it's there
  before assuming a `gltf_validator` `ACCESSOR_JOINTS_INDEX_DUPLICATE`
  report on that fixture is new.
- **Hazards (continued)**: the Attachment/Event/Light glTF nodes added this session
  (see Last state) follow the exact same rule as the multi-root
  synthesized parent node below — **never add them to `skin.joints`**,
  they're plain translation-only child nodes of a real joint, not bones
  themselves (verified via headless Blender's `bone_count` probe staying
  exactly `header.bones.count` with these nodes present). `M2Light`'s
  `bone == -1` ("not attached to any bone," real per wowdev.wiki) is
  currently treated as an out-of-range-joint throw, same as any other bad
  index — no real fixture has exercised this case yet, so if one ever
  does and the throw is wrong, that's new information, not a regression.
  Per-clip `sequence_metadata` extras (`gltf::Animation::SequenceMetadata`,
  `M2Sequence`'s movespeed/frequency/replay/blend-time/bounds fields) are
  carried through unchanged even for an alias clip built from its
  terminal sequence's keyframe data — the alias's *own* metadata fields
  are what's attached, not the terminal sequence's, since those two are
  independent per-`M2Sequence` facts even when the keyframe data itself is
  shared. If `buildAnimations`'s alias-resolution branch
  (`resolveAliasChain`) is touched again: it must keep checking
  `flags & 0x20` ("stored inline") *before* treating a sequence as a "pure"
  alias needing chain resolution — a real fixture
  (`bloodelffemale_hd.skel`) has 31 of 38 alias-flagged sequences also
  carrying `0x20`, meaning they already have real inline data of their own
  that must not be overwritten by a different sequence's keyframes (see
  Last state for how this was caught before shipping). For the multi-root rework (now implemented), never insert a
  synthetic node into `Skeleton::joints` itself (see `src/gltf.hpp`'s
  `Skeleton` doc comment and `DESIGN.md`'s Key design decisions for why:
  every vertex/emitter-anchor/correction/animation joint
  index is a raw, unremapped M2 bone-array index, and a reordered
  `Skeleton::joints` would silently misattribute all of them with no
  crash and no validator error) — `writeGlbMulti`'s actual implementation
  confirmed to respect this (the change lives entirely in glTF-side node/
  scene/skin construction, `Skeleton::joints` itself untouched), covered
  by a real test (`test_gltf.cpp`'s mixed mesh-nodes-plus-multi-root case
  asserting vertex joint indices stay raw/unshifted), not just asserted
  safe by inspection. This session's own changes are each covered by real
  tests (see Last state for the specific test names). One thing worth
  knowing if
  `cmd_export.cpp`'s collision-mesh block is touched again: it always
  appends its `NamedMesh` *after* every render/LOD entry — anything
  indexing `namedMeshes` by position (like the "N LOD tier(s)" summary
  print, fixed in an earlier session via `renderMeshCount`) needs to
  account for that trailing entry, not assume `namedMeshes.size()` equals
  the render-mesh count. Carried over from earlier sessions:
  `completions/husk.bash`/`.zsh` are generated, checked-in
  artifacts (`husk --print-completion=<bash|zsh>`) — if `addExportOptions`'s
  flag table changes, regenerate both rather than hand-editing; **the
  completion generator's per-flag value-taxonomy tables in `src/main.cpp`
  (`bashValueCompletion`/`zshValueAction`/`zshFlagLabel`) are hand-maintained,
  separate from `addExportOptions`, and don't pick up a new flag's
  `none`/directory semantics automatically** — a new flag falls through to
  plain-filename completion until it's added to those tables explicitly
  (found the hard way in an earlier session; verify by actually sourcing
  the regenerated script and driving `_husk_completions`/`_husk`, not just
  diffing that a new flag
  name appears). `HUSK_TEST_DATA_DIR` (`CMakeLists.txt`) is baked absolute
  at configure time, so the default `test_data/`-fallback fixtures are
  immune to the old `ctest`-runs-from-`build/` relative-path trap — but if
  you override any `HUSK_TEST_*` env var by hand for `ctest` specifically
  (not `./build/husk-tests` directly), it still needs to be absolute, or
  that one test fails on a bad relative path, not a real regression.
  If `buildAnimations`'s external-sequence branch (`cmd_export.cpp`) is
  touched again: the FileDataID-vs-basename fallback logic must check
  `f.is_open()`, never `!f` — a default-constructed `std::ifstream` that
  never had `.open()` called on it (the `animFileIds == nullopt` case)
  reports `goodbit`, not `failbit`, so `!f` silently evaluates false and
  the code falls through to reading an unopened stream instead of trying
  the next fallback (a real bug this session's own implementation had
  before an existing test caught it — see Last state). `.phys` chunk tags
  are byte-reversed on disk (WMO/ADT convention) — the opposite of every
  other sidecar husk reads (`.bone`/`.skel`/M2 itself) — `src/phys.cpp`'s
  chunk-tag constants are already the reversed literals; don't pass a
  forward-spelled tag to `findChunk` when touching that file. The
  `mace_1h_warfrontsforsaken_d_0100.skin` fixture (`test_data/item/
  objectcomponents/weapon/`) was added this session specifically to pair
  with the already-committed `mace_1h_warfrontsforsaken_d_01.m2`/`.phys` —
  it's the only committed `.phys` weapon fixture with a matching `.skin`,
  used by `tests/test_integration.cpp`/`test_conformance.cpp`'s real
  `--phys` checks (`HUSK_TEST_WEAPON_PHYS`/`_SKIN`, `tests/
  test_data_paths.hpp`).

---

**2026-08-14, overnight `/loop` session (ongoing)**: Autonomous session,
chained several independently-committed `[UNVERIFIED/STAGING]` increments
(see git log for the exact commits/messages, not duplicated here). (1)
`TODO/MULTI_TEXTURE_LAYER_TODO.md` substantially advanced: found and
corrected a wrong premise (the file's plan was built around a WotLK-only
shader-selection heuristic that wowdev.wiki's own `M2/.skin.wiki` page
says "stops applying from Cata and on" -- husk's own Legion+ scope is past
that line) and the real mechanism that applies instead (decompiled Cata+
`M2GetPixelShaderID`/`M2GetVertexShaderID` + a real on-disk `shader_id`
field husk had never parsed at all). Implemented: `M2Batch::shader_id`
parsing (`skin.hpp`/`.cpp`), a real resolver (`src/m2_shader_names.hpp`/
`.cpp`, transcribed from the wiki's decompiled tables) wired into export
as `pixel_shader`/`vertex_shader` material extras, and a full 287k-file
corpus scan (`tools/corpus_scan_tasks/shader_id_task.py`, new, plugged
into the existing `corpus_scan_framework.py` after an earlier naive serial
scratch-script attempt was caught and killed mid-run for having no
parallelism/checkpointing). Validated against 3 real corpus files by
hand, including one striking unprompted match: a real guild-pennant model
resolves to the literal `"Guild"` pixel shader. (2) `TODO/DPIV_TODO.md`
(the long-open `DPIV` mystery-chunk investigation) advanced significantly:
a real geometry cross-reference, a corrected hypothesis (an early
"`field1` is ground-relative elevation" read was wrong -- the real pattern
is "`field1`/`field0` are the model's own bbox-center coordinates," caught
by checking against real `bounding_box` data rather than trusting the
first pattern that fit a 4-file sample), a full corpus-scale confirmation
that `field2` sits consistently near-or-below the model's own base (a real
ground-contact/shadow-anchor shape, not just a lead anymore), and a
multi-record structural finding (some records are exact `(0,0,0)`
placeholders, not real second points -- filtering them out before testing
the "points form a footprint polygon" hypothesis, which then came back
negative: bounded within the model but not tightly clustered). (3) Closed
`TODO/TEXTURE_TOOLING_TODO.md` outright -- its entire ask (a native `husk
blp-export` subcommand) turned out to already be fully implemented and
tested in a prior real commit, just never documented in `README.md` or
closed out; fixed both gaps. (4) Fixed the real, live-caught `-o` path
usability gap `TODO/TODO_correctness.md` had flagged (a prior session's
own finding, not implemented at the time): `-o <existing-dir>` now infers
`<dir>/<model-basename>.glb` instead of failing with "Is a directory," and
missing parent directories in an explicit `-o` path are now created
(`mkdir -p`-style) instead of failing with "No such file or directory" --
both fixes land in `cmd_export.cpp`, before the real parse/export
pipeline runs (fail-fast, per the original finding's own request), with 3
new real CLI tests reproducing the exact scenarios from that session's own
terminal trace. TODO doc's own now-resolved section removed per this
project's "punch list, not historical record" convention, and a dangling
"item 3" cross-reference (stale from an earlier renumbering pass, found
while removing the section) corrected to "item 2" in the same edit. Full
suite green throughout, 628/628 (1 pre-existing unrelated skip) as of the
`-o` fix. One real process lesson mid-session, corrected directly by
Luna: never `rm` during unattended/`/loop` work (even a fully-recoverable,
git-tracked project file) -- it always triggers a blocking permission
prompt that stalls the entire run until a human happens to be awake to
answer it; `mv` to a `./trash/` directory instead, same rule she'd already
stated for temp/scratch files, just under-scoped by this session at
first. Session still in progress as of this entry -- see git log for
anything past this point.

---

**2026-08-13, continued**: Same session, `TODO/ANIMATED_TEXTURE_EFFECTS_TODO.md`'s
§1 ("a framework for exporting short animated clips, not just one static
image") closed too, prompted directly off the back of the §3 work below --
Luna had independently been building real interactive-viewer support
(`tools/live_gallery/server.py`, a new three.js-based in-browser GLB
viewer, in progress/uncommitted, not this session's own work) that needs a
real `.glb` sitting next to each corpus-render thumbnail to load, which
`tools/corpus_scan_tasks/render_glb.py` never saved (its own driver,
`render_sample_driver.py`, actively deletes its scratch `.glb` after
rendering).

**`render_glb.py`'s real design change**: previously always rendered
exactly one still frame, regardless of whether the model had any real
animation. Now: `render_duration_seconds` (new) determines the model's own
real, native animation duration as the longer of two independently-
measured sources -- (a) its currently-active skeletal action's own frame
range (Blender's glTF importer already leaves `animation_data.action` set
to `animations[0]` post-import for direct `scene.frame_current` scrubbing,
no NLA-track juggling needed -- confirmed empirically against the real
`wolf.m2` fixture, 38 imported actions, not assumed to work this way), and
(b) husk's own `texture_transform_animation`/`tint_animation`/
`fade_animation` extras, via the exact same `apply_texture_transform_animation`/
`apply_tint_fade_animation` (§3's own new functions) this script now also
calls -- both already grow `scene.frame_end` to fit their own real curve
duration, so resetting `frame_start`/`frame_end` to a minimal `(1, 1)`
range first (mirroring the exact trick this session's own earlier
`example_exports` render script used) means whatever they leave it at *is*
their own real duration, not Blender's arbitrary default 250.

A model with real native duration below `MIN_ANIMATED_DURATION_SECONDS`
(0.05s -- genuinely static, or close enough it wouldn't visibly differ)
still takes the old single-still-frame path, unchanged -- keeps the
(likely still-majority) static portion of the corpus at its existing
render cost. Everything else renders across a **fixed
`RENDER_WINDOW_SECONDS` (5.0) window for every animated file**, regardless
of the model's own native duration -- a native clip shorter than 5s loops
to fill the window rather than being skipped, prompted directly ("handle
animations where length is less than 5 seconds -- clamp it to 5 seconds,
not skip"; many real WoW animations are genuinely under a second, so
skipping them as "too short to bother with" would silently drop most of
the corpus's real animated content from this preview pipeline entirely); a
native clip longer than 5s is simply shown from its own start through the
window, not skipped either.

**A first version of this got the frame-count/rendering-mechanism design
wrong, caught by direct pushback, not self-caught**: it rendered a fixed
`N_PREVIEW_FRAMES = 12` *total* samples across the whole 5s window (2.4fps
-- a visible strobe, not real motion) via a manual Python loop calling
`bpy.ops.render.render(write_still=True)` once per frame, framed in this
session's own earlier notes as "12x render passes" -- a real
mischaracterization of the actual cost, called out directly: Blender's own
per-frame EEVEE cost is cheap once the model is already loaded (the real
fixed cost per file is Blender startup + import, not per-frame rendering),
and Blender has a native animation renderer
(`bpy.ops.render.render(animation=True)`) that should be used instead of
hand-rolling a per-frame Python loop. Rebuilt on a real `RENDER_FPS = 24`
playback rate (120 real frames over the 5s window, not 12 coarse samples),
rendered via one `animation=True` call -- confirmed empirically that its
own internal frame-stepping fires `bpy.app.handlers.frame_change_pre`
exactly like manual `scene.frame_set()` calls already did (101 handler
calls recorded for a 100-frame test render, real curve values changing
every one of them), so husk's own `apply_texture_transform_animation`/
`apply_tint_fade_animation` handlers needed zero special-casing to keep
working under it. Measured real wall time end to end (Blender startup +
model import + 120-frame render + WebP stitch): ~16s for both real
fixtures tested (the 50-vertex spell-effect fixture and `wolf.m2`'s
557-vertex/66-bone skeletal case) -- dominated by fixed per-file overhead,
not the animation render itself, exactly as predicted.

A real skeletal action shorter than the render window needed its own real
fix along the way: Blender's pose evaluation holds the last frame past an
action's own range by default (Constant extrapolation), it doesn't loop --
new `loop_action_natively` adds a real Blender `Cycles` F-curve modifier
(the native, engine-evaluated way to loop a clip) to every F-curve in the
active action when its own duration is shorter than the render window, no
Python-side per-frame math needed for the skeletal case at all. Hit a real
Blender-5.x API surprise while writing this: `action.fcurves` doesn't
exist anymore on this Blender version's layered Action data model (4.4+'s
"Animation 2.0") -- F-curves now live under
`action.layers[].strips[].channelbags[].fcurves`, confirmed by directly
inspecting a real imported action's structure rather than trusting older
API muscle memory. Verified the loop actually works, not just that it
didn't crash: a frame near one native-loop boundary of `wolf.m2`'s 1.00s
action is measurably closer to frame 0 (mean pixel diff 1.48) than an
arbitrary mid-cycle frame (mean diff 5.67) -- real pose repetition, not
coincidence.

Frames are rendered to individual PNGs in a scratch subdirectory (cleaned
up after), then stitched into the final animated WebP via Pillow --
confirmed available inside Blender's own bundled Python via this project's
nix flake (`PYTHONPATH` injection, not bundled with Blender itself --
verified by checking `PIL.__file__`'s actual resolved path before relying
on it, not assumed).

**A real bug found and fixed while verifying this against the real
`unk_exp11_7037014.m2` fixture from §3's own work, not by inspection**: the
first attempt rendered every sampled frame as an identical flat color
despite `_update_texture_transform_animations`'s own Mapping-node
`Location` value genuinely changing every single frame (independently
confirmed via direct print statements) -- a real, initially-alarming
"the whole animation pipeline doesn't actually affect the render" scare.
Root-caused via a sequence of isolating tests (item-vs-tuple property
assignment, `view_layer.update()`, `use_persistent_data`, a from-scratch
minimal Emission-color propagation test that *did* show a difference) down
to the real cause: a texture whose pattern repeats at high spatial
frequency relative to its UV footprint mip-averages to the same flat color
regardless of scroll offset under GPU sampling -- mathematically expected
(averaging a periodic signal over whole periods cancels the phase
entirely, provably not data-dependent), not a propagation bug at all. Fixed
for the render path specifically (not the shipped `husk_blender_geoset_mask.py`,
which needed no change) by forcing `Closest` interpolation on every Image
Texture node before rendering and using a lower-frequency demo texture for
the `example_exports` regeneration -- real WoW textures are essentially
never this pathological, so this is a demo/render-script-only
consideration, documented in `example_exports/README.md` for whoever hits
the same confusing symptom next.

**Also closed in the same pass**: every rendered output now gets its real
source `.glb` copied to a sibling path (`out_path`'s own basename, `.glb`
in place of `.webp`) -- the exact convention Luna's own in-progress
three.js viewer already expects (`MODEL_REL = REL.slice(0,
REL.lastIndexOf('.')) + '.glb'`), confirmed by reading her own uncommitted
diff rather than guessing the convention independently. `render_sample_driver.py`
itself needed no change -- it already writes its own scratch `.glb` to a
separate `SCRATCH_DIR` and deletes *that* copy after rendering;
`render_glb.py`'s new copy lands in `render_dir` (next to the `.webp`) and
is untouched by that cleanup. Real cost accepted, not hidden: a full
130k+-file corpus run now renders 120 real frames (one native
`animation=True` call, not 120 separate ones) for every animated file
instead of a single still, plus a full `.glb` copy's worth of extra disk
per rendered file -- both accepted per direct instruction, flagged in the
TODO file rather than silently absorbed, and cheaper in practice than the
raw frame count suggests (see the measured ~16s/file wall time above).

**Two real, concrete usability findings filed as new TODO items, not fixed
silently**: (1) `TODO/TODO_correctness.md` gained a new top item from a
full, real terminal trace Luna pasted directly -- `husk export -o <existing
directory>` and `-o <path with missing parent directories>` both fail only
*after* running the entire multi-second export pipeline (real parse work,
a full wall of notes/warnings), with zero filename/directory inference at
all; four attempts needed before one worked. Framed explicitly around a
stated design principle, quoted directly: *"sinne päin ja silmät kiinni"*
("aim in the right direction and go, eyes closed") -- husk's default
behavior should guess a reasonable output path from minimal input (infer
a filename from the model's own basename when `-o` is an existing
directory; create missing parent directories the way any real destination-
path-taking tool already does), while every flag stays exactly as precise
as it is today for anyone who wants exact control -- guessing well should
be the zero-effort default, not something that requires already knowing
the tool's exact expectations. Not fixed this session -- this is the
finding, staged as two named, separable sub-fixes in the TODO item itself.
(2) `TODO/CLEANUP_TODO.md` gained a second new item (§2, "a corpus-wide
'dangling internal reference' scan"), prompted directly off the back of
this session's own §3 fixture hunt (a real, well-formed animated
`M2TextureTransform` record that no `.skin` batch's own
`textureTransformComboIndex` ever actually resolves to, caught before
committing a fixture built around it) -- proposed as a deliberate
counterweight to every *presence*-only completeness metric this project
already tracks: measure the corpus-wide rate of "claims a target exists,
but it doesn't resolve" across every internal cross-reference husk already
knows how to walk, on the theory that a **low** rate is expected noise
(stale/unused Blizzard data, already-precedented in this project's own
`CORPUS_TODO.md` history) while a **high or systematic** rate is instead a
real signal of a casc-tool extraction blind spot worth raising upstream,
not husk's own problem to solve by reading the format more carefully.
Design-stage only, not implemented.

`example_exports/` regenerated around the same `unk_exp11_7037014.m2`
fixture, now showing a genuinely-animated (not merely still) preview via
the updated `render_glb.py`, confirmed by direct pixel-diff (max 128,
mean ~11 across two frames) after the mip-blur fix above -- not just "it
ran without erroring."

Session's own git commit (`48b2561`, the §3 work) already landed before
this continuation started; this continuation's own changes
(`render_glb.py`, both TODO files) were left for Luna's own review/commit
per her own explicit request this turn, not committed automatically.

---

**2026-08-13**: `TODO/ANIMATED_TEXTURE_EFFECTS_TODO.md`'s §3 ("making these
curves actually animate in Blender post-import") implemented end to end,
prompted directly with a request for a real debug/validate pipeline built
around one simple, unambiguous test case -- "same case as the lightforged
lamp," the fixture that made billboard-rotation correctness easy to verify
by eye. Two parts: measuring the real scope first, then closing the
playback gap.

**Scope measurement**: `tools/corpus_scan_tasks/animated_texture_effects_task.py`
(new, built on `corpus_scan_framework.py`'s adaptive-concurrency driver,
same one `render_glb.py`'s own corpus renders use) scanned the full local
130,576-file corpus in 16.1s for a genuinely-animated (not merely constant
-- same distinction husk's own `resolveAnimatedColorCurve`/
`resolveAnimatedFixed16Curve` already make) `M2TextureTransform`/`M2Color`
tint/alpha/`M2TextureWeight` track, independent of husk's own code (reads
the header arrays and raw `M2Track` bytes directly). Offsets verified
against the real `bloodknightcharger.m2` fixture already in this repo
(correctly found its one genuinely-animated transform record while
correctly treating its two constant ones as non-animated). Result: 36,086 /
130,576 files (27.6%) carry at least one such curve -- 28,454 with an
animated `M2TextureTransform`, 10,906 animated alpha, 7,845 animated
weight, 2,542 animated tint. This is presence, not confirmed batch
reachability (no cross-reference exists yet for tint/fade/weight the way
`find_texture_transform_files.py` already has for the constant-transform
case) -- flagged honestly in the TODO file, not overclaimed -- but even
conservatively this confirms a real, corpus-wide visual gap, not a handful
of edge cases.

**A real, previously-unclosed export gap found while building the fixture
for the playback work**: `gltf_mesh.hpp`'s own old doc comment for
`Material::TextureTransform` said the genuinely-animated case's raw values
are "just each field's un-animated default and not real data" -- true.
Unlike `tint_animation`/`fade_animation` (which already exported the real
curve for their own animated case), the animated `M2TextureTransform` case
exported *nothing* beyond the same default the constant case's absence
already implied. Closed: `m2::TextureTransform` (`src/m2_animation.hpp`)
gained `translationTrackOffset`/`rotationTrackOffset`/`scalingTrackOffset`
(mirroring `Color::colorTrackOffset`/`alphaTrackOffset`), set in
`parseTextureTransforms` (`src/m2_animation.cpp`). Resolving rotation's own
curve needed a genuinely new function pair, not reuse of the existing
`resolveQuatTrackSequence`/`resolveQuatGlobalSequenceTrack`: those decode
`M2CompQuat` (bones' packed int16 format), but `M2TextureTransform::rotation`
is a *raw* `C4Quaternion` (4 plain floats) -- wowdev.wiki says so explicitly,
and `m2::TextureTransform::rotation`'s own doc comment already flagged the
distinction, it just hadn't needed a real resolver until now. New
`resolveRawQuatTrackSequence`/`resolveRawQuatGlobalSequenceTrack`
(`src/m2_animation.{hpp,cpp}`, plus a `readRawQuat` helper next to the
existing `readCompQuat`) mirror the Vec3 resolver pair exactly, just with a
16-byte stride and no decompression step. `export_materials.cpp` gained
`resolveAnimatedRawQuatCurve` (mirrors `resolveAnimatedColorCurve`, wired
the same way tint/fade curves already are) and now resolves
translation/rotation/scaling into `gltf::Material::
textureTransformTranslationAnimation`/`RotationAnimation`/`ScalingAnimation`
whenever the corresponding `*Animated` flag is set. Translation/scaling
reuse `gltf::Material::AnimatedColorCurve`'s shape (seconds → Vec3) rather
than duplicating a struct that's structurally identical, just not
semantically a color -- documented inline as a deliberate reuse, not an
oversight. `gltf_mesh.cpp`'s `emitMaterial` serializes all three as a new
`texture_transform_animation` material extras key (sub-keys `translation`/
`rotation`/`scaling`, each an array of curve objects, same shape
`tint_animation` already used) -- factored a shared `vec3CurvesToValue`
lambda out of the pre-existing `tint_animation` serialization code in the
same pass, since translation/scaling now need the exact same logic.

**Test fixture, found the hard way**: the corpus scan's own CSV gave
plenty of *candidates* with an animated transform, but the first one tried
(`cfx_druid_efflorescence_periodicvar2.m2`, a Druid healing-circle ground
effect, chosen for its clean semantic match to "spinning ground sigil")
turned out to be a real trap -- its one real batch's
`textureTransformComboIndex` resolves (via `textureTransformCombos`) to the
sentinel `0xFFFF`, not the model's own single animated transform record, so
the curve is present in the file but never actually reachable by any real
render. Caught by writing a small one-off cross-check script (mirroring
`find_texture_transform_files.py`'s own skin-batch verification, just for
the animated case instead of the constant one) *before* committing a
fixture, not after a test mysteriously failed -- though in this instance it
was in fact caught by a failing integration test first (`husk export`
printed no `texture_transform` note at all for the file, a clear "the
combo table doesn't actually point here" signal), which is what prompted
writing the verification script in the first place. Re-scanned with the
verification script and landed on `unk_exp11_7037014.m2`
(`test_data/models/spells/unk_exp11_7037014/`, real FileDataID 7037014, no
in-file model name) -- deliberately minimal (18 vertices, 1 bone, 1
material, 1 sequence, 0 particle/ribbon emitters), one real,
batch-referenced translation curve: a clean one-way X-axis UV scroll,
(0,0,0) at 0ms to (1,0,0) at 4167ms, no ping-pong, no rotation/scaling
noise. New real integration test
(`tests/test_integration_texture_transform.cpp`) asserts the exact
keyframe values export correctly, plus 3 new unit tests for the raw-quat
resolvers (`tests/test_m2_animation_tracks.cpp`, including the same
"global-sequence track resolves to empty by sequence index, not
misattributed" guarantee the compressed-quat resolver already has).

**Blender side** (`tools/husk_blender_geoset_mask.py`, gaining a 4th
independent job): direct per-frame computation plus a `frame_change_pre`
handler, the same design choice this project's own billboard-alignment
work already settled on for the same underlying reason -- no native
Blender node animates "current scene time, looped against a duration only
known at runtime" without a driver/handler regardless of curve shape.
`apply_texture_transform_animation` builds one shared `Mapping` node per
concerned material (wired ahead of any Image Texture node whose `Vector`
input isn't already linked to something else -- the shape Blender's stock
glTF importer leaves an imported material in) and recomputes its Location/
Rotation-Z/Scale every frame from the real curve data, via shared
`_eval_scalar_curve`/`_eval_vec3_curve`/`_eval_quat_curve_z_angle`
interpolation helpers (real lerp/slerp between keyframes, not a step
function) and `_curve_duration` (the curve's own real last-keyframe
timestamp). `_eval_quat_curve_z_angle` collapses a slerped rotation to a
single Z-axis angle the same way `gltf_mesh.cpp`'s `textureTransformToKhr`
already does for the constant case (`theta = 2*atan2(qz, qw)`) -- Blender's
Mapping node has only one scalar UV rotation, the same real limitation the
constant case's own planarity check already documents. `_to_pyobj` handles
the real wrinkle found while building this: unlike a scalar custom
property (`texture_type`, already read directly elsewhere in this file), a
*nested* extras value like `texture_transform_animation` comes back from
Blender's importer as `IDPropertyGroup`/`IDPropertyArray` wrapper types,
not plain dict/list -- recursively converted once via `to_dict()`/
iteration so every caller just sees plain Python.

Verified headlessly against the real fixture, not just asserted correct by
reading the code: exported via a synthetic 1×1 PNG texture (same fixture-
construction pattern `tests/test_integration_texture_transform.cpp` already
uses), then a standalone script drove
`husk_blender_geoset_mask.apply_texture_transform_animation` directly and
read the resulting Mapping node's `Location.x` back at three frames --
0.0 at frame 1 (t=0, the curve's own start), 0.5 at frame 51 (t≈2.08s, the
curve's own midpoint -- exactly the halfway lerp value), and a correctly-
wrapped ~0.49 at frame 250 (well past one real loop, confirming the modulo
wrap works, not just the first cycle). A second script confirmed
`_extend_frame_range_for_duration` in isolation: a duration of 4.167s at
24fps computes to exactly 101 (`frame_start` 1 + 100 frames), and it never
shrinks an already-longer `scene.frame_end`. Running the *whole* `main()`
pipeline (not just the isolated function) against the real fixture, and
separately against a real file with actual `tint_animation`/
`fade_animation` data (`stasistotem.m2`, a totem spell-effect model, 9
real animated batches) confirmed no crashes either way -- the tint/fade run
correctly skipped 6 of 7 materials with a clear "no Principled BSDF node to
drive" message (a real, already-documented Blender-importer quirk: unlit
materials, common on additive spell-effect blend modes, get a completely
different node shape with no Principled BSDF at all, same one
`render_glb.py`'s own `fix_additive_materials` already works around for
renders) rather than crashing on the missing node. `apply_tint_fade_animation`
is real, structurally-consistent code sharing the same verified
interpolation/looping machinery -- but has genuinely **not** been checked
against real ground-truth values the way the texture-transform case has
(no minimal, skin-batch-verified tint/fade fixture exists yet), flagged as
such directly in its own doc comment, not overclaimed.

**Clip-length question, answered concretely** (the session's own explicit
ask: "converge on what would be a good animation clip length, the standard
24 frames ≈ 1 second, or something else?"): not a fixed convention -- each
curve already carries its own real duration (its last keyframe's
timestamp), which is the thing that actually matters for correctness.
`_extend_frame_range_for_duration` grows (never shrinks) `scene.frame_end`
to fit the longest registered curve at the scene's *existing* frame rate --
24fps (Blender's own default) is a perfectly fine baseline rate, but the
frame *count* has to come from the real model, not a fixed clip length: the
real fixture's 4.167s loop needs 100 frames at 24fps, computed, not
hardcoded; a different model's real 1.2s pulse would need 29 frames at the
same rate.

**Real robustness prerequisite, closed in the same pass** (flagged in an
earlier draft of the TODO file itself as a hard blocker before adding a
4th/5th stage to this script): `tools/husk_blender_geoset_mask.py`'s
`main()` used to run every stage (geoset switch, billboard alignment,
texture-layout overlay) in one un-isolated body -- an exception in an early
stage silently killed every later one, including completely unrelated
ones. Now every stage runs through a shared `_run_stage(model_name,
stage_name, fn)` wrapper: a failure prints a loud, specific message (model
name, stage name, real exception type and text) and the rest still run.

**Also this session, a separate, unrelated note filed rather than acted
on**: prompted directly mid-session to flag that `src/export_materials.cpp`
needs splitting -- at 1,281+ lines (before this session's own additions) it
had already passed `cmd_export.cpp` (1,239 lines) as the largest file in
`src/`. Filed as `TODO/CLEANUP_TODO.md` (new) with three candidate seams
noted from a first-glance read (texture resolution, per-batch material
building, animated-curve resolvers) -- explicitly not investigated further
this session, no split attempted.

Full C++ test suite green, 612/612 (up from 601 at session start -- 11 new
tests: 1 real integration test for the animated-transform export path, 6
unit tests for the two new raw-quat resolvers -- 3 success-path, 3
edge-case/global-sequence, matching the existing coverage shape for every
other track-resolver pair in this file, plus the pre-existing suite
re-verified unaffected). `TODO/ANIMATED_TEXTURE_EFFECTS_TODO.md`'s §1/§2
(a real short-clip corpus-render pipeline and live-gallery playback support)
remain open and untouched -- this session closed the scope-measurement
question and §3 (the playback mechanism itself), not the render-pipeline
infrastructure those two items describe.

---

**2026-08-09, continued again**: Real regression caught live, mid-run, by
watching actual system usage rather than trusting the job was fine because
it hadn't crashed: after the entry below shipped `--listfile` wired into
the full corpus render, both CPU and GPU usage visibly *dropped*. Root
cause, confirmed by direct timing, not assumed: `src/listfile.cpp`'s
`loadListfile` re-parses the entire 148MB/2.2M-line
`community-listfile.csv` from scratch on every single `husk export`
subprocess call (~1.1s of pure single-threaded parsing each time, since
husk has no persistent process to cache it across the driver's 130,576
calls) -- workers were spending a large, growing fraction of their wall
time re-parsing an unchanging file instead of doing real export/render
work, starving both CPU parallelism and the GPU pipeline behind it. First
fix attempt (filtering the listfile down to only the ~82k FileDataIDs a
specific prior corpus scan had already flagged as relevant) was floated,
built, and then deliberately reverted after a direct, well-founded
objection: pruning to a scan's own snapshot risks silently losing coverage
for any FileDataID that scan didn't happen to flag (including the
listfile's own `unk_exp*` placeholder-named entries, real data that just
isn't identified yet). Fixed at the actual root instead: rewrote
`loadListfile` for raw parse speed against the *full, unpruned* file --
one `fread` instead of `std::getline` per line, manual digit scanning
instead of exception-based `std::stoul`, `unordered_map::reserve()` up
front instead of ~20+ incremental rehashes for 2.2M inserts. Confirmed by
direct timing: the full file's per-call overhead drops to ~50-100ms,
indistinguishable from run-to-run noise against not passing `--listfile`
at all (4.4-4.5s either way on a real fixture, vs 5.65s with the original
naive parser) -- full listfile coverage kept, no pruning, no measurable
cost. Same 4 `tests/test_listfile.cpp` cases still pass unchanged (they
test behavior, not implementation). Also considered and explicitly
deferred, not implemented: keeping Blender itself resident across files
(persistent worker loop fed by a queue, instead of one `blender
--background` process per file) -- measured bare Blender startup at ~0.51s
against ~1.81s total for a real character render (~25-30% of Blender-side
wall time, meaningful at 130k-file scale, roughly 1.5 wall-clock hours
across a 12-way-parallel full run) but a real architecture change (loses
today's clean one-process-per-file crash isolation, needs its own
memory-growth/hung-job handling) -- Luna's own call to launch the already-
fixed pipeline now and revisit this as a separate, carefully-tested follow
-up rather than block tonight's run on it.

**2026-08-09, continued**: Two more real, corpus-render-driven gaps closed
in the same session, plus real render-pipeline throughput/UX work, all
found via actually looking at rendered output rather than just parsing
correctness:

1. **`_sdr` stand-in models render untextured -- root-caused and fixed.**
   `scanFuzzyTexturePool` (`export_materials.cpp`) required a same-basename
   candidate's filename to start with the model's *exact* own basename --
   a "_sdr" model (e.g. `darkirondwarfmale_sdr.m2`, a drastically lower-poly
   self-contained-animation stand-in that shares its real texture files
   with its non-"_sdr" counterpart, confirmed by comparing real vertex/
   texture/sequence counts between the two) can only ever resolve against
   files named `darkirondwarfmale_<suffix>.blp` -- real files sitting right
   there, but the "_sdr" prefix requirement rejected every one of them.
   Fixed with a narrow, confirmed-by-bytes fallback: when the exact-
   basename scan finds nothing and the basename ends in `_sdr`, retry with
   that suffix stripped. Verified against the real fixture (0 embedded
   images before, ~100+ after). 2 new tests.
2. **WoW's additive/multiply blend modes (3-7) have no core-glTF
   equivalent and were collapsing to plain alpha-`BLEND`, which is a real,
   visibly wrong answer** -- a mostly-black additive-designed texture (a
   glow/particle/constellation effect) should contribute nothing where
   it's black; naive alpha-blend instead shows a solid dark panel.
   Confirmed against a real corpus file flagged directly:
   `creature/celestialfoxwyvern/celestialfoxwyvern.m2` rendered as a solid
   dark diamond instead of glowing constellation lines. Closed two ways:
   (a) `gltf::Material` gained a raw `blendMode` field, exported as a
   `blend_mode` material extras value only when > 2 (0-2 already round-trip
   losslessly through `alphaMode`) -- 2 new tests, one proving the extras
   value appears for mode 4, one proving it's absent for mode 2. (b)
   `tools/corpus_scan_tasks/render_glb.py` (the actual corpus-render
   pipeline this whole investigation runs through, not just a future
   Blender-import convenience) now rebuilds a real additive shader
   (Transparent BSDF + Emission combined via Add Shader,
   `surface_render_method = 'BLENDED'`) for any material carrying that
   extras value -- found and fixed a real bug in the first version of this
   before it shipped: WoW's own `unlit` flag (M2Material 0x01) commonly
   co-occurs with additive blend modes, and Blender's glTF importer builds
   a completely different node shape for `KHR_materials_unlit` materials
   (`Emission`+`Transparent`+`Mix Shader`, no `Principled BSDF` at all) --
   the first version only searched for `BSDF_PRINCIPLED` and silently
   fixed zero materials on the real fixture; fixed by reusing whichever
   shape is actually present (the unlit path's own `Emission` node, already
   correctly wired to the right texture, needs no rebuilding at all).
   Verified against the real fixture: the solid dark panel is confirmed
   gone post-fix (2 materials rebuilt); the glow's own visual intensity
   wasn't independently tuned/confirmed beyond that, flagged as a possible
   follow-up polish item, not a correctness gap. Modes 5/6 (Mod/Mod2x,
   multiply) are a real, separate, un-attempted gap -- no demonstrated
   real-corpus case drove it this session.

Also, prompted directly by real observations while this was being
investigated:

- **`tools/live_gallery/server.py`** gained a `world/expansionNN` era
  filter (real folder convention, confirmed against actual zone/doodad
  names per folder -- `expansion01` has `hellfirepeninsula`/`netherstorm`
  (TBC), `expansion05` has `ironhorde` (WoD), etc.; `expansion11`'s real
  expansion is unreleased/unannounced at time of writing, labeled
  conservatively). Deliberately scoped down after checking real data
  first: an initial plan to filter by M2 *file-format version* instead
  turned out to carry zero signal on a modern retail extraction --
  130,242 of 130,576 real files are already version 272 or 274 (both
  Legion+ chunked), because the client re-saves every M2 in the current
  format regardless of the content's original expansion, confirmed via a
  new (deliberately not wired into the gallery, given the null result)
  `tools/corpus_scan_tasks/expansion_task.py`. The path-based `world/`
  convention is real signal where the version-based one wasn't, but only
  covers world/doodad content, not creature/character/item/spells.
- The gallery's live-update path was fully rebuilding the grid (wipe +
  re-fetch + re-decode every visible image) on every single new-file
  notification, which stutters badly during an active multi-hour render
  job. Fixed with a real diff instead of a reset: `prependNew()` fetches
  the current first page and prepends only images not already known
  (items are sorted newest-first, so this stops at the first already-seen
  one), leaving the rest of the grid untouched; batched via
  `DocumentFragment` instead of one `appendChild` per item. Added a
  staggered per-item entrance animation (capped stagger delay) plus a
  brief cyan flash-then-settle accent purely for polish.
- **Render throughput**: this machine's own dual Radeon VII
  (`CLAUDE.md`'s Machines table) were being used unevenly by the 12-way
  worker pool -- confirmed via `/sys/class/drm/card*/device/
  gpu_busy_percent` polling during a real render, not assumed from CPU%
  alone. `render_sample_driver.py` now sets `DRI_PRIME` per worker process
  (stable for that worker's lifetime, based on `os.getpid() % 2`, no
  shared state needed) to alternate which physical GPU each Blender
  subprocess actually renders on -- confirmed via the same busy-percent
  polling that both cards now light up within one run. Vulkan
  (`--gpu-backend vulkan`) was tried too: no measurable per-render
  speedup on this workload (these are simple flat-shaded thumbnails,
  likely CPU/startup-bound rather than GPU-bound), and Mesa's own
  `MESA_VK_DEVICE_SELECT` multi-GPU env var didn't behave deterministically
  across repeated tries in the time available -- not used, staying on the
  OpenGL default (which DRI_PRIME reliably controls) instead of chasing a
  non-reproducible Vulkan multi-GPU setup further.
- Render output switched from PNG to WebP (quality 80, lossy) --
  real corpus examples came in at 2.5-4.7KB vs the old PNGs' much larger
  size, a meaningfully lighter live gallery. Resume logic treats either
  extension as "already done," so already-good PNGs from before the switch
  are never wastefully redone.

Full suite green, 601/601 (596 + 5 new: 2 `_sdr`, 2 `blend_mode`, 1
`--listfile-root`-independence regression from the same-day earlier
entry). The full `renders_full` corpus job was stopped, then restarted
from a cleared output directory once all of the above landed (Luna's own
call, given the scope -- not worth layering a fourth partial-reprocess
pass on top of the `--listfile`/WebP switch's own prior partial-reprocess
pass).

---

**2026-08-09**: New `husk export --listfile <path>` flag closes a real gap
found this session by a from-scratch corpus tool (not husk itself): a full
130,576-file render pass followed by cross-referencing every "missing"
FileDataID-named texture against a real `community-listfile.csv` snapshot
found 99.9% of them (81,809 of 81,890) were actually present in the corpus
under their own real name/path (e.g. `world/goober/bubble.blp`), not
renamed to a bare `<FileDataID>.{blp,png}` the way husk's exact-match
resolution alone can find — write-up in a sibling project's own findings
doc, `casc-tool`'s `FAILURES.md` item 13 (the remaining 79 genuinely-absent
FileDataIDs, mostly `deathknighteyeglow.blp` missing for nearly every
playable race, are casc-tool's own problem to investigate, out of husk's
scope). Per DESIGN.md's Non-goals section (the same clarification already
carved out for a local WoWDBDefs checkout backing `--dbd-dir`), a local
listfile snapshot is architecturally identical: optional, user-supplied,
never fetched/generated by husk itself. `src/listfile.hpp`/`.cpp` (new)
loads a `FileDataID;path`-per-line CSV; `export_materials.cpp`'s two
FileDataID resolution sites (the primary `baseColorTexture` slot, and each
`textureCount > 1` additional layer) both gained a new fallback tier —
tried only after the exact `<FileDataID>.{blp,png}` match fails, but
*before* the fuzzy same-basename pool, since a listfile-resolved path is
still deterministic (real data), not a guess. Threaded through as a plain
`std::unordered_map<uint32_t, std::string>` default parameter
(`buildLodTierMeshes`/`buildMaterialsAndPrimitives`), loaded once per
export in `cmd_export.cpp`, not per-batch — a real listfile is millions of
lines. Verified against the exact real case that motivated this:
`bloodelffemale_hd.m2` exported with `--textures /media/luna/data/wow_export
--listfile <community-listfile.csv>` now embeds 5 real images (previously
0, when `--textures` is pointed at the corpus root rather than the model's
own directory) including real listfile-resolved names for FileDataIDs
3536810/4530998/5210137 — the exact three this file's own much earlier
entry (Independent, unsupervised tasks session) flagged as having "no
matching local file at all" and left as an open, unconfirmed question.
7 new tests: `tests/test_listfile.cpp` (4, the parser itself — well-formed
lines, CRLF tolerance, malformed-line skipping, a bad path throwing) plus
3 CLI-level tests in `tests/test_cli.cpp` (a baseline proving the fixture
alone doesn't resolve without `--listfile`, the actual resolution working
including the `.blp`-in-listfile-vs-`.png`-on-disk extension swap, and a
bad `--listfile` path failing loudly). `DESIGN.md`'s Non-goals section,
`README.md`'s export flag table and texture-resolution-order prose, and
both `completions/husk.{bash,zsh}` (regenerated, plus `src/main.cpp`'s
hand-maintained completion-taxonomy tables updated per this file's own
"Hazards" note about that gap) all updated. Full suite green, 596/596
(589 + 7 new).

**2026-08-09, same day, follow-up**: Before restarting the full-corpus
render job with `--listfile` enabled, re-examined the design above and
found a real correctness gap: it reused `--textures` as the root a
listfile-resolved path is joined against, but `--textures` also drives the
*pre-existing* directory-local embedded-filename/same-basename matching
(`scanFuzzyTexturePool`'s `directory_iterator`, confirmed non-recursive by
reading it). Pointing `--textures` at the real corpus root — necessary for
`--listfile` to find anything, since a listfile path is corpus-relative,
not model-relative — would have made that flat, non-recursive scan see
only the corpus's ~13 top-level category folders, silently blinding the
matching responsible for most of the corpus's already-working 53,700+
successful renders, for every file, not just the ones `--listfile` was
meant to help. Fixed with a new, separate `--listfile-root <dir>` flag
(defaults to `--textures` for the simple single-directory case) — threaded
through as its own parameter (`buildLodTierMeshes`/
`buildMaterialsAndPrimitives`'s new `listfileRootArg`, resolved to
`texturesDir` when empty), used only by the two listfile-resolution sites;
`--textures` itself is untouched by this and keeps its original directory-
local role. One more real bug caught in the same pass, before it shipped
either: the new flag's zsh completion label ("corpus root --listfile's
paths are relative to") contained an apostrophe, which breaks the
single-quoted `_arguments` spec string the regenerated `completions/
husk.zsh` embeds it in — exactly the class of bug `src/main.cpp`'s own
`zshFlagLabel` doc comment warns "only these help strings are hand-written,
to stay inside zsh's quoting rules" about. Caught by actually running
`zsh -n completions/husk.zsh` (and `bash -n` on its bash counterpart) after
regenerating, not by inspection, and fixed before either script was
committed. One new test (`tests/test_cli.cpp`, "--listfile-root is
independent of --textures") proves the two roots resolve genuinely
separately: a hardcoded slot's real file stays co-located with the model
(found via `--textures`), while a totally unrelated fdid-resolvable slot's
file lives under a separate `--listfile-root` directory nowhere near the
model, and both resolve correctly in the same export. Full suite green,
597/597. The running `renders_full` job (resumed earlier this session
after the machine crash reported at the start of this thread) was stopped
first — `kill -TERM` on the driver plus a `pkill -f render_glb.py` sweep
for orphaned Blender children left mid-subprocess-call — specifically so
it wouldn't burn through the ~79,000 remaining files against the broken
`--textures`-as-corpus-root shape before this fix landed.

- **Last state**: Closed `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 2
  ("real placement geometry") end to end, prompted directly: "find what
  features the db files would unlock and implement infrastructure and
  tooling to support that... not MVP of WDC5 and DB2, but not a full on
  comprehensive everything covered implementation." Investigated what the
  now-complete DB2 relational-schema infrastructure (previous entry) could
  actually unlock inside `husk export` itself (not just the separate
  `db2-export` side tool, which never feeds the real pipeline by design) —
  landed on `CHAR_TEXTURE_COMPOSITING_TODO.md`'s own already-staged Stage 2
  as the right-sized target: real, not comprehensive (Stage 3's customization
  choice chain and Stage 4's pixel compositing stay out of scope, both for
  real reasons -- see below), but real infrastructure, not a proof of
  concept.

  Real column names for the three tables Stage 2 needs
  (`ChrModelMaterial`/`CharComponentTextureSections`/`ChrModelTextureLayer`,
  plus `CharComponentTextureLayouts` for the shared atlas size) confirmed
  directly from `reference/WoWDBDefs/definitions/*.dbd` and cross-checked
  against real local files via `husk db2-info`/`db2-export`. Found a real,
  useful naming inconsistency in Blizzard's own schema this way:
  `CharComponentTextureSections`' own FK column is spelled
  `CharComponentTextureLayoutID` (singular "Layout"), while `ChrModelMaterial`/
  `ChrModelTextureLayer` both spell it `CharComponentTextureLayoutsID`
  (plural) — a real fact now baked into `chrmodel_db2.hpp`'s own struct
  field names with a doc comment, not a typo.

  Real investigation surfaced a genuine, previously-undocumented-in-this-repo
  byte-format gap along the way: `chrmodeltexturelayer.db2`'s real local
  layout stores its own `CharComponentTextureLayoutsID` as a
  `$noninline,relation$` column — no field-array slot at all, unlike every
  other relation column checked so far, which are all inline
  (`$relation$`, no `noninline` tag, still position-matched normally). Its
  real value only exists in the section's own `relationship_map`
  (`db2::Section::relationshipEntries`, decoded structurally last session
  but never previously *consumed* for a real value lookup). Confirmed via
  `db2-info`'s existing relationship-map summary print: `chrmodeltexturelayer.db2`
  has `header.flags == 0x04` only (no `0x02`), so `relationship_entry.
  record_index_or_id` is a real record *index* here (not an ID, per DB2.md's
  own flag-0x02-gated note already implemented last session) — the
  `foreign_id` for records 0/1/2 was `2`, matching `chrmodelmaterial.db2`'s
  own layout ID 2 rows for those same records, a real, verifiable link.

  New `db2::nonInlineRelationValuesByRecord` (`db2.hpp`/`.cpp`) resolves
  this generically: returns `{}` (never a guess) whenever `header.flags &
  0x02` is set, since DB2.md's own note means `record_index_or_id` would be
  a real ID in that case, not an index this function knows how to invert
  (no real local fixture exercises that combination for a *value* lookup to
  verify against, only the reorder/ID-lookup cases already covered).
  Paired with new `dbd::findNonInlineNonIdFieldNames` (mirrors the existing
  `findIdFieldName`, but for the non-id non-inline case) so a consumer can
  ask "which column, if any, needs the relationship-map path" by real name.

  New `src/db2table.hpp`/`.cpp` (`husk::db2table::readNamedColumns`) is the
  actual infrastructure payoff: one generic function that reads any list of
  real column names from any `.db2` file, transparently picking the right
  of WDC5's three real column-storage shapes (inline field, non-inline ID,
  non-inline relation) per column, resolved via `--dbd-dir`. Built
  specifically so a future table (world-data work, per Luna's own earlier
  scope note on the SQLite exporter) doesn't need its own bespoke decode
  loop — just a struct and a column-name list. `src/chrmodel_db2.hpp`/`.cpp`
  is exactly that: four ~15-line loader functions wrapping `readNamedColumns`
  into real `ChrModelMaterial`/`CharComponentTextureSection`/
  `ChrModelTextureLayer`/`CharComponentTextureLayout` structs, joined by
  `CharComponentTextureLayoutsID`.

  Verified against real data twice, independently: a standalone scratch
  probe program linked directly against the new module (layout ID 1: 3
  materials, 12 sections, 12 texture layers — matching `db2-export`'s own
  SQLite output for the same tables/ID exactly), then a real end-to-end
  `husk export --db2-dir /media/luna/data/wow_export/dbfilesclient --dbd-dir
  reference/WoWDBDefs --char-layout-id 1` against the real
  `bloodelffemale_hd.m2` fixture — the resulting `.glb`'s own embedded JSON
  (grepped directly out of the binary, not just trusted from stdout)
  contains the real `chr_texture_layout` extras with the right atlas
  dimensions (1024x1024) and per-section rects. Headless Blender import
  (`tests/blender_import_check.py`) succeeded cleanly on the resulting file.
  `gltf_validator` against this same fixture reports 2,498,424 errors
  both with and without the new flags — confirmed pre-existing and
  unrelated (this project's own documented `ACCESSOR_JOINTS_INDEX_DUPLICATE`
  quirk specific to `bloodelffemale_hd.m2`'s own raw `JOINTS_0` data, not
  caused by this session's work).

  New CLI surface: `husk export --db2-dir <dir> --dbd-dir <dir>
  --char-layout-id <id>` (`cmd_export.cpp`'s `attachCharTextureLayout`) --
  deliberately a simpler two-state pattern than `--bones-dir`/`--phys`'s
  three-state `auto`/value/`none` resolution (`DESIGN.md`'s own "Three-state
  resolution, not two" section, now with a new paragraph explaining the
  exception): there's no model-relative "auto" to fall back to, since husk
  has no way to derive a `CharComponentTextureLayoutsID` from an `.m2` file
  on its own (needs `ChrModel.db2` plus a real display-ID/race/gender
  identity this project doesn't have, `chrmodel_db2.hpp`'s own module
  comment). All three flags must be given together or the feature is
  simply off; a partial set prints a diagnostic and skips, never a hard
  failure of the rest of the export. New `gltf::Skeleton::CharTextureLayout`
  (`gltf_skeleton.hpp`/`.cpp`) is the extras struct, same "inert, never
  applied to the render" treatment as `CorrectionSet`/`PhysicsBody` — only
  serializes when a skin exists (a model with 0 bones has nowhere to attach
  skin extras at all, same pre-existing constraint every other skin-extras
  field already has).

  Full suite green, 575/575 (up from 565). New test files:
  `tests/test_db2table.cpp` (unit-level -- `db2table::readNamedColumns`
  exercising all three column-storage shapes together in one synthetic
  fixture, plus `chrmodel::load` building real typed structs from four
  synthetic tables joined by layout ID, including the non-inline-relation
  case for `ChrModelTextureLayer` specifically) and
  `tests/test_cli_chrmodel.cpp` (CLI-boundary -- a real `husk export` run
  with a synthetic M2/skin/skel plus a synthetic two-table DB2/DBD fixture
  set, asserting the actual `chr_texture_layout` JSON text is present in
  the real output `.glb`'s bytes; a second case confirms a partial flag set
  skips cleanly). Completions regenerated
  (`completions/husk.bash`/`.zsh`) for the three new flags, including their
  value-taxonomy entries in `main.cpp`'s hand-maintained
  `bashValueCompletion`/`zshValueAction`/`zshFlagLabel` tables (the
  "doesn't pick up automatically" hazard this project's own CLAUDE.md
  already flags for new export flags). `README.md`
  (new "Character texture-layout geometry" usage paragraph + flags-table
  rows), `DESIGN.md` (flags table + a new "simpler two-state pattern"
  exception paragraph + an updated Non-goals cross-reference for the
  texture-type-resolution note), and `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`
  (Stage 2's own section rewritten to describe what was actually built,
  including the deliberate scope-down from the original per-candidate
  linking plan) all updated to match.

- **Last state**: Real DB2 column naming and a real DB2-to-SQLite exporter,
  landed the same session as (and directly downstream of) the DETL/DPIV/
  PCOL investigation and a repo-root cleanup pass — see the next entry
  below for the reorg/investigation's own full detail; this entry covers
  the DB2/DBD work specifically.

  Investigating `PCOL`'s bit-semantics gap (see the next entry) surfaced a real
  question worth checking directly rather than assuming: given DB2 access
  is now confirmed in scope (`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s own
  Background section, real local `.db2` files under
  `/media/luna/data/wow_export/dbfilesclient/`), was `WIKI_FINDINGS/M2.md`'s
  old framing of `PCOL`'s gap as a "permanent data-source gap" actually
  still accurate? Checked directly: `housedecor.db2` (the obvious candidate
  table for per-furniture-piece collision-flag semantics) is confirmed
  **0 bytes** in the current local export, along with five other housing-
  prefixed tables — a real, second instance of the same extraction-
  completeness gap `CHAR_TEXTURE_COMPOSITING_TODO.md` already found for
  `chrcustomization*.db2`. Reframed the finding (`WIKI_FINDINGS/M2.md`,
  `WIKI_FINDINGS_HISTORY.md` §18) from "permanently blocked" to "a real but
  currently unfulfillable DB2 dependency" — `pcol_files_for_exploration.txt`
  is kept on this basis, not deleted as stale.

  That same check raised the obvious next question directly: is the
  `CHAR_TEXTURE_COMPOSITING_TODO.md` Stage-1-recommended next task itself
  blocked the same way? Checked before recommending it: no — the three
  tables Stage 1/2 actually need (`chrmodelmaterial.db2`,
  `charcomponenttexturesections.db2`, `chrmodeltexturelayer.db2`) are all
  real, populated local files; only the *later*-stage customization-choice-
  chain tables are the ones confirmed 0-byte. First recommended "Stage 1:
  WDC5 parser" without checking git log first — corrected immediately when
  told directly it had already landed and was verified same-day
  (`ecd5a44`, "[VERIFIED] WDC5 DB2 parser proof of concept"), a real miss
  worth naming: should have checked history before recommending a task.

  Corrected recommendation: Stage 1's own doc comment already names the
  real remaining gap precisely — "no table-name-to-struct mapping...
  naming columns needs an external schema (DBD)." Asked directly whether to
  resolve that generically (a full WoWDBDefs `.dbd`-schema consumer for
  *any* table) or narrowly (hand-derive just the 2-3 tables Stage 2 needs,
  matching this project's existing per-format convention). Recommended the
  narrow path; the actual ask, once clarified, was broader than either: a
  real DB2-to-**SQLite** exporter ("I want to be able to convert DB2 to
  something actually browsable and industry standard"), with WoWDBDefs
  pulled as a local reference the same way `reference/wow.export` already
  is — never linked, never a hard dependency, used the same way real-data
  tests fall back to skipping when their fixture isn't present.

  Also caught, prompted directly ("this does not exist, or i could not
  find it"): `db2.hpp`'s existing module comment cited DB2.md's
  "Determining Field Types" section as the reason WDC5 carries no column
  names. Checked the real mirrored wiki page directly — the section exists,
  but it's about inferring a field's *type* (int/float/string across WDB2
  vs. WDB3/4 vs. WDB5/6), and says nothing about field *naming* anywhere.
  A real mis-citation, not a hallucinated section — fixed in `db2.hpp` and
  `README.md` to state the actual fact (no `field_structure`/
  `field_storage_info` entry in DB2.md's own struct definitions carries a
  name string) without citing a section that doesn't support the claim.

  **Implementation.** Cloned `WoWDBDefs` into `reference/` (gitignored,
  matching `reference/wow.export`'s existing role — dev-only source
  reference, never read at runtime). Its own `README.md` turned out to have
  a full, precise, machine-readable grammar spec for the `.dbd` text
  format — no reverse-engineering needed, unlike most of this project's
  other formats. `src/dbd.hpp`/`.cpp` (new): parses the `COLUMNS`
  (name/type) and `LAYOUT <hash>` (one or more hashes, field order,
  `$id$`/`$relation$`/`noninline` annotations) blocks; `manifest.json`
  (tableHash -> tableName) resolved via a narrow hand-rolled brace-depth
  object scan + two regex field extractions, not a real JSON parser
  (sufficient since the file is a flat array of flat objects, no nesting —
  same "no new dependency for a narrow, well-understood need" policy
  `blp/`'s synthetic-DDS-wrapper approach already uses). Fields are matched
  to a real `db2::File`'s own field array purely by *position*
  (declaration order, skipping `noninline` fields, which occupy no field-
  array slot in WDC5 itself) — not cross-validated against
  `FieldStorageInfo`'s own bit sizes; a mismatched inline-field count
  returns `nullopt`, never a guessed/misaligned name.

  Real bug caught before it shipped: the manifest-lookup regex literals
  used `R"(...)"`(empty-delimiter raw strings) whose content itself
  contained a literal `)"` sequence (`...[^"]*)"`), colliding with the raw-
  string terminator and truncating the literal mid-pattern — a real compile
  error (`missing terminating '"' character`), not silently wrong output;
  fixed with a named delimiter (`R"re(...)re"`).

  `pkgs.sqlite` added to `nix/flake.nix` (both the `cpp` devShell set and
  `packages.default`'s `buildInputs`, the latter caught independently after
  being pointed at directly — "lol forgot that one, good catch") and
  `CMakeLists.txt` (`find_package(SQLite3 REQUIRED)`, `SQLite::SQLite3`
  linked into `husk-lib`) — Luna's own explicit permission first, per this
  project's own flake-package-addition rule; she'd already added the
  devShell entry herself by the time the ask landed.

  New command, `husk db2-export <file.db2> <out.sqlite> [--dbd-dir DIR]`
  (`src/cmd_db2.cpp`): decodes every fixed-width, unencrypted section
  (TACT-encrypted and offset-map/sparse sections are skipped, with a real
  count printed, never silently dropped) into a real SQLite table — real
  column names/types via `dbd::resolveFieldNames` when `--dbd-dir` resolves
  a matching layout, generic `field_<N>` otherwise; a real WDC5 array field
  (`decodeField`'s own returned element count, not something DBD tracks the
  length of) gets one `<name>_<i>` column per element, since SQLite has no
  native array column type. Verified against real data both ways: with
  `--dbd-dir`, `chrmodelmaterial.db2` exports 336 rows into a real
  `ChrModelMaterial` table with real `ID`/`CharComponentTextureLayoutsID`/
  `TextureType`/`Width`/`Height`/`Flags`/`Field_9_0_1_34615_006` columns and
  plausible atlas dimensions (2048x1024, 512x256, ...), read back via the
  real `sqlite3` CLI; without it, the same file exports to generic
  `field_<N>` columns correctly; `namesreserved.db2` round-trips real UTF-8
  strings (including non-Latin/Korean text) through the same string-value
  path `db2-info` already used.

  Three new test files, full suite green throughout (555 -> 557/557):
  `tests/test_dbd.cpp` (7 synthetic grammar tests -- COLUMNS parsing,
  multi-hash LAYOUT lines, multiple LAYOUT blocks, `noninline` exclusion,
  a real field-count-mismatch-returns-nullopt case, `findLayout` -- plus 2
  real-data tests skip-gated on `reference/WoWDBDefs`'s presence, one of
  which resolves `ChrModelMaterial`'s real 7 column names end to end
  against the real file's own `table_hash`/`layout_hash`); `tests/
  test_cli_db2.cpp` (2 fully-synthetic CLI-boundary tests -- no external
  fixture needed -- spawning the real `husk db2-export` binary against a
  hand-built WDC5 fixture matching `tests/test_db2.cpp`'s own proven byte
  layout exactly, then reading the resulting `.sqlite` back via the sqlite3
  C API directly, confirming both the real-value path and the "unknown
  --dbd-dir table hash falls back cleanly, doesn't throw" path).

  Per Luna's own direct, twice-repeated scope clarification (quoted in
  `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s own top note): this SQLite
  exporter is an explicitly **separate side project** — "the real pipeline
  is the same as with modern blp's -- read the file, transform in memory,
  write to gltf," no SQLite round-trip in `export`'s own runtime path — but
  a legitimately valuable one on its own terms: a correctness cross-check
  for whatever the real in-process WDC5 consumer eventually becomes, and a
  general-purpose local data source for other consumers of this project's
  WoW-format work (explicitly flagged as likely to matter a lot once WMO/
  ADT world-data work starts). **Genuinely still open, not implied done**:
  the exporter's own stated further ambition -- a real relational schema
  with mapping/join tables preserving cross-file foreign-key relationships
  (e.g. `ChrCustomizationOption` -> `_Choice` -> `_Material`), not just one
  flat table per `.db2` file -- isn't built yet, today's exporter is
  genuinely one-file-in, one-table-out. Stage 2 of the real compositing
  pipeline (real per-table C++ structs feeding `export_materials.cpp`
  inside husk's own process) is also still unstarted -- `dbd::
  resolveFieldNames` proves the name-resolution half works end to end, but
  nothing yet threads those names into a typed struct the exporter
  consumes. `DESIGN.md`'s Non-goals bullet and `CHAR_TEXTURE_COMPOSITING_
  TODO.md`'s Stage 1 entry both updated to state this precisely.

- **Last state, same session, earlier**: a short DETL/DPIV/PCOL corpus-data
  exploration, followed by a repo-root cleanup pass, both prompted directly
  rather than self-initiated.

  DETL: recomputed all 1,043 real `DETL`-bearing files (1,386 records) from
  scratch rather than trusting the earlier session's own 1,386-record
  "sample" framing — confirmed `scale`/`diffuseColorMultiplier`/`unk0`/
  `unk1` are literally identical constants in every single real record; only
  `flags` (bit 3, 0x0000/0x0008) varies at all, and correlates cleanly with
  light-emitting decorative world props (torches, braziers, chandeliers,
  campfires) vs. everything else — plausibly a real shadow-casting toggle,
  inferred not confirmed. Cross-checked against a real exported light
  (`dr_chandelier_01_nosound.m2`) to confirm the "real data lives externally"
  hypothesis didn't apply here: `M2Light` itself already carries fully-
  authored, non-default color/intensity/attenuation, so `DETL`'s own
  multiplier reads as an unused identity hook, not a placeholder. Merged
  what had become two separate `WIKI_FINDINGS/M2.md` DETL sections (§11's
  stride/padding fix, a new §17 finding) into one, after being told
  directly the file's own organization should group by chunk/topic, not
  chronological discovery order — the whole file was regrouped, not just
  the DETL sections.

  DPIV: decoded all 2,632 real `DPIV`-bearing files/2,943 records directly
  from the existing `m2_unknown_chunks_report.json` hex dump. Found real
  structure — fields 0–2 read as plausible 3D points, field 3 is a small
  integer (raw bits 0–3, not a real float) rather than a clean sequential
  index (only 56/260 multi-record files have strictly ordered field-3
  values), fields 4–7 are always zero — but stopped short of a confirmed
  semantic conclusion. Written up as a new `TODO/DPIV_TODO.md` (not left as
  a dead scratch finding) with four concrete next-step leads, since the
  underlying `dpiv_files_for_exploration.txt` file list is a genuine future
  lead, unlike `pcol_files_for_exploration.txt`/`detl_stride_report.json`
  (both closed investigations, kept only as cheap-to-regenerate artifacts).

  PCOL: initially concluded the bit-semantics gap was closed as far as
  investigation goes ("a data-source gap, not an investigation gap"),
  before being asked directly whether that framing still held now that DB2
  access is confirmed in scope — it didn't, fully; see the entry above for
  the `housedecor.db2`-is-0-bytes finding that came out of re-checking it.

  **Repo-root cleanup, prompted directly** ("let's do some cleanup, move
  active TODO files into a new TODO subdir, so we can see what kind of mess
  we are dealing with there"): all 17 files that self-describe as an "open
  punch list, not a historical record" (16 `*_TODO.md` files plus
  `TODO_correctness.md`) moved from repo root into a new `TODO/` directory;
  the 11 world-specific ones (`ADT_LOD`, `ADT_TERRAIN`, `COLLISION_CULLING`,
  `FOG_VOLUMES`, `LIGHTING`, `LIQUID`, `PM4_PD4`, `WDT`, `WMO_GEOMETRY`,
  `WORLD_MISC_METADATA`, `WORLD_PLACEMENT`) moved one level deeper into
  `TODO/WORLD/` along with `TEXTURE_TYPE_COLLISIONS_REPORT.md` and
  `NOTE_ABOUT_WORLD_HANDLING.md` (both real world-scoped documents, not
  `*_TODO`-named, a judgment call flagged explicitly rather than assumed).
  Every cross-reference across docs and source-code comments fixed via a
  scripted, word-boundary-safe pass rather than ~50 manual edits — caught
  and corrected two bugs of its own along the way (files that already had a
  single `../` prefix from the first move needed a second `../` once moved
  a level deeper; a duplicate `## 2.` section header collision in
  `EYES_ON_FINDINGS.md`'s renumbering).

  Told directly that reporting "Items 1/2/5 are fully resolved" without
  actually removing them was insufficient ("should be punched out of a
  punch card") — `EYES_ON_FINDINGS.md`'s three fully-resolved items (bone
  naming, root-bone weighting, the `Submesh::Level` bug — a real fixed bug,
  not a non-issue) were deleted outright, not just marked closed; the three
  genuinely open items (material-naming-granularity, the confusing non-M2-
  file error, `alternate_textures`' compositing gap) kept and renumbered
  1-3, every internal `finding #N`/`item N` self-reference fixed to match.

  `HUSK_CORPUS_FINDINGS.md`/`HUSK_CORPUS_FINDINGS2.md` deleted after
  confirming their content was fully migrated: round 1's findings were all
  fixed or superseded by round 2's own re-sweep; round 2's headline buckets
  already live verbatim in `README.md`'s extraction-gap paragraph, and its
  one loose thread (3 files with NaN/backward-timestamp animation
  keyframes) turned out to already be independently duplicated in
  `TOOL_COMPARISON.md`, so nothing was lost. `INLINE_COMMENT_RULES_
  VIOLATIONS.md` (a real, still-outstanding pre-v1 cleanup audit — "sweep
  every `// TODO: Remove` comment before v1 ships" hadn't happened) was
  condensed into a short pending-cleanup note at the top of `README.md`
  per direct instruction, then deleted, rather than kept as a stale, 413-
  line audit cluttering repo root. `TEXTURE_TYPE_COLLISIONS_REPORT.md` was
  explicitly *not* deleted alongside it despite looking similar (a "report"
  file) — its content isn't actually duplicated anywhere else, unlike the
  two corpus-findings files.

- **Last state**: Investigation-only session, `PCOL`'s `flags` field
  (real, undocumented per-triangle data, flagged "exposed raw, not
  interpreted" since it was first implemented — WIKI_FINDINGS_HISTORY.md
  §10/§9). A dedicated scan (husk's own already-verified `dump-chunks`
  output, no new C++ parser needed) over all 2,354 real `PCOL`-bearing
  files (`pcol_files_for_exploration.txt`) found `flags` is structurally
  a real bitmask, not a sequential enum: every distinct value across the
  full corpus (`{0,1,2,3,4,5,6,7,8,23,221}`) decomposes into a small
  combinable bit set (0–7 is the exact power set of bits 0/1/2). 98.4% of
  files use only bit 0; every value above 5 is a singleton confined to
  one specific decorative doodad (a light sconce, a food prop, a
  player-housing lamp) — plausible per-object special collision
  behavior, but individual bit meaning is unconfirmed (no wiki field
  names, no DB2/client data — `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md` already
  has a staged, in-scope plan for real local WDC5 DB2 access for a
  different feature; the same path would apply here if this is ever
  worth chasing further, not a permanent dead end). Also found a real,
  separate structural fact the original implementation's own doc comment
  overstated: `flagsCount` is usually but not always equal to
  `faceNormCount` — 3 real files (`flagsCount == faceNormCount + 8`) are
  a genuine exception. No code changes were needed for either finding —
  `dumpPcol` (`src/dump_chunks_misc.cpp`) already reads every region via
  its own independent header field, exactly as the format requires — this
  was purely a documentation gap, now closed: `WIKI_FINDINGS_HISTORY.md`
  §16 (new, full byte-level format writeup), `WIKI_FINDINGS/M2.md` (new
  summary entry), `DESIGN.md`/`M2_COMPLETENESS.md`/`src/
  dump_chunks_misc.hpp`'s own doc comment all updated to match, including
  dropping the pre-existing "niche" characterization of player-housing
  content (a full expansion feature, not a niche corner case) and
  correcting a "DB2 data husk doesn't have, by design" overclaim now that
  DB2 access is real, staged, in-scope work elsewhere in this repo, not a
  permanent non-goal. Scan tooling itself (`pcol_flags_scan.py`, run via
  the established `direnv exec . uv run --python tools/venv/bin/python
  <script>` pattern) lived in the session's own scratchpad, not committed
  — a one-off investigation script, not a reusable corpus tool.

- **Previous state**: Third independent, unsupervised task in a row, same
  session (small follow-on to the Light-animation entry directly below,
  already committed as its own `[UNVERIFIED/STAGING]` commit). Noticed
  while resolving Light's `visibility` track that `M2Attachment::
  animate_attached` (M2Track<uint8_t>, "whether the attached model
  animates with this one, default true") has the exact same shape and was
  still explicitly marked "skipped ... not something husk's glTF export
  has a slot for yet" in `m2_scene.hpp`'s own doc comment -- a second,
  smaller, self-identified gap right next to the one just closed.
  Implemented: `m2::Attachment` gained `animateAttachedTrackOffset`;
  `gltf::Skeleton::Attachment` gained an `animateAttached` curve-vector
  field; `cmd_export.cpp`'s Light-specific `resolveLightVisibilityCurve`
  was renamed to `resolveRawByteTrackCurve` (it was already fully generic
  -- a raw M2Track<uint8_t> resolver, nothing Light-specific about its
  body) and reused for both, rather than a second near-duplicate function;
  `gltf_skeleton.cpp`'s attachment-node loop now writes an
  `animate_attached` extras key when there's real data.

  Checked against real data before assuming this was ever actually
  populated: 3 real fixtures with real attachments (`wolf.m2` -- 12,
  `sword_2h_ashbringer_a_01.m2` -- 6, `mace_1h_warfrontsforsaken_d_01.m2`
  -- 5, 23 total) all resolve to *zero* real `animate_attached` keyframe
  data -- a genuine negative result matching the wiki's own "only a bool
  is used, default is true" note, not a bug (same "real, checked absence"
  shape as several other findings this project has logged before, e.g.
  `chrcustomization*.db2`'s 0-byte files in the concurrent DB2 session).
  Correctness still covered by two new synthetic round-trip tests
  (`tests/test_gltf_skeleton.cpp`) since no real fixture exercises the
  populated case; `gltf_validator` confirmed 0 errors/0 warnings on the
  real `wolf.m2` export regardless. Full suite green, 546/546.
  `M2_COMPLETENESS.md`'s Attachments row updated (`deref`/`native` ->
  `full`/`native + extras`) with the real negative-result note attached so
  a future reader doesn't mistake empty output for an unresolved
  offset bug.

- **Last state**: Second independent, unsupervised task in a row (same
  session pattern as the lookup-table entry directly below, already
  committed as its own `[UNVERIFIED/STAGING]` commit). Picked
  `M2_COMPLETENESS.md`'s Lights row: `M2Light`'s 7 `M2Track` fields
  (ambient/diffuse color+intensity, attenuation start/end, visibility) were
  the one field this project's own doc comment
  (`gltf_skeleton.hpp`'s old `Skeleton::Light`) flagged by name as "a
  separate, larger problem (same sibling scope as the animated material
  tint/fade curves ... above)" -- a specific, self-identified next step,
  not a guess. Implemented: `m2::Light` (`m2_scene.hpp`/`.cpp`) now carries
  all 7 M2Track byte offsets (parse depth `deref` -> `full`, matching
  `m2::Color`'s own offset-storage precedent); `cmd_export.cpp` gained
  three light-specific curve resolvers
  (`resolveLightColorCurve`/`resolveLightFloatCurve`/
  `resolveLightVisibilityCurve` -- the third exists separately because
  `visibility` is a raw 0/1 `M2Track<uint8_t>`, not a fixed16-scaled value
  like the material fade curves, so it must NOT go through
  `decodeFixed16`); `gltf::Skeleton::Light` gained a `type` field plus 7
  curve-vector fields, reusing `gltf::Material::AnimatedColorCurve`/
  `AnimatedScalarCurve` directly (via a new `#include "gltf_mesh.hpp"` in
  `gltf_skeleton.hpp`) rather than duplicating the shape a third time;
  `gltf_skeleton.cpp`'s light-node-emission loop now writes a `type`/
  `light_animation` extras pair per light node, mirroring
  `gltf_mesh.cpp`'s own tint/fade extras JSON shape (mirrored, not
  reused -- different translation unit, building a node's extras rather
  than a material's).

  Verified against real data, not just synthetic: every weapon/creature
  fixture already committed to `test_data/` turned out to have
  `lights.count == 0` (a real, checked fact -- M2Light data is essentially
  absent from that model category in practice), so a real corpus scan
  of `interface/glues/models/ui_mainmenu_*` (login-screen models, the
  wiki's own documented use case for this data) found a genuine hit:
  `ui_mainmenu_pandaria.m2`, 2 real lights, both with genuine per-sequence
  keyframe data (plausible warm/cool RGB tuples, 0..1-range intensities,
  sane attenuation values) -- confirmed by hand via `strings` on a real
  export before writing any test (no working Python in this session's
  shell without going through the flake, worked around rather than
  bypassing the sandboxing rule). New fixture committed to (gitignored)
  `test_data/interface/glues/models/ui_mainmenu_pandaria/` (`.m2` + its
  auto-resolved `00.skin`, ~660 KB total, same size class as every other
  real fixture here), registered through `test_data_paths.hpp`/
  `test_main.cpp` the same way every other `HUSK_TEST_*` fixture is.
  New tests: two synthetic (`tests/test_gltf_skeleton.cpp`, the
  round-trip case and the "no animated data -> no `light_animation` key"
  case) plus one new real-fixture integration test in a new file
  (`tests/test_integration_lights.cpp`, added to `CMakeLists.txt` --
  split out rather than folded into `test_integration_weapons.cpp` since
  every fixture there is weapon-scoped and this one genuinely isn't).
  Full suite green, 544/544 (`./build/husk-tests`); real export also
  independently confirmed clean via `gltf_validator` (0 errors, 0
  warnings). `M2_COMPLETENESS.md`'s Lights row updated (`deref`/`native`
  -> `full`/`native + extras`). Deliberately left uncommitted for Luna to
  review at first, then committed as `[UNVERIFIED/STAGING]` per her own
  explicit instruction to follow the same pattern as the two independent
  tasks before it in this session.

- **Last state**: Independent, unsupervised task -- picked TODO/TODO_correctness.md's
  former item 2 (five uint16 lookup-table arrays -- `sequenceLookup`/
  `boneLookup`/`textureLookup`/`attachmentLookup`/`cameraLookup`, wowdev.wiki
  M2#Header -- parsed into `Array` descriptors but never dereferenced or
  printed anywhere, confirmed via a real grep sweep before starting). Fixed:
  `husk info` (`cmd_info.cpp`) now dereferences all five via the existing
  `m2::parseUint16Array`, resolving each entry's index to a name where husk
  already has one (`keyBoneName`/`textureTypeName`/`attachmentTypeName` --
  no new name tables, reused what M2's per-record printing already uses) and
  skipping 0xFFFF ("-1", "no entry") sentinels. `sequenceLookup` specifically
  is printed as its real hash-bucket shape (`bucket = anim_id % count`,
  quadratic-probe collision per the wiki), not pretended to be a direct
  id-indexed array -- printing it as "bucket N -> sequence[value] (id=...)"
  rather than mislabeling the bucket index as an animation id. Verified
  against two real fixtures, not just synthetic data:
  `test_data/creature/wolf/wolf.m2`'s `bone_lookup` resolves key bone 26 to
  bone 0 named "Root" and key bone 6 to the real Head joint (26/27/35 real
  key-bone slots depending on model, matches the wiki's own count note); its
  `attachment_lookup`/`camera_lookup` resolve cleanly too.
  `bloodelffemale_hd.m2`'s `texture_lookup` resolves texture type 1 ("skin")
  through type 20 ("char_jewelry") to the exact texture indices matching its
  own hardcoded per-texture `type=` fields printed just above. One new
  regression test (`tests/test_cli_info.cpp`, synthetic header built past
  `minimalMd20()`'s 0x130-byte end, one resolvable entry + one 0xFFFF
  sentinel per array) -- confirmed to exercise the sentinel-skipping and
  name-resolution paths, not just presence. `M2_COMPLETENESS.md`'s lookup-
  tables row updated from `descriptor`/`none`/"unclaimed" to `deref`/
  `diagnostic`/"pure indirection metadata, no independent renderable shape";
  `TODO/TODO_correctness.md`'s former item 2 removed outright per the file's own
  convention, remaining item renumbered (was 3, now 2). Full suite green,
  541/541 (`./build/husk-tests`). Deliberately left uncommitted for Luna to
  review, per this task's own instructions -- nothing here has had human
  eyes on it yet. No overlap with the concurrent `[UNVERIFIED/STAGING] WDC5
  DB2 parser` commit found already on `master` when this session started
  (`src/db2.*`/`src/cmd_db2.cpp`) -- different M2/DB2 scope entirely, not
  touched here.

- **Last state**: Continuation of the same `TODO/GEOSET_MASK_TODO.md` effort,
  same session as the entry below, prompted by more of Luna's own real
  interactive Blender testing. Ground-truthed the tabard bug from the
  entry below by hand, in Blender's real GUI, not headless scripting:
  group 12 does genuinely control the tabard flaps (`variant_2` = both
  flaps, `variant_3` = back only, `variant_4` = front only), and a real,
  separate gap was found the same way -- no "none" option exists, because
  `bloodelffemale_hd.m2` simply has no submesh for "no tabard at all"
  (geoset ID 1201 is absent from this file's own `.skin` data, a real fact
  about the model, not a husk omission).

  Proposed the real architectural fix directly, two ideas that turned out
  to combine into one design: (1) don't chain `Separate Geometry`
  sequentially against a shrinking remainder -- select independently from
  the original mesh and recombine with `Join Geometry`; (2) go further --
  build one real boolean-math expression per vertex first (no geometry
  operations at all), then apply exactly one `Separate`/`Delete Geometry`
  to the result, hypothesized as "mutually exclusive inside a group,
  inclusive OR between groups," which matches exactly what husk's own tag
  data guarantees. Implemented as a single combined design: a
  `data_type='STRING'` `Menu Switch` per group outputs the *name* of
  whichever variant's vertex group is currently selected (or a sentinel
  string matching no real attribute, for a new synthetic "none" item --
  closing the tabard gap above for every group generally, not just
  tabard), and that string feeds `Named Attribute`'s own `Name` input as a
  *link, not a constant* -- confirmed scriptable this session (the crux of
  the whole redesign, verified with a small synthetic probe before writing
  the real graph). One dynamic attribute read per group then tells you,
  per vertex, whether it belongs to whichever variant is currently active,
  without enumerating every variant's own comparison per switch. Combined
  with a per-group "does this vertex belong to this group at all" OR-chain
  (still enumerated once per group, but purely boolean, no geometry
  operations), the whole graph collapses to one boolean expression
  evaluated once, then exactly **one** `Separate Geometry` call against
  the pristine input mesh -- directly removing the "chain 109 sequential
  separations, each re-deriving selection against an already-shrunk
  remainder" shape that was the leading suspected cause of the arms bug
  from the entry below.

  **Verification hit real limits a second time in the same session, not
  resolved, reported honestly rather than claimed fixed.** A first
  headless check showed vertex counts frozen at exactly one value across
  every single switch tried -- an impossible result if the graph were
  working, which is exactly what made it clear something was wrong before
  trusting it. Root cause: a real Blender scripting gotcha, not a graph
  bug -- `mod[identifier] = value` alone doesn't propagate to the
  evaluated depsgraph without also calling `mod.node_group.interface_
  update(bpy.context)`, something the *previous* redesign's own
  verification script happened to call but this session's fresh scripts
  initially didn't. Fixed, and vertex counts did start visibly responding
  to switches after that. But a more targeted check -- tracking all 26
  real tabard-flap vertex positions (the same ones from the reference
  screenshot two entries below, captured from the pristine pre-modifier
  mesh) across all four of group 12's real states -- found **zero of 26
  present in any state**, including `variant_3`/`variant_4`, which should
  each show roughly half of them; `variant_2` ("both") also evaluated to
  *fewer* total vertices than `variant_4` ("front only"), backwards if
  "both" is really the union of the other two. Both are real, concrete,
  concerning signals -- but headless position-matching has already
  produced one confusing, hard-to-trust result on this exact feature this
  session (the ordinal-vs-identifier stored-value confusion in the entry
  below), so rather than chase a second layer of "is this a real bug or a
  flaw in how I'm checking it" without visual ground truth, this was
  handed back exactly where it was found: real interactive Blender GUI
  testing is what correctly found both original bugs and correctly
  ground-truthed group 12's real semantics tonight -- headless scripting
  has now gotten this feature specifically wrong more than once, and isn't
  the trustworthy verification path here. Full C++ test suite unaffected
  throughout, still green, 532/532 -- everything this entry describes is
  pure Python/Blender-script work, no export-path code touched.

---

- **Last state**: Same overall `TODO/GEOSET_MASK_TODO.md` effort, continued in
  the same session as the entry below — two real design/naming follow-ups,
  then real bugs found via actual interactive Blender use.

  (1) Tag-joint naming changed from a single `geoset_<id>` token to
  comma-separated, prefix-tagged `group_<n>,variant_<n>` fields, prompted
  directly as prep work for a future geometry-nodes rewrite ("splitting
  the group and the variant into neat separate text prefixed fields would
  be nice... that way i can just split the string by comma delimiter, and
  remove prefixes"). Updated everywhere the naming was produced or
  consumed (`gltf_skeleton.cpp`, `gltf.hpp`'s doc comment, the companion
  script's parser, the synthetic test, `TODO/GEOSET_MASK_TODO.md`/`DESIGN.md`),
  verified end to end against the real export (`group_0,variant_0` etc.,
  identical masking behavior to before the rename).

  (2) That "future geometry-nodes rewrite" arrived the same session,
  prompted directly ("utilize the switch nodes to do the filtering instead
  of a insane stack of mask modifiers"). Researched feasibility first (web
  search, since `docs.blender.org` 403'd every direct fetch attempt
  regardless of page for this session's fetch tool) and found a real
  citation from the actual Blender PR that implemented Menu Switch,
  confirming `enum_definition.enum_items` as the real scripting API before
  writing anything. Rebuilt `tools/husk_blender_geoset_mask.py` around a
  real Geometry Nodes graph: one `Menu Switch` dropdown per geoset group,
  fed by a chain of `Separate Geometry` nodes each peeling one variant off
  a running remainder. Found and fixed three real API gotchas by direct
  empirical probing before committing to the full build: `GeometryNodeMenuSwitch`
  starts with two dead placeholder items that must be `.clear()`-ed; its
  `Menu` input only becomes a real modifier-panel dropdown once promoted
  to a `NodeSocketMenu` interface entry and *linked before* `default_value`
  is set (setting it first throws `enum "..." not found in ()` since an
  unlinked socket has no known items yet); the exposed modifier value is
  stored by integer index, not name (confirmed by a real `TypeError` on a
  string assignment). Verified against the real `bloodelffemale_hd.m2`
  export: default-state evaluated mesh matched the just-superseded
  Mask-modifier version's own vertex count exactly (4,232), and switching
  one group's dropdown changed the count (4,232 -> 4,131), confirming
  functional correctness, not just structural plausibility. Fully replaced
  the Mask-modifier version rather than kept as a fallback.

  **Real interactive use the same day (not headless scripting) found that
  aggregate vertex-count checks weren't enough** — two real bugs, reported
  directly with a reference screenshot (Blender Edit Mode, vertex-index
  overlay, tabard back-flap region, vertex indices 20599-20661
  transcribed by hand into `TODO/GEOSET_MASK_TODO.md` since the pasted image
  itself has no accessible filesystem path this session's tooling could
  copy from): (1) picking a different hairstyle (geoset group 0) makes
  unrelated arm geometry disappear; (2) the tabard back-flap never
  disappears no matter what's selected. Same-session follow-up
  investigation, evidence-based, not fully conclusive:

  - Wrote a standalone tinygltf-linked scan tool (scratchpad only) proving
    husk's own C++ export has **zero** cross-*group* vertex tagging across
    the entire real export — ruled out as the cause, the raw glTF data is
    clean, whatever's wrong is in how Blender evaluates the graph built
    from it.
  - Directly inspected the actual built node tree (not just the Python
    that built it): `Compare`'s implicit "B" input really does default to
    exactly `0.0`, and no two `Separate Geometry` nodes share an upstream
    source — ruled out a wiring bug.
  - Found a real, evidenced, unresolved lead instead: a minimal synthetic
    repro (one quad, 2 of 4 verts selected, split across two triangles
    that each straddle the boundary) showed `GeometryNodeSeparateGeometry`
    with `domain='POINT'` (the default, never overridden) does **not**
    cleanly partition geometry — both triangles vanished from *both*
    Selection and Inverted outputs entirely. A structural risk in a design
    that chains 109 sequential separations, each re-evaluating selection
    across the whole remaining mesh — not yet confirmed as *the* mechanism
    reaching all the way to arms specifically, that needs real interactive
    GUI inspection, not more headless scripting.
  - Cross-referenced the exact vertex indices from the reference
    screenshot directly against the imported mesh's own vertex-group
    data: all 26 carry a real `group_12,variant_3` tag at the expected
    ~0.5 rescaled weight — ruled out "untagged geometry" as Bug 2's cause.
  - Found a suspicious, unresolved discrepancy: the modifier's raw stored
    default value for two very differently-sized groups (25 items vs. 3)
    showed the identical value `2` before any interaction — leading
    theory, unconfirmed, is that the two cleared placeholder items still
    occupy internal identifier slots 0/1, meaning "stored value == ordinal
    list index" (an assumption this session's own verification scripts
    made) may itself be wrong, which would mean at least some of tonight's
    checks were misreading their own results rather than exposing a
    second real bug.

  Full C++ test suite green throughout, 532/532 (unaffected by any of
  this — pure Python/Blender-script work). Explicitly **not fixed this
  session** — both bugs, and the stored-value/ordinal-index question, are
  handed off with real, concrete next steps (`TODO/GEOSET_MASK_TODO.md`'s
  "Known bugs"/"Follow-up needed" sections) needing actual interactive
  Blender GUI access to resolve, continuing in a fresh session/thread per
  Luna's own direct instruction.

---

- **Last state**: Implemented `TODO/GEOSET_MASK_TODO.md` end to end (new this
  session), prompted directly by Luna investigating `EYES_ON_FINDINGS.md`'s
  eye-glow finding and then asking how Blender's Mask modifier could hide
  WoW's mutually-exclusive geoset variants (hairstyles, boot cuffs,
  eye-glow, ...) that husk exports unfiltered (no DB2 customization data,
  `DESIGN.md`'s Non-goals). Landed on a real, verified mechanism: extra
  inert "tag" joints appended to the existing skin (never real bones,
  never posed) woven into a second `JOINTS_1`/`WEIGHTS_1` attribute set per
  geoset ID -- Blender's *stock* glTF importer creates one real vertex
  group per skin joint as an ordinary side effect of skin-weight import, so
  this needed zero custom Blender-side mesh-parsing (a competing "custom
  importer that bypasses Blender's own vertex compaction" design was tried
  and rejected first -- confirmed empirically that Blender's stock importer
  does *not* preserve a 1:1 accessor-index<->Blender-vertex-index mapping,
  195,498 raw positions became 32,939 Blender vertices on a real export,
  ruling out any "read the raw index buffers, poke Blender's post-import
  mesh" shortcut). Also empirically verified, before writing any code: (1)
  stacking a full second 1.0-summing weight set on top of real bone weights
  doesn't distort deformation, because Blender's Armature modifier
  renormalizes total influence weight across every joint set at evaluation
  time regardless of what's stored (posed a real bone, checked the actual
  deformed vertex position via `evaluated_get`'s depsgraph -- moved by
  exactly the pose delta, not doubled); (2) Blender vertex groups are
  mesh-owned data, independent of the armature's bones -- deleting a fake
  tag bone post-import leaves its vertex group/weights completely
  untouched and a Mask modifier targeting it keeps working, verified before
  and after deletion, plus a clean re-export afterward. A real, separate
  dual-armature alternative (avoid touching the real skin at all) was also
  raised, investigated, and ruled out with a concrete technical reason
  (different `JOINTS_0` data per node requires a genuinely separate glTF
  mesh entry, which Blender's importer does *not* auto-share vertex-group
  data across the way it does for two nodes pointing at the literal same
  mesh index) -- written up in the TODO doc rather than silently dropped.

  Implementation: `Skeleton::geosetTags` (`gltf_skeleton.hpp`) — one tag
  joint per distinct geoset ID, appended to `skin.joints` strictly after
  every real bone (the one invariant this whole codebase protects
  religiously -- multi-root synthesized-parent-node precedent followed
  exactly: parented under the single real root joint, or the synthesized
  multi-root parent, so the skin's "closest common root" property still
  holds for `gltf_validator`). `emitMeshNode` (`gltf_mesh.cpp`) builds
  `JOINTS_1`/`WEIGHTS_1` from `Primitive::skinSectionId`, splitting weight
  evenly across however many distinct tags touch a seam vertex.
  `cmd_export.cpp` populates `geosetTags` from the union of distinct
  `skinSectionId`s already collected for the existing geoset-extras
  feature (`BuiltMaterials::distinctSkinSectionIds`, no new collection
  logic needed). A real bug was caught by this project's own
  gltf-validator-backed test suite before landing, not after: a tagged
  vertex's combined weight total across both sets was 2.0, which
  `gltf_validator` correctly flags (`ACCESSOR_WEIGHTS_NON_NORMALIZED`)
  even though Blender's own runtime renormalizes regardless -- fixed by
  rescaling *both* sets down together per tagged vertex so the stored
  combined total is exactly 1.0 again, a pure file-format fix with a
  provable zero effect on Blender's actual rendering. A second real bug,
  same class of catch: the multi-root synthesized parent node's own index
  formula wasn't updated to account for the newly-inserted tag-node range,
  producing `gltf_validator` "not a common root"/"not a root node" errors
  on a real multi-root weapon fixture -- caught by the existing test suite,
  fixed by correcting the index arithmetic.

  Verified at real scale, not just synthetic fixtures: a standalone
  tinygltf-linked scan tool (scratchpad only, not committed) confirmed
  every vertex's combined `WEIGHTS_0`+`WEIGHTS_1` sum is exactly 1.0 across
  the real `bloodelffemale_hd.m2` export (113 geoset IDs, 245 real bones ->
  358 skin joints). That same real export surfaced 1.5M+ raw
  `gltf_validator` messages when run with full resource validation on --
  traced down to a pre-existing, unrelated data property (6,879 vertices
  with a duplicate joint index in their own raw `JOINTS_0` slots, husk
  never modifies those values, only copies them through from the M2's own
  `boneIndices`) and confirmed *not* caused by this session's work: a
  clean fixture already covered by an existing "zero errors" conformance
  test (`wolf.m2`, with the new tag joints active) scans with zero bad
  sums and zero duplicate joints via the same tool. Flagged in
  `TODO/GEOSET_MASK_TODO.md` for whoever next touches raw M2 bone-index
  handling, out of scope for this feature.

  Four existing conformance tests needed their hardcoded
  `skin.joints.size() == header.bones.count` assertions updated to account
  for the real, legitimate growth (`+ <distinct geoset ID count>`, counted
  independently from each primitive's own `geoset_id` extras as a real
  cross-check, not a tautology) -- expected per the TODO doc's own
  prediction, not a regression. Two new synthetic unit tests
  (`tests/test_gltf_skeleton.cpp`) lock in the mechanism directly: tag
  joint naming/parenting/`JOINTS_1`/`WEIGHTS_1` values including the
  rescale, and a no-`geosetTags` case proving zero footprint when unused.
  Full suite green, 532/532.

  Stage 6, the companion Blender script
  (`tools/husk_blender_geoset_mask.py`) -- explicitly anticipated back in
  an earlier session's `DESIGN.md` note ("a companion Blender-side script
  that hides extras-tagged-but-visible geometry post-import is real,
  deliberate usability tooling for later... deferred until someone
  actually wants to *use* exports interactively," which is exactly what
  this session's prompt was) -- walks every `geoset_<id>` vertex group,
  groups by `geoset_group` (`id // 100`, matching husk's own extras
  convention), adds one invert-mode Mask modifier per non-default variant
  (lowest ID kept visible, same disclaimed-placeholder-default precedent
  as `orderCandidatesForDefault` elsewhere in this project), then deletes
  every tag bone from the armature. Verified end to end against the real
  export: 358 armature bones before running the script, 245 after (tag
  bones fully removed, matching the real M2 bone count exactly); 90 Mask
  modifiers created correctly; all 358 vertex groups still present after
  bone deletion; the actual evaluated (post-modifier) mesh drops from
  32,939 raw vertices to 4,232 visible ones -- masking is genuinely doing
  something, not a no-op. This closes out every stage of the plan.

---

- **Last state**: Closed `TODO/TODO_correctness.md`'s former item 4
  (texture-transform pivot-correction math, constant case), picked as a
  self-contained task to work through solo (no DB2/casc-tool dependency
  like `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`, no fuzzy-matching design
  decision like `TODO/BONE_NAME_DEDUCTION_TODO.md` tier 2 -- both considered
  and passed over for that reason).

  Derivation: wowdev.wiki's M2#Texture_Transforms note says rotation
  pivots around the texture's own center (0.5, 0.5), and gives the recipe
  (translate to center, rotate, translate back) but not a closed form.
  `reference/wow.export`'s `M2RendererGL.js` (`_update_tex_matrices`) has
  the real client's own matrix composition: for each of rotation/scaling/
  translation independently present, `local_mat = local_mat *
  T(0.5,0.5)*R_or_S*T(-0.5,-0.5)` (rotation/scaling) or `local_mat =
  local_mat * T(tx,ty)` (translation), composed in that order. Expanding
  this out algebraically as one affine map `uv' = M*uv + t` gives `M =
  R*S` (matching `KHR_texture_transform`'s own scale-then-rotate order
  exactly) and `t = R*S*translation + R*t_S + t_R`, where `t_S = pivot -
  S*pivot` and `t_R = pivot - R*pivot` (pivot = (0.5, 0.5)) -- derived by
  hand, then verified two ways before writing any `src/` code: (1) by hand
  against `brewfestmount.m2`'s simplest real case (180-degree rotation
  only, `offset` should come out to exactly (1,1) -- did) and
  `bloodknightcharger.m2`'s combined case (180-degree rotation + (1.0,
  1.5) scale, `offset` should come out to exactly (1, 1.25) -- did); (2)
  against 20,000 randomized (angle, scale, translation, uv) trials
  comparing the closed form against a literal re-implementation of
  `wow.export`'s own translate-rotate-translate matrix composition (a
  scratch C++ program, not checked in) -- max error 1.8e-15, floating-
  point noise only.

  Real fixtures: `tools/find_texture_transform_files.py` (written a prior
  session, already known to have found `brewfestmount.m2`/
  `bloodknightcharger.m2`/`unboundairelemental_low.m2` as real-corpus
  candidates) was rerun against the two chosen files to get their exact
  transform indices/values before writing tests. Copying them into
  `test_data/creature/brewfestmount/`,`.../bloodknightcharger/`
  (`.m2`+`.skin` only, no `.blp` textures -- committing the texture bytes
  wasn't needed to verify the transform math, only a file existing at the
  expected FileDataID path, so tests write a synthetic 1x1 PNG into a
  scratch dir instead) surfaced a real, useful complication: mapping a
  `.skin` batch to its texture's FileDataID and resolved transform index
  needed a byte offset in `.skin`/M2 husk doesn't expose via any existing
  CLI surface, so a one-off scratch Python script (not checked in) did the
  same fixed-offset decode `find_texture_transform_files.py` already does,
  extended to also resolve `textureComboIndex` -> `TXID` FileDataID.

  Implementation: `gltf_mesh.cpp`'s new `textureTransformToKhr` (an
  anonymous-namespace helper next to `emitMaterial`) implements the closed
  form, gated on (a) the quaternion being planar -- `|x|,|y| <
  1e-4` -- since `KHR_texture_transform`'s rotation is a single scalar
  (Z-axis) angle and a genuine 3-axis rotation (never seen in real corpus
  data so far) has no honest equivalent, and (b) a real
  `baseColorTexture` existing to attach the extension to. Wired into
  `emitMaterial` right after `baseColorTexture.index` is set, only when
  `mat.textureTransform->constant` is true. `usedTextureTransformExtension`
  threaded through `emitMeshNode`/`gltf.cpp` the same way
  `usedUnlitExtension` already was, for the document-level
  `extensionsUsed` entry. The raw resolved values stay attached as
  `texture_transform` extras unconditionally, same as before this
  session -- additive, not a replacement (diagnostic, and the animated
  case's only representation).

  A real complication found mid-implementation, not anticipated by the
  plan: exporting `brewfestmount.m2` with a real texture for its
  transform-index-0 batch produced `constant: false` in the extras output,
  even though `find_texture_transform_files.py`'s cruder single-keyframe
  check called it constant. Root cause: husk's own
  `m2_animation.cpp`'s `trackHasAnimatedData` distinguishes a genuinely
  empty track (`outer.count == 0`) from a structured-but-trivial one
  (`outer.count > 1`, i.e. real per-sequence data, even if every
  sequence's resolved value happens to be identity) -- `brewfestmount.m2`'s
  translation/scaling tracks are the latter, so husk correctly refuses to
  treat the whole record as constant, unlike the scanner's cruder
  "exactly one keyframe or nothing" check. Confirmed this is husk being
  more correct, not a bug: applying a single static UV transform when the
  real per-sequence data could (in principle, on a different real file)
  differ per animation would be a silent misrepresentation. Kept
  `brewfestmount.m2` as a fixture anyway -- a real, useful negative-case
  regression (`tests/test_integration.cpp`'s new
  "brewfestmount.m2's real texture-center-pivot rotation... stays
  extras-only" test) that a synthetic fixture wouldn't have caught without
  already knowing to write it.

  Verification: `bloodknightcharger.m2`'s real export (a synthetic 1x1 PNG
  standing in for its real texture, same as the committed tests) produced
  `KHR_texture_transform` `{offset: [1.0, 1.25], rotation: -pi, scale:
  [1.0, 1.5]}` -- an exact match to the hand-derived expectation (`-pi`
  and `pi` are the same rotation). `gltf_validator` raised zero issues
  tied to the new extension (a `bloodknightcharger.m2`-specific batch of
  `JOINTS_0`/`WEIGHTS_0` errors showed up, confirmed unrelated and
  pre-existing by checking that `bloodelffemale.m2`'s already-verified
  export still validates with 0 errors after this change -- not
  investigated further, out of scope). Headless Blender's own glTF
  importer (a one-off scratch script, not checked in) parsed the file and
  built a real Mapping node for that exact material with `location=(1.0,
  1.25)`, `rotation=-180 degrees`, `scale=(1.0, 1.5)` -- an exact,
  independent third-party match. A second material (transform index 1, a
  ~135-degree rotation, not one of the two hand-derived fixtures) produced
  *different*-looking Mapping-node values in Blender than the raw glTF
  JSON numbers -- expected, not a bug: Blender's own glTF importer
  recomposes `KHR_texture_transform` with its own V-flip convention
  (`V_blender = 1 - V_gltf`), which only happens to leave the Mapping
  node's numbers textually identical to the raw JSON when `sin(rotation)
  == 0` (true for the 180-degree case, not the ~135-degree one) -- not
  independently reproduced byte-for-byte for that second material, but the
  extension itself is spec-conformant per `gltf_validator` and built from
  a formula already verified two other ways.

  Six new tests: four synthetic (`tests/test_gltf_mesh.cpp` -- the real
  extension appears for a constant+planar+textured case with exact
  expected values, and is correctly absent for the animated, no-texture,
  and non-planar-rotation cases respectively) and two real-fixture
  integration tests (`tests/test_integration.cpp`, gated on the two new
  fixtures via `HUSK_TEST_TEXTURE_TRANSFORM_SCALE_M2`/`_ROTATION_M2` and
  test_data/ fallback, following `tests/test_data_paths.hpp`'s existing
  `resolve()` convention). Full suite green, 530/530
  (`./build/husk-tests`). `TODO/TODO_correctness.md`'s former item 4 removed
  outright per the file's own convention; `M2_COMPLETENESS.md`'s "Texture
  transform (constant case)" row updated from `native-possible,
  unverified` to `native — 100%`; `DESIGN.md`'s Key design decisions,
  `README.md`'s format-support matrix, `src/m2_animation.hpp`'s
  `TextureTransform` doc comment, and `cmd_export.cpp`'s own stdout note
  for this feature all updated to match (the note previously claimed the
  UV transform was "not applied to the render" unconditionally, no longer
  true for the constant case).

---

- **Last state**: Same session, immediate second pass widening the SQLite
  side-project note directly below: "it's not gonna be just flat tables
  only, it's gonna have mappings tables and stuff... the actual sqlite
  export is a side project to confirm correctness, and to have data
  available for other relevant targets not just the engine... I think it
  will become massively relevant when the world data implementation
  starts." Two real corrections to the just-written note, both in
  `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`/`TODO/TODO_correctness.md`: (1) the
  planned SQLite schema is real relational structure -- mapping/join
  tables for DB2 tables' actual foreign-key relationships (the same
  `ChrCustomizationOption` → `_Choice` → `_Material` chain Stage 3 already
  needs), not one flat table per `.db2` file with relationships discarded;
  (2) its purpose is wider than this one TODO -- a correctness cross-check
  for whatever WDC5 parser Stage 1 builds, *and* a general-purpose local
  data source for other consumers of this project's WoW-format work
  beyond `husk export` itself, called out as likely to matter a lot once
  WMO/ADT world-data implementation starts (`WORLD_COMPLETENESS.md` and
  its companion `*_TODO.md` files -- real placement/area/lighting data
  leans on DB2 tables at least as much as character customization does).
  Worth designing the schema with that wider audience in mind from the
  start. No code changed -- documentation only, both notes updated in
  place since neither had been committed yet.

- **Last state (prior, same session)**: Same session, two small closing items plus one scope
  correction, all prompted directly.
  - **`TRANSFORM_TRIAGE.md` closed out.** Its one deliberately-deferred
    item -- a real animated clip, visually confirmed by Luna in Blender's
    actual GUI viewport -- is done: "Animation looks OK." Asked earlier
    this session ("what's the status of transform_triage? if done,
    delete") and now genuinely done, so deleted, matching this project's
    own established "survey's job is done" lifecycle
    (`BLENDER_EXPORT_TODO.md` is the precedent -- already deleted in an
    earlier session, still cited by name in historical entries like this
    one without issue). The two source-code citations that were
    themselves already marked `// TODO: Remove: TRANSFORM_TRIAGE.md`
    (`src/gltf_math.hpp`'s `zUpToYUp` doc, `src/gltf_math.cpp`'s
    determinant `static_assert`) were cleaned up in the same pass, since
    deleting the file they cited is exactly the trigger those markers
    were waiting for -- substantive content (the formula, the invariant)
    kept, only the dev-trace-doc citation removed. Every other citation
    across `DESIGN.md`/`README.md`/`EYES_ON_FINDINGS.md`/
    `INLINE_COMMENT_RULES_VIOLATIONS.md` left untouched -- historical
    narrative citing a since-deleted file by name is this project's own
    established, accepted pattern, not a dangling reference to fix.
    `INLINE_COMMENT_RULES_VIOLATIONS.md` in particular already has its own
    much larger, separately-scoped cleanup pass planned for every
    `TRANSFORM_TRIAGE.md`/dev-trace-doc citation in `src/`/`tests/` --
    not executed here, out of scope for what was actually asked.
    One real, funny, genuinely non-actionable side note from the
    verification itself, worth preserving for the record: a dead vertex
    sits in the middle of the two-handed swing animation, detached from
    the character, FileDataID 31739 -- confirmed genuinely invisible in
    the real game too (an "invisible texture"), not a husk export bug.
  - **SQLite scope correction, `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`/
    `TODO/TODO_correctness.md`'s own new notes tightened**: the DB2→SQLite idea
    from an earlier conversation was written up as if it might be part of
    the real pipeline; corrected directly -- "that is mainly for debugging
    and investigation, the real pipeline is the same as with modern
    blp's -- read the file, transform in memory, write to gltf." Fixed in
    both docs: SQLite is a `husk dump-chunks`/`husk-blp`-shaped side tool
    for a human to inspect DB2 contents by hand, not something Stage 1's
    real WDC5 parser round-trips through at runtime -- that parser reads
    `.db2` bytes directly into memory and feeds the rest of the pipeline
    straight from that, same architecture as every other sidecar format
    this project already has. The nested-array open question from that
    earlier conversation stays relevant to the *investigation* tool
    specifically, not the real pipeline.

- **Last state (prior, same session)**: Same session, one more real correction on top of the
  terminology fix directly below: called Luna's local `casc-tool` export
  "trustworthy" in chat and once in this file (now fixed). Her own
  pushback: "I would not classify it as trustworthy, considering the
  amount of bugs the mere m2 pipeline has had... more in the scope of if
  it is wrong, i don't get to blame others." Real distinction, not
  pedantry -- "hers, so any bug in it is her own to own" is a statement
  about *accountability*, not a claim that the data is verified-correct.
  This project's own M2/`.skin`/`blp/` pipeline has a long, real history
  of exactly this kind of bug (`skin::Submesh::Level` misread, the
  duplicate-alternate-texture blowup, several corpus-scan findings) --
  nothing about "it's a local file, not a live CASC query" implies it's
  bug-free, and nothing here should be read as claiming that. The real,
  narrower point (still true, still the reason this is in scope) is just
  that a local `.db2` file is the same *tier of data* as every other
  sidecar husk already reads at the user's own direction -- not a claim
  about its correctness.

- **Last state (prior, same session)**: Same session, immediate terminology correction to the
  entry directly below (and to `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`/
  `EYES_ON_FINDINGS.md`/`CLAUDE.md`, all edited in place since none of
  this was committed yet): "please do not mix wow.expor (the untrustworthy
  tool) and wow_export my local export of wow files via my casc-tool."
  Real mistake, not a nitpick -- the DB2-availability writeup called
  `/media/luna/data/wow_export` (Luna's own local `casc-tool` export) "a
  wow.export directory" in several places, conflating it with
  `reference/wow.export` (the unrelated, explicitly untrustworthy
  third-party JS tool this project already treats as "flaky,
  non-authoritative, corroborating signal only, never a gate" --
  `TRANSFORM_TRIAGE.md`'s own framing from an earlier session). Every
  instance fixed to name the real source precisely: Luna's local
  `casc-tool` export, never "wow.export" unqualified. Same shape as the
  `LUNA_NOTES.md`/`LUNA_FINDINGS.md` mixup earlier this session --
  worth remembering as a pattern, not just fixing each instance in
  isolation: two same-session naming mixups now, both caught by Luna
  directly rather than by careful reading on this end.

- **Last state (prior, same session)**: Same session, direct follow-up to the DB2-mechanism
  finding directly below: "Do we have those db2 tables in the wow_export?
  as if casc exports them we can use them, all data in wow_export is free
  for all, to be used, the only hard boundary is not loading casc tool as
  a dependency." Checked directly: yes -- `/media/luna/data/wow_export/
  dbfilesclient/` has all three tables the previous entry named
  (`chrmodelmaterial.db2`, `charcomponenttexturesections.db2`,
  `chrmodeltexturelayer.db2`) as real local files, confirmed `WDC5`
  format by header (`WOWSTATIC_12_0_7_67808`), plus the entire
  `ChrCustomization*` choice-chain family needed to fully resolve *which*
  file goes in a slot for a specific character (`chrcustomizationoption`/
  `_choice`/`_material`/`_element`/... all present too). This is a real
  scope clarification, not just a factual answer -- `DESIGN.md`'s existing
  "never talk to CASC/DB2 directly" non-goal was written about live
  queries (its own text: "husk only reads what's already on disk"), and a
  `.db2` file Luna's own `casc-tool` already extracted to a local
  directory is exactly that, same tier as `.m2`/`.skin`/`--textures`
  files already are.
  Asked (`AskUserQuestion`) how far to take this given the real size of
  full support (a new WDC5 binary-format parser, a multi-table
  choice-chain resolution design, real pixel compositing, all before any
  Blender-side tooling) -- answer: "Write up findings and stuff into a
  new TODO, the plan is to get full compositing pipeline, and if possible
  build a blender shader node graph where user can just pick from the
  existing options of textures that fit that slot." No code changed this
  turn, by design -- documentation only. New `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`
  (matching this project's established `*_TODO.md` template --
  `TODO/BONE_NAME_DEDUCTION_TODO.md` used as the structural reference) lays out
  five stages: WDC5 parser, real placement geometry
  (`ChrModelMaterial`/`CharComponentTextureSection`), the customization-
  choice chain (`ChrModelTextureLayer` → `ChrCustomizationOption` →
  `_Choice` → `_Material`), real pixel compositing (blend-mode math per
  `CharMaterialRenderer.js:345-372`, not guessed at), and — the stretch
  goal Luna named directly — Blender-side picker tooling once real
  placement rects exist, the correctly-UV-positioned version of her very
  first ask this session ("1 texture as default and rest... as unlinked
  texture nodes"). `EYES_ON_FINDINGS.md`/`CLAUDE.md` both cross-reference
  the new TODO rather than duplicating its content.

- **Last state (prior, same session)**: Same session, direct follow-up with two real
  screenshots -- resolving the previous entry's own open question
  ("still not confirmed whether 3500121 is really what Luna meant") and
  correcting a real mischaracterization in the process. Screenshot 1:
  Blender's own image editor, `bloodelffemale_hd_skin_color_3500119`
  (left) next to `_3500123` (right, the real base atlas) with a red arrow
  pointing at the exact chest region `_3500119`'s content matches
  pixel-for-pixel. Screenshot 2: same shape, `_3500115` against a
  *different* region of `_3500123`. Both non-transparent, precisely
  aligned -- real overlay patches meant to composite onto one specific
  rectangular region of the base atlas, not "tiny decals" as the previous
  entry's own inspection had characterized them (accurate about what the
  images *show* -- a strap graphic -- wrong about what that *means*, junk
  vs. a deliberate region-keyed patch). Prompted directly: "worth
  investigating how this is mapped originally."
  - **Investigated the real mechanism, not guessed at**: searched
    `reference/wow.export` for the actual client compositing code.
    `src/js/3D/renderers/CharMaterialRenderer.js:114-118` names the exact
    three DB2 structures involved -- `ChrModelMaterial` (`TextureType`,
    `Width`, `Height`: the base atlas's own dimensions),
    `CharComponentTextureSection` (`SectionType`, `X`, `Y`, `Width`,
    `Height`, `OverlapSectionMask`: the literal placement rectangle a
    patch composites into), and `ChrModelTextureLayer` (`TextureType`,
    `Layer`, `BlendMode`, `TextureSectionTypeBitMask`: which section a
    given layer targets and how it blends) -- and
    `src/js/db/caches/DBCharacterCustomization.js:203-215` confirms these
    are read as real DB2 tables (`db2.ChrModelMaterial`,
    `db2.CharComponentTextureSections`, `db2.ChrModelTextureLayer`), not
    inferred from filenames or heuristics. This is real, named,
    documented DB2 data, and squarely the kind of CASC/DB2 access
    `DESIGN.md`'s Non-goals already rules out for husk, by design -- a
    confirmed, dead-end-with-a-name rather than an open question.
  - **What husk can still usefully do, implemented this session**:
    `AlternateTextureCandidate` (`src/gltf_mesh.hpp`) gained real
    `width`/`height` fields, populated by new `pngDimensions`
    (`src/export_materials.cpp`, replacing the narrower `pngPixelArea`
    the previous entry added -- same IHDR-chunk read, now returning both
    values instead of just their product) and emitted as
    `alternate_textures[].width`/`.height` extras
    (`src/gltf_mesh.cpp`). Not the real placement rectangle (husk has no
    `CharComponentTextureSections` data to source one from), but real,
    already-load-bearing data (the same numbers `orderCandidatesForDefault`'s
    own ranking already depends on) that saves a human from decoding each
    candidate by hand to tell a full atlas apart from a small patch --
    exactly the manual cross-referencing work that surfaced this whole
    finding in the first place. Verified on the real export: a real,
    visible size spread now sits directly in the glTF extras (71 entries
    at 512x512, 20 at 256x256, 13 at 1024x512, 10 at 128x128), filterable
    by a human or Blender script without `husk-blp`. Full suite green,
    524/524 -- no behavior change to prove via a disable/re-enable cycle
    this time (pure additive metadata, the ranking logic itself untouched).

- **Last state (prior, same session)**: Same session, fourth round on the ambiguous-texture
  thread -- prompted by a genuine question, not a bug report: "one thing i
  am having trouble locating manually in blender, is
  bloodelffemale_hd_skin_color_3500121... i am confused, where does that
  go." Luna's own description of the asset roles: `3500123` is "the
  'base' skin color that gets rendered under armors... the whole
  character + face + face jewelry" (a full atlas, matching this session's
  earlier `husk-blp` inspection exactly), while `3500121` is "just the
  body, with the underwear... used when the character is not wearing any
  armor, but it has completely different uv layout" -- a second, real,
  mutually-exclusive full-body atlas variant, not an overlay on the first.
  Investigating to answer the question surfaced a real bug beyond it:
  decoded directly (`husk-blp`), `bloodelffemale_hd`'s twelve
  `skin_color`-category files split into two starkly different size
  classes -- eight (`3500114`-`3500121`) are 256x128 small strap/
  underwear-decal graphics (one inspected: a tiny bra-strap detail on a
  mostly-transparent background), the other four (`3500122`-`3500125`)
  are the real 1024x512 full-body atlases, matched skin-tone color
  variants of one design. The previous entry's "prefer skin_color" default
  rule had no way to tell these apart -- it just picked whichever
  `skin_color` file sorted alphabetically first among *all twelve*, which
  landed on `3500114`, one of the tiny decals, not a real atlas.
  - **`src/export_materials.cpp`**: new `pngPixelArea` reads a candidate's
    real width x height straight out of its own PNG IHDR chunk (bytes
    16..23, big-endian) -- no extra decode pass needed, since
    `readTextureFileBytes` already hands back real PNG bytes for both a
    `.png` source and a decoded `.blp` alike. `orderCandidatesForDefault`
    (previously a pure category-preference sort) now ranks candidates by
    this first, largest pixel area wins, falling back to the `skin_color`
    category preference only as a tiebreak *among same-area candidates* --
    needed because `body_jewelry_3602029` (itself a correct, real
    candidate for this slot per the entry above) happens to also be
    1024x512, tying with the real atlas on size alone, and Luna's own
    explanation of the asset roles (skin_color is the thing meant to stand
    alone; body_jewelry/bracelets/face are overlays layered on top of it)
    is the real, direct evidence for keeping that tiebreak rather than
    leaving it to chance.
  - **A real performance regression caught before shipping, not after**:
    the first working version of the pixel-area check read every
    candidate's bytes into a cache scoped *inside* `orderCandidatesForDefault`
    itself -- correct in isolation, but called once per ambiguous batch,
    so a real export with ~27 batches sharing this one candidate pool
    re-decoded the same ~60 `.blp` files from scratch on every single
    call, the identical "1786 redundant decodes" shape this project
    already found and fixed once before with a different feature (finding
    #6). Caught directly: a verification export against the real
    `bloodelffemale_hd.m2` + its real texture directory blew past the
    120s command timeout instead of finishing in its usual ~5s. Fixed by
    threading `buildMaterialsAndPrimitives`'s own `ambiguousCandidateCache`
    (already shared across every ambiguous batch for the real embed step)
    into `orderCandidatesForDefault` instead of a fresh per-call cache --
    same real export back down to ~4.6s.
  - **Two new regression tests**, each proven to fail with its own signal
    temporarily disabled (`return false` in place of the category
    tiebreak; `if (false && areaA != areaB)` in place of the area
    comparison) before being confirmed passing. Needed a real fixture that
    didn't exist yet: every prior test's embedded-image fixture was one
    fixed 1x1 PNG literal, useless for testing size-based ranking. New
    `solidColorPng(width, height, r, g, b)` (`tests/test_cli_fixtures.hpp`)
    builds a real, valid, stb_image/tinygltf-decodable RGBA PNG of any
    size without a zlib dependency -- its `IDAT` stream uses uncompressed
    ("stored") deflate blocks, a real spec-legal deflate encoding (RFC
    1951 §3.2.4) every decoder tested accepts identically to a compressed
    one, plus a from-scratch CRC32 (standard table-based algorithm) for
    each PNG chunk's trailer.
  - **Verified on the real export**: the `skin` material's default image
    changed from `bloodelffemale_hd_skin_color_3500114` (the tiny decal)
    to `bloodelffemale_hd_skin_color_3500122` (a real 1024x512 atlas).
    Full suite green, 524/524.
  - **The question that started this is still not fully answered**:
    whether `3500121` specifically is the file Luna meant is unconfirmed
    -- her own description reads as a full-body-scale asset, but the file
    at that exact name decodes to a small strap/underwear-decal graphic.
    Flagged back to her directly (`EYES_ON_FINDINGS.md`'s newest
    addendum) rather than silently assumed reconciled.

- **Last state (prior, same session)**: Same session, immediate refinement of the entry directly
  below's own fix — told directly, right after: "the [body_jewelry] and
  the bracelets are overlays to be overlayed on top of the skin texture
  files... they are textural options layered on top of the skin, not
  actual meshes, while the jewlery color are colorings for an actual 3d
  mesh jewlery object instead of just image texture layers on top of
  skin, which is why the jewlery color ones have their own UV map."
  Excluding `body_jewelry`/`bracelets` from type 20 (below) was correct,
  but leaving them unclassified was incomplete -- they have a real home:
  types 1/8 (skin/skin_extra), the same compositing family as `skin_color`/
  `face`, not a mesh-specific slot like `jewelry_color`.
  `candidateCategoryTypes` (`src/export_materials.cpp`) now maps
  `body_jewelry`/`bracelets` to `{1, 8}` explicitly, with a doc comment
  recording *why* (overlay-on-skin-texture vs. separate-3D-mesh-with-its-
  own-UV-map is the real distinguishing fact, not just "which type number
  happened to be wrong"). Verified on the real export: the `skin`-type
  material's candidate pool includes `body_jewelry`/`bracelets` again as
  real compositable overlay candidates (not lost), while `char_jewelry`
  still sees only its own two `jewelry_color` files, unchanged from the
  entry below. Existing regression tests (both this entry's and the one
  below) still pass unmodified -- the fix landed entirely inside
  `candidateCategoryTypes`' own data, no test assumed *where* these two
  tokens mapped, only that they didn't map to type 20. Full suite green,
  523/523.

- **Last state (prior, same session)**: Same session, immediate correction to the entry directly
  below — the "read `LUNA_NOTES.md`" instruction was a misnamed pointer to
  a *different* file, told directly after reporting back that
  `LUNA_NOTES.md` had no new content: "You should have asked me when it
  didn't have findings, i could have pointed out that i fucked up the
  naming, it's `LUNA_FINDINGS.md`." Fair correction, noted for next time
  (a file that's supposed to have new findings but doesn't is exactly the
  kind of surprising-enough-to-ask-about case, not one to just report and
  move past). `LUNA_FINDINGS.md` independently confirmed the material-
  dedup fix and the `char_hair`/`eyereflect` bug below by name/example,
  and added one real fact this session hadn't found on its own: real
  Blender verification against `bloodelffemale_hd`'s one `char_jewelry`
  material found only `jewelry_color_3613861`/`_3613862` are actually
  correct, not `body_jewelry_3602029` -- `candidateCategoryTypes`
  (`src/export_materials.cpp`) had mapped `body_jewelry`/`bracelets` to
  type 20 alongside `jewelry_color` on an unverified English-name
  assumption ("jewelry" sounds like it belongs with "jewelry_color").
  Viewed directly (`husk-blp`): `jewelry_color`'s two files are a real
  matched gold/silver color-variant pair of one collar-and-gem design,
  while `body_jewelry_3602029` is a visually distinct necklace-chain item
  -- not a color variant of the same design, no confirmed type-20
  evidence for it at all. Fixed by removing both from
  `candidateCategoryTypes` entirely rather than reassigning them to a
  type with equally no evidence -- they now fall to the unrecognized
  tier and get excluded from `char_jewelry`'s candidates by the existing
  "prefer recognized" rule, no new logic needed. Verified on the real
  export: `char_jewelry`'s `alternate_textures` now lists exactly the two
  `jewelry_color` files, matching `LUNA_FINDINGS.md` exactly. New
  regression test (`tests/test_cli.cpp`), proven to fail without the fix
  before being confirmed passing. Full suite green, 523/523.

- **Last state (prior, same session)**: Same session, third round on the ambiguous-texture
  thread — asked to investigate `LUNA_NOTES.md` for "concrete Blender
  matching information" first: checked directly, that file's own git diff
  showed nothing added beyond its existing 01.08.2026 notes (already
  superseded, `EYES_ON_FINDINGS.md`'s own intro says so) — reported this
  back plainly rather than fabricating findings from a file that didn't
  have new content. The real new information arrived as a reference
  screenshot (correctly-matched tan skin / blue hair / silver jewelry-
  bracelet close-up) plus a direct, concrete complaint: "we REALLY need to
  get ridd of the 500 materials produced by batches, so that there is only
  1 material per mat<num>_tex<num>_<id> combination and if other batches
  find a existing material TO USE THAT ONE not create a new one," and a
  report of repeated `bloodelffemale_hd_body_jewelry_3602029.<N>`-
  suffixed duplicate images in Blender.
  - **Material dedup (`src/export_materials.cpp`)**: `materialDedupKey`
    serializes every field of a fully-built `gltf::Material` that isn't
    purely batch-numbering -- deliberately including per-batch animation
    curves (`tintAnimation`/`alphaFadeAnimation`/`weightFadeAnimation`),
    since M2Color/M2TextureWeight combo indices are batch-level, not
    material-level, so two batches sharing (materialIndex, textureIndex)
    can still legitimately carry different tint/fade animation and must
    not be silently merged. `materialByKey` (content signature -> stored
    material index) reuses an existing entry instead of pushing a new one;
    the stored material's own name has its `batch<N>_` prefix stripped
    once dedup decides to keep it, so the surviving name is
    `mat<M>_tex<T>_<id>`, exactly what was asked for. Verified on the real
    `bloodelffemale_hd.m2` export: 114 materials -> 10.
  - **Primary-image cross-material cache (`src/gltf_mesh.cpp`)**: the
    duplicate-suffixed-image report turned out to be *mostly* the same
    root cause as the material-count bug (dedup alone fixed most of it),
    but one real, separate case remained even after dedup: two
    genuinely *different* materials (different `textureType`) can still
    resolve to the identical file when both fall back to the same
    unrecognized-category wildcard candidate. The primary
    `baseColorImagePng` embed now shares the same `alternateTextureCache`
    (filename -> texture index) the `alternate_textures` candidates
    already used, so this case shares one glTF image too. Verified via
    headless Blender: 0 `.NNN`-suffixed images left, down from 1 residual
    case with dedup alone.
  - **Real correction to the prior session's own default-picking logic** --
    caught by actually decoding and looking at the candidate images
    (`husk-blp`), not just their names/sizes. The bare
    `bloodelffemale_hd_3255415.blp` file that kept winning the `skin`
    slot's default (first via plain alphabetical sort, then via the
    immediately-prior session's own explicit "prefer bare over face" rule)
    turned out, viewed directly, to be a tiny mostly-transparent
    sparkle/glint icon -- nothing like a skin texture. The real full-body
    skin atlas (1024x512, torso/ears/face combined) was sitting the whole
    time under the *recognized* `skin_color` category. Same shape for
    `char_hair`: the unrecognized `eyereflect.blp` (a 128x128 pure-white
    eye-reflection sprite) was winning purely because `"eyereflect" <
    "hair_color"` alphabetically, over the real `hair_color` hair-strand
    textures. Root cause: `candidateAllowedForType`'s bare-file handling
    was *guessing* what an unlabeled file is (assumed "the base skin
    layer") rather than just correctly excluding what a labeled file
    isn't -- the exact kind of guess this project's own "filtering is
    safer than picking" principle was supposed to avoid, made anyway, now
    disproven by direct evidence. Fixed: `filterCandidatesForType`
    (replaces the old single-pass `candidateAllowedForType` filtering)
    always prefers recognized-and-compatible candidates over bare/
    unrecognized ones, falling back to unlabeled files only when nothing
    recognized exists at all for that slot -- the one case they're still
    genuinely needed (a non-character model with no category vocabulary
    in its texture directory at all). `orderCandidatesForDefault`
    (renamed from `preferBaseLayerCandidate`) keeps exactly one remaining
    preference, now evidence-backed rather than assumed: within the
    recognized set, `skin_color` (confirmed a real full-body atlas) ranks
    above `face` (confirmed real but narrower/darker) for the two
    compositing types specifically. Verified on the real
    `bloodelffemale_hd.m2` export: the `skin` slot's default average color
    went from (0.00, 0.00, 0.00) (the transparent sparkle icon) to (0.44,
    0.27, 0.15) -- a real tan skin tone, matching the reference screenshot
    -- and `char_hair`'s default changed from a pure-white sprite to a
    real hair-strand texture.
  - **Two new regression tests** (`tests/test_cli.cpp`,
    `twoBatchesSameComboSkin` added to `test_cli_fixtures_scenes.hpp`),
    both proven to actually fail without their respective fix before being
    confirmed passing -- the dedup test by temporarily hard-disabling the
    `materialByKey` lookup (2 materials instead of the expected 1), the
    bare-vs-category test by temporarily merging the recognized/fallback
    tiers back together (the bare file leaked back into
    `alternate_textures`). Full suite green throughout, 522/522
    (`./build/husk-tests`).
  - **Genuinely still open, found while investigating, not fixed**:
    `bloodelffemale_hd.m2`'s three real (`textureType == 0`)
    FileDataID-based slots (`3536810`/`4530998`/`5210137`) have no local
    file at all in the real export directory used this session, under
    either their exact FileDataID name or the model's own basename
    convention -- they fall back to the same ambiguous pool as the
    hardcoded slots and land on the fallback tier's pick for a different
    reason than before (genuinely missing local data, not a resolution
    bug). Whether that's specific to this local export or a real, wider
    gap is unconfirmed -- flagged for whoever picks this up next, not
    guessed at. See `EYES_ON_FINDINGS.md`'s newest addendum for the full
    detail.

- **Last state (prior, same session)**: reported
  from Blender after inspecting the fix's own output: "we are still
  getting in blender 'image_<number>' texture names instead of the
  actually useful bloodelffemale_hd_hair_color_5196731 that we get from
  the blp". Root cause: none of `gltf_mesh.cpp`'s three image-embedding
  call sites (`emitMaterial`'s primary `baseColorImagePng`,
  `additionalTextureLayers`, `alternateTextureCandidates`) ever set
  `tinygltf::Image::name`/`Texture::name` — Blender's glTF importer
  auto-generates `Image_<N>` for any unnamed image, which is exactly
  what was showing up.
  - **`src/gltf_mesh.hpp`**: new `Material::baseColorImageName` field —
    the real source filename (no extension) that supplied
    `baseColorImagePng`, purely cosmetic (doesn't affect which texture
    `baseColorTexture.index` points at).
  - **`src/export_materials.cpp`**: populated at all four
    `gm.baseColorImagePng = ...` sites — the M2-embedded-filename match
    (`embeddedStem`), the FileDataID-exact match (`std::to_string(fdid)`),
    the sole fuzzy match (`fuzzy->stem()`), and the chosen candidate out
    of an ambiguous pool (`chosen.filename`'s stem).
  - **`src/gltf_mesh.cpp`**: all three embedding sites now set
    `img.name`/`tex.name` — the primary texture from
    `mat.baseColorImageName`, `additionalTextureLayers` from
    `layer.fileDataId` (no filename tracked there, FileDataID is still
    better than nothing), and `alternateTextureCandidates` from each
    candidate's own `cand.filename` stem (needed `#include <filesystem>`,
    not previously included in this file).
  - **Verified two ways**: a new unit assertion
    (`tests/test_gltf_mesh.cpp`, the existing "baseColorImagePng is
    embedded" test plus the `additionalTextureLayers` test) checking
    `model.images[...].name`/`model.textures[...].name` directly, and a
    real headless-Blender import of the actual fixed
    `bloodelffemale_hd.m2` export (`bpy.ops.import_scene.gltf` +
    `bpy.data.images`) — 99 images, every one previously `Image_0`..
    `Image_98`, all now real names (`bloodelffemale_hd_3255415`,
    `..._eye_color_3608322`, etc.), 0 generic names left. Also added a
    same-check assertion to the two-hardcoded-slots CLI test from the
    entry below (each `alternate_textures` candidate's own embedded
    image now asserted named after its own filename stem, not just
    listed in the extras). Full suite green throughout, 520/520 (no new
    test cases, existing ones gained assertions).

- **Last state (prior, same session)**: Fixed `EYES_ON_FINDINGS.md`'s ambiguous-texture
  cross-contamination gap (finding #3's later addendum + finding #6's own
  "not yet fixed" follow-up), asked for directly with a concrete example:
  "we need to be able to map that a face (ex
  `bloodelffemale_hd_face_3500113.blp`) does *not* map to shoes mesh
  (mesh material name
  `batch36_mat5_tex2_skin_bloodelffemale_hd_3255415`)". Investigated
  before writing any code: `husk info` against the real
  `bloodelffemale_hd.m2` showed only *one* `M2Texture::type == 1` (skin)
  slot exists in the whole model — face and shoes triangles share the
  exact same M2 texture slot, sampling different UV regions of what the
  real WoW client composites at runtime from several separate layers
  (base skin tone + face + others). Traced through
  `reference/wow.export/src/js/modules/tab_characters.js` (per Luna's own
  pointer to use it as a non-authoritative reference) to confirm this:
  `apply_skinned_model_textures` explicitly composites types 1
  (`SKIN_TEXTURE_TYPE`) and 8 (`SKIN_EXTRA_TEXTURE_TYPE`) from multiple
  blended layers, binding every *other* replaceable type (hair, eyes,
  jewelry, blindfold) to one single raw file instead — and its own
  `option_map`/comments name the exact category vocabulary
  (`skin`/`face`/`hair color`/`hair style`/`facial`, `"blindfold = type
  9"`) that a real CASC-export directory's own filenames already carry
  (confirmed directly against `/media/luna/data/wow_export`, a full real
  export Luna pointed at mid-session: files like
  `bloodelffemale_hd_skin_color_3500123.blp`,
  `..._jewelry_color_3613861.blp`, `..._eye_color_3608330.blp`,
  `..._blindfold_7758264.blp` all follow this exact pattern). This
  reframed the task: husk can never *pick* the one correct composited
  layer (no DB2 blend-order data, by design, `DESIGN.md`'s Non-goals) but
  it *can* stop offering a hair-color file to an eyes slot, or a jewelry
  file to a skin slot, since those exclusions are grounded in real,
  already-parsed M2 data (`M2Texture::type`) plus real filename metadata,
  not a guess.
  - **`src/export_materials.cpp`**: new `classifyCandidateCategory`
    (parses the real category token out of a candidate's filename, empty
    for a bare `<model>_<FileDataID>` file), `candidateCategoryTypes` (the
    token → compatible-`M2Texture::type` map, transcribed from
    `tab_characters.js` as above), `candidateAllowedForType` (the actual
    per-slot filter — an unrecognized token stays a wildcard, unchanged
    old behavior for non-character models), `poolHasRecognizedCategory` +
    `bareMeansSkinOnly` (a bare file only gets restricted to skin/
    skin_extra when the model's own pool proves it's a real character-
    customization directory, so a simple equipment model's one plainly-
    named texture still resolves as before), and
    `preferBaseLayerCandidate` (within the two compositing types, orders
    a bare/`skin_color` file ahead of a narrower `face` overlay when
    picking the wired default — a full-body base tone is a far more
    plausible stand-in than a small face-only overlay, even though
    neither is the real composited answer). `claimSoleFuzzyTextureCandidate`
    now type-scopes its "exactly one candidate" check too, not just the
    ambiguous (2+) branch.
  - **`src/gltf_mesh.hpp`/`gltf_mesh.cpp`**: `AlternateTextureCandidate`
    gained a `category` field, populated from the same classification and
    emitted as `alternate_textures[].category` in the glTF extras — so a
    human or Blender script browsing the unlinked candidates can see what
    each one actually is (Luna's own "1 texture as default and rest ...
    as unlinked texture nodes" framing needs exactly this kind of label
    to be useful; the actual node-graph construction is Blender-side
    tooling this repo doesn't have yet, out of scope for `husk export`
    itself).
  - **Verified against real data**, not just synthetic fixtures: built
    husk, ran `husk export` against the real
    `/media/luna/data/wow_export` `bloodelffemale_hd.m2` + its real
    texture directory before and after. Before: every ambiguous
    material's default collapsed to the same one file regardless of
    slot type (the finding #6 bug), and slots got offered wildly
    unrelated categories (a `char_jewelry` slot's candidate list
    included face/hair/blindfold files). After: `skin` slot's pool 94 →
    57 (skin/skin_extra-only), `char_eyes` → its own 9 `eye_color_*`
    files, `char_jewelry` → its own 19 `jewelry_color`/`body_jewelry`/
    `bracelets` files, `ui_skin`(blindfold) → its own 2 `blindfold_*`
    files — zero cross-category leakage in any of them, confirmed by
    grepping the real export's own diagnostic warnings. Full suite green
    throughout, 519 → 520 (`./build/husk-tests`).
  - **New regression test, proven to actually catch the bug**:
    `tests/test_cli.cpp` (`twoHardcodedTexturedModel` fixture added to
    `test_cli_fixtures_scenes.hpp`) builds a synthetic model with two
    hardcoded slots of genuinely different `M2Texture::type`s (skin=1,
    char_jewelry=20) sharing one candidate pool, and asserts each
    material's `alternate_textures` only ever contains its own type's
    candidates. Before trusting it, temporarily hard-disabled
    `candidateAllowedForType` (`return true` first line) and reran just
    this test: failed exactly as predicted (`alt.ArrayLen() == 4` instead
    of `2` on both materials, real cross-contamination reproduced), then
    restored the real filter and reconfirmed green — the same "prove a
    regression test actually regresses" discipline this project's history
    already uses elsewhere.
  - **What's still genuinely open, not fixed and not fixable without more
    data**: within the skin/skin_extra compositing types specifically,
    *which* `skin_color` file and *whether* `face` should be layered in
    for a given character's actual customization choices remains
    unknowable without real `ChrModelTextureLayer`/DB2 data — this
    session narrows "which candidates are even offered" to a
    structurally-grounded set, it does not and cannot produce the one
    correct composited look. Documented as still-open directly in
    `EYES_ON_FINDINGS.md`'s updated finding #3/#6, not left implicit.

- **Last state (prior session)**: Fixed the "upside down" M2→glTF export bug — real code,
  tested, shipped this session, not the reverted one-line patch a prior
  session left off at. Requested directly, after that prior session's
  `BLENDER_EXPORT_TODO.md` §8 finding: not a quick patch, but "a more
  robust system that can test the correctness of the mesh regardless of
  the rotation... if the code has a plethora of hardcoded signals, that is
  prone to break the instant we get a model in an unexpected
  orientation... research and explore how to fix this permanently, so if
  Blizzard changes what their models' up means, it will not be this
  rework again." Wrote `TRANSFORM_TRIAGE.md` first (a full root-cause /
  process-failure / durable-fix investigation, no code touched) — Luna
  pushed back hard on two parts of the first draft before anything was
  built, both real corrections: (1) `reference/wow.export` was drafted as
  a "standing cross-validation check" — corrected to "flaky,
  non-authoritative, corroborating signal only, never a gate," per her own
  "don't assume wow.export is correct... it works *somewhat*"; (2) the
  proposed semantic ground-truth check ("head bone above foot bone") was
  drafted as the primary orientation invariant — corrected after her
  direct "weapons? Other meshes with skeletons? ... weapon orientations
  are not necessarily up as the correct axis" into an asset-agnostic
  synthetic coordinate-frame probe (a fabricated skeleton, not a real
  model, tested for round-trip-identity through a real headless-Blender
  import) as the primary check, with the humanoid-landmark idea demoted to
  an explicitly optional, non-load-bearing secondary signal. Both
  corrections are recorded inline in `TRANSFORM_TRIAGE.md` itself, not
  just in this log.
  - Luna then answered all four of the document's own open questions
    directly and gave explicit go-ahead to implement autonomously: "yes,
    you build while i nap" (tests before the formula fix, per the
    document's own recommended sequencing); "part of this" (fold the
    single-matrix refactor into the same change, don't scope it
    separately); "worth adding, but not critical" (the humanoid-landmark
    secondary check); "no preferences, any will do" (a quadruped fixture).
    Closed with an explicit scope boundary for what she'd verify herself:
    "start implementing, and after all of it is tested and implemented i
    will verify... until then you'll have to rely on headless Blender" —
    everything below was built and verified exactly within that boundary,
    nothing claimed beyond it.
  - **`src/gltf.hpp`/`gltf.cpp`**: the historical three independently
    hand-typed conversion functions (`zUpToYUp`, and `cmd_export.cpp`'s
    separate `toGltf(m2::Quat)`/`toGltfScale`) are now one mechanically-
    derived system — a private `Mat3` plus one matrix (`kWowToGltf`,
    corrected from `(x,-z,y)` to `(x,z,-y)`), with `zUpToYUp`/
    `rotationZUpToYUp`/`scaleZUpToYUp` all derived from it (position/
    normal: direct application; rotation: quaternion → matrix → conjugate
    by the matrix → quaternion; scale: the matrix's permutation, signs
    dropped). A `static_assert` on the matrix's determinant enforces "must
    be a proper rotation" at compile time. `cmd_export.cpp`'s own
    `toGltf(m2::Quat)`/`toGltfScale` are now thin wrappers, not separate
    formulas — the exact fix for the root cause `TRANSFORM_TRIAGE.md`
    traced this bug to (rotation/scale were hand-derived *from* the old,
    wrong position formula, on paper, then never independently
    re-verified against anything real).
  - **The corrected formula is now corroborated three independent ways**,
    not just the prior session's single headless-Blender empirical test:
    the hand-derived change-of-basis math (already existing), the
    headless-Blender round-trip (already existing, re-confirmed), and —
    new this session — `reference/wow.export` (already checked out in this
    repo, never previously mined for this), which has its own,
    independently-written coordinate-conversion code for position,
    normal, rotation, *and* scale, matching the corrected formula exactly
    on every one (scale needed zero code changes — it was already correct,
    informative about the bug's own shape: a sign error, not a wrong axis
    pairing, since scale is sign-insensitive).
  - **New tests, and each proven to actually catch the bug, not just
    proven to pass** — the same "prove a regression test actually
    regresses" discipline this project's history already uses elsewhere:
    a synthetic, asset-agnostic coordinate-frame probe
    (`tests/test_conformance.cpp`, a fabricated `gltf::Skeleton` with no
    dependency on any real M2 file) asserts local X/Y/Z offsets survive a
    real husk-export → Blender-import round trip as the *identical*
    coordinate — before trusting it, `kWowToGltf` was temporarily reverted
    to the historical formula and rerun: it failed exactly as the
    root-cause math predicts (`+X`, the rotation's own invariant axis,
    still correct; `+Y`/`+Z` both flipped), then the fix was restored and
    reverified green. A property-based unit test
    (`tests/test_gltf.cpp`) independently confirms `rotationZUpToYUp`'s
    own matrix-conjugation implementation is self-consistent for several
    real test rotations and probe vectors, regardless of which underlying
    matrix is used — catches a bug in the conversion *machinery*, not in
    which matrix is chosen. (One real false alarm this same test caught in
    itself, before either mattered: a hand-typed "arbitrary rotation" test
    quaternion wasn't quite unit-length, silently violating
    `quatToMat3`'s implicit unit-quaternion assumption and producing a
    small, confusing failure that looked like a real bug — fixed by
    normalizing every test quaternion at test time rather than trusting a
    literal's precision.) A second, explicitly non-load-bearing check
    confirms a real humanoid landmark bone (`_Name`, keyBoneId 22) lands
    above the armature origin on the real `bloodelffemale.m2` fixture —
    caught one more real bug in its own first draft before shipping:
    Blender is natively Z-up, not Y-up, so the check's first version
    compared the wrong raw component (`.y` instead of `.z`) and would have
    silently asserted the wrong thing; caught because the real fixture's
    own landmark prints as `(0, 0, 2.05)`, obviously wrong against a
    `.y > 0` check and obviously right against `.z > 0`.
  - **A real quadruped fixture** (`test_data/creature/wolf/wolf.m2`,
    gitignored, same personal-extraction convention as every other
    `test_data/` fixture — 66 bones, 557 vertices, pulled from the local
    corpus per Luna's own "no preferences, any will do") plus
    `HUSK_TEST_QUADRUPED_M2`/`_SKIN` wiring
    (`tests/test_data_paths.hpp`, `test_main.cpp`'s banner) and two new
    `test_conformance.cpp` cases (gltf_validator zero-errors,
    headless-Blender bone/vertex-count agreement) — explicitly *not*
    additional orientation coverage (the synthetic probe already covers
    any asset type by construction), but real pipeline coverage for a
    body-plan/bone-hierarchy shape `bloodelffemale.m2` doesn't represent.
  - **Full suite green with zero hand-updated literals**: every existing
    test touching a position/rotation/scale value passed unmodified
    against the corrected formula the moment it was flipped — 335 →
    484/484 (+1 permanently-inapplicable skip) via `./build/husk-tests`,
    485/485 via `ctest`. Nothing needed updating, which is itself a real
    signal: no other test in this codebase was silently depending on the
    old formula's specific wrong values.
  - **Docs**: `TRANSFORM_TRIAGE.md` itself updated throughout with
    "Implemented" notes per subsection (not deleted — unlike this
    project's usual fully-closed-TODO lifecycle, one real item is
    deliberately still open, see below, so the file stays as the living
    record for it). `DESIGN.md` (the rotation/scale Key design decision
    corrected to describe the current mechanically-derived implementation
    rather than the stale hand-derived-formula description; a new,
    detailed Follow-up entry after the original upside-down finding).
    `README.md` (the roadmap stage 1 paragraph's literal formula citation
    corrected — it still quoted the wrong, pre-fix formula as current
    fact; the Testing section's Conformance-tier paragraph extended with
    the new probe/landmark/quadruped coverage).
  - **What's deliberately still open, not an oversight**: a real animated
    clip, visually confirmed by Luna in Blender's actual GUI viewport —
    every check this session added is numeric (headless probes, a
    property test, a JS/C++ diff); nothing here substitutes for that last
    look, and it was never meant to be automated away. `TRANSFORM_TRIAGE.md`
    §7/§8 both say this explicitly. Whoever picks this up next — likely
    Luna herself — should start there.
- **Previous state**: Implemented all four items in `RO_COMPLETENESS_TODO.md`
  (a punch list Luna wrote grounding four of README's own 🚧-marked
  format-matrix rows against current source, then handed off with "shouldn't
  be a big task") — every item done, tested, documented, and the TODO file
  itself deleted per this project's own "survey's job is done" lifecycle.
  Worked in the file's own priority order.
  - **Item 2 (header metadata)**: `global_flags` now decodes into every
    wiki-named bit (`m2::globalFlagNames`, `src/m2.hpp`/`m2.cpp`'s new
    `GlobalFlag` namespace — bit positions derived by counting the wiki's
    own reserved `uint32_t : 1` slots, not guessed from hex comments),
    printed by `husk info` alongside the existing raw hex. Two real-file
    cross-checks the item's own plan asked for, both confirmed: (1)
    `flag_load_phys_data` correctly tracks real `.phys` presence —
    set on `mace_1h_warfrontsforsaken_d_01.m2` (has a committed `.phys`
    sidecar), unset on `bloodelffemale.m2` (doesn't); (2) whether
    `flag_new_particle_record` is a reliable proxy for the 492-byte
    `M2Particle` shape, or whether `kMinVerifiedParticleVersion`'s
    version-only gate could disagree with it on a real file — it can:
    `mace_2h_bolvar_d_01.m2` (version 274, the 64-particle-emitter stress
    fixture) does *not* set the flag, confirming the wiki's own text is an
    OR ("if 0x200 is set **or** if version is bigger than 271") and
    `kMinVerifiedParticleVersion`'s existing version-only check was already
    the correct half of that OR, not a bug. `Header::textureCombinerCombos`
    (the header struct's own last field, `M2Array<uint16_t>` at offset
    0x130, only present when its flag bit is set) is now parsed and
    surfaced via `husk info` too — a full 130,576-file local-corpus scan
    found zero real files with the flag set, so this one specific table's
    real-file layout is unverified even though the parse itself is
    low-risk (same well-tested `parseUint16Array` five other lookup tables
    already share). The wiki's own "use this instead of index+1 for
    multitexture blending" cross-reference into `cmd_export.cpp`'s
    material resolution was deliberately **not** wired up — no indexing
    key documented at all, and (per the scan) no real file to verify a
    guess against either.
  - **Item 3 (`WFV1`/`WFV2`/`DPIV`/`AFRA`)**: all four — no wowdev.wiki
    struct at all — now get real structural parsing in `husk dump-chunks`
    (`dumpWfv1`/`dumpWfv2`/`dumpDpiv`/`dumpAfra`, `src/cmd_dump.cpp`) built
    from the real corpus files already sitting in this repo's root
    (`*_files_for_exploration.txt`, from an earlier session's corrected
    scanner-bug finding, `WIKI_FINDINGS.md` §10). `AFRA`/`WFV1`: a single
    fixed 16-byte struct (one real float32 + 12 zero bytes). `DPIV`: the
    wiki's own "always 32 bytes" undersold it — chunk size is *always* an
    exact multiple of 32 (1-4 records seen across 2,632 real hits), a real
    record array (`chunk.size / 32` records, 8x float32 each), not a
    single fixed struct; the last 4 floats are zero in every one of 2,951
    real records decoded, kept as real fields rather than assumed
    reserved. `WFV1`/`WFV2` are a genuinely thin, 2-file,
    byte-identical-content sample (both the same Nazjatar-zone waterfall
    doodads) — flagged tentative rather than confidently typed field-by-
    field (two `WFV2` fields show signs of not really being floats — a
    plausible packed-RGBA-color byte pattern, and a small-integer-as-float
    denormal `DPIV`'s own field_3 also shows — exposed as plain floats
    rather than guessing a reinterpretation from so thin a sample).
    `kFallback`'s raw-hex-dump path (`dumpRawFallback`) was removed
    outright once nothing used it anymore, and its stale notes ("AFRA...
    not observed in any files yet") — already known-wrong since an earlier
    session's scanner-bug correction, just never updated in this specific
    file — went with it.
  - **Item 1 (`blp/` DXT3/JPEG)**: a real corpus scan, not the small
    open question the item's own plan expected to resolve cheaply — this
    ran **779,056** real `.blp` files (not `.m2`-scoped, the biggest and
    single longest-running corpus check this project has done, ~2h55m
    wall-clock, almost entirely disk I/O opening three-quarters of a
    million individual small files one at a time). Result: **DXT3 is real
    and needed** (6,759 real files — character hair/skin textures among
    them), **JPEG is genuinely absent** (0 real files, recorded as a real
    negative result per this project's own "checked, zero real files, not
    implemented blind" discipline, not attempted). The real surprise:
    DXT3 needed **no new decode code at all** — `blp/src/husk_blp/
    decode.py`'s `_decode_dxt`/`_DXT_BLOCK_SIZE`/`_DXT_FOURCC` were
    already generic over `PixelFormat.DXT1`/`DXT3`/`DXT5`, wired through
    the exact same synthetic-DDS-wrapper path DXT1/DXT5 use — it had
    simply never been exercised by a real test or verified against a real
    file, so `README.md`'s own "DXT3... unimplemented" claim was stale
    documentation, not a missing feature. Verified two ways before
    trusting that: a new synthetic single-block test
    (`test_decode_dxt3_solid_green_explicit_alpha_block`, `blp/tests/
    test_decode.py`, same shape as the existing DXT1/DXT5 single-block
    tests) round-trips exactly; a real file
    (`character/troll/hair00_01.blp`, 128×128) decodes to a visibly
    correct troll-hair texture (red strands + braid, 2,333 unique
    colors) — not a crash, not garbage.
  - **Item 4 (Sidecar FileDataID resolution)**: `README.md`'s format-
    matrix row bumped 🚧 → 📖 (the CASC-resolution half this row measures
    against is a deliberate non-goal, not a deferred read — local-file
    resolution, the row's actual full scope, is already complete for all
    six IDs). The one real diagnostics gap found: `resolveSkin`
    (`--skin auto`'s SFID-based resolution stage, `src/cmd_export.cpp`)
    used to report only the *directory* it searched on a "not found"
    failure, not the specific `<FileDataID>.skin` path it actually
    checked — a direct miss against this project's own Foreign Data
    policy ("on failure, always print expected and actual values"). Now
    names the exact candidate path; three existing `tests/test_cli.cpp`
    cases whose assertions depended on the old, vaguer wording were
    updated to check for the specific path instead. Checked the sibling
    resolvers the item's own plan named alongside it
    (`--anim`/`--bones-dir`/`--textures`) and found they don't share the
    gap: all three are deliberately silent-skip-per-item by design
    (matching `--textures`'s already-established "quiet when nothing
    applies" precedent), with no "not found" failure message to improve
    in the first place — the gap was real but narrower than the item's
    own framing suggested.
  - Also bumped 🚧 → 📖 for the "Chunk container / magic detection" and
    "Header / global metadata" format-matrix rows (Item 3's/Item 2's own
    work, respectively, directly closes the gap those symbols described).
  - **Verification discipline**: every claim above was checked against
    real bytes before being written down or shipped — the two real-file
    header-flag cross-checks, the 130,576-file `textureCombinerCombos`
    scan, the 779,056-file BLP scan, and the real troll-hair-texture
    decode all happened *before* the corresponding doc text or code
    change was finalized, not after. Full suite green throughout: 471/471
    `./build/husk-tests` (1 permanently-inapplicable skip), 472/472
    `ctest`, 17/17 `blp/`'s own pytest suite (3 pre-existing, unrelated
    env-var-gated skips).
  - **Docs**: `WIKI_FINDINGS.md` (§10 gained a "Follow-up: implemented"
    subsection for `WFV1`/`WFV2`/`DPIV`/`AFRA`; new §14 for the
    `global_flags`/`textureCombinerCombos`/BLP-scan/`resolveSkin`
    findings; "Where these live in husk" table extended two rows),
    `DESIGN.md` (three new Key design decisions bullets — `WFV1`/`WFV2`/
    `DPIV`/`AFRA` parsing, `global_flags`/`textureCombinerCombos`,
    `resolveSkin` diagnostics — plus a fourth for the DXT3 finding),
    `M2_COMPLETENESS.md` (new `WFV1`/`WFV2`/`DPIV`/`AFRA` row, Header row
    updated), `README.md` (three format-matrix symbol bumps, the `blp/`
    usage paragraph rewritten for DXT3, the BLP `Texture pixel data` row
    rewritten). `RO_COMPLETENESS_TODO.md` deleted outright, same lifecycle
    every prior fully-closed TODO file in this project has used — its five
    remaining code/test cross-references (`src/cmd_dump.cpp`,
    `tests/test_cli.cpp`, `tests/test_dump.cpp` x2,
    `blp/tests/test_decode.py`) were already phrased as `former Item N`
    historical citations before the deletion, so none needed rewriting
    (same "historical log entries aren't rewritten" precedent every prior
    TODO-file deletion here has used).
  - **A real, unrelated observation, not acted on**: partway through this
    session's long-running BLP scan, ten new untracked files appeared in
    the work dir that this session didn't create —
    `TODO/WORLD/ADT_LOD_TODO.md`/`TODO/WORLD/ADT_TERRAIN_TODO.md`/`TODO/WORLD/COLLISION_CULLING_TODO.md`/
    `TODO/ENGINE_TODO.md`/`TODO/WORLD/FOG_VOLUMES_TODO.md`/`TODO/WORLD/LIGHTING_TODO.md`/
    `TODO/WORLD/LIQUID_TODO.md`/`LUNA_NOTES.md`/`TODO/WORLD/WDT_TODO.md`/`TODO/WORLD/WMO_GEOMETRY_TODO.md`/
    `WORLD_COMPLETENESS.md`/`TODO/WORLD/WORLD_PLACEMENT_TODO.md` (plus a
    `README.md` intro-paragraph edit pointing at the new
    `WORLD_COMPLETENESS.md`) — evidently Luna's own concurrent work in a
    separate session, scaffolding a WMO/ADT/world-geometry expansion,
    landing while this session's background scan ran for several hours.
    Confirmed via `git status`/`git diff` that none of it conflicts with
    or was touched by this session's own edits (the one shared file,
    `README.md`, had her intro-paragraph addition and this session's
    format-matrix/`blp/`-paragraph edits land in disjoint sections,
    cleanly coexisting) — left entirely alone, per this project's own
    "Luna-created content, not mine to touch" rule, including five
    "same disposition `RO_COMPLETENESS_TODO.md`... already established"
    -style precedent citations inside her new files that now point at a
    file this session deleted (`TODO/WORLD/LIQUID_TODO.md`/`TODO/WORLD/WDT_TODO.md`/
    `TODO/WORLD/ADT_TERRAIN_TODO.md`/`TODO/WORLD/WMO_GEOMETRY_TODO.md`/`TODO/WORLD/ADT_LOD_TODO.md`) —
    flagged here rather than silently fixed, since they're her files, not
    read closely enough to know if she'd even want them touched.
  - **Environment note, reconfirmed**: the BLP scan needed `time direnv
    exec . uv run --python tools/venv/bin/python <script>`, backgrounded
    (it exceeded the default 120s tool timeout almost immediately and
    took ~2h55m total) — checked on via `/proc/<pid>/fd` (which real file
    it currently had open) rather than polling its own stdout, since the
    script only prints once at the very end; a `Monitor` task
    (`while kill -0 <pid>; do sleep ...; done`) was used for the final
    long stretch so a task-completion notification would arrive instead
    of manual re-checking. `uv run --python .venv/bin/python <script>`
    (not `tools/venv/bin/python`) is `blp/`'s own venv path, needed for
    the two ad hoc real-file verification scripts this session wrote
    (checking Pillow's own decode against a real DXT3 file, saving a PNG
    to eyeball) — `blp/`'s Python package and the top-level `tools/`
    scripts each have their own separate venv, confirmed by `-c` failing
    against the wrong one with an unrelated import error before catching
    it.
- **Previous state**: Closed out the remaining `M2_GAPS_TODO.md` work
  autonomously (Luna: "start implementing the changes independently
  starting from the easiest... continuing to the harder ones," then went
  offline) — two units of work, each committed separately.
  - **Items 9/10 (real-data regression tests for the previous session's
    EXP2/PFDC/BLP2 findings)**: wired the three already-pulled real fixtures
    (`test_data/verification/exp2_126382.m2`/`pfdc_1003471.m2`/
    `blp2_7507381.m2`) into `tests/test_data_paths.hpp`, then wrote real
    `doctest::skip()`-gated `TEST_CASE`s — exact field assertions
    (re-derived fresh from a live `husk dump-chunks` run, not copied from
    the TODO's own orientation numbers) for both EXP2-only and EXP2+PFDC
    fixtures in `tests/test_dump.cpp`, plus three `BLP2`-anomaly
    throws-cleanly cases (`info`/`export`/`dump-chunks`) in
    `tests/test_integration.cpp` (not `test_cli.cpp` as the TODO's own plan
    suggested — that file's own header comment explicitly states none of
    its cases need real fixtures, so the real-fixture-shaped test belongs
    in `test_integration.cpp` instead, which already has the
    `test_data_paths.hpp`/`doctest::skip` infrastructure for exactly this).
    456 → 460 test cases, both items removed from `M2_GAPS_TODO.md` per
    this project's TODO lifecycle, permanent record folded into
    `M2_COMPLETENESS.md`/`WIKI_FINDINGS.md` §13. Committed separately
    (`84a16d9`) before starting Item 4, so a rate-limit or interruption
    mid-PCOL-work wouldn't have put the already-finished Items 9/10 work at
    risk.
  - **Item 4 (`PCOL`, player-housing collision, War Within 11.1.7+) — the
    last remaining item, now implemented.** The wiki gives a full,
    byte-accountable struct (four independent `(count, offset)` regions:
    `vertexPositions`/`faceNormals`/`indices`/`flags`) but flags it
    "preliminary" — verification against real bytes came first, not
    guessed at. `pcol_files_for_exploration.txt` (already sitting in the
    repo root from the previous session's investigation, 2,354 real
    paths) fed a new from-scratch Python decoder
    (independent of husk's own C++ parser, same discipline every prior
    corpus check here uses): **all 2,354 real files decode with every
    region fully in-bounds, zero exceptions** — plus two facts the wiki
    doesn't state: `indexCount == faceNormCount * 3` on all 2,354 (each
    `faceNormal` is a per-triangle normal, the same shape M2's own core
    `collisionFaceNormals` already has — `indices` are triangle triples),
    and every decoded index is in range for that same file's own
    `vertexPosCount` (zero out-of-range references). The wiki's own
    warning — "there can be extra bytes between the data, use the
    offsets" — is real, not defensive boilerplate: a real file
    (`pa_kite_lamp_creature.m2`) has an 8-byte gap between `faceNormals`'
    own end and `indices`' own offset, so the implementation reads each
    region via its own offset field, never accumulated sequentially the
    way `.phys`'s `PLYT` header+data walk is.
    - **Design call made autonomously, not escalated**: diagnostic-only
      (`husk dump-chunks`), no glTF slot — same class as `EXP2`/`PFDC`/
      `DETL` (the TODO's own docs note hedged this: "likely n/a glTF-
      ceiling... unless a real file surfaces and a translation... makes
      sense"). Real files do exist and the shape is genuinely translatable
      (position/index/normal triangles, structurally identical to how M2's
      own core collision mesh already gets a real glTF translation) — but
      `PCOL` is niche (War Within 11.1.7+ player-housing furniture only,
      2,354/130,576 files) sidecar-shaped data, not core render geometry,
      matching every sibling item in this same TODO file (`EXP2`/`PFDC`/
      `DETL` all shipped diagnostic-only despite being translatable in
      principle too) — picked the conservative, precedent-consistent
      option rather than introduce a new mesh into `.glb` output
      unprompted.
    - Implemented as `dumpPcol` (`src/cmd_dump.cpp`), moved from
      `kFallback` to `kDocumented`. Ran husk's own compiled binary against
      all 2,354 real files directly (not just the Python decoder): zero
      exceptions. New `tests/test_dump.cpp` cases: a synthetic fixture with
      deliberately non-contiguous regions (proving the offset-based read,
      not a PLYT-style sequential accumulation) and negative int16 values
      (proving signed, not unsigned, reads for `indices`/`flags`), plus a
      real-data regression test against a newly-committed fixture
      (`test_data/verification/pcol_pa_kite_lamp_creature.m2`, chosen for
      its small size — 2,016-byte chunk, 40 vertices/74 triangles — while
      still real). 460 → 462 test cases, both `./build/husk-tests`
      (462/462 + 1 permanently-inapplicable skip) and `ctest` (463/463)
      green.
    - **`M2_GAPS_TODO.md` deleted outright** once Item 4 (its last item)
      closed — same "survey's job is done" lifecycle every prior TODO file
      here has used. Permanent record: `M2_COMPLETENESS.md`'s Collision &
      physics section, `WIKI_FINDINGS.md` §10's new Follow-up subsection,
      `DESIGN.md`'s Key design decisions (new `PCOL` bullet) and Open work
      section (rewritten now that the file is gone), `README.md` (Usage
      section's `dump-chunks` paragraph, Collision/physics format-matrix
      row).
    - **Full cross-reference sweep**: grep-verified every one of the
      ~50 `M2_GAPS_TODO.md`/`M2_GAPS_TODO` mentions across `src/`/`tests/`/
      `tools/`/docs. Left ones already phrased as historical narrative
      alone (`...'s former Item N`, `Follow-up (...'s item N, now closed)`
      — same "historical log entries aren't rewritten" precedent every
      prior TODO-file deletion here has used) but fixed every mention that
      read as a live pointer to a file that no longer exists (bare
      `M2_GAPS_TODO(.md) Item N` citations in `tests/test_dump.cpp`,
      `tests/test_gltf.cpp`, `tests/test_m2.cpp`, `tests/test_integration.cpp`,
      `src/cmd_export.cpp`, `src/gltf.hpp`, `src/cmd_dump.cpp`,
      `tools/find_m2_unknown_chunks.py`) — same discipline the
      `CORPUS_TODO.md`/`MULTIROOT_SKELETON_TODO.md` deletions already
      established, applied at real scale here (many more live references
      than either of those had, since this TODO file bundled 10 independent
      items across several sessions).
  - **Environment note, reconfirmed, no repeat of a prior mistake**: one
    stray bare `python3 -c ""` (immediately followed by the correct
    `direnv exec . uv run --python tools/venv/bin/python <script>` form) —
    it errored harmlessly (no global Python, same guard as always) and
    nothing was built on its output; still worth noting since a previous
    session's whole correction was specifically about this exact mistake.
    `uv run --python tools/venv/bin/python -c "..."` (inline `-c`, as
    opposed to a script file) does **not** work — `uv run` doesn't accept
    `-c` as a passthrough flag to the interpreter the way bare `python3`
    does (`error: unexpected argument '-c' found`) — write ad hoc checks to
    a scratchpad file and pass the file path instead, confirmed working
    throughout this session.
- **Previous state**: Explored 4 untracked casc-tool scan outputs Luna dropped in
  the work dir (`m2_chunk_discovery.csv`/`.log`, `m3_corpus_scan.csv`/`.log`
  — a separate thread's full-corpus scans against a live CASC install,
  product `wow` build 68887) and turned them into real doc corrections, per
  Luna's own explicit "explore results and summarize into a coherent action
  plan" request. Found and verified three things, none previously known:
  - **`EXP2`/`PFDC`'s "zero real files" claim was a local-extraction gap,
    not a real absence.** `M2_COMPLETENESS.md`/`src/m2.hpp`/`cmd_dump.cpp`
    all previously stated husk's local corpus (`/media/luna/data/
    wow_export`) has zero real files for either tag — both parsers were
    implemented from the wiki struct alone, unverified. The new
    live-CASC chunk census (all 130,576 real `.m2` files, 31 distinct
    tags — broader than the earlier 5-tag `--watch` cross-check `WIKI_
    FINDINGS.md` §10 already used) found **17,065** real `EXP2` files and
    **2,430** real `PFDC` files — too big a gap to be the ~1% extraction
    slack §10's `PCOL`/`DPIV` case already accounted for. Confirmed by
    directly pulling two real files via `casc-tool extract` (storage
    `/media/luna/games/World of Warcraft`, requested from Luna
    mid-session): both parse cleanly through husk's existing, unmodified
    code — one shows a real monotonic 3-keyframe `EXP2` `alphaCutoff`
    curve, the other a real version-6/`phyt`-3 `PFDC` body record
    matching `WIKI_FINDINGS.md` §9's already-verified `.phys` shape. No
    parser changes needed, only the stale "unverified"/"zero files"
    claims — corrected in `M2_COMPLETENESS.md`, `src/m2.hpp`, `src/
    cmd_dump.cpp`.
  - **A genuine anomaly (`BLP2` as a 1-byte top-level M2 chunk, 1 real
    hit) resolved as a listfile mismatch, not an M2 finding at all.**
    `husk` itself refused to open the file outright (a real
    `ParseError`, not a silent misread — the boundary discipline working
    as designed). Pulled the file directly and hex-dumped it: its actual
    content **is** a genuine BLP2 texture (real magic + compression/
    width-height/mipmap-offset header, plausible 512×256), not an M2
    file — FileDataID 7507381 isn't in this project's own listfile
    snapshot, consistent with the upstream chunk-census tool's
    `*.m2`-masked enumeration trusting a stale/wrong listfile-derived
    extension rather than sniffing content. Not a husk bug, not a real
    M2 chunk — written up and closed in one pass.
  - **8 real `.m3` files exist** (a full-storage, non-`.m2`-scoped
    `M3DT`-magic byte-signature scan, 1,891,552 files) — an entirely
    different, undocumented model format, unresolved listfile names
    (`models\unknown\unk_exp*\<fdid>.m3`). Explicitly scoped by Luna as
    "note it, stay out of scope" (not a new investigation) — recorded as
    a `DESIGN.md` Non-goals addendum only.
  - Also reconfirmed, no new information: the full 31-tag census's
    `WFV1`/`WFV2`/`DPIV`/`AFRA`/`PCOL` counts land on the *exact* same
    numbers `WIKI_FINDINGS.md` §10's earlier 5-tag `--watch` cross-check
    already found — an independent second run via a broader tool,
    converging on the same result, not new news but a stronger
    confidence signal for that section.
  - **Explicitly deferred at Luna's direction**: `PCOL` (`M2_GAPS_TODO.md`
    Item 4, the one item already fully unblocked and ready) was *not*
    implemented this session — "not yet," per her own answer when asked
    directly. Next session picking this up should start there; nothing
    else blocks it.
  - **One real process correction mid-session**: reflexively ran
    `find / -maxdepth 4 ...` looking for the CASC storage path before
    asking — correctly blocked by the sandbox/user per this file's own
    "never run commands against system root" hard rule. Stopped, asked
    Luna directly for `--storage`/`--listfile` instead of guessing
    further. Separately corrected for using a bare `python3 -c` (no
    global Python on this system) instead of this project's own
    established `direnv exec . uv run --no-project python3` pattern —
    caught immediately, no repeat; used `jq` (already on `PATH`,
    installed via Luna's own profile, not project-scoped) for the rest
    of this session's JSON inspection instead.
  - The 3 pulled real files (`exp2_126382.m2`, `pfdc_1003471.m2`,
    `blp2_7507381.m2`) were moved into `test_data/verification/` and wired
    into real regression tests in the very next session — see the newer
    Last state entry above, this note is stale as of that session.
  - The 4 untracked CSV/log files that prompted this session
    (`m2_chunk_discovery.*`, `m3_corpus_scan.*`) are still sitting
    untracked in the work dir, not cleaned up or committed — Luna's own
    artifacts from the separate casc-tool thread, hers to dispose of.
- **Previous state**: Implemented 7 of `M2_GAPS_TODO.md`'s 8 items (everything
  except Item 4, `PCOL`, blocked on real data — see below) in one session,
  via **parallel subagents** rather than sequentially — requested directly:
  "start implementing @M2_GAPS_TODO.md," then, mid-triage, "considering it's
  individual tasks, could do subagents." Grouped the 8 items into 4
  worktree-isolated agents by file-overlap (to keep merge conflicts
  tractable, not by the TODO's own priority order): Item 1 alone
  (`M2Sequence` fields + `aliasNext` chain resolution, the biggest/highest-
  value piece); Items 5+7 together (both touch material-extras plumbing);
  Item 6 alone (Attachments/Events/Lights as real glTF nodes, its own
  glTF-schema surface); Items 2+8+3 together (all three are
  `cmd_dump.cpp`-only diagnostic additions). Did the two real-corpus checks
  Items 3/4 explicitly needed *before* dispatching, not after: a fresh
  130,576-file top-level-chunk-tag scan (same scanner shape as
  `tools/find_m2_unknown_chunks.py`) found **zero** real files with either
  `EXP2` or `PCOL` — per each item's own contingency plan, Item 3 still
  shipped (simple, unambiguous struct, synthetic fixture, flagged
  unverified) while Item 4 stayed parked (a wiki-flagged "preliminary"
  struct with zero real bytes to ground it, explicitly not to be
  implemented synthetic-only) and got folded into the Items-2+8+3 agent's
  brief accordingly.
  - **All four agents hit a shared API rate limit simultaneously and were
    killed mid-work** (a genuine platform limit, not a code problem) —
    each had made real, uncommitted progress in its own worktree at the
    moment of the cutoff. Confirmed via `git status`/`git log` in every
    worktree before doing anything else: nothing was lost, nothing had
    been committed prematurely. Resumed all four via `SendMessage` from
    their own transcripts (not fresh respawns — a respawn would have
    re-derived context from scratch) with an explicit "you were cut off by
    a rate limit, not a real failure, resume exactly where you left off"
    framing; all four finished cleanly on resume.
  - **Each agent independently found a real bug or a real, non-obvious
    finding while implementing its own plan** — this project's own
    "verify against real bytes, don't trust the plan document blindly"
    discipline held up under delegation, not just under direct work:
    - Item 1's agent caught that chain-resolving *every* `flags & 0x40`
      ("alias") sequence unconditionally would have been a real
      regression — 31 of `bloodelffemale_hd.skel`'s 38 real alias
      sequences *also* carry `flags & 0x20` ("stored inline"), meaning
      they already have real keyframe data of their own; `0x20` has to
      keep winning priority exactly as it did before `aliasNext`
      resolution existed, or those 31 real clips would have silently had
      the wrong sequence's data substituted in. Caught before shipping,
      not found by a later regression test. Also found, honestly: the
      fix's *measured* effect on the committed fixture is **zero net new
      clips** (all 7 genuinely-alias sequences resolve to a terminal
      sequence needing an external `.anim` file not among the ~104
      already committed) — the original plan's own "don't assume every
      alias necessarily gains a clip" caveat held exactly.
    - Items-5+7's agent caught that `M2Color::alpha`/`M2TextureWeight
      ::weight` are `M2Track<fixed16>` (2-byte wire values), not
      `M2Track<float>` (4 bytes) — the TODO's own suggested plan said to
      reuse `resolveFloatTrackSequence` for these, which would have
      silently misread the 2-byte wire bytes as garbage 4-byte IEEE
      floats. Used `resolveRawIntTrackSequence(..., elementSize=2)`
      instead, decoding fixed16 → 0..1 the same way the existing
      constant-value path already does.
    - Items-2+8+3's agent found that `DETL`'s defensive floor needs to be
      `min(lightCount, chunk.size/12)`, not `chunk.size/12` alone — a real
      3-light file pads 36→48 bytes for 16-byte alignment, and 48/12
      happens to equal exactly 4, silently overcounting by one record if
      the floor isn't taken against the header's own `lights.count` too.
  - **Merging required real, careful conflict resolution, not blind
    `git merge`** — 4 branches all touched the shared `M2_GAPS_TODO.md`
    (each removing its own item's section) and several touched
    `M2_COMPLETENESS.md`/`DESIGN.md`/`WIKI_FINDINGS.md` (each adding its
    own row/entry) and `gltf.hpp`/`gltf.cpp`/`cmd_export.cpp` (each adding
    its own feature). Merged and rebuilt+retested after *every* branch,
    not all 4 at once, so a bad merge would be caught immediately rather
    than compounding: `git branch --merged`-verified fully clean before
    deleting. Two real hand-resolution mistakes happened and were caught
    by re-reading the file afterward, not assumed correct from the diff
    alone: (1) `test_integration.cpp`'s Items-5+7 merge conflict was
    actually two independent branches both appending a "load the exported
    glb, then assert" test case at the same location — git's diff matched
    the two tests' identical boilerplate as shared context, so the naive
    conflict markers implied *interleaving* two unrelated test bodies;
    reconstructed by hand into three separate, complete, non-overlapping
    `TEST_CASE`s (Item 5's, Item 6's already-merged one, Item 7's). (2) The
    final Item-1 merge's `M2_GAPS_TODO.md` conflict was resolved wrong on
    the first pass — kept HEAD's still-full Item 1 section body instead of
    collapsing it now that Item 1 is done, leaving a stale, already-
    obsolete section sitting in the file; caught by re-reading the merged
    file end-to-end afterward (`grep "^## Item"`) rather than trusting the
    conflict resolution had done the right thing, then fixed by trimming
    the section out and rewriting the priority-order list and its
    "here's where the finished items live" note to name all 7 finished
    items, not just the ones each individual merge happened to know about.
  - **Final state, verified via a full clean rebuild** (`rm -rf build`,
    reconfigure, rebuild, both `./build/husk-tests` and `ctest`): 335 → 455
    test cases (456/456 via `ctest`, 1 permanently-inapplicable skip),
    zero failures. All 4 worktrees/branches removed after confirming
    `git branch --merged master` covered every one of them — nothing left
    behind.
  - **Docs**: `M2_GAPS_TODO.md` now holds only Item 4 (`PCOL`), with a
    "checked: 0/130,576 real files" note added to its own Blocker section
    and a combined note naming all 7 finished items and where their
    permanent record lives (not deleted outright, since one real item is
    still genuinely open — unlike every prior fully-emptied TODO file in
    this project's history). `M2_COMPLETENESS.md` (Attachments/Events/
    Lights rows to `native — 100%`; new `M2Sequence`-metadata,
    hardcoded-texture-slot, animated-tint/fade, `DETL`, and `PFDC` rows;
    `EXP2` folded into the particle/ribbon side-chunks row; `Alias
    sequences` row corrected from "n/a, upstream-spec gap" to
    `native — 100%`), `DESIGN.md` (5 new Key design decisions, one per
    shipped feature area), `WIKI_FINDINGS.md` (§11/§12's "Where these live
    in husk" table rows filled in), `README.md` (Materials paragraph).
- **Previous state**: Ran `M2_UNKNOWNS_EXPLORATION.md`'s investigation brief to
  completion — six targets (wowdev.wiki chunk types/fields with no
  field-level struct, or an internally-inconsistent one), each given a real
  disposition grounded in real corpus bytes, not guessed at. Requested
  directly: "Start on @M2_UNKNOWNS_EXPLORATION.md." Same methodology every
  prior wiki-correction session here has used (independent from-scratch
  scanner, cross-checked against many real files, full byte-accounting
  before trusting a stride) — see `WIKI_FINDINGS.md` §10/§11/§12 for the
  full writeups.
  - **Targets 1–4 (`WFV1`/`WFV2`/`DPIV`/`AFRA`) — confirmed absent, a real
    negative result.** New `tools/find_m2_unknown_chunks.py` walked the
    full real corpus (`/media/luna/data/wow_export`, all 130,576 `.m2`
    files, one top-level-chunk-tag pass, ~30s) and found **zero** real
    files carrying any of the four tags. Sanity-checked the scanner's own
    chunk-walk logic against `test_data/bloodelffemale.m2`'s known-good
    `MD21`/`TXAC`/`AFID`/`LDV1`/`SFID`/`TXID` sequence first, so the
    corpus-wide zero isn't a scanner bug — confirmed all 130,576 files are
    `MD21`-chunked (no pre-Legion flat files in this corpus to explain the
    zero as "wrong file era" either). `WFV3` (`WFV1`/`WFV2`'s later,
    fully-documented successor) was found in exactly 9 real files
    elsewhere in this same corpus, already implemented — so the zero here
    is "this corpus's own extraction doesn't happen to have one," not
    "the format never existed." Written up as `WIKI_FINDINGS.md` §10.
    Per Luna's own explicit follow-up request ("also write the unknown
    chunks if not solvable with this data as todo list for casc-tool...
    write it here, i will personally move it to correct place"), a
    **standalone `CASC_TOOL_TODO.md`** (repo root, deliberately
    **not** committed, not referenced from any husk doc) hands this
    negative result to Luna's separate `casc-tool` project as a "worth a
    broader CASC pull across other builds/regions" lead, with the one
    concrete FileDataID the wiki names (`WFV1`, 2445860) and husk's own
    scanner script ready to point at any other corpus root if a hit ever
    turns up.
  - **Target 5 (`DETL`) — fully resolved, a real byte-layout correction
    plus one wholly new finding.** The wiki's own struct lists fields
    summing to 0x0c bytes but ends with a `/*0x0a*/` comment — a
    pre-existing 6-byte discrepancy `cmd_dump.cpp`'s `kFallback` table
    already flagged as the reason this wasn't parsed. New `tools/
    check_detl_stride.py` found 1,043 real `DETL`-bearing files (mostly
    player-housing lighting fixtures, War Within-era). A first crude
    `chunk.size / lights.count` division looked like a confusing 3-way
    split (1,012 files "clean" at 16 bytes, 18 at 12 bytes, 13 at neither)
    — until a direct byte decode on a real multi-light file
    (`goblinspidertank.m2`, 4 lights) showed stride 16 produces garbage
    past the first record while stride 12 decodes all 4 records
    identically clean, revealing the "16-byte" bucket was a numerical
    coincidence (`48 = 12×4 = 16×3` both hold), not a second real struct
    variant. Testing the corrected hypothesis — **real stride is 12 bytes,
    whole chunk zero-padded up to the next 16-byte alignment boundary**
    (undocumented on the wiki) — against all 1,043 files at once: **100%
    match**, vs. 998/1043 and 1012/1043 for the two wrong candidate
    strides. Decoding all 1,386 real records at the confirmed stride found
    `flags` takes only two real values (0/0x8), and `scale`/
    `diffuseColorMultiplier` are a **constant** half-float value
    (0.013885498046875 / exactly 1.0) in every single real record sampled
    — about as clean a confirmation as real data gets. Written up as
    `WIKI_FINDINGS.md` §11; a full implementation plan (trivial — reuses
    `M2Particle`'s existing half-float decoder, diagnostic-only
    `dump-chunks` output, no glTF slot needed) added as `M2_GAPS_TODO.md`
    Item 8, not implemented in `src/` this session per the investigation
    brief's own "investigation, not implementation" scope.
  - **Target 6 (`M2Sequence.aliasNext`) — fully resolved, and it corrects a
    real bug in an *earlier* husk investigation, not just the wiki.** The
    existing `TODO/TODO_correctness.md` #4 (from a prior session) had already
    tried and failed to resolve this on `bloodelffemale_hd.skel` alone,
    finding `aliasNext` values in the 48,861–48,983 range that didn't
    resolve as a local index or a same-file id match. New `tools/
    check_alias_next.py`, reading the field at `M2Sequence`'s real,
    `WIKI_FINDINGS.md` §1-corrected 64-byte stride (`aliasNext` at offset
    **0x3E**, not the wiki's literal, pre-correction 0x22 — 0x22 lands
    inside `M2Range replay`'s second field at the real stride, exactly
    explaining the earlier session's nonsensical 5-digit values), across
    all four real blood-elf family files (`bloodelffemale.m2`/
    `bloodelfmale.m2` inline, `bloodelffemale_hd.skel`/
    `bloodelfmale_hd.skel` external — 1,483 sequences, 157 real aliases):
    **157/157 (100%) resolve as valid local indices into the same file's
    own `sequences` array**, and following the wiki's own documented
    chain-walk (`flags & 0x40` → jump to `sequences[aliasNext]` → repeat)
    terminates cleanly at a non-alias sequence for all 157, zero cycles,
    zero runaways. `aliasNext` is a plain local array index, not an
    `AnimationData.dbc` id and not anything cross-file, despite its own
    "id in the list of animations" doc comment — the wiki's older "I have
    no clue" bullet is simply stale, and the earlier husk session's
    "unresolvable" conclusion was a stale-offset bug, not a real dead end.
    A secondary cross-file `id`-match check (101/157 `aliasNext` values
    also happen to match some sequence's `id` in a sibling file) was
    checked and set aside as very likely coincidental — small integers
    collide constantly with small real ids in a several-hundred-entry
    space, and the match rate exactly tracks the already-explained
    local-index values, not an independent signal. A bounded 2-query web
    search (matching this project's own "don't over-spend" precedent from
    the multi-root-skeleton investigation) found no public prior-art
    resolution to corroborate against, but the real-byte result stands on
    its own. Written up as `WIKI_FINDINGS.md` §12, including the explicit
    "what went wrong the first time" section explaining the earlier
    session's bug. `TODO/TODO_correctness.md` #4 **removed outright** per that
    file's own stated convention (fixed items don't linger as `[Fixed]`
    noise) — it was the last item, no renumbering needed — with a
    summary folded into the file's own intro paragraph. `M2_GAPS_TODO.md`
    Item 1's `aliasNext` bullet rewritten from "parse raw, don't resolve"
    to "parse and resolve" — a real, not-yet-implemented follow-up now
    unblocked: `buildAnimations` currently skips every alias sequence
    outright, and could instead reuse the resolved terminal sequence's own
    animation data to produce a real clip.
  - **Docs**: `WIKI_FINDINGS.md` (three new sections, §10/§11/§12, full
    "current text / proposed addition / evidence" format matching every
    prior section on this page; "Where these live in husk" table extended
    3 rows), `M2_GAPS_TODO.md` (Item 1's `aliasNext` bullet rewritten, new
    Item 8 for `DETL`, priority-order list extended), `TODO/TODO_correctness.md`
    (#4 removed, intro paragraph updated), `DESIGN.md` (Open work section
    gained a `M2_GAPS_TODO.md` pointer it was oddly missing even before
    this session, plus a closing paragraph for
    `M2_UNKNOWNS_EXPLORATION.md`'s own now-completed disposition).
    `M2_UNKNOWNS_EXPLORATION.md` itself **deleted outright** once every one
    of its six targets had a final disposition — same "survey's job is
    done" lifecycle every prior investigation-then-TODO file in this repo
    has used. Its ~3 live cross-references inside the three new `tools/
    *.py` scripts' own docstrings were grep-verified and repointed to
    `WIKI_FINDINGS.md`'s new section numbers rather than left dangling —
    same discipline every prior file-deletion session here has used.
  - **New standalone tools, kept** (same "small, self-contained, one-off,
    independent of husk's own C++ parser" convention `tools/
    find_multiroot_skeletons.py` already established): `tools/
    find_m2_unknown_chunks.py`, `tools/check_detl_stride.py`, `tools/
    check_alias_next.py`. Their generated `*_for_exploration.txt`/
    `*_report.json` output files at repo root are already covered by this
    repo's existing blanket `*.txt`/`*.json` `.gitignore` rules (same as
    `phys_files_for_exploration.txt`/`multiroot_skeleton_files_for_
    exploration.txt` before them) — no cleanup needed, left as local
    scratch artifacts.
  - **Environment note, reconfirmed**: `direnv exec . uv run --python
    tools/venv/bin/python <script>` for the three full-corpus scanner runs
    (all under 30s each), `direnv exec . uv run --no-project python3 -c
    "..."` for ad hoc byte-level verification one-liners (the stride
    cross-check, the half-float decode, the goblinspidertank direct-decode
    sanity check) — same split this project's environment notes have used
    every prior session, inline `-c` fine for quick checks per this
    session having no standing instruction against it.
- **Previous state**: Implemented both `ANIM_TODO.md` and `PHYS_TODO.md` end to
  end, independently, in one autonomous overnight session — requested
  directly: "read both PHYS_TODO and ANIM_TODO, implement them
  independently but carefully... extend the tests to actually cover stuff,
  not just be 'we have more than 100 animations, that counts as a pass
  right?'." No interactive user available partway through, so the two
  plan-mode-flagged open questions each document left for a real design
  pass (`ANIM_TODO.md`'s implementation was already fully speced;
  `PHYS_TODO.md`'s extras-vs-dump-chunks split/CLI flag shape was not) were
  resolved by following each document's own stated recommendation rather
  than blocking — `PHYS_TODO.md`'s explicitly: "the same pattern as
  particles and ribbons, attachment points in glb but data i separate,"
  confirmed by the user mid-session, matching the doc's own Architecture
  recommendation already.
  - **`ANIM_TODO.md` (the `--anim` same-basename fallback)**: implemented
    exactly as planned (`findAnimFileByBasename`, `M2AnimInputs::modelPath`,
    `buildAnimations`'s external branch rewired to try `<FileDataID>.anim`
    then `<model-basename><animId>-<subId>.anim`) — with one real bug the
    plan itself had that only surfaced once the existing test suite ran
    against the change: the file-open fallback logic used `if (!f)` to
    decide whether the FileDataID attempt succeeded, but a default-
    constructed `std::ifstream` that never had `.open()` called on it (the
    `animFileIds == nullopt` case) reports `goodbit`, not `failbit` — `!f`
    is false, so it silently skipped the basename fallback and tried to
    read an unopened stream, producing empty bytes fed straight into
    `extractAnimBlob`/chunk parsing, which threw a real "claims more
    keyframes than this blob holds"-style error. Caught by a genuine
    pre-existing test (`tests/test_cli.cpp`'s "a sequence without
    flags&0x20 ... produces no animation clip" case, which doesn't pass
    `--anim` at all so `animArg` defaults to `auto` and resolves `animDir`
    to a real, non-empty directory) going from pass to fail the moment the
    rewired branch landed — not found by inspection. Fixed by checking
    `f.is_open()` instead of `!f` at both fallback points. Verified as a
    real, non-cosmetic bug (not just "the fix looks more correct") by
    `git stash`-ing the fix, confirming the exact failure, then restoring
    it and confirming green — same "prove a regression test actually
    regresses" discipline the multi-root/collision-mesh sessions already
    established for their own changes.
    - **4 new `tests/test_cli.cpp` cases** (basename fallback with no AFID
      at all, basename fallback when the AFID-mapped file is missing,
      FileDataID-file priority over a basename file when both exist —
      proven by making the basename file deliberately too short to
      resolve, so a wrongly-reversed priority would crash instead of
      silently reading wrong data — and neither resolving), all built on
      the existing `tinyExternalAnimM2`/`AFID`-chunk fixture already used
      by two adjacent tests, not a new fixture shape.
    - **`tests/test_integration.cpp`'s existing AFSB-follow-up case
      strengthened**, exactly per the plan and per this session's own
      explicit instruction to stop asserting fragile loose counts: it used
      to only check `model.animations.size() > 100` (true from inline +
      global-sequence clips alone, proving nothing about the fix). Added
      exact-name assertions for `anim_69_0`/`anim_69_1` (the two real,
      committed `bloodelffemale_hd0069-00/-01.anim` fixtures) — verified as
      a real regression test the same `git stash` way: **both** names are
      absent pre-fix (336 total clips, matching the inline-only baseline
      exactly), both present post-fix (338 — the fix adds exactly the 2
      real external clips this fixture set has, nothing else).
    - **Docs**: `DESIGN.md` (AFSB design note gained the resolution-vs-
      decode distinction; CLI grammar table's `--anim` row), `WIKI_FINDINGS.md`
      §2's follow-up (corrected the "336 clips, verified three independent
      ways" claim to note that verified the decode via a separate script,
      not `--anim`'s own CLI resolution — the actual pre-fix reachable
      count through the CLI was 336, i.e. zero of the genuinely-external
      clips), `README.md` (`--anim` usage paragraph, Animation-sequences-row,
      Sidecar-FileDataID-resolution row). `ANIM_TODO.md` deleted outright
      once every doc-sync item had a final disposition, its own two live
      code comments repointed to `WIKI_FINDINGS.md`/`DESIGN.md`.
  - **`PHYS_TODO.md` (`.phys` physics/collision sidecar support)**: the
    full implementation plan, built essentially as specified. New
    `src/phys.hpp`/`phys.cpp` (mirrors `bone.hpp`'s shape, not `skel.hpp`'s
    — chunk-tag-selected record arrays, not a multi-array header): every
    documented chunk type (`PHYS`/`PHYT`/`BODY`/`BDY2`/`BDY3`/`BDY4`/
    `SHAP`/`SHP2`/`BOXS`/`CAPS`/`SPHS`/`PLYT`/`JOIN`/`WELJ`/`WLJ2`/`WLJ3`/
    `SPHJ`/`SHOJ`/`SHJ2`/`PRSJ`/`PRS2`/`REVJ`/`REV2`/`DSTJ`/`PHYV`) parsed,
    chunk-tag-preference variant selection (`BDY4`→`BDY3`→`BDY2`→`BODY`,
    etc.), `SHOJ`'s real stride ambiguity (0x6c vs. 0x74, same tag)
    disambiguated by which stride the chunk's own size divides evenly —
    throws if a real file ever divides evenly by both (never seen in 103
    real files, per the investigation). `PLYT`'s self-describing variable-
    length header+data region implemented with a full byte-accounting
    check (expected total size computed field-by-field must exactly equal
    the chunk's real size), the same cross-check the original investigation
    used to catch the 0x38-vs-0x50 header-stride bug in the first place.
    Every `Body.shapeBase`/`shapeCount`, `Shape.index`, `Joint.bodyA`/
    `bodyB`/`index` reference validated in-range at parse time (real files
    have zero violations per the investigation, so a real one is corruption
    or a parser bug, not data to accept).
    - **Architecture**: followed the plan's own recommendation, confirmed
      directly by the user mid-session ("we want the same pattern as
      particles and ribbons, attachment points in glb but data i
      separate") — `husk export --phys` (three-state, mirroring `--skel`
      exactly, since `PFID` is a single scalar FileDataID like `SKID`, not
      an array like `BFID`/`AFID`/`SFID`: unset auto-detects a
      same-basename `.phys` next to the model, `none` skips, an explicit
      path overrides) attaches a **minimal** per-body placement anchor
      (`gltf::Skeleton::PhysicsBody` — id/joint/position/bodyType) as
      `physics_bodies` skin `extras`; `husk dump-chunks <file.phys>` (new
      direct-file-input path, sniffed by the reversed `PHYS` tag before the
      `.bone`/M2-magic checks) dumps the **full** body/shape/joint/`PHYV`
      record set, each shape/joint resolved to its real type-specific data
      inline (a body's shapes fully expanded; a joint's `bodyA`/`bodyB`
      left as plain indices, matching how the source data itself relates
      them).
    - **Real test fixture gap found and fixed mid-session**: none of the 7
      already-committed `.phys` weapon fixtures had a matching `.skin`
      committed alongside them (only `.m2`+`.phys`), so no real file could
      exercise the full CLI→gltf-extras→gltf_validator path end to end.
      Checked the real corpus (`/media/luna/data/wow_export`, read-only)
      and found every one of those 7 `.m2` files does have a real `.skin`
      sibling there, just not extracted into `test_data/` yet — copied
      `mace_1h_warfrontsforsaken_d_0100.skin` in (gitignored, same
      "personal WoW extraction, never committed" convention every other
      `test_data/` fixture already follows), giving one real, fully-paired
      `.m2`+`.skin`+`.phys` fixture. Confirmed by hand: 10 real bodies,
      `boneIndex` values `{0..9}` of 17 real bones, exactly matching what
      `PHYS_TODO.md`'s own test plan had predicted from the investigation
      but never verified against a live export.
    - **Verification, not just "it compiles"**: ran the real, already-
      committed 7-file weapon `.phys` set *and* the full 96-file real-corpus
      exploration sample (`phys_files_for_exploration.txt`,
      `/media/luna/data/wow_export`, read-only) through `husk dump-chunks`
      — zero failures across all 103 files, the same sample size and same
      zero-violation result the original investigation reported for its own
      independent Python decoder, now reproduced by husk's real C++ parser.
      Spot-checked the one real `PLYT`-bearing file the investigation named
      by path (`.../8xp_heartofazeroth_prop_floatychain.phys`): decodes to
      the exact `vertexCount=8/count_10=6/nodeCount=24` the investigation's
      own worked example reported. The real paired fixture's `.glb` export
      passes the actual Khronos `gltf_validator` with 0 errors.
    - **Tests**: `tests/test_phys.cpp` (new, 13 cases — happy-path
      round-trip, `BODY`-vs-`BDY4` selection-order preference, `SHOJ`
      stride disambiguation both directions plus the genuinely-ambiguous
      throw path, out-of-range shape/joint/body-index throws, malformed
      stride throws, `PLYT` round-trip and truncation), `tests/test_gltf.cpp`
      (4 new: `PhysicsBody` extras round-trip, absent-means-no-key,
      out-of-range-joint throws, coexists with `correctionSets`/
      `ribbonAnchors`/`particleAnchors`), `tests/test_cli.cpp` (4 new:
      `--phys` default/`none`/explicit-path/out-of-range-throws, synthetic
      fixtures matching `--bones-dir`'s own established fixture-building
      style), `tests/test_dump.cpp` (1 new: full JSON shape round-trip
      through a real capsule shape + weld joint, checking specific resolved
      field values, not just presence), `tests/test_integration.cpp` (1
      new: the real paired fixture, exact 10-body count, exact bone-index
      set `{0..9}`, joint-range bounds), `tests/test_conformance.cpp` (1
      new, `#ifdef`-gated both ways like every other conformance case: the
      real fixture's export passes `gltf_validator` with 0 errors). Full
      suite: 394 → 422 cases, both `./build/husk-tests` (422/422 + 1
      permanently-inapplicable skip) and `ctest` (423/423) green, verified
      via a full clean rebuild (`rm -rf build`), not an incremental one.
    - **Completions**: `--phys` added to `src/main.cpp`'s hand-maintained
      `bashValueCompletion`/`zshValueAction`/`zshFlagLabel` tables (same
      `--skel`-shaped file-or-none treatment — the existing `_husk_skel_value`
      zsh helper was shared and renamed to `_husk_file_or_none_value` since
      it's no longer skel-specific), `completions/husk.bash`/`.zsh`
      regenerated via `--print-completion`. Verified by `bash -n`/`zsh -n`
      syntax-checking both (this sandbox's nix bash build has no
      `compgen`/`complete` builtins compiled in, so the usual "drive
      `_husk_completions` with scripted `COMP_WORDS`" functional check
      wasn't possible this session) plus a direct structural diff against
      `--skel`'s own already-verified-working block, byte-for-byte
      identical shape.
    - **Docs**: `DESIGN.md` (new Key design decisions bullet mirroring the
      ribbon/particle one; Boundaries list; CLI grammar table; Open work
      section's `PHYS_TODO.md` pointer removed, replaced with the same
      "used to live here, now implemented, standalone file removed"
      framing `MULTIROOT_SKELETON_TODO.md`'s own removal used), `WIKI_FINDINGS.md`
      §9 (pointer to `PHYS_TODO.md` replaced with the real Code/Tests
      columns in "Where these live in husk"), `README.md` (Collision/
      physics format-matrix row bumped 📖, Sidecar-FileDataID-resolution
      row, new "`.phys` physics/collision data" Usage paragraph, flag
      table row, `dump-chunks` section heading/paragraph), `M2_COMPLETENESS.md`
      (`.phys` sidecar content row: `none/none/n/a-unscoped` →
      `full/extras+diagnostic/extras-capped-permanent`), `src/cmd_info.cpp`
      (the `phys_file_id` note's stale "not yet resolved by husk" text
      corrected to describe the real `--phys`/`dump-chunks` paths now —
      deliberately did **not** add a new sidecar-content-reading capability
      to `husk info` itself, since `info` has never opened *any* sidecar's
      content, `.skel` included, only ever printed the FileDataID scalar —
      inventing that only for `.phys` would have been unscoped, inconsistent
      new behavior, not a doc-sync fix). `PHYS_TODO.md` deleted outright
      once every doc-sync item had a final disposition, its ~9 live code/test
      comment cross-references repointed to `DESIGN.md`/`WIKI_FINDINGS.md`
      the same way `MULTIROOT_SKELETON_TODO.md`'s deletion repointed its own.
  - **Environment note, reconfirmed**: `direnv exec . uv run --no-project
    python3 <script>` for every ad hoc real-file verification pass this
    session (the 96-file corpus sweep, the `PLYT` spot-check, the JSON
    field inspection) — scripts run inline via `-c`, not written to the
    scratchpad, since none needed more than a few lines and this session
    had no standing instruction against it (unlike an earlier session's
    explicit "write scripts to files, inline `-c` prompts on every
    iteration" note, which applies to *iterative* byte-level derivation
    work, not one-shot JSON inspection).
- **Previous state**: Closed out `MULTIROOT_SKELETON_TODO.md` the same way
  `CORPUS_TODO.md` was closed out below — requested directly: "explore
  MULTIROOT_SKELETON_TODO.md, and make sure appropriate documentation is
  in DESIGN and README, for items that are done/resolved... document
  remaining items and decisions and unfixables in DESIGN, and remove the
  file once empty." Confirmed the file's own "Implemented" framing against
  the actual repo state (not just trusted the file's own claim): its
  Decision, Implementation plan (all 5 steps), and the invariant section
  were all already faithfully reflected in `DESIGN.md`'s Key design
  decisions and `src/gltf.hpp`'s `Skeleton`/`writeGlbMulti` doc comments —
  confirmed by reading both directly, not by inspection of the TODO file
  alone. The one gap: `README.md`'s format-support matrix — this project's
  own source-of-truth table for per-feature state — had zero mention of
  multi-root handling at all, unlike `M2_COMPLETENESS.md`'s parallel row,
  which already had one. Added a matching sentence to README's "Skeleton /
  bone hierarchy" row. `DESIGN.md`'s Open work section gained a new
  paragraph for the three things the original survey explicitly left
  unchased (kept, not discarded, since they're genuinely still open, just
  not blocking): what `gltf_validator`'s `SKIN_NO_COMMON_ROOT` check
  actually measures (empirically ~7% of a random multi-root sample, no
  hypothesis tested explains the rate), why an 11-file hit sample skewed
  toward one item family, and what `M2CompBone.flags & 0x200`
  ("transformed") actually distinguishes among root bones — all three
  low-priority, awareness-only, recorded so a future session doesn't
  re-derive them from scratch.
  `MULTIROOT_SKELETON_TODO.md` itself was then deleted outright, same
  "survey's job is done" disposition `VERIFICATION_IDEAS.md`/
  `DESIGN_CHANGES.md`/`CORPUS_TODO.md` already got. Its ~19 live
  cross-references across `src/gltf.cpp`, `src/gltf.hpp`,
  `tests/test_gltf.cpp`, `tests/test_cli.cpp`, `tests/test_conformance.cpp`,
  `tools/find_multiroot_skeletons.py`, `M2_COMPLETENESS.md`, `PHYS_TODO.md`,
  and this file's own living Next-step/Hazards bullets (below) were each
  grep-verified and rewritten to describe the fact directly or point at
  `DESIGN.md`/`src/gltf.hpp` instead — comment/string-literal-only changes,
  no logic touched. This session's own historical Resume entries further
  down (and their references to `MULTIROOT_SKELETON_TODO.md` by name) were
  deliberately left as-is, same "historical log entries aren't rewritten,
  only living cross-references are repointed" precedent every prior
  file-deletion session here has used. No `src/` behavior changed — pure
  documentation and cleanup, comment-only edits to `src/`/`tests/`, no
  rebuild performed (none of the edits touch code, only comments and
  string literals inside `TEST_CASE`/docstrings).
- **Previous state**: Closed out `CORPUS_TODO.md` — requested directly: "read
  CORPUS_TODO.md, discard items that are genuinely done and documented,
  document rest of them in DESIGN and README." Re-read all 7 items plus
  their DEVELOPER NOTES: every single one already carried a `[DONE]` (or,
  for #7, "Noted") disposition from the earlier punch-card session (commit
  `9c52615`), and every item that changed behavior or established a new
  fact already had a permanent home — #1 (zero-mesh), #3b (2-digit `.skin`
  suffix preference), and #4 (duplicate-keyframe nudge) in `DESIGN.md`'s
  Key design decisions and `M2_COMPLETENESS.md`; #6 (`WFV3` short variant)
  in `WIKI_FINDINGS.md` §8; #2's extraction-gap finding in `README.md`.
  Confirmed the one loose end from the developer notes ("I will manually
  fix this," for `tools/corpus_checks.py`'s truncating
  `_last_meaningful_line`) really is fixed by reading the current source —
  `[:400]` is gone. The only genuinely undocumented items were #3c
  (mismatched `.skin`/`.m2` vertex counts) and #5 (`materialIndex`/
  `textureComboIndex` one-past-the-end) — both confirmed-unfixable
  bad-source-data findings with no behavior change, so no `DESIGN.md`
  entry, but worth the same public-facing honesty #2 already gets: added a
  paragraph to `README.md` right after #2's existing extraction-gap note
  (0-byte files folded in alongside, same extraction-completeness class).
  With every item accounted for, nothing was left to discard piecemeal —
  the whole file's job was done, same "survey's job is done" disposition
  `VERIFICATION_IDEAS.md`/`DESIGN_CHANGES.md`/`PHYS_SIDECAR_FINDINGS.md`
  already got, so `CORPUS_TODO.md` was deleted outright rather than left
  as an all-`[DONE]` husk. Unlike those three, though, its item numbers
  were baked into ~20 live `CORPUS_TODO.md #N` comments across `src/`
  (`cmd_export.cpp`, `gltf.hpp`, `cmd_dump.cpp`), `tests/` (`test_cli.cpp`,
  `test_gltf.cpp`, `test_dump.cpp`), and `tools/find_multiroot_skeletons.py`
  — every one grep-verified and rewritten to describe the fact directly
  (or point at `WIKI_FINDINGS.md`/`DESIGN.md` where the permanent record
  already lives) rather than left dangling, same discipline the
  `VERIFICATION_IDEAS.md` deletion used for its own much smaller
  reference count. `M2_COMPLETENESS.md`'s two `CORPUS_TODO.md #N`
  citations repointed to `DESIGN.md` the same way. This session's own
  historical entries below (and this file's Status section's one
  narrative mention) were deliberately left naming `CORPUS_TODO.md` by
  name where they're describing what happened in the past — same
  "historical log entries aren't rewritten, only living cross-references
  are repointed" precedent `VERIFICATION_IDEAS.md`'s own deletion already
  set. No `src/` behavior changed this session — pure documentation and
  cleanup, verified by a full rebuild + `./build/husk-tests` afterward.
- **Previous state**: Read-only investigation into `.phys` (physics/collision
  sidecar, `M2_COMPLETENESS.md`'s Collision & physics section, previously
  completely unscoped -- husk only ever read the `PFID` FileDataID scalar,
  never the file's own content), requested directly: "do an read only
  investigation on this ... poke around, ask if something is unclear."
  Two-part follow-up in the same session, once the investigation confirmed
  implementation was viable: "would we be able to implement the .phys
  handling into husk with this information? if so, go ahead and write the
  WIKI_FINDINGS, and convert the PHYS_SIDECAR_FINDINGS into a comprehensive
  and testable todo." No `src/` changes -- investigation and documentation
  only, same "findings and plan before code" shape `MULTIROOT_SKELETON_TODO.md`
  used for the multi-root gap above.
  - **Different starting position than every prior sidecar investigation**:
    `.phys` is not undocumented. `documentation/wowdev-wiki/md/PHYS.md`
    (wiki_revision 30458) already gives byte offsets for nearly every
    field, so this was verify-against-real-bytes, not reverse-engineer-
    from-nothing (`.bone`'s situation) or crack-a-format-the-wiki-doesn't-
    cover (`AFSB`'s).
  - **Independent scratch decoder** (Python, not committed, no dependency
    on husk's own not-yet-written parser -- same discipline
    `tools/find_multiroot_skeletons.py` already established), run against
    103 real files: the 7 already-committed weapon fixtures under
    `test_data/item/objectcomponents/weapon/`, plus 96 real corpus files
    Luna had already listed in `phys_files_for_exploration.txt`
    (world doodads, item components, creatures, spell-effect arena flags).
  - **One real transcription bug found and fixed in understanding**:
    `PLYT`'s self-describing header struct is 80 bytes (0x50) per entry,
    not 38 (0x38) -- the wiki's own struct listing has the extra trailing
    `float unk_38[6]` field, but it's easy to misread the struct as ending
    one field earlier. Caught by the second header entry in a real 4-
    polytope file decoding to garbage at the wrong stride and to clean,
    wiki-comment-matching values (`vertexCount=8 count_10=6 nodeCount=24`,
    "mostly 8/6/24" per the wiki's own text) at the corrected one.
  - **One real semantic correction**: `BODY`/`BDY3`/`BDY4`'s `type` field
    comment ("only one body should be of type 0, the root") is contradicted
    by 78 of 98 real files with a body chunk -- multiple type-0 bodies is
    the common case (up to 27 of 44 in one creature file), cross-tabulated
    against `BDY3`'s own `unk1`-as-kinematic-weight field with a 96% clean
    correlation across 1256 real body records, consistent with type-0
    meaning "kinematic, bone-driven" as a real per-body classification,
    not a single distinguished root.
  - **Everything else in PHYS.md's struct listing verified clean**: chunk-
    tag byte-reversal (WMO/ADT convention, opposite of M2's own inline
    chunks -- confirmed via hex dump), the `PHYV` chunk's mutual-
    exclusivity claim and worked example (confirmed on the exact file the
    wiki names by filename, `7vs_detail_nightmareplant01_phys.phys`, plus
    its sibling), version↔chunk-name-variant pairing (zero exceptions),
    `SHOJ`'s documented-but-ambiguous version-2 stride cutover (0x6c vs.
    0x74 -- every one of 86 real chunks divided evenly by exactly one,
    never both), and -- the strongest single piece of corroborating
    evidence -- a full cross-chunk index/bounds validation pass
    (`BODY`/`BDY3`/`BDY4`'s shape ranges, `SHAP`/`SHP2`'s `shapeIndex`,
    `JOIN`'s `bodyAIdx`/`bodyBIdx`/`jointId`) across all 103 files found
    **zero** out-of-range references anywhere.
  - **Findings written to `WIKI_FINDINGS.md` §9** (new), following the
    page's own "current text / proposed addition / evidence" convention,
    with a "Follow-up" subsection for the full verification sweep --
    same shape §2 (`AFSB`) and §8 (`WFV3`) already use. `PHYS_SIDECAR_FINDINGS.md`
    (this session's own intermediate scratch-investigation file) was then
    deleted outright once its content had a permanent home split two ways
    -- same "survey's job is done" disposition `VERIFICATION_IDEAS.md` and
    `DESIGN_CHANGES.md` got in earlier sessions, not left in place with
    `[DONE]` tags.
  - **`PHYS_TODO.md`** (new) is the actionable half -- a concrete
    implementation plan, not another open-ended survey, since the
    investigation resolved essentially every structural question. Covers:
    a verified-vs-unverified coverage table per chunk type (driving
    implementation priority -- `PLYT`/`CAPS`/`SHP2`/`BDY4`/`SHOJ`/`REVJ`/
    `WLJ2` all verified against real files; `BOXS`/`SPHJ`/`PRSJ`/`PRS2`/
    `DSTJ`/`SHJ2`/`WLJ3`/`REV2`/`BDY2` never observed anywhere in the
    103-file sample, flagged for the same "verified floor, warn below it"
    treatment `kMinVerifiedParticleVersion` already uses elsewhere, per
    chunk type rather than per file version); an architecture
    recommendation (the ribbon/particle hybrid pattern -- minimal
    placement-anchor `extras` unconditional in every `.glb`, full body/
    shape/joint/`PHYV` records in `dump-chunks`'s JSON, `.phys` files
    also accepted directly by `dump-chunks` like `.bone` already is --
    reasoned from `.phys` bodies already being `position`+`boneIndex`
    anchors structurally closer to `M2Ribbon`/`M2Particle` than to
    `.bone`'s flat correction-matrix table), explicitly flagged as a
    recommendation for a real plan-mode design pass, not a decision
    already made; a `src/phys.hpp`/`phys.cpp` data-model sketch mirroring
    `bone.hpp`'s shape; a concrete real-fixture test plan, including an
    honest gap callout that zero committed fixtures currently carry
    `PLYT`/`SPHS`/`BOXS`/`SPHJ`/`PRSJ`/`PRS2`/`DSTJ`/`SHJ2`/`WLJ3`/`REV2`/
    `PHYV` (candidate real corpus paths named for the ones this session's
    sample did find, e.g. `PLYT` in
    `world/expansion07/doodads/8xp_heartofazeroth_prop_floatychain.phys`)
    -- same "real test data was the actual blocker" pattern the particle/
    ribbon session hit, flagged proactively this time rather than
    discovered mid-implementation; and a full doc-sync checklist
    (`M2_COMPLETENESS.md`, `README.md`, `DESIGN.md`, completions tables'
    hand-maintained-gotcha) for whenever implementation actually happens.
  - **Docs**: `DESIGN.md`'s Open work section now also points at
    `PHYS_TODO.md`, alongside `TODO/TODO_correctness.md`/`WIKI_FINDINGS.md`/
    `MULTIROOT_SKELETON_TODO.md`.
  - **Environment note, reconfirmed**: `direnv exec . uv run --python
    tools/venv/bin/python <script>` for every ad hoc analysis pass this
    session (the decoder, the index/bounds cross-check, the `husk info`
    bone-count cross-reference) -- scripts lived in the scratchpad, not
    committed, matching every prior session's convention.
- **Previous state**: Implemented `MULTIROOT_SKELETON_TODO.md`'s Implementation
  plan end to end -- the multi-root-bone-forest → glTF representation gap
  (35% of a real 130k-file corpus, per the previous state's own
  measurement) is no longer a decision-and-survey document, it's real code.
  Requested directly: "start working on the implementation step of this
  file."
  - **`src/gltf.cpp`'s `writeGlbMulti`**: exactly the previous state's
    Option 1 -- when `rootJointNodeIndices.size() > 1`, one
    `tinygltf::Node` (default/identity transform) is synthesized with
    `.children = rootJointNodeIndices`, appended past the end of the
    joint-node range (`meshCount + skeleton->joints.size()`), and becomes
    the sole `scene.nodes` entry standing in for those roots;
    `skin.skeleton` is set to it. Single-root models (`size() <= 1`,
    the overwhelming majority): completely unchanged, verified by the
    full pre-existing test suite passing unmodified. `Skeleton::joints`
    itself was never touched, per the file's own "one invariant that
    must never break" -- the whole change lives inside `writeGlbMulti`'s
    node/scene/skin construction.
  - **Empirically resolved the one thing the Decision section had
    explicitly deferred, not guessed at**: does Blender's glTF importer
    count the synthesized node as a bone? Ran the real fixture
    (`offhand_1h_revendreth_d_01.m2`, 15 bones/10 roots) through
    `husk export` then both the real `gltf_validator` and headless
    Blender by hand before writing any test assertion. Confirmed:
    `gltf_validator` reports 0 errors (`SKIN_NO_COMMON_ROOT` gone,
    previously present); Blender's `bone_count` probe reports exactly 15
    -- the synthesized node is *not* counted as a bone, `skin.joints.size()`
    stays exactly `header.bones.count`. Option 1 and Option 2 are
    confirmed *not* equivalent in practice; Option 1 has no Blender-visible
    downside. Both `MULTIROOT_SKELETON_TODO.md`'s Decision section and its
    design-question-A writeup were updated with this finding rather than
    left as an open hedge.
  - **Tests**: 387 → 394 cases (both `./build/husk-tests` and `ctest`
    green, 1 permanently-inapplicable skip unchanged). New
    `tests/test_gltf.cpp` cases (a `buildMultiRootSkeleton()` fixture, 3
    independent roots): synthetic-node-exists-with-correct-children/
    skin.skeleton/untouched-transform, single-root-output-unaffected
    (explicit regression case, not just "the old tests still pass"), and
    a mixed mesh-nodes-plus-multi-root-skeleton case proving vertex joint
    indices stay raw/unshifted. New `tests/test_conformance.cpp` cases
    (real `testWeaponParticleB()` fixture, gated the same
    `doctest::skip`/`#ifdef HUSK_GLTF_VALIDATOR`/`HUSK_BLENDER` way every
    other conformance case is): the `gltf_validator`
    zero-errors-no-`SKIN_NO_COMMON_ROOT` check, and the Blender
    bone-count-matches-header-exactly check, each gated on a
    `countRealRootBones()` sanity check (parses the real bone array
    directly, independent of husk's own code) so the test fails loudly
    rather than passing vacuously if a future fixture swap ever replaces
    this file with a single-root one. New `tests/test_cli.cpp` cases for
    the two combinations `MULTIROOT_SKELETON_TODO.md` flagged as
    genuinely untested: `--lod all` + a synthetic 3-independent-root
    `.skel` (via the existing `buildSkel` helper, since no real fixture
    combines multi-LOD and multi-root), and `--bones-dir` + the same
    multi-root `.skel`, with the `.bone` file deliberately correcting the
    *last* root joint (not joint 0) to prove `CorrectionSet::joint`
    indices are unaffected by the synthesized node's presence, not just
    "should be unaffected in principle."
  - **Docs**: `src/gltf.hpp`'s `Skeleton`/`writeGlbMulti` doc comments
    (the new synthesized-root behavior, now the authoritative contract,
    not just this TODO file's prose); `DESIGN.md` (new Key design
    decisions bullet, matching this session's own corpus numbers and the
    Blender finding; Open work section's multi-root paragraph rewritten
    from "still open" to "implemented"); `M2_COMPLETENESS.md`'s "Skeleton
    / bone hierarchy" row (note now mentions multi-root synthesis, status
    unchanged at `native — 100%`); `MULTIROOT_SKELETON_TODO.md` itself
    (opening framing now says "Implemented," every Implementation-plan
    step and every now-resolved hazard bullet marked `[DONE]`, the
    Decision section's "still genuinely unverified" paragraph rewritten
    with the real Blender numbers).
  - Nothing else in `src/` touched -- `cmd_export.cpp`/`buildSkeleton`
    exactly as before, per the plan's own step 2.
- **Previous state**: Corrected the framing on `MULTIROOT_SKELETON_TODO.md`'s
  whole premise, did bounded prior-art research, and recorded a real
  decision — Luna, not implemented yet, handing off from here. Prompted by
  a direct question after the previous state's corpus-scale measurement:
  "is it right to call it an issue, if the source material does not have a
  problem with multi root files... we want as close as possible 1 to 1
  representation." Correct catch: multi-root bone forests are legitimate,
  common M2 data (WoW's engine never required a single tree), not a
  defect — the file's whole opening section now states this explicitly
  and judges every design option against "closest possible 1:1 fidelity to
  the source," not "make gltf_validator happy." Bounded web research (2
  searches + 1 fetch, deliberately capped per Luna's own "don't spend too
  much time" instruction) found real prior art: glTF's own spec/tooling
  explicitly anticipates a skeleton root's parent not being a joint itself
  (Khronos issue #1270), and `wow.export` (the established community
  WoW-export tool) has a real, shipped "prefix bones" inclusion toggle for
  this exact shape (v0.2.0) — added as a third design option (filter,
  `wow.export`-style) alongside the two from the previous state's survey.
  Luna then chose directly: **Option 1** (a plain non-joint glTF node
  parenting every real root joint, `skin.skeleton` pointed at it) — the
  spec-anticipated, fully-fidelity-preserving choice, overriding the
  previous state's own "needs empirical Blender verification before
  deciding" recommendation (that verification now happens *during*
  implementation's real test-writing, not as a precondition to choosing).
  `MULTIROOT_SKELETON_TODO.md` restructured around this: a new Decision
  section up top, both design questions marked decided (kept for their
  reasoning, not deleted), a concrete numbered Implementation plan
  (supersedes the old "Recommended first steps"). Nothing in `src/`
  touched this turn — pure documentation, per Luna's own "I'll take over
  from there" close. **Whoever picks this up next should start at
  `MULTIROOT_SKELETON_TODO.md`'s Implementation plan section directly.**
- **Previous state**: Measured `MULTIROOT_SKELETON_TODO.md`'s scope for real,
  corpus-wide, and found the previous state's own framing needed a real
  correction. Requested directly: "generate a script that prints all of
  the multiroot skeleton files into a newline separated txt file... similar
  to ./phys_files_for_exploration.txt." New `tools/
  find_multiroot_skeletons.py` (self-contained, same independent-parse
  discipline `corpus_checks.py` uses -- not calling husk) scanned the full
  real corpus in ~40s: **45,804 of 130,576 `.m2` files (35%) have more than
  one root bone**, written to `multiroot_skeleton_files_for_exploration.txt`
  (repo root, plain newline-separated paths, same format as its
  `phys_files_for_exploration.txt` precedent).
  - **The real correction**: Luna asked directly whether "a lot of models
    have multiroot but pass [gltf_validator] because of skinning
    shenanigans, and a common root fix could theoretically apply to every
    file for correctness" -- confirmed, empirically, not just plausibly.
    `bloodelffemale.m2` (the project's own primary fixture) has **90 root
    bones out of 119** and exports with **zero** `gltf_validator` errors;
    `offhand_1h_revendreth_d_01.m2` has only **10** and *does* trigger
    `SKIN_NO_COMMON_ROOT`. Neither raw root count nor root count restricted
    to vertex-weighted joints explains the difference -- both hypotheses
    directly falsified against real bytes before being written down. A
    real 150-file random sample (from the 45,804), each actually exported
    and checked with the real `gltf_validator` (not a proxy), found only
    **11/150 (≈7.3%) currently trigger the error** -- extrapolated, ~3,300
    files corpus-wide are visibly flagged today, a small fraction of the
    45,804 that are genuinely structurally multi-root. `gltf_validator`'s
    own exact trigger condition wasn't reverse-engineered (flagged as an
    open question, not guessed at) -- what *is* now established is that
    "passes the validator" isn't the same as "has one real joint root,"
    which reframes the whole rework: the fix target is the full 35%, not
    just the ~2-3% currently visible, and testing it only against
    known-currently-erroring files would badly under-scope it.
  - **Docs**: `MULTIROOT_SKELETON_TODO.md` rewritten (opening section, a
    new "Open questions this session didn't chase down," step 1 of
    Recommended first steps marked done) to reflect the corrected,
    corpus-measured scope rather than the earlier 2-fixture/26-file
    estimate.
  - Nothing committed this turn -- the new script, the generated `.txt`
    file, and the `MULTIROOT_SKELETON_TODO.md`/`CLAUDE.md` edits are
    sitting in the working tree, same as the previous state's own
    (already-committed) work being built on.
- **Previous state**: Committed the `CORPUS_TODO.md` work below (single commit,
  `git log` for the message), then wrote `MULTIROOT_SKELETON_TODO.md` — a
  pre-implementation risk survey for the `SKIN_NO_COMMON_ROOT` gap the
  previous state below flagged but didn't fix, requested directly: "this
  would need a more robust workaround... write a todo file for this
  rework, what it could affect, and where it would be likely to fail
  invisibly... attempting to pre-empt failure modes we might miss by
  doing something the original file format didn't do." Pure investigation
  and documentation this turn — no `src/` changes, nothing to test-run.
  - **The core risk, identified and documented as the file's own opening
    section**: `Skeleton::joints`' ordering is raw M2 bone-array indices,
    copied verbatim into glTF `JOINTS_0` (`buildSkinning`), and into
    `EmitterAnchor`/`CorrectionSet`/`JointAnimation`'s own `joint` fields
    — none of these are ever remapped, only bounds-checked. A synthetic
    "common root" node inserted into `Skeleton::joints` itself (rather
    than purely on the glTF-node side, past the end of the real range)
    would silently misattribute every one of those to the wrong bone —
    no crash, no validator error, just visually wrong. Confined the fix's
    entire footprint to `gltf.cpp`'s `writeGlbMulti` for exactly this
    reason.
  - **Grounded against real bytes before writing anything else** (a
    from-scratch bone-array parent-chain parser, not reusing husk's own
    code — same independent-check discipline `WIKI_FINDINGS.md`'s other
    entries use): `offhand_1h_revendreth_d_01.m2` (15 bones, 10 roots —
    a mixed shape, a few real small hierarchies plus isolated bones) and
    `mace_2h_bolvar_d_01.m2` (78 bones, **78 roots** — every bone its own
    tree, no hierarchy at all, consistent with "one bone per particle
    emitter, no relation needed between them"). Real, intentional M2 data
    either way, not corruption — and root count can be the *entire* bone
    count, not "usually 2, sometimes a few," which rules out any design
    that only comfortably handles a small fixed number of extra roots.
  - **Two design questions deliberately left open, not decided**: (a)
    whether the synthetic node joins `skin.joints` as one more real bone
    (identity IBM, appended past every real M2 index) or stays a plain
    non-joint parent node outside the skin — the actual deciding factor
    is empirical (does Blender's importer count it as a bone either way?
    `tests/blender_import_check.py`'s `bone_count` probe would answer
    this directly, but only once a real spike exists to run through it —
    not guessed at this session); (b) whether `tinygltf::Skin::skeleton`
    (currently never set at all) should point at the synthetic node —
    glTF spec text and `gltf_validator`/Blender's actual behavior around
    it weren't checked this session, flagged as genuinely unresearched
    rather than assumed either way.
  - **Other concrete invisible-failure scenarios documented**: single-
    root models must produce byte-identical output (the fix has to be
    strictly gated on `rootJointNodeIndices.size() > 1`, and every
    existing `test_gltf.cpp` skeleton test hard-codes exact node
    counts/indices assuming a single root); `test_conformance.cpp`'s
    exact-match bone-count assertions only run against the single-root
    bloodelf fixture today, so a multi-root regression wouldn't be caught
    by anything currently passing; a stray non-identity transform on the
    synthetic node would silently shift every former-root joint's whole
    subtree; `--lod all` and `--bones-dir` combined with a synthesized
    root are both untested combinations with no fixture today.
  - **Recommended sequencing, in the file itself**: characterize the
    shape more broadly first (the 10 geometry-less-VFX files this
    session's own #1 fix uncovered, not sampled for bone-hierarchy shape
    yet), answer the Blender-bone-count question empirically with a
    throwaway spike before committing to a real implementation, check
    glTF's `skin.skeleton` semantics for real, only then implement --
    gated strictly, in `gltf.cpp` alone, with new tests using the two
    real fixtures above (`testWeaponParticleB()`/
    `testWeaponParticleStress()`, already wired up) through both
    `gltf_validator` and the real headless-Blender probe, not just "no
    crash."
  - **Docs**: `DESIGN.md`'s Open work section now points at
    `MULTIROOT_SKELETON_TODO.md` alongside `TODO/TODO_correctness.md`/
    `WIKI_FINDINGS.md`.
- **Previous state**: Worked `CORPUS_TODO.md` as a punch card — a from-scratch
  grounding of an earlier raw sweep (`HUSK_CORPUS_FINDINGS.md`) across a
  real 130k-file corpus (`/media/luna/data/wow_export`), re-checked against
  actual code and real bytes, with Luna's own DEVELOPER NOTES per item
  giving direction/approval and an explicit bottom-of-file priority order.
  Requested as "use this file as a punch card... prio order in the
  bottom." Every item in the file now has a final disposition — the same
  signal that triggered `VERIFICATION_IDEAS.md`'s deletion in an earlier
  session — but `CORPUS_TODO.md` itself was left in place rather than
  deleted unilaterally: it's Luna's own working punch-card doc with her
  manual annotations throughout, a different situation from a purely
  generated scratch survey.
  - **#1 (empty-primitive crash, 3,807 real files, highest-priority
    item) — fixed.** `buildMaterialsAndPrimitives`
    (`src/cmd_export.cpp`) used to manufacture one glTF primitive with
    empty `indices` for a genuinely geometry-less `.skin` (real corpus
    shape: pure particle/ribbon VFX models, 0 vertices at the M2 level,
    not just an empty batch table) — glTF has no valid "primitive with
    zero indices" representation, so every one of these failed outright.
    Went with the "zero meshes" design Luna approved directly: skip
    adding a `NamedMesh` for a LOD tier that resolves to zero primitives
    (both the whole-file-empty case and the rarer per-submesh
    `indexCount == 0` case), and relaxed `gltf::writeGlbMulti`
    (`src/gltf.cpp`/`gltf.hpp`) to accept an empty `meshes` list as long
    as a real skeleton (≥1 joint) exists to fall back to -- the model's
    skeleton and ribbon/particle emitter anchors (already unconditional,
    prior session) still export with zero mesh nodes; `Error`s outright
    only when both are empty (nothing to export at all). Verified: a
    random 25-file sample of real `FAIL-0001` corpus files all had 0
    vertices at the M2 level (confirms the dominant-shape assumption),
    all 25 now export cleanly and pass `gltf_validator` with 0 errors.
    **Real side-finding, out of this item's own scope:** 10 of those 25
    (+1 more checked individually, 26 total) hit a *different*,
    pre-existing `gltf_validator` error (`SKIN_NO_COMMON_ROOT`, "Joints
    do not have a common root") -- the same multi-root-bone-hierarchy gap
    `DESIGN.md`'s Hazards section already documented for 2 of 4 real
    weapon fixtures, here showing up in ~38% of geometry-less VFX models
    too. Not fixed this session (unscoped, needs a real bone-hierarchy-
    reconciliation design) -- flagged in both `CORPUS_TODO.md` and here
    for a future session.
  - **#3b (`findSameBasenameSkins` prefix-collision bug) — fixed.** A
    same-basename numeric-suffix `.skin` match used to accept a digit
    run of any length, which a real corpus scan found genuinely
    ambiguous whenever one model's basename is itself a numeric-suffix
    prefix of a sibling model's basename in the same directory (real
    files: `mogu_library_crate_10.m2` vs. `mogu_library_crate_1.m2`,
    `vebgrs10.m2` vs. `vebgrs1.m2`/`vebgrs11-17.m2`, `vebbsh10.m2` vs.
    `vebbsh1-9.m2`) -- the shorter model's own real 2-digit-suffix file
    parses as a spurious 1-digit match for the longer model's basename
    too, and used to win `std::sort`'s lexicographic tie-break over the
    correct file. Fixed by preferring an exactly-2-digit suffix match
    (WoW's own real convention) whenever at least one exists for a given
    basename, discarding 1-digit/3+-digit matches as collisions -- kept
    as a fallback, not a hard reject, when no 2-digit match exists at
    all (no real-corpus evidence either way for that shape, and a hard
    reject risks a new false-negative regression). Checked against the
    real collision directory directly (`world/nodxt/detail/`, all 26
    `vebgrs`/`vebbsh` siblings) rather than a full corpus walk -- every
    genuine skin there resolves correctly under the new rule, no
    exceptions. Also implemented the doc's second approved idea: the
    vertex-out-of-range error message now reports the *count* of
    out-of-range indices and the *worst offender*, not just the first
    one iteration happened to hit (real wrong-`.skin` pairings reference
    hundreds of out-of-range indices, not one -- the old message made two
    identical bugs look like different shapes purely as an iteration-order
    artifact). Verified against both real confirmed-collision files:
    `mogu_library_crate_10.m2` now resolves to `...crate_1000.skin` (68
    vertices, matches the doc's own figure) and `vebgrs10.m2` to
    `...vebgrs1000.skin` (8 vertices), both exporting cleanly.
  - **#6 (`dump-chunks` `WFV3` short-chunk variant, 9 real files) —
    fixed.** `dumpWfv3` (`src/cmd_dump.cpp`) assumed every `WFV3` chunk
    is a fixed 80-byte struct; all 9 real files carrying one (1
    Shadowlands "Maw"-zone doodad, 8 Nazjatar-zone water-effect doodads
    -- corrected from this doc's own file-list, which had all 9
    mis-attributed to the Maw zone alone) are consistently 64 bytes,
    missing exactly the trailing `unk1`-`unk4` floats. Fixed by reading
    those four conditionally on `c.size >= 0x50`, emitting `null` for
    the short variant (same "genuinely absent, not a parse failure"
    treatment `dumpTextureWeights`'s optional fields already use)
    instead of throwing. **New, previously-undocumented-on-the-wiki
    finding** (`WIKI_FINDINGS.md` §8, new): the wiki's own `WFV3` struct
    listing is unconditionally 80 bytes with no mention of a shorter
    variant at all -- every field before `unk1` decodes cleanly on all 9
    real files, the chunk simply ends exactly 16 bytes short of the
    documented size, every time. Verified: all 9 real files now dump
    cleanly, `unk1`-`unk4` all `null` as expected.
  - **#4 (duplicate-timestamp animation keyframes, 5 real files) --
    fixed.** `checkKeyframesWellFormed` (renamed
    `repairDuplicateTimestampsAndValidate`, `src/cmd_export.cpp`) used
    to reject *any* non-strictly-increasing keyframe timestamp
    identically, whether genuine disorder (a timestamp that actually
    decreases -- real corruption) or an exact duplicate (real, shipped
    Blizzard data: an authored "hard cut" pose, always on `rotation`,
    confirmed on all 5 real files named in the doc -- 2 world bosses,
    2 base character rigs, 1 world doodad). Chose **nudge over collapse**
    (the doc's own two options, left an open decision pending a
    reader cross-check that turned up nothing specific to M2): collapsing
    either keyframe would silently discard one of the two real authored
    values, while nudging the later duplicate's timestamp forward 1ms
    (cascading, so a run of N duplicates spreads out N-1ms apart) keeps
    both -- correct under both glTF LINEAR and STEP sampler
    interpolation, and general glTF-authoring precedent (Blender's own
    exporter deliberately inserts near-zero-gap duplicate keyframes to
    force STEP-like behavior) supports nudge as the standard shape for
    this exact problem. A genuinely *decreasing* timestamp still throws,
    classified against each keyframe's *original* (pre-repair) timestamp
    -- comparing against an already-nudged value would misfire the
    disorder check on a cascading run's second entry, a real bug caught
    while implementing (fixed before it reached any test). Verified
    against all 5 real files: all export cleanly now, `gltf_validator`
    shows zero animation-sampler-related errors on any of them (remaining
    errors on 2 of the 5 are pre-existing/unrelated --
    `ACCESSOR_JOINTS_INDEX_DUPLICATE`/`SKIN_NO_COMMON_ROOT`, same classes
    already documented elsewhere in this repo).
  - **#5 (`materialIndex` out of range, investigated further per Luna's
    request to check more examples before declaring unfixable) --
    confirmed unfixable, now with much stronger evidence.** Original
    doc spot-checked one file; this session checked all 16 real files
    identifiable via current `failures.txt`/`failure_codes.txt`
    (renumbered since the doc was written). All 16 confirm the exact
    same striking, perfectly uniform signature: `materialIndex` is
    always **exactly** the model's own material count (never further out
    of range), and `husk info` confirms each file's own material array
    really does stop one short. No sibling-basename digit collision on
    any of the 16 (all end in letter race/gender codes), so independent
    of #3b's bug -- confirmed, not assumed, since #3b's fix was already
    live when these were re-checked. No wiki-documented sentinel value
    explains an "index == count" `materialIndex` either. Genuinely bad/
    mismatched shared batch data across collections/recolor item
    variants -- not fixable in husk. The doc's ~7 additional
    `textureComboIndex` cases couldn't be re-verified the same way --
    `failures_unique.txt` strips file paths during anonymization, and no
    example path is available from current tooling output -- flagged
    honestly as unverified (structurally identical shape, almost
    certainly the same root cause, but not re-confirmed) rather than
    quietly assumed.
  - **#2's remaining half (missing spell-effect/item `.skin` files) --
    explored, confirmed genuinely unfixable in husk, README note added.**
    Requested as "explore if possible to fix... or if genuinely no way to
    find the correct one, add explanation note in README." Widened the
    sample first and found the doc's own "spell-effect models" framing
    undersold the real scope -- the current `FAIL-0003` bucket also
    includes ordinary item pieces (`item/objectcomponents/shoulder/`,
    `.../collections/`). Checked two real files (the doc's own spell
    example, plus a shoulder-armor item) the same rigorous way: real
    geometry confirmed via `husk info`, then a *targeted* `find` (not
    another 130k-file walk -- see the environment note below) for each
    declared `SFID` FileDataID, decimal-to-hex converted, checked both
    next to the model (already known absent) and in `_unresolved/`
    (`wow_export`'s own "extracted but couldn't place" bucket,
    `FILE<8-hex>.dat` naming -- the one place a "misplaced, not really
    missing" file would surface) -- zero matches anywhere, for every
    FileDataID on both files. Genuinely absent from the extraction, not
    a husk-side false negative. Added a paragraph to `README.md`'s
    `--skin`/`auto` section explaining this is a known extraction-
    completeness gap, not a husk bug, and that re-running the extraction
    tool (not husk) is the only real fix.
  - **Tests**: 378 → 387 cases (`./build/husk-tests`: 387/387 + 1
    permanently-inapplicable skip; `ctest`: 388/388). New cases per fix
    above in `tests/test_gltf.cpp` (empty-meshes-with-skeleton,
    empty-meshes-without-skeleton-throws) and `tests/test_cli.cpp`
    (basename-collision reproduction both directions, out-of-range-count
    error message, single + 3-way-cascading duplicate-timestamp repair),
    `tests/test_dump.cpp` (`WFV3` short-variant round-trip).
  - **Docs**: `CORPUS_TODO.md` (every item's own DEVELOPER NOTES section
    now has a `[DONE]`-tagged disposition, matching this repo's existing
    "fixed items get a disposition, not silently dropped" convention),
    `WIKI_FINDINGS.md` (new §8, `WFV3`'s undocumented short variant),
    `README.md` (the `.skin`-not-found extraction-gap note above),
    `DESIGN.md` (3 new Key design decisions bullets: zero-meshes,
    2-digit-suffix preference, duplicate-timestamp nudge-repair),
    `M2_COMPLETENESS.md` (2 rows -- mesh geometry, animation tracks --
    annotated with the new edge-case handling, no status-symbol changes
    since both were already at `native — 100%`).
  - **Environment note, reconfirmed and reinforced**: started an
    unscoped full-130k-file `os.walk` Python scan (checking
    same-basename-suffix-length distribution for #3b) before Luna
    interrupted directly -- "you do realize there is 130 THOUSAND m2
    files in that tree, which is exactly why i provided the exact
    failures.txt file to map to relevant files." Stopped the background
    task immediately, rescoped to the specific directories/files
    `failures.txt`/`failure_codes.txt` already flagged for the rest of
    the session (every subsequent investigation in this session --
    #5, #2's remainder -- used this same targeted approach, not another
    broad sweep). `direnv exec . uv run --no-project python3 <script>`
    remains the sanctioned ad hoc-analysis pattern for the cases that
    did need a script (reading a `gltf_validator` JSON report), scripts
    left in the scratchpad, not committed.
- **Previous state**: Particles/ribbons (`M2Particle`/`M2Ribbon`) — the single
  biggest remaining visual-identity gap this tool had (weapon glow trails,
  magic/fire/smoke) — went from 0%/static-fields-only to fully parsed:
  every static field, plus every M2Track/FBlock animation curve, for both
  types. Requested as "what's the next biggest step in M2 coverage to WoW
  feel/look" with the user's own hunch (particles and ribbons) confirmed
  and pursued.
  - **Real test data was the actual blocker, and got solved mid-session.**
    Every M2 fixture previously in `test_data/` (blood elf female, base +
    HD) has zero particle/ribbon emitters. Luna extracted the game's full
    weapon set into `test_data/item/objectcomponents/weapon/` (gitignored,
    1.6G, 4112 `.m2` files, same "real, personally-owned extraction, never
    committed" convention as the character fixtures) mid-session, in
    response. A scan found 1270 files with real particle/ribbon data, all
    sampled at M2 version 272/274 (Cataclysm+) — four fixtures selected
    and now permanently referenced by `tests/test_data_paths.hpp`
    (`kWeaponRibbon` = Ashbringer, 3 ribbons/0 particles;
    `kWeaponParticleA`/`B` = two combined ribbon+particle weapons;
    `kWeaponParticleStress` = a 64-particle-emitter mace).
  - **Architecture went through a real design pivot, twice, before
    implementation** (both via `EnterPlanMode`/`AskUserQuestion`, not
    silently decided): first draft put everything in glTF `extras`
    (matching `.bone`-correction/geoset/texture-transform precedent) —
    the user pushed back, asking whether an auxiliary file (the BLP→PNG
    precedent) fit better given `dump-chunks` already exists for "M2 data
    with no glTF slot." Second draft routed everything through
    `dump-chunks` — but that command's own stated scope (`src/
    cmd_dump.cpp`'s doc comment, its usage text) is Legion+ chunk tags
    only, and `M2Ribbon`/`M2Particle` are core `MD20` header arrays
    present in *every* version, a real, narrower boundary than "no glTF
    slot." Landed on a hybrid, confirmed by the user directly ("point it
    at a dir, get generic file equivalents... completeness and
    automatability are the key"): a **minimal placement anchor**
    (id/bone/position, `gltf::Skeleton::EmitterAnchor`) unconditionally in
    the `.glb` skin's `extras`, and the **full record** (every field,
    every resolved curve) in `dump-chunks`'s JSON output — which required
    deliberately broadening that command's own documented scope (usage
    text and doc comment both rewritten to state it explicitly, not left
    to imply chunk-tags-only while secretly doing more).
  - **Offset derivation was hand-done, not guessed, and cross-checked
    twice** — the wiki gives `M2Particle`'s explicit hex offsets only up
    to `childEmittersModelFilename`; everything after (all
    version-conditional branches: late-BC field-width change, Cata's
    `multiTexScale`, Wrath's extra floats and `FBlock`-based curves) was
    summed field-by-field by hand. That derivation landed on exactly the
    wiki's own independently-stated total record size (476 bytes default,
    492 with the Cata+ wrapper) without being fudged to fit — a real
    independent check, not circular. Then cross-checked a second way,
    against real bytes (`mace_2h_bolvar_d_01.m2`, 64 particles): decoded
    colors form a genuine fire/ember gradient, alpha/scale curves are
    clean envelopes, and the `MultiTexture` flag bit correlates exactly
    with non-zero `multiTexScale` — see `WIKI_FINDINGS.md` §6 for the full
    writeup, including a real bug an early ad hoc verification script had
    (skipped the real per-sequence `M2Track` inner-array indirection,
    producing a plausible-but-wrong near-zero alpha value instead of the
    real resolver's correct 0.8) that the "verify against real data before
    trusting a claim" discipline caught before it reached any shipped
    code. Also found and confirmed independently: `FBlock` (the Wrath+
    particle color/alpha/scale/UV curve shape) timestamps are `uint16_t`,
    not the `uint32_t` a real `M2Track` uses — matches the wiki's own "the
    timestamps are shorts" text and decodes to a clean monotonic
    `0..0x7FFF` run against real bytes; interpreted as likely a normalized
    lifetime fraction (hypothesis, not confirmed against an authoritative
    source) but exposed raw regardless, per this project's own "don't
    guess at semantics" discipline.
  - **New shared infrastructure** (`src/m2.hpp`/`m2.cpp`): `readU8`,
    `resolveFloatTrackSequence`/`resolveFloatGlobalSequenceTrack` (a real
    named function, not another hand-duplicated Vec3/Quat-style copy —
    `M2Particle` alone has ~10 real `M2Track<float>` fields, past this
    codebase's own "third occurrence earns an abstraction" bar),
    `resolveRawIntTrackSequence`/`resolveRawIntGlobalSequenceTrack`
    (`elementSize`-as-runtime-parameter, matching `checkInnerArrayFits`'s
    own existing style, for the lower-occurrence uint8_t/uint16_t/fixed16
    cases that didn't individually earn a named function), and
    `FBlockMeta`/`resolveFBlockVec3`/`Vec2`/`Fixed16`/`Uint16` (a
    private templated `resolveFBlockGeneric` helper backing all four —
    the one place this session used a template, since the alternative was
    four near-identical hand-copies of a flat, no-indirection curve
    reader, more duplication than even this codebase's own
    duplication-tolerant style usually accepts).
  - **`m2::Ribbon`/`m2::ParticleEmitter` (`src/m2.hpp`)**: Ribbon gained
    `textureIndices`/`materialIndices` lookup arrays, 6 new track-offset
    fields, and the Wrath+ trailing `priorityPlane`/`ribbonColorIndex`/
    `textureTransformLookupIndex`. `ParticleEmitter` is new outright —
    every Cata+-shape field, ~30 in total, gated to a new
    `kMinVerifiedParticleVersion = 272` (same "verified floor, warn below
    it" policy `kMinVerifiedRecordStrideVersion` already established for
    Bone/Sequence/Ribbon, just a newer floor since `M2Particle`'s own byte
    layout genuinely changed at Cataclysm, unlike those three).
  - **`src/cmd_info.cpp`**: ribbon printout extended (track/lookup
    counts); new particle one-line-per-emitter summary, gated the same
    way, with a loud warning (matching the existing below-Wrath one)
    for real `particle_emitters` data below Cataclysm.
  - **`src/cmd_dump.cpp`**: `dumpEmitters` (+ `writeRibbon`/`writeParticle`/
    `writeTrackCurve`/`writeFloatTrack`/`writeVec3Track`/`writeRawIntTrack`/
    `writeFBlockCurve`) — full JSON, written unconditionally (before the
    existing `header.chunked` early-return, which now only gates the
    Legion+ chunk-tag section, not the whole command) so pre-Legion flat
    files still get real `ribbon_emitters`/`particle_emitters` output.
  - **`src/gltf.hpp`/`gltf.cpp`**: `Skeleton::EmitterAnchor` (one shared
    struct for both `ribbonAnchors`/`particleAnchors` — structurally
    identical, so not duplicated into two types) serialized as
    `ribbon_emitters`/`particle_emitters` keys on the same `skinExtras`
    object `bone_correction_sets` already uses (verified all three
    coexist without clobbering); `writeGlbMulti` gained the matching
    out-of-range-joint validation `correctionSets` already had.
  - **`src/cmd_export.cpp`**: unconditional (no new CLI flag, unlike
    `--bones-dir`) — builds ribbon/particle anchors right after the
    `--bones-dir` block, reusing the already-parsed `header`/`blob`, with
    a real bug caught by the compiler, not by inspection: the first
    attempt aggregate-initialized `EmitterAnchor` directly from
    `m2::Vec3`/`m2::Ribbon::position` without the existing `toGltf()`
    conversion helper — `m2::Vec3` and `gltf::Vec3` are distinct
    aggregate types (no implicit conversion), so it failed to compile
    rather than silently skipping the Z-up→Y-up remap every other
    exported position already goes through. Fixed by using `toGltf()`,
    same as every other position in this pipeline.
  - **Tests**: 337 → 376 cases. New `test_m2.cpp` cases for every new
    resolver (`resolveFloatTrackSequence`/`resolveRawIntTrackSequence`/
    `resolveFBlockVec3`/`Vec2`/`Fixed16`/`Uint16`, global-sequence variants,
    bounds-checking throws), `parseRibbons`'s new fields, and
    `parseParticles` (happy path, extra fields, empty, out-of-bounds).
    New `test_gltf.cpp` cases for `EmitterAnchor` round-trip (present/
    absent/out-of-range-throws/coexists-with-correctionSets). New
    `test_dump.cpp` cases for the JSON output shape, plus a real-byte-
    offset synthetic fixture on a flat (non-chunked) file proving
    `ribbon_emitters`/`particle_emitters` aren't chunk-gated. New
    `test_cli.cpp` cases for the `kMinVerifiedParticleVersion` warning.
    New `test_integration.cpp` real-data cases (`doctest::skip`-gated on
    the new weapon fixtures): exact ribbon/particle anchor counts against
    all four real files via tinygltf, plus a `dump-chunks` NaN/finite
    sanity check on the 64-particle stress file. Both `./build/husk-tests`
    (376/376, 1 permanently-inapplicable skip) and `ctest` (377/377) green.
  - **Verification discipline**: a `gltf_validator` sweep across all four
    real exports found one pre-existing "Joints do not have a common
    root" error on two of the four weapon models — confirmed via
    `git stash`/rebuild/re-export against the unmodified baseline that
    this predates the session entirely (this session's diff never touches
    joint-parent assignment), then `git stash pop`/rebuilt/re-verified
    376/376 green before continuing, rather than assuming.
  - **Docs**: `README.md` (format matrix row rewritten from 🚧 to 📖, `husk
    info`/`dump-chunks` usage sections, a new roadmap-stage-6 paragraph),
    `M2_COMPLETENESS.md` (Ribbons/Particles rows), `DESIGN.md` (new Key
    design decisions bullet on the anchor/full-data split and why,
    Boundaries/data-flow bullets, the flag-gating table), `WIKI_FINDINGS.md`
    (new §6: the offset derivation, the `FBlock`-`uint16_t`-timestamp
    finding, the real bug an early verification script had),
    `TODO/TODO_correctness.md` (former item 1, particles, removed outright per
    this file's own "fixed items get removed" convention — not marked
    `[Fixed]` — remaining items renumbered 2-5 → 1-4, every
    `TODO/TODO_correctness.md #N` cross-reference across `src/`/`tests/`
    grep-verified and updated to match, same careful-renumbering
    precedent the AFSB removal already set).
  - **Environment note, reconfirmed**: `direnv exec . uv run --no-project
    python3 <script>`, scripts written to files in the scratchpad rather
    than passed inline (`python3 -c ...`), per explicit instruction this
    session — inline `-c` invocations otherwise prompt for confirmation
    on every single iteration, which adds up fast during real-data
    byte-level verification work like this session's offset derivation.
- **Previous state**: `VERIFICATION_IDEAS.md`'s survey (source-M2-counts vs.
  exported-glb vs. Blender-readback cross-checks) went from "none of this
  is implemented" to cases 1/2/3/5 all real, in exactly the file's own
  triviality-ranked order (case 4 stayed deliberately skipped, per its own
  reasoning). Requested as "implement the rest of the verification ideas
  findings, in order of triviality." Once every case had a final
  disposition, `VERIFICATION_IDEAS.md` was deleted outright in a same-session
  follow-up (initially left in place with `[IMPLEMENTED]` tags and
  duplicated writeups — a real inconsistency with this project's own
  stated punch-list convention, caught by Luna asking "did you update it
  according to that?" rather than caught proactively) — its survey's job
  (decide what to build) was complete, every real fact already lived in
  its permanent home (`tests/test_conformance.cpp` comments,
  `WIKI_FINDINGS.md` §5, `README.md`, `DESIGN.md`, `M2_COMPLETENESS.md`),
  exactly the situation `DESIGN_CHANGES.md` was in when *it* got deleted.
  Every cross-reference to the file (source comments included, not just
  docs) got repointed rather than left dangling.
  - **Case 1 (vertex count) + case 2 (bone count)**: exactly as
    scoped — two `CHECK`s added to `tests/test_conformance.cpp`'s existing
    Blender `TEST_CASE`, comparing `m2::parseHeader(...)`'s own
    `vertices.count`/`bones.count` against Blender's/tinygltf's readback.
    Getting these *exact* (not `> 0`) surfaced a real, previously-invisible
    contamination bug in `tests/blender_import_check.py`: Blender's
    `--factory-startup` scene (default Cube/Camera/Light) survives into the
    probe unless cleared first, and `bpy.ops.import_scene.gltf`'s own
    `armature_display()` creates a real 42-vertex Icosphere mesh object per
    armature import (a bone custom-shape widget, parked in a hidden
    collection but still a real `bpy.data.objects` entry) unless
    `disable_bone_shape=True` is passed — found by writing the exact-match
    assertion and getting `8111 == 8061` instead of a pass, not by
    inspection. Both fixed in the probe script before either `CHECK` could
    hold.
  - **Case 3 (bounding box)**: the file's own "tolerance match" premise
    was wrong, found by actually computing both sides against real data
    (both `bloodelffemale.m2` and `bloodelffemale_hd.m2`) before writing
    the assertion rather than after — the header's `bounding_box` runs
    roughly 2x–4x the bind-pose mesh's own extent per axis, consistent
    with it covering the model's full *animated* range rather than a tight
    rest-pose fit (documented as a hypothesis, not confirmed against an
    authoritative source — `WIKI_FINDINGS.md` §5, new). Shipped the
    corrected, still tolerance-free invariant instead: the bind-pose
    mesh's own AABB is fully *contained* inside the header's box, per
    axis, after the same Z-up→Y-up remap (`transformedM2BoundingBox`,
    remapping all 8 corners — the axis swap negates one component, so
    naively pairing `zUpToYUp(min)`/`zUpToYUp(max)` would silently produce
    an inside-out box on that axis). Verified the check itself actually
    catches a regression, not just passing vacuously: temporarily
    perturbed `cmd_export.cpp`'s `toGltf` by +50 units on X, confirmed the
    new `TEST_CASE` fails with the exact expected numbers, reverted.
  - **Case 5 (collision mesh)**: the biggest piece — collision data used
    to be `Array` descriptors only (`husk info` counts, nothing
    dereferenced). New `m2::parseVec3Array`/`m2::parseCollisionMesh`
    (`src/m2.hpp`/`m2.cpp`, unit-tested in `tests/test_m2.cpp`) dereference
    `collisionPositions`/`collisionIndices`/`collisionFaceNormals` into
    real data; `cmd_export.cpp` writes it as one more `gltf::NamedMesh`
    (positions via the existing `toGltf`; per-vertex normals *approximated*
    by averaging each vertex's adjacent face normals, since the source is
    one normal per triangle, not per vertex — acceptable since a collision
    mesh isn't shaded, this only satisfies `gltf::Mesh`'s own same-length
    invariant with real data), tagged via new `gltf::NamedMesh::isCollision`
    → `{"collision": true}` in that node's glTF `extras`. Real, unambiguous
    glTF translation (unlike geoset selection/`.bone` corrections/texture
    transforms, which stay `extras`-only because no such translation
    exists) — the geometry itself is native, only the "don't draw this"
    purpose tag is `extras`.
    - **One real API relaxation this forced**: `gltf::writeGlbMulti`
      previously required *every* `NamedMesh` entry to be skinned whenever
      any shared skeleton was in scope (`hasSkeleton && mesh.skinning
      .size() != n` → unconditional `Error`) — too strict for an unskinned
      collision mesh sharing a skinned render mesh's armature. Now each
      entry independently opts in (non-empty, matching-length
      `mesh.skinning`) or out (empty — no glTF `skin` reference on that
      node, not deformed by the armature); the real error case (skinning
      *present* but the wrong length) still throws. Two existing
      `tests/test_gltf.cpp` cases whose whole premise was "mixed
      skinned/unskinned entries must throw" got rewritten (their premise
      is now the supported case) rather than deleted, plus two new cases
      proving both the new positive path and that the real error case
      still fires.
    - `tests/blender_import_check.py` gained `collision_mesh_count`/
      `collision_mesh_vertex_count`/`collision_mesh_triangle_count` probes
      (found via the `collision` extras tag, not by name), checked exactly
      against `header.collisionPositions.count`/`header.collisionIndices
      .count / 3` in `test_conformance.cpp` — small enough (8 positions,
      12 triangles for the real fixture) that exact match is realistic,
      no tolerance needed, pure count/topology.
    - **One real regression this surfaced and fixed in the same pass**:
      `cmd_export.cpp`'s own "N LOD tier(s)" summary print used to key off
      `namedMeshes.size()` directly — with a collision mesh now always
      appended when present, that over-counted by one and would have
      mislabeled it as another LOD tier. Fixed by capturing
      `renderMeshCount` before the collision entry is appended, used for
      both the branch decision and the per-entry print loop.
  - **Case 4 (sequences)**: left alone, exactly as the file's own
    reasoning says — the metric needs to change shape (a resolved/
    skipped/aliased breakdown) before a comparison would mean anything,
    not a suspected bug.
  - **Verification discipline throughout**: every premise got checked
    against real data (`bloodelffemale.m2`/`bloodelffemale_hd.m2`) before
    being written into an assertion, not assumed from the survey doc's own
    text — this is what caught case 3's wrong premise and case 1/2's
    Blender-importer contamination, both invisible from reading code alone.
  - **Tests**: 338 → 345 cases (5 in `test_m2.cpp` for
    `parseVec3Array`/`parseCollisionMesh`, 1 new `test_conformance.cpp`
    bounding-box `TEST_CASE`, 1 new `test_gltf.cpp` mixed-skinning case;
    2 more `test_gltf.cpp` cases rewrote their premise without changing
    count). Both `./build/husk-tests` and `ctest` green (346 total
    including 1 permanently-inapplicable skip).
  - **Docs**: `VERIFICATION_IDEAS.md` deleted outright once every case had
    a final disposition (see this entry's own opening paragraph for why —
    the punch-list convention this repo already uses for
    `TODO/TODO_correctness.md`/`DESIGN_CHANGES.md`, not additive `[IMPLEMENTED]`
    tags), its content folded into: `WIKI_FINDINGS.md` (new §5, the
    bounding-box-isn't-tight finding, tagged hypothesis-confidence since
    the *why* isn't confirmed against an authoritative source), `README.md`
    (Collision/physics format-matrix row bumped from 🚧 to 📖, Testing
    section's Conformance paragraph rewritten), `DESIGN.md` (new Key
    design decisions bullet for the collision-mesh/`writeGlbMulti`
    relaxation, Testing architecture section gained the previously-missing
    4th "Conformance" tier), `M2_COMPLETENESS.md` (Collision & physics
    rows bumped to `full`/`native`/`native — 100%`), and self-contained
    comments in `tests/test_conformance.cpp`/`tests/blender_import_check.py`/
    `src/cmd_export.cpp` (every one of those files' comments used to point
    at `VERIFICATION_IDEAS.md` by name — all repointed rather than left
    dangling once the file was gone).
  - **Environment note, reconfirmed**: `direnv exec . uv run --no-project
    python3 <script>` for ad hoc byte-level scratch analysis (this
    session's minimal-glTF/Blender-object-introspection scripts lived in
    the scratchpad, not committed) — used this session to isolate the
    Blender Icosphere/Cube contamination down to its exact source
    (`io_scene_gltf2/blender/imp/node.py`'s `armature_display()`) before
    trusting the fix, not just patching around the symptom.
- **Previous state**: `TODO/TODO_correctness.md`'s former #1 — `.skel`-sourced
  external `.anim` files' undocumented `AFSB` chunk shape, the single
  biggest remaining animation gap (essentially 0% external-animation
  coverage for any modern character model) — is now **cracked and fully
  resolved**, not just detected-and-skipped. Session ran autonomously
  overnight per explicit standing permission (read-only web search
  pre-approved; no new flake packages, since no one was available to
  approve them) picking up right after the `--bones-dir` work above.
  - **Prior-art search first, properly exhausted before guessing.**
    `WebSearch`/`WebFetch` against wowdev.wiki (direct fetches 403 — same
    bot-blocking this project already knew about, no local proxy available
    this session), GitHub code search, `warcraft-rs`/`wow.export`/
    `WoWDBDefs` repos, and a couple of WoW-modding forums. Found only a
    *semantic* confirmation (wowdev.wiki's own indexed summary: `AFSA` =
    attachment animation, `AFSB` = bone animation) — no byte-level struct
    anywhere reachable. Moved to from-scratch analysis once that was
    genuinely dry, not before.
  - **The crack, in one sentence: `AFSB` isn't a new format at all.** A
    full 104-file chunk survey of `bloodelffemale_hd_*.anim` (correcting an
    earlier claim in `WIKI_FINDINGS.md` §2 that `AFM2`'s stub is always 64
    bytes — it's actually 16–1344, always a multiple of 16) found `AFSB`'s
    first bytes are a clean, monotonic keyframe-timestamp run (0 up to the
    sequence's own `duration`, in ms) — not the "per-bone offset table" an
    earlier shallow peek guessed. Cross-referencing `bloodelffemale_hd.skel`'s
    own `SKB1` bone records against the real `SKS1` sequence array (mapping
    each `.anim` filename's `<animId>-<subId>` to its `SKS1` position) found
    that `src/m2.hpp`'s own doc-comment claim — "every M2Track [a `.skel`
    bone points at] is expected to be genuinely empty" for external
    sequences — is simply wrong: **211 of 245 real bones have non-zero
    per-sequence `(count, offset)` tuples**, and for real bone/sequence
    pairs, that `offset` lands *exactly* on a clean timestamp run inside
    that sequence's own `.anim` file's `AFSB` payload. `husk::m2::
    trackSequenceInnerArrays`/`resolveVec3TrackSequence`/
    `resolveQuatTrackSequence` (unchanged, existing code — the same
    mechanism already used for `AFM2`-external files via their
    `externalDataBlob` parameter) needed zero new parsing logic; the value
    region past each timestamp run (byte length padded to the next multiple
    of 16, confirmed by the next track's offset always starting exactly
    there) decodes as a raw 12-byte `C3Vector` (translation/scale) or the
    existing 8-byte `M2CompQuat` decoder (rotation) — every decoded
    rotation quaternion comes out unit-length to 4 decimal places, every
    translation curve smooth and finite.
  - **Verified three independent ways**, not just "the numbers looked
    consistent": (1) a full self-consistency sweep across all 54
    non-`_sdr` `bloodelffemale_hd*.anim` files (every bone × every track ×
    its own matching sequence) found zero bounds/monotonicity/finiteness
    problems; (2) `husk export` itself, pointed at the real `--anim`
    directory, now reports **336 real animation clips** for
    `bloodelffemale_hd.m2` (up from whatever inline/global-sequence-only
    count was possible before); (3) the Khronos `gltf_validator` reports
    zero *new* errors on that export (the fixture's own pre-existing,
    unrelated `JOINTS_0` duplicate-value issue is identical with or
    without `--anim`, confirmed by diffing against a `--anim`-less
    export); (4) Blender's own glTF importer, run headlessly the same way
    `test_conformance.cpp` already does, independently reports **336
    actions** — an exact match from a completely separate glTF
    implementation.
  - **Code change was small and surgical, given how much existing code
    already generalized correctly**: `src/cmd_export.cpp`'s
    `buildAnimations` — the `AFSB`-peek branch that used to `continue`
    (skip) now extracts `AFSB`'s own chunk payload directly as
    `externalDataBlob` (taking priority whenever both `AFM2` and `AFSB` are
    present, since `AFM2`'s stub still isn't real data — confirmed via the
    same "claims more keyframes than this blob holds" bounds error a prior
    session already found); the `AFM2`-only and neither-chunk-present
    branches are otherwise unchanged. No changes at all to `src/m2.cpp`'s
    resolution functions.
  - **Tests**: rewrote the two `test_cli.cpp` cases that used to assert
    "`AFSB` present → no animation clip" (that assertion is now false) into
    cases asserting a real clip *is* produced — one plain `AFSB`-only file,
    one with a genuine `AFM2` stub alongside real `AFSB` data (proving
    priority) — plus a new third case for the one remaining skip path
    (neither `AFM2` nor `AFSB` present, an unrecognized future shape).
    New `tests/test_integration.cpp` case (`HUSK_TEST_ANIM_DIR`-gated, new
    env var + `testAnimDir()`/banner line, defaults to the same directory
    the `.skel` fixtures already live in since that's where the real
    `.anim` files sit) runs the real 104-file corpus end to end and checks
    every decoded rotation/translation keyframe via tinygltf — asserts a
    conservative `> 100` clip lower bound, not the exact 336, so it doesn't
    become a silent tripwire if the fixture set changes slightly. Full
    suite: 337 → 337 cases (no new cases needed beyond the 3 rewritten + 1
    real-data one — the fix reuses existing resolution machinery, not new
    surface), but assertion count went 368,997 → 7,164,311 (the new
    real-data test checks every keyframe across all 336 clips). Both
    `./build/husk-tests` and `ctest` green.
  - **Docs**: `WIKI_FINDINGS.md` §2 rewritten with the corrected `AFM2`-size
    claim and a full "Follow-up: cracked" section (the receipts above, in
    more detail); `TODO/TODO_correctness.md`'s former item 1 removed outright
    (per this file's own "fixed items get removed, not marked `[Fixed]`"
    convention) and items 2-6 renumbered to 1-5 — a deliberate exception to
    "don't renumber, it touches live code strings," done carefully with a
    full grep-verified sweep across every `TODO/TODO_correctness.md #N`
    reference in `src/`/`tests/` (bone-corrections references moved
    `#6`→`#5`); `README.md` (Usage section's `.anim`/roadmap-stage-6
    prose, format matrix row, Testing-architecture paragraph — the
    "there's no repeatable real-file `.anim` test, a real gap" line was
    itself stale after this session and got corrected), `DESIGN.md` (Key
    design decisions entry rewritten from "skipped outright" to "resolved,
    here's how," Boundaries list, Open-work pointer).
  - **Environment note, reconfirmed**: same `uv run --no-project python3`
    pattern as prior sessions for ad hoc byte-level scratch analysis (this
    session's scripts lived in the scratchpad, not committed); `nu` used
    for a couple of quick chunk-offset dumps needing no Python at all.
- **Previous state**: `TODO/TODO_correctness.md` #6's extras-export half is now
  implemented — real `.bone` correction data attaches to `husk export`'s
  glTF output as inert `extras`, never applied to the render. New
  `husk export --bones-dir <dir>` flag (three-state, same shape as
  `--textures`/`--skin-dir`): resolves every FileDataID the model's/
  `.skel`'s `BFID` array declares to a real `<dir>/<FileDataID>.bone` file
  (silently skipping any that don't resolve, same policy `--textures`
  already uses for a missing PNG), parses each with the existing
  `husk::bone::parse`, and attaches every resolved slot as a
  `bone_correction_sets` key on the glTF **skin**'s own `extras` — one
  entry per `.bone` file, each a `(file_data_id, [{joint, matrix}, ...])`
  record. Deliberately *not* applied to the bind pose or any animation:
  which slot is "correct" for a given character is external,
  client-side customization-choice data husk still doesn't have (this
  session's own prior investigation, see Previous state below, and
  `WIKI_FINDINGS.md` §4/`TODO/TODO_correctness.md` #6) — same "tag it, don't
  guess at semantics" treatment as geoset selection/`textureTransform`.
  Went through a full plan-mode design pass before implementation, given
  the CLI-grammar/parsing-pipeline/glTF-schema surface touched; the
  approved plan is what got built, no deviations. All verified: clean
  rebuild, full 335-case `husk-tests` suite green via both
  `./build/husk-tests` and `ctest` (up from 324), plus a real
  `bloodelffemale_hd.m2`/`.skel` export re-checked by hand (20/20
  `.bone` slots attached, round-tripped through `gltf_validator` — the
  1.7M pre-existing `JOINTS_0` duplicate-value errors on that specific
  fixture are unrelated, confirmed identical with `--bones-dir none`,
  not a regression from this work).
  - **`src/skel.hpp`/`skel.cpp`**: new `findBoneFileDataIds` reads
    `.skel`'s own `BFID` chunk (previously explicitly out of scope, per
    this file's own doc comment) — same flat-`uint32`-array shape as an
    M2's own `BFID`, duplicated locally rather than shared from `m2.cpp`'s
    anonymous-namespace helper, matching this file's existing
    `findAnimFileIds` precedent (same "small parser helper, one per
    translation unit" pattern already established here, not a new one).
  - **`src/gltf.hpp`/`gltf.cpp`**: new `gltf::Skeleton::CorrectionSet`
    (`fileDataId` + `vector<{joint, matrix[16]}>`) as a field on
    `Skeleton` alongside `joints`; `writeGlbMulti` validates every
    correction's `joint` is in range (same `Error` shape as the existing
    parent-range check) and serializes non-empty `correctionSets` into
    `skin.extras["bone_correction_sets"]`, nested `tinygltf::Value`
    construction following the exact existing pattern
    `additional_textures`/`texture_transform` material extras already use.
  - **`src/commands.hpp`/`src/cmd_export.cpp`**: `ExportOptions::
    bonesDirArg` + `--bones-dir` registered in `addExportOptions` (so the
    completion generator picks it up automatically); resolution/
    attachment logic sits right after the existing skeleton-building
    block, keyed off the same `bonesAreInline`/`haveSkel` branch already
    used for choosing inline-vs-`.skel` bones/animation elsewhere in this
    function. Prints a summary note (`attached N/M '.bone' correction
    set(s)...`) only when `N > 0` — silent otherwise, matching
    `--textures`'s existing "quiet when nothing applies" behavior.
  - **`src/main.cpp`**: the hand-maintained bash/zsh completion-generator
    tables (`bashValueCompletion`/`zshValueAction`/`zshFlagLabel` — these
    are *not* derived from CLI11 introspection alone, a real gap this
    session had to discover by testing the regenerated completion
    function directly rather than assuming the flag-table change alone
    was sufficient) needed `--bones-dir` added explicitly, same
    `none`-plus-directory treatment as `--textures`/`--skin-dir`.
    `completions/husk.bash`/`.zsh` regenerated and functionally verified
    the same way prior sessions did (sourcing the script, driving
    `_husk_completions` with scripted `COMP_WORDS`/`COMP_CWORD` — confirmed
    `--bones-dir` offers `none` + real directories, not plain filenames).
  - **Tests**: `tests/test_skel.cpp` (`findBoneFileDataIds`: found/absent/
    malformed-length-throws, mirroring `findAnimFileIds`'s existing
    cases), `tests/test_gltf.cpp` (3 new cases: `correctionSets` round-trip
    as skin extras, no-`correctionSets`-means-no-key, out-of-range joint
    throws — same style as the existing billboard/geoset/textureTransform
    extras tests), `tests/test_cli.cpp` (4 new cases: explicit
    `--bones-dir` attaches + notes, `--bones-dir none` suppresses, unset
    defaults to the model's own directory, an out-of-range `.bone`
    correction fails the export naming the file/bone index — new local
    `buildBoneFile`/`boneCorrectionSkel` fixture helpers), `tests/
    test_data_paths.hpp`+`test_integration.cpp` (new `autoBonesDir`
    mirroring `autoSkinDir`: reads real `BFID` entries out of
    `bloodelffemale_hd.skel` and copies a few of this repo's own real
    `.bone` fixtures under those FileDataIDs — comment notes the NN→
    `BFID[NN]` positional assignment is arbitrary for test purposes, not a
    claimed real mapping; one new `doctest::skip`-gated real-data
    `TEST_CASE` checks the produced `.glb`'s skin extras directly via
    tinygltf). `test_main.cpp`'s startup banner gained a
    `HUSK_TEST_BONES_DIR` line.
  - **Environment note carried over, reconfirmed**: bare `python`/`python3`
    is still guarded off even under `direnv exec .`/`nix develop ./nix -c`
    — `direnv exec . uv run --no-project python3 <script>` is the
    sanctioned path for ad hoc Python in this repo (used again this
    session for the real-file `--bones-dir` smoke test), `nu` remains fine
    for direct byte-level work with no `uv` involved at all.
  - **Docs updated**: `README.md` (`.bone` corrections paragraph + flag
    table row + defaults/`none` lists + sidecar-resolution format-matrix
    row), `DESIGN.md` (CLI grammar table + three-state section + a new
    Key design decisions bullet matching the geoset/texture-transform
    precedent + a Non-goals clarifying sentence: an out-of-band
    CASC/DB2-scraping build tool is fine, husk itself talking to CASC at
    runtime never is), `TODO/TODO_correctness.md` #6 (extras-export marked
    done, remaining gap reframed as "external lookup, not more
    investigation"), `M2_COMPLETENESS.md` (`.bone` row + the sidecar
    FileDataID-resolution rows), `WIKI_FINDINGS.md` §4 (added the
    previously chat-only weapon-type/armor-type ruling-out finding — the
    corrected bones cluster on Head/Jaw, not hand/wrist — since
    `TODO/TODO_correctness.md` #6 now cites it as an established fact and it
    needs real receipts backing it, not just a claim).
- **Earlier state** (condensed — full detail in git history/`WIKI_FINDINGS.md`/
  `DESIGN.md`/`README.md`, which all already captured the durable facts):
  a `.bone`-slot-selection investigation ruled out the LOD/render-distance
  hypothesis by real data (20 `.bone` slots don't fit a 7-tier LOD count,
  collapse into only 5 distinct bone-index sets with heavy exact
  duplication) — the real selector is external client-side DB2 data husk
  has no access to, per `DESIGN.md`'s non-goals (`WIKI_FINDINGS.md` §4,
  `TODO/TODO_correctness.md` #5). Earlier still, `export`'s CLI grammar
  migrated from a positional parser to named CLI11 flags (a breaking
  change to every invocation's argument order, done in one deliberate
  pass) — CLI11 added as a new flake dependency with sign-off,
  `addExportOptions` became the one place the flag surface is declared
  (shared by real parsing and the `--print-completion` generator), and
  `--skin`/`--textures`/`--skin-dir`/`--anim`/`--skel` got the
  three/four-state (`auto`/explicit/`none`) treatment `DESIGN.md`'s CLI
  grammar section still documents in full.
