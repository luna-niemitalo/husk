# GEOSET_MASK_TODO.md

Plan for making WoW's mutually-exclusive geoset variants (hair styles, boot
cuffs, eye-glow, ...) toggleable in Blender without a custom importer,
prompted by a real interactive question: "Blender has a Mask modifier that
hides mesh parts by vertex group -- could each geoset become a vertex
group, switched via Mask?"

## Background

Every primitive `husk export` writes already carries `geoset_id`/
`geoset_group`/`geoset_variant` in its glTF `extras` (`M2_GAPS_TODO.md`'s
now-closed geoset-extras work), but husk exports every geoset unfiltered --
mutually exclusive alternates (e.g. 5+ hairstyle variants) all render
stacked on top of each other. There's no DBC/DB2 customization data husk
has access to that would let it pick the "right" one, by design
(`DESIGN.md`'s Non-goals) -- so the fix isn't picking, it's making the
alternates toggleable by a human in Blender instead.

## The core idea, verified empirically this session

Blender's Mask modifier hides everything *not* in a chosen vertex group (or
its invert: hides only that group). Vertex groups are exactly the tool for
this -- but glTF's core spec has no "vertex group" concept, only skinning
(`JOINTS_0`/`WEIGHTS_0`, up to 4 bone influences/vertex).

The trick: **glTF supports additional joint/weight sets** (`JOINTS_1`/
`WEIGHTS_1`, standard core spec, not an extension) for models needing more
than 4 real bone influences. Nothing requires those extra "joints" to be
real bones -- they can be inert placeholder nodes that are never posed or
animated, used purely as vertex-group labels. Blender's *stock* importer
already creates one real vertex group per skin joint automatically as part
of normal skin-weight import -- so this needs **zero custom Blender
tooling**, unlike every alternative considered (a custom `.glb` parser/
importer bypassing Blender's own mesh-building, which was the first,
rejected approach -- see below for why).

Verified two ways, both empirically, not just reasoned about:

1. **Mechanical**: built a synthetic glTF with a real bone (`BoneA`,
   `JOINTS_0`/`WEIGHTS_0`) plus a fake tag joint (`TagGeoset0`,
   `JOINTS_1`/`WEIGHTS_1`, weight 1.0, never posed). Blender's stock
   importer created a real `TagGeoset0` vertex group with correct
   per-vertex weights, no script involved.
2. **Correctness**: the obvious worry -- glTF expects total weight across
   *all* joint sets to sum to 1.0 per vertex, and stacking a 1.0 tag weight
   on top of a 1.0 real-bone weight sums to 2.0. Posed the real bone and
   compared the actual deformed vertex position via `evaluated_get`'s
   depsgraph: moved by exactly the pose delta, not doubled. Blender's
   Armature modifier normalizes total influence weight across all sets
   automatically, so an un-animated tag joint contributes nothing to
   deformation as long as it's never posed.

