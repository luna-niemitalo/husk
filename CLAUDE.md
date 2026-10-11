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
  its flag bit is set — see CLAUDE_HISTORY.md), `husk export` (static mesh → skeleton +
  skinning, inline or external `.skel` → materials with real embedded textures →
  animation, inline/external-`.anim`/`.skel`-sourced (external `.anim`
  resolution now falls back to the real `wow.export`-shaped same-basename
  filename convention, not just `<FileDataID>.anim`, when a FileDataID-mapped
  file isn't found — see CLAUDE_HISTORY.md), verified against real
  `bloodelffemale.m2`/`bloodelffemale_hd.m2` data), `husk export --lod`
  (single-tier or `all`), `husk export --bones-dir` (real `.bone` correction
  data attached as inert `bone_correction_sets` glTF skin `extras`, never
  applied to the render), `husk export --phys` (real `.phys` physics/collision
  body data attached as inert `physics_bodies` glTF skin `extras` — minimal
  per-body placement anchors only, never applied to the render — see CLAUDE_HISTORY.md),
  `husk export --db2-dir/--dbd-dir/--char-layout-id` (real DB2-derived
  character texture-layout geometry — base atlas size, real placement rects,
  real texture-layer blend info — attached as inert `chr_texture_layout`
  glTF skin `extras`, keyed by a caller-supplied `CharComponentTextureLayoutsID`
  since husk can't derive one on its own; see CLAUDE_HISTORY.md),
  `husk export --db2-dir/--dbd-dir/--creature-display-id` (real
  `CreatureDisplayInfoGeosetData`-derived default geoset selection for
  creatures/NPCs — a true default, no per-choice caller input needed, unlike
  the player-character `--customization-choice-ids` chain above — attached
  as inert `creature_enabled_geosets` glTF skin `extras`; see CLAUDE_HISTORY.md),
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
  files instead, see CLAUDE_HISTORY.md — now get real structural parsing too, not a
  raw hex dump; or `.bone`/`.phys`
  files directly — `.phys`'s full body/shape/joint/`PHYV` record set, each
  shape/joint resolved to its real type-specific data inline, see CLAUDE_HISTORY.md).
  `blp/`'s `husk-blp` (BLP2 → PNG:
  palettized/DXT1/DXT3/DXT5/BGRA — DXT3 turned out to already be wired
  through the same generic decode path as DXT1/DXT5, just unverified until
  this session's real-corpus scan, see CLAUDE_HISTORY.md). `husk export`'s CLI grammar is CLI11-based named
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
- **Architecture direction (2026-08-28, documented, not implemented)**: the
  agreed target is `M2 -> canonical -> Blender` via a four-stage pipeline
  (`parse -> resolve -> canonical -> write`), one resource-resolution
  boundary object, a native husk bundle format, and a real packaged Blender
  addon; glTF becomes an optional best-effort projection. Written up in
  `REFACTOR/` (index: `REFACTOR/README.md`), including `REFACTOR/AUDIT.md`'s
  evidence inventory of every duplicated/divergent path found. **Stages 1-2
  have since landed and Stage 3 is deep in progress** (`src/sources/`'s
  resolution catalog, `m2::Model`'s consolidation, and `canon::`'s own
  value types/assembly functions/`--export-canon` convergence checker) —
  see `REFACTOR/README.md` for the live stage plan and status (the file
  this session's own `LOOP_STATE.md` reference used to point at no longer
  exists; `REFACTOR/README.md` is the current single source for this).
  Everything under Current/Boundaries below otherwise still describes the
  real tree; only the in-progress refactor's own files are ahead of it.
- **World export (ADT terrain)**: `husk export-terrain` (one ADT tile →
  terrain bundle: heightfield, 0–8 splat layers + alpha, ground-cover
  rules, `MH2O` liquid, world-frame `MDDF`/`MODF` placements, both
  FileDataID- and name-table-named) and `husk export-world` (every tile of
  a map or world, parallel, resumable, models exported once via `husk
  export --bundle-only`, ground textures shared). Verified facts:
  `WIKI_FINDINGS/ADT.md`; consumer schema: `REFACTOR/BUNDLE_FORMAT.md`'s
  "Terrain tile bundles"; open items: `TODO/WORLD/ADT_TERRAIN_TODO.md`,
  `WORLD_PLACEMENT_TODO.md`. WMO geometry is not implemented, so `MODF`
  placements are identity-only. MantleCore is the bundle's real consumer.
- **Target**: a real Blender import path for modern (Legion+ chunked) M2 — see
  `DESIGN.md`'s Goal section. All 8 roadmap stages are now done, including stage 7
  (output hardening: real exports now run through the Khronos glTF-Validator *and*
  headless Blender itself, `tests/test_conformance.cpp` — see CLAUDE_HISTORY.md). `AFSB`
  (`.skel`-linked models' real external-animation format, previously the single
  biggest animation gap) is now cracked and resolved end to end — see CLAUDE_HISTORY.md.
  M2-source-vs-exported-glb-vs-Blender-readback cross-checks (former
  `VERIFICATION_IDEAS.md`, now deleted — its survey's job was done, every
  case had a final disposition, folded back into `tests/test_conformance.cpp`/
  `WIKI_FINDINGS.md` §5, same scratch-doc lifecycle `DESIGN_CHANGES.md`
  had) are now implemented too, plus a real collision-mesh export husk
  never had before. `M2Particle`/`M2Ribbon` (weapon glow trails, magic/fire/
  smoke — the single biggest remaining visual-identity gap this tool had) are
  now fully parsed, every field and every resolved animation curve, split
  between a minimal glTF placement anchor and `husk dump-chunks`'s full JSON
  output — see CLAUDE_HISTORY.md. Remaining work is either scope expansion
  (WMO/M3, not started, by design) or the structural gaps this project
  already tracks (`M2Camera`, low-priority by design, see `M2_COMPLETENESS.md`;
  `.bone` correction *selection* — the extras-export half is done, see
  `TODO/BONE_CORRECTION_APPLICATION_TODO.md` and CLAUDE_HISTORY.md; picking which
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
  more than one root bone, see CLAUDE_HISTORY.md). `M2_GAPS_TODO.md`'s full item
  bundle (ten items across several sessions, most recently `PCOL`
  player-housing collision, diagnostic-only via `husk dump-chunks`,
  verified against all 2,354 real `PCOL`-bearing files -- see CLAUDE_HISTORY.md) is
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
- ADT terrain tiles (`src/adt.cpp`): root `<map>_<x>_<y>.adt`, its `_obj0`/
  `_tex0` siblings, and the map `.wdt`'s `MPHD` flags -- same local,
  user-extracted tier as every sidecar above. Name-table paths
  (`MMDX`/`MWMO`/`MTEX`) are resolved through `--listfile` only
  (`sources::fileDataIdsForGamePaths`).
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
in place each session; every prior version of this snapshot already lives
in `CLAUDE_HISTORY.md` in equal or greater detail (confirmed 2026-09-16
during a docs-consolidation pass — this section had drifted into a full
duplicate log, contradicting its own "snapshot, not a log" rule; compressed
back down, nothing lost since it was already all in `CLAUDE_HISTORY.md`).

- **Current state (2026-09-16)**: legacy `gltf_*.cpp`/`cmd_export.cpp`
  pipeline is feature-complete per the Status section above — all 8
  roadmap stages done, corpus-hardening passes done, character-texture
  DB2 resolution chain (layout/placement/blend/customization-choice/
  equipped-gear) done, live Blender-side node-graph texture switching
  done (replacing an earlier, deliberately-reverted software compositor).
  Active work has moved to `REFACTOR/`'s canon:: migration (Stage 3 deep
  in progress — see the Status section's "Architecture direction" entry
  and `REFACTOR/README.md` for the live stage plan). Legacy-pipeline work
  now happens only when it's genuinely dual-use (benefits canon:: too,
  e.g. DB2 parsing completeness) — new legacy-only feature work is not
  the current priority.
