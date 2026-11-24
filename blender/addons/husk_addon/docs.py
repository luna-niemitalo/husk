"""Reading and writing husk documents (schema/*.schema.json is the definition).

The one place that touches manifest JSON and BufferSlice bytes. Everything
else works on numpy arrays and plain dicts.
"""
import json
import os
import uuid

import numpy as np

# Duplicate of the namespace in src/uuid.cpp: ids made here and by the
# exporter come from the same space.
HUSK_UUID_NAMESPACE = uuid.UUID("6f0f3a52-8d1c-4b7e-9a42-5c3e1d7b2f10")

# The schema version every document the addon writes conforms to; its
# counterpart is writeContractHeader in src/writers/bundle_common.cpp. Older
# documents are read as long as nothing they lack is needed.
SCHEMA_VERSION = "0.4.0"


def schema_id(kind):
    """The `$schema` a document of `kind` carries: its schema's `$id` (schema/<kind>.schema.json)."""
    return "https://husk.invalid/schema/%s.schema.json" % kind

SPACE = {
    "length_unit": "yard",
    "angle_unit": "radian",
    "time_unit": "second",
    "up_axis": "+z",
    "handedness": "right",
}

_DTYPES = {"f32": "<f4", "u32": "<u4", "u16": "<u2", "u8": "u1", "i32": "<i4"}


class FormatError(ValueError):
    pass


def load(path):
    """A document by file path, or by bundle directory (its manifest.json)."""
    if os.path.isdir(path):
        path = os.path.join(path, "manifest.json")
    with open(path) as f:
        doc = json.load(f)
    # IN, critical: every position the addon reads assumes this frame;
    # Blender's axes are taken to be the document's, unconverted.
    if doc.get("space") != SPACE:
        raise FormatError("%s: space is %r, expected %r" % (path, doc.get("space"), SPACE))
    return doc


def read_slice(doc_dir, s):
    """A BufferSlice -> numpy array shaped (count,) or (count, component_count)."""
    dtype = np.dtype(_DTYPES[s["component_type"]])
    expected = s["count"] * s["component_count"] * dtype.itemsize
    # IN, critical: a slice whose length disagrees with its shape is a
    # misread waiting to happen, not something to reinterpret.
    if s["byte_length"] != expected:
        raise FormatError("%s: slice byte_length %d, expected %d for %d x %d %s"
                          % (s["file"], s["byte_length"], expected, s["count"], s["component_count"], s["component_type"]))
    a = np.fromfile(os.path.join(doc_dir, s["file"]), dtype=dtype, count=s["count"] * s["component_count"],
                    offset=s["byte_offset"])
    return a if s["component_count"] == 1 else a.reshape(s["count"], s["component_count"])


