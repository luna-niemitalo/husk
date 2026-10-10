# TODO: WMO collision/BSP, convex volumes, terrain-cutting planes, portal culling; ADT terrain collision

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed (see `../INVESTIGATIONS_TODO.md`'s own convention) —
git history is the record of what was fixed and when, not this file.

Scope: `../../WORLD_COMPLETENESS.md`'s "Collision, culling & visibility" section
(WMO `MOBN`/`MOBR`, `MCVP`, `MOPL`, `MOPV`/`MOPT`/`MOPR`/`MOPE`/`MOVV`/`MOVB`,
and the ADT terrain-collision row), plus the collision-relevant half of
`MOPY`/`MPY2` (the materials half of that same dual-purpose chunk belongs to
the sibling `WMO_GEOMETRY_TODO.md` — see that item's own section below for
the exact split, so nobody duplicates the other's work). This file is a
companion to `../../WORLD_COMPLETENESS.md`, one level deeper for this specific
slice — same relationship `M2_GAPS_TODO.md`/`PHYS_TODO.md`/etc. had to
`../../M2_COMPLETENESS.md` before each was implemented and deleted in turn.

Nothing in `src/` reads a WMO or ADT byte yet (`../../DESIGN.md`'s Non-goals:
"tracked, not started"). Every claim below was checked this session against
real bytes from the local corpus (`/media/luna/data/wow_export/world/wmo`,
84,798 real `.wmo` files) — either a full corpus tag-presence census or a
targeted decode-and-bounds-check against specific real files, same
discipline `../../WIKI_FINDINGS.md` throughout (particularly §9, `.phys`) already
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

- M2's collision mesh (`../../M2_COMPLETENESS.md`'s Collision row, `native — 100%`
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
  its target array — `../../WIKI_FINDINGS.md` §9 found zero such violations
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
  `../../CLAUDE.md`'s own Hazards note on this exact trap), prints a one-line
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

## 1. WMO collision mesh (`MOBN`/`MOBR`) and 2. per-face collision flags (`MOPY`/`MPY2`)

**Current state (2026-10-10)**: every WMO face's `MOPY`/`MPY2` flags and
material are mesh attribute channels (`wmo.poly_flags`, `wmo.poly_material`;
schema in `src/writers/bundle_writer.hpp`), so a consumer selects collidable
faces by flag (WMO.md `isCollidable`: 0x08, or 0x20 without 0x04).
Collision-only faces are in the mesh's `indices`, outside every primitive.
M2 collision is `resources.collision` (`canon::CollisionMesh`, with face
normals). 374 sampled WMO roots (41.3M faces, 8.0M collidable by the rule)
and 293 sampled M2s export cleanly.

The wiki's "two representations that sometimes disagree" doesn't apply
to real data: in all 2,628 sampled BSP groups, `MOBR` indexes every
triangle of the group, so the BSP is a spatial index and the face flags
alone select collision (`../../WIKI_FINDINGS/WORLD.md` "Collision &
culling"). The BSP tree itself is not exported; a consumer builds its own
acceleration structure.

Open:
- **`MPY2` flag 0x100 on collision-only faces** (146,103 faces in the
  1,000-root sample, material 0xFFFF, no 0x08): undocumented, so
  `isCollidable` leaves them out. If the client collides with them, the
  rule needs a bit for it.
- **No glTF projection**: the legacy `.glb` path still exports only M2
  collision, and drops the face channels.

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
`../../WORLD_COMPLETENESS.md` already states — a cutting plane has no direct
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
geometry) — `../../WORLD_COMPLETENESS.md`'s own `native-possible, not done`
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

**Current state (2026-10-10)**: portals are `mesh.portals` (`canon::Portal`:
polygon, plane, raw `MOPE`) and each group's `MOPR` range is its part's
`portals` links (`canon::PortalLink`: portal, other part, raw side); schema
in `src/writers/bundle_writer.hpp`. 200 sampled portal-bearing roots export
cleanly (1,539 portals). Facts: `../../WIKI_FINDINGS/WORLD.md` "Collision &
culling".

Open:
- **`side` semantics**: matches the owning group's half-space at its
  vertex centroid in 86% of links; the wiki's culling pseudocode reads the
  other way. Needs a client-side check before anything relies on it.
- **`MOPE`**: kept raw; its contents look like uninitialized memory.
- **`MOVV`/`MOVB`**: 0/71,929 real group files; not parsed.

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
`../../M2_COMPLETENESS.md`'s already-`native — 100%` Collision row) — it says
nothing about the terrain mesh itself having a separate collision
representation, and no other page (`WDT.md`, `ADTLodImplementation.md`,
`WMO.md`) mentions ADT terrain collision at all either (both grepped
directly, zero hits). This is consistent with — and now actually confirms,
rather than merely repeats — the working assumption that ADT terrain
collision is the render mesh itself (`MCVT`'s 9×9+8×8 heightmap grid,
`../../WORLD_COMPLETENESS.md`'s own Terrain geometry section), no separate chunk
to parse.

**Consumption target**: none needed — once `MCVT`/`MCNR`/terrain-hole
parsing exists (`../../WORLD_COMPLETENESS.md`'s own Terrain geometry section,
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
  trap noted in `../../CLAUDE.md`'s own Hazards section); `src/gltf.hpp`/`gltf.cpp`
  (`NamedMesh::isCollision`, `writeGlbMulti`'s per-entry skinning-opt-out
  relaxation); `src/phys.hpp`/`phys.cpp` (the bounds-checked-reference-chain
  precedent, and the reused-`readChunks`-with-reversed-tag-constants
  pattern this file's own `wmo.cpp` sketch follows); `src/chunk.hpp`
  (`readChunks`/`findChunk`, format-agnostic, reused verbatim by `.phys`
  and (once implemented) by WMO).
- **`../../M2_COMPLETENESS.md`**: Collision & physics section (the `native —
  100%` M2 collision-mesh row this file's item 1 mirrors directly).
- **`../../DESIGN.md`**: chunk-tag-reversal Key design decision ("Getting this
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