- **Next step**: `tests/bundle_import_check.py` (2026-09-16) is the first
  real code that reads a husk bundle's `manifest.json` straight into
  Blender with zero glTF involved — mesh + skeleton (verified against
  `bloodelffemale.m2`) + base-color materials (verified against
  `test_data/creature/fox/fox.m2`, a real, simple, non-character fixture
  added this session — deliberately not a real character model, see
  `REFACTOR/BLENDER_ADDON.md`'s new note for why) — matching the legacy
  `.glb`'s own counts exactly, and `--export-canon` reports clean against
  it. The texture overlay/switch/
  picker pipeline (customization choices, geoset selection, multi-layer
  blending) is NOT covered at all and is a much larger, separate slice:
  `canon::Definition`/`Selection`/`Item` exist but nothing assembles them
  from real input, and `bundle_writer.cpp` doesn't write them yet either —
  that's most of `BLENDER_ADDON.md`'s `customization.py`/`geosets.py`/
  `materials.py` (blend modes, tint, UV animation), needing real canon-side
  wiring before any Blender-side reading of it is possible. Natural next
  *bundle_import_check.py* slice within current scope: animation (Actions
  from `resources.animation`) or geosets, so a canon-vs-legacy visual
  comparison can eventually run without
  `tools/compare_canon_render.py`'s current gltf-on-both-sides detour.
  Otherwise continue closing
  `REFACTOR/AUDIT.md`'s Stage 3 gaps (see that file for the current
  concrete list) and the documentation consolidation pass in progress as of
  2026-09-16 (see `REFACTOR_LOG.md`'s newest entries and this file's own
  git history for that pass's scope). Outside of `REFACTOR/`:
  `TODO/INVESTIGATIONS_TODO.md` item 14
  (`m2_full_validation_task.py`'s real full-corpus-scale hang, clean on
  every bounded reproduction — needs a live-attach investigation, not more
  guessing from a killed run) is the one open legacy-pipeline item with no
  current owner.
- **Target and priority (2026-10-09, Luna)**: the canonical bundle is the
  export target (MantleCore consumes it); glTF/Blender are tertiary.
  World order: WMO + all world animations → attachments/lights/emitters/
  fog & particle volumes → liquids → culling domains/LODs; model detail
  (overlays, UV animation) alongside. See `WORLD_COMPLETENESS.md`.
  Done: M2 attachments/events/lights/emitters in `canon::Model::scene` and
  the bundle (incl. `.skel` `SKA1`). WMOs export as object bundles with
  their doodad sets as placement sets (`husk export-wmo`,
  `REFACTOR/PLACEMENT_SETS.md`); terrain tiles place doodads and WMOs as
  two sets (MWDR/MWDS resolved), and `export-world` exports every placed
  WMO -- **MantleCore must switch from the old `placements` array to
  `placement_sets`**. WMO `MNLD`/`MOLT` lights are
  `resources.scene.static_lights`, each `MNLD` light switching with its doodad
  set; `MLIQ` liquid is `resources.geometry.liquids` with LiquidType resolved.
  Fog and volumes are effects (`REFACTOR/EFFECTS.md`, literal tier, every
  effect a placeholder): WMO `MFOG`, `MAVG`/`MAVD`/`MBVD` and `MPVD` are
  effect instances in the doodad set each applies with, parts naming them
  as `{set, id}`; map-wide `_fogs.wdt`/`_mpv.wdt` volumes are the world
  document's `fogs` and `particulate_volumes` sets (`husk export-map`,
  `export-world` pass 4). Per-element data
  goes in generic mesh attribute channels (`canon::AttributeChannel`,
  `resources.geometry.mesh.attributes`; REFACTOR/BUNDLE_FORMAT.md): WMO face flags/material
  are the first, and select collision; M2 collision is
  `resources.geometry.collision`. Portals are `resources.geometry.mesh.portals` with
  per-part links.
