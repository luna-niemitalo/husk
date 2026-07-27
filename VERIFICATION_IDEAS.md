# Verification ideas: source M2 counts vs. exported glb vs. Blender readback

Exploratory, read-only pass. Prompted by adding `collision_box`/
`collision_sphere_radius` to `husk info` and the
question: can husk info's own numbers become a third leg of
`tests/test_conformance.cpp`'s existing two-implementation cross-check
(tinygltf vs. Blender), rather than just a source of eyeballed sanity
checks? No code changed for this pass — this is a survey of what's
already exact, what's exact-with-a-known-scale-factor, and what doesn't
exist yet.

**Status: cases 1, 2, 3, and 5 are now implemented** (case 4 is still
deliberately skipped, see its own section below) — `tests/test_conformance.cpp`,
in triviality order. Case 3's original "tight tolerance" premise turned out
to be wrong once actually checked against real data; see that section for
the corrected version that shipped instead. Case 5 additionally required a
small, deliberate relaxation to `gltf::writeGlbMulti`'s skinning rule (a
NamedMesh entry can now opt out of a shared skeleton's skin, not just
match-or-nothing) and a real Blender-importer contamination fix in
`tests/blender_import_check.py` (see case 1/2's section) — both turned up
directly by trying to make these checks exact rather than loose.

## What test_conformance.cpp already checks

`bone_count`, `action_count`, `mesh_object_count`, `total_vertex_count`
are cross-checked between tinygltf's own reading of the .glb and
Blender's headless importer (`blender_import_check.py`) — agreement
between two independent glTF implementations, not against the M2 source.
The M2 header's own record counts (`husk info`'s numbers) never enter
that comparison today. That's the gap this file explores.

## Case 1: vertex count — exact match, decidable today [IMPLEMENTED]

`cmd_export.cpp`'s `baseMesh` is built once from the *entire* M2 header
vertex array (`m2::parseVertices(blob, header.vertices)` —
`cmd_export.cpp:1146`) and reused unsliced by every LOD tier
(`cmd_export.cpp:1322-1326`, "reuses baseMesh's shared
positions/normals/... as-is"). A `.skin`'s submesh/batch indices only
select a *subset* for drawing — they never slice the accessor itself.

Consequence: one exported glTF mesh's vertex count is **exactly**
`header.vertices.count`, and Blender's `total_vertex_count` is exactly
`header.vertices.count × (number of LOD tiers exported)` — 1× for a
normal export, N× for `--lod all`. Confirmed against real data:
`bloodelffemale.m2` reports `vertices: 8061` from `husk info`; a
single-LOD export should read back as exactly 8061 in Blender (not
verified live this pass — see Recommendation below — but the code path
guarantees it, this isn't a fuzzy claim).

This is the strongest candidate: no new parsing, no new export work,
just a new assertion in `test_conformance.cpp` comparing
`m2::parseHeader(...).vertices.count * skinsToExport.size()` against
`parseProbeInt(..., "total_vertex_count")`.

**Implemented as described**, plus one real, unanticipated fix this
pass turned up: `blender_import_check.py`'s `total_vertex_count`/
`mesh_object_count` probes were silently contaminated by two Blender-
importer-side sources neither this file nor the original `> 0` assertions
accounted for — Blender's own `--factory-startup` scene (a default Cube +
Camera + Light, 8 extra vertices/1 extra mesh object) survives into the
probe unless explicitly cleared first, and `bpy.ops.import_scene.gltf`'s
own `armature_display()` (`io_scene_gltf2/blender/imp/node.py`) creates a
real 42-vertex Icosphere mesh object per armature import as a bone
custom-shape widget (parked in a hidden "not for export" collection, but
still a real object in `bpy.data.objects`) unless `disable_bone_shape=True`
is passed. Both fixed in `blender_import_check.py` before this exact-match
assertion could actually hold — found the hard way, by writing the exact
check and getting `8111 == 8061` (`50 = 8 + 42`) instead of the expected
match. See `tests/test_conformance.cpp`'s Blender `TEST_CASE`.

## Case 2: bone count — exact match, decidable today [IMPLEMENTED]

`header.bones.count` (or the `.skel`-sourced count when bones are
external) maps 1:1 to glTF skin joints, already indirectly confirmed by
the existing `bone_count == model.skins[0].joints.size()` tinygltf-vs-
Blender check. Adding the M2/.skel source count as a third term is a
one-line addition, not a new capability — `test_conformance.cpp` already
has `testM2()`/would need `m2::parseHeader` called once more locally.

**Implemented as described** — one `CHECK` in the same `TEST_CASE` as
case 1, no surprises (the test fixture's bones are inline, not
`.skel`-sourced, so `header.bones.count` was the right field directly).

## Case 3: bounding box — needs new export work, tolerance match [IMPLEMENTED, corrected]

`header.boundingBox`/`boundingSphereRadius` (the *render* bounds, not
collision) are parsed but never written to the glb in any form —
`cmd_export.cpp` never references `header.boundingBox`. tinygltf/Blender
both compute their own bounds from the actual accessor min/max, so a
verification here wouldn't need husk to export anything new — it would
compare **two independently-computed quantities**: the M2 header's
stated bounding box (author-authoritative, baked in by Blizzard's own
exporter) vs. the bounding box of the vertex data husk actually parsed
and wrote out. That's a genuine end-to-end correctness signal — a
mismatch would mean husk's vertex parsing (offset math, coordinate
conversion) is subtly wrong even though counts and topology look fine.

