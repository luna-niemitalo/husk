# BLENDER_ADDON.md — stage 4, the consumer

**Target, not current state.** See `REFACTOR/README.md` for the pipeline and the
invariants referenced by number below.

## Current state, stated plainly

- **No model addon exists.** The legacy glTF Blender scripts
  (`husk_blender_geoset_mask.py`, `husk_blender_options_panel.py`) were
  deleted with the glTF pipeline; Blender's stock importer reads
  `export --glb` (mesh, skin, animation, base-colour materials only).
- **`tests/bundle_import_check.py`** (diagnostic, not the addon) reads a
  model bundle's `manifest.json` and binaries directly — no glTF in the
  loop — and builds a mesh, armature and base-colour-textured materials,
  gated into `tests/test_conformance.cpp`. Fixtures: `bloodelffemale.m2`
  (mesh/skeleton counts equal the M2 header's) and
  `test_data/creature/fox/fox.m2` (materials; deliberately not a character,
  so the check doesn't imply the customization surface is covered). No
  blend-op/tint/UV-animation shading, animation, geosets, customization,
  items or physics.
- **What the bundle carries for the addon to read**: options and presets
  (customization, creature options), geosets, items with their own model
  bundles, physics, correction sets, material tracks
  (`REFACTOR/BUNDLE_FORMAT.md`).

## What the addon must not do

Luna's requirement, and the whole point of I3:

> the blender side script shouldn't look for data from any random directories,
> nor have input params for "extra data dir" and DEFINITELY not depend on husk
> in any way.

The retired scripts broke all three: one question — *where is this
texture* — had five answers (`AUDIT.md` §1.1: a `--textures` argument, the
`.glb`'s directory, the `.blend`'s directory, a `textures/` subdirectory,
and a `*_<fdid>.png` glob in that directory and its parent). Under the
bundle there is one: the manifest's own relative `uri`; resolution already
happened in husk, at stage 2.

Import is: point at a manifest. Nothing else.

## Packaging

`blender/addons/husk_addon/` (so `blender/` is a Blender Script Directory), a
real installable addon, split by concern, each module under the
`FILE_SIZE.md` ceiling. Its world-authoring part landed first (2026-10-17,
`BLENDER_WORLD_AUTHORING.md`); its `docs.py` is the one reader below.

```
blender/addons/husk_addon/
  __init__.py          registration, preferences
  docs.py              THE ONE reader — shared, not copied
  importer.py          bundle -> Blender datablocks
  geosets.py           Geometry Nodes switch
  customization.py     shader-node choice switches
  materials.py         texture layers, blend modes, tint/fade/UV animation
  physics.py           jiggle-bone chains
  emitters.py          ribbon/particle markers
  animation.py         Action asset marking
  panel.py             the View3D N-panel
```

`docs.py` is the one reader every module imports; nothing re-parses the
manifest or copies a naming convention.

## What the bundle gives the addon

- **Real geosets**, per primitive — no fake joints to read as vertex groups.
- **No carrier-bone heuristic**: data is read from the manifest, not found by
  scanning bones (Blender reorders bones against glTF joint order: 242/358
  mismatches on a real 245-bone character).
- **Trustworthy names.** Every `Ref` says where its name came from (I6), so the
  panel can show a DB2 name confidently and mark a listfile-derived one as
  uncertain rather than presenting both identically.
- **A schema version** to check against, instead of probing for key presence.

**One import path for previews too.** A preview renderer that imports by a
different path than the addon can silently skip stages; the retired
`render_glb.py` skipped customization and geosets, and a washed-out preview
was once believed over a correct export. Previews import via the addon.

## Gate

Headless, following `tests/blender_import_check.py`'s existing pattern:

- import a real bundle with **zero arguments** and **no `husk` on `PATH`** —
  proving both the "runnable as a pure Blender script" property Luna already
  asked for and the no-husk-dependency requirement;
- every reader returns the manifest's real data, checked against a real
  `bloodelffemale_hd` / `nightelffemale_hd` fixture;
- a character bundle carrying equipped gear **resolves its referenced item
  bundles with no extra input** — the reference is followed from the manifest
  alone (I3), and an *unresolved* reference (husk knew the item but could not
  produce its model) surfaces as a visible, named gap rather than silently
  missing geometry. This is the real end-to-end case
  `investigations/CHARACTER_PIPELINE_TEST_FINDINGS.md` exercised by hand.

The final visual pass is Luna's own. No self-certifying "looks better."

## Carry forward, don't lose

Findings from the retired Blender scripts (in git history before 8496092),
each paid for once already:

- `ALWAYS_VISIBLE_VARIANTS` — group 0 / variant 0 is the base body, not a
  hairstyle; treating it as a mutually-exclusive choice made the body vanish.
- The single-`Separate Geometry` design — chaining `GeometryNodeSeparateGeometry`
  loses faces with mixed-selection corners from *both* outputs.
- Synthetic "none" choices for groups the M2 has no geoset for (the tabard case).
- The deep-copy requirement on Blender ID properties — live views into Blender's
  storage segfault after an unrelated bone is deleted.
- `_run_stage`'s per-stage isolation, so one failing stage doesn't silently kill
  every later one.
