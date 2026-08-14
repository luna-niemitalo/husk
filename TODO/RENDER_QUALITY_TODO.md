# TODO: render-quality findings from corpus review (rotation, textures, alpha, billboards)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Background

Luna ran another pass through the `/review` corpus-triage page
(`tools/live_gallery/server.py`, merged from the former standalone
`tag_review_server.py` on 2026-08-14) over `corpus_reports/renders_full`'s
rendered preview clips/stills. Her own framing: "same complaints as last
round... except now there was a genuine plethora of fixed cases from the
last round" — this is a fresh/residual sample after real prior fixes, not
a first discovery. Four categories surfaced:

1. A skeletal-animation rotation "shear" — mostly-correct motion that
   visibly collapses/distorts once a bone's rotation angle gets large
   enough. Named repro examples: an elf's two-handed-weapon attack
   animation, and `creature/corruptedtentacle/corruptedtentacle_low.webm`.
2. Renders showing only the background color — no model visible at all.
3. Missing or clearly-wrong textures (partial or complete).
4. Alpha-channel rendering issues, and billboard-alignment issues.

Two research passes (Explore agents, read-only) went through the codebase,
`CLAUDE_HISTORY.md`, `WIKI_FINDINGS*.md`, and everything under `TODO/` to
separate genuinely-new findings from things already fixed or already
tracked elsewhere. Findings below are organized by that split. Nothing in
this file has been fixed yet — investigation and writeup only.

## 1. Rotation "shear" on large-angle bone animation — genuinely new, high-confidence root cause

**Not previously tracked anywhere in this project's docs** (grepped
`CLAUDE_HISTORY.md`/`WIKI_FINDINGS*.md`/`DESIGN.md`/every `TODO/*.md` for
"quaternion"/"slerp"/"gimbal"/"shear"/"hemisphere"/"antipodal"/"sign
flip"/"180 degree"/"M2CompQuat" combined with rotation — every hit is
about unrelated topics, mostly `M2TextureTransform` rotation or *static*
single-rotation coordinate-conversion correctness, never a *sequence* of
rotations for animation continuity).

**Hypothesis (high confidence): a hemisphere/sign discontinuity between
consecutive keyframes, introduced or left unguarded by husk, that gets
linearly interpolated straight through the near-zero quaternion.**

- husk does not slerp or lerp rotation keyframes itself — it emits raw
  (converted) keyframe values as a glTF `LINEAR` (or `STEP`) sampler and
  defers all actual interpolation to the glTF runtime/importer
  (`gltf_skeleton.cpp`'s `addChannel` lambda, ~line 486-530; WoW
  interpolation types 1/2/3 all collapse to glTF `LINEAR`, only type 0
  becomes `STEP` — `gltf_skeleton.hpp` ~line 302-313).
- Two independent, unguarded places can produce a `q` vs. `-q` flip
  between adjacent keyframes (mathematically the same rotation, but a
  literal-component discontinuity for interpolation purposes):
  - **Raw M2 decode**: `readCompQuat` (`src/m2_animation.cpp:112-129`)
    decodes each keyframe's compressed int16 quaternion fully
    independently, with no cross-keyframe continuity check.
  - **Z-up→Y-up conversion**: `gltf::rotationZUpToYUp`
    (`src/gltf_math.cpp:120-124`) round-trips each quaternion through
    `quatToMat3` → conjugate by `kWowToGltf` → `mat3ToQuat`
    (Shepperd's-method matrix→quaternion, branch-selected by the largest
    diagonal component). **`mat3ToQuat`'s own doc comment
    (`gltf_math.cpp:77-84`) explicitly says the returned sign is not
    normalized against any convention** — a deliberate non-guarantee for
    a single static rotation that becomes a real bug once a *sequence* of
    independently-converted quaternions is expected to interpolate
    smoothly. Large rotation swings are more likely to cross the
    branch-selection boundary between one keyframe and the next, which
    lines up with the reported "only at high angles" symptom.
  - Both are called per-keyframe in a loop with zero history:
    `src/export_animation.cpp:123-126`.
- glTF's own `LINEAR` quaternion interpolation is spec-defined to need
  shortest-path (sign-aware) handling, and modern importers (including
  current Blender) generally do this correctly for *already-consistent*
  input — so this doesn't look like a pure glTF-spec/Blender-side
  limitation on its own. But husk's own conversion step can *introduce* a
  flip that wasn't present in the source data, which would defeat even a
  perfectly-correct importer.

**Fixed 2026-08-14.** `gltf::enforceHemisphereContinuity` (new,
`src/gltf_math.hpp`/`.cpp`, alongside `mat3ToQuat` whose own doc comment
already named this exact gap) negates a keyframe's converted quaternion
when its dot product with the *previous already-converted* keyframe is
negative. Wired into `export_animation.cpp`'s `buildJointAnimation`
rotation loop (the single shared function both per-sequence and
global-sequence animation building already funnel through, so both paths
are covered by one fix). Did not additionally apply the fixup before
conversion, directly on raw `M2CompQuat`-decoded keyframes — verifying
post-conversion continuity across the *entire* real repro fixture (14,479
consecutive rotation-keyframe pairs, all 15 of its animations) found zero
remaining hemisphere flips, so there was no evidence of a separate
pre-conversion discontinuity to chase for this fixture; worth revisiting
only if a different real file turns up a case this fix doesn't cover.
Three new unit tests (`tests/test_gltf_math.cpp`) plus real-fixture
verification: exported the committed fixture below, walked every
animation's rotation channel, confirmed 0/14,479 consecutive-keyframe dot
products are negative (was checked against the same real fixture both
before and after the fix, not just after). Full suite green, 625/625.

