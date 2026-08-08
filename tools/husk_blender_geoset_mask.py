"""husk_blender_geoset_mask.py -- companion Blender tooling for `husk
export`'s geoset-tag joints (GEOSET_MASK_TODO.md). Not part of husk itself
(DESIGN.md scopes husk to "read WoW formats, write correct glTF" --
Blender-side concerns are explicitly out of that scope) -- this is the
deferred "companion Blender-side script that hides extras-tagged-but-
visible geometry post-import" DESIGN.md's Key design decisions already
anticipated for exactly this shape of problem.

Run inside Blender, e.g.:
  blender --python tools/husk_blender_geoset_mask.py -- model.glb
(imports model.glb first), or with no trailing args, operates on whatever
mesh+armature objects are already in the current scene (a model already
imported via Blender's own File > Import, or the Scripting tab).

Builds a Geometry Nodes setup, one Menu Switch dropdown per geoset group,
instead of the Mask-modifier stack an earlier version of this script used
("an insane stack of mask modifiers" -- prompted directly, real usability
complaint on a real export with 90 modifiers).

**Second real design, same reason as the Mask-modifier -> Geometry Nodes
rewrite before it**: the first Geometry Nodes version chained `Separate
Geometry` sequentially (one variant peeled off a shrinking "remainder" at
a time). Real interactive use found two bugs (unrelated geometry
disappearing on an unrelated group's switch; some geometry never toggling
at all -- see `GEOSET_MASK_TODO.md`'s "Known bugs"), and this session's
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
     node group, defaulted to the lowest real variant ID per group (never
     "none" by default -- husk doesn't currently resolve real DB2
     customization data to pick an actual default, same disclaimed-
     placeholder precedent as `orderCandidatesForDefault` elsewhere in
     this project).
  9. Deletes every geoset tag bone from the armature once its vertex group
     has been read into the node graph above. Verified empirically
     (GEOSET_MASK_TODO.md): Blender vertex groups (and the point-domain
     attributes Geometry Nodes reads them as) are mesh-owned data,
     independent of the armature's bones -- removing the bone afterward
     doesn't touch the attribute or the node graph built from it, it only
     declutters the armature back to its real bone count and removes the
     exported .glb's inflated joint count (see GEOSET_MASK_TODO.md's
     "Known tradeoff" section) from the finalized Blender scene a human
     actually works in.
"""

import sys

import bpy

GROUP_PREFIX = "group_"
VARIANT_PREFIX = "variant_"
NONE_ITEM_NAME = "none"
# Never a real vertex-group name (husk's own naming is always
# "group_<n>,variant_<n>") -- Named Attribute reading this returns 0 for
# every vertex, which is exactly what "no variant selected" should mean.
NONE_SENTINEL_ATTR = "__husk_geoset_none__"


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


def _build_group_hidden_term(node_tree, group_input, group, variants):
    """One geoset group's contribution to the overall "hidden" expression --
    see this module's own doc comment, point 4, for the shape. `variants`
    is `[(variant, vg_name), ...]`, lowest variant first. Returns a boolean
    output socket: true for a vertex that belongs to this group but isn't
    part of its currently-selected choice.
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
    menu_socket.default_value = item_names[0]  # lowest real variant ID visible by default

    is_selected = _named_attr_gt_zero(node_tree, selector.outputs[0])
    not_selected = _bool_math(node_tree, 'NOT', is_selected)
    return _bool_math(node_tree, 'AND', owns_group, not_selected)


def build_geoset_switch_node_group(name, groups):
    """One GeometryNodeTree implementing the whole group/variant switch --
    see this module's own doc comment for the full shape. `groups` is
    `geoset_groups(mesh_obj)`'s own return value. Builds one combined
    "is this vertex hidden" boolean expression across every multi-variant
    group first (no geometry operations at all), then applies exactly one
    `Separate Geometry` to the pristine input mesh at the very end --
    deliberately not a chain of per-variant separations (the design this
    replaced), which real use found could lose geometry at compounding
    selection boundaries across up to 109 sequential operations.
    """
    node_tree = bpy.data.node_groups.new(name, 'GeometryNodeTree')
    node_tree.interface.new_socket(name="Geometry", in_out='INPUT', socket_type='NodeSocketGeometry')
    node_tree.interface.new_socket(name="Geometry", in_out='OUTPUT', socket_type='NodeSocketGeometry')

    group_input = node_tree.nodes.new('NodeGroupInput')
    group_output = node_tree.nodes.new('NodeGroupOutput')

    hidden_terms = [
        _build_group_hidden_term(node_tree, group_input, group, sorted(groups[group]))
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
    returns this mesh object's own `geoset_groups` result.
    """
    groups = geoset_groups(mesh_obj)
    if not groups:
        return groups
    node_tree = build_geoset_switch_node_group(f"{mesh_obj.name}_geoset_switch", groups)
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


def main():
    argv = sys.argv
    if "--" in argv:
        extra_args = argv[argv.index("--") + 1:]
        if extra_args:
            bpy.ops.import_scene.gltf(filepath=extra_args[0])

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


if __name__ == "__main__":
    main()
