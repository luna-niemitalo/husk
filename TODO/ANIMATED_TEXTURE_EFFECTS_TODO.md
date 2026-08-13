# TODO: animated texture effects (spinning sigils, decals, spell quads)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Background

Prompted directly, scoping a gap first sketched as one line item in
`M2_COMPLETENESS.md`'s "Texture transform (UV scroll/rotate/scale,
animated case)" and "Animated material tint/fade" rows (both correctly
marked `extras-capped, permanent` — no core-glTF animation-channel target
for a material property exists) into its real size: **a lot of spell
effects and decals are nothing but a flat quad with a rotating/scrolling
texture on it** — a spinning ground sigil, a pulsing rune, a scrolling
energy beam. Husk already resolves and exports every real curve involved
(`texture_transform`/`tint_animation`/`fade_animation` extras,
`gltf::Material`) — the gap isn't parsing, it's that nothing downstream
ever plays these curves back, so every one of these effects renders as a
static, non-spinning, non-pulsing quad. Given how common this shape of
effect is across spell/`particles`/decal models, closing it is a bigger
visual-completeness win than its one-line `extras-capped, permanent` table
row suggests.

Real prior art this builds on directly: `render_glb.py`'s
`fix_additive_materials` and `tools/husk_blender_geoset_mask.py`'s
billboard alignment (this session) both already establish the pattern —
husk exports a real, correct extras value; a Blender-side companion step
rebuilds real behavior from it post-import. This is the same pattern,
applied to *animated* extras instead of static ones, which is why it needs
more infrastructure than a single node-graph fix.

**Real scope, measured**: `tools/corpus_scan_tasks/animated_texture_effects_task.py`
(new, built on `corpus_scan_framework.py`, same adaptive-concurrency driver
`render_glb.py`'s own corpus runs use) scanned the full local corpus (130,576
`.m2` files, 16.1s) for a genuinely-animated (not merely constant --
distinguished the same way husk's own `resolveAnimatedColorCurve`/
`resolveAnimatedFixed16Curve` do, see the task module's doc comment)
`M2TextureTransform`/`M2Color` tint/`M2Color` alpha/`M2TextureWeight` track,
independent of husk's own code (reads the header arrays and raw `M2Track`
bytes directly, offsets verified against the real `bloodknightcharger.m2`
fixture already in this repo). Result: **36,086 / 130,576 files (27.6% of
the corpus)** carry at least one such curve --

- 28,454 files have an animated `M2TextureTransform` (UV scroll/rotate/scale)
- 10,906 files have an animated `M2Color` alpha (fade)
- 7,845 files have an animated `M2TextureWeight`
- 2,542 files have an animated `M2Color` tint

This is presence only, not reachability -- it doesn't check whether the
animated record is actually referenced by a live batch/material the way
`find_texture_transform_files.py`'s skin-batch cross-check does for the
constant case (no material-to-color-index cross-reference exists yet for
tint/fade/weight), so the true "renders visibly different once played back"
count is somewhat lower than 36,086 -- but even a conservative reading
confirms the Background section's claim: this is a large, corpus-wide visual
gap, not a handful of spell-effect edge cases, and justifies the staged
investment below.

**This is explicitly staged/scoped down, not a single task**:

## 1. A framework for exporting short animated clips, not just one static image

