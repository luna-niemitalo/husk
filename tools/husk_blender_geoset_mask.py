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
(imports model.glb first -- still the way to tell the script which model
to load when running standalone, and `--textures <dir>`'s own default
still comes from it), or with no trailing args, operates on whatever
mesh+armature objects are already in the current scene (a model already
imported via Blender's own File > Import, or the Scripting tab). Either
way, once the scene is populated, **no file path is needed for anything
this script itself reads back** -- every skin-level extras key this
project attaches (`chr_texture_layout`,
`chr_customization_options`, `chr_enabled_materials`, `enabled_geosets`,
`ribbon_emitters`/`particle_emitters`, `physics_bodies`/`physics_joints`,
...) reads straight from the already-imported scene. Blender's stock glTF
importer has no supported extras target for a glTF *skin* itself
(confirmed empirically: node/mesh/material/camera/light/scene extras all
land as real Blender custom properties post-import; skin extras land
nowhere -- and neither do animation-level extras, also confirmed directly,
not assumed), so `gltf_skeleton.cpp` attaches all of it to the skin's own
root joint's node extras instead -- which *does* survive import as a real
custom property on that Bone, deeply nested structures included (confirmed
directly via a headless Blender round-trip). `_root_joint_extras` reads it
back. A raw numeric bone index (e.g. a ribbon/particle emitter's own
`joint` field) used to need a separate raw-glTF-JSON re-parse to resolve
to the actual Blender bone name -- confirmed directly that Blender's own
post-import bone order does *not* match the raw glTF joint-index order
(242/358 mismatches on a real 245-bone character) -- until husk itself
started resolving it at export time (`bone_name`, alongside the raw
`joint` index, on every entry that carries one; the same real name list
also travels as `joint_names` on the root joint extras, for
`_root_joint_extras`'s own unambiguous-carrier-detection use). No raw
file, no index/name table a caller has to carry around and join
themselves -- every joint reference is already a real name.

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
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

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


def _unique_label(label, taken):
    """`label`, or `label` disambiguated against `taken` (a set of already-
    used labels) by appending a running counter -- guards the unexpected-
    but-not-impossible case of two real choice names colliding (e.g. two
    different options both offering a choice literally named "None").
    """
    if label not in taken:
        return label
    n = 2
    while f"{label} ({n})" in taken:
        n += 1
    return f"{label} ({n})"


def _build_group_hidden_term(node_tree, group_input, group, variants, default_item=None,
                              choice_names=None, group_label=None):
    """One geoset group's contribution to the overall "hidden" expression --
    see this module's own doc comment, point 4, for the shape. `variants`
    is `[(variant, vg_name), ...]`, lowest variant first. Returns a boolean
    output socket: true for a vertex that belongs to this group but isn't
    part of its currently-selected choice. `default_item` (a real
    `"variant_<n>"` or `"none"`) overrides the default dropdown selection
    when given and valid for this group's own real item set -- see
    `CURATED_DEFAULT_VARIANTS`; falls back to the lowest real variant ID
    otherwise, same as before that table existed. `choice_names`
    (`{variant: real choice name}`, from `build_geoset_choice_names`) and
    `group_label` (that group's real option name) label the dropdown with
    real, human-readable strings (e.g. "Short Fin" instead of "variant_7")
    wherever real DB2 customization-choice data resolved one for this
    exact group/variant -- variants with no matching real choice keep the
    plain numeric label. The dropdown item's own internal Blender name
    (`NodeEnumItem.name`, which doubles as both identifier and displayed
    label -- there's no separate id/label pair on this node type) becomes
    the human label when one exists; `default_item`/`CURATED_DEFAULT_VARIANTS`
    stay in the stable `"variant_<n>"` string format regardless (resolved
    to the matching item via `label_by_variant` below), so switching a
    model's own dropdown to human names never breaks a hand-curated or
    DB2-derived default.
    """
    choice_names = choice_names or {}
    owns_group = _or_all(node_tree, [_named_attr_gt_zero(node_tree, vg_name) for _, vg_name in variants])

    selector = node_tree.nodes.new('GeometryNodeMenuSwitch')
    selector.data_type = 'STRING'
    selector.label = f"{group_label} (group {group} selector)" if group_label else f"Geoset group {group} (selector)"
    # Fresh nodes start with two placeholder items ("A"/"B") that have to
    # be cleared before adding real ones, or they'd linger as two extra,
    # unwired, meaningless dropdown entries.
    selector.enum_definition.enum_items.clear()
    item_names = []
    label_by_variant = {}
    for variant, vg_name in variants:
        label = _unique_label(choice_names.get(variant) or f"{VARIANT_PREFIX}{variant}", item_names)
        selector.enum_definition.enum_items.new(label)
        selector.inputs[label].default_value = vg_name
        item_names.append(label)
        label_by_variant[variant] = label
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
    socket_name = f"{group_label} (group {group})" if group_label else f"Geoset group {group}"
    menu_socket = node_tree.interface.new_socket(
        name=socket_name, in_out='INPUT', socket_type='NodeSocketMenu')
    node_tree.links.new(group_input.outputs[menu_socket.name], selector.inputs[0])
    valid_items = item_names + [NONE_ITEM_NAME]

    default_label = None
    if default_item == NONE_ITEM_NAME:
        default_label = NONE_ITEM_NAME
    elif default_item is not None and default_item.startswith(VARIANT_PREFIX):
        try:
            default_label = label_by_variant.get(int(default_item[len(VARIANT_PREFIX):]))
        except ValueError:
            default_label = None
    if default_label is not None and default_label in valid_items:
        menu_socket.default_value = default_label
    else:
        menu_socket.default_value = item_names[0]  # lowest real variant ID visible by default

    is_selected = _named_attr_gt_zero(node_tree, selector.outputs[0])
    not_selected = _bool_math(node_tree, 'NOT', is_selected)
    return _bool_math(node_tree, 'AND', owns_group, not_selected)


def build_geoset_switch_node_group(name, groups, default_overrides=None, chr_customization_options=None):
    """One GeometryNodeTree implementing the whole group/variant switch --
    see this module's own doc comment for the full shape. `groups` is
    `geoset_groups(mesh_obj)`'s own return value. `default_overrides` is
    `CURATED_DEFAULT_VARIANTS`'s own per-model `{group: item_name}` dict, or
    None/empty for the plain lowest-variant default. `chr_customization_options`
    (`read_chr_customization_options`'s own return shape, or None) supplies
    real human names for the dropdown -- see `build_geoset_choice_names`/
    `_build_group_hidden_term`'s own doc comments; absent entirely for
    creature models or when no --db2-dir/--dbd-dir was given at export
    time, in which case every group keeps its plain numeric label, same as
    before this parameter existed. Builds one combined
    "is this vertex hidden" boolean expression across every multi-variant
    group first (no geometry operations at all), then applies exactly one
    `Separate Geometry` to the pristine input mesh at the very end --
    deliberately not a chain of per-variant separations (the design this
    replaced), which real use found could lose geometry at compounding
    selection boundaries across up to 109 sequential operations.
    """
    default_overrides = default_overrides or {}
    variant_names, group_option_names = build_geoset_choice_names(chr_customization_options)
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
                                                       default_overrides.get(group),
                                                       choice_names=variant_names.get(group),
                                                       group_label=group_option_names.get(group)))

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


def apply_geoset_switch(mesh_obj, extra_default_overrides=None, chr_customization_options=None):
    """Builds the node group and adds it as a Geometry Nodes modifier;
    returns this mesh object's own `geoset_groups` result. Looks up
    `CURATED_DEFAULT_VARIANTS` by this mesh object's own real name (see
    `_model_key`) to pick real, hand-verified defaults over the plain
    lowest-variant fallback, when available for this model.
    `extra_default_overrides` (`enabled_geosets_to_default_overrides`'s own
    return shape, or None) layers on top of and takes priority over
    `CURATED_DEFAULT_VARIANTS` -- real DB2-resolved data from a specific
    character's own customization choices is a stronger signal than a
    hand-picked, model-wide curated guess. `chr_customization_options`:
    see `build_geoset_switch_node_group`'s own doc comment.
    """
    groups = geoset_groups(mesh_obj)
    if not groups:
        return groups
    default_overrides = {**CURATED_DEFAULT_VARIANTS.get(_model_key(mesh_obj.name), {}),
                          **(extra_default_overrides or {})}
    node_tree = build_geoset_switch_node_group(f"{mesh_obj.name}_geoset_switch", groups, default_overrides,
                                                chr_customization_options)
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


def apply_geoset_switches(mesh_objs, armature_obj, extra_default_overrides=None, chr_customization_options=None):
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
        groups = apply_geoset_switch(mesh_obj, extra_default_overrides, chr_customization_options)
        switch_groups += sum(1 for gid, variants in groups.items()
                              if switchable_variants(gid, variants))
        for gid, variants in groups.items():
            all_groups.setdefault(gid, []).extend(variants)
    removed = delete_geoset_tag_bones(armature_obj, all_groups)
    return all_groups, switch_groups, removed


def _root_joint_extras(armature_obj):
    """The husk-exported skin's own root joint's custom properties, as a
    plain dict -- where `chr_texture_layout`/`enabled_geosets`/etc. extras
    actually live after Blender's own import. Blender's stock glTF importer
    has no supported extras target for a glTF *skin* at all (confirmed
    empirically -- see the module docstring), so `gltf_skeleton.cpp`
    attaches this data to the skin's first real root joint's node extras
    instead: unlike skin extras, node/bone extras *do* survive import as a
    real custom property, deeply nested arrays/objects included (confirmed
    directly via a headless Blender round-trip).

    Doesn't assume *which* imported bone is that root -- Blender's own
    post-import bone order doesn't reliably match the raw glTF joint-index
    order (confirmed directly: 242/358 mismatches on a real 245-bone
    character), so there's no safe index to hardcode. Instead scans for
    whichever bone actually carries `joint_names`
    (`gltf_skeleton.cpp`'s own always-present marker -- real structural
    data, not an optional anchor list, so exactly one bone ever carries it,
    unlike e.g. `billboard`, which can legitimately land on a different,
    non-carrier joint and would give a false match). Returns {} when
    `armature_obj` is falsy or no bone carries it (e.g. a non-husk glTF).

    Real bug found and fixed via a headless Blender crash reproduction,
    not assumed: a bone's own `IDPropertyGroup`/nested-list custom
    property values are *live views* into Blender's own storage, not
    independent copies -- `{key: bone[key] for key in bone.keys()}` alone
    still holds live references. Deleting *any* bone afterward (even a
    wholly unrelated one, e.g. `delete_geoset_tag_bones` removing a
    geoset tag bone) can invalidate those references and segfault
    Blender on next access -- reproduced directly with a minimal repro
    (fetch this property, delete an unrelated bone, touch the earlier
    reference again: real crash, `Writing: .../scene.crash.txt`).
    `_deep_copy_id_property` below walks the whole structure into plain
    Python dicts/lists/primitives before returning, so every caller holds
    genuinely independent data, safe across any later scene mutation --
    the same safety guarantee the old raw-glTF-JSON-parse approach always
    had for free (a real `json.loads` result is never a live view into
    anything).
    """
    if armature_obj is None:
        return {}
    for bone in armature_obj.data.bones:
        if "joint_names" in bone.keys():
            return {key: _deep_copy_id_property(bone[key]) for key in bone.keys()}
    return {}


