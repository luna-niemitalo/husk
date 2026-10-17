"""The bake: terrain mesh + dicing -> per-cell heightfields.

Sample positions come from terrain.sample_xy; one Geometry Nodes tree
(`husk_bake`) drops each sample straight down onto the Terrain object.
The preview object and export run the same tree, so the preview is what
export writes (BLENDER_WORLD_AUTHORING.md "The bake").
"""
import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

from . import terrain as tr
from .grid import Grid

TREE_NAME = "husk_bake"
PREVIEW_NAME = "BakePreview"
HIT = "husk_hit"
NORMAL = "husk_normal"
PER_CELL = tr.CHUNKS * tr.CHUNKS * tr.SAMPLES
# Cells evaluated per depsgraph pass on export: large enough that the
# per-pass overhead vanishes, small enough to keep the sample mesh ~2M points.
BATCH_CELLS = 48


def _tree():
    ng = bpy.data.node_groups.get(TREE_NAME)
    if ng is not None:
        return ng
    ng = bpy.data.node_groups.new(TREE_NAME, "GeometryNodeTree")
    io = ng.interface
    io.new_socket("Samples", in_out="INPUT", socket_type="NodeSocketGeometry")
    io.new_socket("Terrain", in_out="INPUT", socket_type="NodeSocketObject")
    io.new_socket("Ray Length", in_out="INPUT", socket_type="NodeSocketFloat")
    io.new_socket("Display", in_out="INPUT", socket_type="NodeSocketBool")
    io.new_socket("Geometry", in_out="OUTPUT", socket_type="NodeSocketGeometry")
    n, link = ng.nodes, ng.links.new
    gin, gout = n.new("NodeGroupInput"), n.new("NodeGroupOutput")

    info = n.new("GeometryNodeObjectInfo")
    info.transform_space = "RELATIVE"
    link(gin.outputs["Terrain"], info.inputs["Object"])

    ray = n.new("GeometryNodeRaycast")
    ray.data_type = "FLOAT_VECTOR"
    ray.inputs["Interpolation"].default_value = "Interpolated"
    ray.inputs["Ray Direction"].default_value = (0.0, 0.0, -1.0)
    link(info.outputs["Geometry"], ray.inputs["Target Geometry"])
    link(n.new("GeometryNodeInputNormal").outputs["Normal"], ray.inputs["Attribute"])
    link(n.new("GeometryNodeInputPosition").outputs["Position"], ray.inputs["Source Position"])
    link(gin.outputs["Ray Length"], ray.inputs["Ray Length"])

    # The ray outputs are fields, evaluated wherever they are used: every use
    # must come before Set Position, or the rays restart from the surface.
    hit = n.new("GeometryNodeStoreNamedAttribute")
    hit.data_type, hit.domain = "BOOLEAN", "POINT"
    hit.inputs["Name"].default_value = HIT
    link(gin.outputs["Samples"], hit.inputs["Geometry"])
    link(ray.outputs["Is Hit"], hit.inputs["Value"])

    normal = n.new("GeometryNodeStoreNamedAttribute")
    normal.data_type, normal.domain = "FLOAT_VECTOR", "POINT"
    normal.inputs["Name"].default_value = NORMAL
    link(hit.outputs["Geometry"], normal.inputs["Geometry"])
    link(ray.outputs["Attribute"], normal.inputs["Value"])

    move = n.new("GeometryNodeSetPosition")
    link(normal.outputs["Geometry"], move.inputs["Geometry"])
    link(ray.outputs["Is Hit"], move.inputs["Selection"])
    link(ray.outputs["Hit Position"], move.inputs["Position"])

    # Display only: drop missed samples so holes show. Export reads every
    # sample, so it runs with Display off.
    stored_hit = n.new("GeometryNodeInputNamedAttribute")
    stored_hit.data_type = "BOOLEAN"
    stored_hit.inputs["Name"].default_value = HIT
    missed = n.new("FunctionNodeBooleanMath")
    missed.operation = "NOT"
    link(stored_hit.outputs["Attribute"], missed.inputs[0])
    drop = n.new("GeometryNodeDeleteGeometry")
    drop.domain = "POINT"
    link(move.outputs["Geometry"], drop.inputs["Geometry"])
    link(missed.outputs["Boolean"], drop.inputs["Selection"])
    switch = n.new("GeometryNodeSwitch")
    switch.input_type = "GEOMETRY"
    link(gin.outputs["Display"], switch.inputs["Switch"])
    link(move.outputs["Geometry"], switch.inputs["False"])
    link(drop.outputs["Geometry"], switch.inputs["True"])
    link(switch.outputs["Output"], gout.inputs["Geometry"])
    return ng


