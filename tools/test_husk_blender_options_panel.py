"""Headless verification for husk_blender_options_panel.py.

Run standalone:
  blender --background --factory-startup --python tools/test_husk_blender_options_panel.py

Builds a synthetic armature carrying real-shaped `chr_customization_options`
bone extras (the same nested-IDProperty structure gltf_skeleton.cpp's real
export produces, not a JSON-string stand-in) and exercises the actual
module functions against it -- not a reimplementation of the logic under
test. Exits nonzero on any failed check.
"""
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import husk_blender_options_panel as panel  # noqa: E402

FAILURES = []


def check(label, cond):
    status = "OK" if cond else "FAIL"
    print(f"[{status}] {label}")
    if not cond:
        FAILURES.append(label)


# Mirrors the real chr_customization_options shape (gltf_skeleton.cpp
# field names: option_id/option_name/option_order_index/choices[],
# choice_id/choice_name/choice_order_index/geoset_id/materials[],
# related_choice_id). Two options with a real cross-option dependency:
# "Face Paint" choice 41 ("War Paint") only applies given "Hair Color"
# choice 21 ("Black") also selected -- modeled the same way husk itself
# resolves it, via a nonzero related_choice_id on the material.
MOCK_OPTIONS = [
    {
        "option_id": 1, "option_name": "Hair Color", "option_order_index": 0,
        "category_id": 2, "category_name": "Face", "category_order_index": 1,
        "choices": [
            {"choice_id": 20, "choice_name": "Blonde", "choice_order_index": 0, "materials": []},
            {"choice_id": 21, "choice_name": "Black", "choice_order_index": 1, "materials": []},
        ],
    },
    {
        # No category_id/category_name at all -- real shape for an option
        # whose ChrCustomizationCategoryID is 0 (see
        # gltf::Skeleton::CustomizationOption::categoryId's own doc
        # comment) -- exercises populate_choice_schema's "(Uncategorized)"
        # fallback bucket.
        "option_id": 2, "option_name": "Face Paint", "option_order_index": 1,
        "choices": [
            {"choice_id": 40, "choice_name": "None", "choice_order_index": 0, "materials": []},
            {
                "choice_id": 41, "choice_name": "War Paint", "choice_order_index": 1,
                "materials": [{"file_data_id": 999, "related_choice_id": 21}],
            },
        ],
    },
]


def build_synthetic_armature():
    arm_data = bpy.data.armatures.new("TestArmatureData")
    arm_obj = bpy.data.objects.new("TestArmature", arm_data)
    bpy.context.collection.objects.link(arm_obj)

    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode='EDIT')
    bone = arm_data.edit_bones.new("root")
    bone.head = (0, 0, 0)
    bone.tail = (0, 0, 1)
    bpy.ops.object.mode_set(mode='OBJECT')

    pose_bone_data = arm_data.bones["root"]
    pose_bone_data["joint_names"] = ["root"]
    pose_bone_data["chr_customization_options"] = MOCK_OPTIONS
    return arm_obj


def build_customization_switch_material(mat_name, option_name, choice_names):
    """Mirrors husk_blender_geoset_mask.py's own real, current node shape
    (`_build_customization_option_group` nested inside
    `_build_material_customization_group`, instanced by
    `apply_customization_texture_switch`): a material whose node tree
    carries one `ShaderNodeGroup` with a `NodeSocketMenu` input named
    `option_name`, itself wired (two group-levels deep, the real shape) to
    an inner `GeometryNodeMenuSwitch` carrying `choice_names` as real enum
    items. Built independently here (not by calling that file's own
    functions) so this test exercises apply_selection/sync_from_node_graph
    against the real socket *shape*, not against that file's current
    implementation of it -- confirmed structurally equivalent via the
    conversation's own direct headless verification before this was
    written (see tools/BLENDER_OPTIONS_PANEL.md).
    """
    sub_tree = bpy.data.node_groups.new(f"{mat_name}_{option_name}_sub", "ShaderNodeTree")
    sub_tree.interface.new_socket("Choice", in_out='INPUT', socket_type='NodeSocketMenu')
    sub_tree.interface.new_socket("Color", in_out='OUTPUT', socket_type='NodeSocketColor')
    s_nodes, s_links = sub_tree.nodes, sub_tree.links
    s_gi = s_nodes.new("NodeGroupInput")
    s_go = s_nodes.new("NodeGroupOutput")
    menu_switch = s_nodes.new("GeometryNodeMenuSwitch")
    menu_switch.data_type = 'BUNDLE'
    menu_switch.enum_items.clear()
    s_links.new(s_gi.outputs["Choice"], menu_switch.inputs["Menu"])
    for name in choice_names:
        menu_switch.enum_items.new(name)

    outer_tree = bpy.data.node_groups.new(f"{mat_name}_outer", "ShaderNodeTree")
    outer_tree.interface.new_socket("Base Color", in_out='INPUT', socket_type='NodeSocketColor')
    outer_tree.interface.new_socket("Color", in_out='OUTPUT', socket_type='NodeSocketColor')
    o_nodes, o_links = outer_tree.nodes, outer_tree.links
    o_gi = o_nodes.new("NodeGroupInput")
    o_go = o_nodes.new("NodeGroupOutput")
    sub_node = o_nodes.new("ShaderNodeGroup")
    sub_node.node_tree = sub_tree
    outer_tree.interface.new_socket(option_name, in_out='INPUT', socket_type='NodeSocketMenu')
    o_links.new(o_gi.outputs[-2], sub_node.inputs["Choice"])
    o_links.new(sub_node.outputs["Color"], o_go.inputs["Color"])

    mat = bpy.data.materials.new(mat_name)
    mat.use_nodes = True
    group_node = mat.node_tree.nodes.new("ShaderNodeGroup")
    group_node.node_tree = outer_tree
    group_node.inputs[option_name].default_value = choice_names[0]  # real default, set at "export" time
    return mat


