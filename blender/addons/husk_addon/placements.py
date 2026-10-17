"""Placement-set instances <-> empties that instance an asset collection.

An empty carries what it was imported as (`husk_*` properties); export
reads its transform and its instance collection, and decides from those
properties which set and id it belongs to (BLENDER_WORLD_AUTHORING.md
"Export", Ids).
"""
import json
import os

import bpy
import numpy as np

from . import docs

PLACEMENTS_NAME = "Placements"
AUTHORED = "authored"
_CARRIED_AS_TRANSFORM = ("id", "reference", "translation", "rotation", "scale")


def _shift_distant(rest, delta):
    if "distant" in rest:
        for key in ("bounds_min", "bounds_max"):
            rest["distant"][key] = [float(v) for v in np.asarray(rest["distant"][key]) + delta]
    return rest


def import_set(set_path, set_name, frame_corner, grid, parent, assets, seen, world_level):
    """Every instance of one set document as an empty. `frame_corner`: world
    position of the set's frame origin. `seen` holds instances already made:
    a tile lists every placement overlapping it, and uniqueIds are map-wide."""
    set_doc = docs.load(set_path)
    set_dir = os.path.dirname(set_path)
    coll = parent.children.get(set_name)
    if coll is None:
        coll = bpy.data.collections.new(set_name)
        parent.children.link(coll)
    made = 0
    for inst in set_doc["instances"]:
        key = (world_level, set_name, inst["id"])
        if key in seen:
            continue
        seen.add(key)
        reference = docs.absolute_uris(set_doc["references"][inst["reference"]], set_dir)
        rest = docs.absolute_uris({k: v for k, v in inst.items() if k not in _CARRIED_AS_TRANSFORM}, set_dir)
        # Stored in the world frame so a move between cells is one subtraction.
        rest = _shift_distant(rest, frame_corner)
        world_t = frame_corner + np.asarray(inst["translation"], dtype=np.float64)
        obj = bpy.data.objects.new("%s_%d" % (set_name, inst["id"]), None)
        obj.instance_type = "COLLECTION"
        obj.instance_collection = assets.collection(reference)
        obj.empty_display_size = 1.0
        obj.location = tuple(grid.to_scene(world_t))
        qx, qy, qz, qw = inst["rotation"]
        obj.rotation_mode = "QUATERNION"
        obj.rotation_quaternion = (qw, qx, qy, qz)
        obj.scale = (inst["scale"],) * 3
        coll.objects.link(obj)
        obj["husk_set"] = set_name
        obj["husk_id"] = inst["id"]
        obj["husk_name"] = obj.name
        obj["husk_world_level"] = world_level
        obj["husk_t0"] = [float(v) for v in world_t]
        obj["husk_instance"] = json.dumps(rest)
        shown = set(inst.get("selection", {}).get("placement_sets", []))
        owned = assets.owned_sets(reference)
        obj["husk_owned_sets"] = [index for index, _ in owned]
        for index, always_on in owned:
            if always_on or index in shown:
                add_owned_set(obj, assets, reference, index, always_on)
        made += 1
    return made


def add_owned_set(obj, assets, reference, index, always_on):
    child = bpy.data.objects.new("%s.set_%d" % (obj.name, index), None)
    child.instance_type = "COLLECTION"
    child.instance_collection = assets.set_collection(reference, index)
    child.parent = obj
    child["husk_owned_set"] = index
    child["husk_always_on"] = always_on
    obj.users_collection[0].objects.link(child)
    return child


def is_placement(obj):
    coll = obj.instance_collection if obj.instance_type == "COLLECTION" else None
    return (coll is not None and "husk_ref" in coll and "husk_owned_set" not in obj
            and "husk_asset_member" not in obj)


class Placement:
    """One empty, read back: where it goes and what it writes."""

    def __init__(self, obj, grid):
        self.obj = obj
        mw = obj.matrix_world
        loc, rot, scale = mw.decompose()
        # IN, critical: an instance's scale is one number (placement_set.schema.json).
        if max(scale) - min(scale) > 1e-4 * max(abs(max(scale)), 1e-9):
            raise ValueError("%s: scale %r is not uniform; an instance takes one scale factor" % (obj.name, tuple(scale)))
        self.world_t = grid.to_world(np.array(loc))
        self.rotation = [rot.x, rot.y, rot.z, rot.w]
        self.scale = float(sum(scale) / 3)
        self.reference = json.loads(obj.instance_collection["husk_ref"])
        self.original = "husk_id" in obj and obj.name == obj.get("husk_name")
        self.world_level = bool(obj.get("husk_world_level", False))
        self.set_name = obj["husk_set"] if self.original or self.world_level else AUTHORED
        self.id = int(obj["husk_id"]) if self.original else None
        rest = json.loads(obj.get("husk_instance", "{}"))
        self.unmoved = self.original and np.allclose(self.world_t, obj["husk_t0"], atol=1e-3)
        if "husk_t0" in obj:
            rest = _shift_distant(rest, self.world_t - np.asarray(obj["husk_t0"]))
        rest.setdefault("source", {"flags": "0x0"})
        if "selection" in rest:
            # Sets drawn as child empties are chosen by which children exist;
            # sets not drawn keep the selection they came with.
            drawn = set(obj.get("husk_owned_sets", []))
            kept = {k for k in rest["selection"]["placement_sets"] if k not in drawn}
            chosen = {int(c["husk_owned_set"]) for c in obj.children
                      if "husk_owned_set" in c and not c["husk_always_on"]}
            rest["selection"] = {"placement_sets": sorted(kept | chosen)}
        self.rest = rest


def collect(scene, grid):
    bpy.context.view_layer.update()  # matrix_world is stale until the depsgraph runs
    return [Placement(o, grid) for o in scene.objects if o.type == "EMPTY" and is_placement(o)]


def assign_ids(placements, taken_by_set):
    """Fresh ids for instances made in Blender, above every id their set already uses."""
    for p in placements:
        if p.id is None:
            taken = taken_by_set.setdefault(p.set_name, set())
            p.id = max(taken, default=0) + 1
            taken.add(p.id)
        else:
            taken_by_set.setdefault(p.set_name, set()).add(p.id)


def set_document(name, placements, frame_corner, doc_dir, schema_version, source_detail, resources=None):
    """`resources`: the source set's own (uris absolute), which its
    instances index into (an effect's image inputs); carried unchanged."""
    references, index = [], {}
    instances = []
    for p in sorted(placements, key=lambda p: p.id):
        key = json.dumps(p.reference, sort_keys=True)
        if key not in index:
            index[key] = len(references)
            references.append(p.reference)
        t = p.world_t - frame_corner
        inst = {"id": p.id, "reference": index[key], "translation": [float(v) for v in t],
                "rotation": [float(v) for v in p.rotation], "scale": p.scale}
        inst.update(_shift_distant(json.loads(json.dumps(p.rest)), -frame_corner))
        instances.append(inst)
    doc = {
        "format": "husk-bundle", "kind": "placement_set", "schema_version": schema_version,
        "subject": {"id": {"kind": "none"}, "name": name, "name_source": "synthesized"},
        "space": docs.SPACE, "provenance": docs.provenance(source_detail),
        "definition": {}, "selection": {},
        "references": references, "instances": instances,
    }
    if resources is not None:
        doc["resources"] = resources
    return docs.relative_uris(doc, doc_dir)
