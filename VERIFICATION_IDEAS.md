# Verification ideas: source M2 counts vs. exported glb vs. Blender readback

Exploratory, read-only pass. Prompted by adding `collision_box`/
`collision_sphere_radius` to `husk info` and the
question: can husk info's own numbers become a third leg of
`tests/test_conformance.cpp`'s existing two-implementation cross-check
(tinygltf vs. Blender), rather than just a source of eyeballed sanity
checks? No code changed for this pass — this is a survey of what's
already exact, what's exact-with-a-known-scale-factor, and what doesn't
exist yet.

## What test_conformance.cpp already checks

`bone_count`, `action_count`, `mesh_object_count`, `total_vertex_count`
are cross-checked between tinygltf's own reading of the .glb and
Blender's headless importer (`blender_import_check.py`) — agreement
between two independent glTF implementations, not against the M2 source.
The M2 header's own record counts (`husk info`'s numbers) never enter
that comparison today. That's the gap this file explores.

## Case 1: vertex count — exact match, decidable today

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

## Case 2: bone count — exact match, decidable today

`header.bones.count` (or the `.skel`-sourced count when bones are
external) maps 1:1 to glTF skin joints, already indirectly confirmed by
the existing `bone_count == model.skins[0].joints.size()` tinygltf-vs-
Blender check. Adding the M2/.skel source count as a third term is a
one-line addition, not a new capability — `test_conformance.cpp` already
has `testM2()`/would need `m2::parseHeader` called once more locally.

## Case 3: bounding box — needs new export work, tolerance match

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

## Case 5: collision data — the case that prompted this, and it doesn't work yet

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

## Recommendation

Ranked by (signal value) / (effort):

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

None of this is implemented. This file is the survey; if you want any
of these built, say which case(s).
