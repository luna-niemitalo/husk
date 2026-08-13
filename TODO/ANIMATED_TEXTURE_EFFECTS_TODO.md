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

## 1. A framework for exporting short animated clips, not just one static image — DONE

Closed 2026-08-13, prompted directly, through three real iterations (each
one a direct correction, not self-caught) -- see below for the final
design. `tools/corpus_scan_tasks/render_glb.py`
now renders a short looping animated video instead of one still frame,
whenever a model has *any* real animation source — a skeletal action
(Blender's glTF importer already leaves one active for direct scrubbing,
see `render_duration_seconds`'s doc comment) or husk's own
`texture_transform_animation`/`tint_animation`/`fade_animation` extras
(§3 above). The real design questions this item left open are now answered
concretely, not left as "TBD":

- **How many frames**: a real `RENDER_FPS = 24` playback rate, not a coarse
  handful of total samples — an earlier version of this fix rendered a
  fixed 12 *total* frames across the whole 5s window (2.4fps, a visible
  strobe) via a manual per-frame Python loop (individual `write_still=True`
  calls). Corrected on direct pushback: once Blender and the model are
  loaded (the real fixed cost), per-frame EEVEE rendering is cheap, and
  Blender's own native animation renderer
  (`bpy.ops.render.render(animation=True)`) is both faster and simpler than
  driving it frame-by-frame from Python — confirmed empirically: 100 frames
  at 320×320 rendered in 6.18s via one `animation=True` call, and its own
  internal frame-stepping fires `bpy.app.handlers.frame_change_pre` exactly
  like manual `scene.frame_set()` calls did, so husk's own texture-
  transform/tint-fade handlers need no special-casing to keep working under
  it. A real skeletal action shorter than the render window gets a real,
  native loop too — a Blender `Cycles` F-curve modifier
  (`loop_action_natively`), not per-frame Python math (verified: a frame
  near one native-loop boundary is measurably closer to frame 0 than a
  mid-cycle frame, real pose repetition, not coincidence).
- **Which sequence to pick for the preview**: whichever one the importer
  already activates (`animations[0]` in husk's own export order) — not a
  fresh guess, just using what's already there.
- **Clip length**: `clamp(native_duration, RENDER_MIN_WINDOW_SECONDS=5.0,
  RENDER_MAX_WINDOW_SECONDS=60.0)` — not one fixed window applied to every
  file regardless of its own real length (a second real correction: an
  intermediate version *did* use one fixed 5s window for every file,
  looping short clips *and* truncating long ones alike; corrected again
  once the real per-frame cost turned out to be cheap enough that
  truncating anything under a minute was no longer buying anything). A
  native clip under 5s still loops to fill the floor (don't skip short
  clips, which are common); anything from 5s up to 60s is shown in full, at
  its own real length; only a genuine multi-minute outlier gets clamped to
  the ceiling.
- **New mode vs. universal**: universal, gated by a real measured duration
  (`MIN_ANIMATED_DURATION_SECONDS = 0.05`) — a genuinely static model still
  gets the old single-still path, unchanged, so this doesn't blow up
  render time for the (likely still-majority) static portion of the
  corpus.
- **Output format**: WebM/VP9 video, encoded directly by Blender's own
  FFmpeg output settings (`media_type='VIDEO'`, `file_format='FFMPEG'`,
  `ffmpeg.format='WEBM'`, `ffmpeg.codec='WEBM'` — Blender's own real
  identifier for VP9, confirmed by reading `enum_items` directly rather
  than guessing; `constant_rate_factor='MEDIUM'`, `ffmpeg_preset='GOOD'`,
  `audio_codec='NONE'`), in one `animation=True` call — not the animated-
  WebP-via-PNG-sequence-plus-Pillow-stitch approach an earlier version
  used, a real "step and a half" Luna caught directly and asked to have
  skipped. Confirmed real, not just non-crashing: `ffprobe` on a rendered
  file reports `codec_name=vp9`, the requested resolution, and
  `r_frame_rate=24/1`.
- **Render speed**: `EEVEE_RENDER_SAMPLES = 16` (down from Blender's own
  default 64) plus `use_shadows=False`/`use_fast_gi=False` on
  `scene.eevee`, applied to both the still and animated cases — flat-
  shaded QA thumbnails, not archival renders (this file's own long-
  standing framing for the still case, now applied consistently).
  Measured against the real `wolf.m2` fixture (66 bones, a 120-frame
  animated render): Blender's own defaults took 13.40s; these settings
  took 4.73s for the *entire* animated render, at which point the real
  cost is almost entirely Blender startup + model import, not rendering
  at all -- exactly the "biggest time sink is startup and model loading"
  read this whole item was built around. Sample counts between 1 and 16
  measured within noise of each other time-wise (4.36s-5.02s); 16 was kept
  as a small, effectively-free quality margin over the noisier low end,
  not chosen for speed.

**Real corpus-wide cost, measured, not guessed at**: an animated file now
renders up to `RENDER_MAX_WINDOW_SECONDS × RENDER_FPS` (1440) frames via
one native animation call instead of 1 still-frame call, and the source
`.glb` is now copied next to every rendered output (next item below) — a
real disk-usage increase for a full 130k+-file corpus run, accepted per
direct instruction. The render-time increase turned out to be much smaller
than the frame count alone suggests, once the EEVEE speed settings above
landed: full end-to-end wall time (Blender startup + model import + a
120-frame animated render, WebM/VP9) measured at ~5-6s for both real
fixtures tested (a 50-vertex spell effect and `wolf.m2`'s 557-vertex/
66-bone skeletal case) — barely more than a plain still-frame render, and
dominated by fixed per-file overhead (startup, import), not the animation
render itself, exactly matching the read this whole item was built around.

Also closed in the same pass, a related but independent ask: every
rendered output now gets its real source `.glb` saved as a sibling file
(same basename, `.glb` in place of `.webp`/`.webm`) — not just the baked
preview. This is what makes item 2 below possible at all: a client-side
viewer can only play the *real* glTF animation (skeletal poses, or a
future JS-side port of husk's own extras-driven curves) if the real `.glb`
is actually sitting next to the thumbnail it's browsing, not just the
already-rendered image/video.

**Real downstream ripple, flagged not fixed here**: the animated case's
output extension is now `.webm`, decided by `render_glb.py` itself only
after inspecting the model (a static file still gets the `.webp` its
caller named), so anything that assumed a fixed, caller-known extension
needs to check for both now. `render_sample_driver.py`'s own resume/
already-rendered check (`out_webp.exists()`) does not yet know about
`.webm` — needs updating to treat a `.webm` sibling as "already rendered"
too, or a resumed corpus run will re-render every animated file it already
finished. `tools/live_gallery/server.py` (Luna's own in-progress viewer)
will also need to serve/embed `<video>` for a `.webm` entry rather than
treating everything as a still image. Neither updated in this pass.

## 2. Update the live gallery viewer for animated output — the grid half DONE, the 3D viewer half still open

`tools/live_gallery/server.py` gained a real interactive three.js-based
GLB viewer this session (Luna's own work, in progress/uncommitted as of
this entry — not authored or verified by this session's own agent work,
so not claimed as done here) that loads the `.glb` item 1 above now saves
next to each thumbnail and displays it in-browser, orbit-controls
included. As of this entry it detects and counts `gltf.animations` in the
status line but does **not** yet actually play them back (no
`THREE.AnimationMixer` wired in) — real skeletal playback needs one;
husk's own extras-driven texture-transform/tint/fade curves (§3 above)
have no JS-side port at all yet (the Python/Blender implementation doesn't
translate directly — would need re-implementing the same curve-eval/
looping logic in the viewer's own JS). **Neither gap closed yet** in the
dedicated 3D viewer (`static/viewer.html`/`.js`).

The main grid page (`static/page.html`/`.js`), a separate surface, *was*
closed this session, adopting the same "husk renders real motion, the
browsing UI should show it" principle: a `.webm` item now plays inline
(`<video autoplay muted loop>`) instead of sitting as a static thumbnail,
gated by an `IntersectionObserver` so only on-screen tiles actually decode/
play (same lazy-loading spirit `img.loading='lazy'` already had). A clip
under `FAST_LOOP_THRESHOLD_SECONDS` (0.5s, matching `render_glb.py`'s own
`RENDER_FPS`-derived "12 frames" cutoff) starts **paused** instead, with an
explicit play/pause button in the figcaption — a sub-0.5s native loop reads
as a flicker, not motion, if autoplayed the same way a normal-speed clip
is; real duration is read from the video element itself
(`loadedmetadata`), not guessed. This directly reflects a real, related
correction to item 1's own design: `render_glb.py` no longer pads a short
clip up to a minimum render length at all (dropped `RENDER_MIN_WINDOW_SECONDS`
entirely, once the grid grew native `<video loop>` playback — padding the
*file* never fixed the "too fast" case anyway, since a padded file still
repeats the same fast cycle just as fast, so that's now purely this
client-side concern instead) — every clip renders at exactly its own
native duration, `min(native_duration, RENDER_MAX_WINDOW_SECONDS)`.

Also this session: the whole gallery server split from one monolithic
`tools/live_gallery_server.py` (embedded Python string literals for every
HTML/CSS/JS page) into `tools/live_gallery/` — `server.py` (Python only)
plus `static/page.{html,css,js}`/`static/viewer.{html,css,js}` as real,
independently-editable, IDE-recognized files, served straight off disk per
request (no server restart needed to see an edit) via a new `/static/`
route using the same path-escape guard every other real-file route here
already had. Prompted directly. Content parity confirmed mechanically (an
identical `def`/`class` set extracted from old vs. new before the old file
was removed), not just assumed from the diff.

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
