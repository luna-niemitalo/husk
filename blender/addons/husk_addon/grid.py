"""The dicing: the world grid as a `Grid` empty, and one box empty per cell.

Cell (i, j) of a world document is the tile placed at
origin + (i * axis_i + j * axis_j) * cell_size (BUNDLE_FORMAT.md "World
documents"); the tile extends from there along axis_i and axis_j by one
cell_size each, which is why a point's cell is a floor division.
"""
import math

import bpy
import numpy as np

GRID_NAME = "Grid"
CELLS_NAME = "Cells"


class Grid:
    def __init__(self, origin, cell_size, axis_i, axis_j, scene_offset):
        self.origin = np.asarray(origin, dtype=np.float64)
        self.cell_size = float(cell_size)
        self.axis_i = np.asarray(axis_i, dtype=np.float64)
        self.axis_j = np.asarray(axis_j, dtype=np.float64)
        self.scene_offset = np.asarray(scene_offset, dtype=np.float64)

    @classmethod
    def of(cls, obj):
        return cls(obj["husk_origin"], obj["husk_cell_size"], obj["husk_axis_i"], obj["husk_axis_j"],
                   obj["husk_scene_offset"])

    def cell_corner(self, i, j):
        """World position of cell (i, j)'s tile frame origin."""
        return self.origin + (i * self.axis_i + j * self.axis_j) * self.cell_size

    def cell_of(self, world):
        d = np.asarray(world, dtype=np.float64) - self.origin
        return (math.floor(d @ self.axis_i / self.cell_size), math.floor(d @ self.axis_j / self.cell_size))

    def cells_over(self, world_min, world_max):
        """Every cell whose footprint meets the XY box [world_min, world_max]."""
        corners = [(x, y, 0.0) for x in (world_min[0], world_max[0]) for y in (world_min[1], world_max[1])]
        ids = [self.cell_of(c) for c in corners]
        i0, i1 = min(c[0] for c in ids), max(c[0] for c in ids)
        j0, j1 = min(c[1] for c in ids), max(c[1] for c in ids)
        return [(i, j) for i in range(i0, i1 + 1) for j in range(j0, j1 + 1)]

    def to_scene(self, world):
        return np.asarray(world, dtype=np.float64) - self.scene_offset

    def to_world(self, scene):
        return np.asarray(scene, dtype=np.float64) + self.scene_offset


def create(collection, origin, cell_size, axis_i, axis_j, scene_offset, source_world_dir):
    obj = bpy.data.objects.new(GRID_NAME, None)
    obj.empty_display_type = "ARROWS"
    obj.empty_display_size = cell_size / 8
    obj["husk_origin"] = list(origin)
    obj["husk_cell_size"] = float(cell_size)
    obj["husk_axis_i"] = list(axis_i)
    obj["husk_axis_j"] = list(axis_j)
    obj["husk_scene_offset"] = list(scene_offset)
    # The dicing the scene was loaded with: export compares against it to
    # know whether source tiles outside the loaded cells still fit the grid.
    obj["husk_source_origin"] = list(origin)
    obj["husk_source_cell_size"] = float(cell_size)
    obj["husk_source_world"] = source_world_dir
    obj.location = tuple(np.asarray(origin) - np.asarray(scene_offset))
    for attr in ("lock_location", "lock_rotation", "lock_scale"):
        setattr(obj, attr, (True, True, True))
    collection.objects.link(obj)
    return obj


def is_source_dicing(grid_obj):
    return (list(grid_obj["husk_origin"]) == list(grid_obj["husk_source_origin"])
            and grid_obj["husk_cell_size"] == grid_obj["husk_source_cell_size"])


def rebuild_cells(grid_obj, cells, z_range):
    """Replace the cell boxes with one per (i, j) in `cells`."""
    root = grid_obj.users_collection[0]
    coll = root.children.get(CELLS_NAME)
    if coll is None:
        coll = bpy.data.collections.new(CELLS_NAME)
        root.children.link(coll)
    for obj in list(coll.objects):
        bpy.data.objects.remove(obj)
    g = Grid.of(grid_obj)
    z0, z1 = z_range
    half = g.cell_size / 2
    for i, j in cells:
        centre = g.to_scene(g.cell_corner(i, j) + (g.axis_i + g.axis_j) * half)
        box = bpy.data.objects.new("cell_%d_%d" % (i, j), None)
        box.empty_display_type = "CUBE"
        box.empty_display_size = 1.0
        box.location = (centre[0], centre[1], (z0 + z1) / 2)
        box.scale = (half, half, max((z1 - z0) / 2, 1.0))
        box["husk_i"] = i
        box["husk_j"] = j
        for attr in ("lock_location", "lock_rotation", "lock_scale"):
            setattr(box, attr, (True, True, True))
        box.hide_render = True
        coll.objects.link(box)
    return coll


def selected_cells(context):
    return [(o["husk_i"], o["husk_j"]) for o in context.selected_objects if "husk_i" in o]