`tools/corpus_scan_tasks/render_glb.py` renders exactly one still frame
per file (`bpy.ops.render.render(write_still=True)`). Showing a rotating
sigil actually rotating needs a short animated output instead — a handful
of frames (or a real render across the clip's own duration) encoded as an
animated image (animated WebP, matching the existing still-WebP choice
and its own quality/size precedent, or a short video). Real design
questions, not yet answered: how many frames is enough to read as
"spinning" without blowing up corpus-wide render time/storage across the
scale this project already renders at (130k+ files); which of a model's
several real animation sequences to pick for the preview (idle? the first
sequence? every sequence husk marks as texture-transform-driven
specifically?); whether this becomes a new render mode `render_glb.py`
opts into per-file (only for files that actually have an animated
`texture_transform`/tint curve, not universally) or a wholly separate
script.

## 2. Update the live gallery viewer for animated output

`tools/live_gallery_server.py` currently serves static `.webp`/`.png`
thumbnails (`corpus_reports/renders_full`). Needs to actually play an
animated clip inline (or provide a clear way to trigger playback) once (1)
above produces one — not just silently degrade to showing the first frame.

## 3. Making these curves actually animate in Blender post-import — DONE

Closed this session, prompted directly with a concrete request for a real
debug/validate pipeline "same case as the lightforged lamp" (one simple,
unambiguous fixture, same role that one served for billboard-rotation
verification).

**A real gap closed first, found while building this**: `texture_transform`
extras used to carry NO real data at all for the genuinely-animated case
(`gltf_mesh.hpp`'s own old doc comment: "just each field's un-animated
default and not real data"). `src/export_materials.cpp`/`src/gltf_mesh.cpp`
now export the real translation/rotation/scaling keyframe curve as
`texture_transform_animation` material extras, same "full curve as inert
diagnostic + Blender-script playback source" shape `tint_animation`/
`fade_animation` already had — `resolveAnimatedColorCurve` reused for
translation/scaling (same seconds→Vec3 shape as a tint curve, just not a
color), a new `resolveAnimatedRawQuatCurve`/`resolveRawQuatTrackSequence`/
`resolveRawQuatGlobalSequenceTrack` (`src/m2_animation.{hpp,cpp}`) for
rotation — M2TextureTransform's rotation track is a *raw* `C4Quaternion`
(4 plain floats), not the compressed `M2CompQuat` bone rotations use, so
the existing `resolveQuatTrackSequence` couldn't be reused as-is.

**Test fixture, found the same way the lightforged lamp was**: scanned the
corpus (this file's own `animated_texture_effects_task.py`) for a file with
exactly one animated transform, then cross-checked against real `.skin`
batch data (most corpus hits, including a first-choice candidate, turned
out to be dead array entries no batch's `textureTransformComboIndex`
actually resolves to — a real trap, not a hypothetical one). Landed on
`unk_exp11_7037014.m2` (`test_data/models/spells/`, real FileDataID
7037014): 18 vertices, 1 bone, 1 material, 0 particle/ribbon emitters, one
real, batch-referenced translation curve — a clean one-way X-axis UV scroll
from (0,0,0) at 0ms to (1,0,0) at 4167ms. Committed as a real integration
test (`tests/test_integration_texture_transform.cpp`), plus unit coverage
for the new raw-quat resolvers (`tests/test_m2_animation_tracks.cpp`).

**Blender side** (`tools/husk_blender_geoset_mask.py`, its fourth
independent job): direct per-frame computation + a `frame_change_pre`
handler — the same choice the billboard-alignment work already made between
a native construct and direct computation, and it generalizes here too, for
the same reason (no native Blender node animates on "current scene time
looped against an arbitrary duration" without a driver/handler regardless).
`apply_texture_transform_animation` builds one shared `Mapping` node per
concerned material (wired ahead of any Image Texture node whose own
`Vector` input isn't already linked to something else — the shape Blender's
stock glTF importer leaves) and recomputes its Location/Rotation-Z/Scale
every frame. `apply_tint_fade_animation` drives a Principled BSDF's Base
Color/Alpha directly (inserting a multiply node ahead of Base Color only
when it's already texture-fed). Both share `_eval_scalar_curve`/
`_eval_vec3_curve`/`_eval_quat_curve_z_angle`/`_curve_duration` — real
linear interpolation (slerp for rotation) between real keyframes, not a
step function. **Verified end-to-end, headlessly, against the real
`unk_exp11_7037014.m2` fixture**: the Mapping node's Location.x reads 0.0 at
frame 1 (t=0), 0.5 at frame 51 (t≈2.08s, the curve's own midpoint), and
correctly wraps past the loop boundary at a much later frame — exactly the
lerp+loop math predicts. `apply_tint_fade_animation` was smoke-tested
against a real file with genuine tint/fade curve data (`stasistotem.m2`,
9 animated batches) and runs without crashing (including the real, already-
known "unlit materials get no Principled BSDF at all" Blender-importer
quirk `render_glb.py`'s own `fix_additive_materials` already documents —
handled here by skipping with a clear message, not crashing) — but is
**not verified against real ground-truth values** the way the
texture-transform case is, flagged as such in its own doc comment.

**Clip-length question, answered**: not a fixed convention ("24 frames ≈ 1
second") — each curve already carries its own real duration (its last
keyframe's timestamp), which is what actually matters for correctness.
`_extend_frame_range_for_duration` grows (never shrinks) `scene.frame_end`
to fit the *longest* registered curve at the scene's existing frame rate
(Blender's own default, 24fps, is a fine baseline — the frame *count* is
what should vary per model, not the rate). Concretely: the real fixture's
4.167s loop needs 100 frames at 24fps, computed, not hardcoded — a
different model's real 1.2s pulse would need 29, and both are correct at
the same 24fps.

**Real robustness requirement, closed alongside this** (was a hard
prerequisite before adding a 4th/5th stage to this script, flagged in an
earlier draft of this file): `tools/husk_blender_geoset_mask.py`'s `main()`
now runs every stage (geoset switch, billboard alignment, texture-layout
overlay, texture-transform animation, tint/fade animation) through a shared
`_run_stage` wrapper — a failure in one prints a loud, specific error
(model name, stage name, real exception type/text) and the rest still run,
instead of the whole script dying on the first exception.

## 4. Scope note (v2, not now): skeleton-animated models

Everything above is scoped to **texture-only** animation on models that
may otherwise be static or use only pose-space animation the corpus
renderer doesn't currently play back either. A natural, larger follow-up
once this lands: extending short-clip preview rendering to real skeletal
animation playback too (not just a rotating texture on a static mesh) —
explicitly out of scope for this file, noted here only so it isn't
forgotten or accidentally half-attempted while this v1 (textures only) is
still in progress.

## 5. Open, unresolved

`??? other` — flagged directly as an incomplete list, not a closed one.
Revisit once 1-3 above are further along; more real gaps are likely to
surface once actual implementation starts (same pattern every other
TODO file in this project has followed).
