# CLAUDE.md — husk

Global rules apply (`~/.claude/CLAUDE.md`). Nix conventions: `.claude/rules/nix.md`.
Read `DESIGN.md` before any structural change.

## Purpose

CLI that reads WoW M2 model files (+ `.skin`/`.skel`/`.bone`/`.anim` sidecars) and
exports them to glTF 2.0 (`.glb`) for Blender import; `husk-blp` (separate Python
tool, `blp/`) converts BLP2 textures to PNG.

## Status

- **Current**: `husk info` (header/record-count/chunk-tag summary, incl. per-texture/
  material detail and sidecar FileDataIDs), `husk export` (static mesh → skeleton +
  skinning, inline or external `.skel` → materials with real embedded textures →
  animation, inline/external-`.anim`/`.skel`-sourced, verified against real
  `bloodelffemale.m2`/`bloodelffemale_hd.m2` data), `husk export --lod`
  (single-tier or `all`), `husk export --bones-dir` (real `.bone` correction
  data attached as inert `bone_correction_sets` glTF skin `extras`, never
  applied to the render — see Resume), `husk dump-chunks` (JSON dump of chunks
  with no glTF equivalent, or `.bone` files directly). `blp/`'s `husk-blp` (BLP2 → PNG:
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
  never had before — see Resume. Remaining work is either scope expansion
  (WMO/M3, not started, by design) or the structural gaps `TODO_correctness.md`
  already tracks (`M2Particle`, plus `.bone` correction *selection* — the
  extras-export half is done, see Resume; picking which slot applies is blocked
  on client-side DB2 data husk doesn't have, not on more investigation) —
  nothing currently in flight.
- Anything not listed under Current does not exist yet. In particular: `M2Particle`/
  `M2Camera` are still count-only (not dereferenced). Three FAILURES2.md gaps
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

- **Last state**: `VERIFICATION_IDEAS.md`'s survey (source-M2-counts vs.
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
- **Next step**: nothing in flight. `AFSB` is fully resolved — no further
  work needed there barring a real multi-model cross-check if a
  non-blood-elf `.skel`-linked character file ever shows up in
  `test_data/` (the packing/alignment rule was verified across one
  model's entire real corpus, not multiple distinct models; worth
  reconfirming if the opportunity arises, not urgent since the
  underlying mechanism — reusing already-verified `SKB1` descriptor
  resolution — has no model-specific assumptions baked in). Remaining
  known gaps are exactly what `TODO_correctness.md` tracks post-renumbering
  (item 1: `M2Particle` dereferencing; item 5: `.bone` slot *selection* —
  the extras-export half is done, picking a slot is blocked on client-side
  DB2 data husk doesn't have and, per `DESIGN.md`'s non-goals, never will
  at runtime; two awareness-only footnotes), plus optional scope expansion
  (WMO/M3, or Blender-side tooling for the geoset/multi-texture-layer/
  bone-correction `extras`). The M2-vs-glb-vs-Blender verification cases
  (former `VERIFICATION_IDEAS.md`, see Last state) are now all resolved —
  cases 1/2/3/5 implemented, case 4 deliberately left as-is; nothing
  pending there, the file itself is gone. One real,
  minor loose end from an earlier session worth picking up if
  `cmd_export.cpp` is touched again: `resolveSkin`'s failure messages
  could name the specific candidate path/FileDataID they tried, not just
  the directory — small, not urgent.
- **Hazards**: none new from this session's own changes beyond what's
  already folded into the code/docs (the `writeGlbMulti` skinning
  relaxation and `blender_import_check.py` fixes are both covered by real
  tests, not just asserted safe). One thing worth knowing if
  `cmd_export.cpp`'s collision-mesh block is touched again: it always
  appends its `NamedMesh` *after* every render/LOD entry — anything
  indexing `namedMeshes` by position (like the "N LOD tier(s)" summary
  print, already fixed this session via `renderMeshCount`) needs to
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
