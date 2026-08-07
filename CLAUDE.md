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
  every export also attaching minimal ribbon/particle placement anchors
  (id/bone/position, `ribbon_emitters`/`particle_emitters` skin `extras`,
  unconditional), `husk dump-chunks` (JSON dump of Legion+ chunks with no
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
  (WMO/M3, not started, by design) or the structural gaps `TODO_correctness.md`
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
  in flight. A real interactive Blender pass this session found the
  M2→glTF position/rotation/scale conversion was measurably upside down
  despite the whole conformance suite above passing -- root-caused,
  fixed, and covered by a new asset-agnostic orientation-correctness test
  tier (`TRANSFORM_TRIAGE.md`, see Resume); the one piece still open by
  design is a real animated clip visually confirmed in Blender's own GUI,
  deliberately left for Luna rather than automated.
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
  FileDataID-named, local filesystem only. **Never CASC** — husk has no
  CASC/listfile access and never will, by design (see `DESIGN.md`'s Non-goals).
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

- **Current state**: Resolved the previous entry's own open question with
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
  `export_materials.cpp`'s new `pngDimensions`, emitted as
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
- **Next step**: `CHAR_TEXTURE_COMPOSITING_TODO.md` (new this session) --
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
  `TODO_correctness.md` (`M2Camera`, `.bone` slot *selection* — both
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
- **Hazards**: the Attachment/Event/Light glTF nodes added this session
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
