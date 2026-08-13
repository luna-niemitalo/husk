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

## 3. Making these curves actually animate in Blender post-import

The biggest chunk of real work. husk's own exported extras
(`texture_transform`, `tint_animation`, `fade_animation`) are static data
today — a Blender companion script needs to turn them into real driven
values: either baked Shader Editor node animation (F-curves on a Mapping
node's rotation/location for `texture_transform`, on a Mix/Emission node's
color or factor for tint/fade) or drivers reading a custom property this
script sets up. Needs real investigation into which of husk's curve shapes
(constant per-sequence value vs. genuinely keyframed within a sequence,
`gltf::Material::AnimatedColorCurve`/`AnimatedScalarCurve`'s own real
shape) map cleanly to Blender F-curves versus needing a driver/handler
approach (same kind of choice this session's billboard-alignment work
already had to make between a native constraint and a direct
per-frame computation, and landed on "direct computation + a
`depsgraph_update_post` handler for the interactive case" for reasons
that likely generalize here too).

**Real robustness requirement surfaced by this scope, prompted directly**:
`tools/husk_blender_geoset_mask.py` already runs several independent
stages in one `main()` (geoset switch, texture-layout overlay, billboard
alignment, and now potentially texture-effect animation) — today, if one
stage throws, the whole script dies and every later stage silently never
runs. This needs fixing regardless of the animated-effects work
specifically, but is a hard prerequisite before adding a 4th/5th stage:
wrap each stage in its own try/except, print a loud, specific failure
(model name, stage name, real exception text) on failure, and continue to
the next stage rather than aborting the whole run. Small, mechanical,
should happen first.

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