**Repro, now committed**: `test_data/creature/corruptedtentacle/
corruptedtentacle_low.m2` + `corruptedtentacle_low00.skin` (copied from
`/media/luna/data/wow_export/creature/corruptedtentacle/`, local-only per
this project's usual `test_data/` convention — copyrighted game assets,
gitignored, not committed to git itself).

### Follow-up, same day: the fix is verified correct, but a full corpus
### re-render still shows "shear"-looking artifacts — a second, different
### mechanism, not yet fixed

A fresh full corpus re-render (with the fix above active) was reviewed
live, and several items were flagged as still showing the same visual
symptom: `creature/bloodgodtentacle/bloodgodtentacle.webm`,
`.../bloodgodtentaclethickspikes.webm`, `.../bloodgodtentaclethickspikes_
baked.webm`, `creature/alexstrasza/ladyalexstrasa.webm`/`ladyalexstrasa2.
webm`, `creature/alleria/alleria.webm`/`alleriavoid.webm`,
`creature/abominationsmall/abominationsmall.webm`,
`creature/bloodabomination/bloodabomination.webm`.

Investigated two of these directly (`bloodgodtentacle`, a simple tentacle
chain, and `ladyalexstrasa`, a complex 144-bone humanoid rig) with three
independent, rigorous checks against each file's own actually-previewed
animation clip (`animations[0]`, confirmed via `render_glb.py`'s own
`render_duration_seconds` — Blender's importer already activates this one
by default, and that's what a human watching the corpus render actually
sees, not necessarily whichever clip happens to be picked for a quick
spot check):

1. **Hemisphere-flip count**: re-ran the same dot-product scan the fix
   above was verified with. Zero negative dots in either file's real
   previewed clip — the fix from earlier today is working correctly here
   too, not silently failing.
2. **Bone pose-matrix determinant, every bone, every frame**: a pure
   rotation always has determinant 1.0; any real shear/scale shows up as
   a deviation, independent of camera angle or foreshortening (unlike a
   bounding-box-size proxy, which can't tell a genuine collapse apart
   from a long thin object just pointing more toward the camera). Result:
   determinant stayed at 1.0 (floating-point noise only, ~7e-7) across
   **every bone and every frame in both files**. This is about as
   unambiguous as a check gets — the actual bone transforms being fed to
   the skin are mathematically perfect rigid rotations throughout. There
   is no shear in the transform data itself.
3. **Direct visual render** at the specific frame identified as the
   single worst same-hemisphere large-angle step in `bloodgodtentacle`
   (a real ~179° single-keyframe jump, `anim_16_0` node 14 — not even the
   previewed clip, checked out of thoroughness): looked like an ordinary
   smooth bend, no visible collapse. Consistent with check #2.

**New leading hypothesis: classic linear-blend-skinning ("candy wrapper")
volume loss, not a rotation/export bug at all.** `bloodgodtentacle`'s
mesh has 31.7% of vertices weighted across 2+ bones
(`WEIGHTS_0`/`JOINTS_0`, checked directly from the exported `.glb`) — the
exact precondition for this well-known skinning artifact: even with every
individual bone transform mathematically perfect (confirmed above),
standard linear blend skinning can visibly pinch/thin a mesh at a joint
that bends sharply, because blending two *rotation matrices* linearly
(not the rotations themselves) loses volume — worse the more the two
bones' orientations diverge and the more of a vertex's weight is split
between them. A long, thin, many-jointed chain (a tentacle) bending
sharply is close to a textbook case for this. Checked: Blender's Armature
modifier's own built-in mitigation (`use_deform_preserve_volume`, a real
implementation of the standard fix for this exact artifact) is **not
enabled anywhere in this pipeline** (`render_glb.py`/
`husk_blender_geoset_mask.py`) — neither set by husk's own glTF export
(core glTF has no such flag to carry) nor by Blender's glTF importer nor
by any of this project's own post-import scripts.

**Experiment 1 (falsified): `use_deform_preserve_volume = True`.** Tried
first as the standard LBS-volume-loss mitigation. Made it dramatically
*worse* — a real detached-looking flap/spike appeared that wasn't nearly
as pronounced without it (side-by-side `.webm`s kept at
`example_exports/creature/bloodgodtentacle_shear_investigation/
current_no_preserve_volume.webm` vs. `with_preserve_volume_WORSE.webm`
for reference). Reverted immediately, not shipped. Root cause of *why* it
made things worse turned out to be the real second bug, found next.

**Fixed 2026-08-14, confirmed by Luna directly against the real client.**
The geoset-tag joints (`group_<n>,variant_<n>`, husk's own inert per-
geoset marker bones, always appended to every export — see
`Skeleton::geosetTags`) were assumed to have zero effect on real skinned
deformation, per this project's own prior hazard note ("Blender's own
Armature modifier renormalizes total weight across joint sets regardless
of what's stored, so a second full weight set doesn't distort
deformation"). That's true for the *stored accessor values* (satisfies
`gltf_validator`'s normalization check) but **not** true for actual
runtime deformation: checked directly against `bloodgodtentacle.m2`'s
real exported data, 731/1870 vertices (39%) carry a real 0.5 weight
toward the tag joint (a static, always-identity transform) — meaning
those vertices get measurably pulled halfway back toward their bind pose
every frame, on top of whatever real bone is actually deforming them.
Under plain linear blend skinning this reads as a mild, easy-to-miss
thinning; blended against a *real* rotation via dual-quaternion skinning
(`use_deform_preserve_volume`, experiment 1 above) the same phantom-
identity pull becomes dramatically more visible — explaining both why
plain LBS already looked subtly wrong and why enabling volume
preservation made it look catastrophically wrong, as the exact same root
cause manifesting more severely under a math that's more sensitive to it.

