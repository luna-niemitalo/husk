# TODO: implement real Mod/Mod2x (multiply) blend compositing

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file. Full investigation narrative (including two dead
ends worth knowing about but not repeating) lives in `CLAUDE_HISTORY.md`'s
2026-08-14/15 session entry, not here.

## Background

WoW blend modes 5 (Mod) and 6 (Mod2x) are real multiply compositing —
`dest = dest * src` (Mod2x: `dest = dest * src * 2`, clamped) — with no
core-glTF equivalent. husk exports `blend_mode` as material extras
whenever it's `> 2` (`alphaModeForBlend`, `src/export_texture_resolution.
cpp:230-236`) and always sets glTF `AlphaMode::Blend` for these materials
(never `Opaque`) — Blender's glTF importer accordingly always imports them
with `blend_method = 'BLEND'`, giving them Blender's own default
alpha-over compositing, a wrong answer for either blend mode (same
category of gap `fix_additive_materials()` in `tools/corpus_scan_tasks/
render_glb.py` already closed for modes 3/4 — Mod/Mod2x is `render_glb.py`'s
own explicitly-flagged remaining gap, see its module docstring/comments
near `fix_additive_materials`).

Materials needing this are identifiable post-import exactly like additive
materials already are: `mat.get("blend_mode") in (5, 6)` (`6` = Mod2x,
needs the extra ×2 clamp step; `5` = Mod, doesn't).

## Two things confirmed this session, real and load-bearing

1. **No material-shader-graph trick exists.** Additive compositing
   (`dest + src*alpha`) is expressible as a pure shader output
   (`Transparent BSDF` + `Add Shader` + `Emission`) because the shader
   only ever needs to *contribute* light. Multiply needs the shader to
   *read* the destination color, and neither Cycles nor EEVEE Next expose
   a framebuffer-read node to a material shader graph. Don't look for one.
2. **The Compositor can do it.** It runs on finished per-material renders,
   which is a real framebuffer read. Cryptomatte's per-material matte,
   combined with ordinary compositor math (`Add`/`Multiply` nodes) on a
   *single* render, reconstructs a corrected image without needing a
   second "background-only" render — verified interactively in Blender by
   Luna directly (a real, working node graph: two `Cryptomatte` nodes with
   complementary `matte_id` lists → `Add` → `Multiply` → output). This is
   the technique to build on.

## Two real API details, confirmed, worth getting right the first time

- **`Cryptomatte.matte_id` takes a plain comma-separated list of material
  names, no quotes** (`matte_id = "mult_mat"`, or `"nameA,nameB"` for
  several). A quoted string produces a hash matching no real material —
  `entries` still populates, `Matte` output is silently zero everywhere,
  no error raised.
- **The compositor tree is `scene.compositing_node_group`, not
  `scene.node_tree`, in Blender 5.x** — a real `NodeGroup` with its own
  `interface` (add output sockets via `tree.interface.new_socket(...)`,
  terminal node is `NodeGroupOutput`, not `CompositorNodeComposite` —
  that node type doesn't exist in 5.1.1). Node socket string lookup
  (`node.inputs["X"]`) indexes by display `.name`, not `.identifier` —
  several nodes have multiple same-named sockets (`ShaderNodeMix`'s
  `A_Float`/`A_Vector`/`A_Color` all show as `"A"`); always resolve by
  iterating `.identifier` explicitly.

## One real, confirmed Blender/EEVEE Next bug — root-caused precisely, has a real fix

EEVEE Next's Cryptomatte `Matte` output is **pure black for any material
with `Material.surface_render_method == 'BLENDED'`** — not `blend_method
== 'BLEND'` itself, which was the first, wrong hypothesis (a real
false-negative caused by a Blender API side effect, see below). Confirmed
with a direct visual test: same scene (red plane over a blue cube,
`matte_id="front_mat"`), rendered once per engine — Cycles produces a
clean white cutout exactly where the plane covers, black elsewhere
(correct, and correct regardless of `surface_render_method`); EEVEE Next
produces a fully black image, center pixel `(0.0, 0.0, 0.0, 1.0)` where
it should read white, **only** when `surface_render_method='BLENDED'`.
**With `surface_render_method` explicitly forced to `'DITHERED'`, EEVEE
produces the identical correct white cutout as Cycles** — a real, working
fix, not a workaround-with-caveats.

**The trap that produced the first, wrong "unconditionally broken for any
BLEND-mode material" conclusion**: setting `Material.blend_method =
'BLEND'` has an undocumented side effect — it silently flips
`surface_render_method` from Blender's own default (`'DITHERED'`) to
`'BLENDED'`. Every earlier test in this investigation set `blend_method`
and never re-asserted `surface_render_method` afterward, so every one of
them was silently exercising the broken `BLENDED` path while believing it
was testing the (working) default. Confirmed directly:
```
fresh material default:              DITHERED
after m.blend_method = 'BLEND':      BLENDED   <- silent, unrequested
after explicit m.surface_render_method = 'DITHERED':  DITHERED
```
Caught on Luna's own direct interactive test (a real node graph in
Blender's own Compositor, `Render Method: Dithered` explicitly visible in
the Material Properties panel) producing a correct result where this
investigation's own scripted tests had all failed.

**Practical takeaway for implementation**: post-import, after setting
`blend_method` on any Mod/Mod2x material (or leaving Blender's glTF
importer to set it, which imports glTF `Blend` alpha mode the same way),
**explicitly re-assert `surface_render_method = 'DITHERED'`** — never
assume the default holds once `blend_method` has been touched. This
removes the EEVEE blocker entirely; Cycles is not required for this
feature after all. `render_glb.py`'s existing `fix_additive_materials()`
explicitly sets `BLENDED` for the *additive* (modes 3/4) case it already
handles — that one is unaffected by this bug (a real shader-graph
rebuild, not a Cryptomatte matte) and its own comment about avoiding
`DITHERED`'s "noisy hash" there still stands on its own merits; don't
conflate the two cases when implementing this.

## Where the implementation goes

**`tools/husk_blender_geoset_mask.py`, not `render_glb.py`** — every other
shared post-import fixup that both the interactive script and
`render_glb.py` need lives there (`apply_geoset_switches`,
`apply_billboard_alignment`, `apply_texture_transform_animation`,
`apply_tint_fade_animation`), imported and called from `render_glb.py` as
`billboard_align.apply_X(...)` (see that file's own `import
husk_blender_geoset_mask as billboard_align`). `fix_additive_materials()`
is the one exception, living directly in `render_glb.py` — but that one is
a pure per-material node-graph rebuild with no Scene/Compositor/ViewLayer
setup involved; this feature needs real Compositor graph construction
(`scene.compositing_node_group`, Cryptomatte nodes), which is exactly the
kind of shared, non-trivial Blender-scene-level logic
`husk_blender_geoset_mask.py` already owns. Add a new function there,
e.g. `apply_multiply_blend_compositing(scene, materials)`, matching the
existing functions' own signature shape (scene/materials or
mesh_objs/armature_obj as needed, not a full-context grab-bag).

**`render_glb.py`'s `main()` calls it once**, the same way it already
calls `billboard_align.apply_geoset_switches(mesh_objs, armature_obj)` and
`billboard_align.apply_billboard_alignment(mesh_objs, armature_obj,
cam_obj)` — no duplicate node-graph-building logic in `render_glb.py`
itself. Call it after import, alongside the other post-import fixups,
before framing/camera setup (order likely doesn't matter much here, but
match where `fix_additive_materials()` is already called for consistency).

## Concrete next steps

1. Design the actual node graph precisely for the real Mod/Mod2x formula
   (not just "any Add/Multiply combination that looks plausible") —
   Luna's demonstrated graph proves the *technique*, not yet a specific
   verified-correct formula for this specific use. Build a small headless
   test (same pattern as this session's other Compositor tests) that
   renders a known base+mult color pair through the real proposed graph
   and checks the output against a hand-computed `dest*src` (Mod) and
   `dest*src*2` clamped (Mod2x) expectation, the same way the (abandoned)
   two-ViewLayer approach was verified — that verification discipline is
   worth keeping even though the two-ViewLayer approach itself wasn't.
2. Root-cause (or at least characterize precisely enough to work around)
   the EEVEE Cryptomatte-on-`BLEND`-materials bug above — real blocker for
   the actual pipeline, not optional polish.
3. Implement `apply_multiply_blend_compositing` in
   `tools/husk_blender_geoset_mask.py`, wire it into `render_glb.py`'s
   `main()`, verify against a real corpus fixture — the corpus-scan work
   in `MULTI_TEXTURE_LAYER_TODO.md`/`PIXEL_SHADER_FORMULAS_TODO.md` didn't
   specifically tag Mod/Mod2x-bearing files, so finding one may need a
   small ad hoc scan first (filter `.skin` batches for `blend_mode in (5,
   6)`, same shape as `shader_id_task.py`).
4. Confirm the animated-render case (`bpy.ops.render.render(animation=True)`
   re-evaluating the Compositor graph correctly per frame) — not checked
   at all yet, static-render-only so far.
