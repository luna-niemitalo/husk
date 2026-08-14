"""husk_blender_geoset_mask.py -- companion Blender tooling for `husk
export`'s geoset-tag joints (Skeleton::GeosetTag, src/gltf_skeleton.hpp)
and, since this
session, its `--db2-dir/--dbd-dir/--char-layout-id` character-texture-layout
extras too (TODO/CHAR_TEXTURE_COMPOSITING_TODO.md's Stage 2/5). Not part of
husk itself (DESIGN.md scopes husk to "read WoW formats, write correct
glTF" -- Blender-side concerns are explicitly out of that scope) -- this is
the deferred "companion Blender-side script that hides extras-tagged-but-
visible geometry post-import" DESIGN.md's Key design decisions already
anticipated for exactly this shape of problem, now doing two related but
independent jobs in one post-import pass (per Luna's own direct steer: this
file is "the post-import script to do stuff like this in", not a place to
spin up a second file for a second kind of extras).

Run inside Blender, e.g.:
  blender --python tools/husk_blender_geoset_mask.py -- model.glb
(imports model.glb first), or with no trailing args, operates on whatever
mesh+armature objects are already in the current scene (a model already
imported via Blender's own File > Import, or the Scripting tab). The
texture-layout overlay (see below) needs the real file path -- unlike every
other extras this project attaches, `chr_texture_layout` lives on the glTF
*skin*, and Blender's stock importer has no supported extras target for
skins at all (confirmed empirically: node/mesh/material/camera/light/scene
extras all land as real Blender custom properties post-import; skin extras
land nowhere, not on the Armature object or its data) -- so this script
re-opens the same file and reads the raw JSON chunk directly for that one
piece of data, independent of whatever bpy's own importer already did.
When no trailing arg is given (already-open scene), the texture-layout
overlay is skipped with a note, since there's no file path left to re-read.

Builds a Geometry Nodes setup, one Menu Switch dropdown per geoset group,
instead of the Mask-modifier stack an earlier version of this script used
("an insane stack of mask modifiers" -- prompted directly, real usability
complaint on a real export with 90 modifiers).

**Second real design, same reason as the Mask-modifier -> Geometry Nodes
rewrite before it**: the first Geometry Nodes version chained `Separate
Geometry` sequentially (one variant peeled off a shrinking "remainder" at
a time). Real interactive use found two bugs (unrelated geometry
disappearing on an unrelated group's switch; some geometry never toggling
at all -- both since root-caused and fixed, see `ALWAYS_VISIBLE_VARIANTS`
below for the first and the design change in this paragraph for the
second), and that session's
own investigation found `GeometryNodeSeparateGeometry`'s default `POINT`
domain does not cleanly partition geometry -- a face with mixed-selection
corners vanishes from *both* outputs entirely. Chaining that operation up
to 109 times, each re-evaluating selection across the whole remaining
mesh, is a real structural risk of compounding, hard-to-predict geometry
loss. Prompted directly with the fix: don't chain destructively; compute
one combined boolean "is this vertex visible" expression per vertex
first (cheap, no geometry operations at all), then apply exactly **one**
`Separate Geometry` to the pristine, untouched input mesh at the very
end. What it does, in order:

  1. Finds every "group_<n>,variant_<n>" vertex group husk's tag-joint
     mechanism produced (one per distinct M2 geoset ID -- Skeleton::
     GeosetTag, src/gltf_skeleton.hpp) on the mesh object(s) -- created
     automatically by Blender's own glTF importer as a side effect of
     ordinary skin-weight import, no custom parsing needed here. The
     comma-separated, prefix-tagged naming (rather than one combined
     "geoset_<id>" token) is exactly so this parse is a plain comma-split
     + prefix-strip (`parse_geoset_vgroup_name`), not integer math.
  2. Groups them by geoset *group* (the `group_<n>` field, same numbering
     husk's own glTF extras use, e.g. geoset_id 1702 -> group 17) --
     variants within one group are mutually exclusive alternates (e.g. five
     hairstyle options).
  3. For every group with at least one *switchable* variant (see point 3a),
     adds a real "none" choice alongside its real M2 variants -- prompted
     directly, a real gap found in testing: `bloodelffemale_hd.m2`'s own
     tabard group (12) only has variants for "both flaps"/"back only"/
     "front only", never "no tabard at all", because the M2 itself simply
     has no submesh for that case (there's no real geoset ID to tag --
     husk can only tag what exists in the file). "none" is a synthetic
     addition on the Blender side, not something husk's export needs to
     change for. This now also covers groups with only *one* real M2
     variant (e.g. this model's groups 10/23/33/34) -- previously skipped
     entirely, so there was no way to tell "always shown, no alternative
     exists" apart from "should be toggleable but the toggle is missing";
     a single-variant group now gets a real "none" toggle too, per Luna's
     own direct steer that Blizzard's own runtime customization system can
     offer a "none" option without one needing to exist as a real geoset
     ID in the file, same reasoning as the tabard case).
  3a. `ALWAYS_VISIBLE_VARIANTS` excludes specific `(group, variant)` pairs
     from the switch entirely -- never hidden, never a dropdown item, just
     baked in as always-visible, same as a vertex with no geoset tag at
     all. Currently only `group 0, variant 0`: DESIGN.md's own "Anecdotal
     geoset-group semantics" table independently names group 0
     `SKIN_OR_HAIR` (two separate community reference tables agree) --
     variant 0 in that group is the character's own base skin body
     (torso/arms/legs), not a real hairstyle option, WoW's own geoset
     numbering just happens to co-locate them. Treating it as just another
     mutually-exclusive hairstyle choice made the base body vanish
     whenever a real hairstyle was picked -- a real bug Luna ground-
     truthed directly in Blender's own GUI.
  4. Per group with at least one switchable variant (real or "none"), builds two small pieces:
     a `Menu Switch` (`data_type='STRING'`) whose *output* is not
     geometry but the **name of the currently-selected variant's own
     vertex group** (or a sentinel string matching no real attribute, for
     "none") -- and a `Named Attribute` node whose own `Name` input is
     *linked* to that string output rather than a fixed constant, so it
     dynamically reads whichever variant is currently selected. Comparing
     that to `0` gives `is_selected_for_this_group` in one read, without
     enumerating every variant's own comparison. Separately, `owns_this_
     group` is a plain boolean OR across every real variant's own `Named
     Attribute > 0` (needed once per group, not per Separate Geometry
     call). `hidden_by_this_group = owns_this_group AND NOT is_selected_
     for_this_group` -- true only for a vertex that belongs to this
     group but isn't part of the currently active choice.
  5. `overall_hidden` is a plain boolean OR of every group's own `hidden_
     by_this_group` (`FunctionNodeBooleanMath`, no geometry operations
     yet); `overall_visible = NOT overall_hidden`. A vertex untouched by
     any geoset group at all never contributes a `True` to `overall_
     hidden`, so it stays visible automatically -- no separate "is this
     the untagged base" check needed.
  6. Exactly **one** `Separate Geometry` node, `Selection = overall_
     visible`, applied directly to the node group's own input geometry
     (never a chained/shrinking remainder) -- its `Selection` output is
     the node group's own final output. No `Join Geometry` needed either,
     since there's only ever one geometry stream from start to finish.
  7. The Menu Switch's own `Menu` input is promoted to a real
     `NodeSocketMenu` entry on the node group's own interface -- this is
     what actually makes it a dropdown in the Modifier panel, not just an
     internal node setting a human would have to open the node editor to
     change.
  8. Adds one Geometry Nodes modifier per mesh object referencing that
     node group, its per-group default drawn from `CURATED_DEFAULT_VARIANTS`
     when this model's own name has an entry there (a real, hand-verified-
     in-Blender's-own-GUI selection, not derived or guessed -- see that
     table's own doc comment), falling back to the lowest real variant ID
     per group otherwise (never "none" by default -- husk doesn't currently
     resolve real DB2 customization data to pick an actual default on its
     own, same disclaimed-placeholder precedent as `orderCandidatesForDefault`
     elsewhere in this project).
  9. Deletes every geoset tag bone from the armature once its vertex group
     has been read into the node graph above. Verified empirically:
     Blender vertex groups (and the point-domain
     attributes Geometry Nodes reads them as) are mesh-owned data,
     independent of the armature's bones -- removing the bone afterward
     doesn't touch the attribute or the node graph built from it, it only
     declutters the armature back to its real bone count and removes the
     exported .glb's inflated joint count (a `skin.joints` list well past
     real-time GPU skinning budgets -- see `gltf_skeleton.hpp`'s
     `Skeleton::GeosetTag` doc comment) from the finalized Blender scene a human
     actually works in.

Second, independent job -- character texture-layout overlay: for every
material whose own `texture_type` custom property (a real Blender custom
property, imported by Blender's stock glTF importer straight from the
material's own glTF `extras` -- see `gltf::Material::textureType`,
`src/export_materials.cpp`) matches one of `chr_texture_layout`'s own
`materials[].texture_type` entries, adds a toggleable debug overlay
highlighting every real `CharComponentTextureSections` placement rect for
that layout (`src/chrmodel_db2.hpp`'s `CharComponentTextureSection`) on top
of the material's own shading, without altering the material's real look
when switched off:

  1. Re-reads `chr_texture_layout` directly from the exported file's own
     raw glTF JSON (see the module docstring above for why bpy's own
     importer can't surface it). Nothing to do if the file has none (no
     `--char-layout-id` was given at export time) or no trailing file arg
     was given at all.
  2. Builds exactly one shared shader node group (`_build_section_overlay_
     group`), reused across every concerned material rather than one copy
     per material -- the section rect list is the same for all of them
     (they all belong to the one real `CharComponentTextureLayoutsID` this
     export was filtered to). Internally: an active-UV `ShaderNodeUVMap` ->
     `Separate XYZ` -> per-section axis-aligned box test (`LESS_THAN`/
     `GREATER_THAN` Math nodes on each of U/V against that section's own
     rect, normalized by the atlas's own `width`/`height` -- `MINIMUM`
     combines "inside U range" with "inside V range", the standard
     float-as-boolean AND trick since the Shader Editor's `ShaderNodeMath`
     has no dedicated boolean-logic mode, unlike Geometry Nodes' own
     `FunctionNodeBooleanMath`), then `MAXIMUM`-folds every section's own
     box test together into one combined mask (the float-as-boolean OR
     counterpart). A boolean **group input** (a real `NodeSocketBool`, not
     a hidden driver) gates the whole thing via one final `MULTIPLY` --
     appearing as a plain checkbox directly on the node in the Shader
     Editor once instanced, no Properties-panel/modifier plumbing needed
     (unlike the Geometry Nodes Menu Switch case above, a Shader node's own
     input sockets are already end-user-editable without extra promotion
     work).
  3. Per concerned material: instances that shared group once, feeds its
     `Factor` output into a new `Mix Shader` alongside the material's own
     *existing* output-linked shader and a bright magenta `Emission` (the
     overlay color), then rewires the Material Output's `Surface` input to
     that Mix Shader -- the original shader graph is preserved untouched
     upstream of the mix, not replaced, so turning the checkbox back off
     reproduces the exact original look.
  4. **Known, flagged-not-guessed assumption**: `CharComponentTextureSections`'
     own `Y`/`Height` are pixel coordinates in the atlas's own top-down
     image space (real WoW convention, same as every other 2D UI/texture
     rect this project has seen) -- Blender UV space is bottom-up, so `V`
     is flipped (`v = 1 - y/height`) when building each box test. This
     flip has not been independently re-confirmed against a real, visually
     ground-truthed placement the way e.g. the M2->glTF coordinate-frame
     conversion was (`DESIGN.md`'s Key design decisions) -- treat the
     overlay's real on-screen position as a hypothesis a human should
     check in the viewport, not a verified fact, until someone does.

A third, independent job, same "one post-import script" precedent: aligns
every billboard bone (`M2CompBone` billboard flags -> `gltf::Skeleton::
Joint::billboardMode` -> the `<name>_billboard_<mode>` joint-name suffix
`gltf_skeleton.cpp` writes, e.g. `bone_1_billboard_spherical`) to face the
scene's Camera object. Replaces an earlier prototype
(`/home/luna/Documents/BillboardDetector.blend`, Luna's own scratch file,
read-only reference -- not part of this repo) that rotated every
billboard-tagged *vertex* as one rigid group around one shared bounding-box
centroid, by copying the Camera object's raw world rotation plus a
hand-tuned fixed Euler offset -- only correct for a single billboard
sitting near the world origin, blind to multiple independent billboards
(they'd all pivot around one averaged centroid instead of their own bone),
and computed but never actually used the spherical/cylindrical distinction
its own `BilboardType` attribute carried.

  1. `find_billboard_bones` finds every armature bone whose name carries
     husk's own `_billboard_<mode>` suffix.
  2. `_bone_facing_axis` fits a plane to that bone's own weighted mesh
     vertices (rest pose) and picks whichever of the bone's 3 local rest
     axes is closest to that plane's normal -- computed per bone from real
     geometry, not assumed to be a fixed axis (e.g. always local X) across
     every model, since a billboard bone's roll is whatever the M2 file's
     own bone rotation says. A second local axis ("up", the remaining axis
     closest to the world axis relevant to this bone's mode) is picked the
     same way, giving a full [right, up, facing] orthonormal frame per bone.
  3. **First version of this used a native `Damped Track`/`Locked Track`
     constraint** (aim the facing axis straight at the Camera *object's
     position*). Real interactive verification (actually rendering a real
     fixture, `world/expansion06/doodads/lightforged/
     7lf_lightforged_smalllamp01.m2`) caught this as visibly wrong before it
     shipped further: a look-at-the-target-*position* rotation causes
     parallax skew (the quad renders as a trapezoid, not a flat rectangle)
     for anything off-center in frame -- real engines billboard sprites by
     matching the *camera's own orientation* (screen-plane-aligned)
     instead, precisely to avoid this. Fixed by computing each bone's pose
     rotation directly (`_billboard_target_frame_world`/
     `_apply_billboard_frame`), not via a native constraint at all:
       - `spherical`: bone's [right, up, facing] frame is set to exactly
         match the Camera's own [X, Y, Z] world axes -- true screen-plane
         alignment, correct for a rotationally-symmetric glow/flare sprite
         (the only kind spherical billboards are in practice) anywhere in
         frame.
       - `cylindrical_lock_x/y/z`: `up` is *fixed* to the named world axis
         (never tilts -- the entire point of "locked"); `facing` is a real
         look-at-the-camera direction, projected onto the plane
         perpendicular to that axis (yaw only) -- a deliberate look-at here,
         unlike spherical, matching the classic "Y-locked billboard"
         grass/tree convention most engines actually use.
     The math (`R_bone_world = M_target_world @ B_local^T`, both 3x3
     orthonormal frames) is verified by direct construction, not by hoping
     a native constraint's owner/target-space semantics line up --
     numerically confirmed against `7lf_lightforged_smalllamp01.m2`
     (spherical: facing axis lands within 0.02 degrees of the camera's own
     backward axis across several camera positions) and
     `spells/8fx_jaina_blisteringtornado.m2` (mixed spherical +
     cylindrical_lock_x/z bones on one model: the cylindrical bone's
     in-plane angle, the only thing a locked-track billboard is supposed to
     correct, likewise lands within 0.02 degrees; its nonzero *full* 3D
     angle to the camera is expected -- that's exactly the camera elevation
     a cylindrical billboard is supposed to ignore).

Applied once immediately (correct for a single still frame -- `render_glb.py`'s
own use, called right after that script builds its own camera). When
**not** running under `--background` (an interactive session, the actual
motivating use case: Luna re-opening an export in Blender's normal GUI),
also registers a `depsgraph_update_post` handler
(`_update_registered_billboards`) so every registered bone keeps recomputing
as the Camera object moves -- the same live behavior a real constraint
would give, without depending on a native constraint's axis-remap
semantics for the screen-alignment math. **Not yet independently
ground-truthed against real in-game billboard behavior** the way the
M2->glTF coordinate-frame conversion was (`DESIGN.md`'s Key design
decisions) -- verified here by checking the computed frame lands where the
math says it should, not by comparing to a real WoW client's own rendering
frame-by-frame. Luna should confirm the visual result in the interactive
viewport before trusting it fully, same caveat as the texture-layout
overlay's V-flip above.

**Fourth, independent job, this session (ANIMATED_TEXTURE_EFFECTS_TODO.md)**:
plays back husk's `texture_transform_animation`/`tint_animation`/
`fade_animation` material extras -- the genuinely-*animated* case of
M2TextureTransform/M2Color/M2TextureWeight, which (unlike the constant case)
core glTF has no animation-channel target for at all (see gltf_mesh.hpp's
own doc comments), so husk exports the real keyframe curve as inert extras
and this script is what turns it into real playback, same "husk exports
real data, a Blender companion script builds the actual behavior" pattern
as the billboard alignment and `render_glb.py`'s additive-material fix
before it. `apply_texture_transform_animation` builds one shared `Mapping`
node per concerned material (wired ahead of every Image Texture node whose
own `Vector` input isn't already linked to something else) and
`apply_tint_fade_animation` drives a Principled BSDF's `Base Color`/`Alpha`
directly (through an inserted multiply node when Base Color is already
texture-fed) -- both driven by a `frame_change_pre` handler that evaluates
the real curve at the current scene time (`Luna's own steer: "we have
access to the scene time, so we can use that for the simple animations"`),
looping on the curve's own real last-keyframe duration, not a fixed/guessed
clip length -- `scene.frame_end` is extended to fit the longest registered
curve at the scene's existing frame rate, so playing the timeline through
once shows exactly one real WoW loop. Verified against the real
`unk_exp11_7037014.m2` fixture (`tests/test_data_paths.hpp`'s
`kTextureTransformTranslationM2` doc comment) for `texture_transform_animation`
specifically -- `tint_animation`/`fade_animation` share the same curve-eval/
looping machinery but have **not** been verified against a real corpus
fixture with actual data this session (the corpus scan found real examples,
but none was narrowed to a minimal, skin-batch-verified fixture the way the
texture-transform case was) -- flagged, not asserted correct, same
discipline as the texture-layout overlay's V-flip note above.

**Fifth, independent job**: `apply_emitter_markers` places a small,
distinctly-shaped/colored, non-textured marker object at every real
`ribbon_emitters`/`particle_emitters` placement anchor (`read_emitter_
anchors`) -- previously, every M2Ribbon/M2Particle-driven effect (weapon
glow trails, magic auras, ...) was 100% invisible in Blender, since husk's
own extras deliberately carry only the minimal id/joint/position anchor,
not the full per-emitter texture/blend/curve data (too high-volume to
embed per-.glb -- see gltf_skeleton.hpp's `EmitterAnchor` doc comment; the
full data lives in `husk dump-chunks`'s separate JSON output, out of this
function's scope). This is a placement marker, not a particle-effect
reconstruction -- a real simulation matching WoW's own visual output
(texture, color, motion) is a separate, much bigger task
(TODO/ANIMATED_TEXTURE_EFFECTS_TODO.md's own stages 1-2). See
`apply_emitter_markers`'s own doc comment for the placement math (direct
matrix construction, same "verified by construction, not dependent on
native constraint/parenting semantics" approach as billboard alignment
above -- deliberately not Blender's native BONE object-parenting, whose
tail-vs-head origin convention isn't worth a separate verification pass
for a debug marker). Every marker lands in a dedicated, hidden-by-default
`_debug_marker_collection()` (`hide_render`/`hide_viewport` both set,
plus a belt-and-suspenders `hide_render` on the object itself) -- these
are debug placement aids, not something that should show up in a normal
viewport or leak into `render_glb.py`'s corpus previews. Verified
headlessly against the real `sword_1h_artifactskywall_d_06.m2` fixture (1
ribbon + 2 particle anchors): both marker kinds land within ~10% of the
mesh's own bounding-box diagonal from center (well inside the model's own
volume, not off in space); `visible_get()` confirms both viewport and
render visibility are correctly off. A zero-emitter model
(`bloodelffemale.m2`) is a silent no-op.
"""