Not an exact match, though: needs (a) the WoW Z-up → glTF Y-up axis
remap husk already applies (`toGltf`, `m2.hpp`'s doc comment) applied to
the header's box corners before comparing, and (b) float tolerance,
since the header box is presumably computed by Blizzard's exporter from
the same float vertex data with its own rounding. A `doctest::Approx`
comparison per axis after remapping would do it. Worth doing, but it's
the first case here that needs new test code beyond "read one more
number and compare."

**Implemented, but not as a tolerance-match equality — that premise was
wrong, found by actually computing both sides against real data before
writing the assertion.** `bloodelffemale.m2`'s real bind-pose vertex
bounds (computed directly from the raw M2 vertex array) are roughly *half*
the header's declared `bounding_box` on every axis (e.g. render z:
0 to 1.99 vs. header z: -0.10 to 2.31); `bloodelffemale_hd.m2` is more
extreme — bind-pose z spans ~2.1 units, the header box ~9.6. Not a
parsing bug (double-checked with a from-scratch byte-level read,
independent of husk's own code): the header box is almost certainly sized
for the model's *full animated range* (weapon swings, spread limbs), not
a tight fit around the rest pose alone — no wowdev.wiki text says so
explicitly, but the magnitude and direction of the gap point that way
consistently across both fixtures. So the real, decidable, tolerance-free
invariant that *does* hold (checked both fixtures by hand before
committing to it): **the bind-pose mesh's own AABB is always fully
contained inside the header's declared box, per axis, after the same
Z-up→Y-up remap** — still a genuine correctness signal (a vertex-parsing
bug — wrong offset, wrong scale — could push the computed bounds outside
the header's box), just containment instead of equality. Verified the
test itself actually catches a real regression, not just passing
vacuously: temporarily perturbed `cmd_export.cpp`'s `toGltf` by +50 units
on X, confirmed the assertion fails with the exact expected numbers, then
reverted. See `WIKI_FINDINGS.md` §5 for the fuller writeup and
`tests/test_conformance.cpp`'s own `TEST_CASE` for the corrected
containment check (`transformedM2BoundingBox`, remapping all 8 corners
rather than just `min`/`max`, since the axis swap negates one component
and would otherwise silently produce an inside-out box on that axis).

## Case 4: sequences → animation clips — not a clean match by design

`husk info` reports `sequences: 339` for `bloodelffemale.m2`, but
CLAUDE.md's Resume history records only ~258 exported animation clips
for the same file. Not a bug: `cmd_export.cpp:253-261` documents that
alias sequences (flag 0x40 without 0x20) are skipped outright, and any
sequence whose data lives in an external `.anim` file that wasn't
supplied via `--anim-dir` is silently skipped too
(`M2AnimInputs`'s doc comment, `cmd_export.cpp:263-279`). Global
sequences add clips back on top (`buildGlobalSequenceAnimations`) that
don't correspond to any `M2Sequence` entry at all.

So `header.sequences.count` is only a loose upper bound on exported
clips, and the gap size depends entirely on which `.anim` files happen
to be present locally — not a property of the M2 file alone. A
verification here would need `husk export` to itself report a resolved-
vs-skipped breakdown (it currently only prints the final count,
`cmd_export.cpp:1456`/`1482`) before there's anything meaningful to
assert against. Lower priority: the number is expected to differ, and
quantifying *why* it differs would be new scope (an explicit
resolved/aliased/external-missing counter), not a bug fix.

## Case 5: collision data — the case that prompted this, and it doesn't work yet [IMPLEMENTED]

This is what `husk info`'s new `collision_box`/`collision_sphere_radius`/
`collision_positions`/`collision_indices`/`collision_face_normals` output
actually is today: `m2.hpp:124-133`'s own comment is explicit that these
are **only `Array` descriptors (count + offset)** — `parseHeader` never
dereferences them into real position/index/normal data the way
`parseVertices`/`parseBones` do for the render mesh. Nothing derived from
them reaches `cmd_export.cpp` at all; grepping the export path for
`collision` returns zero hits. So there is currently:

- no husk-side parsing of the actual collision triangle geometry,
- nothing written into the .glb representing it (not even as extras —
  contrast with texture transforms/geoset IDs, which *are* real parsed
  data surfaced as extras; collision data isn't parsed past the array
  descriptor at all),
- therefore nothing for Blender's importer to read back and compare.

A collision-data verification loop needs three new pieces, in order:

1. **Parse it.** A `m2::parseCollisionMesh`-shaped function mirroring
   `parseVertices`/`parseAttachments` — dereference
   `collisionPositions`/`collisionIndices`/`collisionFaceNormals` (all
   C3Vector/uint16, same primitive types husk already reads elsewhere)
   into real `std::vector`s.
2. **Export it.** Unlike geoset selection or multi-texture-layers (data
   with *no* unambiguous glTF translation — that's why those became
   inert extras, per this project's "tag it, don't guess at semantics"
   idiom), a collision mesh is just a plain triangle mesh — an
   unambiguous glTF translation exists. The natural shape: a second,
   unskinned `gltf::Mesh`/node (e.g. `"<name>_collision"`), tagged via
   node/mesh `extras` (e.g. `{"collision": true}`) so a renderer or
   Blender script knows not to draw it — the tag is about *purpose*, not
   about an untranslatable *shape*, which is a different justification
   than the existing extras uses but the same mechanism.
3. **Read it back.** Extend `blender_import_check.py` with a
   `collision_mesh_vertex_count`/`collision_mesh_triangle_count` probe
   (find the tagged object by its extras or name convention), then add
   the `test_conformance.cpp` assertion.

Real numbers already on hand from this session's `collision_indices`/
`collision_positions`/`collision_face_normals` work make this cheap to
verify exactly once built: `bloodelffemale.m2` has 8 collision positions,
36 collision indices (12 triangles), 12 face normals (one per triangle,
consistent) — small enough that an exact match, no tolerance needed, is
realistic (unlike Case 3's render bounding box, there's no independent
Blizzard-computed "expected" scalar to reconcile against float rounding
— it's pure count/topology).

**Implemented as all three pieces above, plus one real design change this
turned up along the way.** `m2::parseVec3Array`/`m2::parseCollisionMesh`
(`src/m2.hpp`/`m2.cpp`) dereference the three Array descriptors into real
`std::vector`s, unit-tested in `tests/test_m2.cpp`. `cmd_export.cpp`
builds one more `gltf::NamedMesh` (positions remapped via the existing
`toGltf`; per-vertex normals *approximated* by averaging each vertex's
adjacent face normals, since the source data is one normal per triangle,
not per vertex — a collision mesh isn't shaded in practice, this is only
to satisfy `gltf::Mesh`'s own positions/normals/texCoords-same-length
invariant with real, finite, unit-length data), tagged `isCollision` —
new `gltf::NamedMesh::isCollision` field, which `writeGlbMulti` turns into
a `{"collision": true}` node `extras` key. The one real design change:
`writeGlbMulti` previously required *every* NamedMesh entry to be skinned
whenever any shared skeleton was present (`hasSkeleton && mesh.skinning
.size() != n` → `Error`, unconditionally) — too strict for an unskinned
collision mesh alongside a skinned render mesh sharing one armature. Now
each entry independently opts in (non-empty, matching-length `skinning`)
or out (empty `skinning`, no glTF `skin` reference on that node at all,
not deformed by the armature) — see `gltf.hpp`'s updated `writeGlbMulti`/
`NamedMesh` doc comments and the two `tests/test_gltf.cpp` cases proving
both the new positive case (mixed skinned/unskinned entries succeed) and
that the real error case (skinning present but the wrong length) still
throws. `blender_import_check.py` gained `collision_mesh_count`/
`collision_mesh_vertex_count`/`collision_mesh_triangle_count` probes
(found by the `collision` extras tag, not by name), checked in
`tests/test_conformance.cpp` against `header.collisionPositions.count`/
`header.collisionIndices.count / 3` exactly. Also fixed, since the
collision mesh's presence changes real export output: cmd_export.cpp's
own "N LOD tier(s)" summary print used to key off `namedMeshes.size()`,
which now over-counts by one whenever a collision mesh is attached —
tracked separately as `renderMeshCount`, captured before the collision
entry is appended.

## Recommendation (historical — see `[IMPLEMENTED]` tags above)

Ranked by (signal value) / (effort), as originally scoped:

1. **Case 1 (vertex count)** and **Case 2 (bone count)** — pure test
   additions, zero export-side changes, exact match, high confidence.
   Cheapest, do these first if any of this gets built.
2. **Case 3 (bounding box)** — genuine new correctness signal (catches
   vertex-parsing bugs that counts alone can't), moderate effort (axis
   remap + tolerance), no export changes needed.
3. **Case 5 (collision mesh)** — real, currently-nonexistent capability;
   most effort (parse + export + probe-script + test, four new pieces),
   but the payoff is a whole data class going from "counted but inert"
   to "verified end-to-end," matching the project's existing bar for
   every other real data class already exported.
4. **Case 4 (sequences)** — skip for now; the metric itself needs to
   change shape (resolved/skipped breakdown) before a comparison would
   mean anything, and the gap is expected/by-design, not a suspected bug.

Cases 1, 2, 3, and 5 are now implemented, in exactly this triviality
order; case 4 remains deliberately unimplemented for the reason stated.
