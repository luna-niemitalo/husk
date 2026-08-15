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

- **Current state**: New session, three independent TODO items implemented
  (ranked by impact first, per direct request). One new corpus-scan task,
  `tools/corpus_scan_tasks/shader_names_task.py` (ports `husk::m2::
  resolveShaderNames` into Python), answered both `MULTI_TEXTURE_LAYER_
  TODO.md` step 0 and `PIXEL_SHADER_FORMULAS_TODO.md` step 1 in one run
  against the full local corpus (287,005 `.skin` files): env-map
  (`_Env`-bearing vertex shader) frequency is **41.33%** of real batches
  (was assumed rare at "3 files"), and **14/17** wowdev.wiki-undocumented
  `Combiners_*` pixel shaders have real corpus repros (some with 15,000+
  files) — both TODO files and `TODO/README.md` updated with the real
  numbers, no husk source touched. Third item, `RENDER_QUALITY_TODO.md`'s
  Mod/Mod2x blend modes: confirmed no material-shader-graph trick exists
  (queried Blender 5.1.1 directly — no framebuffer-read node in either
  engine), then confirmed the Compositor can do it via Cryptomatte's
  per-material matte combined with ordinary `Add`/`Multiply` compositor
  math on a single render — verified interactively in Blender by Luna
  directly. A real quoting bug in `Cryptomatte.matte_id` found and fixed
  along the way (wants plain comma-separated material names, no quotes).
  One real EEVEE Next Cryptomatte bug found, root-caused, and fixed: the
  `Matte` output is pure black whenever `surface_render_method ==
  'BLENDED'` — and setting `blend_method = 'BLEND'` (what every real
  Mod/Mod2x material needs) silently flips that from Blender's own
  default (`DITHERED`) as an undocumented side effect, so it looked
  unconditionally broken until Luna's own interactive test (a working
  node graph, `Render Method: Dithered` visible in her screenshot)
  contradicted this session's own scripted tests and pointed at the real
  cause. Fix: re-assert `surface_render_method = 'DITHERED'` after
  setting `blend_method` — verified this makes EEVEE match Cycles exactly.
  No engine restriction needed for this feature after all.
  Implementation (not yet built) tracked in new
  `TODO/MOD_BLEND_COMPOSITING_TODO.md`, including where the code goes
  (`tools/husk_blender_geoset_mask.py`, called from `render_glb.py`'s
  `main()` the same way every other shared post-import fixup already is —
  no duplicate logic). Full investigation narrative (including two dead
  ends corrected along the way): `CLAUDE_HISTORY.md`'s top entry (prior
  session's own staging-commits + doc-discipline-pass entry is the one
  right below it there).
- **Next step**: No hard blocker. Independent, well-scoped work still
  open: `MULTI_TEXTURE_LAYER_TODO.md` step 4/5 (Blender-side node recipes,
  now backed by real per-file repro data for both env-mapping and several
  of the 17 previously-undocumented pixel shaders), `MOD_BLEND_
  COMPOSITING_TODO.md`'s implementation (node-graph design + verification,
  the EEVEE Cryptomatte bug), `RENDER_QUALITY_TODO.md`'s ambiguous-pool
  tiebreak/blank-render follow-ups, the dangling-internal-reference
  corpus scan (`CLEANUP_TODO.md` #2, not yet designed as a `ScanTask`), or
  the rest of `CLEANUP_TODO.md` #1's comment-hygiene sweep (only 7 files
  audited so far, out of all of `src/`). Two items explicitly need a
  design pass before touching code, not more investigation:
  `BONE_NAME_DEDUCTION_TODO.md`'s Tier 2 (fuzzy reference-skeleton
  matching, an ambiguity policy, a new Rigify dependency) and
  `PIXEL_SHADER_FORMULAS_TODO.md` step 2 (verifying the wow.export-derived
  formulas against real rendered output — now has 14 real repro files to
  pick from, still needs Luna's own visual comparison) — flag these to
  Luna rather than guessing at the shape.
- **Hazards**: `export_texture_resolution.hpp`/`.cpp` (new, split out of
  `export_materials.cpp` this session) now owns every texture-candidate-
  resolution helper (`readTextureFileBytes`/`resolveTextureBytes`/
  `alphaModeForBlend`/the three `resolveAnimated*Curve` functions/the
  `FuzzyTexturePool` machinery/`materialDedupKey`) — `export_materials.cpp`
  itself is down to `scanDirOrWarn` + `buildMaterialsAndPrimitives` only.
  If a doc/comment ever cites one of the moved functions as living in
  `export_materials.cpp`, that's stale; check
  `src/export_texture_resolution.hpp` first. `tools/husk_blender_geoset_
  mask.py`'s new `_debug_marker_collection()` (a "Husk Debug Markers"
  collection, `hide_render`/`hide_viewport` both set) is the established
  pattern for any future debug-only Blender object this script creates —
  reuse it rather than linking into the active collection directly, the
  mistake the first version of `apply_emitter_markers` made before a
  direct follow-up request caught it. A real, external, not-husk's-bug
  incompatibility exists between husk's own spec-correct zero-image glTF
  output (omits the `"images"` key entirely, the *only* glTF-2.0-valid
  shape when there are none) and at least one self-built Blender dev
  branch's `io_scene_gltf2` importer (`blender_gltf.py`'s `len(gltf.data.
  images)` has no `None`-guard) — the project's own pinned flake Blender
  (5.1.1) doesn't have this bug; if it resurfaces, it's that specific
  Blender build, not a regression in husk's output (confirmed via
  `gltf_validator`, which rejects the only alternative shape as invalid).
