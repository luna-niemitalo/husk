# Consumer probe for `husk export-terrain` bundles
# (REFACTOR/BUNDLE_FORMAT.md's "Terrain tile bundles"): reads
# manifest.json + .bin slices directly -- and, for placements/ground effects
# with a `uri`, the referenced model bundles -- builds the scene in Blender,
# and prints "HUSK_PROBE key=value" consistency checks (same convention as
# bundle_import_check.py). Not the real addon.
#
#   blender --background --factory-startup --python terrain_bundle_import_check.py -- \
#       <tile_bundle_dir>... [--render out.png] [--save out.blend] [--ground-effect-scale 0.5]
#
# --render writes out.png (overview) and out_close.png (ground level), EEVEE.
import json
import math
import os
import random
import struct
import sys

import bpy
import numpy as np
from mathutils import Quaternion, Vector

_FORMATS = {"f32": "f", "u32": "I", "u16": "H", "u8": "B", "i32": "i"}


def read_slice(bundle_dir, s):
    with open(os.path.join(bundle_dir, s["file"]), "rb") as f:
        f.seek(s["byte_offset"])
        raw = f.read(s["byte_length"])
    n = s["count"] * s["component_count"]
    flat = struct.unpack("<%d%s" % (n, _FORMATS[s["component_type"]]), raw)
    k = s["component_count"]
    return list(flat) if k == 1 else [flat[i:i + k] for i in range(0, n, k)]


def probe(key, value):
    print("HUSK_PROBE %s=%s" % (key, value))


def corner(r, c):
    return r * 17 + c


def centre(r, c):
    return r * 17 + 9 + c


def vertex_xy(origin, idx, quad):
    row, col = divmod(idx, 17)
    if col < 9:
        return origin[0] - row * quad, origin[1] - col * quad
    return origin[0] - (row + 0.5) * quad, origin[1] - (col - 9 + 0.5) * quad


def fan(r, c):
    """The 4 triangles of quad (r, c), wound counter-clockwise seen from +Z."""
    m, tl, tr, bl, br = centre(r, c), corner(r, c), corner(r, c + 1), corner(r + 1, c), corner(r + 1, c + 1)
    return [(m, tr, tl), (m, br, tr), (m, bl, br), (m, tl, bl)]


class Tile:
    def __init__(self, bundle_dir):
        self.dir = bundle_dir
        self.t = json.load(open(os.path.join(bundle_dir, "manifest.json")))["terrain"]
        ch = self.t["chunks"]
        self.quad = self.t["quad_size"]
        self.origins = read_slice(bundle_dir, ch["origin"])
        self.heights = read_slice(bundle_dir, ch["heights"])
        self.normals = read_slice(bundle_dir, ch["normals"])
        self.holes = read_slice(bundle_dir, ch["hole_rows"])
        self.layers = ch.get("layers")
        self.dominant = read_slice(bundle_dir, ch["dominant_layer"]) if "dominant_layer" in ch else None
        self.suppressed = read_slice(bundle_dir, ch["ground_effect_suppressed_rows"]) if "ground_effect_suppressed_rows" in ch else None
        self.nw = self.origins[0]

    def tile_uv(self, x, y):
        size = self.t["tile_size"]
        return (self.nw[1] - y) / size, 1.0 - (self.nw[0] - x) / size

    def height_at(self, ci, r, c, fr, fc):
        """Height inside quad (r, c) of chunk ci at fractional position (fr, fc) in [0,1), via the fan."""
        h = self.heights[ci]
        # Pick the fan triangle containing the point, then interpolate barycentrically in quad-local space.
        pts = {centre(r, c): (0.5, 0.5), corner(r, c): (0, 0), corner(r, c + 1): (0, 1),
               corner(r + 1, c): (1, 0), corner(r + 1, c + 1): (1, 1)}
        for tri in fan(r, c):
            (a, b, d) = (pts[i] for i in tri)
            det = (b[0] - a[0]) * (d[1] - a[1]) - (d[0] - a[0]) * (b[1] - a[1])
            l1 = ((fr - a[0]) * (d[1] - a[1]) - (d[0] - a[0]) * (fc - a[1])) / det
            l2 = ((b[0] - a[0]) * (fc - a[1]) - (fr - a[0]) * (b[1] - a[1])) / det
            l0 = 1 - l1 - l2
            if min(l0, l1, l2) >= -1e-6:
                return l0 * h[tri[0]] + l1 * h[tri[1]] + l2 * h[tri[2]]
        return h[centre(r, c)]


