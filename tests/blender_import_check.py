# Run headlessly by tests/test_conformance.cpp (blender --background
# --python this_file -- <glb_path>) to answer "does Blender's own glTF
# importer actually make sense of what husk wrote," which the tinygltf
# round-trip checks elsewhere in this suite can't -- tinygltf and
# Blender's importer are two independent implementations of the glTF
# spec, so agreement between them is a real signal, not a tautology.
#
# Prints "HUSK_PROBE key=value" lines the C++ test parses via plain
# substring search (see tests/test_conformance.cpp) and cross-checks
# against the same file's own tinygltf-parsed ground truth -- no
# fixture-specific magic numbers live here or there.
#
# Invoke with --python-exit-code 1 (the C++ test does) so an exception in
# here actually fails the process -- Blender's own default is to exit 0
# even after an unhandled Python exception in --background mode.
import bpy
import sys


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    glb_path = argv[0]

    bpy.ops.import_scene.gltf(filepath=glb_path)

    armatures = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']

    print("HUSK_PROBE armature_count=%d" % len(armatures))
    print("HUSK_PROBE bone_count=%d" % (len(armatures[0].data.bones) if armatures else 0))
    print("HUSK_PROBE mesh_object_count=%d" % len(meshes))
    print("HUSK_PROBE total_vertex_count=%d" % sum(len(o.data.vertices) for o in meshes))
    print("HUSK_PROBE action_count=%d" % len(bpy.data.actions))


if __name__ == "__main__":
    main()
