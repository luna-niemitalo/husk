"""Reading and writing husk documents (schema/*.schema.json is the definition).

The one place that touches manifest JSON and BufferSlice bytes. Everything
else works on numpy arrays and plain dicts.
"""
import json
import os

import numpy as np

# The schema version every document the addon writes conforms to; its
# counterpart is writeContractHeader in src/writers/bundle_common.cpp. Older
# documents are read as long as nothing they lack is needed.
SCHEMA_VERSION = "0.2.2"

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
