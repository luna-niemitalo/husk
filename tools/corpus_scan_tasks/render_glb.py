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


def fix_additive_materials() -> int:
    """WoW blend modes 3 (NoAlphaAdd) and 4 (Add) have no core-glTF
    equivalent (src/gltf_mesh.hpp's Material::blendMode doc comment) --
    husk's default-import shape (Principled BSDF + alpha-blend/hashed) shows
    such a material's near-opaque, mostly-black background as a solid dark
    panel instead of contributing nothing where it's black, which is what
    WoW's own additive compositing actually does. Confirmed wrong against a
    real corpus file (creature/celestialfoxwyvern/celestialfoxwyvern.m2:
    a constellation-style effect that should read as glowing lines on
    nothing, not a dark diamond panel).

    husk exports the real blend_mode as material extras whenever it's > 2
    (see that same doc comment) -- Blender's glTF importer surfaces
    material extras as plain custom properties on the imported material
    (confirmed empirically: unlike glTF *skin* extras, which land nowhere,
    node/mesh/material/camera/light/scene extras all become real bpy custom
    properties post-import). This rebuilds each such material's shader as
    real additive -- Transparent BSDF + Emission (fed by the same
    base-color image the importer already wired into Base Color) combined
    via Add Shader -- in place of the standard import's Principled BSDF
    wiring.

    Modes 5/6 (Mod/Mod2x, multiply) are a real, separate gap, not attempted
    here -- no demonstrated real-corpus case driving it yet, and multiply
    compositing needs a different node shape (no Add Shader equivalent).
    Returns the number of materials rebuilt, purely for the caller's own
    stdout note.
    """
    fixed = 0
    for mat in bpy.data.materials:
        if mat.get("blend_mode") not in (3, 4):
            continue
        if not mat.use_nodes or mat.node_tree is None:
            continue
        nodes = mat.node_tree.nodes
        links = mat.node_tree.links
        output = next((n for n in nodes if n.type == "OUTPUT_MATERIAL"), None)
        if output is None:
            continue

        # Two different importer shapes reach this point, depending on
        # whether husk also set KHR_materials_unlit on this material (WoW's
        # own M2Material flag 0x01 -- real, common alongside additive blend
        # modes, e.g. every material on the celestialfoxwyvern fixture this
        # was verified against): a "lit" import gives a Principled BSDF
        # with Base Color already wired to the right texture; an "unlit"
        # import gives Blender's own KHR_materials_unlit emulation graph,
        # which already *has* a real Emission node fed by that texture --
        # reused directly rather than rebuilt, so the correct source is
        # never in question.
        emission = next((n for n in nodes if n.type == "EMISSION"), None)
        if emission is None:
            principled = next((n for n in nodes if n.type == "BSDF_PRINCIPLED"), None)
            if principled is None:
                continue
            base_color_input = principled.inputs.get("Base Color")
            emission = nodes.new("ShaderNodeEmission")
            emission.location = (principled.location.x, principled.location.y - 200)
            if base_color_input.is_linked:
                links.new(base_color_input.links[0].from_socket, emission.inputs["Color"])
            else:
                emission.inputs["Color"].default_value = base_color_input.default_value

        # Additive blending is unconditional pass-through of the background
        # (Transparent BSDF below) plus this texture's own RGB scaled by its
        # own alpha -- alpha isn't a coverage mask here (nothing occludes in
        # Add mode), it's how much light each pixel contributes. Confirmed
        # against a real corpus file's own bytes: item/objectcomponents/
        # weapon/staff_2h_artifactheartofkure_d_02.m2's glow-ring texture
        # carries a genuine 0..255 alpha gradient under an otherwise
        # uniformly-bright RGB region (alphaextract crop of the source .blp,
        # not a guess) -- both the Blender importer's own unlit-material
        # graph (a Mix Shader gated by an Alpha threshold) and a Principled
        # BSDF's separate Alpha input already account for this; wiring only
        # Color into Emission and dropping Alpha rendered the whole textured
        # area at full brightness regardless of alpha, turning a should-fade
        # glow ring into a hard-edged, inverted-looking donut. Premultiplying
        # here restores that shape from the same texture node's own Alpha
        # output feeding Color -- whichever branch above set that link.
        color_link = emission.inputs["Color"].links[0] if emission.inputs["Color"].is_linked else None
        if color_link is not None and "Alpha" in color_link.from_node.outputs:
            premultiply = nodes.new("ShaderNodeVectorMath")
            premultiply.operation = "MULTIPLY"
            premultiply.location = (emission.location.x - 200, emission.location.y - 50)
            links.new(color_link.from_socket, premultiply.inputs[0])
            links.new(color_link.from_node.outputs["Alpha"], premultiply.inputs[1])
            links.new(premultiply.outputs["Vector"], emission.inputs["Color"])

        transparent = next((n for n in nodes if n.type == "BSDF_TRANSPARENT"), None)
        if transparent is None:
            transparent = nodes.new("ShaderNodeBsdfTransparent")
            transparent.location = (emission.location.x, emission.location.y - 150)

        add_shader = nodes.new("ShaderNodeAddShader")
        add_shader.location = (emission.location.x + 200, emission.location.y - 75)
        links.new(transparent.outputs["BSDF"], add_shader.inputs[0])
        links.new(emission.outputs["Emission"], add_shader.inputs[1])
        links.new(add_shader.outputs["Shader"], output.inputs["Surface"])

        mat.surface_render_method = "BLENDED"  # EEVEE Next: real compositing, not DITHERED's noisy hash
        fixed += 1
    return fixed


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
    fixed_additive = fix_additive_materials()

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
    # Blender's camera default clip_end (1000) silently culls the entire
    # object -- a fully blank render, no error -- for any model whose real
    # posed bounding-box radius exceeds ~322 units (default clip_end / sin
    # half_fov). Real, not hypothetical: creature/dimensiusboss02.m2's root
    # bone carries a constant (every one of its 8 sequences agrees) 20x
    # scale, a genuine large-creature authoring pattern, pushing its own
    # camera distance to ~1601. clip_end must bracket the far side of the
    # bounding sphere from the camera, not just the default; clip_start
    # stays at its default since the near side is never the problem here.
    cam_data.clip_end = distance + radius * 1.1
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
    extra = f", {fixed_additive} additive material(s) rebuilt" if fixed_additive else ""
    print(f"OK rendered {len(mesh_objs)} mesh object(s), bbox radius {radius:.3f}{extra} -> {out_path}")


if __name__ == "__main__":
    main()