- **Bundle format as structure (2026-10-10, Luna's call, in progress)**:
  five document kinds (`model`, `placement_set`, `world`, `effect`,
  `material`) with one
  contract header (`REFACTOR/BUNDLE_FORMAT.md` "Document kinds and the
  contract header"). `schema/*.schema.json` is the definition (JSON Schema
  draft-07, embedded in the binary); `src/bundle_schema.*` validates
  documents (`husk validate-bundle`) and lints the schema for format
  antipatterns. Project is C++23. **All four stages done (2026-10-11)**:
  every document husk writes is one of the kinds and validates;
  every writer test checks what it writes. Terrain tiles are `model`s in
  their own frame (`resources.terrain`); each map gets a `world` document
  (`maps/<map>/<map>.world.bundle`: grid, map-wide volumes), replacing
  `map_scene` (`export-map-scene` is now `export-map`). Verified on real
  data: tile seams, the azeroth grid, volumes on cells, and
  `tests/terrain_bundle_import_check.py` (takes a world bundle and tile
  names). **MantleCore must follow**: tile-local frames placed by the
  world grid, the model-document tile shape, the world document. Every
  bundle's subject carries its FileDataID (M2: passed by export-world,
  else store name or listfile, `sources::fileDataIdForLocalFile`). The
  WDT (`adt::parseWdt`) decides a world's tiles (`MAIN`; the store keeps
  11,014 stale tiles it no longer lists), names them (`MAID`), and places
  a terrain-less map's WMO as the world's off-grid `placements` (228
  maps). Nothing open in the format work itself; MantleCore's migration
  guide is `REFACTOR/MANTLECORE_MIGRATION.md` (scratch, delete once it has
  migrated). Liquids (2026-10-11): `resources.liquid_types` (terrain and
  WMO; DB2 names, materials, texture slots) and
  `resources.terrain.liquid_objects`, plus MH2O fishable/deep masks. Open
  per feature: LOD (`MFOB` exists but is always empty,
  `FOG_VOLUMES_TODO.md` §1), `TODO/WORLD/LIGHTING_TODO.md`,
  `LIQUID_TODO.md` §1–2, `FOG_VOLUMES_TODO.md` §2–4.
  Client shaders are unpacked under `example_exports/shaders/`
  (`tools/export_shaders.py`).
