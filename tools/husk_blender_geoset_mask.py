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

What it does, in order:
  1. Finds every "group_<n>,variant_<n>" vertex group husk's tag-joint
     mechanism produced (one per distinct M2 geoset ID -- Skeleton::
     GeosetTag, src/gltf_skeleton.hpp) on the mesh object(s) -- created
     automatically by Blender's own glTF importer as a side effect of
     ordinary skin-weight import, no custom parsing needed here. The
     comma-separated, prefix-tagged naming (rather than one combined
     "geoset_<id>" token) is deliberate: a future geometry-nodes-based
     rewrite of this same idea (GEOSET_MASK_TODO.md) can recover the raw
     group/variant integers with a plain comma-split + prefix-strip.
  2. Groups them by geoset *group* (the `group_<n>` field, same numbering
     husk's own glTF extras use, e.g. geoset_id 1702 -> group 17) --
     variants within one group are mutually exclusive alternates (e.g. five
     hairstyle options).
  3. Picks the lowest-ID variant per group as the visible default -- husk
     has no DB2 customization-choice data to pick a "correct" one (see
     DESIGN.md's Non-goals), so this is a deterministic placeholder, the
     same "no real answer, pick something consistent and let a human
     override" precedent this project already follows elsewhere (e.g.
     export_materials.cpp's orderCandidatesForDefault).
  4. Adds one Mask modifier per *non-default* variant, invert mode ("hide
     this vertex group, show the rest"), stacked -- toggle a modifier's
     Enabled checkbox to swap which variant is shown instead of the
     default.
  5. Deletes every geoset tag bone from the armature once its vertex group
     has been used to build step 4's masks. Verified empirically
     (GEOSET_MASK_TODO.md): Blender vertex groups are mesh-owned data,
     independent of the armature's bones -- removing the bone afterward
     doesn't touch the group or the masks built from it, it only declutters
     the armature back to its real bone count and removes the exported
     .glb's inflated joint count (see GEOSET_MASK_TODO.md's "Known
     tradeoff" section) from the finalized Blender scene a human actually
     works in.
"""

import sys

import bpy

GROUP_PREFIX = "group_"
VARIANT_PREFIX = "variant_"


def parse_geoset_vgroup_name(name):
    """'group_<n>,variant_<n>' -> (group, variant) ints, or None if not a match.

    Plain comma-split + prefix-strip, not a regex, deliberately -- this is
    meant to double as the reference implementation for how a future
    geometry-nodes-based rewrite of this script (GEOSET_MASK_TODO.md) would
    parse the same name, which is exactly why husk emits it in this
    field-separated shape instead of one combined "geoset_<id>" token.
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


def apply_geoset_masks(mesh_obj):
    """Adds the Mask-modifier stack; returns (groups, hidden_variant_names)."""
    groups = geoset_groups(mesh_obj)
    hidden = []
    for variants in groups.values():
        if len(variants) < 2:
            continue  # a single variant has nothing mutually exclusive to hide
        variants.sort()
        for _variant, vg_name in variants[1:]:  # keep variants[0] (lowest variant ID) visible
            mod = mesh_obj.modifiers.new(name=f"mask_{vg_name}", type='MASK')
            mod.vertex_group = vg_name
            mod.invert_vertex_group = True
            hidden.append(vg_name)
    return groups, hidden


def delete_geoset_tag_bones(armature_obj, all_groups):
    """Removes every group_<n>,variant_<n> bone -- their vertex groups/masks survive this untouched."""
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
    total_hidden = 0
    for mesh_obj in mesh_objs:
        groups, hidden = apply_geoset_masks(mesh_obj)
        total_hidden += len(hidden)
        for gid, variants in groups.items():
            all_groups.setdefault(gid, []).extend(variants)

    removed = delete_geoset_tag_bones(armature_obj, all_groups)
    print(f"husk_blender_geoset_mask: {len(all_groups)} geoset group(s) across "
          f"{len(mesh_objs)} mesh object(s), {total_hidden} variant(s) masked, "
          f"{removed} tag bone(s) removed")


if __name__ == "__main__":
    main()
