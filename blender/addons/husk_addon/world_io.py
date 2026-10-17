"""Import a world into the scene; bake and export the scene as a world.

Export writes a new tree (never the source's): <out>/maps/<map>/ with the
world document, one tile bundle per baked cell, and every asset, effect
and unchanged source tile referenced back into the source tree by
relative uri.
"""
import json
import os

import bpy
import numpy as np
from mathutils import Matrix, Vector

from . import assets as assets_mod
from . import bake, docs, grid as grid_mod, placements as pl, terrain as tr, zones
from .grid import Grid

ROOT_PREFIX = "husk_world:"
# The tile placement sets, in reference order. Two source sets because
# their ids are separate number spaces; `authored` is husk's own space.
TILE_SETS = ("doodads", "map_objects", pl.AUTHORED)

# What a baked tile does not carry (BLENDER_WORLD_AUTHORING.md "Ownership").
_NOT_BAKED = [
    ("texture layers", "ground textures, layers and alpha maps"),
    ("ground effects", "ground-cover rules"),
    ("liquids", "liquid surfaces and types"),
    ("lod", "the surface's coarser mesh levels"),
]


def _cell_refs(world):
    return {(c["i"], c["j"]): (c, world["references"][c["tile"]]["asset"]) for c in world["grid"]["cells"]}


CELLS_HELP = "cells: '32_49 32_50', or a rectangle '31:33,48:50'"


def parse_cells(text):
    """'32_49 32_50' or '31:33,48:50' (inclusive ranges) -> [(i, j)], in order, once each."""
    cells = []
    if not text.split():
        raise ValueError("no cells given; " + CELLS_HELP)
    for token in text.replace(";", " ").split():
        if ":" in token:
            ir, jr = token.split(",")
            i0, i1 = (int(v) for v in ir.split(":"))
            j0, j1 = (int(v) for v in jr.split(":"))
            cells += [(i, j) for i in range(i0, i1 + 1) for j in range(j0, j1 + 1)]
        else:
            i, j = token.split("_")
            cells.append((int(i), int(j)))
    return list(dict.fromkeys(cells))


def import_world(context, world_dir, wanted):
    world_dir = os.path.abspath(world_dir)
    world = docs.load(world_dir)
    g = world["grid"]
    by_cell = _cell_refs(world)
    asked = len(wanted)
    # A rectangle or radius usually covers holes (ocean, unexported tiles).
    wanted = [c for c in wanted if c in by_cell and "uri" in by_cell[c][1]]
    if not wanted:
        i_range = (min(c[0] for c in by_cell), max(c[0] for c in by_cell))
        j_range = (min(c[1] for c in by_cell), max(c[1] for c in by_cell))
        raise ValueError("none of the %d cells asked for has an exported tile; this world's cells span i %d..%d, j %d..%d"
                         % (asked, *i_range, *j_range))
    probe = Grid(g["origin"], g["cell_size"], g["axis_i"], g["axis_j"], (0, 0, 0))
    offset = probe.cell_corner(*wanted[0]) * np.array([1.0, 1.0, 0.0])

    root = bpy.data.collections.new(ROOT_PREFIX + world["subject"].get("name", "world"))
    context.scene.collection.children.link(root)
    root["husk_world_path"] = world_dir
    root["husk_loaded_cells"] = [list(c) for c in wanted]
    grid_obj = grid_mod.create(root, g["origin"], g["cell_size"], g["axis_i"], g["axis_j"], offset, world_dir)
    G = Grid.of(grid_obj)

    tiles = []
    for cell in wanted:
        path = os.path.normpath(os.path.join(world_dir, by_cell[cell][1]["uri"]))
        tiles.append((path, docs.load(path), os.path.dirname(path), G.cell_corner(*cell)))
    terrain_obj = tr.build(tiles, G, root)

    assets = assets_mod.Assets(root)
    place_root = bpy.data.collections.new(pl.PLACEMENTS_NAME)
    root.children.link(place_root)
    seen = set()
    for path, doc, tile_dir, corner in tiles:
        for entry in doc["definition"]["placement_sets"]:
            ref = doc["references"][entry["reference"]]["asset"]
            pl.import_set(os.path.normpath(os.path.join(tile_dir, ref["uri"])), ref["name"], corner, G, place_root,
                          assets, seen, world_level=False)
    for index in world["placements"]:
        ref = world["references"][index]["asset"]
        pl.import_set(os.path.normpath(os.path.join(world_dir, ref["uri"])), ref["name"], np.zeros(3), G,
                      place_root, assets, seen, world_level=True)
    assets.exclude_from_view(context.view_layer)
    rebuild_cells(root)
    return root


