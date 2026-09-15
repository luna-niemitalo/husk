#!/usr/bin/env python3
"""Renders a `husk export --compare-canon` pair (the legacy `.glb` and its
sibling `.canon.glb`) through the same minimal, extras-blind renderer
(`render_lean_glb.py`) and diffs the two resulting images pixel-by-pixel.

This is the automated half of REFACTOR/AUDIT.md §7's "real visual (pixel)
check" gap: `--compare-canon`'s own structural diff (`canon_diff.cpp`)
already confirms mesh/skeleton/animation/material *data* matches, but never
confirms the two files actually render the same. Luna's own framing: render
both, and if they match, that's as much confidence as rendering already
gives for the legacy side alone; if they differ, flag it for a human to
actually look at -- this script does the matching/flagging, not the final
call on a real mismatch.

Usage:
    direnv exec . tools/venv/bin/python tools/compare_canon_render.py <model.glb> [--out-dir DIR] [--threshold N]

<model.glb> is the LEGACY output path `husk export --compare-canon` wrote
(e.g. `bloodelffemale_hd.glb`) -- the canon sibling
(`bloodelffemale_hd.canon.glb`) is derived automatically, the same
`<name>.canon.glb` convention `cmd_export_canon.cpp` itself uses. Exits 0 on
a visual match, 1 on a real mismatch (see --threshold), 2 on a setup/render
failure (missing file, Blender crash) -- distinct from a real mismatch so a
driver script can tell "couldn't check" from "checked, and it differs".
"""

import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops

BLENDER_BIN = "blender"
RENDER_SCRIPT = Path(__file__).resolve().parent / "render_lean_glb.py"
RENDER_TIMEOUT = 120

# Mean per-channel pixel difference (0-255 scale) above which two renders
# of the *same* underlying data are judged to genuinely differ, not just
# to carry ordinary EEVEE TAA/denoise sampling noise. Not measured against
# a large corpus (only the one real bloodelffemale_hd.m2 fixture this repo
# ships with a populated --textures dir for) -- a starting point, expected
# to need retuning once run against more real fixtures; loose enough that
# two genuinely-identical renders should never false-positive, tight
# enough that a wrong texture/pose/material shows up clearly (those produce
# large, spatially-contiguous diffs, not a uniform few-ULP noise floor).
DEFAULT_MEAN_THRESHOLD = 3.0


def render(glb_path: Path, out_png: Path) -> None:
    result = subprocess.run(
        [BLENDER_BIN, "--background", "--factory-startup", "--python", str(RENDER_SCRIPT),
         "--", str(glb_path), str(out_png)],
        capture_output=True, text=True, timeout=RENDER_TIMEOUT,
    )
    if result.returncode != 0 or not out_png.exists():
        raise RuntimeError(
            f"render of {glb_path} failed (exit {result.returncode}):\n"
            f"{result.stdout}\n{result.stderr}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("legacy_glb", type=Path)
    parser.add_argument("--out-dir", type=Path, default=None,
                         help="where to write the two renders + diff image (default: alongside legacy_glb)")
    parser.add_argument("--threshold", type=float, default=DEFAULT_MEAN_THRESHOLD)
    args = parser.parse_args()

    legacy_glb: Path = args.legacy_glb
    canon_glb = legacy_glb.with_suffix("").with_suffix(".canon.glb")
    out_dir = args.out_dir or legacy_glb.parent

    if not legacy_glb.exists():
        print(f"ERROR: {legacy_glb} does not exist", file=sys.stderr)
        return 2
    if not canon_glb.exists():
        print(f"ERROR: {canon_glb} does not exist (expected --compare-canon sibling of {legacy_glb})",
              file=sys.stderr)
        return 2

    legacy_png = out_dir / (legacy_glb.stem + ".render_legacy.png")
    canon_png = out_dir / (legacy_glb.stem + ".render_canon.png")
    diff_png = out_dir / (legacy_glb.stem + ".render_diff.png")

    try:
        render(legacy_glb, legacy_png)
        render(canon_glb, canon_png)
    except RuntimeError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    legacy_img = Image.open(legacy_png).convert("RGB")
    canon_img = Image.open(canon_png).convert("RGB")
    if legacy_img.size != canon_img.size:
        print(f"MISMATCH: render sizes differ ({legacy_img.size} vs {canon_img.size})")
        return 1

    diff = ImageChops.difference(legacy_img, canon_img)
    stats = diff.getdata()
    total = len(stats) * 3
    channel_sum = sum(sum(px) for px in stats)
    mean_diff = channel_sum / total
    max_diff = max(max(px) for px in stats)
    # Pixels with ANY channel differing by more than a small per-pixel
    # tolerance (10/255 -- above ordinary TAA sampling noise) -- reported
    # as a fraction of the image, so a small isolated real difference
    # (e.g. one wrong texture on a small material) is distinguishable from
    # sampling noise spread thinly across every pixel.
    changed_pixels = sum(1 for px in stats if max(px) > 10)
    changed_fraction = changed_pixels / len(stats)

    diff.save(diff_png)

    print(f"legacy render: {legacy_png}")
    print(f"canon render:  {canon_png}")
    print(f"diff image:    {diff_png}")
    print(f"mean per-channel diff: {mean_diff:.3f} (threshold {args.threshold})")
    print(f"max per-channel diff:  {max_diff}")
    print(f"changed pixel fraction (>10/255 on any channel): {changed_fraction:.4%}")

    if mean_diff > args.threshold:
        print("MISMATCH: renders differ beyond threshold -- flagged for review")
        return 1

    print("MATCH: renders visually equivalent within threshold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
