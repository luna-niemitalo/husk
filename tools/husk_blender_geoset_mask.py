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
complaint on a real export with 90 modifiers). What it does, in order:

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
  3. For every group with 2+ variants, builds a small subgraph in a new
     Geometry Nodes node group: one `Separate Geometry` per variant,
     chained through its own `Inverted` output so each variant is peeled
     off a running "remainder" in turn (vertex-group membership read via
     `Named Attribute` -- Blender exposes every vertex group as a
     same-named point-domain float attribute automatically, no manual
     conversion needed), then a `Menu Switch` node with one item per
     variant selecting which peeled-off piece is actually output. The
     Menu Switch's own `Menu` input is promoted to a real `NodeSocketMenu`
     entry on the node group's own interface -- this is what actually
     makes it a dropdown in the Modifier panel, not just an internal node
     setting a human would have to open the node editor to change.
     Groups with fewer than 2 variants are left out of this chain
     entirely -- there's nothing mutually exclusive to switch, so their
     geometry simply stays part of the remainder (same "only touch what's
     genuinely ambiguous" policy the superseded Mask-modifier version
     used).
  4. Whatever never got peeled off (untagged geometry, plus every
     single-variant group's own geometry) is the base, always-visible
     remainder -- joined with every group's active Menu Switch selection
     (`Join Geometry`) into the node group's own output.
  5. Adds one Geometry Nodes modifier per mesh object referencing that
     node group, defaulted to the lowest variant ID per group (husk
     doesn't currently resolve real DB2 customization data to pick an
     actual default, same disclaimed-placeholder precedent as
     `orderCandidatesForDefault` elsewhere in this project).
  6. Deletes every geoset tag bone from the armature once its vertex group
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


def _add_variant_separator(node_tree, remainder_socket, vg_name):
    """One (Named Attribute -> Compare -> Separate Geometry) triple: peels
    every vertex belonging to `vg_name` off `remainder_socket`. Returns
    (this_variant's own geometry output, the new remainder to chain the
    next variant from).
    """
    named_attr = node_tree.nodes.new('GeometryNodeInputNamedAttribute')
    named_attr.data_type = 'FLOAT'
    named_attr.inputs[0].default_value = vg_name  # "Name" input

    compare = node_tree.nodes.new('FunctionNodeCompare')
    compare.data_type = 'FLOAT'
    compare.operation = 'GREATER_THAN'
    node_tree.links.new(named_attr.outputs[0], compare.inputs[0])  # B stays the default 0.0

    separate = node_tree.nodes.new('GeometryNodeSeparateGeometry')
    node_tree.links.new(remainder_socket, separate.inputs['Geometry'])
    node_tree.links.new(compare.outputs[0], separate.inputs['Selection'])

    return separate.outputs['Selection'], separate.outputs['Inverted']


def build_geoset_switch_node_group(name, groups):
    """One GeometryNodeTree implementing the whole group/variant switch --
    see this module's own doc comment for the full shape. `groups` is
    `geoset_groups(mesh_obj)`'s own return value.
    """
    node_tree = bpy.data.node_groups.new(name, 'GeometryNodeTree')
    node_tree.interface.new_socket(name="Geometry", in_out='INPUT', socket_type='NodeSocketGeometry')
    node_tree.interface.new_socket(name="Geometry", in_out='OUTPUT', socket_type='NodeSocketGeometry')

    group_input = node_tree.nodes.new('NodeGroupInput')
    group_output = node_tree.nodes.new('NodeGroupOutput')

    remainder = group_input.outputs['Geometry']
    menu_outputs = []  # one Menu Switch "Output" socket per multi-variant group

    for group in sorted(groups):
        variants = sorted(groups[group])  # [(variant, vg_name), ...], lowest variant first
        if len(variants) < 2:
            continue  # nothing mutually exclusive here -- stays in remainder untouched

        variant_selections = []
        for variant, vg_name in variants:
            selection, remainder = _add_variant_separator(node_tree, remainder, vg_name)
            variant_selections.append((variant, selection))

        menu = node_tree.nodes.new('GeometryNodeMenuSwitch')
        menu.data_type = 'GEOMETRY'
        menu.label = f"Geoset group {group}"
        # Fresh nodes start with two placeholder items ("A"/"B") that have
        # to be cleared before adding real ones, or they'd linger as two
        # extra, unwired, always-empty dropdown entries.
        menu.enum_definition.enum_items.clear()
        item_names = []
        for variant, selection_socket in variant_selections:
            item_name = f"variant_{variant}"
            menu.enum_definition.enum_items.new(item_name)
            node_tree.links.new(selection_socket, menu.inputs[item_name])
            item_names.append(item_name)

        # Promote the Menu input to the node group's own interface as a
        # real NodeSocketMenu entry -- this is what makes it a dropdown in
        # the modifier panel, not just an internal node setting. Linked
        # from this group's own new Group Input output, not left as an
        # internal-only default on the Menu Switch node itself. Must link
        # *before* setting default_value -- an unlinked interface Menu
        # socket has no known items yet (its valid enum values are derived
        # from whatever it's connected to), so setting default_value first
        # throws "enum ... not found in ()".
        menu_socket = node_tree.interface.new_socket(
            name=f"Geoset group {group}", in_out='INPUT', socket_type='NodeSocketMenu')
        node_tree.links.new(group_input.outputs[menu_socket.name], menu.inputs[0])
        menu_socket.default_value = item_names[0]  # lowest variant ID visible by default

        menu_outputs.append(menu.outputs[0])

    join = node_tree.nodes.new('GeometryNodeJoinGeometry')
    node_tree.links.new(remainder, join.inputs[0])
    for out in menu_outputs:
        node_tree.links.new(out, join.inputs[0])
    node_tree.links.new(join.outputs[0], group_output.inputs['Geometry'])

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
