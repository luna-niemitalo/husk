# CLAUDE.md — husk

Global rules apply (`~/.claude/CLAUDE.md`). Nix conventions: `.claude/rules/nix.md`.
Read `DESIGN.md` before any structural change.

## Purpose

CLI that reads WoW M2 model files (+ `.skin`/`.skel`/`.bone`/`.anim` sidecars) and
exports them to glTF 2.0 (`.glb`) for Blender import; `husk-blp` (separate Python
tool, `blp/`) converts BLP2 textures to PNG.

## Status

- **Current**: `husk info` (header/record-count/chunk-tag summary, incl. per-texture/
  material detail and sidecar FileDataIDs, plus a one-line ribbon/particle-emitter
  summary), `husk export` (static mesh → skeleton +
  skinning, inline or external `.skel` → materials with real embedded textures →
  animation, inline/external-`.anim`/`.skel`-sourced, verified against real
  `bloodelffemale.m2`/`bloodelffemale_hd.m2` data), `husk export --lod`
  (single-tier or `all`), `husk export --bones-dir` (real `.bone` correction
  data attached as inert `bone_correction_sets` glTF skin `extras`, never
  applied to the render — see Resume), every export also attaching minimal
  ribbon/particle placement anchors (id/bone/position, `ribbon_emitters`/
  `particle_emitters` skin `extras`, unconditional — see Resume), `husk
  dump-chunks` (JSON dump of Legion+ chunks with no glTF equivalent, *and* —
  broadened this session — full `M2Ribbon`/`M2Particle` records including
  every resolved animation curve, present in every M2 version; or `.bone`
  files directly). `blp/`'s `husk-blp` (BLP2 → PNG:
  palettized/DXT1/DXT5/BGRA). `husk export`'s CLI grammar is CLI11-based named
  flags (`--input`/`--output` positional-fallback, everything else named,
  `--skin`/`--textures`/`--skin-dir`/`--anim`/`--skel`/`--bones-dir`
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
  more than one root bone, see Resume) -- nothing currently in flight.
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

- **Last state**: Closed out `MULTIROOT_SKELETON_TODO.md` the same way
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
    `PHYS_TODO.md`, alongside `TODO_correctness.md`/`WIKI_FINDINGS.md`/
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
    `MULTIROOT_SKELETON_TODO.md` alongside `TODO_correctness.md`/
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
    `TODO_correctness.md` (former item 1, particles, removed outright per
    this file's own "fixed items get removed" convention — not marked
    `[Fixed]` — remaining items renumbered 2-5 → 1-4, every
    `TODO_correctness.md #N` cross-reference across `src/`/`tests/`
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
    `TODO_correctness.md`/`DESIGN_CHANGES.md`, not additive `[IMPLEMENTED]`
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
- **Previous state**: `TODO_correctness.md`'s former #1 — `.skel`-sourced
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
    more detail); `TODO_correctness.md`'s former item 1 removed outright
    (per this file's own "fixed items get removed, not marked `[Fixed]`"
    convention) and items 2-6 renumbered to 1-5 — a deliberate exception to
    "don't renumber, it touches live code strings," done carefully with a
    full grep-verified sweep across every `TODO_correctness.md #N`
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
- **Previous state**: `TODO_correctness.md` #6's extras-export half is now
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
  `WIKI_FINDINGS.md` §4/`TODO_correctness.md` #6) — same "tag it, don't
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
    runtime never is), `TODO_correctness.md` #6 (extras-export marked
    done, remaining gap reframed as "external lookup, not more
    investigation"), `M2_COMPLETENESS.md` (`.bone` row + the sidecar
    FileDataID-resolution rows), `WIKI_FINDINGS.md` §4 (added the
    previously chat-only weapon-type/armor-type ruling-out finding — the
    corrected bones cluster on Head/Jaw, not hand/wrist — since
    `TODO_correctness.md` #6 now cites it as an established fact and it
    needs real receipts backing it, not just a claim).
- **Earlier state** (condensed — full detail in git history/`WIKI_FINDINGS.md`/
  `DESIGN.md`/`README.md`, which all already captured the durable facts):
  a `.bone`-slot-selection investigation ruled out the LOD/render-distance
  hypothesis by real data (20 `.bone` slots don't fit a 7-tier LOD count,
  collapse into only 5 distinct bone-index sets with heavy exact
  duplication) — the real selector is external client-side DB2 data husk
  has no access to, per `DESIGN.md`'s non-goals (`WIKI_FINDINGS.md` §4,
  `TODO_correctness.md` #5). Earlier still, `export`'s CLI grammar
  migrated from a positional parser to named CLI11 flags (a breaking
  change to every invocation's argument order, done in one deliberate
  pass) — CLI11 added as a new flake dependency with sign-off,
  `addExportOptions` became the one place the flag surface is declared
  (shared by real parsing and the `--print-completion` generator), and
  `--skin`/`--textures`/`--skin-dir`/`--anim`/`--skel` got the
  three/four-state (`auto`/explicit/`none`) treatment `DESIGN.md`'s CLI
  grammar section still documents in full.
- **Next step**: `PHYS_TODO.md`'s Implementation plan — `.phys` byte
  layout is fully verified (`WIKI_FINDINGS.md` §9), nothing left to
  investigate before writing `src/phys.hpp`/`phys.cpp`, except the one
  real open call this session deliberately left for a plan-mode pass
  rather than deciding unilaterally: the exact `extras`-vs-`dump-chunks`
  split and CLI flag shape (recommendation: the ribbon/particle hybrid,
  see `PHYS_TODO.md`'s Architecture recommendation section). The M2→glTF
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
  like (a) doesn't stay unverifiable next time. Also still open:
  `TODO_correctness.md`'s own tracked items (`M2Camera`, `.bone` slot
  *selection* — both low-priority by design, not oversight), optional
  scope expansion (WMO/M3, Blender-side tooling for the various `extras`
  this project already exports), and `resolveSkin`'s failure messages not
  naming the specific candidate path/FileDataID they tried.
- **Hazards**: for the multi-root rework (now implemented), never insert a
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