def find_root(context):
    for coll in context.scene.collection.children_recursive:
        if coll.name.startswith(ROOT_PREFIX):
            return coll
    raise ValueError("no imported husk world in this scene")


def _members(root):
    return root.objects[grid_mod.GRID_NAME], root.objects[tr.TERRAIN_NAME]


def _terrain_box(terrain_obj, G):
    corners = np.array([list(terrain_obj.matrix_world @ Vector(c)) for c in terrain_obj.bound_box])
    return G.to_world(corners.min(axis=0)), G.to_world(corners.max(axis=0))


def covered_cells(root, scene):
    grid_obj, terrain_obj = _members(root)
    G = Grid.of(grid_obj)
    lo, hi = _terrain_box(terrain_obj, G)
    cells = set(G.cells_over(lo, hi))
    cells |= {G.cell_of(p.world_t) for p in pl.collect(scene, G) if not p.world_level}
    return sorted(cells)


def rebuild_cells(root):
    grid_obj, terrain_obj = _members(root)
    G = Grid.of(grid_obj)
    lo, hi = _terrain_box(terrain_obj, G)
    grid_mod.rebuild_cells(grid_obj, covered_cells(root, bpy.context.scene), (lo[2], hi[2]))


def scale_world(context, root, factor, pivot_scene):
    """Every distance from `pivot_scene` times `factor`: the terrain
    stretches, placements move apart, and each prop keeps its own size."""
    grid_obj, terrain_obj = _members(root)
    pivot = Vector(pivot_scene)
    about = Matrix.Translation(pivot) @ Matrix.Scale(factor, 4) @ Matrix.Translation(-pivot)
    mw = terrain_obj.matrix_world
    terrain_obj.data.transform(mw.inverted() @ about @ mw)
    terrain_obj.data.update()
    for obj in context.scene.objects:
        if obj.type == "EMPTY" and pl.is_placement(obj) and obj.parent is None:
            obj.location = pivot + (obj.location - pivot) * factor
    # Unloaded source tiles no longer fit a scaled world (export_world).
    root["husk_scale"] = root.get("husk_scale", 1.0) * factor
    context.view_layer.update()
    rebuild_cells(root)


def _taken_authored_ids(src_world, src_dir, kept_cells):
    """Authored ids already used by source tiles this export keeps as they are."""
    taken = set()
    for cell, (_, asset) in _cell_refs(src_world).items():
        if cell not in kept_cells or "uri" not in asset:
            continue
        path = os.path.normpath(os.path.join(src_dir, asset["uri"]))
        doc = docs.load(path)
        for entry in doc["definition"]["placement_sets"]:
            ref = doc["references"][entry["reference"]]["asset"]
            if ref["name"] == pl.AUTHORED:
                set_doc = docs.load(os.path.normpath(os.path.join(os.path.dirname(path), ref["uri"])))
                taken |= {i["id"] for i in set_doc["instances"]}
    return taken