**Rejected approach**: a custom Blender-side `.glb` parser that builds the
mesh manually via `bmesh`, bypassing Blender's stock importer entirely
(needed because the stock importer does NOT preserve a 1:1 accessor-index
<-> Blender-vertex-index correspondence -- confirmed empirically: a real
195,498-entry POSITION accessor became a 32,939-vertex Blender mesh, and a
position lookup at the same index didn't match). The `JOINTS_1`/
`WEIGHTS_1` trick sidesteps this entirely by riding on Blender's already-
battle-tested skin-weight import path, which *is* index-safe by
construction (it's the same mechanism real bone skinning already depends
on and this project has already verified byte-for-byte, `EYES_ON_FINDINGS.md`
#2).

## What does NOT get automated

Creating the actual Mask modifier stack in Blender has no glTF equivalent
at all -- modifiers are a pure Blender-document concept, no chunk/extension/
extra means "add a modifier." So unlike the vertex groups (free via
skinning import), a small companion Blender Python script is still needed,
run after import, to:

1. Walk the imported object's vertex groups matching a `geoset_<id>` naming
   convention.
2. Group them by `geoset_group` (the alternates-of-each-other category --
   already known per-primitive from existing extras, just needs surfacing
   per tag joint too, see below).
3. Pick a default visible variant per group (lowest `geoset_id` -- husk
   doesn't currently resolve which one a real character has actually
   equipped; that's real, locally-extracted DB2 customization data,
   `DESIGN.md`'s Non-goals clarified wording + `CHAR_TEXTURE_
   COMPOSITING_TODO.md`, just not implemented yet, same limitation already
   documented elsewhere in this project).
4. Add one Mask modifier per non-default variant, invert mode ("hide this
   vertex group, show the rest"), stacked -- confirmed directly as the
   desired mechanism (not e.g. one modifier per category trying to
   select the active variant, which doesn't compose the way Mask actually
   works: it always hides everything outside its one target group, which
   would also hide the always-visible base body and every unrelated
   geoset category).

## Known tradeoff in the exported .glb -- neutralized on the Blender side

Adding one tag joint per distinct geoset ID pushes the exported skin's
total joint count well past real-time-engine GPU skinning limits (commonly
~256-joint palettes). `bloodelffemale_hd.m2`: 245 real bones + 114 geoset
tags = 359 joints. Acceptable for husk's actual target (Blender import/
editing, `DESIGN.md`'s Goal) -- Blender itself has no such limit -- but this
`.glb` file, as husk writes it, would still need the tag joints stripped
before use in an engine that assumes a real-time skinning budget.

**Resolved for the Blender-editing path specifically, verified
empirically**: Blender stores vertex groups on the *mesh* datablock, keyed
by name -- independent of the armature's actual bones. Confirmed directly:
importing a synthetic tag-joint glTF, then deleting the fake bone from the
armature in Edit Mode (`edit_bones.remove`), leaves the mesh's vertex group
(name, per-vertex weights) completely untouched -- the Armature modifier
just stops finding a bone to deform with, but nothing about the group
itself changes. A Mask modifier targeting that group by name still works
identically post-deletion (verified: correctly hid/showed vertices before
and after). Re-exporting afterward also succeeds, no dangling-reference
issue.

So the companion script's real sequence (see below) ends with deleting
every fake tag bone once the Mask modifier stack is built from its vertex
group -- the *exported*, husk-produced `.glb` still carries the inflated
joint count (that's still real and worth knowing if that raw file is fed
anywhere other than this Blender workflow), but the **finalized Blender
scene** a human actually works in and re-exports from is back to the real
bone count, with the vertex groups/Mask modifiers fully intact. Not a
theoretical mitigation -- tested end to end (import -> mask -> delete bone
-> re-export, no errors).

## Alternative considered and ruled out: a second, separate armature

Raised directly: instead of appending tag joints to the *same* skin as the
real bones, put them in a genuinely separate second armature/skin, bound
to the same mesh, so the real skin is never touched at all (not even
appended-to).

Checked the two things this hinges on, both empirically:

1. **Does Blender share vertex-group data across two Objects pointing at
   the same underlying mesh?** Yes, confirmed -- vertex groups live on the
   Mesh *datablock*, not the Object; a linked duplicate (`Object.duplicate
   (linked=True)`, same `mesh_obj.data` on both) shows a group added via
   either object immediately on both.
2. **But can two glTF nodes actually share one `mesh` entry while carrying
   *different* `JOINTS_0`/`WEIGHTS_0` data (real bone weights vs. tag
   weights)?** No -- `JOINTS_0`/`WEIGHTS_0` accessor bindings live on
   `mesh.primitives[].attributes`, which belongs to the mesh entry itself,
   not the node. Two nodes referencing the *same* mesh index necessarily
   share the same skinning attributes. A second, differently-weighted
   armature requires a genuinely separate `mesh` entry (reusing the same
   POSITION/NORMAL/indices accessors underneath is fine and cheap -- no
   geometry duplication -- but it's still a distinct glTF mesh object).
   Blender's importer doesn't do cross-mesh accessor-sharing detection, so
   two distinct `mesh` entries become two independent Mesh datablocks in
   Blender, not the shared-datablock case (1) actually tested. Vertex
   groups built on the tag armature's mesh copy would need a manual
   transfer step (Data Transfer / weight copy) to reach the real, visible,
   animated mesh's datablock before a Mask modifier on it could see them.

Net: more moving parts (an extra mesh entry on the export side, an extra
vertex-group-transfer step in the companion script) for no benefit over
appending to the existing skin -- especially now that the single-skin
approach's own downside (fake bones sitting in the real armature) is fully
resolved by deleting them post-import (previous section). Not pursued.

## Implementation stages

1. **`gltf_skeleton.hpp`'s `Skeleton`**: new field, e.g.
   `std::vector<GeosetTag> geosetTags` (one entry per distinct
   `skinSectionId`/`geoset_id` seen across a model's primitives, populated
   by `cmd_export.cpp`, which already collects every batch's `skinSectionId`
   for the existing extras work). Each becomes one inert placeholder node,
   identity transform, never posed/animated, named `geoset_<id>` --
   **appended to `skin.joints` after every real bone** (critical: must
   never disturb existing real-joint indices 0..N-1, the same invariant
   `bone_correction_sets`/emitter-anchor/animation joint indices already
   depend on) with its own identity inverse-bind-matrix entry. This is a
   deliberate, docstring-flagged *exception* to the existing
   "attachment/event/light nodes are never added to `skin.joints`"
   invariant sitting right next to it in `gltf.hpp`'s doc comment --
   needs to be unambiguous about which anchor-node family this new one
   belongs to, so a future reader doesn't assume the existing rule still
   covers it.
2. **`emitSkeletonAndSkin`** (`gltf_skeleton.cpp`): emit the tag nodes,
   extend `skin.joints`/inverse-bind-matrices, return a
   `geosetId -> skin.joints-relative index` map for mesh emission to
   consume (the mesh side needs *skin-relative* joint indices, same
   convention `JOINTS_0` already uses -- not raw node indices).
3. **`Mesh`/`Primitive`** (`gltf_mesh.hpp`): no new per-vertex field needed
   on `Mesh` itself -- `Primitive::skinSectionId` already identifies which
   tag a primitive's vertices belong to. `emitMeshNode` computes, per
   vertex, the *set* of distinct tag joints referencing it (a vertex can be
   shared across primitive/geoset boundaries at a seam) and builds
   `JOINTS_1`/`WEIGHTS_1` accordingly -- weight split evenly across
   however many distinct tags touch that vertex (so `WEIGHTS_1` sums to
   1.0 per vertex, same convention as `WEIGHTS_0`), capped at 4 (glTF's
   per-set influence limit) with a documented "extremely unlikely, M2
   submesh splits don't produce 5-way vertex sharing in practice" note if
   a vertex exceeds it.
4. Only emitted when `hasSkeleton` -- a skinning attribute set requires a
   `node.skin` per glTF's own spec; every husk M2 export has a skeleton
   already (even a single-bone one), so this isn't expected to be a real
   limitation in practice.
5. Tests: extend `gltf_skeleton.cpp`/`gltf_mesh.cpp`'s existing synthetic
   test tiers (node-count/joint-offset assertions will need updating to
   account for the new tag-node range -- expected, not a sign of breakage)
   plus new dedicated tests (tag joint present, weight/joint values
   correct, real deform unaffected by a posed real bone -- same shape as
   this session's own synthetic verification).
6. Companion Blender script (separate deliverable, `tools/` or similar,
   exact location TBD): the Mask-modifier-stack builder described above.
   Not part of `husk export`'s C++ core -- consistent with this project's
   existing stance (`EYES_ON_FINDINGS.md` #6: "a Blender-import-script
   concern, not something `husk export` itself builds").

## Implemented, this session

Stages 1-5 (C++ core) are done: `Skeleton::GeosetTag`/`geosetTags`
(`gltf_skeleton.hpp`), tag-joint emission appended after every real bone
with correct parenting under whatever node is/would be the skin's closest
common root (`gltf_skeleton.cpp`'s `emitSkeletonAndSkin`), `JOINTS_1`/
`WEIGHTS_1` construction from `Primitive::skinSectionId` in `emitMeshNode`
(`gltf_mesh.cpp`), and `cmd_export.cpp` populating `geosetTags` from the
union of distinct `skinSectionId`s across every LOD tier already exported.

A real bug was caught by this project's own gltf-validator-backed test
suite before landing: giving a tagged vertex a full second 1.0-summing
`WEIGHTS_1` on top of its existing 1.0-summing `WEIGHTS_0` produces a
combined per-vertex total of 2.0, which gltf-validator correctly flags
(`ACCESSOR_WEIGHTS_NON_NORMALIZED`) even though Blender's own Armature
modifier was independently verified to renormalize at evaluation time
regardless of what's stored. Fixed by rescaling *both* sets down together
for any tagged vertex so the combined stored total is exactly 1.0 again --
a pure file-format correctness fix, provably a no-op on Blender's actual
rendering. Four existing conformance tests (real multi-root weapon, real
quadruped, real full character export, real weapon attachment test) needed
their hardcoded `skin.joints.size() == header.bones.count` assertions
updated to `== header.bones.count + <distinct geoset ID count, counted
independently from primitive extras>` -- expected, not a regression, per
this doc's own stage-5 prediction. Two new synthetic unit tests
(`tests/test_gltf_skeleton.cpp`) lock in the mechanism directly: tag-joint
naming/parenting/JOINTS_1/WEIGHTS_1 values including the rescale, and a
no-geosetTags case proving zero footprint when the feature isn't used.
Full suite green, 532/532.

Verified against the real `bloodelffemale_hd.m2` export (113 distinct
geoset IDs, 245 real bones -> 358 skin joints) with a standalone tinygltf-
linked scan tool (not committed, scratchpad only): every vertex's combined
`WEIGHTS_0` + `WEIGHTS_1` sum is exactly 1.0, confirming the rescale fix
holds at real scale, not just on small synthetic/test fixtures. That same
real export also surfaced 1.5M+ `gltf_validator` messages when run without
`--no-validate-resources` -- traced down to a **pre-existing, unrelated**
data property: 6,879 real vertices in this specific HD model have a
duplicate joint index within their own raw `JOINTS_0` slots (e.g.
`[30, 16, 16, 22]`, wowdev.wiki M2's own `boneIndices`, husk never
modifies these values, only copies them through), which
`gltf_validator` reports once per primitive referencing each such vertex
(114 primitives share the same vertex domain, hence the huge multiplied
count). Confirmed not caused by this session's work: a clean fixture
(`wolf.m2`, already covered by an existing "zero errors" conformance test,
`JOINTS_1`/`WEIGHTS_1` present and active) scans with zero bad sums *and*
zero duplicate joints via the same tool. Not investigated further or
fixed here -- out of scope for geoset masking, flagged for whoever
next touches raw M2 bone-index handling.

## Implemented, stage 6 (companion Blender script)

`tools/husk_blender_geoset_mask.py` -- run inside Blender (`blender
--python tools/husk_blender_geoset_mask.py -- model.glb`, or against an
already-imported scene). Walks every `geoset_<id>` vertex group, groups by
`geoset_group` (`id // 100`, matching husk's own extras convention), adds
one invert-mode Mask modifier per non-default variant (lowest ID kept
visible -- husk doesn't currently resolve real DB2 customization data to
pick an actual default, same disclaimed-placeholder precedent as
`orderCandidatesForDefault` elsewhere in this project), then deletes every
tag bone from the armature.

Verified end to end against the real `bloodelffemale_hd.m2` export (113
geoset IDs, 245 real bones): 358 armature bones before running the script,
245 after (tag bones fully removed); 90 Mask modifiers created, correctly
named/targeted/inverted; all 358 vertex groups still present on the mesh
after bone deletion (confirms the mesh-owned-data finding above holds at
real scale, not just the earlier synthetic test); the actual evaluated
(post-modifier) mesh drops from 32,939 raw vertices to 4,232 visible ones
-- masking is genuinely doing something, not a no-op. This closes out
every stage of this plan -- nothing left outstanding here.
