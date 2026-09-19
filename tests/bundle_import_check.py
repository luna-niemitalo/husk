# Run headlessly by tests/test_conformance.cpp (blender --background
# --python this_file -- <bundle_dir>) to answer "does the native husk
# bundle (REFACTOR/BUNDLE_FORMAT.md, written by writers::writeBundle) carry
# the same mesh/skeleton content as the legacy glTF pipeline" -- WITHOUT
# ever going through glTF or bpy.ops.import_scene.gltf on the canon side.
# This is the whole point (REFACTOR/BLENDER_ADDON.md's "Import becomes:
# point at a manifest. Nothing else."): reads manifest.json + the raw
# .bin slices directly, per bundle_writer.hpp's own doc comment, which is
# the one canonical description of this schema.
#
# This is a diagnostic probe for test_conformance.cpp, not the real
# packaged addon (REFACTOR/BLENDER_ADDON.md) -- mesh/skeleton/base-color
# materials only, no animation, no geosets, no customization, no
# blend-op/tint/UV-animation shading. It exists to let a
# --export-canon bundle be checked against real Blender-side counts the
# same way blender_import_check.py already does for the legacy .glb, so
# the two pipelines can be compared without gltf as an intermediary on
# either side eventually needing to agree file-for-file.
#
# Prints "HUSK_PROBE key=value" lines, same convention and parser
# (test_conformance.cpp's parseProbeInt) as blender_import_check.py.
import bpy
import json
import os
import struct
import sys

_COMPONENT_FORMATS = {
    "f32": "f",
    "u32": "I",
    "u16": "H",
    "u8": "B",
    "i32": "i",
}


def read_slice(bundle_dir, slice_json):
    """A BufferSlice (bundle_writer.hpp's doc comment) -> a flat list of
    numbers, `component_count` values per element, `count` elements."""
    fmt_char = _COMPONENT_FORMATS[slice_json["component_type"]]
    component_count = slice_json["component_count"]
    count = slice_json["count"]
    path = os.path.join(bundle_dir, slice_json["file"])
    with open(path, "rb") as f:
        f.seek(slice_json["byte_offset"])
        raw = f.read(slice_json["byte_length"])
    total = count * component_count
    values = struct.unpack("<%d%s" % (total, fmt_char), raw)
    if component_count == 1:
        return list(values)
    return [values[i:i + component_count] for i in range(0, total, component_count)]


def build_materials(bundle_dir, materials_json):
    """One real Blender Material per resources.materials[] entry
    (bundle_writer.hpp's doc comment) -- base color only, from whichever
    layer actually resolved to real bytes. No blend-op/tint/UV-animation
    wiring yet (canon::MaterialLayer::blendIntoPrevious is a texture-
    combiner op, not an alpha mode, and this probe checks import counts,
    not shading correctness)."""
    materials = []
    for i, mat_json in enumerate(materials_json):
        mat = bpy.data.materials.new("husk_bundle_material_%d" % i)
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")

        # The first layer that actually resolved to a fetched texture --
        # canon doesn't name a single "base color" layer explicitly
        # (diffuse_layer/specular_layer/... are optional Refs pointing
        # back into `layers` by identity), so this probe takes the
        # simplest real answer: the first resolved-with-bytes layer.
        for layer in mat_json.get("layers", []):
            texture = layer.get("texture")
            if layer.get("texture_state") != "resolved" or not texture or not texture.get("uri"):
                continue
            image_path = os.path.join(bundle_dir, texture["uri"])
            image = bpy.data.images.load(image_path)
            tex_node = mat.node_tree.nodes.new("ShaderNodeTexImage")
            tex_node.image = image
            if bsdf is not None:
                mat.node_tree.links.new(tex_node.outputs["Color"], bsdf.inputs["Base Color"])
            break

        materials.append(mat)
    return materials


