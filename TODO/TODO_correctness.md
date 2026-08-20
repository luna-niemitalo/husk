# TODO: correctness &amp; usability gaps

**Status: an open punch list, not a historical record.** Fixed items get
removed outright rather than kept as `[Fixed]` noise — git history is where
the record of what was fixed and when lives, not a checked-in file.

---

## Read-pipeline correctness

### 1. Cameras (`M2Camera`) — low priority, explicitly deprioritized

`M2Camera` records are WoW's own *baked, model-relative* cinematic camera
paths (character-select rotating camera, some cutscenes, login-screen
framing) — fixed viewpoints authored for Blizzard's own UI/cinematic
contexts, not something a model needs to render correctly from an
arbitrary camera a custom engine already owns. Unlike billboarding,
nothing about normal model rendering depends on this data. Currently
count/offset-only in `husk info`; leave as-is unless the goal ever expands
to literally reproducing WoW's specific character-select/cinematic
screens.

### 2. `.bone` correction-set selection — resolved; application semantics tracked separately

Real DB2 chain confirmed and wired into `husk export --db2-dir/--dbd-dir/
--customization-choice-ids` (`src/chrcustomization_db2.hpp`/`.cpp`):
`ChrCustomizationChoiceID` → `ChrCustomizationElement.
ChrCustomizationBoneSetID` → `ChrCustomizationBoneSet.BoneFileDataID` — a
real customization choice resolves to a real `.bone` FileDataID and, when
that file was also given via `--bones-dir`, marks the matching
`CorrectionSet` with `selected_by_choice_ids` extras. Verified against
real local data (`README.md`'s `.bone` section has usage).

**Update (2026-08-20): name enumeration and a default-choice heuristic are
now implemented.** `ChrCustomizationOption`/`ChrCustomizationChoice` were
0 bytes locally; fetched via `tact-fetch` and placed 2026-08-20 (see
`CHAR_TEXTURE_COMPOSITING_TODO.md`'s Update note). `chrcustomization_db2.hpp`/
`.cpp` now loads both tables (real `Name_lang` strings resolved via a new
`db2table::readNamedStringColumns`, since the existing named-column reader
was scalar-int-only) and exposes `namedChoicesForModel` (real Option/Choice
names paired with their resolved geoset/boneset selector -- "the mapping
of names to the geoset selector") and `defaultChoiceIdsForModel` (the
lowest-`OrderIndex` choice per option for a given `ChrModelID` -- husk's
own heuristic, mirroring the character-creation UI's own first-shown
choice; **not** a client-verified default the way
`--creature-display-id`'s geoset selection is, since no DB2 table states
an explicit player default). Wired into a new `husk export --chr-model-id`
flag: given alongside `--db2-dir`/`--dbd-dir` (and no explicit
`--customization-choice-ids`, which still wins if given), it auto-selects
and resolves the default choice per option, printing each real
`OptionName -> ChoiceName` pair used. Verified against real local data:
`ChrModelID` 89 (Dracthyr) resolves 45 default choices, 7 with real
geoset selections, e.g. "Ears -> Short Fin" (`OrderIndex` 0) -- matches a
direct SQL cross-check against the same fetched tables. New CLI-tier
tests in `tests/test_cli_chrcustomization.cpp` (a synthetic
string-bearing WDC5 fixture, `buildOptionOrChoiceDb2`, exercises the real
`db2::resolveFieldString` path, not a mock). Full suite green, 642/642.

**Same-day follow-up: `--chr-model-id` now also accepts `auto`**, deriving
a real `ChrModelID` from the input `.m2` instead of requiring the caller
to already know the ID (`src/chrrace_db2.hpp`/`.cpp`, new
`chrraces.db2`/`chrracexchrmodel.db2` fetched via `tact-fetch` and placed
locally). Parses WoW's real client-side filename convention
(`<ClientFileString><"male"|"female">[_hd].m2`) and matches it *exactly*,
case-insensitive only, never fuzzily (Luna, 2026-08-20: "if user opens
file 'dwagon_biddies_69' that's not gonna match dracthyr female no matter
how hard you try"). 3 new CLI-tier tests (exact match, ambiguous match,
no match).

**Same-day second follow-up: the filename-only path's "ambiguous" report
for Dracthyr was husk's own bug, not real caution** -- Luna asked to
investigate after noticing the real `character/dracthyr/` folder has
three files (`dracthyrmale.m2`/`dracthyrfemale.m2`/`dracthyrdragon.m2`),
suspecting the "ambiguity" wasn't real once the actual file is known.
Confirmed via the real chain (`ChrModel.DisplayID ->
CreatureDisplayInfo.ModelID -> CreatureModelData.FileDataID`):
`dracthyrmale.m2`'s own FileDataID resolves to exactly `ChrModelID` 127
(not 89, the dragon form) -- the two were only ambiguous because the
race+sex-only path was asking a broader question than the actual input
file answers. Added a second, more precise derivation path,
`deriveChrModelIdFromFileDataId`, tried *first*: given the model's own
real FileDataID (resolved via `--listfile`/`--listfile-root`, the same
mechanism texture resolution already uses -- a linear reverse scan, not a
second indexed copy, since it only runs once per export), chases
`CreatureModelData.FileDataID -> CreatureDisplayInfo.ModelID ->
ChrModel.DisplayID` for an exact, never-ambiguous-for-a-real-file answer.
Falls back to the filename-only race+sex path only when no `--listfile`
match was found at all -- once the FileDataID path resolves (including a
genuine ambiguity report from it specifically), that answer is trusted
over the weaker fallback, not silently overridden. Verified end to end
against all three real Dracthyr files: `dracthyrmale.m2` -> 127,
`dracthyrfemale.m2` -> 128, `dracthyrdragon.m2` -> 89, each cross-checked
directly against `chrmodel.db2`/`creaturedisplayinfo.db2`/
`creaturemodeldata.db2` before trusting the CLI's own output; the
filename-only fallback (no `--listfile`) still correctly reports the
89/127 ambiguity when the FileDataID path isn't available. 1 new CLI-tier
test (`buildFlatDb2`-based `ChrModel`/`CreatureDisplayInfo`/
`CreatureModelData` fixtures). Full suite green, 646/646.

**Genuinely open, tracked separately**: applying the resolved correction
matrix to real Blender rendering — not a format wall
(`tools/husk_blender_geoset_mask.py` already applies other husk extras to
real rendering elsewhere), but the matrix's own application semantics
(multiply order, local-vs-model space) were never verified against real
client behavior. See `TODO/BONE_CORRECTION_APPLICATION_TODO.md`.

### 3. `tools/live_gallery`'s three.js viewer — curve playback not yet visually confirmed in a real browser

`static/viewer.html`/`.js` plays back both real skeletal animation
(`THREE.AnimationMixer`, clip dropdown, play/pause/loop) and husk's own
extras-driven texture-transform/tint/fade curves (a real JS port of
`tools/husk_blender_geoset_mask.py`'s curve-eval logic), plus mesh/
material picking (click to inspect `blend_mode`/`pixel_shader`/
`vertex_shader`/etc.) and lighting-intensity/exposure sliders. Verified
structurally (element-ID wiring, extras JSON shape cross-checked against
`gltf_mesh.cpp`'s actual output, served correctly through a real local
server) — **not yet visually confirmed in an actual browser** (no headless
browser available in this environment); worth a real look before treating
the curve-playback math as trusted the same way the Blender-side port
already is.

Not attempted, real "overlay shenanigans" stretch scope if there's
appetite for more: JS-side parity for the Blender interactive script's
geoset-switch dropdown and texture-layout overlay — the viewer can inspect
a mesh's material but can't yet toggle geoset variants or preview the
character-texture-layout compositing rectangles the way
`husk_blender_geoset_mask.py` does.

