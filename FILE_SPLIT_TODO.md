# TODO: split oversized files into cohesive modules

**Trigger**: a `tree_file_lengths` pass found 11 files over the project's own
1000-line hard limit (`src/cmd_dump.cpp`, `src/cmd_export.cpp`, `src/gltf.cpp`,
`src/m2.cpp`, `src/m2.hpp`, `tests/test_cli.cpp`, `tests/test_dump.cpp`,
`tests/test_gltf.cpp`, `tests/test_integration.cpp`, `tests/test_m2.cpp`, plus
`CLAUDE.md`/`DESIGN.md`/`WIKI_FINDINGS.md` — the last three are docs, treated
separately below, not part of this TODO's scope). Soft target: 750–800 lines,
the point being an editor shouldn't need more than one grep round-trip to
orient inside a file. Hard limit: 1000 lines, no exceptions found so far —
every file surveyed had real, mechanically-separable seams (see the
per-file plans below); if a future file genuinely can't split, that needs an
explicit, written reason in this file, not a silent skip.

**Ground rule for every split below**: split by *behavior*, not by lines.
Each new file should describe one cohesive thing (data model, one pipeline
stage, one CLI-flag family). The original file becomes the index/orchestrator
— it declares the module boundary and calls into the split-out pieces in
order, but doesn't reimplement any of them. No new abstraction layers, no
renamed public API, no behavior change — pure move, verified by a full
rebuild + `./build/husk-tests`/`ctest` green after every single file, not
after the whole batch.

**Working method**: one file at a time, its own commit, full test run before
moving to the next. Do the `src/` splits first — the corresponding `tests/`
splits should mirror whatever grouping falls out of the real `src/` module
boundaries, not be guessed independently ahead of time (per Luna's own
"tests first" instinct elsewhere in this project, this is the exception:
here the src shape has to exist before the test shape can follow it
correctly).

---

## Priority order

1. `src/cmd_export.cpp` — worst offender (2570 lines), most tangled, highest
   value once split.
2. `src/m2.hpp` + `src/m2.cpp` — do together, same struct-by-struct grouping
   applies to both; second-worst by raw size.
3. `src/gltf.hpp` + `src/gltf.cpp` — smaller, but `writeGlbMulti` (880 lines,
   one function) needs internal phase-extraction before the file split even
   helps; do after `cmd_export.cpp` since the phase-function shape there is
   a useful template.
4. `src/cmd_dump.cpp` — lowest risk (mechanical, ~40 independent
   `dumpXxx` functions with no shared state), do whenever convenient.
5. `tests/*` — one test file per corresponding `src/` split, done after
   items 1–4 land so the grouping mirrors the real module boundaries.

---

## Item 1: `src/cmd_export.cpp` (2570 lines)

Six topics, each already a near-contiguous function cluster:

- `src/export_transform.hpp/.cpp` — `toGltf`/`toGltfScale`/`isFinite`/
  `repairDuplicateTimestampsAndValidate` (~130 lines). Pure coordinate/
  validation helpers, no state.
- `src/export_skeleton.cpp` — `checkNoBoneCycles`, `buildSkeleton`,
  `buildSkinning` (~150 lines).
- `src/export_animation.cpp` — `resolveAliasChain`, `buildSequenceMetadata`,
  `M2AnimInputs`, `findAnimFileByBasename`, `buildJointAnimation`,
  `buildGlobalSequenceAnimations`, `buildAnimations` (~350 lines).
- `src/export_materials.cpp` — `alphaModeForBlend`, `BuiltMaterials`,
  `M2MaterialInputs`, `decodeFixed16`, `resolveAnimatedColorCurve`,
  `resolveAnimatedFixed16Curve`, `FuzzyTexturePool` + `scanDirOrWarn`/
  `scanFuzzyTexturePool`/`claimSoleFuzzyTextureCandidate`,
  `buildMaterialsAndPrimitives` (~700 lines — the single biggest cluster,
  bigger than most whole files today; clearly earns standalone status).
- `src/export_skin_resolution.cpp` — `requireSkinFileDataIds`,
  `resolveAutoSkinPaths`, `findSameBasenameSkins`, `resolveSkin` (~220
  lines).
- `src/cmd_export.cpp` keeps `addExportOptions` (CLI flag registration) and
  `exportGlb` (orchestration) — becomes the index.

**Blocker to resolve before the file split pays off**: `exportGlb` itself is
~800 lines of sequential wiring (skin resolution → skeleton → bones-dir/
`.bone` corrections → phys → ribbon/particle anchors → per-LOD mesh build →
glTF write). It's one real pipeline (not artificially tangled — CLAUDE.md's
"if highly interconnected, leave it" applies to the *coupling*, not the
line count), so it shouldn't be broken into files, but it does need internal
extraction into named phase-functions (`attachBoneCorrections`,
`attachPhysicsBodies`, `attachEmitterAnchors`, `buildLodTierMeshes`, …) so a
reader can see the pipeline's shape from function signatures instead of
scrolling 800 lines. Do this extraction as part of this item, before/while
moving the five split-out files above.

