"""Placed assets: each model bundle once, as a collection placements instance.

An asset collection carries its reference (`husk_ref`, absolute uris) so
export reads an instance's asset from whichever collection it draws:
pointing an empty at another asset collection swaps the asset.
"""
import json
import os

import bpy
import numpy as np

from . import docs

ASSETS_NAME = "Assets"


def _material(doc, doc_dir, mat_json):
    mat = bpy.data.materials.new(doc["subject"].get("name", "asset"))
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    textures = doc["resources"].get("textures", [])
    for layer in mat_json.get("layers", []):
        tex = textures[layer["texture"]]
        if tex["state"] != "resolved" or "payload" not in tex:
            continue
        path = os.path.normpath(os.path.join(doc_dir, tex["payload"]["uri"]))
        if not os.path.exists(path):
            continue
        node = mat.node_tree.nodes.new("ShaderNodeTexImage")
        node.image = bpy.data.images.load(path, check_existing=True)
        mat.node_tree.links.new(node.outputs["Color"], bsdf.inputs["Base Color"])
        if mat_json.get("framebuffer_blend", "alpha_key") != "opaque":
            mat.node_tree.links.new(node.outputs["Alpha"], bsdf.inputs["Alpha"])
        break
    return mat


def _mesh(doc, doc_dir, name):
    """The model's level-0 mesh, bind pose, first UV set; None if it has none."""
    mj = doc["resources"].get("geometry", {}).get("mesh")
    if not mj or mj["positions"]["count"] == 0 or mj["indices"]["count"] == 0:
        return None
    positions = docs.read_slice(doc_dir, mj["positions"]).astype(np.float32)
    faces = docs.read_slice(doc_dir, mj["indices"]).astype(np.int32)
    mesh = bpy.data.meshes.new(name)
    mesh.vertices.add(len(positions))
    mesh.vertices.foreach_set("co", positions.ravel())
    mesh.loops.add(len(faces))
    mesh.loops.foreach_set("vertex_index", faces)
    mesh.polygons.add(len(faces) // 3)
    mesh.polygons.foreach_set("loop_start", np.arange(0, len(faces), 3, dtype=np.int32))
    mesh.update(calc_edges=True)
    if mj.get("uv_sets"):
        uv = docs.read_slice(doc_dir, mj["uv_sets"][0]).astype(np.float32)
        uv[:, 1] = 1.0 - uv[:, 1]
        mesh.uv_layers.new(name="uv0").data.foreach_set("uv", uv[faces].ravel())
    for mat_json in doc["resources"].get("materials", []):
        mesh.materials.append(_material(doc, doc_dir, mat_json))
    material = np.zeros(len(faces) // 3, dtype=np.int32)
    for prim in mj.get("primitives", []):
        material[prim["index_start"] // 3:(prim["index_start"] + prim["index_count"]) // 3] = prim["material_index"]
    mesh.polygons.foreach_set("material_index", material)
    return mesh


def _placeholder(coll, name, display):
    obj = bpy.data.objects.new(name, None)
    obj.empty_display_type = display
    obj.empty_display_size = 2.0
    coll.objects.link(obj)


class Assets:
    def __init__(self, root):
        self.root = bpy.data.collections.new(ASSETS_NAME)
        root.children.link(self.root)
        self.by_key = {}
        self.models = {}  # key -> (doc, doc_dir) for models that own placement sets
        self.set_colls = {}

    def exclude_from_view(self, view_layer):
        def find(layer_coll):
            if layer_coll.collection == self.root:
                return layer_coll
            for child in layer_coll.children:
                hit = find(child)
                if hit:
                    return hit
        lc = find(view_layer.layer_collection)
        if lc:
            lc.exclude = True

    @staticmethod
    def key(reference):
        asset = reference["asset"]
        return asset.get("uri") or json.dumps(asset["id"], sort_keys=True) + asset.get("name", "")

    def collection(self, reference):
        """`reference`: a set's reference entry ({kind, asset}), uris absolute."""
        key = self.key(reference)
        if key in self.by_key:
            return self.by_key[key]
        asset = reference["asset"]
        label = asset.get("name") or str(asset["id"].get("value", "asset"))
        coll = bpy.data.collections.new("%s_%s" % (reference["kind"], label))
        coll["husk_ref"] = json.dumps(reference)
        self.root.children.link(coll)
        self.by_key[key] = coll
        uri = asset.get("uri")
        if reference["kind"] != "model":
            _placeholder(coll, label, "SPHERE")
        elif not uri or not os.path.exists(uri):
            # Known but not produced: a visible gap, never missing geometry.
            _placeholder(coll, "missing_" + label, "CUBE")
        else:
            doc_dir = os.path.dirname(uri)
            doc = docs.load(uri)
            mesh = _mesh(doc, doc_dir, label)
            if mesh is None:
                _placeholder(coll, label, "PLAIN_AXES")
            else:
                coll.objects.link(bpy.data.objects.new(label, mesh))
            if doc.get("definition", {}).get("placement_sets"):
                self.models[key] = (doc, doc_dir)
        return coll

    def owned_sets(self, reference):
        """[(index, always_on)] of the asset's owned sets that are their own
        documents (a WMO's doodad sets). An inline set (an M2's bone-anchored
        effects) is the model's own content, not something a placement shows."""
        model = self.models.get(self.key(reference))
        if model is None:
            return []
        doc = model[0]
        return [(k, s["always_on"]) for k, s in enumerate(doc["definition"]["placement_sets"])
                if "uri" in doc["references"][s["reference"]]["asset"]]

    def set_collection(self, reference, index):
        """An owned set (a WMO doodad set) as a collection of instancing empties, asset-local."""
        key = (self.key(reference), index)
        if key in self.set_colls:
            return self.set_colls[key]
        doc, doc_dir = self.models[self.key(reference)]
        entry = doc["definition"]["placement_sets"][index]
        set_ref = doc["references"][entry["reference"]]["asset"]
        coll = bpy.data.collections.new("%s.set_%d" % (set_ref.get("name", "set"), index))
        self.root.children.link(coll)
        self.set_colls[key] = coll
        set_path = os.path.normpath(os.path.join(doc_dir, set_ref["uri"]))
        set_doc = docs.load(set_path)
        set_dir = os.path.dirname(set_path)
        for inst in set_doc["instances"]:
            ref = docs.absolute_uris(set_doc["references"][inst["reference"]], set_dir)
            obj = bpy.data.objects.new("%s_%d" % (coll.name, inst["id"]), None)
            obj.instance_type = "COLLECTION"
            obj.instance_collection = self.collection(ref)
            obj.location = inst["translation"]
            qx, qy, qz, qw = inst["rotation"]
            obj.rotation_mode = "QUATERNION"
            obj.rotation_quaternion = (qw, qx, qy, qz)
            obj.scale = (inst["scale"],) * 3
            # Part of the asset's definition, not a placement of the world.
            obj["husk_asset_member"] = True
            coll.objects.link(obj)
        return coll