def _z_span(terrain_obj):
    corners = [terrain_obj.matrix_world @ Vector(c) for c in terrain_obj.bound_box]
    zs = [c.z for c in corners]
    return min(zs) - 1.0, max(zs) + 1.0


def _sample_mesh(grid, cells, z_top, with_faces):
    xy = np.concatenate([tr.sample_xy(grid, cell).reshape(-1, 2) for cell in cells]) - grid.scene_offset[:2]
    verts = np.column_stack([xy, np.full(len(xy), z_top)]).astype(np.float32)
    mesh = bpy.data.meshes.new(PREVIEW_NAME)
    mesh.vertices.add(len(verts))
    mesh.vertices.foreach_set("co", verts.ravel())
    if with_faces:
        per_chunk = tr.FAN.reshape(-1, 3)
        chunk_base = np.arange(len(cells) * tr.CHUNKS * tr.CHUNKS) * tr.SAMPLES
        faces = (chunk_base[:, None, None] + per_chunk[None]).reshape(-1, 3).astype(np.int32)
        mesh.loops.add(faces.size)
        mesh.loops.foreach_set("vertex_index", faces.ravel())
        mesh.polygons.add(len(faces))
        mesh.polygons.foreach_set("loop_start", np.arange(0, faces.size, 3, dtype=np.int32))
    mesh.update(calc_edges=with_faces)
    return mesh


def _bake_object(collection, terrain_obj, ray_length, display):
    obj = bpy.data.objects.get(PREVIEW_NAME)
    if obj is None:
        obj = bpy.data.objects.new(PREVIEW_NAME, bpy.data.meshes.new(PREVIEW_NAME))
        collection.objects.link(obj)
        obj.modifiers.new("husk_bake", "NODES").node_group = _tree()
        obj.hide_render = True
    mod = obj.modifiers["husk_bake"]
    sockets = {s.name: s.identifier for s in mod.node_group.interface.items_tree if s.item_type == "SOCKET"}
    mod[sockets["Terrain"]] = terrain_obj
    mod[sockets["Ray Length"]] = ray_length
    mod[sockets["Display"]] = display
    return obj


def _swap_mesh(obj, mesh):
    old = obj.data
    obj.data = mesh
    if old.users == 0:
        bpy.data.meshes.remove(old)


def preview(context, grid_obj, terrain_obj, cells):
    z0, z1 = _z_span(terrain_obj)
    grid = Grid.of(grid_obj)
    obj = _bake_object(grid_obj.users_collection[0], terrain_obj, z1 - z0, True)
    _swap_mesh(obj, _sample_mesh(grid, cells, z1, True))
    obj["husk_preview_cells"] = [list(c) for c in cells]
    obj.update_tag()
    return obj


class CellBake:
    """One cell's baked heightfield, already filled and hole-masked."""

    def __init__(self, z, normals, holes, hits):
        self.z = z              # (256, 145) world heights
        self.normals = normals  # (256, 145, 3) unit
        self.holes = holes      # (256, 8) u8, byte r bit c = quad (r, c) is a hole
        self.hits = hits        # number of samples that met the mesh

    @property
    def empty(self):
        return self.hits == 0


_LAYOUT_DIST = np.linalg.norm(tr.LAYOUT[:, None, :] - tr.LAYOUT[None, :, :], axis=2)


def _finish(z, n, hit):
    """Holes from centre misses; misses elsewhere take their nearest hit
    sample's height, so the stored grid stays complete."""
    z, n, hit = z.reshape(256, tr.SAMPLES), n.reshape(256, tr.SAMPLES, 3), hit.reshape(256, tr.SAMPLES)
    hole = ~hit[:, tr.CENTRES].reshape(256, tr.QUADS, tr.QUADS)
    holes = (hole.astype(np.uint8) << np.arange(tr.QUADS, dtype=np.uint8)[None, None, :]).sum(axis=2).astype(np.uint8)
    hits = int(hit.sum())
    if 0 < hits < hit.size:
        z = z.copy()
        fallback = z[hit].mean()
        for ci in np.flatnonzero(~hit.all(axis=1)):
            have = hit[ci]
            if not have.any():
                # Every quad of this chunk is a hole; its heights are never drawn.
                z[ci] = fallback
                continue
            miss = np.flatnonzero(~have)
            nearest = np.flatnonzero(have)[_LAYOUT_DIST[np.ix_(miss, np.flatnonzero(have))].argmin(axis=1)]
            z[ci, miss] = z[ci, nearest]
    lengths = np.linalg.norm(n, axis=2, keepdims=True)
    n = np.where(hit[..., None] & (lengths > 0), n / np.maximum(lengths, 1e-12), np.array([0.0, 0.0, 1.0]))
    return CellBake(z, n, holes, hits)