**Fix, consolidated (Luna's direct steer — the first version below was
real but wrong shape, kept as a note on why).** First attempt: a new
`disable_geoset_tag_deform()` that set `Bone.use_deform = False` on every
tag joint, called only from `render_glb.py`. Worked, but duplicated logic
that already existed in `husk_blender_geoset_mask.py`'s own interactive
`main()` (`geoset_stage`, which already deletes tag bones outright — a
strictly stronger fix, no flag needed) and risked exactly the divergence
this project explicitly doesn't want: a corpus preview render seeing a
different scene setup than a real user opening the same file in Blender.
Corrected: `geoset_stage`'s own body (build every mesh's geoset switch,
merge the groups, delete the tag bones) is now one shared function,
`apply_geoset_switches(mesh_objs, armature_obj)` — `main()`'s
`geoset_stage` and `render_glb.py`'s `main()` both call this exact same
function, not a reimplementation each. `render_glb.py` previously never
called this step at all (it only wanted a plain default-state preview,
no interactive dropdown) — which was a second, independent problem this
consolidation also fixes: without it, every geoset variant (every
hairstyle, every tabard state, ...) rendered simultaneously, unfiltered,
for *any* model with real geoset groups, not just tentacles.

Verified through the real pipeline, not just an ad-hoc test script: full
before/after/reverted-experiment set of real `.webm`/`.glb` files kept at
`example_exports/creature/bloodgodtentacle_shear_investigation/` for
reference (`current_no_preserve_volume` = the original bug,
`with_preserve_volume_WORSE` = the falsified experiment,
`tag_joint_deform_disabled_TEST` = the isolated fix tested standalone,
`render_glb_with_fix_REAL_PIPELINE` = the same fix through the actual
`render_glb.py` entry point — confirmed visually consistent with the
isolated test, no needle-thin taper collapse, no detached flap).
Confirmed correct by Luna directly against the real client.

The other flagged repros from this round (`alexstrasza`/`alleria`/
`abominationsmall`/`bloodabomination`) weren't all individually
re-verified this session — `ladyalexstrasa` was checked structurally
(same "perfect bone determinants" shape before the fix) but not
re-rendered after — worth a spot-check once the full re-render lands,
though the fix is general (any geoset-tagged model with real tag-joint
vertex weight benefits) and not specific to tentacle-shaped creatures.

Corpus re-render, stopped mid-run to investigate this, is being
restarted now that both mechanisms have real fixes.

## 2. Renders showing only the background color

**Correction**: the current review pass followed a full corpus
re-render, so the two previously-fixed causes below are ruled out for
these flagged items — they're listed for completeness (still worth
knowing about if an *older* render is ever compared against), not as an
excuse for what's currently flagged.
- Large-scale models exceeding Blender's default camera `clip_end`
  (fixed 2026-08-09, commit `8f35bd5`, `tools/corpus_scan_tasks/
  render_glb.py:384-393` — `clip_end` now derived from real camera
  distance + posed bounding radius).
- Bone-visualization "Icosphere" shapes inflating the auto-framing bbox
  (fixed same day, commit `3a62210`, `disable_bone_shape=True`).
- Zero-vertex VFX-only models (3,807 real files) used to fail export
  entirely; now export with a fallback skeleton/anchor-only glTF and are
  logged as a distinct `skipped_no_geometry` result
  (`render_sample_driver.py:280-282`), not misreported as a blank OK
  render.

**Confirmed, real root cause — a distinct third class, not either of the
above.** Validated end-to-end against a concrete example Luna supplied,
`creature/cloud/cloudswampgas_white_clickable.webp` (re-rendered
2026-08-13 20:45, well after both fixes above):

- Pixel-checked: the entire 640x480 output is a single flat color
  `(58,58,58)` — genuinely 100% background, not just visually similar.
- The `.glb` has real, non-degenerate geometry (24 vertices, one mesh,
  one material) — this is not the zero-vertex case.
- `husk info` on the real source (`/media/luna/data/wow_export/creature/
  cloud/cloudswampgas_white_clickable.m2`) confirms: material 0 has
  `blend_mode=4` (Add), correctly resolved to texture FileDataID 124154.
  That FileDataID has no exact local file in the model's own directory,
  so husk's `--listfile` fallback resolved it — correctly — to
  `black32.blp`, a real, legitimately-named, intentionally-solid-black
  WoW texture that recurs across many unrelated model directories
  (`environments/stars/`, `creature/ghost/`, `creature/tikiman2/`, ...).
  This is not a husk texture-resolution bug: an additive material
  sourced from a genuinely all-black texture contributes zero light by
  definition, regardless of how correctly it's rendered.