def _tile_document(name_ref, cell, result, areas_global, chunk_area, G, sets, out_dir, schema_version, source):
    quad = G.cell_size / (tr.CHUNKS * tr.QUADS)
    bin_path = os.path.join(out_dir, "terrain.bin")
    os.makedirs(out_dir, exist_ok=True)
    w = docs.BinWriter(bin_path)
    origin = (tr.CHUNK_RC[:, 0:1] * G.axis_j[:2] + tr.CHUNK_RC[:, 1:2] * G.axis_i[:2]) * quad
    used = sorted({int(a) for a in chunk_area})
    local = {a: k for k, a in enumerate(used)}
    areas = [areas_global[a] if a >= 0 else {"id": {"kind": "none"}} for a in used]
    chunks = {
        "count": 256,
        "origin": w.add(origin.astype("<f4"), 2),
        "heights": w.add(result.z.astype("<f4"), tr.SAMPLES),
        "normals": w.add(result.normals.reshape(-1, 3).astype("<f4"), 3, "NORMAL"),
        "hole_rows": w.add(result.holes.astype("u1"), 8),
        "dominant_layer": w.add(np.zeros((256, 64), "u1"), 64),
        "ground_effect_suppressed_rows": w.add(np.zeros((256, 8), "u1"), 8),
        "liquid_fishable_rows": w.add(np.zeros((256, 8), "u1"), 8),
        "liquid_deep_rows": w.add(np.zeros((256, 8), "u1"), 8),
        "area_index": w.add(np.array([local[int(a)] for a in chunk_area], "<u4"), 1),
        "areas": areas,
        "layers": [[] for _ in range(256)],
    }
    w.close()
    references = [{"kind": "placement_set", "asset": {"id": {"kind": "none"}, "name": name, "name_source": "synthesized",
                                                      "uri": "sets/%d.json" % k}} for k, (name, _) in enumerate(sets)]
    prov = docs.provenance(source)
    prov["sources"] += [{"name": n, "role": r, "available": False, "detail": "not baked by the Blender addon"}
                        for n, r in _NOT_BAKED]
    doc = {
        "format": "husk-bundle", "kind": "model", "schema_version": schema_version,
        "subject": name_ref, "space": docs.SPACE, "provenance": prov,
        "definition": {"placement_sets": [{"reference": k, "always_on": True, "instance_count": n}
                                          for k, (_, n) in enumerate(sets)],
                       "terrain": {"edge_vertices": "duplicated"}},
        "selection": {"placement_sets": []},
        "references": references,
        "resources": {"terrain": {
            "chunks_per_side": tr.CHUNKS, "quads_per_side": tr.QUADS, "tile_size": G.cell_size,
            "chunk_size": G.cell_size / tr.CHUNKS, "quad_size": quad,
            "row_axis": list(G.axis_j), "column_axis": list(G.axis_i),
            "chunk_vertex_layout": "interleaved_9x9_corners_8x8_centres", "chunk_topology": "quad_centre_fan",
            "textures": [], "ground_effects": [], "chunks": chunks,
            "lod": {"max_level": 0, "levels": [{"surface": "heightfield"}]}, "liquids": [],
        }},
    }
    docs.write(os.path.join(out_dir, "manifest.json"), doc)


def _bounds(G, cell, result):
    corner = G.cell_corner(*cell)
    xy = np.array([corner + (a * G.axis_i + b * G.axis_j) * G.cell_size for a in (0, 1) for b in (0, 1)])
    # Missed samples hold copies of hit heights, so every stored height is in range.
    zs = result.z.ravel()
    return ([float(xy[:, 0].min()), float(xy[:, 1].min()), float(zs.min())],
            [float(xy[:, 0].max()), float(xy[:, 1].max()), float(zs.max())])


def keeps_source_tiles(root):
    """Whether export keeps the source tiles that weren't loaded: only while
    they still fit, on the source dicing of an unscaled world."""
    grid_obj, _ = _members(root)
    return grid_mod.is_source_dicing(grid_obj) and root.get("husk_scale", 1.0) == 1.0