import ast
import json
import re
import struct
import sys

import bmesh
import bpy
import mathutils

GROUP_PREFIX = "group_"
VARIANT_PREFIX = "variant_"
NONE_ITEM_NAME = "none"
# Never a real vertex-group name (husk's own naming is always
# "group_<n>,variant_<n>") -- Named Attribute reading this returns 0 for
# every vertex, which is exactly what "no variant selected" should mean.
NONE_SENTINEL_ATTR = "__husk_geoset_none__"

# Variants that must never be hidden by the mutual-exclusion switch, and are
# excluded from the switch's own dropdown entirely -- found real, not
# guessed, corroborated by DESIGN.md's own "Anecdotal geoset-group
# semantics" table: geoset group 0
# is independently named `SKIN_OR_HAIR` by two unrelated community
# reference tables, not just "Hair" -- variant 0 within it is the
# character's own base skin body (torso/arms/legs), not a real optional
# hairstyle, WoW's own geoset ID numbering just co-locates them under one
# group. Treating variant 0 as just another mutually-exclusive hairstyle
# choice made the base body disappear whenever a real hairstyle (1-24) was
# selected -- ground-truthed directly by Luna in Blender's own GUI. This is
# a per-model-numbering fact this project has real evidence for, not a
# generalization to every group -- see DESIGN.md's own table for the single
# group this is confirmed on.
ALWAYS_VISIBLE_VARIANTS = {
    0: {0},
}

# Per-model curated default geoset selections, found by hand in Blender's
# real GUI -- a real, disclosed, per-model override table, not something
# derived or guessed, for models with no real customization-choice data
# available at export time. Keyed by the model's own real name (the mesh
# object's name, minus any Blender-assigned ".001"-style dedup suffix -- see
# `_model_key`); each value is `{group: item_name}` where `item_name` is
# `"variant_<n>"` (a real M2 geoset variant ID for that group) or `"none"`.
# A group missing from a model's own dict, or a model missing from this
# dict entirely, falls back to the plain "lowest real variant ID" default.
# Superseded per-group by `enabled_geosets_to_default_overrides` when the
# export was given real `--customization-choice-ids` -- husk *can* now
# resolve a real customization choice to its real geoset ID
# (`src/chrcustomization_db2.hpp`, `read_enabled_geosets` below), this
# table just still matters for any export that wasn't given one.
CURATED_DEFAULT_VARIANTS = {
    "bloodelffemale_hd": {
        0: NONE_ITEM_NAME,
        4: "variant_1",
        5: "variant_1",
        7: "variant_2",
        8: NONE_ITEM_NAME,
        9: NONE_ITEM_NAME,
        11: NONE_ITEM_NAME,
        12: NONE_ITEM_NAME,
        13: "variant_1",
        15: NONE_ITEM_NAME,
        17: NONE_ITEM_NAME,
        18: "variant_1",
        20: "variant_1",
        22: "variant_1",
        32: "variant_2",
        35: "variant_7",
        36: NONE_ITEM_NAME,
        39: NONE_ITEM_NAME,
        51: "variant_1",
    },
}


