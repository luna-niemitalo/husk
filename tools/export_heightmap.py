"""Whole-map grayscale heightmaps from a husk world export's terrain tiles.

Reads every `<map>_<i>_<j>.bundle` in a map directory (export-world output or
a stored build under the corpus mount) and writes two 16-bit grayscale PNGs:

  <map>_heightmap_normalized.png  linear: map minimum -> 0, maximum -> 65535
  <map>_heightmap_wrapped.png     TerraForma2's wrapped encoding (below)
  <map>_heightmap.json            the numbers needed to turn either back into yards

Raster: 256 texels a tile (16 a chunk, half a quad), 64 tiles a side = 16384,
column = tile i (east), row = tile j (south), north up. Even/even texels are
the chunk corners, odd/odd the centres, the rest the midpoint of the quad
edge they sit on: the triangle fan's own surface there, not a smoothing.
Tiles the map lacks are filled with `--fill` (default: the map's minimum).

Wrapped encoding, as measured on MantleCore's TerraForma2 reference
(`mantlecore/design/heightmap-unwrap.md`): height is an integer step count k,
stored = (256 k + 81) mod 65535. The constant low byte 81 is that file's; any
constant decodes the same. A decoder unwraps by neighbour differences, so no
neighbour step may reach 128 steps. WoW terrain has real walls far steeper
than any useful step allows (azeroth: up to 1243 yd between neighbouring
texels), so the wrapped image limits slope: cliff bottoms are raised until
every neighbour step is at most 127 steps (the smallest such field above the
source). Peaks and the tops of cliffs keep their exact heights; the JSON
counts the texels raised. The normalized image is the unmodified source.

Run with blp/'s venv (numpy, Pillow):
  blp/.venv/bin/python -I tools/export_heightmap.py <map dir> <out dir> [--step 0.5]
"""

import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

TILES = 64
CHUNKS = 16
TEXELS_PER_CHUNK = 16
TEXELS_PER_TILE = CHUNKS * TEXELS_PER_CHUNK
SIZE = TILES * TEXELS_PER_TILE

WRAP_PERIOD = 65535
WRAP_UNITS_PER_STEP = 256
WRAP_LOW_BYTE = 81
WRAP_MAX_NEIGHBOUR_STEPS = 127

TILE_NAME = re.compile(r"^(?P<map>.+)_(?P<i>\d+)_(?P<j>\d+)\.bundle$")

# MCVT order: per row r, 9 corners then (r < 8) 8 centres.
CORNER_INDEX = np.array([[r * 17 + c for c in range(9)] for r in range(9)])
CENTRE_INDEX = np.array([[r * 17 + 9 + c for c in range(8)] for r in range(8)])


def chunk_texels(heights):
    """(N, 145) chunk heights -> (N, 16, 16) half-quad texels; the 17th row/column is the next chunk's."""
    corners = heights[:, CORNER_INDEX]
    grid = np.empty((heights.shape[0], 17, 17), dtype=np.float32)
    grid[:, 0::2, 0::2] = corners
    grid[:, 1::2, 1::2] = heights[:, CENTRE_INDEX]
    grid[:, 0::2, 1::2] = (corners[:, :, :-1] + corners[:, :, 1:]) * 0.5
    grid[:, 1::2, 0::2] = (corners[:, :-1, :] + corners[:, 1:, :]) * 0.5
    return grid[:, :16, :16]


def read_tile(bundle):
    """IN, foreign (a bundle document): the tile's (256, 256) texel heights in yards."""
    terrain = json.loads((bundle / "manifest.json").read_text())["resources"]["terrain"]
    if terrain["chunks_per_side"] != CHUNKS or terrain["quads_per_side"] != 8:
        sys.exit(f"{bundle}: expected 16 chunks x 8 quads a side, got "
                 f"{terrain['chunks_per_side']} x {terrain['quads_per_side']}")
    if terrain["chunk_vertex_layout"] != "interleaved_9x9_corners_8x8_centres":
        sys.exit(f"{bundle}: expected interleaved_9x9_corners_8x8_centres, got {terrain['chunk_vertex_layout']}")
    # Chunk index = row * 16 + column only if rows run -X and columns -Y (BUNDLE_FORMAT.md "Frame and heightfield").
    if terrain["row_axis"] != [-1, 0, 0] or terrain["column_axis"] != [0, -1, 0]:
        sys.exit(f"{bundle}: expected row_axis [-1,0,0], column_axis [0,-1,0], got "
                 f"{terrain['row_axis']}, {terrain['column_axis']}")
    view = terrain["chunks"]["heights"]
    if view["component_type"] != "f32" or view["component_count"] != 145 or view["count"] != CHUNKS * CHUNKS:
        sys.exit(f"{bundle}: expected heights f32 x 145 x 256, got "
                 f"{view['component_type']} x {view['component_count']} x {view['count']}")
    raw = (bundle / view["file"]).read_bytes()[view["byte_offset"]:view["byte_offset"] + view["byte_length"]]
    if len(raw) != view["byte_length"]:
        sys.exit(f"{bundle}: heights view wants {view['byte_length']} bytes, file has {len(raw)}")
    texels = chunk_texels(np.frombuffer(raw, dtype="<f4").reshape(CHUNKS * CHUNKS, 145))
    return texels.reshape(CHUNKS, CHUNKS, 16, 16).transpose(0, 2, 1, 3).reshape(TEXELS_PER_TILE, TEXELS_PER_TILE)