def _deep_copy_id_property(value):
    """Recursively converts a Blender custom-property value (an
    `IDPropertyGroup` for a nested object, a plain `list` for a nested
    array -- confirmed directly, not assumed, that Blender already
    returns arrays as real Python `list`s, just with `IDPropertyGroup`
    elements/values still live inside) into an independent structure of
    plain `dict`/`list`/primitives. See `_root_joint_extras`'s own doc
    comment for why this matters -- without it, a caller holds a
    reference that can be invalidated (and crash Blender on next access)
    by an unrelated later bone deletion.
    """
    if type(value).__name__ == "IDPropertyGroup":
        return {key: _deep_copy_id_property(value[key]) for key in value.keys()}
    if isinstance(value, list):
        return [_deep_copy_id_property(v) for v in value]
    return value


def _joint_bone_names_from_extras(armature_obj):
    """`{joint_index: bone_name}` from the already-imported armature's own
    root-joint `joint_names` extras (see `_root_joint_extras`) -- no file
    path needed. `joint_names[i]` is exactly the name husk gave
    `skin.joints[i]`'s own node at export time (billboard suffix included),
    the same real name Blender's importer will have used for that bone.
    Empty dict when `armature_obj` is falsy or carries no `joint_names`
    (e.g. a `.glb` exported before this field existed).
    """
    names = _root_joint_extras(armature_obj).get("joint_names")
    if not names:
        return {}
    return dict(enumerate(names))


def read_chr_texture_layout(armature_obj):
    """Reads `chr_texture_layout` from the already-imported armature's own
    root-joint extras (see `_root_joint_extras`) -- no file path needed.
    Returns None if `armature_obj` is falsy or genuinely has no such extras
    (no --char-layout-id was given at export time) -- never a guess, never
    a hard failure of the rest of this script's work.
    """
    return _root_joint_extras(armature_obj).get("chr_texture_layout")


def read_enabled_geosets(armature_obj):
    """Reads `enabled_geosets` from the already-imported armature's own
    root-joint extras (see `_root_joint_extras`) -- no file path needed.
    Returns a list of `{"choice_id": int, "geoset_id": int}` dicts, or None
    if `armature_obj` is falsy or genuinely has no such extras (no
    `--customization-choice-ids` was given at export time, or none of the
    given choice IDs resolved a geoset) -- never a guess.
    """
    return _root_joint_extras(armature_obj).get("enabled_geosets")


def read_chr_enabled_materials(armature_obj):
    """Reads `chr_enabled_materials` from the already-imported armature's
    own root-joint extras (see `_root_joint_extras`) -- no file path
    needed. Returns a list of `{"choice_id", "chr_model_texture_target_id",
    "material_resources_id", "file_data_id"}` dicts, or None if
    `armature_obj` is falsy or genuinely has no such extras (no
    `--customization-choice-ids`/`--chr-model-id` resolved any material at
    export time) -- never a guess. `file_data_id` may be `0` on an entry
    that couldn't resolve a real texture (e.g. a swatch-color-only choice,
    see TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md's own open questions) --
    callers must check for it, not assume every entry is usable.
    """
    return _root_joint_extras(armature_obj).get("chr_enabled_materials")


def read_chr_customization_options(armature_obj):
    """Reads `chr_customization_options` from the already-imported
    armature's own root-joint extras (see `_root_joint_extras`) -- no file
    path needed. The full real `(Option, Choice)` menu for the model, not
    just whichever choice(s) ended up in `chr_enabled_materials`/
    `enabled_geosets` above. Returns None if `armature_obj` is falsy or
    genuinely has no such extras (no derivable `ChrModelID` at export
    time) -- never a guess. See `TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`
    for the exact real shape of each entry.
    """
    return _root_joint_extras(armature_obj).get("chr_customization_options")


def read_animation_clip_names(armature_obj):
    """`{glTF animation name: real AnimationData.db2 name or None}` for
    every clip in the current Blender file -- no file path needed.
    `animation_data_names` (real names, only for clips that actually
    resolved one) lives on the already-imported armature's own root-joint
    extras (see `_root_joint_extras`) rather than each animation's own
    `sequence_metadata.animation_data_name` extras: confirmed directly,
    not assumed, that Blender's glTF importer drops animation-level
    extras too (an imported Action's own custom properties come back
    empty even when the source file's `animations[].extras` was set), the
    same gap `_root_joint_extras` already works around for skin-level
    extras. Every clip name still comes from `bpy.data.actions` itself
    (the imported Action list), not from this dict -- `animation_data_names`
    is deliberately sparse (present only for clips with a real resolved
    name, same "absence means ordinary" convention as everywhere else in
    this pipeline), so it alone can't answer "which clips exist".

    A clip's own value is None (present as a key, but unresolved) when no
    --db2-dir/--dbd-dir was given at export time, or this clip's id has no
    real AnimationData.db2 row -- current real local extractions
    (2026-08-21) have no Name column left in AnimationData.db2 at all
    (dropped from the client's own schema at some point after 8.x, see
    animationdata_db2.hpp's own doc comment), so every real husk export
    today resolves this to None for every clip; the field/plumbing is
    still real and exercised end to end via a synthetic test fixture
    (tests/test_cli_animationdata.cpp) that does carry a Name column.
    """
    resolved = _root_joint_extras(armature_obj).get("animation_data_names") or {}
    return {action.name: resolved.get(action.name) for action in bpy.data.actions}


def mark_actions_as_assets(clip_names):
    """Marks every real husk-exported Action (matched by `clip_names`, see
    `read_animation_clip_names`) as a Blender asset, so Blender's own
    Asset Browser becomes a real per-animation picker -- husk's clips are
    otherwise only reachable by scrubbing the raw Action list or an NLA
    track by machine name (`anim_<id>_<variationIndex>`/`global_seq_<n>`).
    A real AnimationData.db2 name (when `read_animation_clip_names`
    resolved one) becomes the Action's own Blender name too -- this is
    the one place in this whole pipeline that *renames* rather than just
    annotates, deliberately: an Action's own name is exactly what both the
    Asset Browser and the plain Action list show, so a human name has to
    live there to be visible at all, unlike every other enrichment here
    (geoset/customization names), which labels a dropdown item rather
    than renaming anything real. The original machine name survives as
    the asset's own description, so "which real anim_<id> was this" is
    never lost even after a rename. Returns the number of actions marked.
    """
    marked = 0
    for clip_name, human_name in clip_names.items():
        action = bpy.data.actions.get(clip_name)
        if action is None:
            continue
        if not action.asset_data:
            action.asset_mark()
        action.asset_data.description = f"husk clip: {clip_name}"
        if human_name and action.name != human_name:
            action.name = human_name
        marked += 1
    return marked


def read_emitter_anchors(armature_obj):
    """Reads `ribbon_emitters`/`particle_emitters` from the already-imported
    armature's own root-joint extras (see `_root_joint_extras`) -- no file
    path needed. See gltf_skeleton.hpp's `Skeleton::EmitterAnchor` doc
    comment: these are deliberately *not* real glTF child nodes (unlike
    Attachment/Event/Light) despite sharing the exact same "translation
    relative to an owning joint" shape, because a model can carry dozens of
    them each with several would-be animation curves -- too high-volume to
    embed per-.glb, so only the minimal id/joint/position anchor lives
    here; the full per-emitter data (texture, blend mode, curves) lives in
    `husk dump-chunks`'s separate JSON output instead, out of this
    function's reach (and this script's current scope -- see
    `apply_emitter_markers`).

    Each anchor's own real Blender bone name is already resolved by husk
    itself (`bone_name`, alongside the raw `joint` index it was resolved
    from) -- `gltf_skeleton.cpp` does this at export time now, so no
    Blender-side index/name-table join is needed here at all (an earlier
    version of this function did that join itself, using a separate
    `joint_names` lookup table this same root-joint extras also still
    carries for other purposes -- see `_root_joint_extras`'s own doc
    comment).

    Returns `(ribbon_anchors, particle_anchors)`, lists of `{"id", "joint",
    "bone_name", "position": {"x","y","z"}}` dicts (empty, not None, when
    absent -- these are usually-present, not opt-in-flag-gated the way
    chr_texture_layout/enabled_geosets are).
    """
    extras = _root_joint_extras(armature_obj)
    ribbon_anchors = extras.get("ribbon_emitters") or []
    particle_anchors = extras.get("particle_emitters") or []
    return ribbon_anchors, particle_anchors


def read_physics_bodies(armature_obj):
    """Reads `physics_bodies`/`physics_joints`/`joint_names` from the
    already-imported armature's own root-joint extras (see
    `_root_joint_extras`) -- no file path needed. See
    `gltf::Skeleton::PhysicsBody`/`PhysicsJoint`'s own doc comments
    (`gltf_skeleton.hpp`) for the exact reduced shape of each: `physics_
    bodies` is `{"id", "joint", "position": {"x","y","z"}, "body_type"}`
    per real `.phys` body (`id` is that body's own index into the source
    `.phys` file's BODY array, the join key `physics_joints` uses); `physics_
    joints` is `{"body_a", "body_b", "frequency_hz", "damping_ratio",
    "swing_limit_deg"}` per real `.phys` joint -- both travel inside the
    `.glb` itself, no separate `husk dump-chunks` call needed (see this
    section's own module comment on why an earlier version needed one and
    doesn't anymore). `joint_bone_names` resolves a body's raw `joint`
    (an index into `skin.joints`, i.e. a raw M2 bone index) to the real
    Blender bone name, via `_joint_bone_names_from_extras` -- same
    `joint_names` extras `read_emitter_anchors` reads, no raw file needed
    for that either anymore.

    Returns `(physics_bodies, physics_joints, joint_bone_names)` --
    `physics_bodies`/`physics_joints` are `[]`, not None, when the model
    was exported without `--phys`.
    """
    extras = _root_joint_extras(armature_obj)
    physics_bodies = extras.get("physics_bodies") or []
    physics_joints = extras.get("physics_joints") or []
    joint_bone_names = _joint_bone_names_from_extras(armature_obj)
    return physics_bodies, physics_joints, joint_bone_names


def read_gear_items(armature_obj):
    """Reads `gear_items` (`husk export --appearance`'s 'gear' field, case 1
    -- standalone-geometry equipped items: weapons/shields/shoulders/helms)
    from the already-imported armature's own root-joint extras (see
    `_root_joint_extras`) -- no file path needed. Returns a list of
    `{"slot", "item_modified_appearance_id", "model_file_data_ids",
    "materials"}` dicts (see `gltf::Skeleton::GearItem`, `gltf_skeleton.hpp`),
    or `[]` if `armature_obj` is falsy or genuinely has no such extras (no
    `--appearance` with a real 'gear' entry resolving case-1 data was given
    at export time) -- never a guess. `model_file_data_ids` may be empty on
    an entry with only case-2 (section-overlay) data -- callers must check.
    """
    return _root_joint_extras(armature_obj).get("gear_items") or []


