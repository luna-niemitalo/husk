"""A world's zones (world.schema.json `zones`): which cells to load for a
zone name, and the zone table an exported world writes.

A zone lists only the cells whose surface is directly in it; its whole
extent is its own cells and its descendants' (Northshire is Northshire
Valley, Northshire Vineyards and Echo Ridge Mine).
"""
import json
import uuid

from . import terrain as tr

# Duplicate of the namespace in src/uuid.cpp: ids made here and by the
# exporter come from the same space.
HUSK_UUID_NAMESPACE = uuid.UUID("6f0f3a52-8d1c-4b7e-9a42-5c3e1d7b2f10")

REEXPORT_HINT = ("re-export its world document with zones: husk export-map <maps>/<map> <out>/<map>.world.bundle "
                 "--db2-dir <dbfilesclient> --dbd-dir <WoWDBDefs> --tiles-from <tree>/maps/<map>")


def _zones(world):
    zones = world.get("zones")
    # IN, critical: worlds written before schema 0.2.2, or without
    # AreaTable, carry no zones (or only unnamed ones) to pick from.
    if not zones or not any("name" in z for z in zones):
        raise ValueError("this world document has no named zones; " + REEXPORT_HINT)
    if not all("id" in z for z in zones):
        raise ValueError("this world document's zones have no ids (schema before 0.3.0); " + REEXPORT_HINT)
    return zones


def names(world):
    return sorted({z["name"] for z in world.get("zones", []) if "name" in z}, key=str.casefold)


def cells_for(world, name, min_coverage):
    """Cells of every zone named `name` and of every zone inside one, where
    together they cover at least `min_coverage` of the cell, sorted."""
    zones = _zones(world)
    wanted = name.strip().casefold()
    roots = [z["id"] for z in zones if z.get("name", "").casefold() == wanted]
    if not roots:
        close = sorted({z["name"] for z in zones if wanted in z.get("name", "").casefold()}, key=str.casefold)
        raise ValueError("no zone named %r%s" % (name, ("; did you mean: " + ", ".join(close[:12])) if close else ""))
    by_id = {z["id"]: z for z in zones}
    children = {}
    for z in zones:
        if "parent" in z:
            children.setdefault(z["parent"], []).append(z["id"])
    seen, stack = set(), list(roots)
    while stack:
        zone_id = stack.pop()
        if zone_id in seen:
            continue
        seen.add(zone_id)
        stack += children.get(zone_id, [])
    coverage = {}
    for zone_id in seen:
        for c in by_id[zone_id]["cells"]:
            coverage[(c["i"], c["j"])] = coverage.get((c["i"], c["j"]), 0.0) + c["coverage"]
    return sorted(c for c, share in coverage.items() if share >= min_coverage)


def _key(identity):
    return json.dumps(identity, sort_keys=True)


def rebuild(source_world, kept_cells, baked):
    """The exported world's zones: the source's (names, nesting, source ids)
    with each zone's cells as they now are. `kept_cells`: source cells
    referenced unchanged, which keep their entries. `baked`: {(i, j): {area
    identity key: chunk count}} for every baked cell. An area no source zone
    describes becomes a new, unnamed top-level zone."""
    zones = [{k: v for k, v in z.items() if k != "cells"} for z in source_world.get("zones", [])]
    cells = [[c for c in z["cells"] if (c["i"], c["j"]) in kept_cells] for z in source_world.get("zones", [])]
    index = {_key(z["source"]["id"]): k for k, z in enumerate(zones)}
    map_name = source_world.get("subject", {}).get("name", "")
    chunks = float(tr.CHUNKS * tr.CHUNKS)
    for (i, j), counts in sorted(baked.items()):
        for key, count in sorted(counts.items()):
            if key not in index:
                index[key] = len(zones)
                zone_id = str(uuid.uuid3(HUSK_UUID_NAMESPACE, "zone/%s/%s" % (map_name, key)))
                zones.append({"id": zone_id, "source": {"id": json.loads(key)}})
                cells.append([])
            cells[index[key]].append({"i": i, "j": j, "coverage": count / chunks})
    for z, c in zip(zones, cells):
        z["cells"] = c
    return zones


def area_key(area_ref):
    return _key(area_ref["id"])