def _model_key(mesh_obj_name):
    """Strips Blender's own ".001"-style dedup suffix (added on a second
    import of the same-named object) so `CURATED_DEFAULT_VARIANTS` still
    matches a re-imported model.
    """
    return re.sub(r"\.\d+$", "", mesh_obj_name)


def parse_geoset_vgroup_name(name):
    """'group_<n>,variant_<n>' -> (group, variant) ints, or None if not a match.

    Plain comma-split + prefix-strip, not a regex, deliberately -- husk
    emits the name in this field-separated shape specifically so this
    parse (and the equivalent split inside the geometry-node graph this
    script builds, conceptually the same operation) never needs
    division/modulo on a combined ID.
    """
    parts = name.split(",")
    if len(parts) != 2:
        return None
    group_part, variant_part = parts
    if not group_part.startswith(GROUP_PREFIX) or not variant_part.startswith(VARIANT_PREFIX):
        return None
    try:
        return int(group_part[len(GROUP_PREFIX):]), int(variant_part[len(VARIANT_PREFIX):])
    except ValueError:
        return None


def find_mesh_and_armature():
    mesh_objs = [o for o in bpy.data.objects if o.type == 'MESH']
    arm_objs = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    if not mesh_objs or not arm_objs:
        raise RuntimeError("no mesh/armature objects found in the current scene -- import a husk "
                            "export first")
    return mesh_objs, arm_objs[0]


def switchable_variants(group, variants):
    """`variants` (`[(variant, vg_name), ...]`) minus whatever
    `ALWAYS_VISIBLE_VARIANTS` forces always-on for this group, sorted --
    shared by `build_geoset_switch_node_group` and `main`'s own summary
    count so both agree on which groups actually get a dropdown.
    """
    always_visible = ALWAYS_VISIBLE_VARIANTS.get(group, frozenset())
    return sorted(v for v in variants if v[0] not in always_visible)


def geoset_groups(mesh_obj):
    """{group: [(variant, vertex_group_name), ...]} for this mesh object."""
    groups = {}
    for vg in mesh_obj.vertex_groups:
        parsed = parse_geoset_vgroup_name(vg.name)
        if parsed is None:
            continue
        group, variant = parsed
        groups.setdefault(group, []).append((variant, vg.name))
    return groups


def _named_attr_gt_zero(node_tree, name_socket_or_value):
    """(Named Attribute -> Compare > 0) pair. `name_socket_or_value` is
    either a plain string (wired as a constant "Name") or an output socket
    (wired as a dynamic, linked "Name" -- the trick this whole redesign
    hinges on: Named Attribute's Name input accepts a *linked* string just
    like any other socket, so which attribute gets read can depend on a
    Menu Switch's own current selection). Returns the Compare node's
    boolean output socket.
    """
    named_attr = node_tree.nodes.new('GeometryNodeInputNamedAttribute')
    named_attr.data_type = 'FLOAT'
    if isinstance(name_socket_or_value, str):
        named_attr.inputs[0].default_value = name_socket_or_value
    else:
        node_tree.links.new(name_socket_or_value, named_attr.inputs[0])

    compare = node_tree.nodes.new('FunctionNodeCompare')
    compare.data_type = 'FLOAT'
    compare.operation = 'GREATER_THAN'
    node_tree.links.new(named_attr.outputs[0], compare.inputs[0])  # B stays the default 0.0
    return compare.outputs[0]


def _bool_math(node_tree, operation, *operand_sockets):
    """FunctionNodeBooleanMath with `operation` ('AND'/'OR'/'NOT'), wired
    positionally (inputs[0]/[1] share the same socket *name* on this node
    type, so lookup has to be by position, not by name). NOT only reads
    inputs[0].
    """
    node = node_tree.nodes.new('FunctionNodeBooleanMath')
    node.operation = operation
    for i, socket in enumerate(operand_sockets):
        node_tree.links.new(socket, node.inputs[i])
    return node.outputs[0]


def _or_all(node_tree, bool_sockets):
    """Left-fold OR across `bool_sockets` (non-empty, guaranteed by callers)."""
    acc = bool_sockets[0]
    for b in bool_sockets[1:]:
        acc = _bool_math(node_tree, 'OR', acc, b)
    return acc


def _build_group_hidden_term(node_tree, group_input, group, variants, default_item=None):
    """One geoset group's contribution to the overall "hidden" expression --
    see this module's own doc comment, point 4, for the shape. `variants`
    is `[(variant, vg_name), ...]`, lowest variant first. Returns a boolean
    output socket: true for a vertex that belongs to this group but isn't
    part of its currently-selected choice. `default_item` (a real
    `"variant_<n>"` or `"none"`) overrides the default dropdown selection
    when given and valid for this group's own real item set -- see
    `CURATED_DEFAULT_VARIANTS`; falls back to the lowest real variant ID
    otherwise, same as before that table existed.
    """
    owns_group = _or_all(node_tree, [_named_attr_gt_zero(node_tree, vg_name) for _, vg_name in variants])

    selector = node_tree.nodes.new('GeometryNodeMenuSwitch')
    selector.data_type = 'STRING'
    selector.label = f"Geoset group {group} (selector)"
    # Fresh nodes start with two placeholder items ("A"/"B") that have to
    # be cleared before adding real ones, or they'd linger as two extra,
    # unwired, meaningless dropdown entries.
    selector.enum_definition.enum_items.clear()
    item_names = []
    for variant, vg_name in variants:
        item_name = f"variant_{variant}"
        selector.enum_definition.enum_items.new(item_name)
        selector.inputs[item_name].default_value = vg_name
        item_names.append(item_name)
    selector.enum_definition.enum_items.new(NONE_ITEM_NAME)
    selector.inputs[NONE_ITEM_NAME].default_value = NONE_SENTINEL_ATTR

    # Promote the Menu input to the node group's own interface as a real
    # NodeSocketMenu entry -- this is what actually makes it a dropdown in
    # the modifier panel, not just an internal node setting a human would
    # have to open the node editor to change. Must link *before* setting
    # default_value -- an unlinked interface Menu socket has no known
    # items yet (its valid enum values are derived from whatever it's
    # connected to), so setting default_value first throws "enum ... not
    # found in ()".
    menu_socket = node_tree.interface.new_socket(
        name=f"Geoset group {group}", in_out='INPUT', socket_type='NodeSocketMenu')
    node_tree.links.new(group_input.outputs[menu_socket.name], selector.inputs[0])
    valid_items = item_names + [NONE_ITEM_NAME]
    if default_item is not None and default_item in valid_items:
        menu_socket.default_value = default_item
    else:
        menu_socket.default_value = item_names[0]  # lowest real variant ID visible by default

    is_selected = _named_attr_gt_zero(node_tree, selector.outputs[0])
    not_selected = _bool_math(node_tree, 'NOT', is_selected)
    return _bool_math(node_tree, 'AND', owns_group, not_selected)


def build_geoset_switch_node_group(name, groups, default_overrides=None):
    """One GeometryNodeTree implementing the whole group/variant switch --
    see this module's own doc comment for the full shape. `groups` is
    `geoset_groups(mesh_obj)`'s own return value. `default_overrides` is
    `CURATED_DEFAULT_VARIANTS`'s own per-model `{group: item_name}` dict, or
    None/empty for the plain lowest-variant default. Builds one combined
    "is this vertex hidden" boolean expression across every multi-variant
    group first (no geometry operations at all), then applies exactly one
    `Separate Geometry` to the pristine input mesh at the very end --
    deliberately not a chain of per-variant separations (the design this
    replaced), which real use found could lose geometry at compounding
    selection boundaries across up to 109 sequential operations.
    """
    default_overrides = default_overrides or {}
    node_tree = bpy.data.node_groups.new(name, 'GeometryNodeTree')
    node_tree.interface.new_socket(name="Geometry", in_out='INPUT', socket_type='NodeSocketGeometry')
    node_tree.interface.new_socket(name="Geometry", in_out='OUTPUT', socket_type='NodeSocketGeometry')

    group_input = node_tree.nodes.new('NodeGroupInput')
    group_output = node_tree.nodes.new('NodeGroupOutput')

    hidden_terms = []
    for group in sorted(groups):
        switchable = switchable_variants(group, groups[group])
        if not switchable:
            # Every real variant in this group is either the group's own
            # single option (nothing mutually exclusive to hide) or forced
            # always-visible (ALWAYS_VISIBLE_VARIANTS) -- either way, no
            # switch needed for this group.
            continue
        hidden_terms.append(_build_group_hidden_term(node_tree, group_input, group, switchable,
                                                       default_overrides.get(group)))

    if hidden_terms:
        overall_visible = _bool_math(node_tree, 'NOT', _or_all(node_tree, hidden_terms))
        separate = node_tree.nodes.new('GeometryNodeSeparateGeometry')
        node_tree.links.new(group_input.outputs['Geometry'], separate.inputs['Geometry'])
        node_tree.links.new(overall_visible, separate.inputs['Selection'])
        node_tree.links.new(separate.outputs['Selection'], group_output.inputs['Geometry'])
    else:
        # No group has 2+ variants -- nothing to switch, pass geometry through unchanged.
        node_tree.links.new(group_input.outputs['Geometry'], group_output.inputs['Geometry'])

    return node_tree


def apply_geoset_switch(mesh_obj, extra_default_overrides=None):
    """Builds the node group and adds it as a Geometry Nodes modifier;
    returns this mesh object's own `geoset_groups` result. Looks up
    `CURATED_DEFAULT_VARIANTS` by this mesh object's own real name (see
    `_model_key`) to pick real, hand-verified defaults over the plain
    lowest-variant fallback, when available for this model.
    `extra_default_overrides` (`enabled_geosets_to_default_overrides`'s own
    return shape, or None) layers on top of and takes priority over
    `CURATED_DEFAULT_VARIANTS` -- real DB2-resolved data from a specific
    character's own customization choices is a stronger signal than a
    hand-picked, model-wide curated guess.
    """
    groups = geoset_groups(mesh_obj)
    if not groups:
        return groups
    default_overrides = {**CURATED_DEFAULT_VARIANTS.get(_model_key(mesh_obj.name), {}),
                          **(extra_default_overrides or {})}
    node_tree = build_geoset_switch_node_group(f"{mesh_obj.name}_geoset_switch", groups, default_overrides)
    mod = mesh_obj.modifiers.new(name="HuskGeosetSwitch", type='NODES')
    mod.node_group = node_tree
    return groups


def delete_geoset_tag_bones(armature_obj, all_groups):
    """Removes every group_<n>,variant_<n> bone -- their vertex groups/node graph survive this untouched."""
    tag_names = {vg_name for variants in all_groups.values() for _variant, vg_name in variants}
    if not tag_names:
        return 0
    prev_active = bpy.context.view_layer.objects.active
    bpy.context.view_layer.objects.active = armature_obj
    bpy.ops.object.mode_set(mode='EDIT')
    removed = 0
    for name in tag_names:
        eb = armature_obj.data.edit_bones.get(name)
        if eb is not None:
            armature_obj.data.edit_bones.remove(eb)
            removed += 1
    bpy.ops.object.mode_set(mode='OBJECT')
    bpy.context.view_layer.objects.active = prev_active
    return removed