def build_terrain(tile):
    t, quad = tile.t, tile.quad
    verts, faces, uvs = [], [], []
    winding_dots, hole_quads = [], 0
    for ci in range(t["chunks"]["count"]):
        base = len(verts)
        for i in range(145):
            x, y = vertex_xy(tile.origins[ci], i, quad)
            verts.append((x, y, tile.heights[ci][i]))
        for r in range(8):
            for c in range(8):
                if (tile.holes[ci][r] >> c) & 1:
                    hole_quads += 1
                    continue
                for tri in fan(r, c):
                    faces.append(tuple(base + k for k in tri))
                p0, p1, p2 = (Vector(verts[base + k]) for k in fan(r, c)[0])
                fn = (p1 - p0).cross(p2 - p0).normalized()
                winding_dots.append(fn.dot(Vector(tile.normals[ci * 145 + centre(r, c)])))
    mesh = bpy.data.meshes.new("terrain")
    mesh.from_pydata(verts, [], faces)
    mesh.normals_split_custom_set_from_vertices([tuple(n) for n in tile.normals])
    uv_layer = mesh.uv_layers.new(name="tile")
    for loop in mesh.loops:
        x, y, _ = verts[loop.vertex_index]
        uv_layer.data[loop.index].uv = tile.tile_uv(x, y)
    obj = bpy.data.objects.new("terrain_%d_%d" % (t["tile_x"], t["tile_y"]), mesh)
    bpy.context.collection.objects.link(obj)
    probe("terrain_vertices", len(verts))
    probe("terrain_triangles", len(faces))
    probe("hole_quads", hole_quads)
    probe("winding_vs_stored_normal_mean_cos", round(sum(winding_dots) / len(winding_dots), 4))

    seam = 0.0
    for gy in range(16):
        for gx in range(16):
            ci = gy * 16 + gx
            h = tile.heights
            if gx < 15:
                seam = max(seam, max(abs(h[ci][corner(r, 8)] - h[ci + 1][corner(r, 0)]) for r in range(9)))
            if gy < 15:
                seam = max(seam, max(abs(h[ci][corner(8, c)] - h[ci + 16][corner(0, c)]) for c in range(9)))
    probe("chunk_seam_max_dz", seam)
    return obj


