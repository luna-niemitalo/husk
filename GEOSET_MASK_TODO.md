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

1. Walk the imported object's vertex groups matching a
   `group_<n>,variant_<n>` naming convention (comma-separated, prefix-
   tagged fields -- chosen specifically so a consumer, whether this script
   or a future geometry-nodes-based rewrite, can recover the raw integers
   with a plain comma-split + prefix-strip, no `id / 100` / `id % 100`
   math needed).
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
   identity transform, never posed/animated, named
   `group_<geosetId/100>,variant_<geosetId%100>` --
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

## Implemented, stage 6 (companion Blender script) -- superseded once, real geometry-nodes rewrite

The first version of `tools/husk_blender_geoset_mask.py` built a Mask-
modifier stack (one invert-mode modifier per non-default variant). Real
end-to-end verification against `bloodelffemale_hd.m2` (113 geoset IDs)
produced **90 Mask modifiers on one mesh** -- correct, but, prompted
directly: "an insane stack of mask modifiers" is a real usability problem
of its own, and Blender's Menu Switch geometry node (a real dropdown per
mutually-exclusive group, confirmed scriptable via `bpy` -- see below) is
the better mechanism. The Mask-modifier version was fully replaced, not
kept as a fallback mode, to avoid maintaining two mechanisms doing the
same job.

**Geometry-nodes API, confirmed empirically before writing the real
script** (direct fetches to `docs.blender.org` 403 regardless of page for
this session's fetch tool; web search still surfaced a real citation from
the Blender PR that implemented Menu Switch, `enum_definition.enum_items`,
corroborated by a synthetic scripted probe):

- `GeometryNodeMenuSwitch` starts with two placeholder items ("A"/"B")
  already present -- `menu.enum_definition.enum_items.clear()` before
  adding real ones, or they linger as two dead, unwired dropdown entries.
- Its `Menu` input socket only becomes a real dropdown in the Modifier
  panel if promoted to the node group's own interface as a
  `NodeSocketMenu` entry (`node_tree.interface.new_socket(...,
  socket_type='NodeSocketMenu')`) and linked from a `Group Input` output
  -- setting `default_value` directly on the internal node's own Menu
  input leaves it as an editor-only setting a human would have to open the
  node tree to change, not what "select via dropdown" asked for.
- That interface socket's own `default_value` can only be set *after*
  linking it to the node that defines its valid items -- setting it first
  throws `enum "..." not found in ()` (empirically confirmed: an unlinked
  Menu interface socket has no known items yet).
- The **exposed** modifier input (`mod[identifier]`) for a Menu socket is
  stored as a plain **integer index**, not the item's string name --
  confirmed by trying a string assignment first and getting a real
  `TypeError` (`Cannot assign a 'str' value to the existing '...' Int
  IDProperty`).
- Vertex groups are automatically readable inside a geometry-node graph as
  same-named point-domain `Float` attributes (`GeometryNodeInputNamedAttribute`,
  `data_type = 'FLOAT'`) -- no manual conversion step needed, this is
  exactly what makes reading husk's `group_<n>,variant_<n>` tag data
  inside the graph possible at all.

