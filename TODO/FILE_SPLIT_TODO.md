# TODO: split files back under the 1000-line hard limit

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

A prior `FILE_SPLIT_TODO.md` (Item 5 + a post-completion audit) already
brought every `src/`/`tests/` file under 1000 lines once; five have grown
back past it since. Same convention as that pass: split by CLI-flag/
sub-feature shape, not by "half the lines" — pure moves, no behavior
change, full suite green after each. `.py` tools (`tools/husk_blender_geoset_mask.py`,
`tools/corpus_scan_tasks/render_glb.py`) are deliberately out of scope —
this limit has only ever applied to `src/`/`tests/`.

## 1. `src/cmd_export.cpp` (2098 lines)

Lines ~249-1446 are a cohesive group of `attachX` helpers (`attachBoneCorrections`,
`attachEmitterAnchors`, `attachPlacementNodes`, `attachPhysicsBodies`,
`attachCustomizationChoices`, `attachCharTextureLayout`,
`attachCreatureGeosets`, `appendCollisionMesh`, `printExportSummary`) — each
attaches one kind of inert glTF `extras` metadata to the skin, called once
each from `exportOneModel`. Split into a new `export_extras.hpp`/`.cpp`
(matching the existing `export_skeleton`/`export_animation`/
`export_materials` naming), leaving `cmd_export.cpp` with just
`addExportOptions`/`exportOneModel`/`exportGlb` (~650 lines) — the real CLI
parse + single-model pipeline + batch dispatch, which is what this file's
name should mean.

## 2. `src/cmd_db2.cpp` (1207 lines)

`db2Build`'s own machinery (`KbSourceTable`, `InventoryType`,
`expectedInventoryTypesForPath`, `stampSourceFiles`, `addDb2BuildOptions`,
`db2Build` itself — lines ~681-1144, ~460 lines) is a distinct sub-feature
(the knowledge-base builder, `TODO/KNOWLEDGE_BASE_DESIGN.md`) sharing only
`loadOneFile`/`writeFileTable` with `db2Export`/`db2Info`. Split into
`cmd_db2_build.cpp` (new `commands.hpp` forward-declares already used the
same pattern export's helpers do). Brings `cmd_db2.cpp` to ~745 lines.

## 3. `tests/test_cli.cpp` (1299 lines)

Per this file's own doc comment, it's the post-split leftover from the
prior `FILE_SPLIT_TODO.md` pass ("what's left here is `husk export`'s own
default-resolution and general-flag behavior"). The texture-resolution
cluster (fuzzy/hardcoded-slot matching, ambiguous-slot defaults,
`--listfile`, `_sdr` fallback — lines ~238-940, ~700 lines) is a real,
separable sub-topic from the rest (skin/output defaulting, `--collision`,
geoset/animation/blend-mode notes). Split into `test_cli_textures.cpp`,
same "CLI-flag/topic-shaped, not module-shaped" rule the prior split used.
Brings `test_cli.cpp` to ~600 lines.

## 4. `tests/test_db2.cpp` (1215 lines)

Clean boundary already visible in the test names: everything from
`db2::decodeOffsetMapRecord`'s own tests onward (plus the one
`offset_map_id_list`-reordering test just before them — lines ~998-1213,
~215 lines) is offset-map/sparse-section-specific, distinct from the
fixed-width-section tests making up the rest of the file. Split into
`test_db2_offsetmap.cpp`. Only brings this file to ~1000 lines exactly —
worth rechecking the exact line count once split, may need the boundary
nudged a few tests earlier/later to land clearly under.

## 5. `tests/test_cli_chrcustomization.cpp` (1049 lines)

The `--chr-model-id auto` derivation tests (lines ~524-966, ~440 lines —
distinct from this file's own `--customization-choice-ids` scope, same
"different flag, different file" rule `tests/test_cli_chrmodel.cpp`
already exists for `--char-layout-id`) split into a new sibling
`test_cli_chrmodel_id.cpp`, not merged into `test_cli_chrmodel.cpp` (that
file is `--char-layout-id`-scoped specifically, a different flag). Brings
this file to ~600 lines.

Not started.