def build_weight_maps(tile):
    """One tile-wide 1024x1024 weight map per texture: base layer gets 1 - sum(alpha), others their alpha."""
    n_tex = len(tile.t["textures"])
    weights = np.zeros((n_tex, 1024, 1024), dtype=np.float32)
    seam_err, seam_n = 0.0, 0
    alpha_by_chunk = []
    for ci, layers in enumerate(tile.layers):
        gy, gx = divmod(ci, 16)
        alphas = [np.array(read_slice(tile.dir, l["alpha"]), dtype=np.float32).reshape(64, 64) / 255.0
                  if "alpha" in l else None for l in layers]
        alpha_by_chunk.append({l["texture_index"]: a for l, a in zip(layers, alphas) if a is not None})
        rest = sum(a for a in alphas if a is not None) if len(alphas) > 1 else np.zeros((64, 64), np.float32)
        for l, a in zip(layers, alphas):
            w = a if a is not None else np.clip(1.0 - rest, 0.0, 1.0)
            weights[l["texture_index"], gy * 64:(gy + 1) * 64, gx * 64:(gx + 1) * 64] += w
    # Alpha continuity across chunk borders for a texture both chunks blend
    # with an alpha map -- a transposed or flipped decode breaks this.
    for ci in range(256):
        gy, gx = divmod(ci, 16)
        for nci, edge in ((ci + 1, "col") if gx < 15 else (None, None), (ci + 16, "row") if gy < 15 else (None, None)):
            if nci is None:
                continue
            for tex, a in alpha_by_chunk[ci].items():
                b = alpha_by_chunk[nci].get(tex)
                if b is None:
                    continue
                pair = (a[:, 63], b[:, 0]) if edge == "col" else (a[63, :], b[0, :])
                seam_err += float(np.abs(pair[0] - pair[1]).mean())
                seam_n += 1
    probe("alpha_edge_mean_abs_diff", round(seam_err / max(seam_n, 1), 4))
    # Same check against the transposed reading, as a control.
    t_err, t_n = 0.0, 0
    for ci in range(256):
        gy, gx = divmod(ci, 16)
        if gx < 15:
            for tex, a in alpha_by_chunk[ci].items():
                b = alpha_by_chunk[ci + 1].get(tex)
                if b is not None:
                    t_err += float(np.abs(a.T[:, 63] - b.T[:, 0]).mean())
                    t_n += 1
    probe("alpha_edge_mean_abs_diff_if_transposed", round(t_err / max(t_n, 1), 4))
    images = []
    for i in range(n_tex):
        img = bpy.data.images.new("weight_%d" % i, 1024, 1024, alpha=False, float_buffer=True)
        rgba = np.zeros((1024, 1024, 4), dtype=np.float32)
        # Same value in R, G and B so a colour-to-float conversion reads it unchanged.
        rgba[..., :3] = weights[i][::-1, :, None]  # image row 0 is the bottom (south) edge
        rgba[..., 3] = 1.0
        img.colorspace_settings.name = "Non-Color"
        img.pixels.foreach_set(rgba.ravel())
        img.pack()
        images.append(img)
    coverage = weights.sum(axis=0)
    probe("splat_weight_sum_min_mean_max", "%.3f/%.3f/%.3f" % (coverage.min(), coverage.mean(), coverage.max()))
    return images


def build_terrain_material(tile, weight_images):
    mat = bpy.data.materials.new("terrain")
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.9
    uv = nt.nodes.new("ShaderNodeTexCoord")
    total = None
    loaded = []
    for i, tex in enumerate(tile.t["textures"]):
        d = tex["diffuse"]
        if d.get("texture_state") != "resolved" or "uri" not in d["texture"]:
            continue
        mapping = nt.nodes.new("ShaderNodeMapping")
        repeats = 16 * tex["repeats_per_chunk"]
        mapping.inputs["Scale"].default_value = (repeats, repeats, 1)
        nt.links.new(uv.outputs["UV"], mapping.inputs["Vector"])
        color = nt.nodes.new("ShaderNodeTexImage")
        color.image = bpy.data.images.load(os.path.join(tile.dir, d["texture"]["uri"]))
        # _s terrain textures carry specular in alpha, not coverage.
        color.image.alpha_mode = "CHANNEL_PACKED"
        loaded.append(color.image.size[0] > 0)
        nt.links.new(mapping.outputs["Vector"], color.inputs["Vector"])
        weight = nt.nodes.new("ShaderNodeTexImage")
        weight.image = weight_images[i]
        weight.extension = "EXTEND"
        nt.links.new(uv.outputs["UV"], weight.inputs["Vector"])
        scaled = nt.nodes.new("ShaderNodeVectorMath")
        scaled.operation = "SCALE"
        nt.links.new(color.outputs["Color"], scaled.inputs[0])
        nt.links.new(weight.outputs["Color"], scaled.inputs["Scale"])
        if total is None:
            total = scaled
        else:
            add = nt.nodes.new("ShaderNodeVectorMath")
            add.operation = "ADD"
            nt.links.new(total.outputs["Vector"], add.inputs[0])
            nt.links.new(scaled.outputs["Vector"], add.inputs[1])
            total = add
    if total is not None:
        nt.links.new(total.outputs["Vector"], bsdf.inputs["Base Color"])
    probe("terrain_textures_loaded", "%d/%d" % (sum(loaded), len(tile.t["textures"])))
    return mat