def max_neighbour_step(values):
    return max(np.abs(np.diff(values, axis=0)).max(), np.abs(np.diff(values, axis=1)).max())


def raise_to_slope_limit(steps, limit):
    """Smallest field >= steps whose 4-neighbour differences are <= limit.

    It is max over y of steps(y) - limit * |x - y|_1; that kernel is separable,
    and along one axis the max-plus convolution is two running maxima.
    """
    for axis in (0, 1):
        ramp = np.arange(steps.shape[axis], dtype=np.int32) * limit
        ramp = ramp[:, None] if axis == 0 else ramp[None, :]
        forward = np.maximum.accumulate(steps + ramp, axis=axis) - ramp
        backward = np.flip(np.maximum.accumulate(np.flip(steps - ramp, axis=axis), axis=axis), axis=axis) + ramp
        steps = np.maximum(forward, backward)
    return steps


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("map_dir", type=Path)
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--step", type=float, default=0.5, help="yards per wrapped step; slope limit = 127 steps a texel")
    parser.add_argument("--fill", type=float, help="height in yards for tiles the map lacks (default: map minimum)")
    args = parser.parse_args()

    tiles = {}
    for bundle in sorted(args.map_dir.glob("*.bundle")):
        match = TILE_NAME.match(bundle.name)
        if match is None:
            continue
        tiles[(int(match["i"]), int(match["j"]))] = (match["map"], bundle)
    if not tiles:
        sys.exit(f"{args.map_dir}: expected <map>_<i>_<j>.bundle tiles, found none")
    names = {name for name, _ in tiles.values()}
    if len(names) != 1:
        sys.exit(f"{args.map_dir}: expected one map's tiles, found {sorted(names)}")
    map_name = names.pop()

    heights = np.full((SIZE, SIZE), np.nan, dtype=np.float32)
    for n, ((i, j), (_, bundle)) in enumerate(sorted(tiles.items()), 1):
        heights[j * TEXELS_PER_TILE:(j + 1) * TEXELS_PER_TILE, i * TEXELS_PER_TILE:(i + 1) * TEXELS_PER_TILE] = read_tile(bundle)
        if n % 100 == 0:
            print(f"read {n}/{len(tiles)} tiles", flush=True)
    low = float(np.nanmin(heights))
    high = float(np.nanmax(heights))
    fill = low if args.fill is None else args.fill
    heights[np.isnan(heights)] = fill
    low = min(low, fill)
    high = max(high, fill)
    steepest = float(max_neighbour_step(heights))
    print(f"{map_name}: {len(tiles)} tiles, heights {low:.3f} .. {high:.3f} yd, steepest neighbour step {steepest:.3f} yd")

    step = args.step
    exact = np.rint((heights - low) / step).astype(np.int32)
    steps = raise_to_slope_limit(exact, WRAP_MAX_NEIGHBOUR_STEPS)
    raised = steps != exact
    raise_yards = (steps - exact)[raised] * step
    worst = int(max_neighbour_step(steps))
    assert worst <= WRAP_MAX_NEIGHBOUR_STEPS, worst
    print(f"wrapped: step {step} yd, {int(steps.max()) + 1} steps, {int(raised.sum())} texels raised "
          f"(max {raise_yards.max(initial=0):.1f} yd) to the {WRAP_MAX_NEIGHBOUR_STEPS * step:.1f} yd/texel slope limit")

    normalized = np.rint((heights - low) / (high - low) * 65535).astype(np.uint16)
    wrapped = ((steps.astype(np.int64) * WRAP_UNITS_PER_STEP + WRAP_LOW_BYTE) % WRAP_PERIOD).astype(np.uint16)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    for suffix, image in (("normalized", normalized), ("wrapped", wrapped)):
        path = args.out_dir / f"{map_name}_heightmap_{suffix}.png"
        Image.fromarray(image).save(path)
        print(f"wrote {path}")
    meta = {
        "map": map_name,
        "source": str(args.map_dir),
        "tiles": len(tiles),
        "size": [SIZE, SIZE],
        "length_unit": "yard",
        "texel_pitch": 533.3333 / TEXELS_PER_TILE,
        "orientation": "row 0 = north edge (tile j 0), column 0 = west edge (tile i 0); "
                       "texel (row, column) at world X = (32 - row / 256) * 533.3333, Y = (32 - column / 256) * 533.3333",
        "fill_height": fill,
        "normalized": {"zero_height": low, "full_height": high, "height": "zero_height + value / 65535 * (full_height - zero_height)"},
        "wrapped": {
            "period": WRAP_PERIOD,
            "units_per_step": WRAP_UNITS_PER_STEP,
            "low_byte": WRAP_LOW_BYTE,
            "step": step,
            "zero_height": low,
            "steps": int(steps.max()) + 1,
            "max_neighbour_steps": worst,
            "slope_limit_yards_per_texel": WRAP_MAX_NEIGHBOUR_STEPS * step,
            "texels_raised": int(raised.sum()),
            "max_raise_yards": float(raise_yards.max(initial=0)),
            "height":"zero_height + step * k, where value = (256 k + 81) mod 65535; k unwrapped from neighbours",
        },
    }
    meta_path = args.out_dir / f"{map_name}_heightmap.json"
    meta_path.write_text(json.dumps(meta, indent=2) + "\n")
    print(f"wrote {meta_path}")


if __name__ == "__main__":
    main()
