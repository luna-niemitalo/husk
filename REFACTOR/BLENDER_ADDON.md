# BLENDER_ADDON.md — stage 4, the consumer

**Target, not current state.** See `REFACTOR/README.md` for the pipeline and the
invariants referenced by number below.

## Current state, stated plainly

`DESIGN.md:142-143` lists as a non-goal: *"No Blender addon. A `.glb` file
Blender's stock importer can open is the entire deliverable."*

There are ~4,150 lines of Blender Python in `tools/` — `husk_blender_geoset_mask.py`
(3,505), `husk_blender_options_panel.py` (643) — building Geometry Nodes
switches, shader node graphs, jiggle-physics chains, emitter markers, animation
assets, and a registered `View3D` N-panel. That is an addon in everything but
packaging. The non-goal is stale and gets corrected when this lands.

## What the addon must not do

Luna's requirement, and the whole point of I3:

> the blender side script shouldn't look for data from any random directories,
> nor have input params for "extra data dir" and DEFINITELY not depend on husk
> in any way.

Today it does all three. One question — *where is this texture* — has six
answers (`AUDIT.md` §4): a `--textures` argument, the `.glb`'s directory, the
`.blend`'s directory, a preferred `textures/` subdirectory, a `*_<fdid>.png`
glob in that directory *and its parent*, and a `husk` binary located on `PATH`
or at `../build/husk` and invoked as a subprocess to convert BLP.

Under the bundle, all six are **deleted, not reduced**:

| Deleted | Replaced by |
|---|---|
| `--textures` argument | the manifest's own relative `uri` |
| `.glb`-dir / `.blend`-dir / `textures/` subdir fallbacks | nothing — there is one entry point |
| `*_<fdid>` glob in dir and parent | resolution already happened in husk, at stage 2 |
| `_find_husk_binary` (`:1486`) | nothing — no husk dependency |
| `_convert_blp_to_png_cached` (`:1505`) + temp-dir cache | husk writes PNG at bundle-write time, once |

Import becomes: point at a manifest. Nothing else.

## Packaging

`blender/husk_addon/`, a real installable addon, split by concern rather than by
"the file we already had". Each module under the `FILE_SIZE.md` ceiling — the
current 3,505-line script is 3.5× over it with no §4 justification comment.

```
blender/husk_addon/
  __init__.py          registration, preferences
  manifest.py          THE ONE reader — shared, not copied
  importer.py          bundle -> Blender datablocks
  geosets.py           Geometry Nodes switch
  customization.py     shader-node choice switches
  materials.py         texture layers, blend modes, tint/fade/UV animation
  physics.py           jiggle-bone chains
  emitters.py          ribbon/particle markers
  animation.py         Action asset marking
  panel.py             the View3D N-panel
```

`manifest.py` is what kills the current duplication: `_root_joint_extras` and
`_deep_copy_id_property` are copied verbatim into
`husk_blender_options_panel.py:99-119` under an explicit "resync from there if
they ever diverge" policy, and the geoset vertex-group naming convention is
duplicated a third time as a regex plus two prefix constants (`:73-84`). One
importable module, imported twice, replaces all of it.

## What the bundle buys the addon

- **Real geosets.** No `Skeleton::GeosetTag` fake joints to read as vertex
  groups and then delete again (`delete_geoset_tag_bones`), and no inflated
  `skin.joints`.
- **No carrier-bone heuristic.** `_root_joint_extras` currently finds husk's data
  by scanning bones for known key names, because Blender drops `skin.extras` and
  post-import bone order does not match glTF joint order (242/358 mismatches on a
  real 245-bone character). The manifest is just read.
- **Trustworthy names.** Every `Ref` says where its name came from (I6), so the
  panel can show a DB2 name confidently and mark a listfile-derived one as
  uncertain rather than presenting both identically.
- **A schema version** to check against, instead of probing for key presence.

## `render_glb.py`, resolved by construction

`tools/corpus_scan_tasks/render_glb.py` imports `husk_blender_geoset_mask` for
billboard alignment only (`:41-42`) and never runs the customization or geoset
stages. So a preview of any model carrying `chr_customization_options` renders
whichever candidate husk arbitrarily embedded as each slot's default. This was
found the expensive way: a correct export produced a washed-out preview, and the
preview was believed over the export
(`TODO/RENDER_PIPELINE_DRIFT_TODO.md` item 1).

Once import is a single addon entry point, the preview path *is* the import
path, and a preview cannot silently skip stages again. `render_glb.py` shrinks to
what it should always have been: import via the addon, frame, render.

This file also absorbs `TODO/RENDER_PIPELINE_DRIFT_TODO.md`'s second note — the
~7.9M `gltf_validator` `WEIGHTS_0`/`JOINTS_0` issues on
`example_exports/linore/linore.glb` (non-normalized weight sums, duplicate joint
indices within a vertex). Not yet correlated to anything visual, still worth its
own look, and plausibly related to the fake geoset joints this work removes.

## Gate

Headless, following `tests/blender_import_check.py`'s existing pattern:

- import a real bundle with **zero arguments** and **no `husk` on `PATH`** —
  proving both the "runnable as a pure Blender script" property Luna already
  asked for and the no-husk-dependency requirement;
- every `read_*` equivalent returns the same real data as today's extras path,
  checked against a real `bloodelffemale_hd` / `nightelffemale_hd` fixture;
- a character bundle carrying equipped gear **resolves its referenced item
  bundles with no extra input** — the reference is followed from the manifest
  alone (I3), and an *unresolved* reference (husk knew the item but could not
  produce its model) surfaces as a visible, named gap rather than silently
  missing geometry. This is the real end-to-end case
  `CHARACTER_PIPELINE_TEST_FINDINGS.md` exercised by hand.

The final visual pass is Luna's own — the same human gate
`TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md` already carries. No self-certifying
"looks better."

## Carry forward, don't lose

Real findings embedded in the current script that must survive the rewrite, each
of which was paid for once already:

- `ALWAYS_VISIBLE_VARIANTS` — group 0 / variant 0 is the base body, not a
  hairstyle; treating it as a mutually-exclusive choice made the body vanish.
- The single-`Separate Geometry` design — chaining `GeometryNodeSeparateGeometry`
  loses faces with mixed-selection corners from *both* outputs.
- Synthetic "none" choices for groups the M2 has no geoset for (the tabard case).
- The deep-copy requirement on Blender ID properties — live views into Blender's
  storage segfault after an unrelated bone is deleted.
- `_run_stage`'s per-stage isolation, so one failing stage doesn't silently kill
  every later one.
