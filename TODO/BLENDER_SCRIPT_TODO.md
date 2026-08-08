# TODO: two real findings from interactive use of `husk_blender_geoset_mask.py`

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

Both findings below came from Luna's own real interactive Blender session
against `example_exports/character/bloodelf/female/bloodelffemale_hd.glb`
(the curated-defaults screenshot, same session), after running
`tools/husk_blender_geoset_mask.py`'s current two jobs (geoset switching +
the new `chr_texture_layout` overlay). Neither has been root-caused yet —
both are reported here exactly as observed, with a concrete, verifiable
starting point, not a guessed fix.

## 1. The texture-layout overlay's toggle is hard to find in the Shader Editor

Reported directly: "I could not find where the overlay toggles where."
`apply_texture_layout_overlay` (`tools/husk_blender_geoset_mask.py`) adds
three new nodes per concerned material — a `ShaderNodeGroup` instance
(`HuskChrTextureLayoutOverlay`, carrying the real `Show Overlay` boolean
checkbox), a magenta `ShaderNodeEmission`, and a `ShaderNodeMixShader` —
but **never sets an explicit `.location` on any of them**. Blender places a
freshly-created node at the node tree's origin `(0, 0)` by default, which
is very likely sitting directly on top of (or immediately behind) whatever
this material's own existing Principled BSDF/Image Texture nodes already
occupy — real, but effectively invisible without manually dragging nodes
apart or pressing Home/"View > Frame All" in the Shader Editor first,
neither of which the module's own docstring or `example_exports/README.md`
mentions needing to do.

**Concrete next step**: give the three new nodes real, non-overlapping
`.location` values, offset well clear of the material's existing graph
(e.g. anchor off the Material Output node's own `.location` plus a fixed
margin, since that node's position is already known via
`output_node.location`) rather than relying on the (0,0) default. Verify
with a real screenshot or a headless bounding-box check
(`node.location` / node dimensions of every node in the tree, confirmed
non-overlapping) that this actually fixes findability — this was never
checked at all before shipping the overlay feature, a real gap in that
work's own verification, not just documentation.

**Secondary, unconfirmed possibility**: separately, Blender's Shader
Editor only shows the *active* object's *active* material slot — with 11
materials on this one mesh, it's also plausible the checkbox was never
actually invisible, just on a material the tester wasn't looking at.
`example_exports/README.md`'s own instructions don't say which specific
material (or how many: 5 for this model) to check, which doesn't help.
Worth tightening those instructions regardless of whether the node-position
fix above turns out to be the real cause.

## 2. A skirt/tunic-lower-edge fragment stays visible with every geoset toggled off

Reported directly: "a fragment of skirt is still visible (a tunic lower
edge?) even if all geosets are disabled, implying that that can not be
filtered out by geosets at all, implying a bug."

Real, verified fact (headless, this session, `bloodelffemale_hd.glb`):
`husk_blender_geoset_mask.py`'s own switch only ever governs the **19
groups with 2+ real variants** (`groups 0, 4, 5, 7, 8, 9, 11, 12, 13, 15,
17, 18, 20, 22, 32, 35, 36, 39, 51`) — by design, per the module's own
doc comment ("a single variant has nothing mutually exclusive to hide").
This model has **4 more geoset groups with only one real variant each**,
which the switch never touches at all, always rendered regardless of any
dropdown state:

| group | geoset_id | real variant(s) |
|---|---|---|
| 10 | 1002 | `variant_2` only |
| 23 | 2301 | `variant_1` only |
| 33 | 3301 | `variant_1` only |
| 34 | 3401 | `variant_1` only |

Group 23 (`geoset_id=2301`) is **already a known, separately-documented
finding**, not this one — an earlier session's own
`example_exports/README.md` traced it to a large opaque
cloak/cape-shaped slab dominating the model's silhouette (`FAILURES2.md`
#1: husk doesn't filter geosets, prints a note, exports every one
unfiltered). The skirt/tunic-lower-edge symptom described here is a
*different* silhouette shape than a cape, so groups **33** (`3301`) or
**34** (`3401`) are the more likely real candidates — genuinely
unconfirmed, not verified against the actual viewport.

**Whether this is a husk bug is exactly the open question, not assumed
either way**: two real, distinct possibilities, both consistent with the
symptom, not yet distinguished:

- **Not a husk bug**: this specific `.m2` genuinely has only one real
  submesh for that body region (no "hide this" empty variant exists in the
  file at all) — the exact same shape as this project's own already-closed
  tabard finding (`GEOSET_MASK_TODO.md`: "no 'none' option exists, because
  the M2 itself has no submesh for 'no tabard' ... a real fact about the
  model, not a husk bug"). If so, the fragment is supposed to always
  render for this model — the "bug," if any, is that `husk_blender_
  geoset_mask.py` gives no visual indication that a group is
  intentionally non-optional, so a human can't tell "always shown, no
  alternative exists" apart from "should be toggleable but the toggle
  is missing."
- **A real husk-side gap**: `apply_geoset_switch`/`geoset_groups`
  (`tools/husk_blender_geoset_mask.py`) group vertex groups purely by the
  `group_<n>,variant_<n>` tag-joint naming husk's own export already
  produces (`Skeleton::geosetTags`, `src/gltf_skeleton.hpp`) — if husk's
  own tag-joint emission has a real bug that fails to give one of this
  region's real variants (including a genuine "empty" one) its own tag
  bone, it would look identical to the "only one real variant exists"
  case above from the Blender side alone. Distinguishing these needs
  cross-checking the model's own real `.skin` submesh list directly (`husk
  dump-chunks`/`husk info`, or `WIKI_FINDINGS/`'s own geoset-tag
  verification method) against what groups 33/34 actually contain, not
  just what Blender's vertex-group list shows post-import.

**Concrete next step**: ground-truth which of groups 33/34 is the visible
fragment by hiding each one's own vertex group by hand in Blender (Weight
Paint or a temporary Mask modifier, independent of the Geometry Nodes
switch) and watching the viewport. Once identified, check that specific
geoset ID's real submesh data directly against the `.skin` file (does a
second, empty-mesh variant exist anywhere in the file that husk simply
isn't tagging?) before concluding either way.