class BinWriter:
    """Appends arrays to one .bin file, handing back their BufferSlices."""

    _TYPES = {np.dtype("<f4"): "f32", np.dtype("<u4"): "u32", np.dtype("<u2"): "u16", np.dtype("u1"): "u8",
              np.dtype("<i4"): "i32"}

    def __init__(self, path):
        self.name = os.path.basename(path)
        self.f = open(path, "wb")
        self.offset = 0

    def add(self, array, component_count=1, semantic=None):
        a = np.ascontiguousarray(array)
        raw = a.tobytes()
        self.f.write(raw)
        s = {"file": self.name, "byte_offset": self.offset, "byte_length": len(raw),
             "component_type": self._TYPES[a.dtype], "component_count": component_count,
             "count": a.size // component_count}
        if semantic:
            s["semantic"] = semantic
        self.offset += len(raw)
        return s

    def close(self):
        self.f.close()


def absolute_uris(value, doc_dir):
    """A copy of `value` with every `uri` made absolute against `doc_dir`.

    `uri` appears only in payloads and asset references (BUNDLE_FORMAT.md
    "Structural, not described"), so a key walk finds every one."""
    if isinstance(value, dict):
        return {k: (os.path.normpath(os.path.join(doc_dir, v)) if k == "uri" else absolute_uris(v, doc_dir))
                for k, v in value.items()}
    if isinstance(value, list):
        return [absolute_uris(v, doc_dir) for v in value]
    return value


def identity_key(ref):
    """Mirror of identityKey in src/writers/bundle_common.cpp: the stable-id
    key of an asset ref's identity (BUNDLE_FORMAT.md "Stable ids")."""
    ident = ref["id"]
    kind = ident["kind"]
    if kind == "file_data_id":
        return "file_data_id/%d" % ident["value"]
    if kind == "db2_row":
        return "db2_row/%s/%d" % (ident["table"], ident["row"])
    if kind == "record_index":
        key = "record_index/%d" % ident["value"]
        return key + "/name/" + ref["name"] if ref.get("name") else key
    if not ref.get("name"):
        raise FormatError("%r has neither identity nor name, so no stable id" % (ref,))
    return "name/" + ref["name"]


def reference_id(kind, asset):
    """A reference's stable id; stableId/referenceId in src/writers/bundle_common.hpp."""
    return str(uuid.uuid3(HUSK_UUID_NAMESPACE, kind + "/" + identity_key(asset)))


def reference(kind, asset, **more):
    """A references[] entry for `asset`, carrying its id."""
    return dict({"id": reference_id(kind, asset), "kind": kind, "asset": asset}, **more)


def by_id(entries):
    """Array entries keyed by their stable id, for the pointers that name them."""
    return {e["id"]: e for e in entries}


def references_by_id(doc):
    """A document's references keyed by id: everything points at them by id."""
    return by_id(doc["references"])


def list_document(doc, doc_dir, kind):
    """The document `doc`'s one reference of `kind` (appearances,
    animation_fallbacks) holds, inline (`document`) or as its own file
    (`asset.uri`, a tree's shared list); None without such a reference or
    for an identity-only one (BUNDLE_FORMAT.md "Appearances")."""
    refs = [r for r in doc["references"] if r["kind"] == kind]
    if not refs:
        return None
    # IN, critical: a second list would leave which one holds an entry undecided.
    if len(refs) > 1:
        raise FormatError("expected at most one %s reference, got %d" % (kind, len(refs)))
    ref = refs[0]
    if "document" in ref:
        return ref["document"]
    if "uri" not in ref["asset"]:
        return None
    path = os.path.normpath(os.path.join(doc_dir, ref["asset"]["uri"]))
    # IN, critical: a shared list the tree doesn't hold is a broken tree, not an absent list.
    if not os.path.exists(path):
        raise FormatError("expected the %s document at %s, got no such file" % (kind, path))
    return load(path)


def fallback_animation(table, wanted, clips):
    """The animation a model plays for `wanted`: itself when `clips` (identity
    keys of the model's clip animations) holds it, else the first of its
    fallback chain that does; None when the chain ends without one or comes
    back to where it started (the source's behaviour there is unknown).
    `table`: an animation_fallbacks document; `wanted`: an animation ref
    (REFACTOR/ANIMATION.md "Fallbacks")."""
    following = {identity_key(f["animation"]): identity_key(f["fallback"]) for f in table["fallbacks"]}
    otherwise = identity_key(table["otherwise"])
    key = identity_key(wanted)
    seen = set()
    while key not in clips:
        if key in seen:
            return None
        seen.add(key)
        key = following.get(key, otherwise)
    return key


def relative_uris(value, doc_dir):
    """The inverse of absolute_uris, for a document written to `doc_dir`."""
    if isinstance(value, dict):
        return {k: (os.path.relpath(v, doc_dir) if k == "uri" else relative_uris(v, doc_dir))
                for k, v in value.items()}
    if isinstance(value, list):
        return [relative_uris(v, doc_dir) for v in value]
    return value


def write(path, doc):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")


def provenance(source_detail):
    from . import VERSION
    return {
        "producer": "husk_addon %s" % VERSION,
        "input_format": "blender",
        "sources": [{"name": "source world", "role": "the world this scene was imported from",
                     "available": True, "detail": source_detail}],
    }