**Real graph shape built** (`build_geoset_switch_node_group`): a running
"remainder" geometry threaded through one `Separate Geometry` node per
variant (selection = that variant's own `Named Attribute` > 0), chained
via each node's own `Inverted` output -- not an OR-reduction across every
tag's attribute, which would need one `Boolean Math` node per variant
beyond the first just to combine them. Each group's own peeled-off variant
pieces feed one `Menu Switch` node (one dropdown per group); whatever
never gets peeled off (untagged geometry, and every single-variant group's
own geometry, deliberately left out of the separation chain -- nothing
mutually exclusive to switch) is the always-visible base, joined with
every group's active selection (`Join Geometry`) into the node group's
output. One Geometry Nodes modifier per mesh object, tag bones deleted
from the armature afterward exactly as the superseded version did (this
part didn't change -- vertex groups/attributes are unaffected by bone
deletion regardless of which mechanism reads them).

**Verified end to end against the real `bloodelffemale_hd.m2` export**
(113 geoset IDs, 245 real bones, 19 of 23 groups have 2+ variants and get
a real dropdown -- the other 4 have exactly one variant and stay in the
base remainder untouched): 358 armature bones before running the script,
245 after (tag bones fully removed, unchanged from before); node group
built with 349 real nodes and 19 real `NodeSocketMenu` interface entries,
one per multi-variant group. The default-state evaluated mesh (every
dropdown at its lowest variant) has **exactly 4,232 vertices** -- an exact
match to the superseded Mask-modifier version's own independently-verified
result across all 19 groups simultaneously, strong evidence the rewrite is
behaviorally equivalent, not just structurally plausible. Functional
correctness (not just the default) also confirmed directly: switching one
group's dropdown to a non-default variant changes the evaluated vertex
count (4,232 -> 4,131).

**Correction, prompted by real interactive use the same day**: the line
that used to sit here ("this closes out every stage of this plan --
nothing left outstanding") was wrong. All six implementation stages did
land and the vertex-count/switch-functionality checks above are real, but
they only checked *aggregate* counts, never *which* vertices moved --
real hands-on use in Blender's own GUI found two genuine remaining bugs
the aggregate checks couldn't have caught. See "Known bugs" below.

## Known bugs, found via real interactive use (2026-08-08), not yet fixed

Reported directly, with a reference screenshot (described below, the raw
image itself isn't in this repo -- see that section's own note) after
actually using the built `.blend` in Blender's GUI, not headless:

### Bug 1: selecting a different hairstyle (geoset group 0) makes the arms disappear

A group-0-only change (nothing to do with arms) causes arm geometry to
vanish. **Investigated this session, root cause not fully found, three
hypotheses checked with real evidence:**

1. **Ruled out**: cross-group contamination in husk's own C++ export.
   Wrote a standalone tinygltf-linked scan tool (scratchpad only) that
   checks every vertex's `JOINTS_1`/`WEIGHTS_1` for membership in more
   than one distinct geoset *group* (not just multiple variants within one
   group, which is expected/correct at real seams) -- zero such vertices
   found across the entire real `bloodelffemale_hd.m2` export. The raw
   glTF data itself is clean; whatever's wrong is downstream, in how
   Blender evaluates the node graph built from it.
2. **Ruled out**: a wiring bug in `build_geoset_switch_node_group`.
   Directly inspected the actual built node tree (not just the Python that
   built it): the `Compare` node's implicit "B" input really does default
   to exactly `0.0` (checked via `node.inputs[1].default_value`), and no
   two `Separate Geometry` nodes share the same upstream `Geometry` source
   (checked every link in the tree programmatically) -- the chain really
   is one linear sequence, not accidentally branched/duplicated.
3. **Real, evidenced, un-followed-up lead**: `GeometryNodeSeparateGeometry`
   with `domain='POINT'` (the default, never explicitly overridden) does
   **not** cleanly partition geometry into "Selection" + "Inverted" --
   proven with a minimal synthetic repro (a single quad, 2 verts selected,
   2 not, split across two triangles that each straddle the selection
   boundary): both triangles vanished from **both** outputs entirely,
   surviving only as 2 loose points on each side. Any face whose corners
   have mixed selection state is silently dropped from the whole graph,
   not assigned to either branch. Since this script chains up to 109
   sequential separations (one per variant across every multi-variant
   group), and each one re-evaluates selection across the *entire*
   remaining mesh, boundary-adjacent faces are at risk of erosion at
   *every* step, not just their own group's -- a real, structural design
   risk in the "peel one variant off a running remainder" architecture
   this script uses, not a one-off glitch. Not yet confirmed this is
   *the* mechanism that reaches all the way to arms specifically (that
   needs interactive GUI inspection of the real mesh topology, not
   headless scripting) -- flagged as the strongest lead, not a confirmed
   root cause.
4. **Also found, possibly related, not reconciled with the above**: the
   modifier's own *stored default value* for a Menu Switch's exposed
   dropdown doesn't match what `build_geoset_switch_node_group` intends.
   Both "Geoset group 0" (25 items) and "Geoset group 12" (3 items) showed
   the identical raw stored value `2` before any manual interaction --
   suspicious, since "2" being the *lowest-variant default* would require
   both groups' lowest variant to coincidentally sit at ordinal position 2
   of very differently-sized item lists. Leading theory, unconfirmed: the
   two placeholder items every fresh `GeometryNodeMenuSwitch` starts with
   ("A"/"B", cleared before adding real items -- see the empirically-found
   API notes above) may still occupy internal identifier slots 0/1 even
   after `.enum_items.clear()`, meaning the *first* real item added lands
   on identifier `2`, not ordinal `0` -- if true, `default_value =
   item_names[0]` is actually landing on the right item after all, and a
   later verification attempt that assumed "stored value == ordinal list
   index" (this session's own `check_tabard_switch.py`, scratchpad only)
   was reading its own results wrong, not exposing a real bug. **Needs
   resolving with real interactive Blender access, not further headless
   scripting** -- open the actual node tree in Blender's UI, click each
   Menu Switch's dropdown by hand, and confirm what actually gets
   selected versus what the modifier panel displays as "current."

### Bug 2: the tabard back-flap geometry never disappears, regardless of any dropdown selection

**Ruled out this session, with real evidence**: the geometry isn't
untagged. Cross-referenced the exact Blender vertex indices from the
reference screenshot below (20599-20661) directly against the imported
mesh's own vertex-group data: every one of them carries a real
`group_12,variant_3` tag at the expected ~0.5 rescaled weight -- not
missing, not zero, not some fallback `skinSectionId == -1` case. Group 12
does get a real dropdown (confirmed present in the node group's own
interface, `"Geoset group 12"`).

**Not yet resolved**: a targeted check switching that specific group's
modifier value away from its current setting, then re-evaluating and
searching the result for any vertex near vertex 20599's original bind-pose
position, found **zero matches both before and after the switch** -- i.e.
the check couldn't even confirm the geometry was visible in the
"default"/pre-switch state, let alone that switching hid it. Given Bug 1
item 4's finding about stored-value/ordinal-index confusion, this result
is more likely a flaw in *how this session's own verification script*
interpreted "which variant is currently selected" than proof the geometry
truly never moves -- but that's exactly why this needs the same real,
interactive Blender access Bug 1 does, not another headless script
guessing at internal identifier semantics. **Concrete next step**: open
the real `.blend` file in Blender's own GUI, manually click through every
item in "Geoset group 12"'s dropdown one at a time, and watch whether the
back-flap geometry (the region shown in the reference screenshot) actually
changes -- ground truth a script can currently only guess at.

### Reference screenshot (described, not embedded)

Luna attached a real Blender viewport screenshot (Edit Mode, vertex-index
overlay enabled) showing the tabard back-flap region from Bug 2, with
Blender's own per-vertex index labels visible and readable: **20599,
20602, 20606, 20607, 20608, 20616, 20620, 20621, 20622, 20624, 20626,
20631, 20633, 20635, 20636, 20639, 20642, 20643, 20645, 20646, 20650,
20653, 20654, 20657, 20659, 20661** (transcribed by hand from the image,
all 26 confirmed present and correctly tagged `group_12,variant_3` this
session, see Bug 2 above). **The image file itself was not saved into this
repo** -- pasted images in a chat session aren't exposed to tooling as a
filesystem path, so there was no way to copy the actual bytes anywhere
persistent; only this transcription survives. If a persistent reference
image is wanted, Luna will need to save/attach it manually (e.g. into a
`GEOSET_MASK_TODO_images/` directory or similar) in a future session.

## Follow-up needed, concrete

- Resolve Bug 1's still-open item 4 (stored-value/ordinal-index confusion)
  and item 3 (boundary-face erosion in chained `Separate Geometry`) with
  real interactive Blender access -- both need eyes on the actual node
  tree and actual viewport behavior, not more headless scripting.
- Resolve Bug 2 the same way -- confirm by hand whether "Geoset group 12"'s
  dropdown genuinely fails to toggle the back flap, or whether this
  session's own verification tooling was reading the wrong thing.
- If item 3 (destructive `POINT`-domain boundary dropping) turns out to be
  the real mechanism behind Bug 1, the fix is architectural: either switch
  `Separate Geometry`'s domain (`FACE` instead of `POINT` may partition
  cleanly where `POINT` doesn't -- unconfirmed, would need the same kind
  of synthetic-repro verification this session used to find the problem
  in the first place), or replace the "chain 109 sequential separations"
  design with something that doesn't re-derive selection against the
  *entire* remaining mesh at every step (e.g. one single multi-way
  classification pass instead of N sequential binary ones).