---

## Item 2: `src/m2.hpp` (1175 lines) + `src/m2.cpp` (2150 lines)

`m2.hpp` is ~28 independent structs with doc comments, no logic — splits
cleanly by concern. `m2.cpp`'s function bodies group along the identical
axes, so do both together:

- `m2_primitives.hpp` — `Array`, `Vec3`, `Vec2`, `Quat`, `BoundingBox`,
  `ParseError`. `m2_primitives.cpp` — the matching `read*` blob helpers
  (`readU32`/`readU16`/`readU8`/`readF32`/`readArray`/`readVec3`/
  `readBoundingBox`/`readName`) plus `parseBlob`/`resolveBlob`/
  `parseHeader`/`extractBlob`.
- `m2_header.hpp/.cpp` — `Header`, `Texture`, `Material`, `GlobalFlag`/
  `BoneFlag` namespaces, `parseTextures`/`parseMaterials`/
  `globalFlagNames`/`billboardModeName`/`keyBoneName`/`textureTypeName`.
- `m2_skeleton.hpp/.cpp` — `Bone`, `Vertex`, `CollisionMesh`,
  `parseVertices`/`parseBones`/`parseVec3Array`/`parseCollisionMesh`.
- `m2_animation.hpp/.cpp` — `Sequence`, `Range`, `Color`, `TextureWeight`,
  `TextureTransform`, `TrackMeta`, `FBlockMeta` — the biggest cluster
  (~850 lines in the `.cpp`): all the track/curve resolvers
  (`trackHasAnimatedData`, `readFixed16TrackValue`, `readVec3TrackValue`,
  `readQuatFloatTrackValue`, `readCompQuat`, `readTrackMeta`,
  `resolveFloatTrackSequence`/`Global...`, `resolveRawIntTrackSequence`/
  `Global...`, `resolveFBlockVec3`/`Vec2`/`Fixed16`/`Uint16`,
  `parseColors`/`parseTextureWeights`/`parseSequences`/`parseGlobalLoops`/
  `extractAnimBlob`).
- `m2_scene.hpp/.cpp` — `Attachment`, `Event`, `Light`, `Ribbon`,
  `ParticleEmitter`, `ExtendedParticle`, `parseAttachments`/`parseEvents`/
  `parseLights`/`parseRibbons`/`parseParticles`.
- `m2.hpp` keeps only `#include`s of the five above (re-exporting the
  `husk::m2` namespace, so no caller-visible change) plus anything that
  doesn't fit a single group (`loadFile`, `expansionForVersion` — check at
  split time whether these belong in `m2_primitives` instead). `m2.cpp`
  mirrors it — becomes the index, likely near-empty once the split lands.