def apply_geoset_switches(mesh_objs, armature_obj, extra_default_overrides=None):
    """Builds the real geoset Menu Switch dropdown for every mesh object
    and deletes the now-unnecessary tag bones -- the single, shared
    "make this a geoset-correct scene" step, used identically by every
    real entry point into this pipeline (`main()`'s own `geoset_stage`
    below, and `render_glb.py`'s corpus-preview render) so neither one
    can drift from the other. Two real, independent reasons every caller
    needs this, not just the interactive one: (1) without it, every
    geoset variant (every hairstyle, every tabard state, ...) renders
    simultaneously, unfiltered -- husk's own extras note applies
    literally; (2) `delete_geoset_tag_bones` removes the inert geoset-tag
    joints outright, closing a real skinning bug found 2026-08-14
    (TODO/RENDER_QUALITY_TODO.md section 1) -- leaving them in measurably
    pulls any vertex carrying real tag weight back toward its bind pose
    every frame, visible as a "shear"/detached-flap artifact, confirmed
    against the real client. `extra_default_overrides`: see
    `apply_geoset_switch`'s own doc comment -- applied identically to every
    mesh object (the tag-joint/vertex-group structure is shared across
    LOD tiers of the same model, so one real customization choice applies
    the same way to all of them). Returns (all_groups, switch_groups,
    removed) for the caller's own logging.
    """
    all_groups = {}
    switch_groups = 0
    for mesh_obj in mesh_objs:
        groups = apply_geoset_switch(mesh_obj, extra_default_overrides)
        switch_groups += sum(1 for gid, variants in groups.items()
                              if switchable_variants(gid, variants))
        for gid, variants in groups.items():
            all_groups.setdefault(gid, []).extend(variants)
    removed = delete_geoset_tag_bones(armature_obj, all_groups)
    return all_groups, switch_groups, removed


def _read_glb_json(filepath):
    """Parses `filepath`'s raw glTF JSON chunk directly -- the shared
    mechanics every `read_*` function below needs, since Blender's own
    importer has no supported extras target for a glTF *skin* at all
    (confirmed empirically: node/mesh/material/camera/light/scene extras
    all land as real Blender custom properties post-import; skin extras
    land nowhere -- see the module docstring). Third real occurrence of
    this exact parse (chr_texture_layout, enabled_geosets, now emitter
    anchors) is what earns it as a shared helper per this project's own
    "abstractions are earned" rule, rather than a fourth copy-pasted body.
    Returns None on any falsy/unreadable/unparseable `filepath` -- never a
    guess, never a hard failure of the caller's own work.
    """
    if not filepath:
        return None
    try:
        if filepath.lower().endswith(".gltf"):
            with open(filepath, "r", encoding="utf-8") as f:
                return json.load(f)
        with open(filepath, "rb") as f:
            raw = f.read()
        # glTF binary container: 12-byte header (magic/version/length), then
        # chunks of (length: u32, type: u32, data). The first chunk is
        # always JSON per the glTF 2.0 spec -- no need to scan for it.
        (chunk_length, _chunk_type) = struct.unpack_from("<II", raw, 12)
        return json.loads(raw[20:20 + chunk_length])
    except (OSError, ValueError, struct.error):
        return None


def read_chr_texture_layout(filepath):
    """Reads `chr_texture_layout` straight out of the exported file's own
    raw glTF JSON (skins[].extras). Returns None if `filepath` is falsy,
    isn't a .glb/.gltf husk could have written, or genuinely has no such
    extras (no --char-layout-id was given at export time) -- never a
    guess, never a hard failure of the rest of this script's work.
    """
    data = _read_glb_json(filepath)
    if data is None:
        return None
    for skin in data.get("skins", []):
        extras = skin.get("extras")
        if extras and "chr_texture_layout" in extras:
            return extras["chr_texture_layout"]
    return None


def read_enabled_geosets(filepath):
    """Reads `enabled_geosets` straight out of the exported file's own raw
    glTF JSON (skins[].extras). Returns a list of `{"choice_id": int,
    "geoset_id": int}` dicts, or None if `filepath` is falsy, isn't a real
    husk `.glb`/`.gltf`, or genuinely has no such extras (no
    `--customization-choice-ids` was given at export time, or none of the
    given choice IDs resolved a geoset) -- never a guess.
    """
    data = _read_glb_json(filepath)
    if data is None:
        return None
    for skin in data.get("skins", []):
        extras = skin.get("extras")
        if extras and "enabled_geosets" in extras:
            return extras["enabled_geosets"]
    return None


def read_emitter_anchors(filepath):
    """Reads `ribbon_emitters`/`particle_emitters` straight out of the
    exported file's own raw glTF JSON (skins[].extras) -- see
    gltf_skeleton.hpp's `Skeleton::EmitterAnchor` doc comment: these are
    deliberately *not* real glTF child nodes (unlike Attachment/Event/
    Light) despite sharing the exact same "translation relative to an
    owning joint" shape, because a model can carry dozens of them each
    with several would-be animation curves -- too high-volume to embed
    per-.glb, so only the minimal id/joint/position anchor lives here; the
    full per-emitter data (texture, blend mode, curves) lives in `husk
    dump-chunks`'s separate JSON output instead, out of this function's
    reach (and this script's current scope -- see `apply_emitter_markers`).

    Also reads `skins[0].joints`/`nodes[].name` from the same JSON, needed
    to resolve an anchor's raw `joint` (an index into `skin.joints`, i.e.
    a raw M2 bone index -- see `gltf_skeleton.cpp`'s `skin.joints.push_back`
    loop) to the real Blender bone name Blender's importer will have used
    (`Skeleton::Joint::name` when known, else husk's own `bone_<index>`
    fallback -- never guessable from the index alone without this lookup).

    Returns `(ribbon_anchors, particle_anchors, joint_bone_names)` --
    the first two are lists of `{"id", "joint", "position": {"x","y","z"}}`
    dicts (empty, not None, when absent -- these are usually-present, not
    opt-in-flag-gated the way chr_texture_layout/enabled_geosets are), the
    third a `{joint_index: bone_name}` dict. All empty when `filepath` is
    falsy/unreadable or the model has no skin at all.
    """
    data = _read_glb_json(filepath)
    if data is None:
        return [], [], {}
    nodes = data.get("nodes", [])
    joint_bone_names = {}
    ribbon_anchors, particle_anchors = [], []
    for skin in data.get("skins", []):
        skin_joints = skin.get("joints", [])
        for joint_index, node_index in enumerate(skin_joints):
            if 0 <= node_index < len(nodes):
                joint_bone_names[joint_index] = nodes[node_index].get("name", f"bone_{joint_index}")
        extras = skin.get("extras")
        if extras:
            ribbon_anchors = extras.get("ribbon_emitters", []) or ribbon_anchors
            particle_anchors = extras.get("particle_emitters", []) or particle_anchors
    return ribbon_anchors, particle_anchors, joint_bone_names