# husk-appearance/1's own SLOT token is caller-defined and NOT validated
# against Blizzard's own equipment-slot enum (src/appearance_string.hpp's
# own doc comment: "an opaque uppercase token... husk's grammar does not
# hardcode Blizzard's equipment-slot name enum") -- this table is *this
# script's own* convention, mapping the common WoW inventory-slot names a
# caller is most likely to use onto the real M2Attachment id(s)
# (documentation/wowdev-wiki/md/M2.md's "Attachments" section table,
# confirmed against real husk-exported data: every base character export
# carries these as real named `attachment_<id>` child objects
# unconditionally, regardless of gear -- see `apply_gear_items`'s own doc
# comment). A slot mapping to more than one id (SHOULDER) gets one real
# imported copy of the item parented at each id -- WoW mirrors a shoulder
# piece onto both real attachment points, not one. RANGED has no separate
# real attachment id of its own in the wowdev.wiki table (ranged weapons
# share HandRight with melee mainhand weapons in practice) -- a real,
# checkable gap, not an oversight. A slot token not in this table is
# reported and skipped, never guessed at.
GEAR_SLOT_TO_ATTACHMENT_IDS = {
    "MAINHAND": [1],    # HandRight / ItemVisual1
    "HAND": [1],
    "OFFHAND": [2],     # HandLeft / ItemVisual2
    "SHIELD": [0],      # Shield / MountMain / ItemVisual0
    "RANGED": [1],      # best-effort: shares HandRight, no dedicated real id
    "HEAD": [11],       # Helm
    "HELM": [11],
    "SHOULDER": [5, 6],  # ShoulderRight + ShoulderLeft
    "SHOULDERS": [5, 6],
    "BACK": [12],       # Back
    "CLOAK": [12],
    "CAPE": [12],
    "CHEST": [34],      # Chest
    "WAIST": [53],      # BeltBuckle
    "BELT": [53],
}


def _import_gltf_top_level_objects(filepath):
    """Imports `filepath` via Blender's stock glTF importer and returns
    just the newly-created top-level objects (`.parent is None`) --
    diffed against `bpy.data.objects` before/after the import call, since
    `bpy.ops.import_scene.gltf` itself doesn't hand back a usable object
    list. A real item export can bring in more than one top-level object
    (e.g. a real armature alongside its mesh, for a rare skinned prop) --
    every one of them needs to move together, so the caller wraps them
    all under one new parent Empty rather than assuming exactly one.
    """
    before = set(bpy.data.objects.keys())
    bpy.ops.import_scene.gltf(filepath=filepath)
    after = set(bpy.data.objects.keys())
    new_names = after - before
    return [obj for name in new_names if (obj := bpy.data.objects.get(name)) is not None
            and obj.parent is None]


def apply_gear_items(armature_obj, gear_items, main_glb_path):
    """Case 1 (TODO/EQUIPPED_GEAR_RENDER_TODO.md) -- turns each real
    `gear_items` entry (`read_gear_items`) into a real second `.glb`
    imported and parented to the base character's own real
    `attachment_<id>` node. Mechanically simple by design (per Luna's own
    framing: "one correctly-set-up positional constraint/parent
    relationship relative to the armature"): `attachment_<id>` objects are
    already real Empties, bone-parented (`parent_type='BONE'`) to the
    correct real bone with the real `M2Attachment::position` offset baked
    into their own `.location` (confirmed directly via a headless
    round-trip against a real fixture, `bloodelffemale.m2`) -- so parenting
    an imported item's own root object(s) to that Empty at local-space
    origin reproduces correct in-game placement with no second manual
    offset, exactly the way the real client attaches an item model at its
    own authored origin to the resolved attachment point.

    Each entry's own `aux_glb_path` -- a path RELATIVE to `main_glb_path`'s
    own directory -- is resolved and imported directly
    (`_import_gltf_top_level_objects`); no listfile, no `husk` subprocess,
    no FileDataID resolution happens on this side at all. husk itself
    already resolved the item's real FileDataID -> local `.m2` path (via
    its own `--listfile`/`--listfile-root`) and exported its `.glb` to
    `<main .glb's own dir>/aux_models/...` at `husk export --appearance`
    time (`exportGearAuxItemModels`, `src/cmd_export.cpp`) -- same
    "husk resolves/prepares, Blender-side script only reads back
    already-baked extras" discipline `_root_joint_extras` established for
    every other extras field here (this project's own standing "no
    external file-path knowledge beyond what's baked into extras or found
    by a fixed relative-path convention" rule). Parents one copy per real
    `attachment_<id>` object this slot maps to (`GEAR_SLOT_TO_ATTACHMENT_IDS`)
    that actually exists on this character's own skeleton (a real, common
    case: not every model has every attachment id -- reported and skipped
    per missing id, not fatal to the rest). Returns the number of gear
    items that got at least one real attachment placed.
    """
    if not gear_items:
        return 0
    if armature_obj is None:
        print("husk_blender_geoset_mask: gear_items present but no armature in the scene -- "
              "skipping gear attachment")
        return 0
    main_glb_dir = os.path.dirname(os.path.abspath(main_glb_path)) if main_glb_path else None

    attached = 0
    for item in gear_items:
        slot = (item.get("slot") or "").upper()
        appearance_id = item.get("item_modified_appearance_id")
        aux_glb_rel = item.get("aux_glb_path")
        if not aux_glb_rel:
            print(f"husk_blender_geoset_mask: gear slot '{slot}' (appearance "
                  f"{appearance_id}) has no aux_glb_path -- husk didn't export this item's own "
                  "geometry (missing --listfile/--listfile-root at export time, no listfile "
                  "entry for its model FileDataID, or a genuine DB2/case-2-only gap) -- skipping")
            continue
        if main_glb_dir is None:
            print(f"husk_blender_geoset_mask: gear slot '{slot}' has a real aux_glb_path but "
                  "this script wasn't given the main .glb's own file path -- can't resolve the "
                  "relative aux_models path, skipping (run with '-- <file.glb>', not a bare "
                  "already-imported scene)")
            continue

        attachment_ids = GEAR_SLOT_TO_ATTACHMENT_IDS.get(slot)
        if not attachment_ids:
            print(f"husk_blender_geoset_mask: gear slot '{slot}' isn't in this script's own "
                  "SLOT -> M2Attachment-id table (GEAR_SLOT_TO_ATTACHMENT_IDS) -- skipping, "
                  "not guessed at")
            continue

        item_glb = os.path.join(main_glb_dir, aux_glb_rel)
        if not os.path.isfile(item_glb):
            print(f"husk_blender_geoset_mask: gear slot '{slot}' aux_glb_path '{aux_glb_rel}' "
                  f"doesn't exist under '{main_glb_dir}' -- skipping (moved the .glb without its "
                  "aux_models/ sibling directory?)")
            continue

        placed_any = False
        for attachment_id in attachment_ids:
            attachment_obj = bpy.data.objects.get(f"attachment_{attachment_id}")
            if attachment_obj is None:
                print(f"husk_blender_geoset_mask: gear slot '{slot}' wants attachment_"
                      f"{attachment_id}, but this character's own skeleton has no such "
                      "attachment node -- skipping this attachment point (real and common: "
                      "not every model carries every M2Attachment id)")
                continue

            new_objs = _import_gltf_top_level_objects(item_glb)
            if not new_objs:
                print(f"husk_blender_geoset_mask: importing '{item_glb}' for gear slot "
                      f"'{slot}' produced no new top-level object -- skipping")
                continue

            carrier = bpy.data.objects.new(f"gear_{slot.lower()}_att{attachment_id}", None)
            bpy.context.collection.objects.link(carrier)
            carrier.parent = attachment_obj
            carrier.location = (0.0, 0.0, 0.0)
            for obj in new_objs:
                obj.parent = carrier
                obj.location = (0.0, 0.0, 0.0)
            placed_any = True

        if placed_any:
            attached += 1

    return attached


def build_geoset_choice_names(chr_customization_options):
    """`chr_customization_options` (`read_chr_customization_options`'s own
    return shape) -> `({group: {variant: choice_name}}, {group: option_name})`
    -- real DB2-resolved names for the exact geoset group/variant numbers
    `geoset_groups` already parses off each mesh object's own vertex group
    names (`choice["geoset_id"] // 100`/`% 100` is the same inverse
    `enabled_geosets_to_default_overrides` below already uses). Groups/
    variants with no matching real customization choice (creature models,
    or a group with no player-facing customization at all -- e.g. group 0's
    base body) are simply absent from either dict; callers fall back to the
    plain numeric label for those. A group fed by choices from more than
    one distinct real option (not expected, not verified against real
    data) is left out of the second dict rather than picking one option
    name arbitrarily -- the per-variant names in the first dict are
    unaffected either way.
    """
    variant_names = {}
    group_option_names = {}
    group_option_conflict = set()
    for option in chr_customization_options or []:
        option_name = option.get("option_name") or ""
        for choice in option.get("choices", []):
            geoset_id = choice.get("geoset_id")
            if geoset_id is None:
                continue
            group, variant = geoset_id // 100, geoset_id % 100
            choice_name = choice.get("choice_name") or ""
            if choice_name:
                variant_names.setdefault(group, {})[variant] = choice_name
            if option_name and group not in group_option_conflict:
                existing = group_option_names.get(group)
                if existing is None:
                    group_option_names[group] = option_name
                elif existing != option_name:
                    group_option_conflict.add(group)
                    del group_option_names[group]
    return variant_names, group_option_names


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


# WoW's real char.fragment.shader/CharMaterialRenderer.js blend-mode enum,
# collapsed to Blender's ShaderNodeMix(data_type='RGBA').blend_type --
# see TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md's own "Real blend modes"
# table for the full derivation. Modes 0/1/9/15 all resolve to plain 'MIX'
# here: this function already gates every candidate on its own accumulated
# alpha (see apply_customization_texture_switch below), so a factor-driven
# Mix already reproduces "straight overwrite where covered" (0/1) and
# "standard alpha-over" (9/15) identically -- there is no separate
# Blender blend_type for either. A blend_mode with no entry here is
# genuinely unused in character customization per CharMaterialRenderer.js's
# own comment, or unconfirmed -- flagged at apply time, never guessed.
CHR_BLEND_MODE_TO_BLEND_TYPE = {
    0: 'MIX',
    1: 'MIX',
    4: 'MULTIPLY',
    6: 'OVERLAY',
    7: 'SCREEN',
    9: 'MIX',
    15: 'MIX',
}

