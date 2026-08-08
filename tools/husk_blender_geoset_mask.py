"""husk_blender_geoset_mask.py -- companion Blender tooling for `husk
export`'s geoset-tag joints (TODO/GEOSET_MASK_TODO.md) and, since this
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
at all -- see `TODO/GEOSET_MASK_TODO.md`'s "Known bugs"), and this session's
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
  3. For every group with 2+ variants, adds a real "none" choice alongside
     its real M2 variants -- prompted directly, a real gap found in
     testing: `bloodelffemale_hd.m2`'s own tabard group (12) only has
     variants for "both flaps"/"back only"/"front only", never "no
     tabard at all", because the M2 itself simply has no submesh for that
     case (there's no real geoset ID to tag -- husk can only tag what
     exists in the file). "none" is a synthetic addition on the Blender
     side, not something husk's export needs to change for.
  4. Per group with 2+ variants (real or "none"), builds two small pieces:
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
     has been read into the node graph above. Verified empirically
     (TODO/GEOSET_MASK_TODO.md): Blender vertex groups (and the point-domain
     attributes Geometry Nodes reads them as) are mesh-owned data,
     independent of the armature's bones -- removing the bone afterward
     doesn't touch the attribute or the node graph built from it, it only
     declutters the armature back to its real bone count and removes the
     exported .glb's inflated joint count (see TODO/GEOSET_MASK_TODO.md's
     "Known tradeoff" section) from the finalized Blender scene a human
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
"""

import json
import re
import struct
import sys

import bpy

GROUP_PREFIX = "group_"
VARIANT_PREFIX = "variant_"
NONE_ITEM_NAME = "none"
# Never a real vertex-group name (husk's own naming is always
# "group_<n>,variant_<n>") -- Named Attribute reading this returns 0 for
# every vertex, which is exactly what "no variant selected" should mean.
NONE_SENTINEL_ATTR = "__husk_geoset_none__"

# Per-model curated default geoset selections, found by hand in Blender's
# real GUI -- husk itself has no DB2 customization-choice data to ground a
# "correct" default in (this module's own doc comment, point 8), so this is
# a real, disclosed, per-model override table, not something derived or
# guessed. Keyed by the model's own real name (the mesh object's name, minus
# any Blender-assigned ".001"-style dedup suffix -- see `_model_key`); each
# value is `{group: item_name}` where `item_name` is `"variant_<n>"` (a real
# M2 geoset variant ID for that group) or `"none"`. A group missing from a
# model's own dict, or a model missing from this dict entirely, falls back
# to the pre-existing "lowest real variant ID" default.
CURATED_DEFAULT_VARIANTS = {
    "bloodelffemale_hd": {
        0: "variant_0",
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

    hidden_terms = [
        _build_group_hidden_term(node_tree, group_input, group, sorted(groups[group]),
                                  default_overrides.get(group))
        for group in sorted(groups)
        if len(groups[group]) >= 2  # a single variant has nothing mutually exclusive to hide
    ]

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


def apply_geoset_switch(mesh_obj):
    """Builds the node group and adds it as a Geometry Nodes modifier;
    returns this mesh object's own `geoset_groups` result. Looks up
    `CURATED_DEFAULT_VARIANTS` by this mesh object's own real name (see
    `_model_key`) to pick real, hand-verified defaults over the plain
    lowest-variant fallback, when available for this model.
    """
    groups = geoset_groups(mesh_obj)
    if not groups:
        return groups
    default_overrides = CURATED_DEFAULT_VARIANTS.get(_model_key(mesh_obj.name), {})
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


def read_chr_texture_layout(filepath):
    """Reads `chr_texture_layout` straight out of the exported file's own
    raw glTF JSON (skins[].extras) -- see the module docstring for why
    Blender's own importer can't be used for this one piece of data.
    Returns None if `filepath` is falsy, isn't a .glb/.gltf husk could have
    written, or genuinely has no such extras (no --char-layout-id was given
    at export time) -- never a guess, never a hard failure of the rest of
    this script's work.
    """
    if not filepath:
        return None
    try:
        if filepath.lower().endswith(".gltf"):
            with open(filepath, "r", encoding="utf-8") as f:
                data = json.load(f)
        else:
            with open(filepath, "rb") as f:
                raw = f.read()
            # glTF binary container: 12-byte header (magic/version/length),
            # then chunks of (length: u32, type: u32, data). The first chunk
            # is always JSON per the glTF 2.0 spec -- no need to scan for it.
            (chunk_length, _chunk_type) = struct.unpack_from("<II", raw, 12)
            data = json.loads(raw[20:20 + chunk_length])
    except (OSError, ValueError, struct.error):
        return None

    for skin in data.get("skins", []):
        extras = skin.get("extras")
        if extras and "chr_texture_layout" in extras:
            return extras["chr_texture_layout"]
    return None


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

        group_node = node_tree.nodes.new("ShaderNodeGroup")
        group_node.node_tree = overlay_group
        group_node.inputs["Show Overlay"].default_value = False

        emission = node_tree.nodes.new("ShaderNodeEmission")
        emission.inputs["Color"].default_value = (1.0, 0.0, 1.0, 1.0)

        mix = node_tree.nodes.new("ShaderNodeMixShader")
        node_tree.links.new(group_node.outputs["Factor"], mix.inputs["Fac"])
        node_tree.links.new(original_socket, mix.inputs[1])
        node_tree.links.new(emission.outputs["Emission"], mix.inputs[2])
        node_tree.links.new(mix.outputs["Shader"], output_node.inputs["Surface"])

        touched += 1
    return touched


def main():
    argv = sys.argv
    filepath = None
    if "--" in argv:
        extra_args = argv[argv.index("--") + 1:]
        if extra_args:
            filepath = extra_args[0]
            bpy.ops.import_scene.gltf(filepath=filepath)

    mesh_objs, armature_obj = find_mesh_and_armature()

    all_groups = {}
    switch_groups = 0
    for mesh_obj in mesh_objs:
        groups = apply_geoset_switch(mesh_obj)
        switch_groups += sum(1 for variants in groups.values() if len(variants) >= 2)
        for gid, variants in groups.items():
            all_groups.setdefault(gid, []).extend(variants)

    removed = delete_geoset_tag_bones(armature_obj, all_groups)
    print(f"husk_blender_geoset_mask: {len(all_groups)} geoset group(s) across "
          f"{len(mesh_objs)} mesh object(s), {switch_groups} dropdown switch(es) built, "
          f"{removed} tag bone(s) removed")

    layout = read_chr_texture_layout(filepath)
    if layout is None:
        print("husk_blender_geoset_mask: no chr_texture_layout extras found "
              "(no --char-layout-id given at export time, or no file path given here) -- "
              "skipping the texture-layout overlay")
    else:
        materials = {obj.material_slots[i].material
                     for obj in mesh_objs
                     for i in range(len(obj.material_slots))
                     if obj.material_slots[i].material is not None}
        touched = apply_texture_layout_overlay(layout, materials)
        print(f"husk_blender_geoset_mask: chr_texture_layout {layout.get('layout_id')} -- "
              f"{touched} material(s) got a toggleable section-boundary overlay "
              f"(off by default; enable 'Show Overlay' on the HuskChrTextureLayoutOverlay "
              f"node in the Shader Editor)")


if __name__ == "__main__":
    main()
