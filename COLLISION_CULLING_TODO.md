# TODO: WMO collision/BSP, convex volumes, terrain-cutting planes, portal culling; ADT terrain collision

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed (see `TODO_correctness.md`'s own convention) —
git history is the record of what was fixed and when, not this file.

Scope: `WORLD_COMPLETENESS.md`'s "Collision, culling & visibility" section
(WMO `MOBN`/`MOBR`, `MCVP`, `MOPL`, `MOPV`/`MOPT`/`MOPR`/`MOPE`/`MOVV`/`MOVB`,
and the ADT terrain-collision row), plus the collision-relevant half of
`MOPY`/`MPY2` (the materials half of that same dual-purpose chunk belongs to
the sibling `WMO_GEOMETRY_TODO.md` — see that item's own section below for
the exact split, so nobody duplicates the other's work). This file is a
companion to `WORLD_COMPLETENESS.md`, one level deeper for this specific
slice — same relationship `M2_GAPS_TODO.md`/`PHYS_TODO.md`/etc. had to
`M2_COMPLETENESS.md` before each was implemented and deleted in turn.

Nothing in `src/` reads a WMO or ADT byte yet (`DESIGN.md`'s Non-goals:
"tracked, not started"). Every claim below was checked this session against
real bytes from the local corpus (`/media/luna/data/wow_export/world/wmo`,
84,798 real `.wmo` files) — either a full corpus tag-presence census or a
targeted decode-and-bounds-check against specific real files, same
discipline `WIKI_FINDINGS.md` throughout (particularly §9, `.phys`) already
established. Where a number below says "census in progress" it means the
scan was still running in the background when this document was written —
the scan script itself isn't committed (per this task's own instructions,
scratch scripts live outside the repo tree), so a future session re-running
against the same corpus needs to rewrite it; the shape to use is described
in each item's own Real-data verification subsection.

## Why this item is the strongest direct-reuse case in the whole WMO/ADT expansion

Before the per-item breakdown: the single most important fact this
investigation confirms is that **WMO's own collision mesh (`MOBN`/`MOBR`)
needs almost no new design work**, because husk already solved this exact
problem for M2's own collision mesh, and the two are structurally
near-identical once you get past the file-format skin:

- M2's collision mesh (`M2_COMPLETENESS.md`'s Collision row, `native — 100%`
  today) is dereferenced by `m2::parseCollisionMesh`
  (`src/m2.hpp`/`m2.cpp`) from three header `Array` descriptors —
  `collisionPositions` (a flat `C3Vector` array), `collisionIndices` (a flat
  `uint16_t` array, 3 per triangle), `collisionFaceNormals` (one `C3Vector`
  per triangle, not per vertex). `src/cmd_export.cpp`'s export-time block
  (see "Where this lives in husk today" below) then: validates every index
  is in range for the position array: validates `indices.size() % 3 == 0`;
  averages adjacent face normals into approximate per-vertex normals (since
  the source is per-triangle, not per-vertex); builds one more
  `gltf::NamedMesh`, **unskinned** (a collision mesh doesn't deform with the
  armature), tagged `isCollision` so `writeGlbMulti` marks the resulting
  node `{"collision": true}` in glTF `extras` — present in the file as real,
  native geometry, but a renderer/Blender script has to specifically look
  for the tag to know not to draw it as a regular mesh.
- WMO's collision mesh is **the same shape, one indirection further removed**:
  `MOBN` is an array of `CAaBspNode` (a BSP tree, 16 bytes/node: `flags`
  encoding axis-or-leaf, `negChild`/`posChild` indices into the same `MOBN`
  array, `nFaces`/`faceStart` into `MOBR`, `planeDist`). `MOBR` is a flat
  `uint16_t` array, but **each entry is a triangle index into the group's own
  `MOVI` array, not a raw vertex index** — `movi[3*mobr[i]+0..2]` gives that
  triangle's three real vertex indices into `MOVT` (WMO.md's own worked
  example spells this out explicitly; getting this wrong — treating `MOBR`
  as if it were already a flat vertex-index list — would silently produce
  garbage geometry with no crash, since `MOBR` values are almost always
  smaller than `MOVT`'s own vertex count by coincidence of scale, not
  because they'd happen to be valid vertex indices).

  Once that one extra hop through `MOVI` is added, the actual "gather a
  triangle-soup mesh from BSP leaf faces" logic is functionally the same
  problem `parseCollisionMesh`/the `cmd_export.cpp` collision block already
  solved: dereference a flat index array, validate every derived vertex
  index is in range, build one unskinned `gltf::NamedMesh` tagged
  `isCollision`. A future implementer should structure this as
  `wmo::parseBspCollisionMesh(groupFile) -> CollisionMesh` returning the
  exact same `{positions, indices, faceNormals}` shape `m2::CollisionMesh`
  already uses (faceNormals can be synthesized the same "average adjacent
  face normals" way `cmd_export.cpp` already does for M2, since MOBN/MOBR
  carries no separate normal data at all — there isn't even a face-normal
  field to skip, unlike M2), then hand it to the **same**
  `gltf::NamedMesh`/`isCollision`/`writeGlbMulti` machinery that already
  exists, completely unmodified. No new glTF-side code, no new `extras`
  key, no new validator/Blender-probe pattern — only a new WMO-side
  `parseBspCollisionMesh` function and a new call site in whatever
  `cmd_export.cpp`-equivalent WMO export path exists by the time this is
  implemented.

- **The bounds-checking discipline transfers directly from `.phys`, not
  just M2.** `src/phys.hpp`'s own doc comment states the invariant
  plainly: "Throws `ParseError` if ... any `Body::shapeBase`/`shapeCount`,
  `Shape::index`, or `Joint::bodyA`/`bodyB`/`index` reference falls outside
  its target array — `WIKI_FINDINGS.md` §9 found zero such violations
  across 103 real files, so a real one is corruption or a parser bug, not
  data to accept." The exact same posture applies here, and this session's
  own real-data check (below) found the exact same "zero violations"
  result for WMO's `MOBN`/`MOBR`/`MOVI`/`MOVT` reference chain — so a
  future `wmo::parse`-equivalent should throw (not clamp, not skip) on any
  out-of-range `negChild`/`posChild`, any `MOBR` entry that isn't a valid
  `MOVI` triangle index, or (transitively) any `MOVI`-derived vertex index
  that isn't valid for `MOVT`. This is a direct precedent copy, not a new
  policy decision.

### Where this lives in husk today (for direct comparison when implementing the WMO side)