# wowdev.wiki M2#Textures' "Texture Types" table (documentation/wowdev-wiki/
# md/M2.md), mirroring src/m2_header.cpp's own textureTypeName -- the exact
# same table husk's C++ exporter already uses to name a material's own
# `_<type>` suffix (export_materials.cpp). Used here for a short, real,
# canonical group/material label instead of re-embedding a whole verbose
# `mat.name` (which already contains this same short name, buried inside
# batch/tex-index/FileDataID/embedded-filename cruft) -- see Luna's own
# "girthy names" finding. IDs 24-26 have no real name per the wiki (kept
# absent here too, same as the C++ table); an unmapped type falls back to
# a plain numeric label, never a guess.
M2_TEXTURE_TYPE_NAME = {
    1: "skin",
    2: "object_skin",
    3: "weapon_blade",
    4: "weapon_handle",
    5: "environment",
    6: "char_hair",
    7: "char_facial_hair",
    8: "skin_extra",
    9: "ui_skin",
    10: "tauren_mane",
    11: "monster_1",
    12: "monster_2",
    13: "monster_3",
    14: "item_icon",
    15: "guild_background_color",
    16: "guild_emblem_color",
    17: "guild_border_color",
    18: "guild_emblem",
    19: "char_eyes",
    20: "char_jewelry",
    21: "char_secondary_skin",
    22: "char_secondary_hair",
    23: "char_secondary_armor",
}


def _find_husk_binary():
    """Locates the real `husk` binary (not `blp/`'s standalone `husk-blp`
    Python package, a superseded predecessor kept only as an independent
    reference implementation -- see `blp-export`'s own doc comment on
    `_convert_blp_to_png_cached` below): `PATH` first (the flake's own
    dev shell puts a build of it there), then this repo's own
    `build/husk` relative to this script's own location (the common case
    when this script is run without the flake env activated first --
    real interactive use, this session). Returns None if neither exists;
    callers degrade to reporting a `.blp`-only match rather than failing.
    """
    exe = shutil.which("husk")
    if exe:
        return exe
    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidate = os.path.join(script_dir, "..", "build", "husk")
    return candidate if os.path.isfile(candidate) else None