- **Effects (2026-10-11, `REFACTOR/EFFECTS.md`)**: the fourth document
  kind, `effect` (bodies checked in as `effects/*.json`, embedded),
  placed by placement-set instances that supply typed inputs, a classic
  `style` and `unresolved` fields. Every WMO and map volume source, and
  every M2 particle/ribbon emitter (bone-anchored, in an inline effects
  set), is on it as a placeholder effect (literal tier, Phase 2 done);
  `validate-bundle` warns on placeholders and `--release` fails.
  Phase 3 started (four derivation rules, provenance on instances);
  Phase 4: each map's `Light` zones are zone fog instances, their fog by
  time of day from `LightData` (`lightdb`; export-map/export-world
  `--db2-dir`); WMO/map particle emitters carry a derived light role.
  **Next** (each needs a design call or Luna): zone fog as a medium (a
  medium is one value; zone fog is keyed by time of day), deduplicating
  `zone_fogs`' shared LightParams (azeroth 7.8 MB), M2 particle light
  roles (blend ids unsettled), Phase 5 classification before 1.0
  (mechanical grouping checked by derivation coverage, no screenshots). Effect documents are written per
  bundle (`<bundle>/effects/`), not shared across an export-world tree
  yet. The format is at schema_version 0.1.0.
- **Creature options (2026-10-11, Luna's shape)**: the model declares
  options, each valid alone (renderer chooses): texture slots with their
  texture options (layers name their `slot`), particle colour options,
  the mesh's geosets; `selection.presets` saves the combinations the
  client uses and where (with scale). Filled for M2 creatures from
  CreatureModelData -> CreatureDisplayInfo (`creaturedisplay`,
  `creatureinput`; `export --bundle-only` and `export-world` with
  `--db2-dir`/`--dbd-dir`). REFACTOR/BUNDLE_FORMAT.md "Options and
  presets"; open items (creatures spanning several models is a design
  call, humanoid NPCs, conditional displays) in
  `TODO/CREATURE_OPTIONS_TODO.md`. Humanoid NPCs (2026-10-11) are presets
  on their character model: baked skin as a `skin` option, customization
  choices indexing `definition.customization` (every ChrModel drawing the
  file; options and geosets, no choice textures yet), items indexing
  `definition.items` (refs only so far). export-world puts every texture in one shared store.