def build_liquids(tile):
    t, quad = tile.t, tile.quad
    lverts, lfaces = [], []
    below, above = 0, 0
    for l in t["liquids"]:
        ci = l["chunk_grid_y"] * 16 + l["chunk_grid_x"]
        rect = l["quad_rect"]
        w, h = rect["width"], rect["height"]
        lh = read_slice(tile.dir, l["heights"])
        exists = read_slice(tile.dir, l["quad_exists"])
        base = len(lverts)
        for i in range(h + 1):
            for j in range(w + 1):
                lverts.append((tile.origins[ci][0] - (rect["y"] + i) * quad,
                               tile.origins[ci][1] - (rect["x"] + j) * quad, lh[i * (w + 1) + j]))
        for i in range(h):
            for j in range(w):
                if not exists[i * w + j]:
                    continue
                a = base + i * (w + 1) + j
                lfaces.append((a, a + w + 1, a + w + 2, a + 1))
                tz = tile.heights[ci][centre(rect["y"] + i, rect["x"] + j)]
                water = sum(lh[k] for k in (a - base, a - base + 1, a - base + w + 1, a - base + w + 2)) / 4
                if tz <= water:
                    below += 1
                else:
                    above += 1
    probe("liquid_surfaces", len(t["liquids"]))
    probe("liquid_quads", len(lfaces))
    probe("liquid_quads_terrain_below_water", below)
    probe("liquid_quads_terrain_above_water", above)
    if not lverts:
        return None
    m = bpy.data.meshes.new("liquid")
    m.from_pydata(lverts, [], lfaces)
    obj = bpy.data.objects.new("liquid", m)
    bpy.context.collection.objects.link(obj)
    mat = bpy.data.materials.new("liquid")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (0.05, 0.18, 0.25, 1)
    bsdf.inputs["Roughness"].default_value = 0.05
    bsdf.inputs["Alpha"].default_value = 0.85
    m.materials.append(mat)
    return obj


_model_cache = {}


