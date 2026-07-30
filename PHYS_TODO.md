# PHYS_TODO — implementation plan for `.phys` physics/collision sidecar support

**Status: ready to implement.** Unlike the multi-root-bone-forest
investigation that preceded it (now resolved and folded into `DESIGN.md`'s
Key design decisions), this
isn't a pre-mortem full of open questions — a read-only investigation this
session (findings folded into `WIKI_FINDINGS.md` §9) decoded every
documented `.phys` chunk type cleanly, self-consistently, and with zero
cross-chunk index errors across 103 real files. The struct layouts are
known; what's below is a concrete build plan, a coverage map of what's
verified vs. not, and a real test plan — not a design survey.

**One thing this file *is* still deliberately leaving open**: whether
`.phys` data should attach to `.glb` exports as inert `extras` (the
`.bone`/geoset/texture-transform pattern) or live entirely in
`dump-chunks`'s JSON output (no glTF footprint at all), or split between
the two (the ribbon/particle pattern — see Architecture below for why
that's the recommendation, but it's a recommendation, not a decision Luna
has made yet). That question, plus the exact CLI flag shape, went through
a real plan-mode design pass for both `--bones-dir` and the particle/
ribbon work before implementation — do the same here before writing code,
per this project's own established practice for anything touching the
CLI-grammar/parsing-pipeline/glTF-schema surface at once.

---

## What's verified vs. not (drives implementation scope/priority)

From `WIKI_FINDINGS.md` §9, against 103 real files (7 committed in
`test_data/`, 96 from `phys_files_for_exploration.txt`):

| Chunk(s) | Real-file coverage | Confidence |
|---|---|---|
| `PHYS`, `PHYT` | 103/103 files | verified |
| `BODY`(0x1c)/`BDY2`(0x20, unverified)/`BDY3`(0x2c)/`BDY4`(0x30) | 98/103 (`BODY`×3, `BDY3`×1, `BDY4`×90 — `BDY2` never seen) | verified except `BDY2` |
| `SHAP`(0x14)/`SHP2`(0x20) | 98/103 (`SHAP`×3, `SHP2`×91) | verified |
| `CAPS` | 91/103, 264 of 366 shape records | verified, dominant shape type |
| `PLYT` (0x50 header stride, corrected) | 55/103 | verified, common |
| `SPHS` | 2/103 (`creature/wingederedar{,boss}/`) | verified, rare |
| `BOXS` (shapeType 0) | **0/103 — never observed** | unverified, offsets transcribed from wiki only |
| `JOIN` | 96/103 | verified |
| `WELJ`(0x68)/`WLJ2`(0x70) | `WELJ`×3, `WLJ2`×35 | verified |
| `WLJ3`(0x74) | **0/103** | unverified |
| `SHOJ` (0x74 stride, v2/post-7.0.1.20979) | 86/103 | verified |
| `SHOJ` 0x6c (pre-7.0.1.20979) / `SHJ2` | **0/103** | unverified |
| `REVJ`(0x70) | 59/103 | verified |
| `REV2`(0x78) | **0/103** | unverified |
| `SPHJ`, `PRSJ`, `PRS2`, `DSTJ` | **0/103** | unverified, offsets transcribed from wiki only |
| `PHYV` | 2/103 (`world/expansion06/doodads/valsharah/7vs_detail_nightmareplant0{1,2}_phys.phys`) | verified |

**Implication for implementation order**: everything in the "verified"
rows can be implemented and tested against real bytes today. Everything in
the "unverified" rows should still be *parsed* (the byte offsets are real,
just never independently confirmed against real data) but treated with the
same "verified floor, warn below it" posture husk already uses elsewhere
(`kMinVerifiedParticleVersion`, `kMinVerifiedRecordStrideVersion`) — accept
and expose the data, but don't claim confidence beyond "transcribed from
the wiki, structurally plausible" until a real example surfaces.

---

## Architecture recommendation

**No native glTF translation exists for physics/collision simulation
input** — same situation `.bone` corrections and particle/ribbon emitters
were in, different from the collision *mesh* (`M2Camera`'s collision
triangles), which had a real 1:1 glTF mesh-node translation. Two prior
patterns this project already chose between for that exact situation:

- **`.bone`'s pattern**: everything as inert `extras` on the glTF skin,
  gated behind an explicit `--bones-dir` flag (opt-in directory, `none` to
  suppress).
- **Ribbon/particle's pattern** (chosen after a real plan-mode pivot away
  from an all-`extras` first draft): a **minimal placement anchor**
  (id/joint/position) unconditionally in `.glb` `extras`, full field/curve
  data in `husk dump-chunks`'s JSON output instead — because the full data
  was "high-volume enough (potentially dozens of emitters, each with
  several animation curves) that it lives in dump-chunks instead of
  bloating every .glb" (`gltf.hpp`'s `EmitterAnchor` doc comment).

**`.phys` fits the ribbon/particle shape better than the `.bone` shape**:
`BODY`/`BDY3`/`BDY4` records already *are* essentially
`position + boneIndex` anchors (like `M2Ribbon`/`M2Particle`), not a flat
correction-matrix-per-bone table (like `.bone`). And the volume is real —
a single file (`creature/gallywix/gallywix.phys`) has 44 bodies, 44+
shapes, and dozens of joints; attaching the *full* record set (shapes,
joint frames, motor parameters) to every `.glb`'s `extras` would be a much
bigger payload increase than `bone_correction_sets` ever was.
**Recommendation**: follow the hybrid — a minimal per-body placement
anchor (id/joint/position/bodyType) unconditional in `.glb` `extras`
(`physics_bodies`, matching `ribbon_emitters`/`particle_emitters`'s
naming), full body/shape/joint/`PHYV` records in `dump-chunks`'s JSON
output, and `dump-chunks` gains the ability to take a `.phys` file
directly (matching the existing `.bone`-file-direct precedent) since
`.phys`, like `.bone`, is a standalone sidecar file, not just an M2-header
array.

Whether `PFID` resolution should be a three-state flag mirroring `--skel`
(`auto`/explicit path/`none`, since `PFID` — like `SKID` — is a single
scalar FileDataID, not an array like `BFID`/`AFID`/`SFID`) is the natural
default given the precedent, but confirm this in the plan-mode pass rather
than assuming — `--bones-dir` deliberately chose a *directory* flag
instead because `BFID` is an array of several files, which doesn't apply
here.

---

## Data model plan (`src/phys.hpp`/`phys.cpp`, new files)

Mirror `src/bone.hpp`'s shape (`ParseError` convention, bounds-checked raw
field readers, `readChunks`/`findChunk` reuse) — **not** `skel.hpp`'s more
elaborate multi-array-header shape, since `.phys`'s chunk-per-record-type
layout is structurally closer to `.bone`'s BIDA/BOMT pair, just with many
more chunk types.

**Chunk-tag handling — simpler than the investigation's own scratch tool
assumed**: `husk::readChunks`/`findChunk` (`src/chunk.hpp`) don't reverse
tags themselves, they just split `(tag, size, payload)` records literally
— the M2-vs-WMO/ADT reversal is purely about which ASCII string a caller
compares against. `.phys` can reuse `readChunks`/`findChunk` **directly**,
the same functions `bone.cpp`/`skel.cpp` already use, as long as every
`findChunk` call passes the *reversed* literal (`findChunk(chunks,
"SYHP")` for `PHYS`, `"TYHP"` for `PHYT`, `"SPAC"` for `CAPS`, etc.) —
no new chunk-walking function needed, just reversed string literals (or a
small `constexpr` helper that reverses a 4-char literal at compile time,
for readability — either is fine, pick in review).

Suggested structs (names illustrative, adjust in review):

