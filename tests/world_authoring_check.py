# Headless check of the Blender world-authoring addon (blender/addons/husk_addon,
# REFACTOR/BLENDER_WORLD_AUTHORING.md): import cells of a world, export them
# unedited on the source dicing and compare with the source tiles, then
# optionally scale the world, re-dice and export again.
#
#   blender --background --factory-startup --python world_authoring_check.py -- \
#       <world_bundle_dir> <out_dir> [--cells "32_49 32_50"] [--scale 20] [--cell-size 533.3333] [--save x.blend]
#
# Prints "HUSK_PROBE key=value" lines (same convention as bundle_import_check.py).
# Validate the written trees with `husk validate-bundle <out_dir>/...`.
import json
import os
import sys
import time

import bpy
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "blender", "addons"))
from husk_addon import docs, world_io  # noqa: E402
from husk_addon import grid as grid_mod  # noqa: E402
from husk_addon import terrain as tr  # noqa: E402


def probe(key, value):
    print("HUSK_PROBE %s=%s" % (key, value), flush=True)


def arg(argv, name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


def tiles_of(world_dir):
    world = docs.load(world_dir)
    out = {}
    for c in world["grid"]["cells"]:
        asset = world["references"][c["tile"]]["asset"]
        if "uri" in asset:
            out[(c["i"], c["j"])] = os.path.normpath(os.path.join(world_dir, asset["uri"]))
    return world, out


def chunks(path):
    doc = docs.load(path)
    ch = doc["resources"]["terrain"]["chunks"]
    d = os.path.dirname(path)
    return doc, docs.read_slice(d, ch["heights"]), docs.read_slice(d, ch["hole_rows"])


def corner(world, cell):
    g = world["grid"]
    return np.array(g["origin"]) + (cell[0] * np.array(g["axis_i"]) + cell[1] * np.array(g["axis_j"])) * g["cell_size"]


def instances(world, cell, path):
    """{(set name, id): (world translation, rotation, scale)} of one tile."""
    doc = docs.load(path)
    out = {}
    for entry in doc["definition"]["placement_sets"]:
        ref = doc["references"][entry["reference"]]["asset"]
        s = docs.load(os.path.normpath(os.path.join(os.path.dirname(path), ref["uri"])))
        for i in s["instances"]:
            out[(ref["name"], i["id"])] = (corner(world, cell) + i["translation"], np.array(i["rotation"]), i["scale"])
    return out


def compare_round_trip(src_world_dir, out_world_dir, cells):
    _, src = tiles_of(src_world_dir)
    _, out = tiles_of(out_world_dir)
    worst_dz, hole_diff, missing = 0.0, 0, 0
    for cell in cells:
        if cell not in out:
            missing += 1
            continue
        _, sh, sholes = chunks(src[cell])
        _, oh, oholes = chunks(out[cell])
        hole_diff += int(np.count_nonzero(sholes != oholes))
        # Compare only samples a drawn quad uses: hole-surrounded heights are refilled.
        hole = ((sholes[:, :, None] >> np.arange(8)) & 1).reshape(256, 64).astype(bool)
        used = np.zeros((256, 145), bool)
        for q in range(64):
            used[:, np.unique(tr.FAN[q])] |= ~hole[:, q:q + 1]
        worst_dz = max(worst_dz, float(np.abs(sh - oh)[used].max()))
    probe("round_trip_tiles_missing", missing)
    probe("round_trip_max_abs_dz", "%.6f" % worst_dz)
    probe("round_trip_hole_row_bytes_differing", hole_diff)

    # A source tile lists every placement overlapping it; export lists each
    # once, in the cell holding its origin. So: every source placement whose
    # origin is in a loaded cell comes back, same set and id, same transform.
    src_world, _ = tiles_of(src_world_dir)
    out_world, _ = tiles_of(out_world_dir)
    src_inst, out_inst = {}, {}
    for cell in cells:
        src_inst.update(instances(src_world, cell, src[cell]))
        out_inst.update(instances(out_world, cell, out[cell]))
    g = src_world["grid"]
    G = grid_mod.Grid(g["origin"], g["cell_size"], g["axis_i"], g["axis_j"], (0, 0, 0))
    expected = {k for k, v in src_inst.items() if G.cell_of(v[0]) in set(cells)}
    probe("placements_expected_back", len(expected))
    probe("placements_missing", len(expected - set(out_inst)))
    probe("placements_extra", len(set(out_inst) - set(src_inst)))
    worst_t = max((np.abs(src_inst[k][0] - out_inst[k][0]).max() for k in expected & set(out_inst)), default=0.0)
    # q and -q are one rotation.
    worst_r = max((min(np.abs(src_inst[k][1] - out_inst[k][1]).max(), np.abs(src_inst[k][1] + out_inst[k][1]).max())
                   for k in expected & set(out_inst)), default=0.0)
    worst_s = max((abs(src_inst[k][2] - out_inst[k][2]) for k in expected & set(out_inst)), default=0.0)
    probe("placements_max_translation_error", "%.6f" % worst_t)
    probe("placements_max_rotation_error", "%.6f" % worst_r)
    probe("placements_max_scale_error", "%.6f" % worst_s)


def seam_check(world_dir, baked_under):
    """Seams between tiles the export baked (kept source tiles keep their own cracks)."""
    world, tiles = tiles_of(world_dir)
    tiles = {c: p for c, p in tiles.items() if p.startswith(baked_under)}
    worst, pairs, where = 0.0, 0, None
    loaded = {c: chunks(p) for c, p in tiles.items()}
    heights = {c: v[1].reshape(16, 16, 145) for c, v in loaded.items()}
    # (16, 16, 8, 8): quad (r, c) of chunk (row, col) is a hole.
    holes = {c: (((v[2][:, :, None] >> np.arange(8)) & 1).astype(bool)).reshape(16, 16, 8, 8) for c, v in loaded.items()}
    # Only corners a drawn quad on each side uses: heights under holes are
    # refilled per tile and never drawn.
    for (i, j), h in heights.items():
        for (ni, nj), axis in (((i + 1, j), "i"), ((i, j + 1), "j")):
            if (ni, nj) not in heights:
                continue
            pairs += 1
            n, ha, hb = heights[(ni, nj)], holes[(i, j)], holes[(ni, nj)]
            for k in range(16):
                for e in range(9):
                    # Neighbour along axis_i (the tile's column axis): last chunk column's
                    # right corners meet the neighbour's first column's left; along axis_j, rows.
                    if axis == "i":
                        a, b = h[k, 15, e * 17 + 8], n[k, 0, e * 17]
                        qa = [ha[k, 15, r, 7] for r in (e - 1, e) if 0 <= r < 8]
                        qb = [hb[k, 0, r, 0] for r in (e - 1, e) if 0 <= r < 8]
                    else:
                        a, b = h[15, k, 8 * 17 + e], n[0, k, e]
                        qa = [ha[15, k, 7, c] for c in (e - 1, e) if 0 <= c < 8]
                        qb = [hb[0, k, 0, c] for c in (e - 1, e) if 0 <= c < 8]
                    if all(qa) or all(qb):
                        continue
                    if abs(a - b) > worst:
                        worst, where = abs(a - b), ((i, j), (ni, nj), k, e)
    probe("seam_pairs", pairs)
    probe("seam_max_abs_dz", "%.6f" % worst)
    probe("seam_worst_at", where)


def detail_check(ctx, root, out):
    """Free edit: subdivide and ripple the terrain, export, and compare the
    baked heights with direct ray casts of the edited mesh (an independent
    check on the node tree)."""
    import bmesh
    from mathutils import Vector
    from mathutils.bvhtree import BVHTree
    terrain = root.objects[tr.TERRAIN_NAME]
    bm = bmesh.new()
    bm.from_mesh(terrain.data)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=1, use_grid_fill=True)
    bm.to_mesh(terrain.data)
    bm.free()
    co = np.empty(len(terrain.data.vertices) * 3, np.float32)
    terrain.data.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    co[:, 2] += 3.0 * np.sin(co[:, 0] / 7.0) * np.cos(co[:, 1] / 11.0)
    terrain.data.vertices.foreach_set("co", co.ravel())
    terrain.data.update()
    ctx.view_layer.update()
    probe("detail_terrain_vertices", len(terrain.data.vertices))
    t = time.time()
    world = world_io.export_world(ctx, root, out)
    probe("detail_export_seconds", "%.1f" % (time.time() - t))
    G = grid_mod.Grid.of(root.objects[grid_mod.GRID_NAME])
    tree = BVHTree.FromObject(terrain, ctx.evaluated_depsgraph_get())
    _, tiles = tiles_of(world)
    tiles = {c: p for c, p in tiles.items() if p.startswith(out)}
    rng = np.random.default_rng(7)
    worst, n = 0.0, 0
    # Rays start just above the terrain: from far up, float32 hit distances
    # alone would cost this check more precision than it measures.
    top = float(co[:, 2].max()) + 10.0
    for cell in [list(tiles)[k] for k in rng.choice(len(tiles), min(8, len(tiles)), replace=False)]:
        _, heights, _ = chunks(tiles[cell])
        xy = tr.sample_xy(G, cell) - G.scene_offset[:2]
        for ci, s in zip(rng.integers(0, 256, 300), rng.integers(0, 145, 300)):
            hit = tree.ray_cast(Vector((xy[ci, s, 0], xy[ci, s, 1], top)), Vector((0, 0, -1)), 1e5)
            if hit[0] is not None:
                worst = max(worst, abs(hit[0].z - heights[ci, s]))
                n += 1
    probe("detail_samples_checked", n)
    probe("detail_max_abs_dz_vs_raycast", "%.6f" % worst)
    seam_check(world, out)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    world_dir, out_dir = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    cells = world_io.parse_cells(arg(argv, "--cells", "32_49 32_50"))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    ctx = bpy.context

    t = time.time()
    root = world_io.import_world(ctx, world_dir, cells)
    probe("import_seconds", "%.1f" % (time.time() - t))
    terrain = root.objects[tr.TERRAIN_NAME]
    probe("terrain_vertices", len(terrain.data.vertices))
    probe("terrain_faces", len(terrain.data.polygons))
    probe("placement_empties", sum(1 for o in ctx.scene.objects if o.type == "EMPTY" and "husk_set" in o))

    t = time.time()
    rt = world_io.export_world(ctx, root, os.path.join(out_dir, "round_trip"))
    probe("round_trip_export_seconds", "%.1f" % (time.time() - t))
    compare_round_trip(world_dir, rt, cells)
    seam_check(rt, out_dir)

    scale = arg(argv, "--scale")
    if scale:
        grid_obj = root.objects[grid_mod.GRID_NAME]
        t = time.time()
        world_io.scale_world(ctx, root, float(scale), (0.0, 0.0, 0.0))
        if arg(argv, "--cell-size"):
            grid_obj["husk_cell_size"] = float(arg(argv, "--cell-size"))
            world_io.rebuild_cells(root)
        probe("scaled_cells", len(world_io.covered_cells(root, ctx.scene)))
        scaled = world_io.export_world(ctx, root, os.path.join(out_dir, "scaled"))
        probe("scaled_export_seconds", "%.1f" % (time.time() - t))
        _, tiles = tiles_of(scaled)
        probe("scaled_tiles_written", len(tiles))
        seam_check(scaled, out_dir)
        # Props keep their size: every instance scale written equals its source scale.
        scales = []
        for path in tiles.values():
            doc = docs.load(path)
            for entry in doc["definition"]["placement_sets"]:
                ref = doc["references"][entry["reference"]]["asset"]
                s = docs.load(os.path.normpath(os.path.join(os.path.dirname(path), ref["uri"])))
                scales += [i["scale"] for i in s["instances"]]
        probe("scaled_instance_scale_min_max", "%.4f/%.4f" % (min(scales), max(scales)) if scales else "none")

    if "--detail" in argv:
        detail_check(ctx, root, os.path.join(out_dir, "detail"))

    save = arg(argv, "--save")
    if save:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(save))


main()
