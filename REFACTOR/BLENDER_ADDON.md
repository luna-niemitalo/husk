# BLENDER_ADDON.md — stage 4, the consumer

**Target, not current state.** See `REFACTOR/README.md` for the pipeline and the
invariants referenced by number below.

**First real step landed (2026-09-16), diagnostic only, not the addon
below**: `tests/bundle_import_check.py` reads a real `--export-canon`
bundle's `manifest.json` + `mesh.bin`/`skeleton.bin`/`resources.materials`
directly — no `bpy.ops.import_scene.gltf`, no glTF anywhere in the loop —
and builds a real Blender mesh + armature + base-color-textured materials
from it, gated into `tests/test_conformance.cpp` alongside the existing
legacy-`.glb` Blender tests. Verified against `bloodelffemale.m2`/`.skin`
for mesh/skeleton (bone/vertex counts read back from the bundle match the
M2 header's own raw counts exactly) and a new real, simple, non-character
fixture (`test_data/creature/fox/fox.m2` — user-populated/gitignored like
every other `test_data/` fixture, added this session specifically because
it has real local-basename-resolvable textures and neither existing
non-character quadruped fixture did) for materials — deliberately not a
real character model: canon::Material's base-color resolution has no
character-specific shape, and a real character fixture also carries the
customization/geoset-selection surface named below as NOT YET WIRED into
canon::Model/bundle_writer.cpp at all, so testing materials against one
risks implying that whole unbuilt subsystem is covered when it isn't.
Material/loaded-image counts match the legacy `.glb`'s own tinygltf-parsed
counts exactly, and `--export-canon`'s own structural diff reports "clean,
no deviations found" against this fixture. This is the "import a real
bundle with zero arguments" half of the Gate section below, proven for mesh
+ skeleton + base-color materials — no blend-op/tint/UV-animation shading,
animation, geosets, customization, or physics yet, and not
`manifest.py`/`importer.py` as real addon modules. The real packaged addon
described in the rest of this file is still not started, and neither is any
canon-side representation of character customization at all (`canon::
Definition`/`Selection`/`Item` types exist but nothing assembles them from
real input yet, and `bundle_writer.cpp` doesn't write them even if they
existed — see this file's own "What the bundle buys the addon" section,
which still describes only geosets/naming, not customization choices).

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

Today it does all three. One question — *where is this texture* — has five
answers (`AUDIT.md` §1.1): a `--textures` argument, the `.glb`'s directory,
the `.blend`'s directory, a preferred `textures/` subdirectory, and a
`*_<fdid>.png` glob in that directory *and its parent*. A sixth answer — a
`husk` binary located on `PATH` or at `../build/husk`, invoked as a
subprocess to convert BLP — was removed outright on 2026-08-29, not reduced:
a `.glb` isn't guaranteed to travel with a `husk` binary nearby, so the
script now reports and skips a `.blp`-only match instead of converting it.

Under the bundle, all five remaining answers are **deleted, not reduced**:

| Deleted | Replaced by |
|---|---|
| `--textures` argument | the manifest's own relative `uri` |
| `.glb`-dir / `.blend`-dir / `textures/` subdir fallbacks | nothing — there is one entry point |
| `*_<fdid>` glob in dir and parent | resolution already happened in husk, at stage 2 |

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