- The model's real visible content — per `husk info`, 4 real
  `M2Particle` emitters (bone-anchored, real `blendingType`/`emitterType`/
  `rows`/`columns` data) — is what actually draws the "swamp gas cloud"
  effect in the live client. glTF has no native representation for a
  procedural particle emitter (it's not a geometry format), so husk
  correctly exports only a minimal placement-anchor extras entry
  (id/bone/position) for it, confirmed present in this file's own skin
  extras — that part really is a hard format wall, not a choice. **What
  is not a wall, and was mischaracterized as one in the first pass of
  this file: actually turning those anchors into visible particle
  geometry is a Blender-side reconstruction task that hasn't been built
  yet**, the same category as the billboard-alignment and texture-
  transform-animation companion-script work `tools/
  husk_blender_geoset_mask.py` already does — not a permanent non-goal.
  Corrected directly by Luna. The 24-vertex mesh that *does* export is
  the model's real, separate "clickable" collision/interaction
  placeholder — additively blended off intentionally-black `black32.blp`
  in the real game too, i.e. correctly near-invisible there as well.

**Quantified against the real corpus, not just this one fixture.** A
first attempt used a cheap static heuristic (`tools/corpus_scan_tasks/
particle_only_task.py`: every material additive-family blend + at least
one particle emitter) across the full 130,576-file corpus — 2,283
candidates. Cross-checked against the 368 of those with an existing
rendered still: only **36 (9.8%)** actually render blank. The heuristic
alone is a bad exclude signal — most additive+particle models render
something real and visible (commonly an animated glow/streak texture on
a small quad, unrelated to the particle system itself), which the static
blend-mode check can't distinguish from the genuinely-black-texture case.

**Ground truth instead**: scanned all 6,610 currently-rendered `.webp`
stills directly for pixel flatness (std < 1.0 across the whole image —
the same check that confirmed `cloudswampgas_white_clickable` above).
**104 renders are genuinely blank.** Cross-referencing those 104 against
real `husk info` geometry/particle data: only **36 are cleanly explained**
by the particle-driven-additive-mesh class (same shape as
`cloudswampgas_white_clickable`) — these 36 are now excluded from the
render/review file list, following the exact precedent
`full_corpus_file_list.no_objectcomponents.txt` already set: a new
`corpus_reports/full_corpus_file_list.no_objectcomponents.no_particle_only.txt`
(68,841 → 68,805 lines) excludes them, with the raw list at
`corpus_reports/particle_only_exclude_list.txt` for reference. **Marked
as a known gap** (real, understood, deferred to future Blender-side
particle reconstruction work — not urgent to fix, safe to exclude from
review in the meantime), not a bug.

**The other 68 blank renders are a real, separate, more concerning
finding — not excluded, not explained by particles at all.** Several have
*substantial* real geometry that should clearly be visible:
- `world/expansion01/doodads/shattrath/passivedoodads/giantdoodads/
  shattrath_scryerhedges.webp` — **16,632 vertices**, `blend_mode=1`
  (AlphaKey, an ordinary alpha-tested surface, not additive), 0 particles
  — renders fully blank.
- `world/expansion01/doodads/generic/bloodelf/planetarium/
  be_planetarium_active.webp` — 4,654 vertices, mixed
  opaque/additive materials (`blend_modes=[0,4,0,4,0,0]`), 2 particles.
- `creature/deathwingcorruptedjaw/deathwingcorruptedjaw.webp` — 2,119
  vertices, `blend_mode=4` (Add) but 0 particles at all — the additive-
  texture-source-is-black theory doesn't even apply here structurally.
- Several `spells/*` and `world/*` doodads with small (3-25 vertex) quads
  across every blend mode including plain `blend_mode=0`/`1`/`2`
  (opaque/alpha-key/alpha-blend — not additive), some with real particle
  counts, some without.

Full list of all 104 blank renders (paths, vertex counts, blend modes,
particle counts, and which class each falls into) is at
`corpus_reports/blank_renders_classified.csv` — filter `class ==
"unexplained"` for the 68 that still need investigation.

**Follow-up (2026-08-14): spot-checked 6 of the 68 individually, real
root causes found for all 6 — not one bundled mechanism, but not
mysterious either. None required a code fix; each is either already-
correct behavior or already-understood/tracked elsewhere.** Re-exported
each with `--listfile`/`--listfile-root` (the `blank_renders_classified.csv`
scan predates that flag existing in the render driver by a few hours —
worth knowing if this file is ever regenerated and the counts don't
match) and, where relevant, re-rendered through the real
`render_glb.py` pipeline:

- **`shattrath_scryerhedges.webp`** (16,632 vertices, `blend_mode=1`) —
  its one texture (FileDataID 192069) has no local file in the model's
  own directory at all; `--listfile` resolves it to a real file living
  under a completely different path
  (`world/expansion01/doodads/generic/bloodelf/hedge/silvermoon_hedge01.blp`,
  confirmed present locally, a real 256x256 DXT texture, 85% non-transparent
  pixels — not itself blank). Re-exporting with `--listfile` and
  re-rendering no longer produces a 100%-flat image (std 0.94, still just
  under this scan's <1.0 threshold, but visibly non-blank — a handful of
  small green foliage-cluster shapes are now visible). Root cause for the
  *remaining* sparseness: this file lives under `giantdoodads/` for a
  reason — its real bounding sphere radius is 123.9 (a ~250-yard
  structure, city-block scale), and the corpus preview's generic
  auto-framing camera zooms out to fit the *entire* bounding sphere in
  one 640x480 frame regardless of how sparse the actual foliage geometry
  is within it — individual leaf-cluster shapes that would fill the
  screen at an in-game viewing distance shrink to a handful of ~20px
  blobs. Real, but a QA-preview framing tradeoff for unusually large
  sparse "giant doodad"-class assets, not an export/data bug — no
  obviously-correct fix without changing framing behavior for every other
  model too, so left as a documented limitation rather than "fixed."
- **`deathwingcorruptedjaw.webp`** (2,119 vertices, `blend_mode=4`/Add, 0
  particles) — its one texture (FileDataID 130930) has no local file in
  its own directory either; `--listfile` resolves it to
  `interface/characterframe/ui-party-background.blp`, decoded directly:
  32x32, mean RGBA `(0,0,0,255)` — genuinely, fully black, not a
  resolution miss. This is the *exact* mechanism already confirmed for
  `cloudswampgas_white_clickable` above (additive blend of a real,
  correctly-resolved, legitimately-black texture contributes zero light
  by definition) — just without a particle emitter, so
  `particle_only_task.py`'s exclude-candidate heuristic
  (`blend_mode >= 3 AND particle_count > 0`) doesn't catch it. **Real,
  scoped follow-up, not done this session**: the heuristic's particle
  requirement was a proxy for "this model's only real visible content is
  something husk can't bake into geometry," but the actual root cause
  (additive blend + black source texture) doesn't need particles at all
  — relaxing the candidate scan to check resolved-texture darkness
  directly (export via husk, decode the embedded base-color PNG, mean
  brightness near 0) instead of proxying through particle-presence would
  catch this class too, without the false-positive risk plain blend-mode
  checking alone has (documented above: 90%+ of additive+particle
  candidates aren't actually blank).
- **`ui_alliance_lowres.webp`**/**`ui_horde_lowres.webp`**/
  **`ui_pandarencharacterselect_lowres.webp`** (4 vertices each) — not
  visible meshes at all: real attachment points (Shield/HandRight), 3
  lights, and an embedded camera, `blend_mode=0`+unlit — these are
  character-select-screen **rig/stage markers**: their real visual
  content in the live client is whatever character model gets attached
  at their named attachment points and lit by their named lights, viewed
  through their own embedded camera, none of which this preview pipeline
  reconstructs (it renders exactly one model's own mesh, doesn't attach
  others or use embedded M2Camera data). Correctly near-empty for what
  they are — not a bug, a different asset category than a normal
  creature/doodad model.
- **`minimaparrow.webp`**/**`minimapcompassring.webp`** — real bounding
  sphere radius **0.0185** (`minimaparrow`, confirmed via `husk info`) —
  genuinely UI-icon-scale geometry (a fraction of one in-game yard),
  designed for a 2D screen-space UI camera entirely outside this preview
  pipeline's generic world-object framing. Same category as the rig
  markers above: correctly near-invisible for what it structurally is.
- **`invisiblestalkernoname.webp`**/`creature/greysquare/
  10xp_greysquare01.webp` — real, named, intentionally-invisible/debug
  game entities (`invisiblestalker` is a genuine, widely-reused
  scripted-invisible-NPC model; `greysquare` reads as a GM/debug
  placeholder marker) — correctly renders as nothing, not a bug in husk
  or the preview pipeline.

**Net effect on the "genuinely unexplained" framing**: the 68-count and
the "likely several distinct causes bundled together" read from the
first pass both undersold how much of this list is already-correct
behavior once actually looked at — of the 6 spot-checked, 0 were husk
export bugs; 5 are correctly-blank-by-design (rig markers, UI-scale
icons, debug/invisible entities), and 1 (`shattrath_scryerhedges`) was a
real texture-resolution miss now fixed by `--listfile`, revealing a
separate, understood-but-unfixed framing limitation underneath. Worth a
similar spot-check pass over the remaining ~62 before assuming they're a
genuinely-open rendering bug bucket — plausible many more are the same
already-understood categories (especially the small 3-25-vertex
`spells/`/`world/` doodad quads, structurally similar to the rig-marker/
UI-icon cases above) rather than fresh bugs. Downgraded from "top
priority" accordingly — not urgent, not proven to hide a real bug at
volume.

**Known limitation of this pass**: only `.webp` stills were checked
(6,610 of them). The ~1,915 particle-only-candidate files that render as
`.webm` (animated clips) were not checked — a single-frame flatness check
doesn't directly apply to video, and a real check would need multi-frame
sampling. Left for a follow-up, not blocking the 36-file exclude list
above (which is scoped only to what was actually confirmed).

## 3. Missing / wrong textures

**Another real repro of the hardcoded-slot gap, same session**:
`creature/drogbarchieftain/drogbarchieftain.webm` ("partial missing
texture") — `husk info` shows the same shape as the dragonspawn case
above: `textureType` 11 (`monster_1`) and 13 (`monster_3`) are both
hardcoded/customization-driven slots husk can't resolve from in-file data
alone. Same documented, permanent (without DB2/CASC access) limitation —
not investigated further, just another concrete data point for how often
this shows up in the flagged set.

**Texture-resolution logic itself has already had four real rounds of
fixes** (`src/export_materials.cpp`'s candidate filtering/ranking —
`candidateAllowedForType`/`filterCandidatesForType`/
`orderCandidatesForDefault`; see `CLAUDE_HISTORY.md` for the specific
`bloodelffemale_hd` tiny-decal-vs-atlas, `body_jewelry`/`jewelry_color`
category-assignment, and material-dedup fixes). If today's sample repeats
one of those *exact* symptoms, check the render predates the fix rather
than assuming a regression.

**Two real gaps are already tracked, not new** — likely explain a chunk
of "missing/wrong" complaints without being fresh findings:
- **`TODO/MULTI_TEXTURE_LAYER_TODO.md`** (open, actively worked — most
  recently touched in today's own commit history): ~79% of real `.skin`
  files have `textureCount > 1` (a second/third combined texture layer);
  husk exports the data as extras but neither husk nor `render_glb.py`
  blend it into the render. Documented symptom: "flat plastic" armor/
  weapons missing a detail map, tint overlay, or shine layer.
- **`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`**: character models needing
  live DB2/CASC-driven layer compositing husk deliberately doesn't do
  (non-goal, not a bug) — an uncomposited base/overlay skin layer here is
  a documented wall, not something to re-flag as fixable.

**Genuinely new**: `orderCandidatesForDefault`
(`src/export_materials.cpp`, ~line 528-548) only has real tiebreak logic
(pixel-area, then `skin_color`-category preference) for the specific
skin/skin_extra/char_jewelry cases prior sessions had real evidence for.
Outside those, ties fall through to `scanFuzzyTexturePoolForBasename`'s
plain alphabetical order — two same-category, same-resolution, non-
skin_color candidates (e.g. two color variants of one accessory) have no
principled tiebreak. Could plausibly produce "completely wrong palette/
color variant" for non-character categories. Not previously flagged as
its own item.

**Concrete real example of a related but distinct symptom, found this
session, not yet investigated**: `creature/dragonspawn/
dragonspawntwilightoverlord.webm` and `creature/dragonspawn2caster/
dragonspawn2caster.webm` — described directly as textures that "switch
per face," not a single consistently-wrong color pick. `husk info` on
the twilightoverlord fixture shows exactly the shape that would produce
this: `textureType` 11 (`monster_1`) and 12 (`monster_2`) are both
hardcoded/customization-driven slots (per this project's own documented
`textureType != 0` gap — no in-file data to resolve which of several
same-basename recolor candidates is correct), and this specific model
has six real local candidates for those slots
(`dragonspawntwilightoverlord_{red,green,purple}{1,2}.blp`). Two
*different* texture types independently defaulting to two *different*
colors (e.g. monster_1 -> red, monster_2 -> green) would look exactly
like "switches per face" if those two types are used on different
batches/faces of the same creature — plausible, but not confirmed; needs
real per-batch/per-material inspection (which batch uses which
`textureType`, and whether it's genuinely two independently-resolved
slots vs. the same slot resolving inconsistently across batches, which
would be a different and more concerning bug) before concluding this is
"working as expected given no DB2 data" vs. a real resolution bug. Not
investigated further this session — flagged with concrete repros only.

**Worth re-checking, not re-investigating from scratch**: `bloodelffemale_hd.m2`'s
three `textureType == 0` FileDataID slots (3536810/4530998/5210137) with
no local file in the real export directory, previously flagged as "genuinely
still open... whether specific to this local export or a wider gap is
unconfirmed" (`CLAUDE_HISTORY.md`). Worth checking whether it's systemic
across the current corpus now that `--listfile` resolution exists.

## 4. Alpha-channel issues

**Additive blend modes (3/4) already fixed** — `render_glb.py`'s
`fix_additive_materials()` (~line 103-204) rebuilds real Transparent+
Emission shading post-import; verified against real fixtures.

**Genuinely new — two real gaps**:
- **Mod/Mod2x (multiply) blend modes 5/6 are explicitly unimplemented**,
  by the render script's own comment (`render_glb.py:125-127`): "a real,
  separate gap, not attempted here... multiply compositing needs a
  different node shape." `alphaModeForBlend` (`src/export_materials.cpp:94`)
  collapses every WoW blend mode ≥2 to glTF `BLEND` at the husk level (by
  design — core glTF has only 3 alpha modes, real `blend_mode` is exposed
  as extras for exactly this kind of Blender-side reconstruction), but
  only modes 3/4 currently get rebuilt into correct shading; 5/6 fall
  through to plain alpha-`BLEND`, a plausible wrong answer (could read as
  either wrongly-transparent or wrongly-opaque depending on the base
  texture's luminance) for whatever real material uses them.
- ~~**`alphaCutoff` is never set explicitly**~~ — **checked and fixed
  2026-08-14.** The real client's own threshold is `0.501960814`
  (`reference/wow.export/src/js/3D/renderers/M2RendererGL.js`:
  `u_alpha_test`, i.e. 128/255) — cross-checked against every 8-bit alpha
  value: it's mathematically indistinguishable from glTF's implicit 0.5
  default for byte-quantized textures (both land strictly between the
  127/255 and 128/255 texel values, so no real texture's rounding could
  ever tell them apart). husk now sets `alphaCutoff` explicitly to
  `128.0/255.0` on every `MASK`-mode material (`gltf_mesh.cpp`'s
  `emitMaterial`) rather than relying on that coincidence — real, not a
  behavior change for any already-rendered output, just removes the
  question. Two new tests (`tests/test_gltf_mesh.cpp`). Full suite green,
  634/634.

**Unverified, not confirmed broken**: `apply_tint_fade_animation`
(`tools/husk_blender_geoset_mask.py`) — structurally sound, doesn't crash
against real fixtures with genuine tint/fade data, but explicitly not
ground-truthed against real per-frame alpha values the way the texture-
transform animation curve was. A material with a real fade-in/out could
plausibly show wrong transparency at some frame; open, not newly found.

## 5. Billboard alignment

**Not untouched territory — a real, fairly complete system already
exists**, contrary to how "billboard issues" might read as a fresh gap:
export side (`src/m2_header.cpp:40-44`, `src/gltf_skeleton.cpp:137-143`)
plus a real Blender-side reconstruction
(`tools/husk_blender_geoset_mask.py:200-280` — `find_billboard_bones`,
`_apply_billboard_frame`, a live `depsgraph_update_post` handler),
verified numerically against two real fixtures (a spherical lamp glow,
a mixed spherical/cylindrical_lock spell effect), including a real fixed
parallax-skew bug from an earlier native-constraint prototype.

**The one already-documented open item — most likely explanation for
fresh billboard complaints**: the script's own doc comment
(`husk_blender_geoset_mask.py` ~line 264-276) states plainly this math
has **never been ground-truthed against real in-game billboard
behavior** — verified only by checking the computed frame matches what
the math says it should, not by comparing to an actual WoW client
frame-by-frame. If the review is seeing billboard misalignment, this is
almost certainly it — a known, named gap, not a fresh discovery. Closing
it needs a human side-by-side against the real client, not more code
archaeology.

## 6. Animated texture-transform scroll runs in the wrong V direction — confirmed, trivial fix

**Confirmed root cause, not a hypothesis.** Repro:
`world/expansion02/doodads/boreantundra/magnatauritems/
borean_redplant_burningpile_01.webm` — a burning-pile doodad whose flame
texture scrolls the wrong way ("burning upside down").

Real exported data (`husk export` against the real source `.m2`,
confirmed by hand): material `mat1_tex1_fdid195953`'s
`texture_transform_animation.translation` curve goes from `(0,0,0)` at
`t=0` to `(0, +1.0, 0)` at `t=0.833s` — a plain, real, positive V-axis
scroll, looping every 0.833s. Nothing wrong with husk's own export here.

**The bug is on the Blender-reconstruction side**:
`tools/husk_blender_geoset_mask.py`'s `_update_texture_transform_animations`
(~line 1276-1279) takes that curve's `y` value and assigns it directly to
the Mapping node's `Location` V component with no sign correction:
```python
if translation:
    x, y, _z = _eval_vec3_curve(translation[0]["keyframes"], t)
    mapping.inputs["Location"].default_value[0] = x
    mapping.inputs["Location"].default_value[1] = y
```
But **this same file already has an established, explicit convention for
exactly this axis** (the texture-layout overlay code, ~line 689-692):
"WoW atlas Y grows downward (top-down pixel convention); Blender UV V
grows upward -- flip." That flip (`v_blender = 1 - v_wow`) is applied
there for absolute placement rects — but `_update_texture_transform_animations`
never applies the equivalent correction for a *scrolling delta*. Since
`v_blender = 1 - v_wow`, differentiating gives `d(v_blender)/dt =
-d(v_wow)/dt` — a positive WoW V-scroll must become a **negative**
Blender `Location` delta, not a same-sign one. The current code passes
the raw sign straight through, so every animated V-axis texture scroll
in this project runs backwards relative to intended.

**Fix**: negate `y` before assigning to `Location[1]` in
`_update_texture_transform_animations` (~line 1279):
`mapping.inputs["Location"].default_value[1] = -y`. Trivial, one line,
Python-only (no rebuild), and this is the same kind of asset that made
the original texture-transform-animation feature real in the first
place — worth re-running that feature's own verification (a Mapping
node reading correctly at a known frame) with the sign fix applied
before calling it closed, same discipline the rest of this project's
animation work already follows. **Applied 2026-08-14.**

**Why `be_fountain01_base.webm` looked fine despite the same bug being
present the whole time** — investigated directly, not assumed. Its one
genuinely-animated `texture_transform_animation.translation` curve
(material `mat12_tex12_fdid192043`) goes from `(0,0,0)` to
`(2.999, 0.0, 386.055)` over 5s — **the Y (V) component is `0.0` for the
entire curve**; all real motion is on X (U) and an unused/ignored Z. The
V-flip bug only affects the V axis — U has no WoW/Blender convention
mismatch (only V does, per the same overlay-code precedent above), so a
model whose animated scroll happens to be purely horizontal was never
touched by this bug at all. The fountain isn't evidence the code was
right; it's evidence the bug is axis-specific and this file's data simply
never exercises the broken axis. The fire (`borean_redplant_
burningpile_01`), by contrast, scrolls entirely on Y — exactly the axis
that was broken. Confirms the fix above is both correct and sufficient;
no further per-file investigation needed.

**Scope note**: only the V (Y) component is affected — U (X) has no
axis-convention mismatch between WoW and Blender, so horizontal scrolls
were never wrong. Any doodad/spell/creature with a real animated V-axis
texture scroll (fire, water, lava, energy beams, ...) is a plausible hit
— likely a meaningful slice of "wrong/weird texture motion" complaints
beyond just this one fixture.

## 7. Black silhouette / unlit-looking materials — real category, confirmed, root cause still open

Repro: `creature/demolishercannonball/demolishercannonball.webp` — reads
as a near-black hole in the background, not a missing/wrong texture
(there's real per-pixel variance, 778 unique colors, not a flat fill).

Confirmed via this project's own existing, already-calibrated
`tools/categorize_flagged_renders.py` (`categorize_image()`, calibrated
in an earlier session against a known real bug case,
`mace_1h_raidmidnight_d_01`) — this file categorizes as
`black_silhouette_or_unlit`, not a false positive.

Checked the obvious explanations and ruled them out: the real texture
(`demolishercannonball.blp`, decoded directly) is a genuinely dark
iron/bronze color (mean RGB ~37,31,18 — dark, but not black, with real
internal variance, std 11.4) — not missing, not corrupt. `husk info`
shows `material 0: flags=0x0` — the unlit bit (`kMaterialUnlitFlag`,
`0x01`) is **not** set, so this material is lit, ordinary PBR shading
applies, not the "unlit material has no Principled BSDF" importer quirk
this project has hit before elsewhere. So it's a real dark texture,
correctly lit-shaded, that still renders too dark to read as anything
but a silhouette — root cause not yet identified (possibly render
lighting intensity too low for genuinely low-albedo materials generally,
possibly something specific to this file/material). **Not fixed this
session** — flagged with a fresh, confirmed repro so it's not lost;
this project's earlier categorize_flagged_renders.py work already
implies there may be a whole bucket of these in the corpus (that tool
exists specifically to sort a large flagged pile into this category
alongside missing-texture, at scale) — worth running it over the current
review's flagged set to size the bucket before investigating further.

## 8. Other flagged repros from this review round, not yet investigated

Caught live during this session's review pass, logged with a repro path
and the reporter's own description only — no investigation done yet,
listed here so they aren't lost:

- `creature/druidtreeharanir/druidtreeharanir.webm` — "the body having...
  a disco? (animation / visibility issue)" — likely a flicker/strobe.
  Quick check: 82 sequences, `global_flags` includes
  `chunked_anim_files`, no lights, nothing else obviously implicated from
  `husk info` alone. Could be geoset-tag-joint interaction, a duplicate-
  keyframe timing issue, or something in the animated-tint/fade path
  (`apply_tint_fade_animation`, already flagged elsewhere in this file as
  unverified against real data) — genuinely unknown, needs a real look.
- `creature/earthspiritsmall/earthspiritsmalllesser.webm` — a fully
  **white** silhouette, distinct from the usual missing-texture look.
  `husk info` shows no local `.blp` file at all in this model's directory
  for any of its 4 real texture FileDataIDs (147183/538471/193381/144322)
  -- a genuine total texture-resolution miss, not a partial one. Also
  `material 0: flags=0x1` (unlit). Plausible combination: missing texture
  (Blender's own flat-fallback-material case, already characterized by
  `categorize_flagged_renders.py`'s `missing_texture_or_shader` category)
  plus the already-known "unlit material gets no Principled BSDF on
  import" quirk (found earlier this project for additive materials,
  `render_glb.py`'s `fix_additive_materials` work) -- together plausibly
  explain white instead of the usual off-white/gray fallback look. Not
  confirmed; two already-understood mechanisms combining is a hypothesis,
  not a verified explanation.

## Suggested priority

1. ~~**Texture-transform V-scroll direction (§6)**~~ — **fixed 2026-08-14.**
2. ~~**Rotation shear (§1, both mechanisms)**~~ — hemisphere continuity
   **fixed 2026-08-14**; the second, unrelated cause (geoset-tag joints
   pulling weighted vertices toward bind pose) **also fixed 2026-08-14**,
   confirmed by Luna against the real client.
3. **The 68 "unexplained" blank renders (§2)** — downgraded 2026-08-14:
   spot-checked 6, found 0 husk bugs (5 correctly-blank-by-design rig-
   marker/UI-icon/debug-entity cases, 1 real `--listfile` fix revealing
   an already-understood giant-doodad framing limitation, not a data
   bug). Real next step if picked back up: extend
   `particle_only_task.py`'s exclude heuristic to catch the no-particle
   black-additive-texture class (`deathwingcorruptedjaw`'s shape) via
   actual resolved-texture-darkness checking rather than a particle-count
   proxy, then spot-check the remaining ~62 before assuming any of them
   are fresh bugs.
4. ~~**Alpha cutoff (§4)**~~ — **fixed 2026-08-14.** **Mod/Mod2x (§4)**
   still open — needs a real multiply-blend shader shape in
   `render_glb.py` (no `Add Shader` equivalent for multiply), no demonstrated
   real-corpus repro driving it yet.
5. **Billboard ground-truth pass (§5)** — needs Luna's own real-client
   comparison, same as the earlier billboard/geoset-mask verification
   pattern in this project's history; not something to chase blind in
   Blender alone a second time.
6. **Ambiguous-pool tiebreak (§3)** — lower severity, needs a concrete
   example pulled from the current `renders_full_review.jsonl` flagged
   set before there's anything specific to fix.
7. **Particle-effect Blender-rendering task (§2)** — the 36-file known
   gap is already excluded from review, so a *real, textured, simulated*
   particle system is still genuinely optional scope expansion, not
   urgent. **A smaller first step landed 2026-08-14**:
   `tools/husk_blender_geoset_mask.py`'s new `apply_emitter_markers`
   places a small, distinctly-shaped/colored placement marker at every
   real `ribbon_emitters`/`particle_emitters` anchor, bone-following
   through animation — not a simulation (no texture/blend/curve data, see
   its own doc comment), but it closes the "100% invisible, nothing there
   at all" gap `cloudswampgas_white_clickable` above illustrated, with an
   honest placeholder rather than nothing. The real simulation
   (`render_glb.py` integration, real per-emitter texture/blend/curve
   data via `husk dump-chunks`) is still open, same shape as before.
