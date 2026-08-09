"""Headless Blender script: import one .glb, frame it, render one WebP image.

Usage:
    blender --background --factory-startup --python render_glb.py -- <in.glb> <out.webp>

`out_path`'s own extension is trusted exactly as given (use_file_extension
is disabled below) -- the caller decides the format by what it names the
output, this script doesn't infer one. WebP (quality 80, lossy) chosen over
the original PNG output for a lighter-weight corpus-wide gallery -- these
are flat-shaded QA thumbnails, not archival renders, so lossy compression
costs nothing that matters here.

Exits nonzero (via a bare exception, letting Blender's own traceback print
to stderr) on any failure -- the caller captures stdout+stderr and stores
it as this file's failure detail, same "let the tool print its own real
error" discipline as husk's own ParseError text.
"""
import math
import sys

import bpy
import mathutils


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:]
    in_glb, out_path = argv[0], argv[1]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    # disable_bone_shape=True: the importer's default per-armature "Icosphere"
    # bone-visualization mesh is a real MESH-type scene object with no relation
    # to husk's own exported geometry -- left enabled, it silently inflates the
    # combined bounding box used for camera framing below (Luna caught this
    # from a render where the character was tiny in-frame).
    bpy.ops.import_scene.gltf(filepath=in_glb, disable_bone_shape=True)

    mesh_objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not mesh_objs:
        # A real, expected shape for some corpus files (e.g. cameras/*.m2
        # cinematic camera-track models: 0 vertices by design, per `husk
        # info`) -- not a husk/Blender defect, so this is a distinct
        # "SKIPPED" sentinel, not a crash/failure.
        print("SKIPPED no mesh objects imported (file has 0 vertices -- camera/track-only model)")
        return

    bbox_min = mathutils.Vector((math.inf, math.inf, math.inf))
    bbox_max = mathutils.Vector((-math.inf, -math.inf, -math.inf))
    for obj in mesh_objs:
        for corner in obj.bound_box:
            world_corner = obj.matrix_world @ mathutils.Vector(corner)
            bbox_min = mathutils.Vector(min(a, b) for a, b in zip(bbox_min, world_corner))
            bbox_max = mathutils.Vector(max(a, b) for a, b in zip(bbox_max, world_corner))

    center = (bbox_min + bbox_max) / 2
    radius = max((bbox_max - bbox_min).length / 2, 0.01)

    cam_data = bpy.data.cameras.new("cam")
    cam_obj = bpy.data.objects.new("cam", cam_data)
    bpy.context.scene.collection.objects.link(cam_obj)
    cam_data.lens = 35  # wide-ish, generous headroom over a tight fit
    cam_data.sensor_fit = "VERTICAL"  # most WoW models are tall/thin -- frame by height, not width
    half_fov = math.atan((cam_data.sensor_height / 2) / cam_data.lens)
    distance = (radius / math.sin(half_fov)) * 1.03  # 3% margin
    cam_dir = mathutils.Vector((1, -1.6, 0.5)).normalized()
    cam_obj.location = center + cam_dir * distance
    cam_obj.rotation_euler = (center - cam_obj.location).to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.camera = cam_obj

    sun_data = bpy.data.lights.new("sun", type="SUN")
    sun_data.energy = 5.0
    sun_obj = bpy.data.objects.new("sun", sun_data)
    sun_obj.rotation_euler = (math.radians(55), 0, math.radians(35))
    bpy.context.scene.collection.objects.link(sun_obj)

    fill_data = bpy.data.lights.new("fill", type="SUN")
    fill_data.energy = 1.2
    fill_obj = bpy.data.objects.new("fill", fill_data)
    fill_obj.rotation_euler = (math.radians(-40), 0, math.radians(-120))
    bpy.context.scene.collection.objects.link(fill_obj)

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 640
    scene.render.resolution_y = 480
    scene.render.filepath = out_path
    # use_file_extension defaults to True (auto-appends the format's own
    # extension) -- disabled so the caller's own extension is trusted
    # exactly as given, never doubled up (e.g. "foo.webp" -> "foo.webp.webp").
    scene.render.use_file_extension = False
    scene.render.image_settings.file_format = "WEBP"
    scene.render.image_settings.quality = 80  # lossy: these are flat-shaded QA thumbnails, not archival
    scene.world = bpy.data.worlds.new("world")
    scene.world.color = (0.12, 0.12, 0.14)

    bpy.ops.render.render(write_still=True)
    print(f"OK rendered {len(mesh_objs)} mesh object(s), bbox radius {radius:.3f} -> {out_path}")


if __name__ == "__main__":
    main()