def enabled_geosets_to_default_overrides(enabled_geosets):
    """`read_enabled_geosets`'s own return shape -> `{group: item_name}`,
    the same shape `CURATED_DEFAULT_VARIANTS`'s per-model dicts and
    `build_geoset_switch_node_group`'s `default_overrides` parameter
    already use -- `geoset_id // 100`/`geoset_id % 100` is the exact
    inverse of husk's own `group*100+variant` convention
    (`src/chrcustomization_db2.cpp`'s `resolveChoice`), so no separate
    lookup table is needed here. Doesn't validate that a given group/variant
    actually exists on this specific mesh object -- `build_geoset_switch_
    node_group`'s own `default_item in valid_items` check already handles
    an unmatched entry by falling back to the plain lowest-variant default,
    same as any other invalid/missing override.
    """
    overrides = {}
    for entry in enabled_geosets or []:
        geoset_id = entry.get("geoset_id")
        if geoset_id is None:
            continue
        overrides[geoset_id // 100] = f"{VARIANT_PREFIX}{geoset_id % 100}"
    return overrides


_OVERLAY_GROUP_NAME = "HuskChrTextureLayoutOverlay"


def _build_section_overlay_group(layout):
    """One shared node group -- `Show Overlay` (bool) in, `Factor` (float)
    out -- computing "is this shading point inside any of this layout's own
    real CharComponentTextureSections rects," gated by the boolean input.
    Reused (instanced) across every concerned material rather than rebuilt
    per material, since the section list is the same for all of them (one
    real CharComponentTextureLayoutsID per export, per --char-layout-id).
    """
    atlas_w = layout.get("width") or 1
    atlas_h = layout.get("height") or 1
    sections = layout.get("sections", [])

    group = bpy.data.node_groups.new(_OVERLAY_GROUP_NAME, "ShaderNodeTree")
    group.interface.new_socket("Show Overlay", in_out='INPUT', socket_type='NodeSocketBool')
    group.interface.new_socket("Factor", in_out='OUTPUT', socket_type='NodeSocketFloat')

    nodes, links = group.nodes, group.links
    group_input = nodes.new("NodeGroupInput")
    group_output = nodes.new("NodeGroupOutput")

    uv_map = nodes.new("ShaderNodeUVMap")  # empty uv_map name -- the object's active UV layer
    separate_uv = nodes.new("ShaderNodeSeparateXYZ")
    links.new(uv_map.outputs["UV"], separate_uv.inputs["Vector"])

    def const(value):
        node = nodes.new("ShaderNodeMath")
        node.operation = 'ADD'
        node.inputs[0].default_value = value
        node.inputs[1].default_value = 0.0
        return node.outputs[0]

    section_masks = []
    for section in sections:
        x0 = section.get("x", 0) / atlas_w
        y0 = section.get("y", 0) / atlas_h
        x1 = x0 + section.get("width", 0) / atlas_w
        y1 = y0 + section.get("height", 0) / atlas_h
        # WoW atlas Y grows downward (top-down pixel convention); Blender UV
        # V grows upward -- flip, per this file's own module-docstring note
        # that this hasn't been visually re-confirmed yet.
        v0, v1 = 1.0 - y1, 1.0 - y0

        ge_x0 = nodes.new("ShaderNodeMath"); ge_x0.operation = 'GREATER_THAN'
        links.new(separate_uv.outputs["X"], ge_x0.inputs[0])
        ge_x0.inputs[1].default_value = x0
        le_x1 = nodes.new("ShaderNodeMath"); le_x1.operation = 'LESS_THAN'
        links.new(separate_uv.outputs["X"], le_x1.inputs[0])
        le_x1.inputs[1].default_value = x1
        ge_y0 = nodes.new("ShaderNodeMath"); ge_y0.operation = 'GREATER_THAN'
        links.new(separate_uv.outputs["Y"], ge_y0.inputs[0])
        ge_y0.inputs[1].default_value = v0
        le_y1 = nodes.new("ShaderNodeMath"); le_y1.operation = 'LESS_THAN'
        links.new(separate_uv.outputs["Y"], le_y1.inputs[0])
        le_y1.inputs[1].default_value = v1

        inside_x = nodes.new("ShaderNodeMath"); inside_x.operation = 'MINIMUM'
        links.new(ge_x0.outputs[0], inside_x.inputs[0])
        links.new(le_x1.outputs[0], inside_x.inputs[1])
        inside_y = nodes.new("ShaderNodeMath"); inside_y.operation = 'MINIMUM'
        links.new(ge_y0.outputs[0], inside_y.inputs[0])
        links.new(le_y1.outputs[0], inside_y.inputs[1])
        inside = nodes.new("ShaderNodeMath"); inside.operation = 'MINIMUM'
        links.new(inside_x.outputs[0], inside.inputs[0])
        links.new(inside_y.outputs[0], inside.inputs[1])
        section_masks.append(inside.outputs[0])

    if section_masks:
        combined = section_masks[0]
        for mask in section_masks[1:]:
            or_node = nodes.new("ShaderNodeMath")
            or_node.operation = 'MAXIMUM'
            links.new(combined, or_node.inputs[0])
            links.new(mask, or_node.inputs[1])
            combined = or_node.outputs[0]
    else:
        combined = const(0.0)

    gated = nodes.new("ShaderNodeMath")
    gated.operation = 'MULTIPLY'
    links.new(combined, gated.inputs[0])
    links.new(group_input.outputs["Show Overlay"], gated.inputs[1])
    links.new(gated.outputs[0], group_output.inputs["Factor"])

    return group


def apply_texture_layout_overlay(layout, materials):
    """For every material whose own `texture_type` custom property matches
    one of `layout`'s own `materials[].texture_type`, mixes a toggleable
    magenta section-boundary overlay on top of its existing shader output
    (see the module docstring's "Second, independent job" section for the
    full node graph). Returns the count of materials touched.
    """
    concerned_types = {m.get("texture_type") for m in layout.get("materials", [])}
    if not concerned_types or not layout.get("sections"):
        return 0

    overlay_group = None
    touched = 0
    for mat in materials:
        if mat.get("texture_type") not in concerned_types:
            continue
        if mat.node_tree is None:
            continue
        if overlay_group is None:
            overlay_group = _build_section_overlay_group(layout)

        node_tree = mat.node_tree
        output_node = next((n for n in node_tree.nodes if n.type == 'OUTPUT_MATERIAL'), None)
        if output_node is None or not output_node.inputs["Surface"].is_linked:
            continue
        original_link = output_node.inputs["Surface"].links[0]
        original_socket = original_link.from_socket

        # These three nodes used to get no explicit `.location` at all, so
        # Blender dropped them at the node tree's origin (0, 0) -- very
        # likely on top of whatever this material's own existing graph
        # already occupies there, real but effectively invisible without
        # manually dragging nodes apart first (found via real interactive
        # use). Anchored below the existing graph's own lowest node
        # instead, so they land in guaranteed-empty space regardless of
        # this material's own layout.
        existing_ys = [n.location.y for n in node_tree.nodes]
        overlay_y = (min(existing_ys) if existing_ys else 0.0) - 400.0
        output_x = output_node.location.x

        group_node = node_tree.nodes.new("ShaderNodeGroup")
        group_node.node_tree = overlay_group
        group_node.inputs["Show Overlay"].default_value = False
        group_node.location = (output_x - 600.0, overlay_y)

        emission = node_tree.nodes.new("ShaderNodeEmission")
        emission.inputs["Color"].default_value = (1.0, 0.0, 1.0, 1.0)
        emission.location = (output_x - 300.0, overlay_y)

        mix = node_tree.nodes.new("ShaderNodeMixShader")
        mix.location = (output_x, overlay_y + 150.0)
        node_tree.links.new(group_node.outputs["Factor"], mix.inputs["Fac"])
        node_tree.links.new(original_socket, mix.inputs[1])
        node_tree.links.new(emission.outputs["Emission"], mix.inputs[2])
        node_tree.links.new(mix.outputs["Shader"], output_node.inputs["Surface"])

        touched += 1
    return touched


BILLBOARD_NAME_RE = re.compile(r"_billboard_(spherical|cylindrical_lock_[xyz])$")

_WORLD_AXES = {
    'X': mathutils.Vector((1.0, 0.0, 0.0)),
    'Y': mathutils.Vector((0.0, 1.0, 0.0)),
    'Z': mathutils.Vector((0.0, 0.0, 1.0)),
}


def find_billboard_bones(armature_obj):
    """{bone_name: mode} for every bone carrying husk's own `_billboard_<mode>`
    joint-name suffix (`gltf_skeleton.cpp`, mirroring `m2::billboardModeName`)."""
    bones = {}
    for bone in armature_obj.data.bones:
        m = BILLBOARD_NAME_RE.search(bone.name)
        if m:
            bones[bone.name] = m.group(1)
    return bones


def _fit_plane_normal(points):
    """Plane normal for a (near-)planar point set: the largest-magnitude
    cross product of any two centroid-relative points, robust against
    picking a near-collinear pair on a real (not perfectly axis-aligned)
    billboard quad. None if every pair is degenerate (fewer than 3 real
    points, or all collinear).
    """
    centroid = sum(points, mathutils.Vector()) / len(points)
    best, best_len = None, 0.0
    for i in range(len(points)):
        for j in range(i + 1, len(points)):
            n = (points[i] - centroid).cross(points[j] - centroid)
            if n.length > best_len:
                best, best_len = n, n.length
    if best is None or best_len < 1e-8:
        return None
    return best.normalized()


def _bone_local_axis_closest_to(bone, direction_world, exclude=None):
    """Which of `bone`'s 3 rest-pose local axes (`bone.matrix_local`, already
    in armature/world space -- Blender bone matrices are relative to the
    armature, not the parent bone) is closest to `direction_world`. Returns
    a signed letter ('X'/'-X'/'Y'/...) so the caller can tell a facing axis
    from its reverse; `exclude` (a bare letter, no sign) drops one axis from
    consideration, used when picking a *second*, necessarily-different axis
    on the same bone.
    """
    mat = bone.matrix_local.to_3x3()
    axes = {'X': mat.col[0], 'Y': mat.col[1], 'Z': mat.col[2]}
    if exclude:
        axes.pop(exclude, None)
    best_name, best_dot = None, -1.0
    for name, axis in axes.items():
        dot = axis.normalized().dot(direction_world)
        if abs(dot) > best_dot:
            best_name = ('-' if dot < 0 else '') + name
            best_dot = abs(dot)
    return best_name


def _bone_facing_axis(mesh_objs, armature_obj, bone_name):
    """Which of `bone_name`'s local rest axes is its billboard quad's own
    facing normal -- fit from the real mesh vertices weighted to that bone
    (searched across every mesh object, first match wins), not assumed to
    be a fixed convention across models. None if no mesh carries a matching
    vertex group with enough weighted geometry to fit a plane.
    """
    bone = armature_obj.data.bones[bone_name]
    for mesh_obj in mesh_objs:
        vg = mesh_obj.vertex_groups.get(bone_name)
        if vg is None:
            continue
        local_verts = [v.co.copy() for v in mesh_obj.data.vertices
                       for g in v.groups if g.group == vg.index and g.weight > 0.01]
        if len(local_verts) < 3:
            continue
        normal_local = _fit_plane_normal(local_verts)
        if normal_local is None:
            continue
        normal_world = (mesh_obj.matrix_world.to_3x3() @ normal_local).normalized()
        return _bone_local_axis_closest_to(bone, normal_world)
    return None


def find_camera_object():
    """husk's own export always names the render-anchor camera node 'Camera'
    when the source M2 has one; fall back to the scene's active camera, then
    any CAMERA-type object, so this still works against a scene where the
    artist added their own camera instead.
    """
    cam = bpy.data.objects.get("Camera")
    if cam is not None and cam.type == 'CAMERA':
        return cam
    if bpy.context.scene.camera is not None:
        return bpy.context.scene.camera
    return next((o for o in bpy.data.objects if o.type == 'CAMERA'), None)


def _signed_axis_vector(letter):
    """'X'/'-Z'/... -> the corresponding unit Vector."""
    v = _WORLD_AXES[letter.lstrip("-")].copy()
    return -v if letter.startswith("-") else v


def _local_frame_matrix(right, up, facing):
    """3x3 whose *columns* are `right`/`up`/`facing` (each a Vector) --
    `mathutils.Matrix((row0, row1, row2))` takes rows, so this is built
    component-wise rather than via `.transposed()`, to keep the column
    meaning obvious at the call site.
    """
    return mathutils.Matrix((
        (right.x, up.x, facing.x),
        (right.y, up.y, facing.y),
        (right.z, up.z, facing.z),
    ))


def _billboard_target_frame_world(mode, bone_head_world, camera_obj):
    """The desired world-space [right, up, facing] orthonormal frame for a
    billboard bone, given its mode:

      - spherical: exactly the camera's own [X, Y, Z] world axes (right, up,
        backward-i.e.-toward-viewer) -- screen-plane-aligned, matching how
        real-time engines actually billboard sprites (align to the camera's
        orientation), not a look-at-the-camera's-*position* rotation. The
        latter (this script's first version, and what a native Damped/
        Locked Track constraint gives) causes visible parallax skew for
        anything off-center in frame -- confirmed by actually rendering a
        real fixture and seeing a trapezoid, not a flat rectangle, before
        this fix.
      - cylindrical_lock_<axis>: `up` is *fixed* to the named world axis
        (never tilts, the entire point of "locked"); `facing` is the
        direction from the bone to the camera, projects out onto the plane
        perpendicular to that fixed axis, then normalized -- a real look-at
        (not screen-aligned) is deliberate here, restricted to yaw only,
        matching the classic "Y-locked billboard" grass/tree convention.
    """
    if mode == "spherical":
        cam_mat = camera_obj.matrix_world.to_3x3()
        right = cam_mat.col[0].normalized()
        up = cam_mat.col[1].normalized()
        facing = cam_mat.col[2].normalized()  # camera local +Z = toward the viewer, i.e. away from its view direction
        return right, up, facing

    world_letter = mode.rsplit("_", 1)[-1].upper()  # cylindrical_lock_z -> Z
    up = _WORLD_AXES[world_letter].copy()
    toward_camera = (camera_obj.matrix_world.translation - bone_head_world)
    facing = (toward_camera - toward_camera.dot(up) * up)
    if facing.length < 1e-8:
        return None  # camera sits directly on the locked axis through the bone -- no defined yaw
    facing.normalize()
    right = up.cross(facing)
    return right, up, facing


def _apply_billboard_frame(armature_obj, bone_name, right_local, up_local, facing_local, camera_obj, mode):
    """Sets `bone_name`'s pose rotation so its local [right, up, facing]
    axes map to `_billboard_target_frame_world`'s world-space frame --
    correct by direct construction (verified numerically, see the module
    docstring), not dependent on guessing a native constraint's owner/
    target-space semantics. Returns False (no-op) if the target frame is
    momentarily undefined (camera on the locked axis).
    """
    pbone = armature_obj.pose.bones[bone_name]
    bone_head_world = armature_obj.matrix_world @ pbone.matrix.translation
    target = _billboard_target_frame_world(mode, bone_head_world, camera_obj)
    if target is None:
        return False
    right_world, up_world, facing_world = target

    b_local = _local_frame_matrix(right_local, up_local, facing_local)
    m_target_world = _local_frame_matrix(right_world, up_world, facing_world)
    # b_local is orthonormal, so its inverse is its transpose -- see the
    # module docstring's derivation (R_bone_world = M_target_world @ B_local^T).
    r_bone_world = m_target_world @ b_local.transposed()

    new_world = r_bone_world.to_4x4()
    new_world.translation = bone_head_world
    pbone.matrix = armature_obj.matrix_world.inverted() @ new_world
    return True


def _update_registered_billboards(_scene=None, _depsgraph=None):
    """`depsgraph_update_post` handler body -- recomputes every registered
    billboard bone's pose from its camera's *current* transform, so moving
    the camera in an interactive viewport keeps billboards correctly
    aligned live, not just at the moment `apply_billboard_alignment` ran.
    Silently drops a registration whose armature/camera object was deleted
    since (a real possibility in an interactive session, not a batch
    render) rather than raising out of a handler, which would otherwise
    permanently break Blender's own depsgraph update cycle.
    """
    global _billboard_registry
    still_valid = []
    for armature_obj, bone_name, right_local, up_local, facing_local, camera_obj, mode in _billboard_registry:
        try:
            _apply_billboard_frame(armature_obj, bone_name, right_local, up_local, facing_local, camera_obj, mode)
            still_valid.append((armature_obj, bone_name, right_local, up_local, facing_local, camera_obj, mode))
        except ReferenceError:
            pass  # one of the objects above was deleted since registration
    _billboard_registry = still_valid


_billboard_registry = []


def apply_billboard_alignment(mesh_objs, armature_obj, camera_obj):
    """Aligns every husk-tagged billboard bone to face `camera_obj` -- see
    the module docstring's third job for the full design (why this is a
    direct per-bone matrix computation rather than a native Track
    constraint) and `_billboard_target_frame_world` for the per-mode math.

    Applied once immediately (correct for a single-frame batch render,
    `render_glb.py`'s own use); when NOT running in Blender's `--background`
    mode (an interactive session -- Luna re-opening the export in the
    normal GUI, the actual motivating use case for this whole feature),
    also registers a `depsgraph_update_post` handler so moving the camera
    keeps every billboard correctly aligned live, the same as a real
    constraint would, without depending on Blender's constraint-space
    semantics to get the screen-alignment math right.

    Returns the count of bones actually aligned.
    """
    bones = find_billboard_bones(armature_obj)
    if not bones:
        return 0
    if camera_obj is None:
        print("husk_blender_geoset_mask: no Camera object found -- add one before "
              "running billboard alignment")
        return 0

    aligned = 0
    for bone_name, mode in bones.items():
        facing = _bone_facing_axis(mesh_objs, armature_obj, bone_name)
        if facing is None:
            print(f"husk_blender_geoset_mask: billboard bone {bone_name!r} has no "
                  "weighted geometry to fit a facing axis from -- skipped")
            continue
        bone = armature_obj.data.bones[bone_name]
        up_world_hint = _WORLD_AXES[mode.rsplit("_", 1)[-1].upper()] if mode != "spherical" else _WORLD_AXES['Z']
        up = _bone_local_axis_closest_to(bone, up_world_hint, exclude=facing.lstrip("-"))
        if up is None or up.lstrip("-") == facing.lstrip("-"):
            print(f"husk_blender_geoset_mask: billboard bone {bone_name!r} ({mode}) -- "
                  "no distinct up axis found, skipped")
            continue

        right_local = _signed_axis_vector(up).cross(_signed_axis_vector(facing))
        up_local = _signed_axis_vector(up)
        facing_local = _signed_axis_vector(facing)

        if not _apply_billboard_frame(armature_obj, bone_name, right_local, up_local, facing_local, camera_obj, mode):
            print(f"husk_blender_geoset_mask: billboard bone {bone_name!r} ({mode}) -- "
                  "camera sits on its own locked axis, skipped this frame")
            continue

        _billboard_registry.append((armature_obj, bone_name, right_local, up_local, facing_local, camera_obj, mode))
        aligned += 1

    if aligned and not bpy.app.background and _update_registered_billboards not in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.append(_update_registered_billboards)

    return aligned


# --- ribbon/particle emitter placement markers ---
# `EmitterAnchor`'s id/joint/position (see `read_emitter_anchors`'s doc
# comment) is real, but husk's own extras deliberately don't carry the full
# per-emitter texture/blend/curve data (TODO/ANIMATED_TEXTURE_EFFECTS_TODO.md
# tracks the separate, much bigger task of a real particle-simulation
# reconstruction using `husk dump-chunks`'s fuller JSON output). What this
# closes instead: previously, every M2Ribbon/M2Particle-driven effect
# (weapon glow trails, magic auras, ...) was 100% invisible in Blender --
# not "wrong," just nothing there at all. A small, distinctly-shaped/
# colored, non-textured marker at each anchor's real bind-pose position,
# parented to its owning bone through animation, turns "nothing" into "an
# honest placeholder marking where a real effect attaches" -- the same
# "tag it, don't guess" incremental step this project already took for
# geoset tag joints/bone-correction extras before either had a full
# consumer, not a claim that this *is* the particle effect.

_RIBBON_MARKER_COLOR = (0.1, 0.9, 0.9, 1.0)   # cyan -- ribbons (trails)
_PARTICLE_MARKER_COLOR = (1.0, 0.3, 0.9, 1.0)  # magenta -- particles (bursts/auras)
_EMITTER_MARKER_RADIUS = 0.15  # world units (WoW yards) -- small, non-intrusive


def _emitter_marker_prototype(kind, color):
    """One shared (mesh, material) pair per `kind` ("ribbon"/"particle"),
    built once and instanced (linked mesh data, like the geoset tag joints'
    own shared-instance treatment) across every anchor of that kind --
    cheap even for a model with dozens of emitters, and exactly one
    material per kind to toggle/hide from the Outliner if unwanted, not
    one per anchor.
    """
    mesh_name = f"HuskEmitterMarker_{kind}"
    mesh = bpy.data.meshes.get(mesh_name)
    if mesh is None:
        if kind == "ribbon":
            # A flat diamond (4-gon), distinct silhouette from the
            # particle marker's sphere -- oriented in the XZ plane (glTF/
            # husk's own Y-up convention) so it reads as a small pennant/
            # trail marker rather than a ground-flat disc.
            bm_verts = [(0, _EMITTER_MARKER_RADIUS, 0), (_EMITTER_MARKER_RADIUS, 0, 0),
                        (0, -_EMITTER_MARKER_RADIUS, 0), (-_EMITTER_MARKER_RADIUS, 0, 0)]
            mesh = bpy.data.meshes.new(mesh_name)
            mesh.from_pydata(bm_verts, [], [[0, 1, 2, 3]])
            mesh.update()
        else:
            mesh = bpy.data.meshes.new(mesh_name)
            bm = bmesh.new()
            bmesh.ops.create_icosphere(bm, subdivisions=1, radius=_EMITTER_MARKER_RADIUS)
            bm.to_mesh(mesh)
            bm.free()

    mat_name = f"HuskEmitterMarker_{kind}"
    mat = bpy.data.materials.get(mat_name)
    if mat is None:
        mat = bpy.data.materials.new(mat_name)
        mat.node_tree.nodes.clear()
        emission = mat.node_tree.nodes.new("ShaderNodeEmission")
        emission.inputs["Color"].default_value = color
        emission.inputs["Strength"].default_value = 2.0
        output = mat.node_tree.nodes.new("ShaderNodeOutputMaterial")
        mat.node_tree.links.new(emission.outputs["Emission"], output.inputs["Surface"])
    return mesh, mat


def _emitter_marker_world_matrix(armature_obj, bone_name, local_offset):
    """The anchor's real bind-pose world matrix: `position` is a plain
    translation relative to its owning joint, the exact same "child-node
    translation" shape Attachment/Event/Light nodes use
    (`gltf_skeleton.cpp`'s `appendAnchorNode`) -- EmitterAnchor just never
    got materialized as a real node (see `read_emitter_anchors`'s doc
    comment), so this reproduces the same placement by direct matrix
    construction instead, same "verified by direct construction, not
    dependent on guessing a native constraint/parenting's semantics"
    approach this file's own billboard alignment already established
    (`_apply_billboard_frame`'s doc comment) -- deliberately not using
    Blender's native BONE object-parenting, whose tail-vs-head origin
    convention would need its own separate verification.
    """
    pbone = armature_obj.pose.bones[bone_name]
    return armature_obj.matrix_world @ pbone.matrix @ mathutils.Matrix.Translation(local_offset)


def _update_registered_emitter_markers(_scene=None, _depsgraph=None):
    """`depsgraph_update_post` handler body -- keeps every registered marker
    following its owning bone through animation/interactive posing, same
    pattern as `_update_registered_billboards`. Silently drops a
    registration whose objects were deleted since (an interactive-session
    possibility, not a batch render)."""
    global _emitter_marker_registry
    still_valid = []
    for armature_obj, bone_name, local_offset, marker_obj in _emitter_marker_registry:
        try:
            marker_obj.matrix_world = _emitter_marker_world_matrix(armature_obj, bone_name, local_offset)
            still_valid.append((armature_obj, bone_name, local_offset, marker_obj))
        except ReferenceError:
            pass  # one of the objects above was deleted since registration
    _emitter_marker_registry = still_valid


_emitter_marker_registry = []

_DEBUG_MARKER_COLLECTION_NAME = "Husk Debug Markers"


def _debug_marker_collection():
    """The shared collection every debug-only marker this script creates
    lives in (currently just emitter placement markers, but named/scoped
    generically since the geoset-boundary overlay/billboard work could
    plausibly want the same treatment later) -- created once, reused on
    a re-run against an already-open scene. `hide_render = True` keeps
    these out of every render pass (`render_glb.py`'s corpus previews
    included) unconditionally; `hide_viewport = True` collapses/hides it
    in the Outliner and 3D viewport by default too, so a normal File >
    Import doesn't clutter the scene with debug geometry -- both are real
    per-collection flags, not per-object, so toggling the collection's own
    eye/camera icons in the Outliner is the one place to look to bring
    these back, not a per-marker hunt.
    """
    collection = bpy.data.collections.get(_DEBUG_MARKER_COLLECTION_NAME)
    if collection is None:
        collection = bpy.data.collections.new(_DEBUG_MARKER_COLLECTION_NAME)
        bpy.context.scene.collection.children.link(collection)
    collection.hide_render = True
    collection.hide_viewport = True
    return collection


def apply_emitter_markers(armature_obj, filepath):
    """Places one placement-marker object per real `ribbon_emitters`/
    `particle_emitters` anchor (see `read_emitter_anchors`) -- returns
    `(ribbon_count, particle_count)`, both 0 if `filepath` gave no anchors
    (falsy path, no ribbon/particle emitters on this model, or a joint
    index this model's own skin doesn't have -- logged, not raised, same
    as `apply_billboard_alignment`'s per-bone skip behavior).

    Every marker lands in `_debug_marker_collection()` (hidden-by-default,
    excluded from every render pass -- see that function's own doc
    comment) and additionally gets `hide_render = True` set on the object
    itself, belt-and-suspenders: a collection-level flag stops applying
    the moment someone relinks a marker into a different collection, an
    object-level flag doesn't.
    """
    ribbon_anchors, particle_anchors, joint_bone_names = read_emitter_anchors(filepath)
    if not ribbon_anchors and not particle_anchors:
        return 0, 0

    collection = _debug_marker_collection()
    counts = {"ribbon": 0, "particle": 0}
    for kind, anchors, color in (("ribbon", ribbon_anchors, _RIBBON_MARKER_COLOR),
                                  ("particle", particle_anchors, _PARTICLE_MARKER_COLOR)):
        if not anchors:
            continue
        mesh, mat = _emitter_marker_prototype(kind, color)
        for anchor in anchors:
            joint = anchor.get("joint", -1)
            bone_name = joint_bone_names.get(joint)
            if bone_name is None or bone_name not in armature_obj.pose.bones:
                print(f"husk_blender_geoset_mask: {kind} emitter id={anchor.get('id')} -- "
                      f"joint {joint} has no matching bone, skipped")
                continue
            pos = anchor.get("position", {})
            local_offset = mathutils.Vector((pos.get("x", 0.0), pos.get("y", 0.0), pos.get("z", 0.0)))

            marker_obj = bpy.data.objects.new(f"husk_{kind}_marker_{anchor.get('id')}", mesh)
            if not marker_obj.material_slots:
                marker_obj.data.materials.append(mat)
            marker_obj.matrix_world = _emitter_marker_world_matrix(armature_obj, bone_name, local_offset)
            marker_obj.hide_render = True
            collection.objects.link(marker_obj)
            _emitter_marker_registry.append((armature_obj, bone_name, local_offset, marker_obj))
            counts[kind] += 1

    if sum(counts.values()) and not bpy.app.background and \
            _update_registered_emitter_markers not in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.append(_update_registered_emitter_markers)

    return counts["ribbon"], counts["particle"]


# --- animated texture-effect playback (ANIMATED_TEXTURE_EFFECTS_TODO.md) ---
# See the module docstring's "Fourth, independent job" section for the full
# design. Shared by both texture_transform_animation (Mapping node) and
# tint_animation/fade_animation (Principled BSDF) below.

def _to_pyobj(value):
    """Recursively converts a glTF-extras-derived Blender custom property
    into plain Python dict/list/scalar. Material extras land as real bpy
    custom properties (this file's own module docstring) -- fine as-is for
    a scalar (`mat.get("texture_type")`, used elsewhere in this file), but a
    *nested* dict/array extras value (texture_transform_animation's own
    shape: a dict of curve arrays, each curve a dict with a keyframes array
    of dicts) comes back as Blender's own IDPropertyGroup/IDPropertyArray
    wrapper types, not plain dict/list -- callers can't `.get()`/index into
    those the same way. `to_dict()` (IDPropertyGroup) and plain iteration
    (IDPropertyArray) both exist on current Blender ID-property types; this
    recurses through either uniformly so every caller below just sees plain
    Python.
    """
    if value is None:
        return None
    to_dict = getattr(value, "to_dict", None)
    if callable(to_dict):
        return {k: _to_pyobj(v) for k, v in to_dict().items()}
    if hasattr(value, "keys"):
        return {k: _to_pyobj(value[k]) for k in value.keys()}
    if isinstance(value, str):
        # Blender's glTF importer doesn't always build nested ID properties
        # for a large/deeply-nested extras value -- confirmed against a real
        # corpus fixture (creature/vicioussnaplizardmount.m2's
        # texture_transform_animation, 57 keyframe curves): past whatever
        # size/depth threshold it gives up and stores the whole value as one
        # flat string, a Python repr() of the parsed JSON (single-quoted,
        # not re-serialized JSON -- ast.literal_eval, not json.loads).
        # Without this, every caller's own `.get()`/`[...]` on what's
        # assumed to be a dict crashes with AttributeError on real,
        # non-trivial curve data -- this was a real crash, not a
        # hypothetical (render TIMEOUT/FAIL on that exact file). Falls back
        # to the plain string for a genuine short string extras value that
        # just doesn't happen to parse as a literal.
        try:
            parsed = ast.literal_eval(value)
        except (ValueError, SyntaxError):
            return value
        return _to_pyobj(parsed) if isinstance(parsed, (dict, list)) else value
    if hasattr(value, "__len__") and hasattr(value, "__getitem__"):
        return [_to_pyobj(v) for v in value]
    return value


def _lerp(a, b, t):
    return a + (b - a) * t


def _eval_scalar_curve(keyframes, t):
    """Linear-interpolates `keyframes` (a list of {"time": float, "value":
    float}, already sorted ascending by husk's own export order) at time
    `t`. Clamps to the first/last value outside the curve's own range --
    `t` is expected pre-wrapped into [0, duration] by the caller (see
    `_curve_duration`), so this only ever sees genuine in-range values in
    practice, but stays correct either way rather than indexing out of
    bounds."""
    if not keyframes:
        return 0.0
    if t <= keyframes[0]["time"]:
        return keyframes[0]["value"]
    for i in range(len(keyframes) - 1):
        t0, v0 = keyframes[i]["time"], keyframes[i]["value"]
        t1, v1 = keyframes[i + 1]["time"], keyframes[i + 1]["value"]
        if t <= t1:
            frac = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
            return _lerp(v0, v1, frac)
    return keyframes[-1]["value"]


def _eval_vec3_curve(keyframes, t):
    """Same as _eval_scalar_curve, but each keyframe's "value" is a 3-tuple
    (translation/scaling's own real shape, gltf_mesh.cpp's
    textureTransformTranslationAnimation/ScalingAnimation) -- component-wise
    lerp."""
    if not keyframes:
        return (0.0, 0.0, 0.0)
    if t <= keyframes[0]["time"]:
        return tuple(keyframes[0]["value"])
    for i in range(len(keyframes) - 1):
        t0, v0 = keyframes[i]["time"], keyframes[i]["value"]
        t1, v1 = keyframes[i + 1]["time"], keyframes[i + 1]["value"]
        if t <= t1:
            frac = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
            return tuple(_lerp(v0[k], v1[k], frac) for k in range(3))
    return tuple(keyframes[-1]["value"])


def _eval_quat_curve_z_angle(keyframes, t):
    """Evaluates a raw-quaternion rotation curve (gltf_mesh.cpp's
    textureTransformRotationAnimation, x/y/z/w per keyframe) at time `t` via
    slerp, then collapses it to a single Z-axis angle the same way
    gltf_mesh.cpp's textureTransformToKhr does for the *constant* case
    (`theta = 2*atan2(qz, qw)`) -- Blender's Mapping node only has one
    scalar UV rotation (about Z), same real limitation the constant case's
    own planarity check already documents; a genuinely 3-axis-animated
    rotation has no honest representation here either, same as there.
    """
    import math

    def to_quat(v):
        return mathutils.Quaternion((v[3], v[0], v[1], v[2]))  # Blender order: w,x,y,z

    if not keyframes:
        return 0.0
    q = to_quat(keyframes[0]["value"])
    if t > keyframes[0]["time"]:
        for i in range(len(keyframes) - 1):
            t0, v0 = keyframes[i]["time"], keyframes[i]["value"]
            t1, v1 = keyframes[i + 1]["time"], keyframes[i + 1]["value"]
            if t <= t1:
                frac = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
                q = to_quat(v0).slerp(to_quat(v1), frac)
                break
        else:
            q = to_quat(keyframes[-1]["value"])
    return 2.0 * math.atan2(q.z, q.w)


def _curve_duration(curves):
    """Real max keyframe timestamp across every curve in one track's own
    array (e.g. texture_transform_animation.translation) -- the track's
    real WoW loop period, not a guessed/standard clip length (this is the
    concrete answer to ANIMATED_TEXTURE_EFFECTS_TODO.md's "what's a good
    clip length" question: there isn't one fixed value, each model's own
    curve already carries its real duration)."""
    m = 0.0
    for c in curves or []:
        kfs = c.get("keyframes") or []
        if kfs:
            m = max(m, kfs[-1]["time"])
    return m


def _scene_seconds_per_frame():
    scene = bpy.context.scene
    fps = scene.render.fps / max(scene.render.fps_base, 1e-6)
    return (1.0 / fps) if fps else 0.0


def _extend_frame_range_for_duration(duration):
    """Grows (never shrinks) scene.frame_end so the timeline covers at
    least one full real loop of `duration` seconds at the scene's current
    frame rate -- e.g. this session's own real fixture (a 4.167s UV scroll)
    needs 100 frames at Blender's default 24fps, not a fixed "24 frames"
    clip (`round()`, not `int()`/`ceil()`, so a duration landing exactly on
    a frame boundary doesn't get bumped up a spurious extra frame)."""
    if duration <= 0:
        return
    scene = bpy.context.scene
    spf = _scene_seconds_per_frame()
    if spf <= 0:
        return
    needed_end = scene.frame_start + max(1, round(duration / spf))
    if scene.frame_end < needed_end:
        scene.frame_end = needed_end


def _find_or_build_material_uv_mapping(node_tree):
    """Finds this material's own HuskTextureTransformAnimation Mapping node
    if one already exists (idempotent across repeated calls/re-runs), or
    builds one -- wired ahead of every Image Texture node whose own
    `Vector` input isn't already linked to something else (the common shape
    Blender's stock glTF importer leaves: an Image Texture node samples the
    active UV map by default, `Vector` unlinked). Returns None if this
    material's node tree has no Image Texture node at all to drive."""
    existing = node_tree.nodes.get("HuskTextureTransformAnimation")
    if existing is not None and existing.type == 'MAPPING':
        return existing

    image_nodes = [n for n in node_tree.nodes if n.type == 'TEX_IMAGE']
    if not image_nodes:
        return None

    existing_ys = [n.location.y for n in node_tree.nodes]
    row_y = (min(existing_ys) if existing_ys else 0.0) - 400.0

    uv_map = node_tree.nodes.new("ShaderNodeUVMap")
    uv_map.location = (-800.0, row_y)
    mapping = node_tree.nodes.new("ShaderNodeMapping")
    mapping.name = "HuskTextureTransformAnimation"
    mapping.location = (-600.0, row_y)
    node_tree.links.new(uv_map.outputs["UV"], mapping.inputs["Vector"])

    for img_node in image_nodes:
        vec_in = img_node.inputs["Vector"]
        if vec_in.is_linked:
            continue  # already sourced from something else -- don't override real wiring
        node_tree.links.new(mapping.outputs["Vector"], vec_in)

    return mapping


_texture_transform_registry = []  # (mapping_node, translation_curves, rotation_curves, scaling_curves, duration)


def _update_texture_transform_animations(_scene=None, _depsgraph=None):
    """`frame_change_pre` handler body -- recomputes every registered
    material's Mapping node from the *current* scene frame, converted to
    seconds and wrapped into that curve's own real loop duration."""
    spf = _scene_seconds_per_frame()
    scene = bpy.context.scene
    t_abs = (scene.frame_current - scene.frame_start) * spf

    still_valid = []
    for mapping, translation, rotation, scaling, duration in _texture_transform_registry:
        t = (t_abs % duration) if duration > 0 else 0.0
        try:
            if translation:
                x, y, _z = _eval_vec3_curve(translation[0]["keyframes"], t)
                mapping.inputs["Location"].default_value[0] = x
                # WoW V grows downward, Blender UV V grows upward (same
                # convention the texture-layout overlay code above already
                # flips for absolute rects) -- a scroll *delta* needs the
                # same correction, differentiated: d(v_blender)/dt =
                # -d(v_wow)/dt. Fixed 2026-08-14: a real fixture with an
                # animated V scroll (borean_redplant_burningpile_01, a
                # flame texture) ran backwards without this; a real
                # fixture with an animated U-only scroll
                # (be_fountain01_base, V always 0) never exercised the bug
                # at all, which is why it looked fine.
                mapping.inputs["Location"].default_value[1] = -y
            if rotation:
                mapping.inputs["Rotation"].default_value[2] = _eval_quat_curve_z_angle(
                    rotation[0]["keyframes"], t)
            if scaling:
                x, y, _z = _eval_vec3_curve(scaling[0]["keyframes"], t)
                mapping.inputs["Scale"].default_value[0] = x
                mapping.inputs["Scale"].default_value[1] = y
        except ReferenceError:
            continue  # material/node deleted since registration -- drop it
        still_valid.append((mapping, translation, rotation, scaling, duration))
    _texture_transform_registry[:] = still_valid


def apply_texture_transform_animation(materials):
    """For every material carrying a genuinely-animated M2TextureTransform
    curve (`texture_transform_animation` extras, gltf_mesh.cpp's
    emitMaterial -- present only for a translation/rotation/scaling track
    husk couldn't fold into a real KHR_texture_transform because it isn't
    constant), builds/reuses a Mapping node driving that material's Image
    Texture node(s) and registers a `frame_change_pre` handler to keep it
    live. Returns the count of materials touched. Each curve's own real
    duration (its last keyframe's timestamp) extends scene.frame_end so
    playing the timeline through once shows exactly one real loop -- see
    _extend_frame_range_for_duration's doc comment.
    """
    touched = 0
    max_duration = 0.0
    for mat in materials:
        if mat is None or mat.node_tree is None:
            continue
        raw = mat.get("texture_transform_animation")
        if raw is None:
            continue
        anim = _to_pyobj(raw) or {}
        translation = anim.get("translation") or []
        rotation = anim.get("rotation") or []
        scaling = anim.get("scaling") or []
        if not translation and not rotation and not scaling:
            continue

        mapping = _find_or_build_material_uv_mapping(mat.node_tree)
        if mapping is None:
            print(f"husk_blender_geoset_mask: material {mat.name!r} has "
                  "texture_transform_animation extras but no Image Texture node to drive -- "
                  "skipping")
            continue

        duration = max(_curve_duration(translation), _curve_duration(rotation),
                        _curve_duration(scaling))
        max_duration = max(max_duration, duration)
        _texture_transform_registry.append((mapping, translation, rotation, scaling, duration))
        touched += 1

    if touched:
        _update_texture_transform_animations()
        if _update_texture_transform_animations not in bpy.app.handlers.frame_change_pre:
            bpy.app.handlers.frame_change_pre.append(_update_texture_transform_animations)
        _extend_frame_range_for_duration(max_duration)
    return touched


def _find_principled_bsdf(node_tree):
    return next((n for n in node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)


_tint_fade_registry = []  # (principled_node, tint_mix_node_or_None, tint_curves, alpha_curves, weight_curves, duration)


def _update_tint_fade_animations(_scene=None, _depsgraph=None):
    """`frame_change_pre` handler body, same shape as
    _update_texture_transform_animations -- see apply_tint_fade_animation's
    doc comment for why this one is unverified against real corpus data."""
    spf = _scene_seconds_per_frame()
    scene = bpy.context.scene
    t_abs = (scene.frame_current - scene.frame_start) * spf

    still_valid = []
    for principled, tint_mix, tint, alpha_curves, weight_curves, duration in _tint_fade_registry:
        t = (t_abs % duration) if duration > 0 else 0.0
        try:
            if tint:
                r, g, b = _eval_vec3_curve(tint[0]["keyframes"], t)
                if tint_mix is not None:
                    tint_mix.inputs["Color2"].default_value = (r, g, b, 1.0)
                else:
                    base = principled.inputs["Base Color"]
                    base.default_value = (r, g, b, base.default_value[3])
            if alpha_curves or weight_curves:
                alpha = _eval_scalar_curve(alpha_curves[0]["keyframes"], t) if alpha_curves else 1.0
                weight = _eval_scalar_curve(weight_curves[0]["keyframes"], t) if weight_curves else 1.0
                principled.inputs["Alpha"].default_value = alpha * weight
        except ReferenceError:
            continue  # material/node deleted since registration -- drop it
        still_valid.append((principled, tint_mix, tint, alpha_curves, weight_curves, duration))
    _tint_fade_registry[:] = still_valid


def apply_tint_fade_animation(materials):
    """Same pattern as apply_texture_transform_animation, for M2Color's
    animated tint (`tint_animation`) and M2Color::alpha/
    M2TextureWeight::weight's animated fade (`fade_animation`.alpha/.weight,
    multiplied together the same way cmd_export.cpp's static baseColorFactor
    path already does) -- see gltf::Material::tintAnimation/
    alphaFadeAnimation/weightFadeAnimation's doc comments for why core glTF
    can't play these back natively. Drives a Principled BSDF's Base Color/
    Alpha directly; when Base Color is already texture-fed, inserts a
    multiply node ahead of it instead of overriding the texture outright.

    **Not verified against a real corpus fixture with actual tint/fade
    curve data this session** -- structurally consistent with the same
    curve-eval/looping machinery apply_texture_transform_animation uses
    (which *is* real-fixture-verified), but flagged, not asserted correct,
    same discipline the texture-layout overlay's V-flip note already uses.
    """
    touched = 0
    max_duration = 0.0
    for mat in materials:
        if mat is None or mat.node_tree is None:
            continue
        tint = _to_pyobj(mat.get("tint_animation")) or []
        fade = _to_pyobj(mat.get("fade_animation")) or {}
        alpha_curves = fade.get("alpha") or []
        weight_curves = fade.get("weight") or []
        if not tint and not alpha_curves and not weight_curves:
            continue

        principled = _find_principled_bsdf(mat.node_tree)
        if principled is None:
            print(f"husk_blender_geoset_mask: material {mat.name!r} has tint/fade animation "
                  "extras but no Principled BSDF node to drive -- skipping")
            continue

        tint_mix = None
        if tint:
            base_input = principled.inputs["Base Color"]
            if base_input.is_linked:
                existing_socket = base_input.links[0].from_socket
                tint_mix = mat.node_tree.nodes.new("ShaderNodeMixRGB")
                tint_mix.name = "HuskTintAnimation"
                tint_mix.blend_type = 'MULTIPLY'
                tint_mix.inputs["Fac"].default_value = 1.0
                tint_mix.location = (principled.location.x - 200.0, principled.location.y - 200.0)
                mat.node_tree.links.new(existing_socket, tint_mix.inputs["Color1"])
                mat.node_tree.links.new(tint_mix.outputs["Color"], base_input)
            # else: Base Color is already a plain constant -- drive it directly, no extra node.

        duration = max(_curve_duration(tint), _curve_duration(alpha_curves),
                        _curve_duration(weight_curves))
        max_duration = max(max_duration, duration)
        _tint_fade_registry.append((principled, tint_mix, tint, alpha_curves, weight_curves, duration))
        touched += 1

    if touched:
        _update_tint_fade_animations()
        if _update_tint_fade_animations not in bpy.app.handlers.frame_change_pre:
            bpy.app.handlers.frame_change_pre.append(_update_tint_fade_animations)
        _extend_frame_range_for_duration(max_duration)
    return touched


def _run_stage(model_name, stage_name, fn):
    """Runs one main() stage in isolation -- a real robustness gap flagged
    directly in ANIMATED_TEXTURE_EFFECTS_TODO.md before this session added a
    4th/5th stage to this script: several independent stages used to share
    one try-less main() body, so an exception in an early stage (e.g. the
    geoset switch) killed every later stage silently, including ones
    completely unrelated to whatever failed. Prints a loud, specific
    failure (model name, stage name, real exception text) and returns None
    on failure so the caller can tell "ran, returned nothing interesting"
    apart from "never ran at all" if it needs to.
    """
    try:
        return fn()
    except Exception as exc:  # noqa: BLE001 -- deliberately broad, see doc comment
        print(f"husk_blender_geoset_mask: FAILED stage {stage_name!r} for {model_name!r}: "
              f"{type(exc).__name__}: {exc}")
        return None


def main():
    argv = sys.argv
    filepath = None
    if "--" in argv:
        extra_args = argv[argv.index("--") + 1:]
        if extra_args:
            filepath = extra_args[0]
            bpy.ops.import_scene.gltf(filepath=filepath)

    mesh_objs, armature_obj = find_mesh_and_armature()
    model_name = mesh_objs[0].name if mesh_objs else (filepath or "<unknown>")
    materials = {obj.material_slots[i].material
                 for obj in mesh_objs
                 for i in range(len(obj.material_slots))
                 if obj.material_slots[i].material is not None}

    def geoset_stage():
        enabled_geosets = read_enabled_geosets(filepath)
        extra_default_overrides = enabled_geosets_to_default_overrides(enabled_geosets)
        all_groups, switch_groups, removed = apply_geoset_switches(
            mesh_objs, armature_obj, extra_default_overrides)
        print(f"husk_blender_geoset_mask: {len(all_groups)} geoset group(s) across "
              f"{len(mesh_objs)} mesh object(s), {switch_groups} dropdown switch(es) built, "
              f"{removed} tag bone(s) removed")
        if extra_default_overrides:
            print(f"husk_blender_geoset_mask: {len(extra_default_overrides)} group default(s) "
                  f"driven by real enabled_geosets extras ({len(enabled_geosets)} customization "
                  "choice(s) resolved at export time), not the curated/lowest-variant fallback")

    def billboard_stage():
        billboard_bones = find_billboard_bones(armature_obj)
        if not billboard_bones:
            return
        constrained = apply_billboard_alignment(mesh_objs, armature_obj, find_camera_object())
        print(f"husk_blender_geoset_mask: {constrained}/{len(billboard_bones)} billboard "
              "bone(s) got a camera-facing constraint")

    def texture_layout_overlay_stage():
        layout = read_chr_texture_layout(filepath)
        if layout is None:
            print("husk_blender_geoset_mask: no chr_texture_layout extras found "
                  "(no --char-layout-id given at export time, or no file path given here) -- "
                  "skipping the texture-layout overlay")
            return
        touched = apply_texture_layout_overlay(layout, materials)
        print(f"husk_blender_geoset_mask: chr_texture_layout {layout.get('layout_id')} -- "
              f"{touched} material(s) got a toggleable section-boundary overlay "
              f"(off by default; enable 'Show Overlay' on the HuskChrTextureLayoutOverlay "
              f"node in the Shader Editor)")

    def texture_transform_animation_stage():
        touched = apply_texture_transform_animation(materials)
        if touched:
            print(f"husk_blender_geoset_mask: {touched} material(s) got a real animated UV "
                  "transform (texture_transform_animation) -- driven live from the current "
                  "scene frame, scene.frame_end extended to fit its real loop duration")

    def tint_fade_animation_stage():
        touched = apply_tint_fade_animation(materials)
        if touched:
            print(f"husk_blender_geoset_mask: {touched} material(s) got real animated tint/fade "
                  "(tint_animation/fade_animation) -- driven live from the current scene frame; "
                  "NOT verified against a real corpus fixture with actual data this session, see "
                  "apply_tint_fade_animation's own doc comment")

    def emitter_marker_stage():
        ribbon_count, particle_count = apply_emitter_markers(armature_obj, filepath)
        if ribbon_count or particle_count:
            print(f"husk_blender_geoset_mask: {ribbon_count} ribbon + {particle_count} particle "
                  "emitter placement marker(s) added -- placeholders, not a real particle-effect "
                  "simulation, see apply_emitter_markers's own doc comment")

    _run_stage(model_name, "geoset switch", geoset_stage)
    _run_stage(model_name, "billboard alignment", billboard_stage)
    _run_stage(model_name, "texture-layout overlay", texture_layout_overlay_stage)
    _run_stage(model_name, "texture-transform animation", texture_transform_animation_stage)
    _run_stage(model_name, "tint/fade animation", tint_fade_animation_stage)
    _run_stage(model_name, "emitter placement markers", emitter_marker_stage)


if __name__ == "__main__":
    main()
