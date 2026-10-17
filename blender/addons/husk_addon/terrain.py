"""Terrain tiles -> one merged, freely editable mesh.

A tile is 16x16 chunks of 145 heights each (BUNDLE_FORMAT.md "Frame and
heightfield"). The layout below is the one both import and the bake use,
so a tile read in and baked back on its own dicing lands on the same points.
"""
import json

import bpy
import numpy as np

from . import docs

CHUNKS = 16
QUADS = 8
SAMPLES = 145
TERRAIN_NAME = "Terrain"
AREA_ATTRIBUTE = "husk_area"


def _layout():
    """(145, 2): each sample's (row, column) within its chunk, in quads."""
    rc = []
    for v in range(SAMPLES):
        pair, k = divmod(v, 17)
        rc.append((pair, k) if k < 9 else (pair + 0.5, k - 9 + 0.5))
    return np.array(rc, dtype=np.float64)


def _fan():
    """(64, 4, 3): the four triangles of each quad (r * 8 + c), as sample indices."""
    tris = []
    for r in range(QUADS):
        for c in range(QUADS):
            m = r * 17 + 9 + c
            tl, tr, bl, br = r * 17 + c, r * 17 + c + 1, (r + 1) * 17 + c, (r + 1) * 17 + c + 1
            tris.append([(m, tr, tl), (m, br, tr), (m, bl, br), (m, tl, bl)])
    return np.array(tris, dtype=np.int64)


LAYOUT = _layout()
FAN = _fan()
CENTRES = np.array([r * 17 + 9 + c for r in range(QUADS) for c in range(QUADS)])


CHUNK_RC = np.array([(r, c) for r in range(CHUNKS) for c in range(CHUNKS)], dtype=np.float64) * QUADS
PER_SIDE = CHUNKS * QUADS


def lattice_xy(grid, doubled_row, doubled_column):
    """World XY of a point on the dicing's lattice, from its doubled
    lattice coordinates (corners even, centres odd) counted from the grid
    origin. Every sample position comes from here, so a border point two
    cells share is the same float in both."""
    quad = grid.cell_size / PER_SIDE
    return (grid.origin[:2] + (doubled_row[..., None] * (quad / 2)) * grid.axis_j[:2]
            + (doubled_column[..., None] * (quad / 2)) * grid.axis_i[:2])


def sample_xy(grid, cell):
    """(256, 145, 2) world XY of every sample of cell (i, j)."""
    i, j = cell
    rc = CHUNK_RC[:, None, :] + LAYOUT[None, :, :]
    return lattice_xy(grid, 2 * (j * PER_SIDE + rc[..., 0]), 2 * (i * PER_SIDE + rc[..., 1]))


def _check_tile(path, t, grid):
    # IN, critical: the layout above and the weld below assume exactly the
    # tile shape the schema describes, on the world grid's own axes.
    expected = {"chunks_per_side": CHUNKS, "quads_per_side": QUADS,
                "chunk_vertex_layout": "interleaved_9x9_corners_8x8_centres", "chunk_topology": "quad_centre_fan"}
    for key, want in expected.items():
        if t[key] != want:
            raise docs.FormatError("%s: terrain %s is %r, expected %r" % (path, key, t[key], want))
    for key, want in (("row_axis", grid.axis_j), ("column_axis", grid.axis_i)):
        if not np.allclose(t[key], want):
            raise docs.FormatError("%s: terrain %s is %r, expected the grid's %r" % (path, key, t[key], list(want)))
    if not np.isclose(t["tile_size"], grid.cell_size, rtol=1e-6):
        raise docs.FormatError("%s: tile_size %r, expected the grid's cell_size %r" % (path, t["tile_size"], grid.cell_size))