def bake(context, grid_obj, terrain_obj, cells, probe, progress=None):
    """Yields ((i, j), CellBake) for every cell, in order.

    The node tree's raycast is a coin flip for a ray landing exactly on the
    mesh's outer boundary (every sample on the edge of the loaded region
    does); `probe` re-casts each of its misses, which are few: boundaries
    and real holes."""
    grid = Grid.of(grid_obj)
    z0, z1 = _z_span(terrain_obj)
    obj = _bake_object(grid_obj.users_collection[0], terrain_obj, z1 - z0, False)
    for start in range(0, len(cells), BATCH_CELLS):
        batch = cells[start:start + BATCH_CELLS]
        _swap_mesh(obj, _sample_mesh(grid, batch, z1, False))
        obj.update_tag()
        depsgraph = context.evaluated_depsgraph_get()
        depsgraph.update()
        m = obj.evaluated_get(depsgraph).data
        count = len(batch) * PER_CELL
        # IN, critical: the tree must hand back one point per sample, in order.
        if len(m.vertices) != count:
            raise RuntimeError("bake returned %d samples, expected %d" % (len(m.vertices), count))
        co = np.empty(count * 3, dtype=np.float32)
        m.vertices.foreach_get("co", co)
        nrm = np.empty(count * 3, dtype=np.float32)
        m.attributes[NORMAL].data.foreach_get("vector", nrm)
        hit = np.empty(count, dtype=bool)
        m.attributes[HIT].data.foreach_get("value", hit)
        co, nrm = co.reshape(-1, 3), nrm.reshape(-1, 3)
        nudge = 1e-3 * grid.cell_size / (tr.CHUNKS * tr.QUADS)
        for s in np.flatnonzero(~hit):
            found = probe.cast(co[s, 0], co[s, 1], z1, z1 - z0, nudge)
            if found is not None:
                co[s, 2], nrm[s] = found[0], found[1]
                hit[s] = True
        co, nrm = co.reshape(len(batch), PER_CELL, 3), nrm.reshape(len(batch), PER_CELL, 3)
        hit = hit.reshape(len(batch), PER_CELL)
        for k, cell in enumerate(batch):
            yield cell, _finish(co[k, :, 2].astype(np.float64), nrm[k].astype(np.float64), hit[k])
        if progress:
            progress(start + len(batch), len(cells))


class TerrainProbe:
    """Single rays against the Terrain from Python: re-casting the node
    tree's misses, and reading which source area (`husk_area`, per face)
    lies under each chunk's middle. Built once per export: the BVH is the
    expensive part."""

    MIDDLE = 4 * 17 + 4  # corner (4, 4)

    def __init__(self, terrain_obj):
        depsgraph = bpy.context.evaluated_depsgraph_get()
        mesh = terrain_obj.evaluated_get(depsgraph).data
        self.to_world = terrain_obj.matrix_world.copy()
        self.to_local = self.to_world.inverted()
        self.normal_to_world = self.to_local.to_3x3().transposed()
        self.down = self.to_local.to_3x3() @ Vector((0.0, 0.0, -1.0))
        self.tree = BVHTree.FromObject(terrain_obj, depsgraph)
        attr = mesh.attributes.get(tr.AREA_ATTRIBUTE)
        self.values = np.full(len(mesh.polygons), -1, dtype=np.int32)
        if attr is not None and attr.domain == "FACE":
            attr.data.foreach_get("value", self.values)

    def _ray(self, x, y, z_top, length):
        loc, normal, index, _ = self.tree.ray_cast(self.to_local @ Vector((x, y, z_top)), self.down,
                                                   length / self.down.length)
        return None if loc is None else (self.to_world @ loc, normal, index)

    # A sample exactly on the mesh's outer edge (a scaled world's border
    # lands on cell borders) misses straight down; these offsets, tried in
    # this fixed order, decide it the same way for every cell sharing it.
    NUDGES = ((0.0, 0.0), (1, 1), (1, -1), (-1, 1), (-1, -1))

    def cast(self, x, y, z_top, length, nudge):
        """(world z, unit world normal) of the first surface below (x, y), or None."""
        for dx, dy in self.NUDGES:
            hit = self._ray(float(x) + dx * nudge, float(y) + dy * nudge, z_top, length)
            if hit is not None:
                n = (self.normal_to_world @ hit[1]).normalized()
                return hit[0].z, (n.x, n.y, n.z)
        return None

    def chunk_areas(self, grid, cell, result):
        xy = tr.sample_xy(grid, cell)[:, self.MIDDLE] - grid.scene_offset[:2]
        out = np.full(256, -1, dtype=np.int64)
        for ci in range(256):
            hit = self._ray(xy[ci, 0], xy[ci, 1], result.z[ci, self.MIDDLE] + 1.0, 2.0)
            if hit is not None:
                out[ci] = self.values[hit[2]]
        return out