---

## Item 3: `src/gltf.hpp` (652 lines) + `src/gltf.cpp` (1081 lines)

- `gltf_math.hpp/.cpp` — `Vec3`/`Vec2`/`Quat`, `Mat3` + the
  determinant/multiply/transpose/quatToMat3/mat3ToQuat core, and
  `zUpToYUp`/`rotationZUpToYUp`/`scaleZUpToYUp`. Small (~150 lines each),
  self-contained, zero dependency on the rest of the module — safe first
  cut.
- `gltf_mesh.hpp/.cpp` — `Material`, `Primitive`, `Mesh`, `NamedMesh`,
  `alphaModeString`, `padTo4`.
- `gltf_skeleton.hpp/.cpp` — `Skeleton`, `JointAnimation`, `Animation`.
- `gltf.hpp`/`gltf.cpp` keep `Error` and `writeGlbMulti` — the document
  assembler, becomes the index.

**Same blocker shape as item 1**: `writeGlbMulti` is one 880-line function
building buffers/accessors/nodes/materials/skins/animations/scene in
sequence. Extract into named phase-functions first (`emitMaterials`,
`emitMeshesAndPrimitives`, `emitSkin`, `emitAnimations`, `emitScene`, …),
matching whatever shape item 1's `exportGlb` extraction settles on — then
those phase-functions are natural candidates to live in
`gltf_mesh.cpp`/`gltf_skeleton.cpp` alongside their matching data types,
with `gltf.cpp` left as the thin caller.

---

## Item 4: `src/cmd_dump.cpp` (1594 lines)

Lowest risk — ~40 independent `dumpXxx(Writer&, Chunk&)` functions, no
shared state between them beyond small writer helpers.

- `dump_writer_utils.hpp/.cpp` — `readU32`/`readU16`/`readF32`/`hexDump`/
  `ChunkArray`/`readChunkArray`/`writeVec3`/`writeFixed16AsFloat`/
  `writeRawIntAsIs`/`readHalfFloat`/`physPayloadRealLength`.
- `dump_emitters.cpp` — `dumpEmitters` + its ribbon/particle write helpers
  (~250 lines, particles/ribbons only).
- `dump_phys.cpp` — the 8 `writePhys*` functions (`writePhysVec3`/
  `writeMat3x4`/`writePhysShape`/`writePhysWeldJoint`/
  `writePhysSphericalJoint`/`writePhysShoulderJoint`/
  `writePhysPrismaticJoint`/`writePhysRevoluteJoint`/
  `writePhysDistanceJoint`/`writePhysJoint`/`writePhysFile`, ~350 lines,
  `.phys` sidecar only).
- `dump_chunks_misc.cpp` — the remaining ~30 small per-tag dumpers
  (`dumpTxac`/`dumpExpt`/`dumpU16ArrayChunk`/`dumpPadc`/`dumpPsbc`/
  `dumpPedc`/`dumpFileDataIdArrayChunk`/`dumpWfv1`/`dumpWfv2`/`dumpWfv3`/
  `dumpNerf`/`dumpEdgf`/`dumpDboc`/`dumpTexl`/`dumpAfra`/`dumpDpiv`/
  `dumpDetl`/`dumpPfdc`/`dumpExp2`/`dumpPcol`).
- `cmd_dump.cpp` keeps `dumpChunks` (CLI entry + tag dispatch) — becomes
  the index.

---

## Item 5: tests

Mirror whichever split each corresponding `src/` file gets, once items 1–4
are done — don't guess the grouping ahead of the real module boundaries.
Rough expectation, to be confirmed at split time:

- `tests/test_m2.cpp` (129 cases) → `test_m2_primitives.cpp`/
  `test_m2_header.cpp`/`test_m2_skeleton.cpp`/`test_m2_animation.cpp`/
  `test_m2_scene.cpp`, matching item 2.