```cpp
struct Body {
    uint16_t type = 0;       // 0 = kinematic (commonly >1 per file, WIKI_FINDINGS.md §9 — do NOT assume "the root"), 1 = dm_dynamicBody, other = ?
    uint16_t boneIndex = 0;
    Vec3 position;
    int32_t shapeBase = 0;   // BODY/BDY2's shapes_base, or BDY3/4's shapeIndex
    int32_t shapeCount = 0;
    // version>=3 (BDY3/4) fields, default-valued when absent (BODY/BDY2):
    float unk0 = 1.0f, x1c = 1.0f, drag = 0.0f, unk1 = 0.0f, x28 = 0.9f;
};

struct Shape {
    int16_t type = 0;   // 0=box, 1=capsule, 2=sphere, 3=polytope (v3+)
    int16_t index = 0;  // index into the type-specific chunk
    float friction = 0, restitution = 0, density = 0;
};

struct BoxShape { std::array<float,12> a; Vec3 c; };            // unverified -- §above
struct CapsuleShape { Vec3 localPosition1, localPosition2; float radius; };
struct SphereShape { Vec3 localPosition; float radius; };
struct PolytopeShape {
    uint32_t vertexCount = 0, nodeCount = 0;
    std::vector<Vec3> vertices;
    // unk_1[count_10]/unk_2[count_10]/nodes[nodeCount] -- surface raw, semantics
    // not confirmed (WIKI_FINDINGS.md §9's "not resolved" list); don't over-model.
};

struct Joint {
    uint32_t bodyA = 0, bodyB = 0;
    int16_t type = 0;  // 0=spherical,1=shoulder,2=weld,3=revolute,4=prismatic,5=distance
    int16_t index = 0; // index into the type-specific chunk
};
// + one struct per joint-type chunk (WeldJoint/SphericalJoint/ShoulderJoint/
//   RevoluteJoint/PrismaticJoint/DistanceJoint), fields per PHYS.md, version-gated
//   the same way Body is.

struct PhysV { std::array<float,6> values; };

struct File {
    int16_t version = 0;
    uint32_t phyt = 0;
    std::vector<Body> bodies;
    std::vector<Shape> shapes;
    std::vector<BoxShape> boxes;
    std::vector<CapsuleShape> capsules;
    std::vector<SphereShape> spheres;
    std::vector<PolytopeShape> polytopes;
    std::vector<Joint> joints;
    // + one vector per joint-type record type
    std::vector<PhysV> phyv;
};

File parse(const std::vector<uint8_t>& fileBytes);
```

**Version/chunk-variant selection logic to transcribe carefully** (all
confirmed in WIKI_FINDINGS.md §9, table above):