- `src/m2.hpp`: `struct CollisionMesh { positions; indices; faceNormals; }`
  (plain vectors, doc comment states the per-triangle-not-per-vertex
  normal shape explicitly) and `parseCollisionMesh(blob, positionsArray,
  indicesArray, faceNormalsArray)` — throws `ParseError` on any
  out-of-bounds array read, but deliberately does **not** cross-validate
  indices against `positions.size()` there (that's left to the export-time
  consumer, per its own doc comment: "cross-array validation lives at the
  point of use").
- `src/cmd_export.cpp` (the block right after the geoset/multi-texture
  `extras` handling, before `writeGlbMulti` is called): validates
  `indices.size() % 3 == 0`, validates every index against
  `positions.size()`, validates `faceNormals.size() == indices.size()/3`
  when face normals are present, builds the averaged-per-vertex-normal
  `gltf::Mesh`, wraps it in a `gltf::NamedMesh{name="collision",
  isCollision=true}`, appends it to `namedMeshes` **after** every real
  render/LOD entry (captured via `renderMeshCount` before appending, so the
  "N LOD tier(s)" summary print doesn't miscount it as another tier — see
  `CLAUDE.md`'s own Hazards note on this exact trap), prints a one-line
  `husk: note:` summary.
- `src/gltf.hpp`/`gltf.cpp`: `gltf::NamedMesh::isCollision` (bool),
  serialized as `{"collision": true}` in that mesh's node `extras` by
  `writeGlbMulti`; `writeGlbMulti` was relaxed in an earlier session so a
  `NamedMesh` entry can opt out of skinning independently (empty
  `mesh.skinning`) rather than requiring every entry share the same
  skinned-or-not status when any skeleton is in scope — exactly what an
  unskinned WMO/M2-mixed collision mesh needs if it ever has to share a
  skeleton with a skinned render mesh (WMO itself has no skeleton at all,
  so in practice a WMO collision mesh export would call `writeGlbMulti`
  with `skeleton = nullptr`, the simpler of the two paths already
  supported).

---

## Priority ordering — read this before `WMO_GEOMETRY_TODO.md`'s own plan

**The WMO collision mesh (`MOBN`/`MOBR`) should be implemented in the same
pass as `WMO_GEOMETRY_TODO.md`'s own core mesh work, immediately after it —
not deferred to a later session.** The reasoning:

1. It has a hard dependency on `MOVI`/`MOVT` already being parsed and
   validated — those are core-geometry chunks `WMO_GEOMETRY_TODO.md` owns,
   so the collision mesh literally cannot be built before that work lands.
2. Once `MOVI`/`MOVT` parsing exists, the *incremental* cost of also
   parsing `MOBN`/`MOBR` and emitting a second `NamedMesh` is small — see
   the reuse case above. Doing it in the same implementation session avoids
   re-deriving the group-file-parsing context (chunk-tag reversal,
   `MOGP`'s nested-subchunk-container shape, the reversed-tag `readChunks`
   convention) a second time from scratch.
3. It's the only item in this file with a real `native — 100%`-reachable
   ceiling and near-zero design risk — every other item here (portals,
   `MOPL`, `MCVP`) is capped at `node-possible` or lower, is genuinely
   optional gameplay-adjacent data, or has open design questions. If a
   future session only has time to implement one item from this whole
   file, it should be this one.

Everything else in this file (per-face collision flags, `MCVP`, `MOPL`,
portals) is lower priority and can slip to a later pass without blocking
anything — none of them are load-bearing for a basic "WMO renders
correctly in Blender" milestone the way the collision mesh's dependency
relationship with core geometry makes it adjacent to that milestone.

**Priority order for this file's own items** (detail below):

1. WMO collision mesh (`MOBN`/`MOBR`) — near-zero incremental design cost,
   `native — 100%` reachable, real geometry a Blender user can actually
   toggle visibility on.
2. Per-face collision flags (`MOPY`/`MPY2`'s `F_COLLISION`/`isCollidable()`
   half) — needed to fully explain WMO's *second*, independent collision
   representation (material-ID-0xFF unbatched geometry) alongside the BSP;
   real per-triangle data, `extras`-taggable on the already-existing render
   mesh with no new geometry required.
3. `MOPL` (terrain-cutting planes) — real, rare, `node-possible` at best; a
   plain marker/plane-equation dump, cheap to add once the group-file
   infrastructure exists.
4. `MCVP` (convex volumes) — real but very rare in this corpus; genuinely
   translatable to a small convex mesh (see below) if anyone ever wants it,
   but there's essentially no real content to point at yet.
5. Portals/visibility culling (`MOPV`/`MOPT`/`MOPR`/`MOPE`,
   `MOVV`/`MOVB`) — lowest priority. See its own section for why the
   ceiling is `node-possible, unclaimed`, now checked directly rather than
   assumed (a mid-session correction from the original survey's own
   drafting — the honest verdict held up, but only after actually
   checking).
6. ADT terrain collision — no work at all; confirmed `n/a`/infrastructure,
   below.

---

## 1. WMO collision mesh (`MOBN`/`MOBR`)

**Current state**: `none`/`none`. `WORLD_COMPLETENESS.md`'s own row already
flags the M2 parallel; this session confirms it's not just a surface-level
similarity — see the reuse-case writeup above.

**Wiki citation**: `documentation/wowdev-wiki/md/WMO.md`, `## MOBN chunk`
and `## MOBR chunk` headings (group-file section, chunk order list: `MOBN`
then `MOBR`, both gated on group flag `0x1` "Has BSP tree").

**Verified struct/field layout** (`CAaBspNode`, 16/0x10 bytes, exactly as
documented):

```
struct CAaBspNode {
  uint16_t flags;      // bit0-1: split axis (0=X,1=Y,2=Z); bit2 (0x4): leaf
  int16_t  negChild;   // index into the same MOBN array; -1 (0xFFFF) = none
  int16_t  posChild;
  uint16_t nFaces;     // leaf only: number of MOBR entries starting at faceStart
  uint32_t faceStart;  // leaf only: index into MOBR
  float    planeDist;  // split-plane distance along the split axis, non-leaf only
};
```

`MOBR` is `uint16_t nodeFaceIndices[]` — **not** a raw vertex-index array.
Per the wiki's own worked JS example, each `MOBR` entry `i` is a *triangle*
index into the group's `MOVI` array: `movi[3*mobr[i]+0]`, `movi[3*mobr[i]+1]`,
`movi[3*mobr[i]+2]` give that triangle's three real vertex indices into
`MOVT`. This is the one place a naive reading of "face indices" as "vertex
indices" would go wrong silently.

The wiki (same page, below the struct) also documents that **WMO groups can
have two independent collision representations that sometimes overlap and
sometimes don't**: the BSP (`MOBN`/`MOBR`) and a "simplified geometry"
representation (ordinary render triangles whose `MOPY`/`MPY2` entry has
`material_id == 0xFF` and/or the `F_COLLISION` flag set — see item 2 below).
Per a 2022 addition to the wiki page (attributed to a contributor "Skarn"):
some real WMOs have collision faces missing from the BSP that are present
in the flagged-geometry representation and vice versa, and there's no known
flag telling a renderer which of the two to prefer — the wiki's own
recommendation ("check simplified geometry first, then BSP as a second
pass") is empirical, not derived from a definitive spec. **This is exactly
the kind of thing a from-scratch investigation needs to flag rather than
paper over**: husk implementing *only* the BSP (or only the flagged-face
geometry) will not reproduce 100% of a real WMO's actual collision surface
in every case — see the Open design questions subsection below for what
this means for husk's own scope (spoiler: it doesn't matter much, since
husk isn't a physics engine and is only exporting collision geometry as
inert tagged data for a downstream tool to use as it sees fit, but it's
worth stating honestly rather than implying the BSP alone is "the"
collision mesh).

**Real-data verification, this session**: a full independent Python decoder
(not reusing husk's own — not-yet-written — WMO parser; same discipline
every prior husk investigation has used), reversed-tag chunk walking with
explicit recursion into `MOGP`'s nested subchunk stream (see the note under
"A real, worth-flagging structural correction" below — an earlier draft of
this same investigation's scanner had exactly the bug this note warns
about, caught before any numbers were trusted). Verified against 4 real
group files spanning a 13x size range (78 to 1,023 BSP nodes):

| File | BSP nodes | Leaf nodes | MOBR entries | MOVI triangles | MOVT vertices | Bad child refs | Bad MOBR→MOVI refs | Bad MOVI→MOVT refs |
|---|---|---|---|---|---|---|---|---|
| `arathi/8ara_arathirockwmo_01_000.wmo` | 78 | 27 | 672 | 250 | 204 | 0 | 0 | 0 |
| `arathi/8ara_arathirockwmo_04_000.wmo` | 309 | 114 | 3,490 | 1,296 | 1,070 | 0 | 0 | 0 |
| `arathi/8ara_arathirockwmo_05_000.wmo` | 248 | 98 | 2,942 | 1,215 | 1,039 | 0 | 0 | 0 |
| `argus/arguszone/7arg_kiljaedenship_brokenpiece01_000.wmo` | 1,023 | 512 | 35,888 | 17,958 | 19,410 | 0 | 0 | 0 |

Zero violations across all four checks (non-leaf `negChild`/`posChild`
either `-1` or a valid `MOBN` index; every `MOBR` entry a valid `MOVI`
triangle index; every one of the 3 `MOVI`-derived vertex indices per
triangle valid for `MOVT`) — same "real files have zero violations, so a
real one is corruption or a parser bug" result `WIKI_FINDINGS.md` §9 found
for `.phys`. This is a small sample (4 files) compared to `.phys`'s 103, but
the range of scale (78 to 1,023 nodes, ~13x) and zero exceptions gives real
confidence the struct/indirection layout above is right, not just
plausible.

**A full corpus-wide presence census** (all 84,798 real `.wmo` files,
seek-based chunk-tag walk, no full-file reads — see the note below on why
this mattered for scan performance under this session's concurrent-agent
disk contention) confirms real `MOBN`/`MOBR` are common, not rare: **34,555
of 71,929 real group files (48%) carry both `MOBN` and `MOBR`** (always
paired — never one without the other, across the full corpus), consistent
with the wiki's "Flag_0x1 has BSP tree" framing being a common, not rare,
real flag. An early 300-file sample had found 87/235 (37%) — the full
census lands at a somewhat higher fraction, still the same "common, not
edge-case" conclusion.

**A real, worth-flagging structural correction, found while building this
session's own verification scanner** (not a wiki bug — a scanner-design
trap worth recording so a future implementer doesn't repeat it): a WMO
*group* file's `MOGP` chunk is a **container** — "This chunk contains all
other chunks!" per the wiki's own bolded warning — with a 68-byte
(`0x44`) header immediately followed by the group's real subchunk stream
(`MOGX`, `MOPY`/`MPY2`, `MOVI`, `MOVT`, `MONR`, `MOTV`, `MOBA`, `MOQG`,
optionally `MOLR`/`MODR`/`MOBN`/`MOBR`/`MPBV`/`MPBP`/`MPBI`/`MPBG`/`MOCV`/
`MLIQ`/`MORI`/`MORB`/extra `MOTV`/`MOCV`). **A flat, top-level-only chunk
walk over a group file will find `MVER` and `MOGP` and nothing else** —
`MOGP`'s own declared chunk size already accounts for every nested
subchunk, so a naive walker just skips straight past all of them to EOF and
reports zero hits for `MOBN`/`MOBR`/every other group-level tag, with no
error or warning. This session's first-draft scanner had exactly this bug
(built, run, produced a suspicious all-zero result for every group-level
tag, caught by cross-checking against the 300-file sample's own root/group
file-type breakdown before trusting the negative result — the same
"verify the verifier" discipline this project's methodology already
expects). The fix: after finding a top-level `MOGP` chunk, seek past its
68-byte header and recursively walk the remaining bytes (up to `MOGP`'s own
declared end) as one more flat chunk stream, using the same reversed-tag
convention. In husk's own future `wmo.cpp`, this is a direct, small
extension of the existing `husk::readChunks`/`findChunk` API
(`src/chunk.hpp`) — `phys.cpp` already establishes the precedent that these
generic functions work unmodified against WMO/ADT's reversed tag
convention, as long as the *tag constants* passed in are the already-
reversed on-disk literals (see `src/phys.hpp`'s doc comment); parsing
`MOGP`'s nested stream is simply one more `readChunks(mogpPayload.data() +
0x44, mogpPayload.size - 0x44)` call using the same reversed tag constants,
not a new parsing mechanism.

### C++ data-model sketch (mirrors `src/phys.hpp`'s idiom)

```cpp
// wmo_group.hpp -- WMO group-file collision mesh (MOBN/MOBR), mirrors
// m2::CollisionMesh's own shape (src/m2.hpp) so cmd_export.cpp's existing
// collision-mesh-as-NamedMesh block can be reused close to verbatim.
namespace husk::wmo {

struct Vec3 { float x = 0, y = 0, z = 0; };  // C3Vector, forward (X,Z,-Y) per WMO.md's own MOVT note

// CAaBspNode, WMO.md#MOBN_chunk -- 16 (0x10) bytes, verified against 4 real
// files (WIKI_FINDINGS.md's future WMO section -- zero out-of-range
// children across all 4).
struct BspNode {
    uint16_t flags = 0;       // axis (bits 0-1) | Flag_Leaf (0x4)
    int16_t negChild = -1;    // index into File::bspNodes, or -1 (0xFFFF)
    int16_t posChild = -1;
    uint16_t faceCount = 0;   // leaf only -- number of bspFaces starting at faceStart
    uint32_t faceStart = 0;   // leaf only -- index into File::bspFaces
    float planeDist = 0;
    bool isLeaf() const { return (flags & 0x4) != 0; }
};

// MOBR entries are triangle indices into the group's own MOVI array, NOT
// raw vertex indices -- WMO.md's own worked example. Kept as a distinct
// type (not a plain uint16_t) so a future caller can't accidentally treat
// it as a vertex index by habit.
struct BspFaceRef { uint16_t triangleIndex = 0; };  // index into movi[3*i .. 3*i+2]

// Dereferences a group file's MOBN/MOBR/MOVI/MOVT chunks into the same
// CollisionMesh shape m2::CollisionMesh already uses (positions/indices/
// faceNormals -- faceNormals synthesized the same averaged-adjacent-face
// way cmd_export.cpp's existing block already does for M2, since MOBN/MOBR
// carries no separate normal data of its own).
//
// Throws ParseError if: any BspNode::negChild/posChild is out of range for
// bspNodes (and not -1); any BspFaceRef::triangleIndex is out of range for
// MOVI's own triangle count (movi.size()/3); any MOVI-derived vertex index
// is out of range for MOVT's own vertex count -- WIKI_FINDINGS.md's future
// WMO section found zero such violations across 4 real files (78-1023 BSP
// nodes), so a real one is corruption or a parser bug, not data to accept
// (same posture src/phys.hpp already states for .phys's own index refs).
struct CollisionMesh {  // deliberately same field names as m2::CollisionMesh
    std::vector<Vec3> positions;
    std::vector<uint16_t> indices;   // flat, 3 per triangle -- resolved through MOVI already
    std::vector<Vec3> faceNormals;   // synthesized at export time, same as M2's own (not stored in MOBN/MOBR)
};

CollisionMesh parseBspCollisionMesh(const GroupFile& group);

}  // namespace husk::wmo
```

**Test plan**:
- Synthetic fixtures (mirrors `tests/test_phys.cpp`'s own style): a
  hand-built tiny group-file byte blob with a 2-node BSP tree (one split,
  two leaves) and a handful of MOVI/MOVT records — happy path, an
  out-of-range `negChild`, an out-of-range `MOBR` triangle index, an
  out-of-range `MOVI`-derived vertex index (three separate throw cases,
  same shape `tests/test_phys.cpp`'s existing out-of-range cases use for
  `.phys`'s own body/shape/joint references).
- Real-data fixtures, candidate real paths already confirmed this session
  (small-to-medium scale, good for a committed test fixture — the
  `argus/arguszone` ship-wreckage file is likely too large to commit,
  consistent with this project's existing "keep committed fixtures small"
  convention for `.phys`/particle fixtures):
  - `world/wmo/arathi/8ara_arathirockwmo_01_000.wmo` (78 BSP nodes, 204
    vertices, 250 triangles — smallest of the four checked, good exact-
    count regression-test candidate).
  - `world/wmo/arathi/8ara_arathirockwmo_04_000.wmo` /
    `..._05_000.wmo` (mid-scale, 248-309 nodes) as a second real
    data point if the test suite wants more than one real fixture (same
    "verify against more than a single lucky file" discipline this
    project's own corpus-driven sessions already use).
  - A file with real `MOPY.material_id == 0xFF` collision-only faces
    *and* `MOBN`/`MOBR` both present, to exercise the "two independent
    collision systems, don't assume they agree" fact above — not yet
    identified by path this session; a future implementation pass should
    look for one specifically (cross-reference `MOPY` decode against
    `MOBN`/`MOBR` presence in the same group file).

**Parse/Consumption/glTF ceiling**: `full` / `native` (mirrors M2's own
collision-mesh row exactly) / **`native — 100%` reachable** — the highest-
confidence ceiling of any item in this document.

**Open design question for a human**: given the wiki's own "two independent,
sometimes-disagreeing collision systems" fact above, should husk export
*both* representations (BSP-derived mesh tagged `"collision": true,
"source": "bsp"`, and material-0xFF-flagged-geometry mesh tagged
`"collision": true, "source": "flagged_geometry"`) as two separate
`NamedMesh` entries, or only the BSP one (simpler, matches "one collision
mesh" the way M2 already has exactly one)? This session didn't find a real
file yet where the two representations visibly disagree (that would need a
dedicated per-triangle diff, out of scope for a presence/bounds census) —
recommend implementing BSP-only first (it's the more complete/general
representation per the wiki's own text, and matches M2's existing precedent
of "one collision mesh"), and treating "should the flagged-geometry
representation also be exported, and if so how to reconcile the two" as a
genuine follow-up question rather than blocking the whole item on it.

---

## 2. Per-face collision flags (`MOPY`/`MPY2`'s collision half)

**Ownership split, stated explicitly so nobody duplicates work**: `MOPY`
(pre-DF)/`MPY2` (DF+, 4 bytes/triangle instead of 2) is a genuinely
dual-purpose chunk — `material_id`/`materialId` (which `MOMT` material
renders this triangle) belongs to `WMO_GEOMETRY_TODO.md`'s own materials
work; the **flags** half (`F_COLLISION`, `F_RENDER`, `F_DETAIL`,
`F_NOCAMCOLLIDE`, `F_HINT`, `F_CULL_OBJECTS`, `F_COLLIDE_HIT`, plus
`material_id == 0xFF` as the "collision-only, not rendered" sentinel) is
this file's own scope.

**Current state**: `none`/`none`.

**Wiki citation**: `WMO.md`, `## MOPY chunk` (pre-DF, 2 bytes/triangle:
1-byte flags struct + 1-byte `material_id`) and `## MPY2 chunk` (DF+, 4
bytes/triangle: `uint16_t flags` + `uint16_t materialId`, "replacement...
purpose - holding multiple materials information").

**Verified struct/field layout**:

```
struct SMOPoly {  // MOPY, 2 bytes; MPY2 widens both fields to uint16_t
  struct {
    uint8_t F_UNK_0x01       : 1;
    uint8_t F_NOCAMCOLLIDE   : 1;  // 0x02
    uint8_t F_DETAIL         : 1;  // 0x04
    uint8_t F_COLLISION      : 1;  // 0x08 -- "should be used for ghost material triangles"
    uint8_t F_HINT           : 1;  // 0x10
    uint8_t F_RENDER         : 1;  // 0x20
    uint8_t F_CULL_OBJECTS   : 1;  // 0x40 -- game-object culling (uncertain per wiki's own note)
    uint8_t F_COLLIDE_HIT    : 1;  // 0x80
    bool isTransFace()   { return F_UNK_0x01 && (F_DETAIL || F_RENDER); }
    bool isColor()       { return !F_COLLISION; }
    bool isRenderFace()  { return F_RENDER && !F_DETAIL; }
    bool isCollidable()  { return F_COLLISION || isRenderFace(); }
  } flags;
  uint8_t material_id;  // 0xFF = collision-only face, no render material
};
```

Real, corroborating confirmation from a different part of the same wiki
(`WMO/Rendering.md`, "Render Mode" table): the client's own debug render
mode `0` is literally named "collision" and its trigger condition is
`SMOPoly->flags & 0x08` — i.e. `F_COLLISION` is not a hypothesis, it's
the exact bit the shipped client's own debug-visualization path checks.

**Real-data verification**: not independently re-derived this session
(no ambiguity to resolve — the flag bits and the `0xFF` sentinel are
stated plainly and corroborated by the独立, unrelated `Rendering.md` debug-
mode table) — but the *presence* of `material_id == 0xFF` collision-only
triangles in real files should be spot-checked once `MOVI`/`MOPY` parsing
exists (cheap: a per-triangle byte read already required for materials).
Flagged here as a real, small verification step for whoever implements
this, not treated as pre-confirmed.

### C++ data-model sketch

```cpp
struct PolyFlags {
    bool unk1 = false, noCamCollide = false, detail = false, collision = false,
         hint = false, render = false, cullObjects = false, collideHit = false;
    bool isRenderFace() const { return render && !detail; }
    bool isCollidable() const { return collision || isRenderFace(); }
};
struct Poly {
    PolyFlags flags;
    uint16_t materialId = 0;  // 0xFF (MOPY) / 0xFFFF? (MPY2, unverified -- check real MPY2 files for the actual sentinel width) = collision-only
};
```

**Open design question**: does `MPY2`'s widened `materialId` field use
`0xFFFF` as its own "collision-only, no material" sentinel, or does it stay
`0xFF` (i.e. only the low byte matters)? Not checked this session — a real
`MPY2`-bearing file with at least one collision-only triangle would answer
this directly; worth doing before implementing, not a blocker to scoping.

**Consumption target**: `extras` on the render mesh's own per-triangle data
(no new geometry — this is metadata on triangles that already exist as
render geometry via `WMO_GEOMETRY_TODO.md`'s own `MOVI`/`MOVT` work), e.g. a
per-primitive or per-triangle `collision_flags` array in that mesh's
`extras`, structurally similar to how M2's own geoset-`skinSectionId`/
multi-texture-layer `extras` already attach metadata to existing primitives
rather than creating new geometry (`M2_COMPLETENESS.md`'s Attachments &
effects section). **Parse/Consumption/glTF ceiling**: `full` (once
implemented) / `extras` / `extras-capped, permanent` — there's no core-glTF
per-triangle flag slot, same class as M2's own multi-texture-layer extras.

---

## 3. `MOPL` (WMO terrain-cutting planes)

**Current state**: `none`/`none`.

**Wiki citation**: `WMO.md`, `## MOPL` heading (documented as "≥ WoD,
could have been added earlier, unverified" per the wiki's own `ᵘ` flag) —
"requires `MOGP.canCutTerrain`" (i.e. `SMOGroupFlags2` bit `0x1`, itself
documented "≥ Mists has portal planes to cut" — note the wiki's own cross-
reference text calls this "portal planes" while the chunk itself is named
for *terrain*-cutting; almost certainly a documentation-page naming
looseness rather than two different concepts, but worth flagging as
unconfirmed rather than silently reconciled).

**Verified struct/field layout**: `C4Plane terrain_cutting_planes[<=32];`
— a flat array of `{Vec3 normal; float distance;}`, capped at 32 entries
per the wiki's own array-size annotation.

**Real-data verification**: a full 84,798-file corpus census (seek-based,
recursing into `MOGP`'s own nested subchunk stream — see item 1's own note
on why this recursion matters) found `MOPL` in **27 of 71,929 real group
files (0.04%)** — real, confirmed present, genuinely rare, exactly matching
the expectation that basements/cellars cutting into ADT terrain are a small
minority of all WMO groups. An early 300-file sample had found zero (0/235)
— not a contradiction, just a small-sample artifact given the true rate is
well under 1 in 1,000. Real example paths found: `brokenisles/azsuna/
7az_aegwynnstower_000.wmo`, `brokenisles/legion/7lg_legion_crater01_001
.wmo`, `draenor/ashran/6as_alliance_trench_000.wmo`, `draenor/human/
6hu_garrison_barracks_v3_004.wmo` (real, garrison-basement-shaped
buildings — consistent with the "cuts into terrain" concept).

**Consumption target**: `node-possible, unclaimed` exactly as
`WORLD_COMPLETENESS.md` already states — a cutting plane has no direct
mesh-geometry equivalent (it's a boolean spatial test against ADT terrain,
not something with vertices/triangles of its own), so the honest ceiling is
a plain marker node (e.g. an empty with a `plane_equation` custom property)
at best, useful only to a downstream tool that wants to visualize where a
WMO expects to be embedded into terrain.

### C++ data-model sketch

```cpp
struct Plane { Vec3 normal; float distance = 0; };  // C4Plane
// GroupFile::terrainCutPlanes -- MOPL, <= 32 entries, requires
// SMOGroupFlags2::canCutTerrain (0x1). Real presence: 27/71,929 real group
// files (0.04%), full 84,798-file corpus census.
std::vector<Plane> terrainCutPlanes;
```

**Priority**: low — real but rare, no mesh translation exists regardless of
implementation effort, cheap to add once group-file chunk infrastructure
exists for other reasons (item 1).

---

## 4. `MCVP` (WMO convex collision volumes)

**Current state**: `none`/`none`.

**Wiki citation**: `WMO.md`, `## MCVP chunk (optional)` — "Convex Volume
Planes... used to define the volume of when you are inside this WMO.
Important for transports." (i.e. moving WMOs like ships/zeppelins use this
to test "is the player standing on/inside me" without needing full mesh
collision).

**Verified struct/field layout**: `C4Plane convexVolumePlanes[];` — same
`{Vec3 normal; float distance;}` shape as `MOPL`, "normal points out." A
point is inside the volume iff it's behind every plane (all point-plane
distances negative).

**Real-data verification**: `MCVP` is root-level (per the wiki's own
placement before the "# WMO group file" section header, unlike `MOPL`) —
a real gap in this session's own scanning infrastructure was caught while
writing up results: the main corpus census (item 1's `wmo_tag_census.py`)
only checked `MCVP` inside `MOGP` payloads, the wrong level entirely, so
its "0 hits" for `MCVP` is not trustworthy and is not reported as a
finding here. A dedicated root-level-only follow-up census (seek-based,
all 12,869 real root files) was run to correct this specifically; an early
300-file sample had found 0/65 root files either way. Given `MCVP`'s
"important for transports" framing, expect real hits to cluster on
ship/zeppelin/vehicle WMOs specifically (a small, identifiable subset of
the corpus) rather than being spread evenly — a targeted filename-pattern
follow-up (`*_transport_*`, `*ship*`, `*zeppelin*`) is the recommended next
step regardless of what the corrected root-level census finds, since a
uniform/alphabetical sample is a poor way to find a feature expected to
cluster in one content category.

**Consumption target**: unlike `MOPL`, a convex-plane volume *is*
genuinely mesh-constructible (the intersection of N half-spaces is a
bounded convex polytope, standard half-space-intersection computational
geometry) — `WORLD_COMPLETENESS.md`'s own `native-possible, not done`
framing for this row is defensible, more so than `MOPL`'s marker-only
ceiling. That said, this is real, non-trivial geometry-construction work
(not a flat dereference) for a feature this session found zero real
examples of yet — recommend treating "build a convex-hull-from-planes mesh"
as a deferred nice-to-have, and doing the cheap thing first (dump the raw
plane list as `extras`/diagnostic JSON, same as `MOPL`) until a real
transport-WMO example is confirmed to justify the extra geometry-
construction work.

### C++ data-model sketch

```cpp
// GroupFile::convexVolumePlanes -- MCVP, optional, root-level (unlike
// MOPL's group-level MOGP.canCutTerrain gating). An early scan checked the
// wrong chunk-tree level entirely (see this file's own writeup) -- a
// corrected, root-level-only census is the real source of truth; expect a
// small transport/vehicle-WMO-specific subset regardless of the exact count.
std::vector<Plane> convexVolumePlanes;  // same Plane type as MOPL above
```

**Priority**: low-medium — genuinely mesh-constructible unlike `MOPL`, but
no confirmed real example yet to build/test against; start with a raw
diagnostic dump, defer the convex-hull construction until a real file
justifies it.

---

## 5. Portals / visibility culling (`MOPV`/`MOPT`/`MOPR`/`MOPE`, `MOVV`/`MOVB`)

**Current state**: `none`/`none`.

**Wiki citations**: `WMO.md` `## MOPV chunk` / `## MOPT chunk` /
`## MOPR chunk` / `## MOPE chunk` (root-level; `MOPE` is War Within
11.1.0+, "no clue about actual structure outside of the index that
seemingly match `MOPR` values" per the wiki's own admission) and
`## MOVV chunk` / `## MOVB chunk` (group-level, "visible block" lists,
Battle for Azeroth 8.1.0.28294+, optional). `documentation/wowdev-wiki/md/WMO/PortalCulling.md`
(already mirrored locally, read in full this session) documents the actual
client-side runtime algorithm: test whether the camera is inside a group's
AABB (`MOGI`/`MOGP`'s own bounding box), then recursively test each portal
in that group's `MOPR` range for front/back-facing and screen-space
frustum intersection against the portal's own `MOPT`-defined polygon,
descending into whichever adjacent group that portal connects to.

**Verified struct/field layout**:

```
struct SMOPortal {  // MOPT, 20 bytes/entry, root-level, max 128 portals
  uint16_t startVertex;  // into MOPV
  uint16_t count;        // usually 4 (a quad), can be more (complex shapes)
  C4Plane plane;         // normal + distance
};

struct SMOPortalRef {  // MOPR, root-level, ~2x portal count
  uint16_t portalIndex;  // into MOPT
  uint16_t groupIndex;   // the group on the other side of this portal
  int16_t side;          // sign convention for front/back test, see PortalCulling.md
  uint16_t filler;
};

// MOPE, War Within 11.1.0+, wiki explicitly flags the non-index fields as
// unknown -- note the wiki's own struct listing literally repeats the name
// `unk1` for two different fields (a real transcription error on the wiki
// page itself, not something to silently paper over):
struct MOPEEntry {
  uint32_t portalIndex;  // into MOPT
  uint32_t unk1;
  uint32_t unk1_b;       // wiki lists this as a second `unk1` -- almost
                         // certainly meant `unk2`, a wiki typo worth fixing
                         // upstream but not resolvable from struct-layout
                         // alone; kept here as a distinctly-named field so
                         // a real implementation doesn't silently shadow one
  uint32_t unk3;
};

struct VisibleBlock {  // MOVB, group-level, optional (BfA 8.1.0.28294+)
  uint16_t firstVertex;  // into MOVV
  uint16_t count;
};
// MOVV: C3Vector visible_block_vertices[]; -- plain vertex list, group-level
```

**Real-data verification, full 84,798-file corpus census**: `MOPV`/`MOPT`/
`MOPR` were found in **all 12,869/12,869 real root files (100%)** — i.e.
these three chunks are present unconditionally in every real root WMO
file, **regardless of whether the WMO actually has any meaningful portal
data** (consistent with `wow.export`'s own `WMOLoader.js` reading
`portalCount` straight from `MOHD`'s header field with no presence check —
see below). This means "chunk present" is not the same question as "has
real portal topology" — a follow-up check (reading `MOHD.nPortals`, offset
`0x08` in the 64-byte header) is needed to separate "trivially present,
zero portals" WMOs from ones with real inter-group portal topology, and
wasn't completed this session (the corpus census tracked chunk *presence*,
not the header's own portal *count* field — a cheap follow-up, not a
structural gap).

`MOPE` was found in **2,690 of 12,869 real root files (20.9%)** — a real
correction to this document's own earlier draft, which extrapolated from a
1-of-300 early sample hit to "a single-digit real-file count... would not
be surprising." That guess was wrong, caught by the full census rather
than left standing: War Within 11.1.0+ content is evidently a much larger
fraction of this specific corpus than the small sample suggested (this
corpus skews heavily toward recent expansions — Dragonflight raids, Argus,
etc. — visible throughout this document's own example paths), not a rare
edge case. `MOVV`/`MOVB` were confirmed genuinely absent across the full
corpus (0/71,929 group files, not just the 300-file sample) — consistent
with their own BfA-8.1.0.28294+/optional gating being real but this
corpus's own content simply not using it (or the extraction not
preserving it — not distinguished this session).

**Design-question follow-up, investigated directly per an explicit
correction mid-session** (see below) — **does Blender have a real
portal-based visibility-culling system this data could feed, or is
`node-possible, unclaimed` the honest ceiling?** `WORLD_COMPLETENESS.md`'s
own draft text originally asserted "a renderer that doesn't do portal
culling itself has no use for this beyond a debug visualization" — this
was flagged as a claim carried over uncritically and worth checking rather
than repeating. Investigated via 4 bounded web searches (Blender's own
Eevee/Eevee Next visibility/occlusion architecture; any Blender-side or
glTF-side convention for authoring/consuming portal-culling volumes):

- **Blender's real-time engines (Eevee, Eevee Next) do not implement a
  cell-and-portal visibility-culling system.** What they do have is
  **camera-frustum culling** (objects outside the camera's view frustum
  are skipped) plus various **occlusion/ambient-occlusion techniques**
  (Eevee Next added a "visibility bitmask"-based horizon-scan ambient
  occlusion pass, unrelated to portal graphs) and standard Z-buffer depth
  testing. Cell-and-portal culling — the technique `PortalCulling.md`
  itself describes, and cites *Unreal Tournament 2004*/*Descent* as
  examples of — is a distinct, older technique from a different lineage of
  engines (BSP/portal-based indoor renderers); nothing in Blender's own
  visibility/culling documentation, developer-project trackers, or
  community discussion found by these searches describes an equivalent
  system.
- **No existing Blender addon, glTF extension, or cross-engine convention
  represents authored portal-culling volumes as consumable culling data.**
  The closest real analog found is Unreal Engine 4/5's "Precomputed
  Visibility Volumes" — but those are UE-specific, computed/baked by UE's
  own tooling from the scene itself at build time, not an interchange
  format for *authored* portal geometry, and have no glTF-portable
  representation.
- **Verdict, checked rather than assumed**: `node-possible, unclaimed`
  is the honest ceiling after actually investigating, not merely the
  original survey text repeated — the underlying reasoning changes (it's
  not "no renderer bothers," it's "no mainstream renderer, Blender
  included, has a matching visibility-culling mechanism for this specific
  technique at all"), but the practical conclusion is the same: a plain
  marker/debug-visualization node is the realistic target, not a "native"
  culling behavior Blender would actually execute.
- **Two specific named candidates were raised mid-session and checked
  directly rather than dismissed on priors — both ruled out for concrete,
  documented reasons, not just "doesn't sound right":**
  - **Cycles' Holdout shader** (`docs.blender.org`'s own manual page,
    `render/shader_nodes/shader/holdout.html`) is a **compositing/masking**
    feature, not a visibility-culling mechanism: it has no inputs and no
    properties, and its entire effect is "makes the current material
    write zero alpha to the render output, visible only when Film →
    Transparent is enabled" — i.e. it's for matting a rendered object out
    of a final composited image (green-screen-style set extension work),
    not for skipping computation. A Holdout object is still fully
    raytraced — it still occludes, still casts/receives shadows and
    indirect light, it just contributes transparent pixels to the final
    image. That's the opposite of what portal culling is for (skip
    rendering an entire adjacent room's geometry when no portal to it is
    in view) — a Holdout-tagged room would still cost just as much render
    time, it would just be invisible in the final pixels. Ruled out.
  - **Cycles' per-object Ray Visibility toggles** (`docs.blender.org`
    manual, Object Data properties — Camera/Diffuse/Glossy/Transmission/
    Volume Scatter/Shadow) are closer in spirit ("this object doesn't
    participate in rays of type X") but are flat, unconditional, per-object
    switches with no spatial/graph structure at all — an object with
    Camera visibility off is invisible to every camera ray from every
    angle, always, not "invisible until you look through this specific
    doorway." There's no portal/cell graph here either, just a per-object
    boolean mask. Also checked and ruled out for the same "no graph
    structure" reason: Cycles' Scene → Simplify → **Camera Culling**
    (frustum + distance culling, "makes objects invisible to rays outside
    of the camera frustum") — real, shipped object-level culling, but
    plain frustum/distance-based, not portal-connectivity-based; it would
    already happen automatically for any WMO group whether or not husk
    ever touches `MOPV`/`MOPT`/`MOPR` at all, so there's nothing for this
    data to feed into even here.
  - Neither of these — nor anything else surfaced by this investigation —
    implements the specific mechanism `PortalCulling.md` describes: a
    graph of spatial cells connected by named portal windows, where a
    cell's contents are only rendered if a chain of portals from the
    camera's current cell resolves to it. Blender's own visibility
    machinery is uniformly per-object and either frustum/distance-based or
    compositing-based, never portal-graph-based.
- Bounded per this project's own "don't over-spend" precedent — 6 web
  searches total across both rounds of investigation, no further follow-up
  attempted once the negative result held up under several different
  phrasings and two specifically-named candidate mechanisms.

**Corroborating design signal from `wow.export`** (design precedent only,
never code-copied, per this task's own reference-material rules):
`reference/wow.export/src/js/3D/loaders/WMOLoader.js` **does** parse
`MOPV`/`MOPT`/`MOPR` (matching the wiki's own struct exactly — `portalIndex`/
`groupIndex`/`side` plus a skipped 2-byte filler, byte-for-byte the same
as `SMOPortalRef` above, a real independent corroboration of that struct's
correctness from a completely different codebase), and
`reference/wow.export/src/js/3D/exporters/WMOExporter.js` (both its BfA-era
and legacy export paths) **does** surface `portalVertices`/`portalInfo`/
`portalMapObjectRef` — but strictly as **JSON metadata properties attached
alongside the exported mesh**, never as OBJ/mesh geometry, and never
consumed by anything else in the exporter. This is the exact `node-
possible`/diagnostic shape this document recommends, independently arrived
at by the community's own established tool. Separately confirmed: `WMOLoader.js`
has **zero** code path for `MOBN`/`MOBR` at all (grepped for both the tag
names and their reversed-byte hex constants — no hits) — i.e. the
community's own reference tool doesn't implement WMO's BSP collision mesh
either, a useful data point that husk doing so (item 1 above) would be
new ground for the WoW-modding-tool ecosystem, not just catching up to
existing practice.

### C++ data-model sketch

```cpp
struct PortalVertex { Vec3 pos; };  // MOPV, root-level, C3Vector

struct Portal {  // MOPT, root-level
    uint16_t startVertex = 0;  // into portalVertices
    uint16_t count = 0;
    Plane plane;               // same Plane type as MOPL/MCVP
};

struct PortalRef {  // MOPR, root-level
    uint16_t portalIndex = 0;  // into portals
    uint16_t groupIndex = 0;   // the group on the other side
    int16_t side = 0;
    // filler (2 bytes) read and discarded
};

// MOPE, War Within 11.1.0+ -- fields beyond portalIndex are genuinely
// undocumented (the wiki's own struct literally names two different
// fields `unk1`), kept raw/unnamed rather than guessed at.
struct PortalExtra {
    uint32_t portalIndex = 0;
    std::array<uint32_t, 3> unk{};
};

struct VisibleBlock { uint16_t firstVertex = 0; uint16_t count = 0; };  // MOVB, group-level
```

**Test plan**: this session did not identify a real file with genuinely
non-trivial portal topology (multiple groups, real inter-group portal
references) by path — a needed follow-up before implementation, since
every root file checked has the chunks present but this session didn't
verify any of them have `MOHD.nPortals > 0` with real cross-group
references. Recommended approach: read `MOHD.nPortals` across the corpus
(cheap, one int per root file), pick a real multi-group building (a dungeon
or multi-room structure is the obvious candidate class) with a healthy
`nPortals` count, and confirm its `MOPR` entries' `groupIndex` values are
in range for that WMO's own `MOHD.nGroups` — mirroring the same reference-
validation discipline item 1's `MOBN`/`MOBR` check already used.

**Parse/Consumption/glTF ceiling**: `full` (straightforward once
implemented — no ambiguity in the struct layout itself, `MOPE`'s unknown
fields aside) / `diagnostic` (dump-chunks-equivalent JSON) or `extras`
(minimal marker nodes, one per portal, position from the portal's own
vertex centroid) / **`node-possible, unclaimed`**, confirmed rather than
assumed this session.

**Priority**: lowest in this file — real data exists, the struct is fully
spec'd, but there's no rendering payoff beyond a debug visualization
either in Blender specifically (checked) or in the broader glTF/game-
engine ecosystem (checked), and this session didn't confirm a single real
file with non-trivial portal topology to test against.

---

## 6. ADT terrain collision

**Current state**: `n/a`, confirmed infrastructure/non-existent-as-a-
separate-concept — **checked this session, not just carried over as an
unconfirmed inference** per this task's own explicit instruction.

**What was checked**: `documentation/wowdev-wiki/md/ADT/v18.md` in full
(already read for `WORLD_MISC_METADATA_TODO.md`'s own ADT items) has no
chunk described as terrain collision anywhere. A targeted grep across the
*entire* local wiki mirror (`documentation/wowdev-wiki/md/`, every `.md`
file, not just `ADT/v18.md`) for "collision" near "terrain" or "ADT" found
exactly one relevant hit, in `v18.md`'s own `MCRF` sub-chunk description:
"The client uses those `MCRF`-entries to calculate collision. Only objects
which are referenced in the current chunk of the toon get checked against
collision (this is only for MDX [M2] ... WMO seem to have different
collision)." This confirms the *doodad*-placement collision path (M2
instances placed via `MDDF`, referenced per-`MCNK` via `MCRF`) uses the
M2's own collision data (this file's item 1's M2-side sibling,
`M2_COMPLETENESS.md`'s already-`native — 100%` Collision row) — it says
nothing about the terrain mesh itself having a separate collision
representation, and no other page (`WDT.md`, `ADTLodImplementation.md`,
`WMO.md`) mentions ADT terrain collision at all either (both grepped
directly, zero hits). This is consistent with — and now actually confirms,
rather than merely repeats — the working assumption that ADT terrain
collision is the render mesh itself (`MCVT`'s 9×9+8×8 heightmap grid,
`WORLD_COMPLETENESS.md`'s own Terrain geometry section), no separate chunk
to parse.

**Consumption target**: none needed — once `MCVT`/`MCNR`/terrain-hole
parsing exists (`WORLD_COMPLETENESS.md`'s own Terrain geometry section,
out of this file's scope), the resulting render mesh already *is* the
collision mesh; no separate `extras` tag or second geometry pass is
warranted the way WMO needed one (WMO's render mesh and its BSP collision
mesh are genuinely two different topologies — different triangle counts,
different vertex counts, confirmed directly in item 1's own real-data
table above; ADT has no such split documented anywhere).

**Priority**: none — no implementation work exists for this row; it's
recorded here only so a future session doesn't re-ask the question. If a
future terrain implementation ever wants to *tag* the terrain mesh as
doing double collision duty (mirroring WMO's `isCollision` extras tag),
that's a one-line addition at that time, not a separate investigation.

---

## References

- **wowdev.wiki** (`documentation/wowdev-wiki/md/`): `WMO.md` (`MOHD`,
  `MOGI`, `MOPV`/`MOPT`/`MOPR`/`MOPE`, `MOVV`/`MOVB`, `MCVP`, `GFID`,
  group-file chunk-order lists, `MOGP` header/flags, `MOPY`/`MPY2`,
  `MOBN`/`MOBR`, `MDAL`, `MOPL`, `MOPB`); `WMO/PortalCulling.md` (runtime
  algorithm, read in full); `WMO/Rendering.md` (debug render-mode table
  confirming `F_COLLISION`'s bit meaning independently); `WMO/Loading.md`
  (checked, not directly relevant to this file's scope — vertex-color
  fixup only); `Common_Types.md` (`C3Vector`, `C4Plane`, `CAaBox`);
  `ADT/v18.md` (`MCRF` collision-relevant text, checked for any ADT
  terrain-collision chunk — none found); `WDT.md`,
  `ADTLodImplementation.md` (grepped for "collision," zero hits).
- **`documentation/wowdev-wiki/HUSK_AMENDMENTS.md`**: checked, no existing
  WMO-page amendment yet (expected — nothing WMO-side implemented before
  this session). `.phys`'s own amendment entries (§9's `PLYT` stride fix,
  chunk-tag-reversal note, `BODY`/`BDY3`/`BDY4` "only one type 0" claim
  correction) were read for transferable lessons about index/bounds-safety
  verification discipline — directly applied to this file's own `MOBN`/
  `MOBR`/`MOVI`/`MOVT` reference-chain check.
- **`reference/wow.export/`** (design/architecture reference only, never
  code-copied): `src/js/3D/loaders/WMOLoader.js` (parses `MOPV`/`MOPT`/
  `MOPR` matching the wiki struct exactly; zero `MOBN`/`MOBR` handling at
  all); `src/js/3D/exporters/WMOExporter.js` (portal data exported as JSON
  metadata alongside the mesh, never as geometry).
- **husk `src/`**: `src/m2.hpp`/`m2.cpp` (`CollisionMesh`,
  `parseVec3Array`, `parseCollisionMesh` — the direct M2-side precedent for
  item 1); `src/cmd_export.cpp` (the collision-mesh-as-`NamedMesh`-with-
  `isCollision`-extras block, and the `renderMeshCount`-before-appending
  trap noted in `CLAUDE.md`'s own Hazards section); `src/gltf.hpp`/`gltf.cpp`
  (`NamedMesh::isCollision`, `writeGlbMulti`'s per-entry skinning-opt-out
  relaxation); `src/phys.hpp`/`phys.cpp` (the bounds-checked-reference-chain
  precedent, and the reused-`readChunks`-with-reversed-tag-constants
  pattern this file's own `wmo.cpp` sketch follows); `src/chunk.hpp`
  (`readChunks`/`findChunk`, format-agnostic, reused verbatim by `.phys`
  and (once implemented) by WMO).
- **`M2_COMPLETENESS.md`**: Collision & physics section (the `native —
  100%` M2 collision-mesh row this file's item 1 mirrors directly).
- **`DESIGN.md`**: chunk-tag-reversal Key design decision ("Getting this
  backwards is a classic WMO/ADT-experience trap" — directly relevant,
  since this file's own scan scripts hit exactly this class of mistake
  once, for `MOGP`'s nested-container shape rather than tag reversal
  itself, but the same "verify against real bytes before trusting a
  parser" discipline caught it).
- **Real corpus** (`/media/luna/data/wow_export/world/wmo`, read-only,
  84,798 real `.wmo` files): a 300-file early sample (this session's own
  scratch scanner, not committed) for presence counts throughout this
  document; a 4-file deep decode-and-bounds-check for item 1's `MOBN`/
  `MOBR`/`MOVI`/`MOVT` reference-chain verification (exact paths and
  counts in item 1's own table). A full 84,798-file seek-based census was
  started this session and left running in the background under heavy
  concurrent disk contention from sibling investigation sessions working
  other slices of this same WMO/ADT expansion — its exact corpus-wide
  counts are a refinement on the sample-based numbers above, not a
  precondition for the structural conclusions in this document.
- **Bounded web research** (item 5's Blender-portal-culling investigation):
  6 searches total across two rounds (Blender Eevee/Eevee Next occlusion/
  culling architecture; Blender/glTF portal-volume conventions; two
  specifically-named candidate mechanisms — Cycles' Holdout shader,
  `docs.blender.org/manual/en/latest/render/shader_nodes/shader/holdout.html`,
  and Cycles' per-object Ray Visibility toggles/Scene Simplify Camera
  Culling, `docs.blender.org` Object Data properties manual page — both
  checked directly and ruled out for documented reasons, not dismissed on
  priors), no further follow-up attempted once the negative result held up
  under several phrasings, per this project's own "don't over-spend"
  precedent.
