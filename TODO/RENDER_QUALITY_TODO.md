# TODO: render-quality findings from corpus review (rotation, textures, alpha, billboards)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Background

Sourced from a corpus-triage review pass over `corpus_reports/renders_full`'s
rendered preview clips/stills (`tools/live_gallery/server.py`'s `/review`
page). A skeletal-animation rotation "shear" and an animated-texture
V-scroll direction bug, both originally tracked here, are now fully fixed
(git history has the investigations) — remaining sections below are what's
still open.

## 1. Renders showing only the background color

**Confirmed, real root cause.** Validated end-to-end against a concrete example Luna supplied,
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

## 2. Missing / wrong textures

**Another real repro of the hardcoded-slot gap, same session**:
`creature/drogbarchieftain/drogbarchieftain.webm` ("partial missing
texture") — `husk info` shows the same shape as the dragonspawn case
above: `textureType` 11 (`monster_1`) and 13 (`monster_3`) are both
hardcoded/customization-driven slots husk can't resolve from in-file data
alone. Same documented, permanent (without DB2/CASC access) limitation —
not investigated further, just another concrete data point for how often
this shows up in the flagged set.

**Texture-resolution logic itself has already had four real rounds of
fixes** (`src/export_texture_resolution.cpp`'s candidate filtering/ranking —
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
(`src/export_texture_resolution.cpp`, moved from `export_materials.cpp`
2026-08-14, see `TODO/CLEANUP_TODO.md`) only has real tiebreak logic
(pixel-area, then `skin_color`-category preference) for the specific
skin/skin_extra/char_jewelry cases prior sessions had real evidence for.
Outside those, ties fall through to `scanFuzzyTexturePoolForBasename`'s
plain alphabetical order — two same-category, same-resolution, non-
skin_color candidates (e.g. two color variants of one accessory) have no
principled tiebreak. Could plausibly produce "completely wrong palette/
color variant" for non-character categories. Not previously flagged as
its own item.

**Checked and closed (2026-08-14)**: `creature/dragonspawn/
dragonspawntwilightoverlord.webm`/`creature/dragonspawn2caster/
dragonspawn2caster.webm` were reported as textures that "switch per
face." Investigated directly (re-exported with `--textures`, inspected
husk's own per-material resolution notes, rendered both): on both files,
every ambiguous hardcoded slot (`monster_1`/`monster_2`) independently
resolves to the *identical* default candidate, and a rendered frame shows
a consistent color/pattern across the whole model, not a mismatch. The
underlying structural gap above (alphabetical fallback isn't a
principled tiebreak) is still real, but turns out self-consistent in
practice — every ambiguous slot on one model shares the same candidate
pool and the same deterministic tiebreak, so they agree with each other.
Whatever produced the original report was either a stale pre-fix render
or the already-tracked `MULTI_TEXTURE_LAYER_TODO.md` gap, not a fresh
resolution-inconsistency bug — nothing to fix here without a fresher
repro.

**Worth re-checking, not re-investigating from scratch**: `bloodelffemale_hd.m2`'s
three `textureType == 0` FileDataID slots (3536810/4530998/5210137) with
no local file in the real export directory, previously flagged as "genuinely
still open... whether specific to this local export or a wider gap is
unconfirmed" (`CLAUDE_HISTORY.md`). Worth checking whether it's systemic
across the current corpus now that `--listfile` resolution exists.

## 3. Alpha-channel issues

**Additive blend modes (3/4) already fixed** — `render_glb.py`'s
`fix_additive_materials()` (~line 103-204) rebuilds real Transparent+
Emission shading post-import; verified against real fixtures.

**Genuinely new — two real gaps**:
- **Mod/Mod2x (multiply) blend modes 5/6 are explicitly unimplemented**,
  by the render script's own comment (`render_glb.py:125-127`): "a real,
  separate gap, not attempted here... multiply compositing needs a
  different node shape." `alphaModeForBlend` (`src/export_texture_resolution.cpp`)
  collapses every WoW blend mode ≥2 to glTF `BLEND` at the husk level (by
  design — core glTF has only 3 alpha modes, real `blend_mode` is exposed
  as extras for exactly this kind of Blender-side reconstruction), but
  only modes 3/4 currently get rebuilt into correct shading; 5/6 fall
  through to plain alpha-`BLEND`, a plausible wrong answer (could read as
  either wrongly-transparent or wrongly-opaque depending on the base
  texture's luminance) for whatever real material uses them.
**Unverified, not confirmed broken**: `apply_tint_fade_animation`
(`tools/husk_blender_geoset_mask.py`) — structurally sound, doesn't crash
against real fixtures with genuine tint/fade data, but explicitly not
ground-truthed against real per-frame alpha values the way the texture-
transform animation curve was. A material with a real fade-in/out could
plausibly show wrong transparency at some frame; open, not newly found.

## 4. Billboard alignment

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

## 5. Black silhouette / unlit-looking materials — real category, confirmed, root cause still open

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

## 6. Other flagged repros from this review round, not yet investigated

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

1. **The 68 "unexplained" blank renders (§1)** — 6 spot-checked, 0 husk
   bugs found (5 correctly-blank-by-design, 1 a `--listfile` fix revealing
   an already-understood framing limitation). Real next step: extend
   `particle_only_task.py`'s exclude heuristic to catch the no-particle
   black-additive-texture class (`deathwingcorruptedjaw`'s shape) via
   actual resolved-texture-darkness checking rather than a particle-count
   proxy, then spot-check the remaining ~62 before assuming any of them
   are fresh bugs.
2. **Mod/Mod2x (§3)** — needs a real multiply-blend shader shape in
   `render_glb.py` (no `Add Shader` equivalent for multiply), no
   demonstrated real-corpus repro driving it yet.
3. **Billboard ground-truth pass (§4)** — needs Luna's own real-client
   comparison, same as the earlier billboard/geoset-mask verification
   pattern in this project's history; not something to chase blind in
   Blender alone a second time.
4. **Particle-effect Blender-rendering task (§1)** — the 36-file known
   gap is already excluded from review, so a *real, textured, simulated*
   particle system is still genuinely optional scope expansion, not
   urgent. `tools/husk_blender_geoset_mask.py`'s `apply_emitter_markers`
   (2026-08-14) is a smaller first step — a placement marker at every
   real `ribbon_emitters`/`particle_emitters` anchor, not a simulation
   (no texture/blend/curve data, see its own doc comment) — closing the
   "100% invisible, nothing there at all" gap with an honest placeholder.
   The real simulation (`render_glb.py` integration, real per-emitter
   texture/blend/curve data via `husk dump-chunks`) is still open.
