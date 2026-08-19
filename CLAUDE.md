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
  `export`"), with generated bash/zsh completions in `completions/`. See
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

- **Current state (2026-08-20)**: Fixed the `db2::resolveFieldString`
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
  green, 641/641. `chrcustomizationchoice.db2`'s field 0 still shows
  occasional garbage on rows whose real value is a small integer, not a
  string (`resolveFieldString`'s permissive high-byte/UTF-8-continuation
  heuristic accepting a non-string as printable) — a real, pre-existing,
  separately-scoped heuristic false-positive, not this bug; not fixed
  this session, not yet filed as its own TODO item.
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
- **Next step**: the `resolveFieldString` bug above is now fixed —
  `TODO/TODO_correctness.md` #4 closed and removed. The
  `chrcustomizationchoice.db2` heuristic false-positive noted above
  (small integers occasionally decoding as garbage strings) is a real,
  independent, small follow-up if anyone wants it — not yet filed. The
  manual visual pass over the 22 successfully-exported HD character
  `.glb`s (deriving sane per-race/gender geoset defaults, hunting further
  player-character bugs) is still Luna's own next action, not queued husk
  work — if it turns up bugs, they'll be new entries here. Otherwise
  unchanged from before: `MULTI_TEXTURE_LAYER_TODO.md`'s step 5,
  `RENDER_QUALITY_TODO.md`'s ambiguous-pool tiebreak/blank-render
  follow-ups, the dangling-internal-reference corpus scan
  (`CLEANUP_TODO.md` #2), `CLEANUP_TODO.md` #1's comment-hygiene sweep,
  and `BONE_NAME_DEDUCTION_TODO.md`'s Tier 2 (still needs a design pass).

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