def load_model(bundle_dir, uri):
    """A model bundle (bundle_writer.hpp schema) as one shared mesh datablock, static bind pose."""
    path = os.path.normpath(os.path.join(bundle_dir, os.path.dirname(uri)))
    if path in _model_cache:
        return _model_cache[path]
    res = json.load(open(os.path.join(path, "manifest.json")))["resources"]
    mj = res["mesh"]
    positions = read_slice(path, mj["positions"])
    indices = read_slice(path, mj["indices"])
    uv0 = read_slice(path, mj["uv0"])
    mesh = bpy.data.meshes.new(os.path.basename(path))
    mesh.from_pydata(positions, [], [tuple(indices[i:i + 3]) for i in range(0, len(indices), 3)])
    uvl = mesh.uv_layers.new(name="uv0")
    for loop in mesh.loops:
        u, v = uv0[loop.vertex_index]
        uvl.data[loop.index].uv = (u, 1.0 - v)
    for mat_json in res["materials"]:
        mat = bpy.data.materials.new(mesh.name)
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes["Principled BSDF"]
        for layer in mat_json.get("layers", []):
            tex = layer.get("texture")
            if layer.get("texture_state") != "resolved" or not tex or "uri" not in tex:
                continue
            node = mat.node_tree.nodes.new("ShaderNodeTexImage")
            node.image = bpy.data.images.load(os.path.join(path, tex["uri"]), check_existing=True)
            mat.node_tree.links.new(node.outputs["Color"], bsdf.inputs["Base Color"])
            # framebuffer_blend is the M2 render blend mode. Opaque ignores
            # texture alpha; alpha_key (foliage) clips it at 0.5; every other
            # mode blends it. A bundle without the field (older exports)
            # falls back to clipping, right for foliage and harmless for
            # opaque textures with a full alpha channel.
            blend = mat_json.get("framebuffer_blend", "alpha_key")
            if blend == "alpha_key":
                clip = mat.node_tree.nodes.new("ShaderNodeMath")
                clip.operation = "GREATER_THAN"
                clip.inputs[1].default_value = 0.5
                mat.node_tree.links.new(node.outputs["Alpha"], clip.inputs[0])
                mat.node_tree.links.new(clip.outputs["Value"], bsdf.inputs["Alpha"])
            elif blend != "opaque":
                mat.node_tree.links.new(node.outputs["Alpha"], bsdf.inputs["Alpha"])
            break
        mesh.materials.append(mat)
    for prim in mj["primitives"]:
        for p in range(prim["index_start"] // 3, (prim["index_start"] + prim["index_count"]) // 3):
            mesh.polygons[p].material_index = prim["material_index"]
    _model_cache[path] = mesh
    return mesh


def build_placements(tile, seen):
    """`seen` holds unique_ids already placed: a placement straddling a tile
    border is listed by every tile it touches."""
    near, measured, linked, duplicates = 0, 0, 0, 0
    coll = bpy.data.collections.new("placements")
    bpy.context.scene.collection.children.link(coll)
    for p in tile.t["placements"]:
        if p["unique_id"] in seen:
            duplicates += 1
            continue
        seen.add(p["unique_id"])
        x, y, z = p["position"]
        if p["kind"] == "model":
            gy = int((tile.nw[0] - x) // tile.t["chunk_size"])
            gx = int((tile.nw[1] - y) // tile.t["chunk_size"])
            if 0 <= gx < 16 and 0 <= gy < 16:
                ci = gy * 16 + gx
                fr = (tile.origins[ci][0] - x) / tile.quad
                fc = (tile.origins[ci][1] - y) / tile.quad
                r, c = min(int(fr), 7), min(int(fc), 7)
                measured += 1
                near += abs(tile.height_at(ci, r, c, fr - r, fc - c) - z) < 1.0
        uri = p["asset"].get("uri")
        if not uri:
            continue
        linked += 1
        obj = bpy.data.objects.new("placement_%d" % p["unique_id"], load_model(tile.dir, uri))
        obj.location = (x, y, z)
        qx, qy, qz, qw = p["rotation"]
        obj.rotation_mode = "QUATERNION"
        obj.rotation_quaternion = Quaternion((qw, qx, qy, qz))
        obj.scale = (p["scale"],) * 3
        coll.objects.link(obj)
    probe("placements", len(tile.t["placements"]))
    probe("placements_already_placed_by_earlier_tile", duplicates)
    probe("placements_with_model_uri", linked)
    probe("doodads_within_1yd_of_terrain_surface", "%d/%d" % (near, measured))


def build_ground_effects(tile, scale):
    """Scatter detail doodads per quad: the quad's dominant layer picks the
    effect, `density` (scaled) is treated as doodads per quad -- an
    unverified unit -- and doodads are drawn by relative weight."""
    effects = tile.t.get("ground_effects", [])
    if not effects or tile.layers is None:
        probe("ground_effect_points", 0)
        return
    rng = random.Random(1234)
    points = {}
    for ci, layers in enumerate(tile.layers):
        for r in range(8):
            for c in range(8):
                if (tile.holes[ci][r] >> c) & 1 or (tile.suppressed[ci][r] >> c) & 1:
                    continue
                li = tile.dominant[ci][r * 8 + c]
                if li >= len(layers) or "ground_effect_index" not in layers[li]:
                    continue
                eff = effects[layers[li]["ground_effect_index"]]
                doodads = [d for d in eff["doodads"] if d["model"].get("uri")]
                if not doodads or not eff.get("density"):
                    continue
                count = eff["density"] * scale
                count = int(count) + (rng.random() < count - int(count))
                for _ in range(count):
                    d = rng.choices(doodads, weights=[max(dd["weight"], 1) for dd in doodads])[0]
                    fr, fc = rng.random(), rng.random()
                    x = tile.origins[ci][0] - (r + fr) * tile.quad
                    y = tile.origins[ci][1] - (c + fc) * tile.quad
                    points.setdefault(d["model"]["uri"], []).append((x, y, tile.height_at(ci, r, c, fr, fc)))
    total = 0
    for uri, pts in points.items():
        mesh = load_model(tile.dir, uri)
        cloud = bpy.data.meshes.new("ge_points")
        cloud.from_pydata(pts, [], [])
        obj = bpy.data.objects.new("ground_effect_" + mesh.name, cloud)
        bpy.context.collection.objects.link(obj)
        # Vertex instancing: the child mesh is drawn once per parent vertex.
        obj.instance_type = "VERTS"
        child = bpy.data.objects.new("ge_src_" + mesh.name, mesh)
        bpy.context.collection.objects.link(child)
        child.parent = obj
        total += len(pts)
    probe("ground_effect_models", len(points))
    probe("ground_effect_points", total)


def setup_render(path):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.eevee.taa_render_samples = 16
    scene.render.resolution_x = 1600
    scene.render.resolution_y = 1000
    scene.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("sky")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.55, 0.7, 0.9, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
    scene.world = world
    sun_data = bpy.data.lights.new("sun", "SUN")
    sun_data.energy = 3.5
    sun = bpy.data.objects.new("sun", sun_data)
    sun.rotation_euler = (math.radians(50), 0, math.radians(140))
    scene.collection.objects.link(sun)


def render_from(location, target, lens, path):
    scene = bpy.context.scene
    cam_data = bpy.data.cameras.new("cam")
    cam_data.lens = lens
    cam_data.clip_end = 5000
    cam = bpy.data.objects.new("cam", cam_data)
    scene.collection.objects.link(cam)
    cam.location = location
    cam.rotation_euler = (Vector(target) - Vector(location)).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    bundle_dirs = []
    for a in argv:
        if a.startswith("--"):
            break
        bundle_dirs.append(a)
    # Absolute: Blender resolves a bare relative output path against its own idea of the base dir.
    render_path = os.path.abspath(argv[argv.index("--render") + 1]) if "--render" in argv else None
    save_path = os.path.abspath(argv[argv.index("--save") + 1]) if "--save" in argv else None
    ge_scale = float(argv[argv.index("--ground-effect-scale") + 1]) if "--ground-effect-scale" in argv else 0.5

    tiles, terrains, seen_placements = [], [], set()
    for bundle_dir in bundle_dirs:
        tile = Tile(bundle_dir)
        terrain = build_terrain(tile)
        if tile.layers is not None and tile.t.get("textures"):
            terrain.data.materials.append(build_terrain_material(tile, build_weight_maps(tile)))
        build_liquids(tile)
        build_placements(tile, seen_placements)
        build_ground_effects(tile, ge_scale)
        tiles.append(tile)
        terrains.append(terrain)

    if save_path:
        bpy.ops.wm.save_as_mainfile(filepath=save_path)
    if render_path:
        setup_render(render_path)
        size = tiles[0].t["tile_size"]
        xs = [t.nw[0] - size / 2 for t in tiles]
        ys = [t.nw[1] - size / 2 for t in tiles]
        zs = [v.co.z for terrain in terrains for v in terrain.data.vertices]
        cx, cy, cz = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, (min(zs) + max(zs)) / 2
        span = max(max(xs) - min(xs), max(ys) - min(ys)) + size
        render_from((cx - 0.8 * span, cy - 0.8 * span, cz + 0.55 * span), (cx, cy, cz), 35, render_path)
        # Ground level, looking north from just south of the middle tile's centre.
        mid = min(tiles, key=lambda t: abs(t.nw[0] - size / 2 - cx) + abs(t.nw[1] - size / 2 - cy))
        gz = mid.height_at(8 * 16 + 8, 4, 4, 0.5, 0.5)
        mx, my = mid.nw[0] - size / 2, mid.nw[1] - size / 2
        root, ext = os.path.splitext(render_path)
        render_from((mx - 40, my, gz + 6), (mx + 60, my, gz + 2), 28, root + "_close" + ext)


main()