def build(tiles, grid, collection):
    """`tiles`: [(path, doc, tile_dir, corner_world)]. Returns the Terrain object.

    Border samples every tile and chunk stores its own copy of are welded
    into one vertex, keeping the first-loaded copy's height (BUNDLE_FORMAT.md
    "Seams": weld to one side rather than rely on them matching)."""
    areas, area_ids = [], {}
    xyz, keys, faces, face_area = [], [], [], []
    base = 0
    for path, doc, tile_dir, corner in tiles:
        t = doc["resources"]["terrain"]
        _check_tile(path, t, grid)
        ch = t["chunks"]
        quad = t["quad_size"]
        heights = docs.read_slice(tile_dir, ch["heights"]).astype(np.float64)
        holes = docs.read_slice(tile_dir, ch["hole_rows"])
        area_index = docs.read_slice(tile_dir, ch["area_index"])
        origins = docs.read_slice(tile_dir, ch["origin"]).astype(np.float64)
        xy = corner[:2] + origins[:, None, :] + (LAYOUT[None, :, 0:1] * grid.axis_j[:2]
                                                 + LAYOUT[None, :, 1:2] * grid.axis_i[:2]) * quad
        xyz.append(np.concatenate([xy, heights[..., None]], axis=2).reshape(-1, 3))
        d = xy - grid.origin[:2]
        # Doubled lattice coordinates: corners land on even, centres on odd.
        keys.append(np.stack([np.rint(2 * (d @ grid.axis_j[:2]) / quad),
                              np.rint(2 * (d @ grid.axis_i[:2]) / quad)], axis=2).reshape(-1, 2).astype(np.int64))
        tile_area = []
        for ref in ch["areas"]:
            k = json.dumps(ref, sort_keys=True)
            if k not in area_ids:
                area_ids[k] = len(areas)
                areas.append(ref)
            tile_area.append(area_ids[k])
        hole = (holes[:, :, None] >> np.arange(QUADS)[None, None, :]) & 1  # (256, row, column)
        for ci in range(CHUNKS * CHUNKS):
            open_quads = np.flatnonzero(hole[ci].ravel() == 0)
            f = FAN[open_quads].reshape(-1, 3) + base + ci * SAMPLES
            faces.append(f)
            face_area.append(np.full(len(f), tile_area[area_index[ci]] if tile_area else -1, dtype=np.int32))
        base += CHUNKS * CHUNKS * SAMPLES
    xyz, keys = np.concatenate(xyz), np.concatenate(keys)
    faces, face_area = np.concatenate(faces), np.concatenate(face_area)

    packed = (keys[:, 0] << 32) + (keys[:, 1] & 0xFFFFFFFF)
    _, first, welded = np.unique(packed, return_index=True, return_inverse=True)
    faces = welded[faces]
    used, faces = np.unique(faces, return_inverse=True)
    faces = faces.reshape(-1, 3)
    # Onto the exact lattice the bake samples: source chunk origins drift
    # from it by up to ~0.01 yd (stored MCNK positions), which on a steep
    # slope would move a round-tripped height by ~0.1 yd.
    # The bake's lattice, not the file's float32 quad_size: the two differ
    # by ~1e-7 yd per quad, 0.0006 yd across a map, enough to put the
    # loaded region's edge just past the samples on it.
    k = keys[first[used]].astype(np.float64)
    verts = xyz[first[used]].copy()
    verts[:, :2] = lattice_xy(grid, k[:, 0], k[:, 1])
    verts = grid.to_scene(verts)

    mesh = bpy.data.meshes.new(TERRAIN_NAME)
    mesh.vertices.add(len(verts))
    mesh.vertices.foreach_set("co", verts.astype(np.float32).ravel())
    mesh.loops.add(faces.size)
    mesh.loops.foreach_set("vertex_index", faces.astype(np.int32).ravel())
    mesh.polygons.add(len(faces))
    mesh.polygons.foreach_set("loop_start", np.arange(0, faces.size, 3, dtype=np.int32))
    mesh.update(calc_edges=True)
    attr = mesh.attributes.new(AREA_ATTRIBUTE, "INT", "FACE")
    attr.data.foreach_set("value", face_area)
    mesh.shade_smooth()
    obj = bpy.data.objects.new(TERRAIN_NAME, mesh)
    obj["husk_areas"] = json.dumps(areas)
    collection.objects.link(obj)
    return obj