def build_mesh(bundle_dir, mesh_json, materials):
    positions = read_slice(bundle_dir, mesh_json["positions"])
    indices = read_slice(bundle_dir, mesh_json["indices"])
    faces = [tuple(indices[i:i + 3]) for i in range(0, len(indices), 3)]

    mesh = bpy.data.meshes.new("husk_bundle_mesh")
    mesh.from_pydata(positions, [], faces)
    mesh.update()

    for mat in materials:
        mesh.materials.append(mat)

    # Each primitive's own "material_index" (writeMeshSection's doc
    # comment) is already resolved via canon::resolveMaterialIndex into
    # resources.materials -- a plain per-face-range assignment, one
    # contiguous polygon range per primitive (index_start/index_count are
    # in units of the flat triangle-index buffer, /3 for polygon indices).
    for prim in mesh_json["primitives"]:
        first_poly = prim["index_start"] // 3
        poly_count = prim["index_count"] // 3
        for p in range(first_poly, first_poly + poly_count):
            mesh.polygons[p].material_index = prim["material_index"]

    obj = bpy.data.objects.new("husk_bundle_mesh", mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def build_armature(bundle_dir, skeleton_json):
    joint_count = skeleton_json["joint_count"]
    if joint_count == 0:
        return None

    parents = read_slice(bundle_dir, skeleton_json["parents"])
    local_translation = read_slice(bundle_dir, skeleton_json["bind_translation"])

    armature = bpy.data.armatures.new("husk_bundle_armature")
    obj = bpy.data.objects.new("husk_bundle_armature", armature)
    bpy.context.scene.collection.objects.link(obj)

    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')

    # bind_translation is PARENT-RELATIVE (bundle_writer.hpp's doc comment,
    # writers::localBindTranslation) -- absolute head position is the
    # accumulated sum up the parent chain, computed here via memoized
    # recursion so this doesn't depend on `parents` being topologically
    # sorted (canon::Skeleton itself states no such ordering guarantee).
    absolute = [None] * joint_count

    def absolute_position(i):
        if absolute[i] is None:
            p = parents[i]
            base = absolute_position(p) if p >= 0 else (0.0, 0.0, 0.0)
            local = local_translation[i]
            absolute[i] = tuple(base[k] + local[k] for k in range(3))
        return absolute[i]

    edit_bones = [None] * joint_count
    for i in range(joint_count):
        eb = armature.edit_bones.new("bone_%d" % i)
        head = absolute_position(i)
        eb.head = head
        # No real tail direction available from bind pose alone (M2 bind
        # pose carries no baked rotation/scale, canon::Joint's own doc
        # comment) -- a fixed short offset avoids Blender's zero-length
        # bone rejection; this probe checks counts/hierarchy, not bone
        # shape.
        eb.tail = (head[0], head[1], head[2] + 0.01)
        edit_bones[i] = eb
    for i in range(joint_count):
        p = parents[i]
        if p >= 0:
            edit_bones[i].parent = edit_bones[p]

    bpy.ops.object.mode_set(mode='OBJECT')
    return obj


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    bundle_dir = argv[0]

    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)

    with open(os.path.join(bundle_dir, "manifest.json"), "r") as f:
        manifest = json.load(f)

    resources = manifest["resources"]
    materials = build_materials(bundle_dir, resources.get("materials", []))
    build_mesh(bundle_dir, resources["mesh"], materials)
    armature_obj = build_armature(bundle_dir, resources["skeleton"])

    armatures = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']

    print("HUSK_PROBE armature_count=%d" % len(armatures))
    print("HUSK_PROBE bone_count=%d" % (len(armature_obj.data.bones) if armature_obj else 0))
    print("HUSK_PROBE mesh_object_count=%d" % len(meshes))
    print("HUSK_PROBE total_vertex_count=%d" % sum(len(o.data.vertices) for o in meshes))

    print("HUSK_PROBE material_count=%d" % len(materials))
    real_images = [img for img in bpy.data.images if img.name not in ("Render Result", "Viewer Node")]
    widths = [img.size[0] for img in real_images]
    heights = [img.size[1] for img in real_images]
    print("HUSK_PROBE loaded_image_count=%d" % len(real_images))
    print("HUSK_PROBE min_image_width=%d" % (min(widths) if widths else -1))
    print("HUSK_PROBE min_image_height=%d" % (min(heights) if heights else -1))


if __name__ == "__main__":
    main()
