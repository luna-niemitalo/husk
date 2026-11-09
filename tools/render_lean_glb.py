"""Headless Blender script: import ONE .glb with zero husk-specific
post-processing and render one deterministic still PNG.

Meant for `husk export --glb`'s lean projection (src/writers/gltf_lean.hpp):
whatever a bare glTF importer does with core mesh/skin/material/animation
data, nothing more. That projection carries no options, presets or effects,
so the render is a geometry/skin/base-material preview, not the in-game look.

Usage:
    blender --background --factory-startup --python render_lean_glb.py -- <in.glb> <out.png>

Always renders the bind/rest pose (frame 1, no animation scrubbing): one
fixed, reproducible state. Exits nonzero (Blender's own traceback to
stderr) on failure.
"""

import math
import sys

import bpy
import mathutils

RESOLUTION = (640, 480)
EEVEE_SAMPLES = 32  # a preview of one file: accuracy over throughput


def _world_bbox(mesh_objs):
    bbox_min = mathutils.Vector((math.inf, math.inf, math.inf))
    bbox_max = mathutils.Vector((-math.inf, -math.inf, -math.inf))
    for obj in mesh_objs:
        for corner in obj.bound_box:
            world_corner = obj.matrix_world @ mathutils.Vector(corner)
            bbox_min = mathutils.Vector(min(a, b) for a, b in zip(bbox_min, world_corner))
            bbox_max = mathutils.Vector(max(a, b) for a, b in zip(bbox_max, world_corner))
    return bbox_min, bbox_max


def _create_camera(radius: float, center: "mathutils.Vector"):
    cam_data = bpy.data.cameras.new("cam")
    cam_obj = bpy.data.objects.new("cam", cam_data)
    bpy.context.scene.collection.objects.link(cam_obj)
    cam_data.lens = 35
    cam_data.sensor_fit = "VERTICAL"
    half_fov = math.atan((cam_data.sensor_height / 2) / cam_data.lens)
    distance = (radius / math.sin(half_fov)) * 1.03
    cam_dir = mathutils.Vector((1, -1.6, 0.5)).normalized()
    cam_obj.location = center + cam_dir * distance
    cam_obj.rotation_euler = (center - cam_obj.location).to_track_quat("-Z", "Y").to_euler()
    cam_data.clip_end = distance + radius * 1.1
    return cam_obj


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:]
    in_glb, out_path = argv[0], argv[1]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=in_glb, disable_bone_shape=True)

    mesh_objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not mesh_objs:
        print("SKIPPED no mesh objects imported (file has 0 vertices)")
        return

    # Force the literal bind/rest pose -- clearing animation_data.action
    # alone is NOT enough: Blender's glTF importer can push imported clips
    # onto NLA tracks, which keep influencing pose evaluation independently
    # of whatever .action is currently assigned (an unmuted NLA strip still
    # evaluates at the current frame even with action=None). Setting
    # pose_position = 'REST' instead bypasses pose evaluation entirely --
    # every bone shows its literal edit-bone rest transform, regardless of
    # actions/NLA state -- the only way to guarantee both files render the
    # SAME fixed state rather than whatever each file's own first clip/NLA
    # track happens to evaluate to at frame 1.
    for obj in bpy.context.scene.objects:
        if obj.type == "ARMATURE":
            obj.data.pose_position = "REST"
    bpy.context.view_layer.update()

    bbox_min, bbox_max = _world_bbox(mesh_objs)
    center = (bbox_min + bbox_max) / 2
    radius = max((bbox_max - bbox_min).length / 2, 0.01)

    cam_obj = _create_camera(radius, center)
    bpy.context.scene.camera = cam_obj

    sun_data = bpy.data.lights.new("sun", type="SUN")
    sun_obj = bpy.data.objects.new("sun", sun_data)
    sun_obj.rotation_euler = (math.radians(55), 0, math.radians(35))
    bpy.context.scene.collection.objects.link(sun_obj)

    fill_data = bpy.data.lights.new("fill", type="SUN")
    fill_data.energy = 0.5
    fill_obj = bpy.data.objects.new("fill", fill_data)
    fill_obj.rotation_euler = (math.radians(-40), 0, math.radians(-120))
    bpy.context.scene.collection.objects.link(fill_obj)

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.eevee.taa_render_samples = EEVEE_SAMPLES
    scene.eevee.use_shadows = True
    scene.eevee.use_fast_gi = False
    scene.render.resolution_x = RESOLUTION[0]
    scene.render.resolution_y = RESOLUTION[1]
    scene.render.filepath = out_path
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.use_file_extension = False

    bpy.ops.render.render(write_still=True)
    print(f"OK rendered {len(mesh_objs)} mesh object(s), bbox radius {radius:.3f} -> {out_path}")


if __name__ == "__main__":
    main()