def export_world(context, root, out_root, keep_source_tiles=None, log=print):
    """`keep_source_tiles`: None decides by keeps_source_tiles(root)."""
    grid_obj, terrain_obj = _members(root)
    context.view_layer.update()
    G = Grid.of(grid_obj)
    src_dir = root["husk_world_path"]
    src_world = docs.load(src_dir)
    schema_version = docs.SCHEMA_VERSION
    map_name = src_world["subject"].get("name", "world")
    out_maps = os.path.join(os.path.abspath(out_root), "maps", map_name)
    world_dir = os.path.join(out_maps, map_name + ".world.bundle")
    if os.path.realpath(world_dir) == os.path.realpath(src_dir):
        raise ValueError("export would overwrite the source world %s" % src_dir)

    placements = pl.collect(context.scene, G)
    tile_pl = [p for p in placements if not p.world_level]
    by_cell = {}
    for p in tile_pl:
        by_cell.setdefault(G.cell_of(p.world_t), []).append(p)
    lo, hi = _terrain_box(terrain_obj, G)
    covered = set(G.cells_over(lo, hi))

    source_cells = _cell_refs(src_world)
    kept = set()
    if keep_source_tiles is None:
        keep_source_tiles = keeps_source_tiles(root)
    if keep_source_tiles:
        loaded = {tuple(c) for c in root["husk_loaded_cells"]}
        kept = set(source_cells) - loaded
        # A loaded tile lists every placement overlapping it, so some stand
        # in a neighbour that is kept as it is and lists them itself.
        moved = []
        for cell in [c for c in by_cell if c in kept]:
            moved += [p.obj.name for p in by_cell.pop(cell) if not p.unmoved]
        if moved:
            raise ValueError("placements moved into source cells that weren't loaded and are kept as they are: %s"
                             % ", ".join(sorted(moved)[:20]))
        covered -= kept
    cells = sorted(covered | set(by_cell))

    taken = {}
    if any(p.id is None and p.set_name == pl.AUTHORED for p in placements):
        taken[pl.AUTHORED] = _taken_authored_ids(src_world, src_dir, kept)
    pl.assign_ids(placements, taken)

    areas_global = json.loads(terrain_obj.get("husk_areas", "[]"))
    probe = bake.TerrainProbe(terrain_obj)
    refs, grid_cells = [], []
    baked_areas = {}
    written = 0

    def progress(done, total):
        log("husk bake: %d/%d cells" % (done, total))

    for cell, result in bake.bake(context, grid_obj, terrain_obj, cells, probe, progress):
        here = by_cell.get(cell, [])
        if result.empty and not here:
            continue
        i, j = cell
        name = "%s_%d_%d" % (map_name, i, j)
        tile_dir = os.path.join(out_maps, name + ".bundle")
        corner = G.cell_corner(i, j)
        sets = []
        for set_name in TILE_SETS:
            members = [p for p in here if p.set_name == set_name]
            if not members:
                continue
            path = os.path.join(tile_dir, "sets", "%d.json" % len(sets))
            docs.write(path, pl.set_document(set_name, members, corner, os.path.dirname(path), schema_version, src_dir))
            sets.append((set_name, len(members)))
        # A baked cell is still its source tile only while the world is
        # unscaled on the source dicing; otherwise it is new content.
        source_ref = source_cells.get(cell)
        subject = ({k: v for k, v in source_ref[1].items() if k != "uri"}
                   if source_ref and keeps_source_tiles(root)
                   else {"id": {"kind": "none"}, "name": name, "name_source": "synthesized"})
        chunk_area = probe.chunk_areas(G, cell, result)
        _tile_document(subject, cell, result, areas_global, chunk_area, G, sets, tile_dir, schema_version, src_dir)
        counts = {}
        for a in chunk_area[chunk_area >= 0]:
            key = zones.area_key(areas_global[int(a)])
            counts[key] = counts.get(key, 0) + 1
        baked_areas[cell] = counts
        bmin, bmax = _bounds(G, cell, result)
        refs.append({"kind": "model", "asset": dict(subject, uri=os.path.join(tile_dir, "manifest.json"))})
        grid_cells.append({"i": i, "j": j, "tile": len(refs) - 1, "bounds_min": bmin, "bounds_max": bmax})
        written += 1

    for cell in sorted(kept):
        source_cell, asset = source_cells[cell]
        refs.append({"kind": "model", "asset": docs.absolute_uris(asset, src_dir)})
        grid_cells.append(dict(source_cell, tile=len(refs) - 1))

    world_places = []
    for index in src_world["placements"]:
        src_ref = src_world["references"][index]["asset"]
        set_name = src_ref["name"]
        src_set_path = os.path.normpath(os.path.join(src_dir, src_ref["uri"]))
        src_set = docs.load(src_set_path)
        resources = docs.absolute_uris(src_set["resources"], os.path.dirname(src_set_path)) if "resources" in src_set else None
        members = [p for p in placements if p.world_level and p.set_name == set_name]
        path = os.path.join(world_dir, "sets", "%d.json" % len(world_places))
        docs.write(path, pl.set_document(set_name, members, np.zeros(3), os.path.dirname(path), schema_version, src_dir,
                                         resources))
        refs.append({"kind": "placement_set", "asset": {"id": {"kind": "none"}, "name": set_name,
                                                        "name_source": "synthesized", "uri": path}})
        world_places.append(len(refs) - 1)

    world = {
        "format": "husk-bundle", "kind": "world", "schema_version": schema_version,
        "subject": src_world["subject"], "space": docs.SPACE, "provenance": docs.provenance(src_dir),
        "definition": {}, "selection": {}, "references": refs,
        "grid": {"origin": [float(v) for v in G.origin], "cell_size": G.cell_size,
                 "axis_i": [float(v) for v in G.axis_i], "axis_j": [float(v) for v in G.axis_j],
                 "cells": grid_cells},
        "placements": world_places,
        "zones": zones.rebuild(src_world, kept, baked_areas),
    }
    docs.write(os.path.join(world_dir, "manifest.json"), docs.relative_uris(world, world_dir))
    log("husk export: %d tiles baked, %d source tiles kept, %d placements -> %s"
        % (written, len(kept), len(placements), world_dir))
    return world_dir