- `tests/test_gltf.cpp` (88 cases) → `test_gltf_math.cpp`/
  `test_gltf_mesh.cpp`/`test_gltf_skeleton.cpp`, matching item 3.
- `tests/test_dump.cpp` (35 cases) → mirrors item 4's grouping.
- `tests/test_cli.cpp` (142 cases) — doesn't mirror a `src/` split 1:1
  (it's CLI-flag-shaped, not module-shaped); split by flag family instead
  (`--skin`/`--skin-dir`/`--lod`, `--anim`, `--skel`/`--bones-dir`,
  `--phys`, general/error-path cases).
- `tests/test_integration.cpp` (29 cases) — small enough (1138 lines) that
  it may not need splitting at all once the others shrink; re-measure
  after items 1–4 land before deciding.

**Post-completion audit (follow-up)**: after Item 5's own splits landed, a
separate audit found four files still over the 1000-line hard limit —
`tests/test_cli.cpp` (1575), `tests/test_cli_fixtures.hpp` (1137),
`tests/test_m2_animation.cpp` (1038), `tests/test_cli_anim.cpp` (1022) — the
flag-family/module split above was real progress but not enough on its own
for these four. Each got one further real, content-based split (not an
arbitrary line-count cut):

- `tests/test_cli.cpp` → itself (general `husk export` default-resolution/
  flag behavior) + `tests/test_cli_info.cpp` (`husk info` output) +
  `tests/test_cli_errors.cpp` (corrupted/adversarial file-content "fails
  cleanly" cases) + `tests/test_cli_argv.cpp` (argv-grammar: named/
  positional flags, --help/--version, CLI11/argc-guard parse errors).
- `tests/test_cli_fixtures.hpp` → itself (cross-cutting byte-builder
  primitives, used by 3+ files) + `tests/test_cli_fixtures_scenes.hpp`
  (composite, scenario-specific fixtures, each used by only 1-2 files).
- `tests/test_m2_animation.cpp` → itself (outer struct-array parsers:
  parseColors/parseTextureWeights/parseTextureTransforms/parseSequences) +
  `tests/test_m2_animation_tracks.cpp` (per-track/per-curve keyframe
  resolvers, FBlock family, extractAnimBlob).
- `tests/test_cli_anim.cpp` → itself (inline-M2/pure-alias sequence
  resolution, no `.skel` involved) + `tests/test_cli_anim_skel.cpp`
  (`.skel`-sourced SKS1/AFSB external sequences, keyframe-data validation,
  `--anim` flag-value tests).

All four (plus their two new siblings each) verified under 1000 lines, full
`./build/husk-tests` green (490/490, 1 skipped, 3732 assertions) after each
individual file's split, one commit per file.

---

## Done: `CLAUDE.md` and `WIKI_FINDINGS.md` — current/history split, not a topic split

Luna gave explicit direction for these two, different from the code-file
treatment above: split each into a short, always-current "rules"/"current
fact" file plus a separate, append-only history file one step behind it —
so the file an AI opens for "what's true right now" never makes it read
through superseded narrative to find the still-current answer.

- **`CLAUDE.md`** (was 2260 lines) → `CLAUDE.md` (266 lines: Purpose/
  Status/Boundaries unchanged, Resume section trimmed to a condensed
  current-state summary + Next step + Hazards) + `CLAUDE_HISTORY.md` (2031
  lines: every "Last state"/"Previous state" entry, moved verbatim,
  append-at-top convention). `CLAUDE.md`'s Resume section is now a
  snapshot, updated in place each session — the full narrative goes to
  `CLAUDE_HISTORY.md` instead of accumulating in `CLAUDE.md` itself.
- **`WIKI_FINDINGS.md`** (was 1722 lines) → split two ways at once:
  - **Per-page current-fact files**, mirroring
    `documentation/wowdev-wiki/md/`'s own layout so it's obvious which file
    answers which wiki page: `WIKI_FINDINGS/M2.md`, `WIKI_FINDINGS/M2/anim.md`,
    `WIKI_FINDINGS/M2/skel.md`, `WIKI_FINDINGS/M2/skin.md`,
    `WIKI_FINDINGS/BONE.md`, `WIKI_FINDINGS/PHYS.md`, and
    `WIKI_FINDINGS/WORLD.md` (the WMO/ADT/WDT/WDL/PM4/PD4 investigation
    pass, kept as one file since it's genuinely a single planning-stage,
    not-yet-implemented pass — split further once real parsers land, the
    same way the other six are already split).
  - **`WIKI_FINDINGS_HISTORY.md`**: the full original evidence trail
    (every "current text"/"proposed addition"/"evidence" write-up),
    section numbers 1–15 unchanged, moved verbatim.
  - **`WIKI_FINDINGS.md` itself** is now a short index: a table pointing at
    each per-page file plus the "Where these live in husk" code/test
    cross-reference table.
  - **~86 existing `WIKI_FINDINGS.md §N` citations** across `src/`/
    `tests/`/`tools/`/`README.md`/`DESIGN.md`/`M2_COMPLETENESS.md`/
    `TODO_correctness.md`/`documentation/wowdev-wiki/HUSK_AMENDMENTS.md`
    were mechanically repointed to the matching new topic file (the §N→file
    mapping is 1:1 and deterministic, so this was a scripted, verified
    substitution, not hand-editing each site). Full test suite re-verified
    green afterward (490/490, `./build/husk-tests`) since the sweep touched
    live `src/`/`tests/` comment text.
  - **Deliberately left untouched**: citations inside `CLAUDE_HISTORY.md`
    (historical log, never rewritten, same convention as everywhere else in
    this project) and inside the WMO/ADT/world-expansion `*_TODO.md` files
    Luna is working on in a concurrent, separate thread (`WORLD_COMPLETENESS.md`,
    `LIQUID_TODO.md`, `COLLISION_CULLING_TODO.md`, `ENGINE_TODO.md`,
    `BLENDER_EXPORT_TODO.md`, `TRANSFORM_TRIAGE.md`,
    `WORLD_MISC_METADATA_TODO.md`, `TOOL_COMPARISON.md`,
    `INLINE_COMMENT_RULES_VIOLATIONS.md`) — her own files, not this
    session's to edit, per this project's established "Luna-created
    content, not mine to touch" precedent. Their `§N` citations still
    resolve fine (the same section numbers still exist in
    `WIKI_FINDINGS_HISTORY.md`), just without the extra precision the
    repointed citations elsewhere now have.

**Going forward**: when a new wowdev.wiki finding is added, write the full
evidence write-up as a new numbered section appended to
`WIKI_FINDINGS_HISTORY.md`, then update (don't append to) the matching
topic file under `WIKI_FINDINGS/` with the distilled current fact. Same
pattern for `CLAUDE.md`/`CLAUDE_HISTORY.md` each session.

## Explicitly out of scope

- `DESIGN.md` (1499 lines, under the 1000-line trigger's own file list but
  worth noting since it's close) — not touched by this pass; re-measure
  once the code splits above land and DESIGN.md's Key design decisions
  section grows further. Same current/history split could apply here too
  if it grows past the limit — not decided yet, ask before assuming.

## Verified so far

The doc split above (`CLAUDE.md`/`CLAUDE_HISTORY.md`,
`WIKI_FINDINGS.md`/`WIKI_FINDINGS/*`/`WIKI_FINDINGS_HISTORY.md`) is
implemented, and the resulting `src/`/`tests/` comment changes were
rebuilt + re-tested green (490/490). The `src/` code-split items (1–4)
below are still plan-only, not implemented — grounded in an actual read of
each file's function/struct list (`grep -n` over top-level declarations),
not guessed from file size alone.
