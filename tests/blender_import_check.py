# Run headlessly by tests/test_conformance.cpp (blender --background
# --python this_file -- <glb_path> [bone_name ...]) to answer "does
# Blender's own glTF importer actually make sense of what husk wrote,"
# which the tinygltf round-trip checks elsewhere in this suite can't --
# tinygltf and Blender's importer are two independent implementations of
# the glTF spec, so agreement between them is a real signal, not a
# tautology.
#
# Prints "HUSK_PROBE key=value" lines the C++ test parses via plain
# substring search (see tests/test_conformance.cpp) and cross-checks
# against ground truth read independently of Blender -- no fixture-specific
# magic numbers live here.
#
# Invoke with --python-exit-code 1 (the C++ test does) so an exception in
# here actually fails the process -- Blender's own default is to exit 0
# even after an unhandled Python exception in --background mode.
import bpy
import sys


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    glb_path = argv[0]
    named_bones = argv[1:]

    # --factory-startup still loads the stock startup scene (Cube/Camera/
    # Light) -- clear it so object/vertex counts below reflect only what
    # the .glb itself contains, not Blender's own defaults.
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)

    # disable_bone_shape=True: the importer's own armature_display() (see
    # io_scene_gltf2/blender/imp/node.py) otherwise creates a real
    # 42-vertex Icosphere mesh object (parked in a hidden, "not for
    # export" collection) purely as the bone custom-shape widget --
    # invisible in the viewport by convention, but still a real object in
    # bpy.data.objects, silently inflating mesh/vertex counts below.
    bpy.ops.import_scene.gltf(filepath=glb_path, disable_bone_shape=True)

    armatures = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']

    print("HUSK_PROBE armature_count=%d" % len(armatures))
    print("HUSK_PROBE bone_count=%d" % (len(armatures[0].data.bones) if armatures else 0))
    print("HUSK_PROBE mesh_object_count=%d" % len(meshes))
    print("HUSK_PROBE total_vertex_count=%d" % sum(len(o.data.vertices) for o in meshes))
    print("HUSK_PROBE action_count=%d" % len(bpy.data.actions))

    # A failed/unresolved image still shows up in bpy.data.images, just with
    # size (0, 0) -- min_image_width/height across every real loaded image
    # (excluding Blender's own always-present "Render Result"/"Viewer Node"
    # compositor images, which the .glb never declared) is 0 iff at least
    # one image failed to decode.
    real_images = [img for img in bpy.data.images if img.name not in ("Render Result", "Viewer Node")]
    print("HUSK_PROBE loaded_image_count=%d" % len(real_images))
    widths = [img.size[0] for img in real_images]
    heights = [img.size[1] for img in real_images]
    print("HUSK_PROBE min_image_width=%d" % (min(widths) if widths else -1))
    print("HUSK_PROBE min_image_height=%d" % (min(heights) if heights else -1))

    # Coordinate-frame probe support (see tests/test_conformance.cpp's
    # synthetic-skeleton transform test): any armature bone whose name
    # starts with "husk_probe_" gets its world-space head position printed
    # as three floats. Asset-agnostic by construction -- the probe skeleton
    # is a minimal fabricated armature built purely to measure whether a
    # known local-space offset lands where a correct Z-up -> Y-up ->
    # (Blender's own Y-up -> Z-up import) round trip says it should.
    for arm in armatures:
        for bone in arm.data.bones:
            if not bone.name.startswith("husk_probe_"):
                continue
            head = arm.matrix_world @ bone.head_local
            print("HUSK_PROBE %s=%.6f,%.6f,%.6f" % (bone.name, head.x, head.y, head.z))

    # Bones the caller names on the command line (e.g. a real key bone,
    # found by its glTF node name): world head position, for real-content
    # orientation cross-checks on top of the synthetic probe above.
    for name in named_bones:
        for arm in armatures:
            bone = arm.data.bones.get(name)
            if bone is not None:
                head = arm.matrix_world @ bone.head_local
                print("HUSK_PROBE bone:%s=%.6f,%.6f,%.6f" % (name, head.x, head.y, head.z))


if __name__ == "__main__":
    main()