- **Corpus store (2026-10-11, `REFACTOR/CORPUS_EXPORT.md`)**: `husk
  store-add` records an export tree as `builds/<game>/<husk>/` in a
  content-addressed store (versioned like casc-tool's, husk's own code);
  `husk mount` serves it read-only. Store `/media/luna/data/canonical_storage`
  (empty), mount `/media/luna/canonical_export` (command in
  `development/REFERENCES.md`). Documents no longer carry `exported_at`
  (deterministic). A stored build needs a clean producer: Luna's
  uncommitted `.gitignore` line makes this checkout `-dirty`. export-world
  writes `inputs.log` (each output's source files with md5, `src/input_log.*`;
  `*` = the run's DB2s and listfile), which store-add turns into
  `inputs.tsv`. `husk export-corpus` (export-world --all-assets over every
  map, index-tree, store-add) drives a whole build; `--sample N` projects
  one. Next: `TODO/CORPUS_EXPORT_TODO.md`.
- **Textures and refs (2026-10-11, Luna)**: each document lists its
  textures once in `resources.textures` and everything uses indices
  (layers, slot options, terrain, liquid frames, effect image inputs);
  refs omit `name`/`name_source` when there is no name.
  REFACTOR/BUNDLE_FORMAT.md "Textures"; MantleCore must follow
  (MANTLECORE_MIGRATION.md §1). The DB2
  parser now indexes CommonData once (it scanned per record).
- **Terrain export (2026-10-09)**: promoted from WIP to a tested,
  documented feature. Open items, in order: ground-cover scatter placing
  nothing on grassy quads (`TODO/WORLD/ADT_TERRAIN_TODO.md` §1), confirming
  MantleCore's south-edge seam cracks are in the source bytes (§2),
  placement yaw sign (`WORLD_PLACEMENT_TODO.md`). `tests/
  test_integration_terrain.cpp` runs against the real tile
  `test_data/world/maps/azeroth/azeroth_32_49.adt` (+ siblings + WDT,
  copied from the mount 2026-10-11; `test_data/` is gitignored) and
  passes.
- **Hazards** (standing facts, not narrative — see `CLAUDE_HISTORY.md` for
  the sessions that established each one):
  - **Corpus location (2026-10-10, Luna)**: the extracted corpus moves
    from `/media/luna/data/wow_export` (gone; `wow_export_old`/
    `wow_export_superseded` beside it are not the current one) to
    `/media/luna/wow_export` (game files under `live/`), a FUSE mount
    from the CASC work that makes source-data updates declarative and
    deduplicated. It may be absent. The mount command, flag paths and
    listfile are in `development/REFERENCES.md` (gitignored, machine-
    local); running that mount command is pre-approved whenever the mount
    is missing. `~/.config/husk/config.toml` (husk's own config, in
    scope) points at the mount. Tests, tools and usage docs point there
    too; evidence citations in TODO/findings docs keep the old path on
    purpose (they record where something was verified).
  - **Listfile names can belong to another FileDataID**: e.g.
    `helm_armor_ethereal_d_01_gn_m00.skin` is FDID 6217827, while that
    M2's SFID declares 6217937. Same-basename lookups (skins, fuzzy
    texture pool) can't detect this; the mount's `_by_fdid/<fdid>` can't
    be wrong. Every FDID lookup tries it (`sources::
    storePathForFileDataId`/`localFileForFileDataId`): SFID skins and
    AFID anims before the same-basename names, textures after the
    listfile tier, models/WMO groups/terrain textures after the listfile
    path.
  - `tools/corpus_scan_tasks/unfillable_texture_task.py` is the one true
    source for "does this file's texture actually resolve" —
    `missing_texture_task.py` is an older, simpler check that over-flags;
    don't treat its output as authoritative for exclusion decisions. Any
    reimplementation of husk's texture resolution must mirror all three
    real tiers (literal → `--listfile` → fuzzy same-basename,
    `export_texture_resolution.cpp`) — dropping one is a real, previously-
    hit bug class, not a safe simplification.
  - `export_texture_resolution.hpp`/`.cpp` owns every texture-candidate-
    resolution helper in the real (legacy) C++ pipeline — check there
    first, not `export_materials.cpp`, for texture-resolution doc/comment
    citations.
  - DB2 output built before cbd6e38 (2026-10-11) is stale wherever a
    column is narrower than 32 bits and packed (pallet/common data): husk
    kept the cell's junk high bits (`LiquidObject.LiquidTypeID` 5 read as
    0x3F000005). That covers `knowledge.sqlite` and any older `db2-export`
    database; rebuild before trusting them.
  - Any DB2 code that checks `section.header.tactKeyHash != 0` directly to
    decide whether a section is readable is checking the wrong thing —
    use `db2::Section::recordsAvailable()` (`db2.hpp`) instead. A nonzero
    `tactKeyHash` doesn't mean husk's bytes are still ciphertext; a real
    CASC extraction usually already decrypts these before the file reaches
    disk. husk has no TACT key store and deliberately never will.
  - `/media/luna/work/cache/husk/knowledge.sqlite` (`husk db2-build`'s
    output) contains real but incomplete object-skin data — same-slot
    cross-item collisions are common. Don't pass `--knowledge-db` to `husk
    export` for real output; see `STYLE_CLEANUP.md` item 2 and
    `TODO/TEXTURE_POOL_RECALL_TODO.md` for the disabled subsystem's own
    status and the disambiguation design that would need to land first.
  - `tools/full_render.py`'s `.renderignore` is the real render-exclusion
    mechanism; older scan-result-subtraction file lists are superseded.
  - The full-corpus render step is human-gated by policy — never run it
    without Luna's explicit go-ahead, never foreground/blocking.
  - `/media/luna/data/wow_export/buildinfo.json` (`clientBuild: 61621`, an
    11.x build) is stale: the corpus is game version 12.1.0 (Midnight),
    proven by its DB2 layout hashes against WoWDBDefs. Date the corpus by
    DB2 layouts, never by that file.