def _convert_blp_to_png_cached(blp_path, file_data_id):
    """Auto-converts `blp_path` to a real PNG via `husk blp-export`,
    cached by FileDataID under the system temp dir (`file_data_id`s are
    globally unique WoW asset identifiers, so this cache is safe to reuse
    across every run/model/session, not just this one invocation) --
    real usability fix, prompted directly: an earlier version of this
    script made the caller manually run `husk blp-export --dir ...`
    ahead of time as a separate step, which real interactive use (Luna,
    this session) flagged as exactly the kind of ceremony `husk export`
    itself already avoids for its own embedded textures (real `.blp`
    files are auto-detected and decoded in-memory there, no separate
    step -- see `--textures`'s own `--help` text). This makes the
    Blender-side path match that same "auto-detect and convert, don't
    make the user run a second tool" behavior, using `husk blp-export`
    (the exact same C++ decoder `husk export` itself uses internally,
    unlike `blp/`'s older, now-superseded standalone Python
    implementation) as a subprocess since this script's own Python
    (Blender's bundled interpreter) can't reach husk's internal C++ code
    directly. Returns None (and prints why) if no `husk` binary is found
    or the conversion itself fails -- never fatal to the rest of the
    switch, same as every other per-choice resolution failure here.
    """
    cache_dir = os.path.join(tempfile.gettempdir(), "husk_blp_cache")
    out_path = os.path.join(cache_dir, f"{file_data_id}.png")
    if os.path.isfile(out_path):
        return out_path

    husk_bin = _find_husk_binary()
    if husk_bin is None:
        return None

    os.makedirs(cache_dir, exist_ok=True)
    try:
        result = subprocess.run([husk_bin, "blp-export", blp_path, out_path],
                                 capture_output=True, text=True, timeout=30, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        print(f"husk_blender_geoset_mask: 'husk blp-export {blp_path}' failed: {exc}")
        return None
    if result.returncode != 0 or not os.path.isfile(out_path):
        print(f"husk_blender_geoset_mask: 'husk blp-export {blp_path}' failed: "
              f"{result.stderr.strip() or result.stdout.strip()}")
        return None
    return out_path


def _resolve_customization_texture_path(textures_dir, file_data_id):
    """`<textures_dir>/<file_data_id>.png` first -- the exact-name
    convention every other husk texture resolution already uses
    (`src/export_texture_resolution.hpp`'s `resolveTextureBytes` doc
    comment) -- then, since a real corpus extraction commonly keeps
    character-customization textures under their own real content name
    with the FileDataID only as a trailing `_<id>` suffix (confirmed
    against real local data: `character/bloodelf/eyes00_00_3492879.blp`,
    not `3492879.blp`), a `*_<file_data_id>.png` glob match. Both are
    tried again in `textures_dir`'s own parent directory too: these
    per-choice assets are commonly shared across every model under one
    race (real local data again: every blood elf eye-color file sits at
    `character/bloodelf/`, one level above the specific `female`/`male`
    model folder a caller would naturally pass as `--textures`). Blender's
    own Image Texture node can load a `.png` directly, unlike `.blp` --
    a `.blp`-only match is auto-converted via `husk blp-export`
    (`_convert_blp_to_png_cached`, cached by FileDataID) rather than
    requiring the caller to run that conversion by hand first. Returns
    None (and prints why) when nothing matches, the match is a `.blp` and
    no `husk` binary could be found to convert it, or
    `file_data_id`/`textures_dir` is falsy.
    """
    if not textures_dir or not file_data_id:
        return None

    search_dirs = [textures_dir]
    parent = os.path.dirname(os.path.normpath(textures_dir))
    if parent and parent != textures_dir:
        search_dirs.append(parent)

    blp_match = None
    for d in search_dirs:
        exact_png = os.path.join(d, f"{file_data_id}.png")
        if os.path.isfile(exact_png):
            return exact_png
        suffix_png = glob.glob(os.path.join(d, f"*_{file_data_id}.png"))
        if suffix_png:
            return sorted(suffix_png)[0]

    for d in search_dirs:
        exact_blp = os.path.join(d, f"{file_data_id}.blp")
        if os.path.isfile(exact_blp):
            blp_match = exact_blp
            break
        suffix_blp = glob.glob(os.path.join(d, f"*_{file_data_id}.blp"))
        if suffix_blp:
            blp_match = sorted(suffix_blp)[0]
            break

    if blp_match is not None:
        converted = _convert_blp_to_png_cached(blp_match, file_data_id)
        if converted is not None:
            return converted
        print(f"husk_blender_geoset_mask: customization choice texture {file_data_id} only "
              f"matched {blp_match!r} (.blp) -- Blender can't load BLP directly, and no `husk` "
              "binary was found to auto-convert it (checked PATH and this repo's own build/husk) "
              "-- skipping this choice")
    return None


def _load_customization_texture_image(path):
    try:
        return bpy.data.images.load(path, check_existing=True)
    except RuntimeError as exc:
        print(f"husk_blender_geoset_mask: failed to load texture {path!r}: {exc}")
        return None


def _build_customization_option_group(name, choice_infos):
    """Builds one self-contained node group implementing a single real
    `ChrCustomizationOption`'s own live choice switch: a real `Choice`
    dropdown (`NodeSocketMenu`, one named enum item per real choice --
    not a plain float index) promoted to the group's own interface,
    driving one `GeometryNodeMenuSwitch` (`data_type='BUNDLE'`, confirmed
    directly to work inside a `ShaderNodeTree` in Blender 5.1.1 despite
    its `GeometryNode` idname -- there is no separate `Shader`-prefixed
    menu-switch node type). Outputs `Color`/`Alpha`.

    Replaces this function's earlier `Math(COMPARE)`-gated chain of `Mix`
    nodes (one full accumulate-mix pair per choice, each also gated by its
    own real section rect via a since-removed `_uv_rect_mask`) after Luna
    pointed out two real problems with it, having actually used the
    generated graph in Blender: (1) a plain float `Choice Index` shows as
    an unlabeled number on the closed node -- a real `NodeSocketMenu`
    shows real choice names directly, no legend printout needed to decode
    it; (2) the whole Math/Mix accumulation chain is unnecessary
    machinery -- a real `MenuSwitch` is already exclusive (exactly one
    case active), so the section-rect gating that chain needed (to stop an
    off-rect choice's texture from bleeding into another choice's own area
    while both were being accumulated) has nothing left to protect
    against; every choice's own `Color`/`Alpha` pair is combined into one
    `NodeCombineBundle` and routed through the switch as a single unit, so
    there's no way for two choices' pixels to mix at all. Each choice's
    own texture is trusted to already carry real alpha=0 outside the area
    it paints, same as every other real per-choice texture in this corpus.

    Still built as its own closed `ShaderNodeGroup` per option (not
    inlined into the material's own tree) for the same real usability
    reason as before -- a real option can have dozens of choices, and
    collapsing them behind one labelled node is what makes the material's
    own graph readable at all; see git history for the fuller narrative.
    """
    tree = bpy.data.node_groups.new(name, "ShaderNodeTree")
    tree.interface.new_socket("Choice", in_out='INPUT', socket_type='NodeSocketMenu')
    tree.interface.new_socket("Color", in_out='OUTPUT', socket_type='NodeSocketColor')
    tree.interface.new_socket("Alpha", in_out='OUTPUT', socket_type='NodeSocketFloat')

    nodes, links = tree.nodes, tree.links
    group_input = nodes.new("NodeGroupInput")
    group_input.location = (-900.0, 0.0)
    group_output = nodes.new("NodeGroupOutput")

    menu_switch = nodes.new("GeometryNodeMenuSwitch")
    menu_switch.data_type = 'BUNDLE'
    menu_switch.location = (300.0, 0.0)
    # Real default items ('A'/'B') on a freshly-created MenuSwitch --
    # cleared so every remaining item is a real choice name, not a
    # leftover placeholder.
    menu_switch.enum_items.clear()
    links.new(group_input.outputs["Choice"], menu_switch.inputs["Menu"])

    separate = nodes.new("NodeSeparateBundle")
    separate.location = (600.0, 0.0)
    separate.bundle_items.new(socket_type='RGBA', name="Color")
    separate.bundle_items.new(socket_type='FLOAT', name="Alpha")
    links.new(menu_switch.outputs["Output"], separate.inputs[0])
    links.new(separate.outputs[0], group_output.inputs["Color"])
    links.new(separate.outputs[1], group_output.inputs["Alpha"])

    x = -600.0
    for choice in choice_infos:
        image = _load_customization_texture_image(choice["path"])
        if image is None:
            continue
        # Real, human-readable Image datablock name -- clean-name priority
        # (real listfile content name -> choice_name -> whatever
        # bpy.data.images.load
        # already picked from `path`'s own basename, e.g. a bare
        # FileDataID or a verbose wow_export-style filename). Renamed
        # post-load rather than passed to `.load()` itself, since Blender
        # names a freshly-loaded image from its file path regardless.
        clean_name = choice.get("content_name") or choice.get("choice_name")
        if clean_name:
            image.name = clean_name
        img_node = nodes.new("ShaderNodeTexImage")
        img_node.image = image
        img_node.label = choice["choice_name"]
        img_node.location = (x, 300.0)

        combine = nodes.new("NodeCombineBundle")
        combine.location = (x, 0.0)
        combine.bundle_items.new(socket_type='RGBA', name="Color")
        combine.bundle_items.new(socket_type='FLOAT', name="Alpha")
        links.new(img_node.outputs["Color"], combine.inputs[0])
        links.new(img_node.outputs["Alpha"], combine.inputs[1])

        # `enum_items.new` appends both the enum item and its matching
        # case-input socket at the end of `inputs`, right before the
        # always-present `__extend__` virtual socket -- confirmed
        # directly (Blender 5.1.1), not assumed.
        menu_switch.enum_items.new(choice["choice_name"])
        links.new(combine.outputs[0], menu_switch.inputs[-2])

        x += 350.0

    group_output.location = (900.0, 0.0)
    return tree


def _build_material_customization_group(name, relevant_options):
    """One combined node group per *material*: real Base Color/Alpha in,
    every relevant real `ChrCustomizationOption`'s own choice-switch
    layered on top in real `texture_layers[].layer` order using its real
    WoW blend mode (`CHR_BLEND_MODE_TO_BLEND_TYPE`), real Color/Alpha out.

    Replaces the earlier design of one small group *per option* plus a
    chain of `Mix` nodes spliced directly into the material's own tree.
    Luna, running the real pipeline in Blender, found that design put
    several stacked group nodes on one material with no single obvious
    "the" node to edit, and asked for exactly one group per material with
    the base texture already folded in -- no mixing left outside the
    group at all. Each option's own switch is still built by (unchanged)
    `_build_customization_option_group` and instanced here as a nested
    `ShaderNodeGroup`, with its own `Choice` input promoted one level
    further out -- so every relevant option's own real dropdown ends up on
    this *one* outer group's own closed node, matching Luna's own
    hand-built prototype (two independent dropdowns, "Pink" and "With
    Tiara", on a single group node).

    `relevant_options` is `[(option, choice_infos), ...]`, already sorted
    by real layer order (see the caller).
    """
    tree = bpy.data.node_groups.new(name, "ShaderNodeTree")
    tree.interface.new_socket("Base Color", in_out='INPUT', socket_type='NodeSocketColor')
    tree.interface.new_socket("Base Alpha", in_out='INPUT', socket_type='NodeSocketFloat')
    tree.interface.new_socket("Color", in_out='OUTPUT', socket_type='NodeSocketColor')
    tree.interface.new_socket("Alpha", in_out='OUTPUT', socket_type='NodeSocketFloat')

    nodes, links = tree.nodes, tree.links
    group_input = nodes.new("NodeGroupInput")
    group_input.location = (-900.0, 0.0)
    group_output = nodes.new("NodeGroupOutput")

    current_color = group_input.outputs["Base Color"]
    current_alpha = group_input.outputs["Base Alpha"]

    x = -600.0
    taken_menu_names = set()
    for option, choice_infos in relevant_options:
        option_name = _unique_label(option.get('option_name', 'Option'), taken_menu_names)
        taken_menu_names.add(option_name)

        sub_tree = _build_customization_option_group(f"{name}_{option_name}", choice_infos)
        sub_node = nodes.new("ShaderNodeGroup")
        sub_node.node_tree = sub_tree
        sub_node.label = option_name
        sub_node.location = (x, 400.0)

        # See `_build_customization_option_group`'s own comment on
        # `enum_items.new` -- `tree.interface.new_socket` appends the same
        # way, real (not virtual) sockets always right before the
        # always-present `__extend__` slot. Confirmed directly (Blender
        # 5.1.1), not assumed.
        tree.interface.new_socket(option_name, in_out='INPUT', socket_type='NodeSocketMenu')
        links.new(group_input.outputs[-2], sub_node.inputs["Choice"])

        blend_mode = choice_infos[0]["blend_mode"]
        blend_type = CHR_BLEND_MODE_TO_BLEND_TYPE.get(blend_mode)
        if blend_type is None:
            print(f"husk_blender_geoset_mask: group {name!r} option {option_name!r} has "
                  f"unrecognized blend_mode {blend_mode!r} -- falling back to plain Mix")
            blend_type = 'MIX'

        color_mix = nodes.new("ShaderNodeMix")
        color_mix.data_type = 'RGBA'
        color_mix.blend_type = blend_type
        color_mix.clamp_result = True
        color_mix.location = (x, 100.0)
        links.new(sub_node.outputs["Alpha"], _node_socket(color_mix.inputs, "Factor_Float"))
        links.new(current_color, _node_socket(color_mix.inputs, "A_Color"))
        links.new(sub_node.outputs["Color"], _node_socket(color_mix.inputs, "B_Color"))
        current_color = _node_socket(color_mix.outputs, "Result_Color")
        # The topmost (last, highest-layer) option's own alpha defines the
        # material's final coverage -- real WoW data backs this for the
        # hair/tiara case Luna found (a tiara-bearing hair choice's own
        # texture genuinely carries less-covering alpha than a plain one),
        # but hasn't been visually reconfirmed for every blend_mode/option
        # combination -- flagged, not asserted as universally correct.
        current_alpha = sub_node.outputs["Alpha"]

        x += 400.0

    links.new(current_color, group_output.inputs["Color"])
    links.new(current_alpha, group_output.inputs["Alpha"])
    group_output.location = (x + 200.0, 0.0)
    return tree


def apply_customization_texture_switch(options, layout, enabled_materials, materials, textures_dir):
    """Builds a real, live, switchable shader node graph per material that
    layers `chr_customization_options`' own real per-choice textures onto
    `chr_texture_layout`'s own real base atlas -- the actual goal of
    TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md (see that file for the full
    join-chain derivation and the reverted husk-side pixel compositor this
    replaces). Exactly one combined `ShaderNodeGroup` per material
    (`_build_material_customization_group`) -- real Base Color/Alpha in
    (whatever already fed the material's Principled BSDF, preserved, same
    "insert before the existing consumer" technique
    `apply_texture_layout_overlay` above already uses), every relevant
    real `ChrCustomizationOption`'s own real `NodeSocketMenu` dropdown
    (named items, one per real choice) and its resolved layer folded in
    internally, real Color/Alpha out -- wired *directly* to the Principled
    BSDF, no `Mix` nodes left in the material's own top-level tree at all.
    Each dropdown defaults to whichever choice `chr_enabled_materials`
    actually resolved for this export. Final Alpha is alpha-clipped (a
    `Math(GREATER_THAN, 0.0)` node) rather than blended/dithered before
    reaching the Principled BSDF's own `Alpha` input -- real WoW
    customization textures (hair in particular) use cutout alpha, not
    translucency; harmless on a material whose own `blend_method` isn't
    `'CLIP'` (Blender simply ignores the Alpha input there), so this is
    always wired, not conditioned on texture_type. Returns the count of
    materials touched.
    """
    material_layout_by_type = {m.get("texture_type"): m for m in layout.get("materials", [])}
    concerned_types = set(material_layout_by_type)
    if not concerned_types or not layout.get("texture_layers") or not textures_dir:
        return 0

    texture_layer_by_target = {tl.get("chr_model_texture_target_id"): tl
                                for tl in layout.get("texture_layers", [])}
    default_choice_id_by_target = {e.get("chr_model_texture_target_id"): e.get("choice_id")
                                    for e in (enabled_materials or [])
                                    if e.get("file_data_id")}

    touched = 0
    skipped_related_materials = 0
    for mat in materials:
        mtype = mat.get("texture_type")
        if mtype not in concerned_types or mat.node_tree is None:
            continue

        node_tree = mat.node_tree
        principled = _find_principled_bsdf(node_tree)
        if principled is None:
            continue
        base_color_input = principled.inputs.get("Base Color")
        if base_color_input is None:
            continue

        alpha_input = principled.inputs.get("Alpha")

        # One (option, [choice info]) entry per real ChrCustomizationOption
        # that resolves at least one real, textured choice onto *this*
        # material's own texture_type -- see the join-chain derivation in
        # CHAR_TEXTURE_BLENDER_SWITCH_TODO.md. `default_choice_id_by_target`
        # is resolved per choice_info below (not carried as a separate
        # per-option `target_id` anymore -- each choice already knows its
        # own `target_id`).
        relevant_options = []
        for option in options:
            choice_infos = []
            for choice in option.get("choices", []):
                for m_entry in choice.get("materials", []) or []:
                    tl = texture_layer_by_target.get(m_entry.get("chr_model_texture_target_id"))
                    if tl is None or tl.get("texture_type") != mtype:
                        continue
                    if m_entry.get("related_choice_id"):
                        # This material only applies together with that
                        # other real ChrCustomizationChoiceID (a different,
                        # related option) also being selected -- e.g. a real
                        # "Tiara" Hairstyle choice carries one dedicated
                        # material per real Hair Color choice, not one
                        # unconditional material. This switch has no live
                        # notion of "what's currently selected in the other
                        # dropdown" (each option's own Menu Switch is
                        # independent), so a conditional material can't be
                        # correctly resolved here yet -- conservatively
                        # skipped rather than blended in as if unconditional
                        # (the bug this filter replaces: every conditional
                        # variant was previously attached and layered
                        # together, indiscriminately). See
                        # TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md's own
                        # "Independent color/alpha axes" item for the real
                        # follow-up (a true cross-product dropdown) this
                        # still needs.
                        skipped_related_materials += 1
                        continue
                    fdid = m_entry.get("file_data_id")
                    if not fdid:
                        continue  # unresolved (e.g. a swatch-color-only choice) -- flagged, not guessed
                    path = _resolve_customization_texture_path(textures_dir, fdid)
                    if path is None:
                        continue
                    choice_infos.append({
                        "choice_id": choice.get("choice_id"),
                        "choice_name": choice.get("choice_name") or f"choice_{choice.get('choice_id')}",
                        # Real --listfile content-path stem (e.g.
                        # "scalpupperhair00_08"), when husk's own export
                        # resolved one for this material -- clean-name
                        # priority: this beats choice_name for the loaded
                        # Image datablock's own name below. None when --listfile
                        # wasn't given at export time, or didn't resolve
                        # this fileDataId.
                        "content_name": m_entry.get("content_name"),
                        "path": path,
                        "blend_mode": tl.get("blend_mode"),
                        "layer": tl.get("layer", 0),
                        "target_id": m_entry.get("chr_model_texture_target_id"),
                    })
                    break  # one resolved target is enough for this material's own type
            if choice_infos:
                relevant_options.append((option, choice_infos))

        if not relevant_options:
            continue

        relevant_options.sort(key=lambda ov: min(c["layer"] for c in ov[1]))

        # Real default choice, per option, set directly on that option's
        # own promoted `Choice` dropdown -- see
        # `_build_material_customization_group`'s own doc comment for why
        # every relevant option's dropdown now lives on one combined group
        # node rather than one small group node each.
        for option, choice_infos in relevant_options:
            for c in choice_infos:
                c["is_default"] = default_choice_id_by_target.get(c["target_id"]) == c["choice_id"]

        short_type_name = M2_TEXTURE_TYPE_NAME.get(mtype, f"type{mtype}")
        group_tree = _build_material_customization_group(
            f"Husk_{short_type_name}_customization", relevant_options)

        # Anchored below the existing graph's own lowest node, same
        # pitfall/fix `apply_texture_layout_overlay` above already
        # established.
        existing_ys = [n.location.y for n in node_tree.nodes]
        base_y = (min(existing_ys) if existing_ys else 0.0) - 400.0
        base_x = principled.location.x - 900.0

        original_color_socket = base_color_input.links[0].from_socket if base_color_input.is_linked else None
        if original_color_socket is None:
            const_rgb = node_tree.nodes.new("ShaderNodeRGB")
            const_rgb.outputs[0].default_value = tuple(base_color_input.default_value)
            const_rgb.location = (base_x - 300.0, base_y + 150.0)
            original_color_socket = const_rgb.outputs[0]

        original_alpha_socket = (alpha_input.links[0].from_socket
                                  if alpha_input is not None and alpha_input.is_linked else None)
        if original_alpha_socket is None:
            const_alpha = node_tree.nodes.new("ShaderNodeValue")
            const_alpha.outputs[0].default_value = alpha_input.default_value if alpha_input is not None else 1.0
            const_alpha.location = (base_x - 300.0, base_y - 150.0)
            original_alpha_socket = const_alpha.outputs[0]

        # The one node a user actually needs to find and edit for this
        # whole material -- given real screen presence (a custom color,
        # extra width, and its own real name in the label). Every relevant
        # option's own `Choice` input is left unlinked, so each shows as a
        # real, directly editable dropdown right on this closed node -- no
        # need to enter the group at all.
        group_node = node_tree.nodes.new("ShaderNodeGroup")
        group_node.node_tree = group_tree
        group_node.label = f"{short_type_name} customization"
        group_node.name = group_node.label
        group_node.location = (base_x, base_y)
        group_node.width = 260.0
        group_node.use_custom_color = True
        group_node.color = (0.15, 0.55, 0.15)
        node_tree.links.new(original_color_socket, group_node.inputs["Base Color"])
        node_tree.links.new(original_alpha_socket, group_node.inputs["Base Alpha"])
        for option, choice_infos in relevant_options:
            default_choice = next((c for c in choice_infos if c["is_default"]), choice_infos[0])
            group_node.inputs[option.get('option_name', 'Option')].default_value = \
                default_choice["choice_name"]

        node_tree.links.new(group_node.outputs["Color"], base_color_input)
        if alpha_input is not None:
            # Alpha-clipped, not blended/dithered -- see this function's
            # own doc comment for why.
            clip = node_tree.nodes.new("ShaderNodeMath")
            clip.operation = 'GREATER_THAN'
            clip.inputs[1].default_value = 0.0
            clip.location = (base_x + 400.0, base_y - 150.0)
            node_tree.links.new(group_node.outputs["Alpha"], clip.inputs[0])
            node_tree.links.new(clip.outputs[0], alpha_input)

        touched += 1

    if skipped_related_materials:
        print(f"husk_blender_geoset_mask: {skipped_related_materials} conditional material(s) "
              "skipped (each only applies together with another specific choice, not resolvable "
              "by this switch yet -- see CHAR_TEXTURE_BLENDER_SWITCH_TODO.md's own "
              "\"Independent color/alpha axes\" item)")

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


def apply_emitter_markers(armature_obj):
    """Places one placement-marker object per real `ribbon_emitters`/
    `particle_emitters` anchor (see `read_emitter_anchors`) -- returns
    `(ribbon_count, particle_count)`, both 0 if `armature_obj` gave no
    anchors (falsy armature, no ribbon/particle emitters on this model, or
    a joint index this model's own skin doesn't have -- logged, not
    raised, same as `apply_billboard_alignment`'s per-bone skip behavior).

    Every marker lands in `_debug_marker_collection()` (hidden-by-default,
    excluded from every render pass -- see that function's own doc
    comment) and additionally gets `hide_render = True` set on the object
    itself, belt-and-suspenders: a collection-level flag stops applying
    the moment someone relinks a marker into a different collection, an
    object-level flag doesn't.
    """
    ribbon_anchors, particle_anchors = read_emitter_anchors(armature_obj)
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
            bone_name = anchor.get("bone_name")
            if not bone_name or bone_name not in armature_obj.pose.bones:
                print(f"husk_blender_geoset_mask: {kind} emitter id={anchor.get('id')} -- "
                      f"joint {anchor.get('joint')} has no matching bone, skipped")
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


# --- .phys jiggle-bone wiring (real spring-chain data -> the "Jiggle
# Physics" Blender addon, https://extensions.blender.org/add-ons/jiggle-physics/,
# naelstrof/blender-jiggle-physics on GitHub) ---
# `read_physics_bodies` reads both `physics_bodies` (per-body placement
# anchor) and `physics_joints` (a deliberately reduced body-to-body spring/
# limit graph -- body pair, spring frequency/damping or a normalized
# swing-limit angle, no frame matrices or shape geometry) straight from the
# already-imported armature's own root-joint extras (see
# `_root_joint_extras`) -- both travel inside the `.glb` itself now, no
# `husk` binary or separate `.phys` file needed at Blender-import time (an
# earlier version of this shelled out to `husk dump-chunks <file>.phys` for
# the joint graph; real feedback flagged that as a portability bug, since a
# `.glb` isn't guaranteed to travel with a `husk` binary nearby -- fixed by
# embedding the reduced graph in husk's own C++ export instead, see
# `gltf::Skeleton::PhysicsJoint`). This section drives the Jiggle Physics
# addon's own plain bpy properties directly from that data -- no bpy.ops
# needed, the addon has no operator for "enable jiggle on this bone", just
# PoseBone-level properties (`pose_bone.jiggle.enable`, `.mode`,
# `jiggle_angle_elasticity`, ...), confirmed by reading the addon's own
# `__init__.py` (single-file addon, no `bpy.ops.jiggle.*` namespace).
#
# WoW's own joint graph connects two *bodies*, not bones directly, and
# isn't guaranteed to follow the skeleton's own parent/child bone chain --
# but the Jiggle Physics addon's solver rides *only* on the existing bone
# parent chain (verlet along parent->child), it has no concept of an
# arbitrary body-to-body spring graph. Every real fixture checked this
# session (a real chain prop, `8xp_heartofazeroth_prop_floatychain.phys`)
# had every joint edge exactly matching a real bone parent/child pair, but
# this isn't assumed true in general -- any edge that doesn't match the
# armature's own bone parents is skipped with a loud note, never silently
# force-fit onto the wrong bone.
#
# The addon is deliberately "authorable, not physically accurate" (its own
# README) -- a 0..1 "elasticity" knob, not real spring math. WoW's own
# `.phys` spring fields are real physical units (Hz, damping ratio) or, in
# the very common case observed in the real fixture above, simply
# absent/zero (a Shoulder/Revolute joint with zero motor frequency -- that
# file shape describes a swing *limit*, not a spring-back force; the
# restoring motion is gravity + the cone/twist angle limit, not an
# authored stiffness). There is no exact conversion between these two
# systems -- `_phys_elasticity_heuristic` below is a labeled best-effort
# approximation, meant to be tuned by eye in the viewport afterward, not a
# physically faithful port.

def _phys_elasticity_heuristic(frequency_hz, swing_limit_deg):
    """Best-effort `physics_joints` spring/limit data -> Jiggle Physics
    addon's 0..1 `jiggle_angle_elasticity`/`jiggle_length_elasticity`
    knobs. See this section's own module-level comment for why this can't
    be an exact conversion. `frequency_hz`/`swing_limit_deg` are the
    already-normalized scalars `gltf::Skeleton::PhysicsJoint` resolves on
    the husk C++ side (real spring frequency when this joint type carries
    one, else a real swing-limit angle, else both 0) -- this function only
    owns the elasticity *curve*, not which raw `.phys` field means what.

    Preference order:
    1. A real nonzero `frequency_hz` -- higher frequency = stiffer, mapped
       via `f / (f + 2)` (a simple saturating curve chosen so ~2 Hz, a soft
       cloth-like spring, lands near the addon's own 0.6 default, and
       higher real frequencies climb toward 1.0).
    2. No spring frequency at all (the common case observed in real data --
       a pure angle-limit joint) -- `swing_limit_deg`, when nonzero:
       narrower allowed swing = stiffer, mapped via
       `1 - min(swing_deg, 180) / 180`.
    3. Neither -- the addon's own built-in default (0.6), unchanged.
    """
    if frequency_hz:
        return max(0.0, min(1.0, frequency_hz / (frequency_hz + 2.0)))
    if swing_limit_deg:
        return max(0.0, min(1.0, 1.0 - min(swing_limit_deg, 180.0) / 180.0))
    return 0.6


class HUSK_OT_install_jiggle_physics(bpy.types.Operator):
    """Installs the third-party 'Jiggle Physics' addon (`naelstrof/
    blender-jiggle-physics`, package id `jiggle_physics`) from
    extensions.blender.org -- what `apply_physics_jiggle_bones` needs to do
    anything with real `.phys` spring/joint data. Only ever runs from its
    own confirm dialog (`invoke`, below): this script itself never
    downloads or installs anything without a real, explicit click from a
    human on that dialog -- `execute` (the part that actually calls
    `bpy.ops.extensions.package_install`) is only reached after that.
    """
    bl_idname = "husk.install_jiggle_physics"
    bl_label = "Install Jiggle Physics addon"
    bl_options = {'REGISTER'}
    _PKG_ID = "jiggle_physics"
    _REPO_MODULE = "blender_org"

    def invoke(self, context, event):
        return context.window_manager.invoke_confirm(
            self, event,
            title="husk: install the 'Jiggle Physics' addon?",
            message="Downloads and installs the third-party 'Jiggle Physics' addon "
                     "(naelstrof/blender-jiggle-physics) from extensions.blender.org, needed "
                     "to turn this model's real .phys spring/joint data into live jiggle bones.",
            confirm_text="Install")

    def execute(self, context):
        repo_index = next((i for i, r in enumerate(context.preferences.extensions.repos)
                            if r.module == self._REPO_MODULE), None)
        if repo_index is None:
            self.report({'ERROR'}, "husk: no 'extensions.blender.org' repository configured in "
                                    "this Blender -- install manually from "
                                    "https://extensions.blender.org/add-ons/jiggle-physics/")
            return {'CANCELLED'}
        if not bpy.app.online_access:
            self.report({'ERROR'}, "husk: online access is disabled (Preferences > System > "
                                    "Network > Allow Online Access) -- enable it, or install "
                                    "manually from https://extensions.blender.org/add-ons/"
                                    "jiggle-physics/")
            return {'CANCELLED'}
        bpy.ops.extensions.repo_sync(repo_index=repo_index)
        result = bpy.ops.extensions.package_install(repo_index=repo_index, pkg_id=self._PKG_ID,
                                                      enable_on_install=True)
        if 'FINISHED' not in result:
            self.report({'ERROR'}, "husk: Jiggle Physics install failed -- see the console, or "
                                    "install manually from https://extensions.blender.org/"
                                    "add-ons/jiggle-physics/")
            return {'CANCELLED'}
        self.report({'INFO'}, "husk: Jiggle Physics installed -- re-run this script (Scripting "
                               "tab > Run Script, or re-import the .glb) to wire up jiggle bones")
        return {'FINISHED'}


_install_prompt_shown = False


def _prompt_install_jiggle_physics():
    """Pops `HUSK_OT_install_jiggle_physics`'s own confirm dialog, deferred
    one Blender-event-loop tick via `bpy.app.timers` -- `invoke_confirm`
    needs a real window/event, which isn't reliably available yet while
    this script is still executing as part of Blender's own `--python`
    startup handling (the exact same "not ready until the event loop
    actually starts spinning" timing issue every deferred-registration
    pattern in this file works around, e.g. `_update_registered_emitter_markers`).
    Never prompts more than once per Blender session (`_install_prompt_shown`),
    and never runs at all in `--background` mode -- there's no window to
    show a dialog in, so `physics_jiggle_stage` falls back to a
    console-only message there instead.
    """
    global _install_prompt_shown
    if _install_prompt_shown or bpy.app.background:
        return
    _install_prompt_shown = True
    if not hasattr(bpy.types, "HUSK_OT_install_jiggle_physics"):
        bpy.utils.register_class(HUSK_OT_install_jiggle_physics)

    def _show():
        bpy.ops.husk.install_jiggle_physics('INVOKE_DEFAULT')
        return None

    bpy.app.timers.register(_show, first_interval=0.1)


def apply_physics_jiggle_bones(armature_obj, physics_bodies, physics_joints, joint_bone_names):
    """Wires up the Jiggle Physics addon (see this section's module-level
    comment) from real `.phys` body/joint data: `physics_bodies` gives
    `id -> bone`, `physics_joints` gives the real (reduced) joint graph
    connecting those bodies -- both from this file's own
    `read_physics_bodies`, both already inside the `.glb`, no separate file
    or subprocess needed.

    Returns `(enabled_count, skipped_count)`. `enabled_count` is the
    number of bones that got `pose_bone.jiggle.enable = True`;
    `skipped_count` counts joint edges that didn't map onto a real bone
    parent/child pair in this armature (see the module-level comment on
    why those are dropped, not force-fit). Returns `(0, 0)` if the Jiggle
    Physics addon isn't installed/enabled, or `physics_bodies`/
    `physics_joints` is empty (model exported without `--phys`).
    """
    if not hasattr(bpy.types.PoseBone, "jiggle"):
        if bpy.app.background:
            print("husk_blender_geoset_mask: physics_bodies extras present but the 'Jiggle "
                  "Physics' addon isn't installed/enabled (Edit > Preferences > Get Extensions, "
                  "search 'Jiggle Physics', naelstrof/blender-jiggle-physics) -- skipping "
                  "jiggle-bone setup")
        else:
            print("husk_blender_geoset_mask: physics_bodies extras present but the 'Jiggle "
                  "Physics' addon isn't installed/enabled -- a confirm dialog will offer to "
                  "install it (husk.install_jiggle_physics); re-run this script afterward to "
                  "wire up jiggle bones")
            _prompt_install_jiggle_physics()
        return 0, 0
    if not physics_bodies or not physics_joints:
        return 0, 0

    body_bone = {}
    for body in physics_bodies:
        bone_name = joint_bone_names.get(body.get("joint", -1))
        if bone_name and bone_name in armature_obj.pose.bones:
            body_bone[body["id"]] = bone_name

    parent_names = {b.name: (b.parent.name if b.parent else None) for b in armature_obj.pose.bones}

    is_child_of = {}  # bone_name -> set of bones it is the jiggle-child of
    enabled_names = set()
    skipped = 0
    for joint in physics_joints:
        bone_a = body_bone.get(joint.get("body_a"))
        bone_b = body_bone.get(joint.get("body_b"))
        if bone_a is None or bone_b is None:
            continue
        if parent_names.get(bone_b) == bone_a:
            child, parent = bone_b, bone_a
        elif parent_names.get(bone_a) == bone_b:
            child, parent = bone_a, bone_b
        else:
            print(f"husk_blender_geoset_mask: physics joint body {joint.get('body_a')}<->"
                  f"{joint.get('body_b')} (bones '{bone_a}'/'{bone_b}') isn't a real bone "
                  "parent/child pair in this armature -- skipped (Jiggle Physics can only "
                  "chain along the existing bone hierarchy)")
            skipped += 1
            continue
        enabled_names.add(child)
        enabled_names.add(parent)
        is_child_of.setdefault(child, set()).add(parent)

        elasticity = _phys_elasticity_heuristic(joint.get("frequency_hz", 0), joint.get("swing_limit_deg", 0))
        pose_bone = armature_obj.pose.bones[child]
        pose_bone.jiggle.enable = True
        pose_bone.jiggle_angle_elasticity = elasticity
        pose_bone.jiggle_length_elasticity = elasticity

    for name in enabled_names:
        pose_bone = armature_obj.pose.bones[name]
        pose_bone.jiggle.enable = True
        # A bone that's someone's jiggle-child but never a parent itself in
        # this graph is a chain tip; a bone that's only ever a parent is
        # this chain's root -- Jiggle Physics' own `mode` enum distinguishes
        # both from an ordinary mid-chain bone (real behavior difference:
        # 'root' bones don't simulate themselves, 'tip' bones get no child
        # to inherit twist from).
        is_root = name not in is_child_of
        is_tip = not any(name in parents for parents in is_child_of.values())
        if is_root and not is_tip:
            pose_bone.jiggle.mode = 'root'
        elif is_tip and not is_root:
            pose_bone.jiggle.mode = 'tip'
        else:
            pose_bone.jiggle.mode = 'normal'

    if enabled_names:
        armature_obj.jiggle.enable = True
        bpy.context.scene.jiggle.enable = True
    return len(enabled_names), skipped


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


def _material_tint_color(mat):
    """A single representative RGB for this material's own Base Color --
    the flat baseColorFactor tint if untextured, or the average pixel of
    its Base Color texture if textured. A deliberate simplification (one
    flat color per material, not a per-pixel texture sample in the
    compositor) -- good enough for the tint/grime/glow-overlay style
    Mod/Mod2x materials WoW actually uses this blend mode for."""
    if mat.node_tree is None:
        return (1.0, 1.0, 1.0)
    # Unlit (KHR_materials_unlit) materials -- common for blend_mode > 2, see
    # fix_additive_materials's own doc comment -- import as an Emission node,
    # not a Principled BSDF; check both shapes.
    emission = next((n for n in mat.node_tree.nodes if n.type == "EMISSION"), None)
    if emission is not None:
        base_input = emission.inputs["Color"]
    else:
        principled = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if principled is None:
            return (1.0, 1.0, 1.0)
        base_input = principled.inputs["Base Color"]
    if not base_input.is_linked:
        c = base_input.default_value
        return (c[0], c[1], c[2])
    tex_node = base_input.links[0].from_node
    image = getattr(tex_node, "image", None)
    if image is None:
        return (1.0, 1.0, 1.0)
    px = image.pixels
    n = len(px) // 4
    if n == 0:
        return (1.0, 1.0, 1.0)
    step = max(1, n // 4096)  # subsample large textures -- an average, not a render
    r = g = b = 0.0
    count = 0
    for i in range(0, n, step):
        o = i * 4
        r += px[o]
        g += px[o + 1]
        b += px[o + 2]
        count += 1
    return (r / count, g / count, b / count)


def _average_tint(materials, name_list):
    r = g = b = 0.0
    for name in name_list:
        mat = next(m for m in materials if m is not None and m.name == name)
        mr, mg, mb = _material_tint_color(mat)
        r += mr
        g += mg
        b += mb
    count = len(name_list)
    return (r / count, g / count, b / count)


def _node_socket(sockets, identifier):
    return next(s for s in sockets if s.identifier == identifier)


def apply_multiply_blend_compositing(scene, materials):
    """WoW blend modes 5 (Mod) and 6 (Mod2x): `dest = dest*src` (Mod2x:
    `dest*src*2`, clamped) -- and, confirmed against the real GL blend
    factors (not the naive "it's just another alpha blend" assumption),
    material alpha is not read by this blend equation at all (GL blend func
    (DST_COLOR, ZERO) / (DST_COLOR, SRC_COLOR)). Applied directly to the
    scene's own existing beauty pass -- Blender's default alpha-over import
    already gives us that for free, in the one render this runs before --
    no second render, no reconstructing whatever's "behind" the material
    (an earlier version of this investigation tried exactly that via
    Cryptomatte's Image output and found it recovers a blend of both
    layers, not the true occluded background; see MOD_BLEND_COMPOSITING_
    TODO.md history). Each Mod/Mod2x material's own Cryptomatte Matte (real
    per-material coverage, already correct for partial-alpha materials too,
    verified against real EEVEE/Cycles renders) picks how much of a flat
    darken-by-tint-color operation to mix onto the beauty pixel. Mod and
    Mod2x materials chain (Mod first, Mod2x reading Mod's own output as its
    base) so both can appear in the same scene without one clobbering the
    other's contribution. A deliberate, accepted simplification: a fully
    opaque (alpha=1) Mod/Mod2x material's own beauty pixel is already just
    its own color (nothing behind it survives the default alpha-over
    import), so this darkens that color by itself rather than by whatever
    WoW's own renderer would have shown behind it -- not byte-exact, but
    the common real case (WoW artists use Mod/Mod2x for partial-alpha tint/
    grime/glow overlays, not full replacement) is unaffected."""
    mod_names = [m.name for m in materials if m is not None and m.get("blend_mode") == 5]
    mod2x_names = [m.name for m in materials if m is not None and m.get("blend_mode") == 6]
    if not mod_names and not mod2x_names:
        return 0

    vl = scene.view_layers[0]
    vl.use_pass_cryptomatte_material = True
    vl.use_pass_cryptomatte_accurate = True

    tree = scene.compositing_node_group
    if tree is None:
        tree = bpy.data.node_groups.new(f"{scene.name} Compositing", 'CompositorNodeTree')
        scene.compositing_node_group = tree
    if not any(item.name == "Image" for item in tree.interface.items_tree):
        tree.interface.new_socket("Image", in_out='OUTPUT', socket_type='NodeSocketColor')
    group_out = next((n for n in tree.nodes if n.bl_idname == "NodeGroupOutput"), None)
    if group_out is None:
        group_out = tree.nodes.new("NodeGroupOutput")

    nodes, links = tree.nodes, tree.links

    rlayers = nodes.new("CompositorNodeRLayers")
    rlayers.name = "HuskMultiplyBlendRLayers"
    rlayers.scene = scene
    rlayers.layer = vl.name
    beauty = rlayers.outputs["Image"]

    def mix_rgba(factor_socket, a_socket, b_socket, name):
        n = nodes.new("ShaderNodeMix")
        n.name = name
        n.data_type = 'RGBA'
        n.clamp_result = True
        links.new(factor_socket, _node_socket(n.inputs, "Factor_Float"))
        links.new(a_socket, _node_socket(n.inputs, "A_Color"))
        links.new(b_socket, _node_socket(n.inputs, "B_Color"))
        return _node_socket(n.outputs, "Result_Color")

    def apply_group(name_list, scale, node_prefix, source):
        crypto = nodes.new("CompositorNodeCryptomatteV2")
        crypto.name = node_prefix + "Crypto"
        crypto.scene = scene
        crypto.layer_name = f"{vl.name}.CryptoMaterial"
        crypto.matte_id = ",".join(name_list)
        links.new(beauty, crypto.inputs["Image"])

        tr, tg, tb = _average_tint(materials, name_list)

        darken = nodes.new("ShaderNodeMix")
        darken.name = node_prefix + "Darken"
        darken.data_type = 'RGBA'
        darken.blend_type = 'MULTIPLY'
        darken.clamp_result = True
        darken.inputs["Factor"].default_value = 1.0
        links.new(source, _node_socket(darken.inputs, "A_Color"))
        _node_socket(darken.inputs, "B_Color").default_value = (tr * scale, tg * scale, tb * scale, 1.0)
        darkened = _node_socket(darken.outputs, "Result_Color")

        return mix_rgba(crypto.outputs["Matte"], source, darkened, node_prefix + "Mix")

    result = beauty
    if mod_names:
        result = apply_group(mod_names, 1.0, "HuskMod", result)
    if mod2x_names:
        result = apply_group(mod2x_names, 2.0, "HuskMod2x", result)

    links.new(result, _node_socket(group_out.inputs, "Socket_0"))
    return len(mod_names) + len(mod2x_names)


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
    textures_dir = None
    if "--" in argv:
        extra_args = argv[argv.index("--") + 1:]
        if extra_args and not extra_args[0].startswith("--"):
            filepath = extra_args[0]
            bpy.ops.import_scene.gltf(filepath=filepath)
        if "--textures" in extra_args:
            idx = extra_args.index("--textures")
            if idx + 1 < len(extra_args):
                textures_dir = extra_args[idx + 1]
        elif filepath:
            # Same "no separate flag needed for the common case" default
            # `husk export --textures` itself already uses ("the model's
            # own directory") -- real usability fix, prompted directly:
            # requiring this explicitly here on top of an already-short
            # `husk export` invocation defeated the point of that
            # shortness. Falls back to `_resolve_customization_texture_path`'s
            # own parent-directory search too, so this still finds a real
            # race-level shared texture even when the .glb itself sits one
            # level below (e.g. a `female`/`male` subfolder).
            textures_dir = os.path.dirname(os.path.abspath(filepath)) or "."

    if textures_dir is None:
        # Blender's own relative-path convention (an Image's own `filepath`
        # can be `//`-prefixed, relative to the current .blend file's own
        # directory; bpy.path.abspath("//") resolves that base directory
        # directly) -- same "textures live next to the model" assumption
        # the CLI-arg-derived default above already makes, just sourced
        # from the saved .blend file's own location instead of a trailing
        # argv path. Real: confirmed directly that Blender returns a plain
        # empty string here (not a silent cwd guess) when the file hasn't
        # been saved anywhere yet, so this only ever fires with a real
        # directory. Closes the "runnable as a pure Blender script, no
        # arguments at all" case Luna asked for -- import the model via
        # File > Import, save the .blend next to it, run this script from
        # the Text Editor with zero configuration.
        blend_dir = bpy.path.abspath("//")
        if blend_dir:
            # A real "textures/" subfolder next to the .blend, when
            # present, is preferred over the bare .blend directory itself
            # (a real, common asset-layout convention this default should
            # also recognize, not just "everything flat in one folder").
            # `_resolve_customization_texture_path`'s own existing
            # "also check the parent directory" search means using this
            # subfolder as `textures_dir` still finds a real texture that
            # instead sits directly in the bare .blend directory -- no
            # need to check both explicitly here.
            textures_subdir = os.path.join(blend_dir, "textures")
            textures_dir = textures_subdir if os.path.isdir(textures_subdir) else blend_dir

    mesh_objs, armature_obj = find_mesh_and_armature()
    model_name = mesh_objs[0].name if mesh_objs else (filepath or "<unknown>")
    materials = {obj.material_slots[i].material
                 for obj in mesh_objs
                 for i in range(len(obj.material_slots))
                 if obj.material_slots[i].material is not None}

    def geoset_stage():
        enabled_geosets = read_enabled_geosets(armature_obj)
        extra_default_overrides = enabled_geosets_to_default_overrides(enabled_geosets)
        chr_customization_options = read_chr_customization_options(armature_obj)
        all_groups, switch_groups, removed = apply_geoset_switches(
            mesh_objs, armature_obj, extra_default_overrides, chr_customization_options)
        print(f"husk_blender_geoset_mask: {len(all_groups)} geoset group(s) across "
              f"{len(mesh_objs)} mesh object(s), {switch_groups} dropdown switch(es) built, "
              f"{removed} tag bone(s) removed")
        if extra_default_overrides:
            print(f"husk_blender_geoset_mask: {len(extra_default_overrides)} group default(s) "
                  f"driven by real enabled_geosets extras ({len(enabled_geosets)} customization "
                  "choice(s) resolved at export time), not the curated/lowest-variant fallback")
        if chr_customization_options:
            variant_names, group_option_names = build_geoset_choice_names(chr_customization_options)
            print(f"husk_blender_geoset_mask: {len(group_option_names)} group(s) labeled with a "
                  f"real option name, {sum(len(v) for v in variant_names.values())} variant(s) "
                  "labeled with a real choice name, from chr_customization_options")

    def billboard_stage():
        billboard_bones = find_billboard_bones(armature_obj)
        if not billboard_bones:
            return
        constrained = apply_billboard_alignment(mesh_objs, armature_obj, find_camera_object())
        print(f"husk_blender_geoset_mask: {constrained}/{len(billboard_bones)} billboard "
              "bone(s) got a camera-facing constraint")

    def texture_layout_overlay_stage():
        layout = read_chr_texture_layout(armature_obj)
        if layout is None:
            print("husk_blender_geoset_mask: no chr_texture_layout extras found "
                  "(no --char-layout-id given at export time, or no armature in the scene) -- "
                  "skipping the texture-layout overlay")
            return
        touched = apply_texture_layout_overlay(layout, materials)
        print(f"husk_blender_geoset_mask: chr_texture_layout {layout.get('layout_id')} -- "
              f"{touched} material(s) got a toggleable section-boundary overlay "
              f"(off by default; enable 'Show Overlay' on the HuskChrTextureLayoutOverlay "
              f"node in the Shader Editor)")

    def customization_texture_switch_stage():
        options = read_chr_customization_options(armature_obj)
        if not options:
            print("husk_blender_geoset_mask: no chr_customization_options extras found "
                  "(no derivable ChrModelID at export time, or no armature in the scene) -- "
                  "skipping the customization texture switch")
            return
        layout = read_chr_texture_layout(armature_obj)
        if layout is None:
            print("husk_blender_geoset_mask: chr_customization_options present but no "
                  "chr_texture_layout extras (no --char-layout-id given at export time) -- "
                  "can't place customization textures without real section rects, skipping")
            return
        if not textures_dir:
            print("husk_blender_geoset_mask: chr_customization_options/chr_texture_layout "
                  "present but no '-- <file.glb> --textures <dir>' given -- can't load real "
                  "per-choice textures, skipping the customization texture switch")
            return
        enabled_materials = read_chr_enabled_materials(armature_obj)
        touched = apply_customization_texture_switch(options, layout, enabled_materials, materials,
                                                       textures_dir)
        if touched:
            print(f"husk_blender_geoset_mask: {touched} material(s) got a real live customization "
                  "texture switch -- open the Shader Editor, select the material, press Home to "
                  "frame all nodes, and look for the green '<type> customization' group node; "
                  "every relevant option's own real dropdown is directly editable right there")

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
        ribbon_count, particle_count = apply_emitter_markers(armature_obj)
        if ribbon_count or particle_count:
            print(f"husk_blender_geoset_mask: {ribbon_count} ribbon + {particle_count} particle "
                  "emitter placement marker(s) added -- placeholders, not a real particle-effect "
                  "simulation, see apply_emitter_markers's own doc comment")

    def animation_asset_stage():
        clip_names = read_animation_clip_names(armature_obj)
        if not clip_names:
            return
        marked = mark_actions_as_assets(clip_names)
        named = sum(1 for v in clip_names.values() if v)
        print(f"husk_blender_geoset_mask: {marked} animation Action(s) marked as real Blender "
              f"assets (Asset Browser-pickable) -- {named} renamed to a real AnimationData.db2 "
              "name, the rest keeping husk's own anim_<id>_<variationIndex>/global_seq_<n> name")

    def multiply_blend_compositing_stage():
        touched = apply_multiply_blend_compositing(bpy.context.scene, list(materials))
        if touched:
            print(f"husk_blender_geoset_mask: {touched} material(s) got real Mod/Mod2x "
                  "multiply-blend compositing (compositor node graph built -- render normally, "
                  "F12, to see it)")

    def physics_jiggle_stage():
        if armature_obj is None:
            return
        physics_bodies, physics_joints, joint_bone_names = read_physics_bodies(armature_obj)
        if not physics_bodies:
            return
        enabled, skipped = apply_physics_jiggle_bones(armature_obj, physics_bodies,
                                                        physics_joints, joint_bone_names)
        if enabled:
            print(f"husk_blender_geoset_mask: {enabled} bone(s) wired up as Jiggle Physics "
                  f"jiggle bones ({skipped} joint edge(s) skipped, didn't match a real bone "
                  "parent/child pair) -- enable 'Jiggle' on the armature/scene if this comes back "
                  "off, and tune per-bone elasticity by eye, see apply_physics_jiggle_bones's own "
                  "doc comment on why the numbers are a best-effort heuristic, not an exact port")

    def gear_item_stage():
        gear_items = read_gear_items(armature_obj)
        if not gear_items:
            return
        attached = apply_gear_items(armature_obj, gear_items, filepath)
        print(f"husk_blender_geoset_mask: {attached}/{len(gear_items)} gear_items entry(ies) got "
              "at least one real attachment_<id> placement (case 1 -- standalone-geometry "
              "equipped items; case 2/object-skin overlay isn't rendered by this script yet, see "
              "TODO/EQUIPPED_GEAR_RENDER_TODO.md)")

    _run_stage(model_name, "geoset switch", geoset_stage)
    _run_stage(model_name, "billboard alignment", billboard_stage)
    _run_stage(model_name, "texture-layout overlay", texture_layout_overlay_stage)
    _run_stage(model_name, "customization texture switch", customization_texture_switch_stage)
    _run_stage(model_name, "texture-transform animation", texture_transform_animation_stage)
    _run_stage(model_name, "tint/fade animation", tint_fade_animation_stage)
    _run_stage(model_name, "emitter placement markers", emitter_marker_stage)
    _run_stage(model_name, "animation asset marking", animation_asset_stage)
    _run_stage(model_name, "multiply-blend compositing", multiply_blend_compositing_stage)
    _run_stage(model_name, "physics jiggle bones", physics_jiggle_stage)
    _run_stage(model_name, "gear item attachment", gear_item_stage)


if __name__ == "__main__":
    main()