- Body: try `BDY4` → `BDY3` → `BDY2` → `BODY`, in that order (whichever
  chunk tag is present selects both the stride *and* whether the v3+ extra
  fields exist — do not derive this from the top-level `PHYS` version
  field, PHYS.md's own text: "chunk identifiers changed at v2*").
- Shape: `SHP2` → `SHAP`.
- Weld joint: `WLJ3` → `WLJ2` → `WELJ`.
- Shoulder joint: `SHJ2` (0x7c) is unambiguous by tag; a bare `SHOJ` tag is
  **not** — disambiguate by `chunk.size % 0x6c == 0` vs. `% 0x74 == 0`
  (never both in the 103-file sample; if a real file ever hits both,
  that's new information, not a case to silently guess at — throw
  `ParseError` naming the ambiguity rather than picking one).
- Prismatic/Revolute: `PRS2`/`REV2` → `PRSJ`/`REVJ`.
- Distance: `DSTJ` only (no variant).

---

## Implementation plan

1. **`src/phys.hpp`/`phys.cpp`**: the parser above. `ParseError`
   convention identical to `bone.hpp`/`skel.hpp`. Every offset
   bounds-checked before reading (`chunk.size` vs. required bytes),
   matching every other sidecar parser in this codebase — foreign data,
   never trust its own claims. `Body.shapeBase+shapeCount` / `Shape.index`
   / `Joint.bodyA`/`bodyB`/`index` should all be validated in-range at
   parse time (not deferred to the caller) — the investigation's own §5
   finding (zero out-of-range refs in 103 real files) means a real
   violation is either genuine corruption or a parser bug, either way
   worth catching immediately with a descriptive `ParseError`, same
   discipline `skel.cpp`'s "exact multiple of record size or throw"
   already uses.
2. **`src/m2.hpp`/`m2.cpp`**: no changes needed — `Header::physFileId`
   already exists (`m2.cpp:345`) and is exactly analogous to
   `skeletonFileId`/`SKID`; the new code just needs to *use* it, the way
   `cmd_export.cpp` already uses `skeletonFileId` for `.skel` resolution.
3. **CLI wiring** (`src/commands.hpp`, `src/cmd_export.cpp`): new
   `ExportOptions` field + flag, shape TBD by the plan-mode pass above
   (leaning `--phys`, three-state like `--skel`). Resolution happens in
   `cmd_export.cpp` right after the existing `--bones-dir` block (same
   place in the pipeline: after the skeleton is fully built, so
   `boneIndex` bounds-checks against real `skeleton.joints.size()`).
4. **`src/gltf.hpp`/`gltf.cpp`**: new `Skeleton::PhysicsBody` (or similar
   name — mirror `EmitterAnchor`'s shape: `id`/`joint`/`position`, plus
   whatever minimal extra field earns its place, e.g. `bodyType`) as a new
   `Skeleton` member (`physicsBodies`), serialized the same way
   `correctionSets`/`ribbonAnchors`/`particleAnchors` already are — same
   nested `tinygltf::Value` construction pattern, same out-of-range-joint
   validation in `writeGlbMulti`.
5. **`src/cmd_dump.cpp`**: new `dumpPhys` (mirroring `dumpEmitters`'s
   `writeRibbon`/`writeParticle` helper-function shape) — full JSON:
   every body/shape/joint/`PHYV` record, resolved (a body's shapes
   resolved to their real box/capsule/sphere/polytope data inline, a
   joint's `bodyA`/`bodyB` left as indices since that's how the source
   data relates them). Extend `dump-chunks`'s existing "or a `.bone` file
   directly" input path to also accept a `.phys` file directly — update
   the command's own doc comment (top of `cmd_dump.cpp`) the same way it
   was updated when ribbon/particle broadened this command's stated scope.
6. **`src/cmd_info.cpp`**: one-line-per-body/shape/joint summary (or a
   compact per-chunk-type count line, given `.phys` files can have 40+
   bodies — match `husk info`'s existing "keep it scannable" convention
   rather than the fuller per-entry dump particle/ribbon's summary uses),
   gated the same "verified floor, warn below it" way `kMinVerifiedParticleVersion`
   is — but per **chunk type present**, not per file version (§ above:
   e.g. a file with a `BOXS` chunk should warn specifically about `BOXS`
   being unverified, even if everything else in that same file is a
   verified chunk type).

---

## Test plan

**Real-data fixtures already committed** (`test_data/item/objectcomponents/weapon/*.phys`,
all 7 exercised this session): cover `PHYS`/`PHYT`/`BDY4`/`SHP2`/`CAPS`/`SHOJ`(0x74)/`JOIN`
unconditionally, `WLJ2`/`REVJ` on 2 of the 7 each
(`offhand_1h_artifactskulloferedar_d_06.phys`, `mace_1h_moargbruteboss_b_01.phys`
have `REVJ`; `offhand_1h_artifactskulloferedar_d_06.phys`,
`offhand_1h_voidelf_d_01.phys` have `WLJ2`). `mace_1h_warfrontsforsaken_d_01.m2`
+ `.phys` is a real paired fixture for a `boneIndex`-plausibility test
(bones: 17, `.phys` `boneIndex` values `{0..9}`, confirmed this session).

**Real-data gap, worth flagging explicitly**: **zero committed fixtures
have `PLYT`, `SPHS`, `BOXS`, `SPHJ`, `PRSJ`/`PRS2`, `DSTJ`, `SHJ2`, `WLJ3`,
`REV2`, or `PHYV`** — over half the real corpus sample has `PLYT` (the
structurally trickiest chunk, and the one this session's own bug was in),
but none of the 7 committed weapon fixtures happen to have one. Candidate
real corpus files, if Luna wants to extract 1-2 into `test_data/` the same
way the particle/ribbon session's weapon-set extraction happened
mid-session when real test data turned out to be the actual blocker:
- `PLYT`: `world/expansion07/doodads/8xp_heartofazeroth_prop_floatychain.phys`
  (2530 bytes, already used as this session's own worked example —
  4 polytopes, header fields matching PHYS.md's "mostly 8/6/24" exactly).
- `SPHS`: `creature/wingederedar/wingederedar.phys` or
  `creature/wingederedarboss/wingederedarboss.phys`.
- `BOXS`/`SPHJ`/`PRSJ`/`PRS2`/`DSTJ`/`SHJ2`/`WLJ3`/`REV2`: **no known real
  example anywhere in this session's 103-file sample** — either accept
  synthetic-only unit test coverage for these (hand-built minimal byte
  buffers, same style `test_bone.cpp`'s `buildBoneFile` helper uses), or
  run a wider corpus sweep first (a new `tools/find_phys_variants.py`,
  same shape as `find_multiroot_skeletons.py`, scanning
  `/media/luna/data/wow_export` for these specific chunk tags) if real
  coverage matters before shipping.

**New test files/cases**:

- `tests/test_phys.cpp` (new, mirrors `tests/test_bone.cpp`'s shape):
  synthetic byte-buffer construction per chunk type (happy path,
  truncated-chunk-throws, malformed-count-throws), one case per
  version/variant-selection branch (`BDY4`-preferred-over-`BODY`-when-both-
  present [shouldn't happen in real data, but the parser's own selection
  order needs a test], `SHOJ` stride disambiguation both ways, the
  ambiguous-both-divide-evenly throw path), out-of-range shape/joint index
  throws (mirrors `.bone`'s "corrects bone N, out of range" test style).
- `tests/test_gltf.cpp`: `PhysicsBody` extras round-trip (present/absent/
  out-of-range-joint-throws/coexists-with-`correctionSets`/`ribbonAnchors`/
  `particleAnchors` — same coexistence test `EmitterAnchor` got).
- `tests/test_cli.cpp`: the new `--phys` flag's three states (explicit
  path, `none`, auto-default), a real `.phys` fixture producing the
  expected `physics_bodies` extras + summary note, an out-of-range
  `.phys` body failing the export by naming the file/body index (mirrors
  the `.bone` out-of-range test).
- `tests/test_dump.cpp`: `dumpPhys`'s JSON shape (real fixture, checking
  every field surfaces, not just body count), the "`.phys` file passed
  directly to `dump-chunks`" input path.
- `tests/test_integration.cpp`: a `HUSK_TEST_*`-gated real-data case
  using `mace_1h_warfrontsforsaken_d_01.phys` (or whichever fixture ends
  up used) checked via tinygltf against exact body/shape/joint counts,
  plus a `gltf_validator` sweep (matching every other real-fixture test in
  this file) confirming `.phys` extras don't introduce new validator
  errors.

**Verification discipline to carry over**: before calling any of the
above "done," re-run the same independent Python cross-checks this
session already did (§5 of `WIKI_FINDINGS.md` §9 — index/bounds
integrity) against husk's *own* new C++ output, not just the scratch
decoder, the same way the collision-mesh and multi-texture-layer sessions
cross-checked husk's real output against a from-scratch independent
computation before trusting it.

---

## Docs to update once implemented

- `M2_COMPLETENESS.md`'s "Collision & physics" section — the
  `.phys sidecar content` row currently reads `none ... n/a — unscoped ...
  nobody has reverse-engineered .phys's own byte layout yet` — all three
  claims become false once this lands.
- `README.md` — the Collision/physics format-matrix row (currently notes
  "`.phys` file's own *content* is still completely unparsed") and the
  Sidecar FileDataID resolution row (currently: "`PFID`'s own `.phys` file
  content isn't touched at all yet").
- `DESIGN.md` — new Key design decisions bullet (the anchor/dump-chunks
  split, mirroring the existing particle/ribbon bullet), Boundaries list,
  CLI grammar table (if `--phys` lands as a new three/four-state flag),
  Open work pointer (remove once this file's own job is done, matching how
  the multi-root-bone-forest TODO's Open work pointer was removed and
  folded into its own Key design decisions bullet once that job was done).
- `WIKI_FINDINGS.md`'s "Where these live in husk" table — §9's row
  currently reads "not yet implemented — see `PHYS_TODO.md`"; fill in
  real `Code`/`Tests` columns once landed, same as every other row.
- `completions/husk.bash`/`.zsh` — regenerate via
  `husk --print-completion=<bash|zsh>` **and** add the new flag to
  `src/main.cpp`'s hand-maintained `bashValueCompletion`/`zshValueAction`/
  `zshFlagLabel` tables explicitly — these do **not** pick up a new flag's
  `none`/directory semantics automatically (found the hard way in an
  earlier session, per `CLAUDE.md`'s own Hazards note); verify by actually
  sourcing the regenerated script and driving `_husk_completions`/`_husk`,
  not just diffing that the flag name appears.
- `CLAUDE.md`'s `## Status`/`## Resume` — same pattern every prior
  feature landing used.