def build_skinned_mesh(arm_obj, mesh_name, materials):
    mesh = bpy.data.meshes.new(mesh_name)
    mesh.from_pydata([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [], [(0, 1, 2)])
    mesh.update()
    obj = bpy.data.objects.new(mesh_name, mesh)
    bpy.context.collection.objects.link(obj)
    for mat in materials:
        obj.data.materials.append(mat)
    modifier = obj.modifiers.new("Armature", 'ARMATURE')
    modifier.object = arm_obj
    return obj


panel.register()

arm_obj = build_synthetic_armature()

hair_mat = build_customization_switch_material("MatHair", "Hair Color", ["Blonde", "Black"])
paint_mat = build_customization_switch_material("MatPaint", "Face Paint", ["None", "War Paint"])
build_skinned_mesh(arm_obj, "TestMesh", [hair_mat, paint_mat])

# --- extras reading round-trips through the real _root_joint_extras path ---
options = panel.read_chr_customization_options(arm_obj)
check("read_chr_customization_options returns the real 2 options", len(options or []) == 2)
check("option order preserved (Hair Color first)", options[0]["option_name"] == "Hair Color")

# --- population ---
panel.populate_choice_schema(arm_obj, options)
schema = arm_obj.husk_choices
check("schema has one row per option (2)", len(schema.items) == 2)

hair_row = schema.items[0]
paint_row = schema.items[1]
check("row option_name matches", hair_row.option_name == "Hair Color" and paint_row.option_name == "Face Paint")
check("Hair Color row carries its real category name", hair_row.category_name == "Face")
check("Hair Color row carries its real category_id", hair_row.category_id == 2)
check("Face Paint (no real category) falls back to the (Uncategorized) bucket",
      paint_row.category_name == "(Uncategorized)")
check("Face Paint (no real category) keeps category_id 0, not fabricated",
      paint_row.category_id == 0)
check("categorized row sorts before the (Uncategorized) bucket",
      list(schema.items).index(hair_row) < list(schema.items).index(paint_row))

# --- dynamic enum items are independent per row ---
hair_items = hair_row.choice_enum_items(bpy.context)
paint_items = paint_row.choice_enum_items(bpy.context)
check("Hair Color row has 2 enum choices", len(hair_items) == 2)
check("Face Paint row has 2 enum choices, independently resolved", len(paint_items) == 2)
check("enum items cache holds distinct lists per row_key", hair_row.row_key != paint_row.row_key)

# --- dependency status: unmet case ---
hair_row.choice_index = 0  # Blonde
paint_row.choice_index = 1  # War Paint (needs Hair Color == Black/21)
panel.recompute_dependency_status(arm_obj)
check("War Paint flagged 'unmet' when Hair Color != Black", paint_row.dependency_status == "unmet")
check("Hair Color itself has no dependency, so 'ok'", hair_row.dependency_status == "ok")

# --- dependency status: satisfied case ---
hair_row.choice_index = 1  # Black
panel.recompute_dependency_status(arm_obj)
check("War Paint becomes 'ok' once Hair Color == Black", paint_row.dependency_status == "ok")

# --- current_choice() reflects the live index ---
check("current_choice() on paint_row matches choice_id 41", paint_row.current_choice()["choice_id"] == 41)

# --- _customization_group_nodes finds both real material group nodes ---
found = list(panel._customization_group_nodes(arm_obj))
check("_customization_group_nodes finds both fixture materials", len(found) == 2)

# --- sync_from_node_graph reads the LIVE socket value back, not just index 0 ---
# Simulate husk having resolved non-first-choice defaults at export time
# (the real chr_enabled_materials-driven case) directly on the sockets,
# independent of whatever the panel's own schema currently holds.
for mat, node in found:
    if "Hair Color" in node.inputs:
        node.inputs["Hair Color"].default_value = "Black"
    if "Face Paint" in node.inputs:
        node.inputs["Face Paint"].default_value = "War Paint"

panel.populate_choice_schema(arm_obj, options)  # resets both rows to index 0
check("populate reset both rows to index 0 before sync", hair_row.choice_index == 0 and paint_row.choice_index == 0)
panel.sync_from_node_graph(arm_obj)
check("sync_from_node_graph pulled 'Black' (index 1) for Hair Color from the live socket",
      hair_row.choice_index == 1)
check("sync_from_node_graph pulled 'War Paint' (index 1) for Face Paint from the live socket",
      paint_row.choice_index == 1)

# --- apply_selection pushes the panel's choice back into the live sockets ---
hair_row.choice_index = 0  # Blonde
applied = panel.apply_selection(arm_obj)
check("apply_selection reports the Hair Color material as applied",
      any(a[1] == "Hair Color" and a[2] == "Blonde" for a in applied))
live_value = next(node.inputs["Hair Color"].default_value
                   for mat, node in panel._customization_group_nodes(arm_obj)
                   if "Hair Color" in node.inputs)
check("apply_selection's write actually landed on the real socket ('Blonde')", live_value == "Blonde")
check("Face Paint's own socket untouched by an unrelated row's apply_selection call ('War Paint' still)",
      next(node.inputs["Face Paint"].default_value
           for mat, node in panel._customization_group_nodes(arm_obj)
           if "Face Paint" in node.inputs) == "War Paint")

# --- re-population is idempotent (re-export/re-import case) ---
panel.populate_choice_schema(arm_obj, options)
check("re-population resets to row count 2, not accumulating", len(arm_obj.husk_choices.items) == 2)
check("re-population resets choice_index to 0", arm_obj.husk_choices.items[1].choice_index == 0)

# --- operator path (Reload button) ---
bpy.context.view_layer.objects.active = arm_obj
result = bpy.ops.husk.load_customization_options()
check("HUSK_OT_load_customization_options runs cleanly against real extras", result == {'FINISHED'})
check("operator repopulated the schema", len(arm_obj.husk_choices.items) == 2)

# --- graceful no-extras case (a plain, non-husk armature) ---
plain_arm_data = bpy.data.armatures.new("PlainArmatureData")
plain_arm_obj = bpy.data.objects.new("PlainArmature", plain_arm_data)
bpy.context.collection.objects.link(plain_arm_obj)
bpy.context.view_layer.objects.active = plain_arm_obj
result = bpy.ops.husk.load_customization_options()
check("operator on a non-husk armature reports CANCELLED, not an exception", result == {'CANCELLED'})

# =======================================================================
# Geoset-switch integration (apply_geoset_selection / sync_geoset_from_modifier)
# -- isolated in its own armature/mesh so it doesn't interfere with the
# material-switch fixture's row-count assertions above.
# =======================================================================

MOCK_GEOSET_OPTIONS = [
    {
        "option_id": 3, "option_name": "Ears", "option_order_index": 0,
        "choices": [
            {"choice_id": 30, "choice_name": "Short Fin", "choice_order_index": 0,
             "geoset_id": 307, "materials": []},
            {"choice_id": 31, "choice_name": "Long Fin", "choice_order_index": 1,
             "geoset_id": 308, "materials": []},
        ],
    },
]


def build_geoset_switch_fixture(arm_obj, group, variant_names_and_geosets):
    """Mirrors husk_blender_geoset_mask.py's own real, current shape
    (`build_geoset_switch_node_group`/`_build_group_hidden_term`,
    `apply_geoset_switch`): a `HuskGeosetSwitch` Geometry Nodes modifier
    whose node tree promotes one `NodeSocketMenu` input named
    `f"Ears (group {group})"`, wired to a `GeometryNodeMenuSwitch` whose
    per-choice value sockets carry real `"group_<n>,variant_<n>"` vertex-
    group name strings -- built independently here (not by calling that
    file's own functions), same "exercise the real shape, not that file's
    current implementation" approach the material-switch fixture above
    uses. `variant_names_and_geosets` is `[(label, variant), ...]`.
    """
    tree = bpy.data.node_groups.new("GeosetSwitchFixture", "GeometryNodeTree")
    tree.interface.new_socket(name="Geometry", in_out='INPUT', socket_type='NodeSocketGeometry')
    tree.interface.new_socket(name="Geometry", in_out='OUTPUT', socket_type='NodeSocketGeometry')
    nodes, links = tree.nodes, tree.links
    gi = nodes.new("NodeGroupInput")
    go = nodes.new("NodeGroupOutput")

    selector = nodes.new("GeometryNodeMenuSwitch")
    selector.data_type = 'STRING'
    selector.enum_definition.enum_items.clear()
    for label, variant in variant_names_and_geosets:
        selector.enum_definition.enum_items.new(label)
        selector.inputs[label].default_value = f"group_{group},variant_{variant}"

    menu_socket = tree.interface.new_socket(
        name=f"Ears (group {group})", in_out='INPUT', socket_type='NodeSocketMenu')
    links.new(gi.outputs[menu_socket.name], selector.inputs[0])
    links.new(gi.outputs["Geometry"], go.inputs["Geometry"])
    menu_socket.default_value = variant_names_and_geosets[0][0]

    mesh = bpy.data.meshes.new("GeosetMesh")
    mesh.from_pydata([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [], [(0, 1, 2)])
    mesh.update()
    mesh_obj = bpy.data.objects.new("GeosetMesh", mesh)
    bpy.context.collection.objects.link(mesh_obj)
    mod = mesh_obj.modifiers.new("HuskGeosetSwitch", 'NODES')
    mod.node_group = tree
    modifier = mesh_obj.modifiers.new("Armature", 'ARMATURE')
    modifier.object = arm_obj
    return mesh_obj, mod


arm_obj_geoset = build_synthetic_armature()
_geoset_mesh_obj, _geoset_mod = build_geoset_switch_fixture(
    arm_obj_geoset, group=3, variant_names_and_geosets=[("Short Fin", 7), ("Long Fin", 8)])
panel.populate_choice_schema(arm_obj_geoset, MOCK_GEOSET_OPTIONS)
ears_row = arm_obj_geoset.husk_choices.items[0]

check("geoset fixture defaults to Short Fin (index 0)", ears_row.choice_index == 0)

# --- apply_geoset_selection pushes the panel's choice into the real modifier ---
ears_row.choice_index = 1  # Long Fin, geoset_id 308
applied = panel.apply_geoset_selection(arm_obj_geoset)
check("apply_geoset_selection reports the Ears row as applied",
      any(a[1] == "Ears" and a[2] == "Long Fin" for a in applied))

menu_item = next(i for i in _geoset_mod.node_group.interface.items_tree if i.name == "Ears (group 3)")
selector_node = next(n for n in _geoset_mod.node_group.nodes if n.bl_idname == 'GeometryNodeMenuSwitch')
long_fin_input = selector_node.inputs["Long Fin"]
expected_int = int(long_fin_input.identifier.split('_', 1)[1])
check("apply_geoset_selection's write landed the correct real modifier int value",
      _geoset_mod[menu_item.identifier] == expected_int)

# --- sync_geoset_from_modifier reads the LIVE modifier state back ---
ears_row.choice_index = 0  # reset to Short Fin before syncing
panel.sync_geoset_from_modifier(arm_obj_geoset)
check("sync_geoset_from_modifier pulled 'Long Fin' (index 1) back from the live modifier",
      ears_row.choice_index == 1)

# --- already_resolved skip: a pre-resolved option is left untouched ---
ears_row.choice_index = 0
panel.sync_geoset_from_modifier(arm_obj_geoset, already_resolved={"Ears"})
check("sync_geoset_from_modifier skips an option already in already_resolved",
      ears_row.choice_index == 0)

panel.unregister()
check("unregister() runs cleanly", True)

print("\n=== SUMMARY ===")
if FAILURES:
    print(f"{len(FAILURES)} FAILURE(S):")
    for f in FAILURES:
        print(f"  - {f}")
    sys.exit(1)
else:
    print("All checks passed.")
    sys.exit(0)
