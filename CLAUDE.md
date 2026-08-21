# CLAUDE.md — husk

Global rules apply (`~/.claude/CLAUDE.md`). Nix conventions: `.claude/rules/nix.md`.
Read `DESIGN.md` before any structural change.

## Purpose

CLI that reads WoW M2 model files (+ `.skin`/`.skel`/`.bone`/`.anim`/`.phys` sidecars) and
exports them to glTF 2.0 (`.glb`) for Blender import; `husk-blp` (separate Python
tool, `blp/`) converts BLP2 textures to PNG.

## Status

- **Current**: `husk info` (header/record-count/chunk-tag summary, incl. per-texture/
  material detail and sidecar FileDataIDs, plus a one-line ribbon/particle-emitter
  summary, `global_flags` decoded into its wiki-named bits alongside the raw
  hex value, and the conditional `textureCombinerCombos` header array when
  its flag bit is set — see Resume), `husk export` (static mesh → skeleton +
  skinning, inline or external `.skel` → materials with real embedded textures →
  animation, inline/external-`.anim`/`.skel`-sourced (external `.anim`
  resolution now falls back to the real `wow.export`-shaped same-basename
  filename convention, not just `<FileDataID>.anim`, when a FileDataID-mapped
  file isn't found — see Resume), verified against real
  `bloodelffemale.m2`/`bloodelffemale_hd.m2` data), `husk export --lod`
  (single-tier or `all`), `husk export --bones-dir` (real `.bone` correction
  data attached as inert `bone_correction_sets` glTF skin `extras`, never
  applied to the render), `husk export --phys` (real `.phys` physics/collision
  body data attached as inert `physics_bodies` glTF skin `extras` — minimal
  per-body placement anchors only, never applied to the render — see Resume),
  `husk export --db2-dir/--dbd-dir/--char-layout-id` (real DB2-derived
  character texture-layout geometry — base atlas size, real placement rects,
  real texture-layer blend info — attached as inert `chr_texture_layout`
  glTF skin `extras`, keyed by a caller-supplied `CharComponentTextureLayoutsID`
  since husk can't derive one on its own; see Resume),
  `husk export --db2-dir/--dbd-dir/--creature-display-id` (real
  `CreatureDisplayInfoGeosetData`-derived default geoset selection for
  creatures/NPCs — a true default, no per-choice caller input needed, unlike
  the player-character `--customization-choice-ids` chain above — attached
  as inert `creature_enabled_geosets` glTF skin `extras`; see Resume),
  every export also attaching minimal ribbon/particle placement anchors
  (id/bone/position, `ribbon_emitters`/`particle_emitters` skin `extras`,
  unconditional), every export also emitting one inert geoset "tag" joint
  per distinct geoset ID (`Skeleton::geosetTags`, `JOINTS_1`/`WEIGHTS_1`)
  so Blender's stock glTF importer builds a real per-geoset vertex group
  with zero custom import tooling — `tools/husk_blender_geoset_mask.py`
  turns that into a Geometry Nodes Menu Switch dropdown per geoset group
  for WoW's mutually-exclusive geoset variants (hairstyles, boot cuffs,
  eye-glow, ...); two real bugs found interactively on 2026-08-08 (wrong
  geometry disappearing on an unrelated group's switch; the tabard dropdown
  never toggling) are now both root-caused, fixed, and verified — see
  Resume — `husk dump-chunks` (JSON dump of Legion+ chunks with no
  glTF equivalent, full `M2Ribbon`/`M2Particle` records including every
  resolved animation curve, present in every M2 version; `WFV1`/`WFV2`/
  `DPIV`/`AFRA` — no wowdev.wiki struct at all, byte-decoded from real
  files instead, see Resume — now get real structural parsing too, not a
  raw hex dump; or `.bone`/`.phys`
  files directly — `.phys`'s full body/shape/joint/`PHYV` record set, each
  shape/joint resolved to its real type-specific data inline, see Resume).
  `blp/`'s `husk-blp` (BLP2 → PNG:
  palettized/DXT1/DXT3/DXT5/BGRA — DXT3 turned out to already be wired
  through the same generic decode path as DXT1/DXT5, just unverified until
  this session's real-corpus scan, see Resume). `husk export`'s CLI grammar is CLI11-based named
  flags (`--input`/`--output` positional-fallback, everything else named,
  `--skin`/`--textures`/`--skin-dir`/`--anim`/`--skel`/`--bones-dir`/`--phys`
  three-or-four-state — see `DESIGN.md`'s "CLI argument grammar for
  `export`"), with generated bash/zsh completions in `completions/`, and
  optional TOML config-file defaults for the per-machine-stable flags
  (`--config`/`$HUSK_CONFIG`/XDG-default autodiscovery — see `DESIGN.md`'s
  "Config-file defaults for `export`" and `README.md`'s "Config file"
  subsection). See
  `README.md`'s format-support matrix and roadmap for the exact per-feature
  state — that table is the source of truth, not this file.
- **Target**: a real Blender import path for modern (Legion+ chunked) M2 — see
  `DESIGN.md`'s Goal section. All 8 roadmap stages are now done, including stage 7
  (output hardening: real exports now run through the Khronos glTF-Validator *and*
  headless Blender itself, `tests/test_conformance.cpp` — see Resume). `AFSB`
  (`.skel`-linked models' real external-animation format, previously the single
  biggest animation gap) is now cracked and resolved end to end — see Resume.
  M2-source-vs-exported-glb-vs-Blender-readback cross-checks (former
  `VERIFICATION_IDEAS.md`, now deleted — its survey's job was done, every
  case had a final disposition, folded back into `tests/test_conformance.cpp`/
  `WIKI_FINDINGS.md` §5, same scratch-doc lifecycle `DESIGN_CHANGES.md`
  had) are now implemented too, plus a real collision-mesh export husk
  never had before. `M2Particle`/`M2Ribbon` (weapon glow trails, magic/fire/
  smoke — the single biggest remaining visual-identity gap this tool had) are
  now fully parsed, every field and every resolved animation curve, split
  between a minimal glTF placement anchor and `husk dump-chunks`'s full JSON
  output — see Resume. Remaining work is either scope expansion
  (WMO/M3, not started, by design) or the structural gaps `TODO/TODO_correctness.md`
  already tracks (`M2Camera`, low-priority by design; `.bone` correction
  *selection* — the extras-export half is done, see Resume; picking which
  slot applies is blocked on client-side DB2 data husk doesn't have, not on
  more investigation), or the corpus-hardening follow-ups a real 130k-file
  corpus sweep turned up this session --
  five real export-robustness bugs found and fixed (a geometry-less-model
  crash affecting 3,807 real files, a `.skin`-pairing collision bug, an
  undocumented `WFV3` short-chunk variant, a duplicate-animation-keyframe
  crash), two more findings confirmed genuinely unfixable in husk
  (mismatched shared batch data, an extraction-completeness gap), and one
  concrete follow-up identified and now implemented (the multi-root-bone-
  hierarchy gap, `MULTIROOT_SKELETON_TODO.md` -- `writeGlbMulti` now
  synthesizes a non-joint glTF parent node for the 35% of the corpus with
  more than one root bone, see Resume). `M2_GAPS_TODO.md`'s full item
  bundle (ten items across several sessions, most recently `PCOL`
  player-housing collision, diagnostic-only via `husk dump-chunks`,
  verified against all 2,354 real `PCOL`-bearing files -- see Resume) is
  now fully implemented and the file itself deleted -- nothing currently
  in flight. A real interactive Blender pass found the M2→glTF position/
  rotation/scale conversion was measurably upside down despite the whole
  conformance suite above passing -- root-caused, fixed, and covered by a
  new asset-agnostic orientation-correctness test tier. The one piece
  deliberately left for Luna rather than automated -- a real animated
  clip, visually confirmed in Blender's own GUI -- is now done too: "Animation
  looks OK" (2026-08-08), closing out the investigation. `TRANSFORM_TRIAGE.md`
  itself deleted per this project's own "survey's job is done" lifecycle;
  the two dangling source-code citations of it (`src/gltf_math.hpp`/`.cpp`)
  cleaned up in the same pass. One funny, non-actionable side note from
  that verification: a dead vertex sits in the middle of the two-handed
  swing animation, detached from the character, carrying FileDataID 31739
  -- genuinely invisible in the real game too (an "invisible texture"),
  not a husk export bug.
- Anything not listed under Current does not exist yet. In particular: `M2Camera`
  is still count-only (not dereferenced). Three FAILURES2.md gaps
  (geoset selection #1, multi-texture-layer rendering #6, global-sequence animation
  #7) all went further than a diagnostic this session: geoset `skinSectionId` and
  additional (`textureCount > 1`) texture layers are now real glTF `extras`
  metadata on every primitive/material (husk still doesn't *filter*/*render* either
  one — no DBC data to ground a default geoset choice in, no core-glTF slot for a
  second texture layer — but a custom renderer or Blender script now has everything
  it needs to implement its own selection/blend on top), and global-sequence
  tracks resolve to real, separate glTF animation clips
  (`global_seq_<n>`). Verified against real data: `bloodelffemale.m2` goes from 256
  to 258 animation clips, and its 66-geoset/1-multi-texture-batch `.skin` exports
  cleanly with the new extras attached.

## Boundaries

- Model file bytes (`.m2`) — chunk container + fixed-offset header/arrays
  (`src/chunk.cpp`, `src/m2.cpp`).
- `.skin` sidecar — triangle-index lookup, submesh/batch structure (`src/skin.cpp`).
- `.skel` sidecar — external bones + sequences (`src/skel.cpp`).
- `.bone` sidecar — per-bone correction matrices, reverse-engineered (`src/bone.cpp`).
- `.anim` sidecar — external per-sequence keyframe blob; `AFM2` (flat) and `AFSB`
  (`.skel`-linked models' real shape) both resolved (`m2::extractAnimBlob`,
  `cmd_export.cpp`'s `buildAnimations`).
- `--textures`/`--skin-dir`/`--anim` directories — user-populated,
  FileDataID-named, local filesystem only. **Never *live* CASC** — husk
  never talks to CASC/DB2 at runtime or depends on the CASC tool itself, by
  design (see `DESIGN.md`'s Non-goals). A local, optional, user-supplied
  `community-listfile.csv`-style snapshot (`--listfile`, `src/listfile.hpp`/
  `.cpp`) is the same "already on disk, never live CASC" tier as every
  other sidecar here — used only as a last-resort FileDataID -> real-name
  fallback in texture resolution, same clarified scope as `--dbd-dir` below.
- `.db2` files (real WDC5 container, `src/db2.hpp`/`.cpp`), real column
  names via an optional local WoWDBDefs checkout (`src/dbd.hpp`/`.cpp`),
  a generic named-column reader on top of both (`src/db2table.hpp`/`.cpp`),
  and real typed character-texture-layout structs on top of that
  (`src/chrmodel_db2.hpp`/`.cpp`, consumed by `husk export --db2-dir/
  --dbd-dir/--char-layout-id` and the separate `husk db2-export` side tool),
  plus a sibling typed reader for the customization-choice → geoset/bone-
  correction-set chain (`src/chrcustomization_db2.hpp`/`.cpp`, consumed by
  `husk export --db2-dir/--dbd-dir/--customization-choice-ids`)
  — locally-extracted files only, same "user-populated, never CASC" tier as
  every other sidecar above (see `DESIGN.md`'s Non-goals' clarified
  wording).
- `.blp` texture files (separate `blp/` Python tool) — container hand-rolled, block
  decode delegated to Pillow via a synthetic DDS wrapper.
- No network access anywhere in this tool. No user input beyond CLI argv (parsed in
  `cmd_info.cpp`/`cmd_export.cpp`/`cmd_dump.cpp`, no interactive prompts).

Every boundary above is read via explicit bounds-checked parsing at named offsets,
throwing a descriptive `ParseError`/`std::runtime_error` on anything foreign data
claims that doesn't fit — never a silent misread (see git history for the specific
bugs found and fixed under this discipline), and `WIKI_FINDINGS.md` for every
real-file-driven spec correction found along the way.

## Resume

Full session-by-session narrative: `CLAUDE_HISTORY.md` (append new entries
there, most recent first). This section is a snapshot, not a log — update it
in place each session; append the full story to `CLAUDE_HISTORY.md` instead.

- **Current state (2026-08-22, skin extras -> root-joint-extras migration)**:
  Luna asked why `husk_blender_geoset_mask.py`'s post-import functions need
  a file path at all -- answer: `chr_texture_layout`/`chr_customization_options`/
  `chr_enabled_materials`/`enabled_geosets`/etc. live on the glTF *skin*'s
  own extras, and Blender's stock glTF importer has no supported extras
  target for a skin at all (confirmed empirically), so every `read_*`
  function had to re-open the raw `.glb` and re-parse the JSON chunk by
  hand instead of reading from the already-imported scene. Fixed by
  noticing this is the geoset-tag trick, inverted: instead of encoding
  data in vertex *weighting* on a fake joint (what the geoset tag joints
  already do, because Blender's importer turns joint weights into real
  vertex groups), attach the data to a real joint's own node `extras`
  (because Blender's importer *does* keep node/bone extras as real custom
  properties, confirmed directly via a headless Blender round-trip --
  flat and deeply-nested structures both survive intact). Chose the
  skin's own existing root joint over minting a new fake carrier joint
  (the more surgical fix, avoiding ~10 tightly-coupled offset-arithmetic
  sites in `gltf_skeleton.cpp`'s existing geoset-tag-joint machinery a
  new joint category would have needed touching) -- confirmed by Luna
  directly as the right tradeoff. `gltf_skeleton.cpp`'s
  `emitSkeletonAndSkin` now merges the whole `skinExtras` object onto
  `out.rootJointNodeIndices.front()`'s own node extras instead of
  `skin.extras`, never overwriting existing extras that joint already
  carries (e.g. `billboard`). New `_root_joint_extras`
  (`husk_blender_geoset_mask.py`) reads it back, scanning for whichever
  imported bone actually carries one of husk's own known top-level keys
  rather than assuming an index -- confirmed directly that Blender's own
  post-import bone order does *not* reliably match the raw glTF
  joint-index order (242/358 mismatches on a real 245-bone character), so
  `read_chr_texture_layout`/`read_enabled_geosets`/
  `read_chr_enabled_materials`/`read_chr_customization_options`/
  `read_emitter_anchors` now take `armature_obj` instead of `filepath` and
  need **no file path at all** for their own payload (`read_emitter_anchors`
  still takes `filepath` too, but only to resolve a raw numeric bone index
  to a name -- a different, still-real need the raw file order legitimately
  answers and the imported scene can't). Verified end to end against the
  real `bloodelffemale_hd` export (245 bones, 480 customization materials,
  17 options): every migrated `read_*` function returns the same real data
  with zero file path, confirmed via a fresh `bpy.ops.import_scene.gltf`
  and no `-- model.glb` arg at all. C++ tests updated for the new extras
  location (`model.skins[0].extras` -> the root joint's own node extras;
  real-corpus integration tests scan for whichever joint carries it,
  since real M2 files don't guarantee `joints[0]` is root). Full suite
  green, 678/678 (C++) before the concurrent `physics_joints` addition
  below landed, still green together after. This landed concurrently with
  peer session `husk-ed`'s own physics-jiggle work in the same files
  (`gltf_skeleton.cpp`/`.hpp`, `tools/husk_blender_geoset_mask.py`) --
  coordinated live via SendMessage: their new `physics_joints` key rides
  this migration's merge-onto-root-joint code for free, and per their own
  request I left `read_physics_bodies`/`_dump_phys_json`/
  `_phys_elasticity_heuristic`/`apply_physics_jiggle_bones`/
  `physics_jiggle_stage`/`HUSK_OT_install_jiggle_physics` untouched for
  them to migrate onto `_root_joint_extras` themselves.
- **Previous state (2026-08-22, `.phys` -> Blender "Jiggle Physics" addon
  wiring)**: `tools/husk_blender_geoset_mask.py` turns real `--phys`
  export data into live secondary motion via the third-party "Jiggle
  Physics" Blender addon (`naelstrof/blender-jiggle-physics`,
  extensions.blender.org). New `apply_physics_jiggle_bones` builds a real
  jiggle-bone chain from the real joint graph, skipping (loudly, never
  force-fitting) any joint edge that doesn't match a real bone
  parent/child pair -- confirmed this matches real data via a real
  chain-prop fixture (`8xp_heartofazeroth_prop_floatychain.phys`, every
  edge a real parent/child pair). Elasticity is a documented best-effort
  heuristic (spring frequency when present, else derived from real
  swing-limit data), not a physical port -- the addon itself is
  "authorable, not physically accurate" by its own design. Also added
  `HUSK_OT_install_jiggle_physics`, a real registered Blender operator
  that pops a native confirm dialog offering to install the addon
  in an interactive session when it's missing (real install only on a
  real click, never automatic; headless/`--background` runs keep the
  console-only message).

  **Follow-up (portability fix)**: the first version shelled out to `husk
  dump-chunks <file>.phys` at Blender-*import* time for the real joint
  graph -- Luna flagged this as a real gap, since a `.glb` isn't
  guaranteed to travel with a `husk` binary nearby, unlike every other
  feature here (resolve once in husk, embed inert self-contained extras).
  Fixed with a new `gltf::Skeleton::PhysicsJoint` (`gltf_skeleton.hpp`) --
  a deliberately reduced view of the real joint graph (`bodyA`/`bodyB`
  plus just `frequencyHz`/`dampingRatio`/`swingLimitDeg`, no frame
  matrices/shape geometry -- those, not raw byte count, were the real
  reason the full graph was kept off the `.glb` in the first place),
  populated in `export_extras.cpp` and written as a new `physics_joints`
  extras key alongside `physics_bodies`. Verified end to end against the
  same real fixture -- the `.glb`'s own values matched the earlier `husk
  dump-chunks` numbers exactly. New `writeGlb` round-trip tests, full
  suite green, 680/680. This landed concurrently with peer session
  `husk-0a`'s own fix in the same file (Blender's glTF importer drops
  `skin.extras` entirely -- every extras key, including
  `physics_bodies`/`physics_joints`, now lands on the skin's root joint
  node instead, see the current-state entry above) -- coordinated live via
  SendMessage, `physics_joints` rides their already-landed merge code for
  free, no clobbering. Once `husk-0a` landed their generic `read_*`
  migration (`_root_joint_extras`), the Python side was updated to match:
  `read_physics_bodies(armature_obj, filepath)` now reads
  `physics_bodies`/`physics_joints` via `_root_joint_extras`;
  `apply_physics_jiggle_bones`/`_phys_elasticity_heuristic` take the flat
  `physics_joints` list directly. `_dump_phys_json` and the `--phys` CLI
  flag on the Blender script are gone -- no `husk` binary, no subprocess,
  no separate `.phys` file needed at Blender-import time at all now.
  Re-verified against the same real fixture (same throwaway
  stand-in-PropertyGroup technique, never the real addon installed into
  Luna's own profile without asking): identical results to the old
  dump-chunks path. `README.md` updated to match, full suite still green,
  680/680. **Final follow-up**: once `husk-0a` added a `joint_names` array
  to the same root-joint extras (per Luna's own separate ask, closing
  Blender's post-import bone-order mismatch gap for good), `read_physics_bodies`
  dropped its own `filepath` parameter entirely too -- swapped for the new
  `_joint_bone_names_from_extras(armature_obj)` primitive, same
  `{joint_index: bone_name}` shape, zero file I/O anywhere in the
  physics-jiggle path now. The now-dead old `_joint_bone_names(data)`
  helper was removed outright (confirmed zero remaining call sites).
  Re-verified end to end once more (had to re-export the fixture first,
  since the cached `.glb` predated `joint_names` and briefly read back 0
  physics bodies once the carrier-detection changed underneath it -- an
  expected consequence, not a bug) -- same 5-bone/0-skipped result. Full
  suite still green, 680/680. Full narrative: `CLAUDE_HISTORY.md`'s newest
  entries.
- **Previous state (2026-08-21, `CHAR_TEXTURE_COMPOSITING_TODO.md` Stage 6:
  equipped-gear appearance resolution)**: `husk appearance-string`'s `gear`
  entries (opaque `ItemModifiedAppearanceID`s since the format was first
  built) now resolve to real equipped-item data given `--db2-dir`/
  `--dbd-dir`. New `src/itemappearance_db2.hpp`/`.cpp` (the real
  `ItemModifiedAppearance -> ItemAppearance -> ItemDisplayInfo ->
  ItemDisplayInfoModelMatRes` chain -- this stage's own previously-open
  bridge question, now confirmed against the current local extraction's
  live layout hashes) and `src/modelfiledata_db2.hpp`/`.cpp` (new --
  `ModelFileData.db2`'s `ModelResourcesID -> FileDataID` reverse lookup,
  `texturefiledata_db2.hpp`'s missing sibling). `--db2-dir`/`--dbd-dir`
  also gained `--config`/`$HUSK_CONFIG` support, same as `export`/
  `db2-build`. Verified end to end against real local data
  (`ItemModifiedAppearanceID` 15 -> `ItemDisplayInfoID` 1542 -> model
  FileDataID 370361 -> texture FileDataID 148134, each hop independently
  cross-checked via `husk db2-export` + `sqlite3` before trusting the
  command's own output). 2 new CLI-tier tests (`tests/
  test_cli_appearance.cpp`, a synthetic 6-table DB2 fixture). Same "husk
  resolves, never applies" policy as every other DB2 feature here --
  turning the resolved FileDataIDs into an actually attached/rendered
  weapon or armor piece is downstream Blender-side work, not started, out
  of this stage's own scope. `README.md` gained a new "`husk
  appearance-string`" section; `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`/
  `TODO/README.md` updated (Stages 1-6 all now done). Full suite green,
  674/674 (671 + 3 new). Full narrative: `CLAUDE_HISTORY.md`'s newest
  entry.
- **Previous state (2026-08-21, human-readable geoset/animation names +
  Blender Asset Browser as an animation picker)**: answered three
  usability questions from Luna by reading the real code/data first (see
  `CLAUDE_HISTORY.md`'s newest entry for the full narrative), then
  implemented all three. Geoset switch dropdowns
  (`tools/husk_blender_geoset_mask.py`) now show real customization-
  choice names ("Short Fin") instead of `variant_<n>` wherever
  `chr_customization_options` covers a group/variant, falling back to the
  plain numeric label otherwise -- verified against real data
  (`bloodelffemale_hd.m2`: 6/23 groups, 30 variants labeled). New
  `src/animationdata_db2.hpp`/`.cpp` attaches real `AnimationData.db2`
  names to matching clips' `sequence_metadata` extras when
  `--db2-dir`/`--dbd-dir` are given -- correct and verified via a
  synthetic fixture, but current real local extractions have dropped the
  `Name` column from `AnimationData.db2` entirely (confirmed via `husk
  db2-info`), so no real export resolves a name today; same "client
  schema genuinely dropped this column" class as the earlier `aliasNext`-
  name finding. `mark_actions_as_assets`
  (`tools/husk_blender_geoset_mask.py`) marks every imported clip's
  Action as a real Blender asset after import (Asset Browser now works as
  a per-animation picker), renaming to a real `animation_data_name` when
  one resolves -- verified against real data (`bloodelffemale_hd.m2`, 338
  clips marked, 0 renamed, consistent with the AnimationData.db2 finding
  above). `DESIGN.md`'s new "Human-readable names for animations/geosets,
  and Blender's Asset Browser as an animation picker" section has the
  full design rationale; no TODO file was left behind (all three items
  fully closed this session, folded straight into history per this
  project's own "closed items get removed, not kept as a stray TODO
  file" convention). **Same-day follow-up**: investigated the 3 failures
  initially reported as pre-existing rather than leaving them
  unexplained -- all three traced to one shared cause, not three bugs:
  this machine's own real `~/.config/husk/config.toml` (XDG-autodiscovered
  by every `husk` invocation, including CLI-tier test subprocess spawns)
  was silently injecting real `dbd-dir`/`db2-dir`/`listfile-root` values
  into tests that never asked for them. Fixed at the shared root --
  `tests/run_husk.hpp`'s `runHusk` now always runs with
  `HUSK_CONFIG=/dev/null` -- rather than patched in each of the 3 tests.
  Full suite now genuinely green, 671/671, no known-failing tests left.
- **Previous state (2026-08-21, completion-tree follow-up + `CLEANUP_TODO.md`
  housekeeping)**: closed the gap flagged at the end of the previous
  entry's own session summary -- `db2-export`/`db2-info`/`db2-build`/
  `blp-export`/`appearance-string` were migrated to real `CLI::App`s but
  never wired into `main.cpp`'s `--print-completion` tree (a pre-existing
  gap, not new from that migration). Fixed by factoring each command's
  flag registration into its own shared `addXOptions(CLI::App&, XOptions&)`
  (new `InfoOptions`/`DumpChunksOptions`/`Db2InfoOptions`/
  `Db2ExportOptions`/`Db2BuildOptions`/`BlpExportOptions`/
  `AppearanceStringOptions` in `commands.hpp`), the exact same
  single-source-of-truth split `ExportOptions`/`addExportOptions` already
  established -- `generateCompletionScript` (`main.cpp`) now registers
  all 8 subcommands (`info`/`dump-chunks` were already there, inlined;
  now share the same real function the command itself parses against)
  instead of only 3. `completions/husk.{bash,zsh}` regenerated -- real
  growth (91/41 new lines), not cosmetic, since 5 subcommands' flags were
  genuinely absent from tab-completion before this. Also closed
  `TODO/CLEANUP_TODO.md`'s stale item 1 (a comment-hygiene sweep marked
  "now closed" in its own text but never actually removed from the file,
  contradicting the doc's own stated "fixed items get removed outright"
  convention) -- removed, item 2 renumbered to item 1. Full suite green,
  669/669 (no behavior change, completions + doc housekeeping + the
  registration refactor itself, which is a pure move -- every add_option
  call kept its exact same flag name/description/validator). **Same-day
  follow-up**: built and ran that one remaining item too (confirmed with
  Luna first, since it was a real new multi-hour tool build, not a quick
  fix) -- `tools/corpus_scan_tasks/dangling_references_task.py`, checking
  10 internal reference kinds against every real local `.m2`. One real bug
  in the scanner itself caught before the full run (a first-pass smoke
  test showed an implausible 64% dangling rate for
  `texture_transform_combo`, root-caused to a missing sentinel check --
  `textureTransformCombos` entries use `0xFFFF` as a real "no transform"
  marker, confirmed against `export_materials.cpp`'s own best-effort
  handling there). Real corpus result (130,242 files checked): every
  M2-only lookup kind (`bone_lookup`/`sequence_lookup`/
  `attachment_lookup`/`camera_lookup`/`texture_lookup`) is 100% clean
  across 1.35M+ references; the `.skin`-dependent kinds show a low real
  rate (0.06%-0.37%), concentrated almost entirely in
  `item/objectcomponents/head`/`collections` -- the same real
  recolor-variant/shared-batch-data class `CORPUS_TODO.md`'s own history
  already confirmed, not a new bug or a casc-tool extraction gap. `TODO/
  CLEANUP_TODO.md` is now fully empty; full narrative (including the
  false-positive bug, the synthetic-fixture verification, and the exact
  per-kind numbers): `CLAUDE_HISTORY.md`'s newest entry.
- **Previous state (2026-08-21, `TODO/CLEANUP_TODO.md` #3: CLI11 migration
  for every remaining command)**: `db2-export`/`db2-info`/`db2-build`/
  `dump-chunks`/`blp-export`/`info`/`appearance-string` all now parse argv
  via a real `CLI::App`, matching `export`'s own earlier migration
  (previous entry below) instead of hand-rolled `argc`/`args[N]`
  positional parsing. `db2-export`/`db2-build` also gained the same
  `--config`/`$HUSK_CONFIG`/XDG-autodiscovery TOML config-file support
  `export` has (`husk::defaultConfigPath()`, shared, not export-specific,
  exactly as anticipated) for their genuinely per-machine-stable flags
  (`--dbd-dir`; `--db2-dir`/`--dbd-dir`/`--listfile`) — `db2-info`/
  `dump-chunks`/`blp-export`/`info`/`appearance-string` were left without
  config wiring since none has a flag that's actually per-machine-stable
  (confirming the earlier "info may not need this at all" guess). The
  now-fully-dead hand-rolled `isHelpFlag` helper (`commands.hpp`) removed.
  Every command's own descriptive usage prose was preserved as its
  `CLI::App`'s own description string, shown by CLI11's real `--help`,
  rather than discarded. Dual-mode grammars (`db2-export`/`blp-export`'s
  `--dir <dir> <out>` vs. `<in> <out>`) keep their exact original argv
  shape via two generically-named positional slots (`pos1`/`pos2`)
  reinterpreted by mode, rather than a redesign into new flag names —
  every existing test invocation (`db2-export foo.db2 out.sqlite`,
  `blp-export --dir <dir> <out-dir>`, ...) still parses identically.
  **One real regression caught before landing**: the first pass of
  `db2-export`'s `--dir` mode only checked that exactly one positional
  followed `--dir`, forgetting to also check that a *second* stray
  positional hadn't been accepted too — `db2-export --dir <dir> <out>
  <extra>` silently exported anyway, dropping `<extra>` instead of
  erroring, caught by manually exercising the dir-mode extras case
  (blp-export's own analogous code already had the check; db2-export's
  didn't) — fixed, and a regression test added
  (`tests/test_cli_db2.cpp`) so it can't silently regress again. 4
  existing `tests/test_cli_argv.cpp` assertions (info/dump-chunks
  `--help`/no-args/too-many-args) rewritten to check CLI11's own real
  generated content (`"required"`/`"not expected"`) instead of the old
  hand-written `"usage: husk info"` text, same "check real generated
  content, not stale hand-written prose" precedent `export`'s own earlier
  migration already set. 12 new CLI-tier tests across
  `tests/test_cli_db2.cpp` (db2-export dir-mode-extras regression,
  db2-info basic + bad `--rows`, db2-build required-flags) and new
  `tests/test_cli_appearance.cpp` (appearance-string had zero prior CLI
  coverage). `main.cpp`'s `--print-completion` shell-completion tree
  was deliberately *not* extended to the newly-migrated commands (a
  pre-existing gap — `db2-export`/`db2-build`/`db2-info`/`blp-export`/
  `appearance-string` were never in that tree even before this session,
  only `export`/`info`/`dump-chunks` are) — out of this item's own
  stated scope, not touched. Full suite green, 669/669.
- **Previous state (2026-08-21, `husk export --config` TOML config-file
  support)**: `husk export` now reads default flag values from a TOML
  config file for the per-machine-stable flags (`--dbd-dir`, `--db2-dir`,
  `--listfile`, `--listfile-root`, `--textures`, ...), avoiding the
  need to repeat them on every invocation. Deliberately built on
  [CLI11](https://github.com/CLIUtils/CLI11)'s own `App::set_config`
  rather than a hand-rolled config parser: config keys map directly onto
  the same `add_option` registrations `addExportOptions` already
  declares, so a config value gets the exact same `->check()` validator a
  CLI flag would (foreign-data checking for free, no second validation
  path), and CLI11's own precedence already matches what was wanted:
  explicit CLI flag > config value > built-in default. Path resolution:
  `--config <path>` > `$HUSK_CONFIG` env var > `husk::defaultConfigPath()`
  (new `src/husk_config.hpp`/`.cpp` — `$XDG_CONFIG_HOME/husk/config.toml`,
  falling back to `~/.config/husk/config.toml`); a missing file at the
  resolved path is not an error, same "unset is the no-flag state"
  convention every other opt-in sidecar here follows. Deliberately not
  filtered to a "safe" flag subset — CLI11 has no such notion, and
  building a filter would be new code fighting the very mechanism chosen
  to avoid new code — `--input`/`--output` are technically config-settable
  too, just not shown in the documented example, per Luna's own explicit
  call ("no reason to stop users from being dumb as long as they do it in
  a consistent language that is acceptable for our program"). 5 new
  CLI-tier tests (`tests/test_cli_config.cpp`: config-supplied output,
  CLI-flag-overrides-config precedence, `$HUSK_CONFIG`, XDG
  autodiscovery, missing-config-is-not-an-error). `README.md` (new "Config
  file" subsection under `export`)/`DESIGN.md` (new "Config-file defaults
  for `export`" subsection)/`completions/` regenerated. `db2-export`/
  `db2-info`/`db2-build`/`dump-chunks`/`blp-export`/`info`/
  `appearance-string` all still hand-parse argv positionally rather than
  through a `CLI::App`, so they can't get this the same free way yet —
  tracked as `TODO/CLEANUP_TODO.md` #3, not started. Full suite green,
  662/662.
- **Previous state (2026-08-21, `--char-layout-id` auto-derivation)**:
  `husk export`'s DB2-driven character features need one fewer flag now.
  `ChrModel.db2`'s loader (`chrrace_db2.cpp`'s `loadChrModels`) now also
  reads its real `CharComponentTextureLayoutID` column (previously only
  `ID`/`DisplayID`), and `attachCharTextureLayout` (`cmd_export.cpp`,
  relocated below `tryDeriveChrModelId` so it can reuse it) auto-derives
  `--char-layout-id` from whichever `ChrModelID` `--chr-model-id`
  resolves — same `auto`/`none`/`<id>` convention as `--chr-model-id`
  itself, explicit value still overrides. Verified against real data:
  `bloodelffemale_hd.m2` with only `--db2-dir`/`--dbd-dir` now
  auto-derives layout `122` from `ChrModelID` 20, matching the value
  found manually earlier in the same session. Real workflow is now just
  `husk export model.m2 --db2-dir <dir> --dbd-dir <dir>` — no
  `--char-layout-id` needed for the common case. New regression test,
  full suite green (657/657). `--help`/`README.md`/`DESIGN.md`/
  `chrmodel_db2.hpp`'s own now-stale claims (both said husk "has no
  concept" of which layout ID applies) updated to match. Full narrative,
  including why `--db2-dir`/`--dbd-dir` themselves stay hard requirements
  (not just "nice to have for column names" — real WoWDBDefs
  `layoutHash` mappings are structural, not cosmetic, and a hardcoded
  positional fallback would silently misread fields under a different
  real layout revision): `CLAUDE_HISTORY.md`'s newest entry.
- **Previous state (2026-08-20, Blender-switch TODO: node-graph findability
  fix)**: `TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`'s Blender-side work
  (Steps 2-4) is implemented and, after two real interactive-use rounds
  with Luna, now actually usable. `tools/husk_blender_geoset_mask.py`
  gained `apply_customization_texture_switch` (plus
  `read_chr_enabled_materials`/`read_chr_customization_options`): a real,
  live, switchable node graph per material, one closed `ShaderNodeGroup`
  per real `ChrCustomizationOption` (`_build_customization_option_group`
  — a `Choice Index` field promoted to the group's own interface, so it's
  directly editable on the closed node, same technique
  `_build_section_overlay_group`'s pre-existing "Show Overlay" toggle
  already uses; internally, a `Math(COMPARE)`-gated chain of `Mix` nodes
  per choice, each masked by its own real section rect). Real
  `ShaderNodeMenuSwitch`/`ShaderNodeCompare` confirmed absent from shader
  trees in the pinned Blender 5.1.1 (only `GeometryNodeMenuSwitch`
  exists) — this is the TODO's own named fallback for both. Options
  combine in real `texture_layers[].layer` order via a new blend-mode
  table, spliced in front of each material's own Principled BSDF Base
  Color. **The group-node design is itself a fix**: the first version
  sprayed every node directly into the material's own tree; Luna ran it
  in Blender and reported "I still can't find the options" — one real
  option with 30 choices produced ~180 raw nodes, one real material hit
  364 top-level nodes where the original had 2-5, un-findable regardless
  of node `.location` (also a real bug the first pass had: no node had
  *any* location, piling up at the tree's own origin, fixed en route).
  Collapsing each option into one closed, labelled, green group node
  dropped that same material to 10 top-level nodes. Also fixed, per
  Luna's own direct correction: real per-choice texture files exist
  locally but under a suffix-named convention
  (`character/bloodelf/eyes00_00_3492879.blp`, FileDataID as a `_<id>`
  suffix, shared at the race-level parent dir), not the bare
  `<file_data_id>.blp` the first pass checked —
  `_resolve_customization_texture_path` now also tries a suffix-glob
  match in both `--textures` and its parent dir. Converted the real
  local `.blp` corpus (1,079 files) with `husk blp-export --dir` (the
  canonical in-binary tool per Luna — `blp/`'s standalone `husk-blp` is a
  superseded predecessor) and re-verified end to end against the real
  `bloodelffemale_hd` DB2 export (`ChrModelID` 20, 17 options/206
  choices): both group nodes' outputs and both materials' Base Color
  confirmed linked, "Skin Color"/"Hair Color" load genuinely distinct
  real texture images (confirmed by real pixel content). **Same-day
  follow-up (round 2, CLI ergonomics)**: Luna pushed back on the
  workflow itself (8 explicit `husk export` flags + 2 manual `husk
  blp-export` calls + a `blender --textures` restating the same dir) —
  checked `husk export --help` directly and found `--skin`/`--skel`/
  `--textures`/`--output` already default sensibly, no ceremony needed
  there. Real fixes: the Blender script's own `--textures` now defaults
  to the `.glb`'s own directory (matches `husk export`'s own default and
  Luna's real workflow of exporting next to source); a `.blp`-only match
  now auto-converts via a new `_convert_blp_to_png_cached` (shells to
  `husk blp-export`, cached by FileDataID under the system temp dir) —
  no manual conversion step at all anymore. Real workflow is now just
  `husk export model.m2 --db2-dir <dir> --dbd-dir <dir> --char-layout-id
  <id>` + `blender --python tools/husk_blender_geoset_mask.py --
  model.glb` (`--textures <dir>` only when output isn't next to source).
  No C++ changed either round. Full narrative: `CLAUDE_HISTORY.md`'s
  three newest 2026-08-20 entries.
- **Previous state (2026-08-20, Blender-switch TODO + its own Step 1)**:
  Wrote `TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`, a fully self-contained
  plan for Stage 5 (live Blender-side customization-choice texture
  switching, the real follow-up to the reverted Stage 4 compositor below)
  — then immediately implemented its own Step 1 per Luna's direct
  instruction that it shouldn't be gated behind a flag: `husk export` now
  attaches the **full** real customization menu (`chr_customization_options`
  skin extras — every real `ChrCustomizationOption`/`Choice` for the
  model, not just the choice(s) a given run resolved) **automatically**,
  whenever a real `ChrModelID` can be determined at all — explicit
  `--chr-model-id`, `--chr-model-id auto`, or (new) a best-effort attempt
  at the same auto-derivation even when only `--customization-choice-ids`
  was given, no separate opt-in flag. `src/cmd_export.cpp` gained
  `tryDeriveChrModelId` (the existing `--chr-model-id auto` logic,
  factored out for reuse) and `gltf::Skeleton::CustomizationOption`/
  `CustomizationChoice` (`gltf_skeleton.hpp`). Verified end to end,
  including the "only `--customization-choice-ids`, no `--chr-model-id`
  at all" enrichment case (real filename-fallback derivation, no
  `--listfile` needed). **Same-day follow-up**: Luna pushed further --
  why require any flag at all? `--chr-model-id` now defaults to `auto`
  (the standard `auto`|`none`|`<id>` convention `--textures`/`--skin-dir`
  already use, not a one-off "empty means auto" hack) -- given only
  `--db2-dir`/`--dbd-dir`, husk tries real derivation with zero
  customization flags; `--chr-model-id none` is the real opt-out. One
  existing test's own stale assertion (checking
  a choice ID's raw string absence anywhere in the file) was tightened to
  check the real `enabled_geosets` object shape specifically, since the
  new extras legitimately mention every choice including non-default
  ones. Full suite green, 656/656.
- **Previous state (2026-08-20, character-texture compositing, reverted +
  corrected same session)**: `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s
  Stage 3 (the real `ChrCustomizationMaterial → TextureFileData` FileDataID
  chain) is done — wired into the existing `--customization-choice-ids`/
  `--chr-model-id` flags (no new CLI surface) as `chr_enabled_materials`
  skin extras, joined against `chr_texture_layout`'s `texture_layers`
  (which now also carry `chr_model_texture_target_id`) for placement/blend
  data. A real software pixel compositor (Stage 4, `src/char_composite.hpp`)
  was also built and verified this session, then **deliberately reverted**
  after Luna's own direct pushback: husk doing pixel compositing broke
  this project's own "attach real data, never interpret/apply it" policy,
  and Blender's own shader nodes (Mix Color already has Multiply/Overlay/
  Screen built in) are strictly the better layer for it anyway — live
  compositing there lets a user switch skin color/tattoo/face-marking
  independently in real time, something husk precomputing static images
  fundamentally can't do. Stage 5 (Blender-side node-graph tooling, same
  "read raw extras JSON, build a real node graph" pattern
  `husk_blender_geoset_mask.py` already established for geosets) is now
  the real next step, not started. Full suite green, 653/653. Full
  narrative, including the reverted compositor's own design: `CLAUDE_HISTORY.md`'s
  2026-08-20 entries (the compositor build, then its revert).
- **Previous state (2026-08-20, overnight batch-export pass)**: Also
  committed this pass: the previous entry below's uncommitted
  `ChrCustomizationOption`/`--chr-model-id auto` work (it had sat
  unstaged in the working tree since it was written) — no code changes,
  just closing the loop on `git commit`, the one step that session
  narrative was missing. New work this pass: `husk export --from-list
  <file> --output-dir <dir>` (`TODO/CLEANUP_TODO.md`'s former item 3) —
  batch mode mirroring `casc-tool`'s own `extract-batch --from-list
  <ids-file> <out-dir>` shape. `exportOneModel` factored out of the old
  monolithic `exportGlb` so a batch run shares one parsed `ExportOptions`/
  one `--listfile` load across every entry instead of re-parsing argv or
  re-reading a multi-million-line CSV per file (the exact cost
  `loadListfile`'s own doc comment already worried about for
  `render_sample_driver.py`'s 130k-call driver loop). A single bad entry
  is reported and skipped, not fatal to the rest of the batch — real
  corpus worklists are large enough that one truncated/missing file
  shouldn't abort hours of unrelated work — with a final `N succeeded, M
  failed` summary line and an exit code reflecting partial failure.
  Output naming collisions (two entries sharing a basename, common across
  race/expansion subdirectories in a real corpus) fall back to
  `<parent-dir-name>_<basename>.glb`, then a numeric suffix. 5 new
  CLI-tier tests (`tests/test_cli_batch.cpp`, real subprocess spawns via
  `run_husk.hpp`, same convention as every other `test_cli_*.cpp`):
  multi-entry batch + comment/blank-line skipping, one-bad-entry-doesn't-
  abort, collision disambiguation, `--output`/`--input` mutual-exclusion
  errors, empty-worklist no-op. `README.md`/`TODO/CLEANUP_TODO.md`/
  completions updated (`--print-completion` regenerated, not hand-edited).
  Full suite green, 651/651. **Same pass, foreign-data robustness follow-up**:
  a real gap found matching the framing Luna flagged for this overnight
  session ("warn loudly on a recoverable failure, don't silently
  degrade") -- `--listfile` opening fine but parsing to zero usable
  entries (empty file, or every line malformed -- wrong file entirely, or
  corrupted mid-download) previously degraded with no visibility at all:
  every consumer already treats an empty listfile map the same as "no
  `--listfile` given" and falls back to local-only resolution correctly,
  but nothing ever told the caller that fallback had silently kicked in
  for a flag they explicitly passed. Fixed with a loud `std::cerr`
  warning right after the load, once, in `exportGlb` -- the export itself
  still succeeds via local fallback (recoverable, matches example 1 in
  Luna's framing), it just isn't silent about it anymore. A bad
  `--listfile` *path* (file doesn't open) already threw before this
  change, unaffected. New regression test in `tests/test_cli.cpp`. Full
  suite green, 652/652.
- **Previous state (2026-08-20, final)**: The filename-only path's
  "genuinely ambiguous" report for Dracthyr (previous entry below) turned
  out to be husk's own bug, not real caution — Luna asked to investigate
  after noticing the real `character/dracthyr/` folder has three files
  (`dracthyrmale.m2`/`dracthyrfemale.m2`/`dracthyrdragon.m2`), suspecting
  the ambiguity wasn't real once the specific file is known. Confirmed
  via the real `ChrModel.DisplayID -> CreatureDisplayInfo.ModelID ->
  CreatureModelData.FileDataID` chain: `dracthyrmale.m2`'s own FileDataID
  resolves to exactly `ChrModelID` 127, not 89 (the shared dragon form) —
  the previous entry's own claim that `dracthyrfemale.m2` derives
  `ChrModelID` 89 was also wrong on the same grounds (real answer: 128).
  The two were only ambiguous because race+sex alone is a broader
  question than a specific input file actually answers.

  Added a second, more precise derivation path, tried first:
  `chrrace::deriveChrModelIdFromFileDataId` (`src/chrrace_db2.hpp`/`.cpp`)
  chases `CreatureModelData.FileDataID -> CreatureDisplayInfo.ModelID ->
  ChrModel.DisplayID` given the model's own real FileDataID — resolved
  via `--listfile`/`--listfile-root` (a new `findFileDataIdForModelPath`
  in `cmd_export.cpp`, a linear reverse scan over the already-loaded
  FileDataID->path listfile map, not a second indexed copy, since it
  only runs once per export). Per Luna's explicit instruction ("this
  should still work without the listfile, so implement the listfile as
  the primary path with fallback onto the name matching"): the
  filename-only race+sex path (previous entry) is now strictly a
  fallback, used only when no FileDataID was found via `--listfile` at
  all — once the FileDataID path resolves anything (including a genuine
  ambiguity report from it specifically), that answer is trusted over the
  weaker fallback, never silently overridden by it. Verified end to end
  against all three real Dracthyr files, each cross-checked directly
  against `chrmodel.db2`/`creaturedisplayinfo.db2`/
  `creaturemodeldata.db2` before trusting the CLI's own output:
  `dracthyrmale.m2` -> 127, `dracthyrfemale.m2` -> 128,
  `dracthyrdragon.m2` -> 89 (all three, real files, `--listfile
  community-listfile.csv --listfile-root /media/luna/data/wow_export`).
  The filename-only fallback (no `--listfile`) still correctly reports
  the 89/127 ambiguity when the FileDataID path isn't available — real
  local data confirms this genuinely happens (Dracthyr's dragon form is
  a real, valid answer to "Dracthyr, male" too, just not the *specific*
  file being exported). 1 new CLI-tier test (`ChrModel`/
  `CreatureDisplayInfo`/`CreatureModelData` synthetic fixtures via the
  existing `buildFlatDb2`, plus a `--listfile` CSV fixture matching
  `tests/test_cli.cpp`'s own existing convention). `README.md`/`TODO/
  TODO_correctness.md`/`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`/`TODO/
  README.md`/completions all updated (including correcting the previous
  entry's wrong "dracthyrfemale.m2 -> 89" claim wherever it appeared in
  committed docs). Full suite green, 646/646.
- **Previous state (2026-08-20, latest)**: `--chr-model-id` now also
  accepts the literal value `auto`, deriving a real `ChrModelID` from the
  input `.m2`'s own filename instead of requiring the caller to already
  know the ID — closing most of the gap flagged at the end of the
  previous entry below ("i want char 89" wasn't the actual workflow;
  "file dracthyrfemale.m2 -> husk -> glb" is, and the filename already
  carries the race+gender the DB2 chain needs). New `src/chrrace_db2.hpp`/
  `.cpp`: parses WoW's real client-side naming convention
  (`<ClientFileString><"male"|"female">[_hd].m2`, verified against real
  local data, e.g. `character/bloodelf/female/bloodelffemale_hd.m2`) and
  resolves it against `chrraces.db2`'s `ClientFileString` column joined
  through `chrracexchrmodel.db2` — both previously genuine 0-byte files
  locally, fetched via `tact-fetch` this session (FileDataID 1305311 for
  `chrraces.db2`, a real network call, confirmed missing via a dry-run
  first) and placed the same way as the previous entry's three tables.
  Deliberately an *exact*, case-insensitive match only, never fuzzy —
  Luna's explicit instruction: "if user opens file 'dracthyrfemale' match
  it to a dracthyr female... if the user opens file 'dwagon_biddies_69'
  that's not gonna match dracthyr female no matter how hard you try".
  Real wrinkle found and handled, not assumed away: a (race, sex) pair
  isn't always 1:1 with `ChrModelID` — confirmed via SQL join that
  Dracthyr male resolves to two genuinely distinct real `ChrModelID`s (89
  dragon form, 127 Visage form), a real alternate-form case, not a data
  bug. `deriveChrModelId` only returns a value when every match collapses
  to exactly one distinct `ChrModelID`; a genuine ambiguity is reported
  (naming both candidates) and left for the caller to resolve with an
  explicit `--chr-model-id <id>`, never guessed at — same for "filename
  doesn't fit the convention at all" and "race token matches no real
  `ChrRaces` row". Verified end to end against real local data (a
  synthetic `dracthyrfemale.m2` derives `ChrModelID` 89 in this
  filename-only test, since the synthetic fixture only defines one real
  ChrModelID for that race+sex — NOTE, corrected in the entry above: the
  *real* `dracthyrfemale.m2` derives `ChrModelID` 128, not 89, once the
  more precise FileDataID-based path exists; 89 is Dracthyr's shared
  dragon form, a different real model); `dracthyrmale.m2` correctly
  reports the 89/127 ambiguity and skips rather than picking one; a real
  `bloodelffemale_hd.m2` (`test_data/`) derives `ChrModelID` 20,
  independently cross-checked (race 10 = BloodElf, sex 1 -> `ChrModelID`
  20). 3 new CLI-tier tests (exact match, ambiguous match, no match — a
  new `buildChrRacesDb2` synthetic-fixture builder, same
  real-`resolveFieldString`-path convention as the previous entry's
  `buildOptionOrChoiceDb2`). `README.md`/`TODO/TODO_correctness.md`/`TODO/
  CHAR_TEXTURE_COMPOSITING_TODO.md`/`TODO/README.md`/completions all
  updated. Full suite green, 645/645.
- **Previous state (2026-08-20, later)**: Found and placed the three
  `ChrCustomizationOption`/`_Choice`/`_Category` files tact-fetch fetched
  last session (2026-08-19) but never placed — they were sitting in a
  stale session scratchpad dir, several near-duplicate copies (an
  earlier, wrong-locale pass vs. the real final English-string pass;
  verified by content, not filename, which copies were correct). Placed
  at `/media/luna/data/wow_export/dbfilesclient/
  chrcustomization{option,choice,category}.db2` (previously genuine
  0-byte placeholders there), verified end to end via `husk db2-export`
  (1148 real `ChrCustomizationOption` rows, real `Name_lang` strings).
  Also found `~/dev/tact-fetch`'s own README was stale (still said "CDN
  fetch not implemented" when `CLAUDE.md` showed it fully built and
  live-verified) — fixed there too. Then implemented the two things this
  data unblocks (`TODO/TODO_correctness.md` #2, `TODO/
  CHAR_TEXTURE_COMPOSITING_TODO.md` Stage 3's name-mapping half):
  `chrcustomization_db2.hpp`/`.cpp` now loads `ChrCustomizationOption`/
  `_Choice` (real `Name_lang` strings, needing a new
  `db2table::readNamedStringColumns` — the existing named-column reader
  was scalar-int-only; moved `stringOffsetSectionCorrection` from
  `cmd_db2.cpp` into `db2.hpp`/`.cpp` as `db2::stringOffsetSectionCorrection`
  so both readers share one implementation, not two) and exposes
  `namedChoicesForModel` (real Option/Choice names paired with their
  resolved geoset/boneset selector) and `defaultChoiceIdsForModel` (the
  lowest-`OrderIndex` choice per option for a given `ChrModelID` — husk's
  own heuristic, explicitly *not* a client-verified default the way
  `--creature-display-id`'s is). New `husk export --chr-model-id` flag
  wires this in: auto-selects and resolves default choices when no
  explicit `--customization-choice-ids` is given (which still wins if
  both are given), printing every real `OptionName -> ChoiceName` pair
  used. Verified end to end against real local data: `ChrModelID` 89
  (Dracthyr) resolves 45 default choices, 7 with real geoset selections
  (e.g. "Ears -> Short Fin", `OrderIndex` 0), cross-checked directly
  against the source DB2 rows via SQL. New CLI-tier tests in
  `tests/test_cli_chrcustomization.cpp`, including a new synthetic
  string-bearing WDC5 fixture builder (`buildOptionOrChoiceDb2`) that
  exercises the real `db2::resolveFieldString` path rather than mocking
  it. `README.md`/`TODO/README.md`/`TODO/TODO_correctness.md`/`TODO/
  CHAR_TEXTURE_COMPOSITING_TODO.md`/completions all updated. Full suite
  green, 642/642. Also did an unrelated cleanup while investigating where
  the tact-fetch output had gone: 397 stale Claude session scratchpad
  dirs (~4.6 GB) removed across all projects under `/media/luna/work/
  cache/tmp/claude-1000/`, keeping only the then-current session.
- **Previous state (2026-08-20, earlier)**: Fixed the `db2::resolveFieldString`
  multi-section string-offset bug (`TODO/TODO_correctness.md` #4, now
  closed/removed). Root cause: WDC2+ string offsets are relative to a
  *virtual* blob the client assembles at load time — every section's
  record data back to back, then every section's string block back to
  back (`documentation/wowdev-wiki/md/DB2.md`'s WDC2 "String Block"
  section, explicit about this and about a real Blizzard build once
  shipping broken DB2s from missing this exact correction). A
  single-section file's real layout already matches that virtual blob, so
  the old single-addition formula happened to work for the common case —
  but a multi-section file (this repo's own `test_data/db2/
  chrcustomizationcategory.db2`, 2 sections) needs the gap bridged:
  subtract every later section's record-data size, add every earlier
  section's string-block size. New `cmd_db2.cpp::
  stringOffsetSectionCorrection`, threaded through `decodeRecordValues`
  via a new `sectionIndex` parameter at all 3 call sites. Verified
  end to end: `chrcustomizationcategory.db2` row 0/1/2/3/4's
  `CategoryName_lang` now decode as `"Body"`/`"Face"`/`"Accessories"`/
  `"Hair"`/`"Markings"` (previously `"cessories"`/`"ries"`/garbage —
  provably wrong, not NUL-preceded); `chrcustomizationoption.db2` (also
  multi-section) now decodes real English option names (`"Skin Color"`,
  `"Face"`, `"Hair Style"`, `"Hair Color"`, `"Facial Hair"`). Full suite
  green, 641/641. **Follow-up, same session**: `chrcustomizationchoice.db2`
  row 1 field 0 initially still decoded as garbage (`".\x8c}\xff"`) even
  after the fix above — turned out to be a second, direct consequence of
  the same fix rather than an unrelated heuristic bug: `rawValue == 0` is
  WDC2+'s explicit "no string" sentinel (real client code special-cases
  it before computing any position, confirmed in `reference/wow.export`'s
  `WDCReader.js`: `if (ofs == 0) out[prop] = ''`), but `resolveFieldString`
  never special-cased it -- harmless before today's section-correction fix
  (a zero offset resolved to the field's own record bytes, reliably
  non-printable), but the correction can now be negative, sending a zero
  offset into real binary data (pallet_data, confirmed via `od` at file
  offset 17332) that occasionally passes the printable-byte heuristic by
  coincidence. Fixed with an explicit `rawValue == 0` early return in
  `db2::resolveFieldString` (`src/db2.cpp`). Verified: all of
  `chrcustomizationchoice.db2`'s zero-offset rows now correctly fall back
  to raw int `0`; `chrcustomizationcategory.db2`'s real names unaffected.
  Full suite still green.
- **Previous state (2026-08-19)**: Added real creature default-geoset
  selection (`husk export --db2-dir/--dbd-dir --creature-display-id`,
  `src/creature_geoset_db2.hpp`/`.cpp`, `gltf::Skeleton::
  CreatureEnabledGeoset`) — resolves `CreatureDisplayInfoGeosetData.db2`
  into real, no-caller-input-needed default geoset selections, unlike the
  existing player-character `--customization-choice-ids` chain which needs
  per-choice IDs supplied. Verified end to end against real local data
  (`creature/gnoll2/gnoll2.m2`, display 137795, 13 rows, gltf_validator
  clean). Full suite green, 641/641. Also exported all 23 real HD
  character models to `.glb` (`/media/luna/work/cache/husk/
  hd_character_export/`, `tools/export_hd_characters.nu` — the repo's
  first `.nu` file) for a manual player-character bug hunt — see
  `CLAUDE_HISTORY.md`'s 2026-08-19 entry for the full narrative, including
  a real `--skin auto`+`--lod` fallback gap found along the way
  (`TODO/CLEANUP_TODO.md` #4) and the still-unexplained `scourgemale_hd`
  NaN keyframe (#1 failure of 23, not yet investigated).
  **Same-day follow-up**: chased real `ChrCustomizationOption`/`_Choice`/
  `_Category` name data (for the geoset-defaults work above) through a
  full cross-project loop — found genuinely blocked locally (bytes never
  downloaded, not a re-extraction bug), unblocked via the sibling
  `tact-fetch` project (two real bugs found and fixed *there*: a missing
  `CASC_OVERCOME_ENCRYPTED` flag causing silent truncation, and a locale
  override that only touched the wrong CascLib storage handle), and then
  a third, real bug found in **husk itself** once real English data was
  finally in hand: `db2::resolveFieldString` misresolves some string-field
  offsets (confirmed at the byte level — `TODO/TODO_correctness.md` #4).
  Deliberately left unfixed this session (root cause not found, and it's
  currently a latent bug — no shipped feature reads strings through this
  path). Real fixture data now lives in the repo:
  `test_data/db2/chrcustomization{option,choice,category}.db2`.
- **Next step**: this session's own two items (human-readable names +
  Blender Asset Browser, then `CHAR_TEXTURE_COMPOSITING_TODO.md` Stage 6
  equipped-gear resolution) are all fully closed, nothing queued from
  them -- Stage 6's own "not started" mention two paragraphs below is now
  stale, see the current-state entry above instead. Otherwise unchanged:
  `TODO/TODO_correctness.md` #2's name-mapping/default-
  choice work, and `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stages 1-5
  in full (model identity, real placement geometry, the real material
  FileDataID chain, and the live Blender-side customization texture
  switch replacing the reverted pixel compositor) are all now done — see
  the current-state entry above for Stage 5, `CLAUDE_HISTORY.md` for the
  full narrative. `TODO/CLEANUP_TODO.md`'s former item 3 (`husk export`
  batch mode) is also done. What's left of `CHAR_TEXTURE_COMPOSITING_TODO.md`:
  Stage 5's own real interactive Blender GUI pass with real (not
  placeholder) per-choice texture bytes, still Luna's own eyes to do
  (`TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`'s own "Still open" section),
  and Stage 6 (equipped-gear appearance resolution via
  `ItemModifiedAppearanceID`), not started; a model whose FileDataID
  can't be resolved via --listfile and whose filename doesn't follow the
  naming convention still needs an explicit `--chr-model-id <id>`. The manual visual pass over the 22
  successfully-exported HD character `.glb`s (deriving sane per-race/
  gender geoset defaults, hunting further player-character bugs) is still
  Luna's own next action, not queued husk work — if it turns up bugs,
  they'll be new entries here. Otherwise unchanged from before:
  `MULTI_TEXTURE_LAYER_TODO.md`'s step 5, `RENDER_QUALITY_TODO.md`'s
  ambiguous-pool tiebreak/blank-render follow-ups, the dangling-internal-
  reference corpus scan (`CLEANUP_TODO.md`, now item 2), `CLEANUP_TODO.md`
  item 1's comment-hygiene sweep, and `BONE_NAME_DEDUCTION_TODO.md`'s
  Tier 2 (still needs a design pass). The foreign-data-robustness gap
  Luna flagged for this overnight pass is partially closed: an empty-
  after-parsing `--listfile` now warns loudly instead of silently
  degrading (see the current-state entry above). Not yet audited: the
  "genuinely needed, no fallback exists" half of Luna's framing (example
  2 — an unrecoverable error is the *correct* response when no fallback
  can produce a correct answer) — no concrete case has been found yet
  where a husk feature both requires listfile/DB2 data and silently
  produces wrong output instead of erroring when that data is missing;
  worth a deliberate pass over `--knowledge-db`/`--chr-model-id auto`/
  `--char-layout-id` et al. specifically hunting for that shape, not just
  the "listfile missing -> warn and continue" shape already covered.
  Also not audited: a listfile that's present, non-empty, and *parses*
  successfully but contains wrong/stale data (rows pointing at paths that
  don't exist, or a stale snapshot from an older game version) — a
  different failure mode than "corrupted" (this parses fine, the fix
  above doesn't see it), closer to the DB2 "known-wrong, not just
  unverified" case `KNOWLEDGE_BASE_DESIGN.md` already documents for a
  different chain.

<details>
<summary>Previous entry (2026-08-16), preserved for context</summary>

- **Previous state**: picked up
  `TODO/EXPLORATION_TODO.md`. Mapped and quantified the real DB2 chain
  from a `.m2` FileDataID to a texture FileDataID (full narrative:
  `CLAUDE_HISTORY.md`'s later 2026-08-16 entry, "`EXPLORATION_TODO.md`
  follow-up"): `.m2` FileDataID → `ModelFileData.db2` →
  `ModelResourcesID` → `ItemDisplayInfo.db2`'s `ModelResourcesID_0`/`_1`
  → `ID`/`ModelMaterialResourcesID_0`/`_1` → `TextureFileData.db2`'s
  `MaterialResourcesID` → real texture `FileDataID`. Shorter than
  originally guessed (no `Item`/`ItemAppearance`/`ItemModifiedAppearance`
  hop needed) and one guessed table (`ComponentTextureFileData.db2`) was
  wrong — the real target is `TextureFileData.db2`. Verified against a
  real file end to end. Quantified across all 4,733
  `replaceable_only` files from the earlier session's unfillable-texture
  scan: all 4,733 resolve through `ModelFileData`; 4,220 (89.2%) further
  resolve to a real `ItemDisplayInfo` row with a nonzero
  `ModelMaterialResourcesID`. **Every one of those 4,220 still dead-ends
  at the last hop** — `texturefiledata.db2` is a genuine 0-byte file
  locally, a fresh casc-tool re-extraction gap, not something this
  session's earlier `recordsAvailable()` fix touches. Also confirmed (not
  assumed) that the 144 excluded `character/` files are still blocked by
  `CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 3 0-byte
  `chrcustomization*.db2` tables, unchanged. Re-audited every DB2
  consumer in `src/` for the same `recordsAvailable()` blast radius as
  the earlier fix — none had the bug, nothing else to re-run.
  **Same-day follow-up**: casc-tool re-extracted `texturefiledata.db2`
  (was 0 bytes); re-verified the final hop of the chain against real
  data — 4,216 of the 4,733 `replaceable_only` files now resolve end to
  end to a real, locally-present texture FileDataID (checked via real
  listfile path, not filename-by-ID — an early mistake this session,
  caught before it produced a wrong answer). No casc-tool ask remains
  for this bucket. `EXPLORATION_TODO.md` trimmed to just the real
  remaining work (implement the chain in husk, re-render) per its own
  "punch list, not a log" convention.
  (Superseded: see the current-state entry above the `<details>` fold for
  the up-to-date Next step.)
- **Hazards**: `/media/luna/work/cache/husk/knowledge.sqlite` (`husk
  db2-build`'s output, including `model_object_skin_verified`, its
  `Item.InventoryType`-filtered table) still contains **real but
  incomplete** object-skin data — the slot filter eliminates
  cross-category errors (a weapon texture for a helmet) but same-slot
  cross-item collisions are still common (confirmed: 15/15 random
  spot-checked "verified" answers were plausible-slot but wrong-item).
  Don't pass `--knowledge-db` to `husk export` for real output
  (`render_sample_driver.py` already doesn't); the feature that actually
  works now is the local race/gender-suffix fallback, which needs no
  flag at all. A scratch `db2.sqlite` (full corpus DB2 export, built
  with `husk db2-export --dir /media/luna/data/wow_export/dbfilesclient
  --dbd-dir reference/WoWDBDefs`, plus a `listfile_raw`/`corpus_paths`
  import from `community-listfile.csv` and
  `corpus_reports/unfillable_textures_full.csv`) was used for this
  session's real-data joins and lives only in the session scratchpad —
  not committed, not a persistent artifact, rebuild it fresh if this
  chain needs re-querying rather than assuming it still exists.
  `tools/corpus_scan_tasks/unfillable_texture_task.py` is
  the one true source for "does this file's texture actually resolve" —
  `missing_texture_task.py` (same directory) is an older, deliberately
  simpler check that only looks for a literal `<FileDataID>.blp/.png`
  next to the model and will over-flag anything the listfile or fuzzy
  tiers would actually resolve; don't treat its output as authoritative
  for exclusion decisions. Any future fast Python reimplementation of
  husk's own resolution logic must mirror all three real tiers (literal →
  `--listfile` → fuzzy same-basename, `export_materials.cpp:437-456`) —
  dropping the seemingly-least-important one is exactly what caused this
  session's core bug. `tools/full_render.py`'s `.renderignore` is now the
  real exclusion mechanism for the render entry point; the old
  `corpus_reports/full_corpus_file_list.textured_only.txt`-style scan-
  result-subtraction file lists are superseded, not a second source of
  truth to keep in sync. `export_texture_resolution.hpp`/`.cpp` (split
  out of `export_materials.cpp` in an earlier session) still owns every
  texture-candidate-resolution helper in the real C++ pipeline — check
  there first, not `export_materials.cpp`, if a texture-resolution doc/
  comment citation looks stale. Any DB2 code that checks
  `section.header.tactKeyHash != 0` directly to decide whether a section
  is readable is checking the wrong thing — use
  `db2::Section::recordsAvailable()` instead (`db2.hpp`); a nonzero
  `tactKeyHash` means the section *would* be TACT-key-gated if the key
  were missing at extraction time, not that husk's own bytes are still
  ciphertext, and a real CASC extraction usually already decrypts these
  before the file reaches disk (111/126 real cases this session). husk
  has no TACT key store and no Salsa20 implementation, deliberately —
  don't add one; the actual bug this session was husk ignoring data it
  already had, not a missing crypto feature.

</details>
