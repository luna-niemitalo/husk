# TODO: render_glb.py / geoset-mask Blender pipeline drift

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

**Scope: legacy-only.** This is drift between two legacy-pipeline tools
(`render_glb.py`'s preview and `husk_blender_geoset_mask.py`'s switch
logic); already flagged for absorption into `REFACTOR/BLENDER_ADDON.md`
once the canon:: Blender addon replaces both.

1. **`tools/corpus_scan_tasks/render_glb.py` never runs
   `tools/husk_blender_geoset_mask.py`'s customization/geoset logic, so its
   previews are not representative of the real two-step Blender pipeline
   (import → run geoset-mask script → render).** Found 2026-08-23 while
   verifying `CHARACTER_PIPELINE_TEST_FINDINGS.md` Finding 1: a fresh
   Linore render via `render_glb.py` alone came out washed-out lavender,
   no visible face/hair/skin detail — confirmed via an A/B rebuild
   (pre-fix vs. post-fix `db2.cpp`) that this is unrelated to Finding 1's
   `ModelResourcesID` bug (identical render either way). Root cause per
   Luna: this is exactly the drift she flagged twice while developing
   `husk_blender_geoset_mask.py` — `render_glb.py` imports the raw glTF
   and renders whichever customization-choice texture husk happened to
   embed as each hardcoded slot's default (`export_materials.cpp`'s
   "picked arbitrarily as the default" among same-basename candidates,
   `chr_customization_options`), since only the geoset-mask script builds
   the real per-choice switch node graphs
   (`_build_customization_option_group`) and picks the correct choice.
   `render_glb.py` is therefore only a true preview of geometry/rigging,
   never of real customization-correct appearance, for any model carrying
   `chr_customization_options` extras.

   Separately, `gltf_validator` on the same real export
   (`example_exports/linore/linore.glb`) reported ~7.9M issues, almost all
   in two buckets: non-normalized `WEIGHTS_0` sums (~4.47M) and duplicate
   joint indices within one `JOINTS_0` vertex (~3.46M), spread across 44
   primitives sharing one skin — not yet correlated to the washed-out
   render (could be unrelated pre-existing skinning noise, since the
   validator flags the same handful of underlying vertices once per
   primitive that shares the accessor), not investigated further this
   session (out of Finding 1's own scope).

   Next step: decide whether `render_glb.py` should grow an opt-in step
   that runs (or reuses) the geoset-mask script's customization-switch
   logic before rendering, or whether it should stay a raw-geometry-only
   preview with that limitation documented loudly at the top of the
   script and in `README.md`'s render-preview instructions. Either way,
   the current silent drift (a preview that looks broken when the
   underlying export is actually correct) is the real problem to close.
   Once resolved, also check whether the `WEIGHTS_0`/`JOINTS_0` validator
   noise above is a real, separate bug worth its own investigation.
