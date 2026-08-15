"""Headless Blender script: import one .glb, frame it, render one WebP image
(a static model) or a short looping WebM/VP9 video (a model carrying real
skeletal animation and/or husk's own animated texture-transform/tint/fade
extras) -- see `render_duration_seconds`'s doc comment for which case
applies and why. Also copies the source `.glb` next to the rendered preview
(same basename, `.glb` extension) so `tools/live_gallery/server.py`'s
interactive three.js viewer -- which derives its own model URL the
identical way -- can load and play back the *real* glTF animation
client-side, independent of whatever this script baked into the preview.

Usage:
    blender --background --factory-startup --python render_glb.py -- <in.glb> <out.webp>

`out_path` names the **static** case exactly (its extension is trusted as
given -- use_file_extension is disabled below, so the caller decides the
still-image format by what it names the output). The **animated** case
instead writes to `out_path` with its extension swapped to `.webm`
(`final_out_path`, printed on success) -- an animated preview is a real
video, encoded directly by Blender's own FFmpeg/WebM/VP9 output settings in
one native `bpy.ops.render.render(animation=True)` call, not a PNG-per-
frame sequence hand-stitched into an animated WebP afterward (an earlier
version of this script did exactly that -- a real, avoidable extra step-
and-a-half once Luna pointed out Blender can encode video natively).
WebP (quality 80, lossy) is kept for the still case -- these are
flat-shaded QA thumbnails, not archival renders, so lossy compression
costs nothing that matters here.

Exits nonzero (via a bare exception, letting Blender's own traceback print
to stderr) on any failure -- the caller captures stdout+stderr and stores
it as this file's failure detail, same "let the tool print its own real
error" discipline as husk's own ParseError text.
"""
import math
import shutil
import sys
from pathlib import Path

import bpy
import mathutils

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import husk_blender_geoset_mask as billboard_align  # noqa: E402 -- see sys.path.insert above

# The animated render window is min(native_duration, RENDER_MAX_WINDOW_SECONDS)
# -- every clip renders at its own real length, no padding to a minimum.
# An earlier version of this padded any clip under 5s up to a fixed 5s
# floor (looping it a few extra times within the file itself) on the theory
# that a very short clip needed to be "long enough to read as animating" --
# dropped once tools/live_gallery/server.py grew a real `<video loop>`
# player (this session, same day): a native-duration clip already loops
# seamlessly in the browser and via the .glb's own Cycles-modifier loop
# (loop_action_natively) when opened directly, so baking extra repetitions
# into the encoded file bought literally nothing -- same frames shown, just
# more of them to render/encode/store, for a browser that already repeats
# them for free. A genuinely fast loop (well under a second) is a real,
# separate UI concern -- not solved by padding the file, since a padded
# file still repeats the same fast cycle just as fast -- so it's handled
# client-side instead (live_gallery/server.py starts a sub-0.5s clip
# paused, with an explicit play button, rather than autoplaying something
# that would flicker). Only a genuinely long outlier (multi-minute -- real
# cinematic-length ambient loops exist in the corpus) gets clamped down to
# the ceiling at all.
RENDER_MAX_WINDOW_SECONDS = 60.0
# A real playback frame rate, not a coarse handful of samples -- an earlier
# version of this script rendered a fixed 12 *total* frames across the
# whole 5s window (2.4fps, a visible strobe, not real motion) and rendered
# them one at a time via individual write_still=True calls plus a manual
# Python frame loop. Corrected on Luna's own direct pushback: once Blender
# and the model are actually loaded (the real fixed cost here), per-frame
# EEVEE rendering itself is cheap and Blender's own native animation
# renderer (bpy.ops.render.render(animation=True), below) is both faster
# and simpler than driving it frame-by-frame from Python -- confirmed
# empirically, not assumed: 100 frames at 320x320 rendered in 6.18s via a
# single animation=True call (~62ms/frame), and animation=True's own
# internal frame-stepping fires bpy.app.handlers.frame_change_pre exactly
# the way manual scene.frame_set() calls did (101 handler calls recorded
# for 100 rendered frames, real curve values changing every one of them --
# so husk_blender_geoset_mask's texture-transform/tint-fade handlers need
# no special-casing here at all, they just work under animation=True).
RENDER_FPS = 24
# Below this, a model's own longest real animation source (skeletal action
# or husk's own texture-transform/tint/fade curves) is treated as "not
# really animated" -- render the single static-still path instead of paying
# for a full animated render of a model that would just show the same frame
# over and over.
MIN_ANIMATED_DURATION_SECONDS = 0.05

# Real EEVEE render-quality knobs, tuned down hard for corpus-wide speed --
# these are flat-shaded QA thumbnails, not archival renders (this file's
# own long-standing framing for the still-image WebP case, now applied to
# the animated case too). Measured against the real wolf.m2 fixture (66
# bones, 120-frame animated render): Blender's own EEVEE defaults
# (taa_render_samples=64, shadows on) took 13.40s; these settings took
# 4.73s for the *entire* animated render -- at that point the real cost is
# almost entirely Blender startup + model import, not rendering, exactly
# matching the "biggest time sink is startup and model loading" read this
# was built around. Sample counts between 1 and 16 measured within noise
# of each other time-wise (4.36s-5.02s) -- 16 kept as a small, effectively-
# free quality margin over the noisier 1-4 range, not chosen for speed.
EEVEE_RENDER_SAMPLES = 16


def fix_additive_materials() -> int:
    """WoW blend modes 3 (NoAlphaAdd) and 4 (Add) have no core-glTF
    equivalent (src/gltf_mesh.hpp's Material::blendMode doc comment) --
    husk's default-import shape (Principled BSDF + alpha-blend/hashed) shows
    such a material's near-opaque, mostly-black background as a solid dark
    panel instead of contributing nothing where it's black, which is what
    WoW's own additive compositing actually does. Confirmed wrong against a
    real corpus file (creature/celestialfoxwyvern/celestialfoxwyvern.m2:
    a constellation-style effect that should read as glowing lines on
    nothing, not a dark diamond panel).

    husk exports the real blend_mode as material extras whenever it's > 2
    (see that same doc comment) -- Blender's glTF importer surfaces
    material extras as plain custom properties on the imported material
    (confirmed empirically: unlike glTF *skin* extras, which land nowhere,
    node/mesh/material/camera/light/scene extras all become real bpy custom
    properties post-import). This rebuilds each such material's shader as
    real additive -- Transparent BSDF + Emission (fed by the same
    base-color image the importer already wired into Base Color) combined
    via Add Shader -- in place of the standard import's Principled BSDF
    wiring.

    Modes 5/6 (Mod/Mod2x, multiply) are a real, separate gap, not attempted
    here -- no demonstrated real-corpus case driving it yet, and multiply
    compositing needs a different node shape (no Add Shader equivalent).
    Returns the number of materials rebuilt, purely for the caller's own
    stdout note.
    """
    fixed = 0
    for mat in bpy.data.materials:
        if mat.get("blend_mode") not in (3, 4):
            continue
        if not mat.use_nodes or mat.node_tree is None:
            continue
        nodes = mat.node_tree.nodes
        links = mat.node_tree.links
        output = next((n for n in nodes if n.type == "OUTPUT_MATERIAL"), None)
        if output is None:
            continue

        # Two different importer shapes reach this point, depending on
        # whether husk also set KHR_materials_unlit on this material (WoW's
        # own M2Material flag 0x01 -- real, common alongside additive blend
        # modes, e.g. every material on the celestialfoxwyvern fixture this
        # was verified against): a "lit" import gives a Principled BSDF
        # with Base Color already wired to the right texture; an "unlit"
        # import gives Blender's own KHR_materials_unlit emulation graph,
        # which already *has* a real Emission node fed by that texture --
        # reused directly rather than rebuilt, so the correct source is
        # never in question.
        emission = next((n for n in nodes if n.type == "EMISSION"), None)
        if emission is None:
            principled = next((n for n in nodes if n.type == "BSDF_PRINCIPLED"), None)
            if principled is None:
                continue
            base_color_input = principled.inputs.get("Base Color")
            emission = nodes.new("ShaderNodeEmission")
            emission.location = (principled.location.x, principled.location.y - 200)
            if base_color_input.is_linked:
                links.new(base_color_input.links[0].from_socket, emission.inputs["Color"])
            else:
                emission.inputs["Color"].default_value = base_color_input.default_value

        # Additive blending is unconditional pass-through of the background
        # (Transparent BSDF below) plus this texture's own RGB scaled by its
        # own alpha -- alpha isn't a coverage mask here (nothing occludes in
        # Add mode), it's how much light each pixel contributes. Confirmed
        # against a real corpus file's own bytes: item/objectcomponents/
        # weapon/staff_2h_artifactheartofkure_d_02.m2's glow-ring texture
        # carries a genuine 0..255 alpha gradient under an otherwise
        # uniformly-bright RGB region (alphaextract crop of the source .blp,
        # not a guess) -- both the Blender importer's own unlit-material
        # graph (a Mix Shader gated by an Alpha threshold) and a Principled
        # BSDF's separate Alpha input already account for this; wiring only
        # Color into Emission and dropping Alpha rendered the whole textured
        # area at full brightness regardless of alpha, turning a should-fade
        # glow ring into a hard-edged, inverted-looking donut. Premultiplying
        # here restores that shape from the same texture node's own Alpha
        # output feeding Color -- whichever branch above set that link.
        color_link = emission.inputs["Color"].links[0] if emission.inputs["Color"].is_linked else None
        if color_link is not None and "Alpha" in color_link.from_node.outputs:
            premultiply = nodes.new("ShaderNodeVectorMath")
            premultiply.operation = "MULTIPLY"
            premultiply.location = (emission.location.x - 200, emission.location.y - 50)
            links.new(color_link.from_socket, premultiply.inputs[0])
            links.new(color_link.from_node.outputs["Alpha"], premultiply.inputs[1])
            links.new(premultiply.outputs["Vector"], emission.inputs["Color"])

        transparent = next((n for n in nodes if n.type == "BSDF_TRANSPARENT"), None)
        if transparent is None:
            transparent = nodes.new("ShaderNodeBsdfTransparent")
            transparent.location = (emission.location.x, emission.location.y - 150)

        add_shader = nodes.new("ShaderNodeAddShader")
        add_shader.location = (emission.location.x + 200, emission.location.y - 75)
        links.new(transparent.outputs["BSDF"], add_shader.inputs[0])
        links.new(emission.outputs["Emission"], add_shader.inputs[1])
        links.new(add_shader.outputs["Shader"], output.inputs["Surface"])

        mat.surface_render_method = "BLENDED"  # EEVEE Next: real compositing, not DITHERED's noisy hash
        fixed += 1
    return fixed


def _mrsocket(sockets, identifier):
    return next(s for s in sockets if s.identifier == identifier)


def _mix_rgba(nodes, links, name, blend_type, a, b, fac=1.0):
    """RGBA Mix node (`a` MIX/MULTIPLY/ADD `b`, weighted by `fac`) -- `a`/`b`
    are output sockets (never raw constants, callers wrap those with a
    plain RGB node first if they ever need one -- none of the formula
    table below does)."""
    n = nodes.new("ShaderNodeMix")
    n.name = name
    n.data_type = 'RGBA'
    n.blend_type = blend_type
    n.clamp_result = True
    _mrsocket(n.inputs, "Factor_Float").default_value = fac
    links.new(a, _mrsocket(n.inputs, "A_Color"))
    links.new(b, _mrsocket(n.inputs, "B_Color"))
    return _mrsocket(n.outputs, "Result_Color")


def _scale_rgb(nodes, links, name, a, factor):
    """Scales color socket `a` by `factor` -- a plain Python float (a
    constant, e.g. the `*2.0`/`*4.0` in a Mod2x/Mod2x_Mod2x formula) or
    another socket (e.g. an alpha channel, for a `tex.rgb * tex.a` term --
    VectorMath's own SCALE operation accepts a linked Float for its Scale
    input just as readily as a constant, no separate broadcast node
    needed)."""
    n = nodes.new("ShaderNodeVectorMath")
    n.name = name
    n.operation = 'SCALE'
    links.new(a, _mrsocket(n.inputs, "Vector"))
    scale_in = _mrsocket(n.inputs, "Scale")
    if isinstance(factor, float):
        scale_in.default_value = factor
    else:
        links.new(factor, scale_in)
    return _mrsocket(n.outputs, "Vector")


def _amul(nodes, links, name, *sockets):
    """Chains `ShaderNodeMath` MULTIPLY over 2+ Float sockets -- used for
    alpha terms like `tex1.a * tex2.a` or `tex1.a * tex2.a * 2.0` (pass a
    plain float alongside sockets for the constant scale case)."""
    result = sockets[0]
    for i, operand in enumerate(sockets[1:]):
        n = nodes.new("ShaderNodeMath")
        n.name = f"{name}_{i}"
        n.operation = 'MULTIPLY'
        n.use_clamp = True
        links.new(result, n.inputs[0])
        if isinstance(operand, float):
            n.inputs[1].default_value = operand
        else:
            links.new(operand, n.inputs[1])
        result = n.outputs["Value"]
    return result


def _aadd(nodes, links, name, a, b):
    n = nodes.new("ShaderNodeMath")
    n.name = name
    n.operation = 'ADD'
    n.use_clamp = True
    links.new(a, n.inputs[0])
    links.new(b, n.inputs[1])
    return n.outputs["Value"]


def _ainv(nodes, links, name, a):
    """`1.0 - a`."""
    n = nodes.new("ShaderNodeMath")
    n.name = name
    n.operation = 'SUBTRACT'
    n.use_clamp = True
    n.inputs[0].default_value = 1.0
    links.new(a, n.inputs[1])
    return n.outputs["Value"]


# Real WoW pixel-shader combiner formulas for a batch's 2nd texture layer
# (`M2Batch::textureCount > 1`), keyed by husk's own resolved `pixel_shader`
# extras name (`src/m2_shader_names.cpp`) -- transcribed from
# `documentation/wowdev-wiki/wikitext/Pixel_shader_logic_for_mixing_colors.wiki`,
# **not trusted uncritically**: cross-checked line-by-line against
# `reference/wow.export`'s own independently-implemented GLSL
# (`m2.fragment.shader`) this session (`TODO/PIXEL_SHADER_FORMULAS_TODO.md`'s
# investigation) before being wired in here. One real, confirmed wiki bug
# found and corrected: `Combiners_Opaque_Mod2xNA_Alpha`'s own `tex1*tex2`
# cross term is missing a `*2.0` the wiki's neighboring `Mod2x`/`Mod2xNA`
# entries both have and a second, independent wowdev.wiki page
# (`documentation/wowdev-wiki/md/M2/Rendering.md`) confirms belongs there --
# applied below, not the wiki text as literally written.
#
# `meshResColor` (the wiki's own per-batch vertex-color tint term) is
# deliberately dropped from every formula below, not approximated: Blender's
# own glTF importer already doesn't apply `baseColorFactor` as a real
# multiply once a texture is linked (confirmed empirically against a real
# multi-material fixture -- Base Color/Alpha link straight to the Image
# Texture node, `default_value` sits there unused) -- matching that same
# existing baseline instead of inventing a *new* tint multiply this fixup
# would be the only thing applying is the honest choice, not a shortcut.
# Real, higher-value gap tracked separately if it ever matters
# (`baseColorFactor` not visually applying at all is a pre-existing,
# unrelated Blender-import limitation, not something this fixup owns).
#
# Alpha handling follows the "NA" naming convention directly (`Mod2xNA`/
# `ModNA`/`AddNA` = "No Alpha", i.e. the wiki's own `finalColor.a =
# meshResColor.a` for exactly these suffixes) -- forced fully opaque
# (`ALPHA_OPAQUE` sentinel below) for every "NA"-suffixed formula, since
# `meshResColor.a` collapses to ~1.0 under the same dropped-tint reasoning
# above. Non-"NA" formulas keep the wiki's real alpha math (with the same
# `meshResColor.a`-dropped simplification applied to their own formula).
#
# Deliberately **not** in this table -- real formulas exist for some of
# these, but each has its own reason not to guess further:
# - `Combiners_Add_Mod`: `meshResColor` is a raw *addend* here (`tex1 +
#   meshResColor`), not the final multiply every other formula uses --
#   the dropped-tint simplification above doesn't safely apply, would need
#   real accounting, not scoped this pass.
# - `Combiners_Opaque_Mod2xNA_Alpha_Add`/`_3s`/`_UnshAlpha`/`_Alpha_Alpha`:
#   share `Opaque_Mod2xNA_Alpha`'s own corrected core formula (reused
#   below as an approximation) but each adds a real 3rd-texture-layer
#   "specular"/crossfade refinement husk doesn't attach data for yet
#   (`additional_textures` only carries what husk actually embedded --
#   real corpus check this session found the 3rd layer's own FileDataID
#   frequently isn't even resolvable in the local corpus at all).
# - `Combiners_Mod_AddAlpha_Alpha`: the wiki's own documented formula has a
#   literal duplicated term (independent evidence of a wiki transcription
#   bug, not just a wow.export one) -- not implementing a formula neither
#   source actually gives cleanly.
# - `Combiners_Mod_Add_Alpha`/`Mod_AddAlpha_Wgt`/`Opaque_AddAlpha_Wgt`/
#   `Opaque_Mod_Add_Wgt`: only "moderate confidence" per this session's
#   investigation (sub-pattern reuse, not independently confirmed), and
#   wow.export's own renderer has a real, separate bug silently dropping
#   every one of these formulas' additive term -- not implemented pending
#   real visual confirmation.
# - `Guild`/`Guild_NoBorder`/`Guild_Opaque`: real per-material tint inputs
#   (`generic0/1/2`) unresolved by any source checked this session.
# - `Combiners_Mod_Dual_Crossfade`/`Combiners_Mod_Masked_Dual_Crossfade`:
#   wow.export's own `ShaderMapper.js` disagrees with wowdev.wiki's table
#   on the shaderId this session's investigation found -- unresolved.
# - `Illum`: no formula from any source, zero real corpus repros found.
# - `Combiners_Mod_Depth`: functionally single-texture `Combiners_Mod`
#   (real, per this session's investigation) -- Blender's default import
#   already renders this correctly, nothing to rebuild.
ALPHA_OPAQUE = "opaque"


def _pixel_shader_formula_table(nodes, links):
    """Returns {pixel_shader_name: (rgb_builder, alpha_spec)}, built fresh
    per-material (the builders close over this material's own `nodes`/
    `links`) -- `rgb_builder(t1c, t1a, t2c, t2a)` returns the combined
    color socket; `alpha_spec` is `ALPHA_OPAQUE` or a
    `(t1c, t1a, t2c, t2a) -> socket` callable."""
    def mix2x_real(t1c, t1a, t2c, t2a):
        doubled = _scale_rgb(nodes, links, "Mod2xCross", _mix_rgba(nodes, links, "Mod2xMul", 'MULTIPLY', t1c, t2c), 2.0)
        n = nodes.new("ShaderNodeMix")
        n.data_type = 'RGBA'
        n.clamp_result = True
        links.new(t1a, _mrsocket(n.inputs, "Factor_Float"))
        links.new(doubled, _mrsocket(n.inputs, "A_Color"))
        links.new(t1c, _mrsocket(n.inputs, "B_Color"))
        return _mrsocket(n.outputs, "Result_Color")

    def modna_real(t1c, t1a, t2c, t2a):
        mul = _mix_rgba(nodes, links, "ModNAMul", 'MULTIPLY', t1c, t2c)
        n = nodes.new("ShaderNodeMix")
        n.data_type = 'RGBA'
        n.clamp_result = True
        links.new(t1a, _mrsocket(n.inputs, "Factor_Float"))
        links.new(mul, _mrsocket(n.inputs, "A_Color"))
        links.new(t1c, _mrsocket(n.inputs, "B_Color"))
        return _mrsocket(n.outputs, "Result_Color")

    def alpha_alpha_real(t1c, t1a, t2c, t2a):
        inner = nodes.new("ShaderNodeMix")
        inner.data_type = 'RGBA'
        inner.clamp_result = True
        links.new(t2a, _mrsocket(inner.inputs, "Factor_Float"))
        links.new(t1c, _mrsocket(inner.inputs, "A_Color"))
        links.new(t2c, _mrsocket(inner.inputs, "B_Color"))
        inner_out = _mrsocket(inner.outputs, "Result_Color")
        outer = nodes.new("ShaderNodeMix")
        outer.data_type = 'RGBA'
        outer.clamp_result = True
        links.new(t1a, _mrsocket(outer.inputs, "Factor_Float"))
        links.new(inner_out, _mrsocket(outer.inputs, "A_Color"))
        links.new(t1c, _mrsocket(outer.inputs, "B_Color"))
        return _mrsocket(outer.outputs, "Result_Color")

    def alpha_only(t1c, t1a, t2c, t2a):
        n = nodes.new("ShaderNodeMix")
        n.data_type = 'RGBA'
        n.clamp_result = True
        links.new(t2a, _mrsocket(n.inputs, "Factor_Float"))
        links.new(t1c, _mrsocket(n.inputs, "A_Color"))
        links.new(t2c, _mrsocket(n.inputs, "B_Color"))
        return _mrsocket(n.outputs, "Result_Color")

    def add_alpha_weighted(t1c, t2c, t2a, name):
        # tex1 + tex2.rgb*tex2.a -- Combiners_*_AddAlpha's own shared shape.
        weighted = _scale_rgb(nodes, links, f"{name}Wgt", t2c, t2a)
        return _mix_rgba(nodes, links, name, 'ADD', t1c, weighted)

    return {
        "Combiners_Opaque_Opaque": (
            lambda t1c, t1a, t2c, t2a: _mix_rgba(nodes, links, "OpOp", 'MULTIPLY', t1c, t2c),
            ALPHA_OPAQUE),
        "Combiners_Opaque_Add": (
            lambda t1c, t1a, t2c, t2a: _mix_rgba(nodes, links, "OpAdd", 'ADD', t1c, t2c),
            ALPHA_OPAQUE),
        "Combiners_Opaque_AddNA": (
            lambda t1c, t1a, t2c, t2a: _mix_rgba(nodes, links, "OpAddNA", 'ADD', t1c, t2c),
            ALPHA_OPAQUE),
        "Combiners_Opaque_Mod2x": (
            lambda t1c, t1a, t2c, t2a: _scale_rgb(nodes, links, "OpMod2x", _mix_rgba(nodes, links, "OpMod2xMul", 'MULTIPLY', t1c, t2c), 2.0),
            lambda t1c, t1a, t2c, t2a: _amul(nodes, links, "OpMod2xA", t2a, 2.0)),
        "Combiners_Opaque_Mod2xNA": (
            lambda t1c, t1a, t2c, t2a: _scale_rgb(nodes, links, "OpMod2xNA", _mix_rgba(nodes, links, "OpMod2xNAMul", 'MULTIPLY', t1c, t2c), 2.0),
            ALPHA_OPAQUE),
        "Combiners_Opaque_Mod": (
            lambda t1c, t1a, t2c, t2a: _mix_rgba(nodes, links, "OpMod", 'MULTIPLY', t1c, t2c),
            lambda t1c, t1a, t2c, t2a: t2a),
        "Combiners_Mod_Opaque": (
            lambda t1c, t1a, t2c, t2a: _mix_rgba(nodes, links, "ModOp", 'MULTIPLY', t1c, t2c),
            lambda t1c, t1a, t2c, t2a: t1a),
        "Combiners_Mod_Add": (
            lambda t1c, t1a, t2c, t2a: _mix_rgba(nodes, links, "ModAdd", 'ADD', t1c, t2c),
            lambda t1c, t1a, t2c, t2a: _aadd(nodes, links, "ModAddA", t1a, t2a)),
        "Combiners_Mod_AddNA": (
            lambda t1c, t1a, t2c, t2a: _mix_rgba(nodes, links, "ModAddNA", 'ADD', t1c, t2c),
            ALPHA_OPAQUE),
        "Combiners_Mod_Mod2x": (
            lambda t1c, t1a, t2c, t2a: _scale_rgb(nodes, links, "ModMod2x", _mix_rgba(nodes, links, "ModMod2xMul", 'MULTIPLY', t1c, t2c), 2.0),
            lambda t1c, t1a, t2c, t2a: _amul(nodes, links, "ModMod2xA", t1a, t2a, 2.0)),
        "Combiners_Mod_Mod2xNA": (
            lambda t1c, t1a, t2c, t2a: _scale_rgb(nodes, links, "ModMod2xNA", _mix_rgba(nodes, links, "ModMod2xNAMul", 'MULTIPLY', t1c, t2c), 2.0),
            ALPHA_OPAQUE),
        "Combiners_Mod_Mod": (
            lambda t1c, t1a, t2c, t2a: _mix_rgba(nodes, links, "ModMod", 'MULTIPLY', t1c, t2c),
            lambda t1c, t1a, t2c, t2a: _amul(nodes, links, "ModModA", t1a, t2a)),
        "Combiners_Mod2x_Mod2x": (
            lambda t1c, t1a, t2c, t2a: _scale_rgb(nodes, links, "Mod2xMod2x", _mix_rgba(nodes, links, "Mod2xMod2xMul", 'MULTIPLY', t1c, t2c), 4.0),
            lambda t1c, t1a, t2c, t2a: _amul(nodes, links, "Mod2xMod2xA", t1a, t2a, 4.0)),
        "Combiners_Opaque_Mod2xNA_Alpha": (mix2x_real, ALPHA_OPAQUE),
        # Approximation, see module comment -- shares the corrected core
        # formula, missing each variant's own extra refinement term.
        "Combiners_Opaque_Mod2xNA_Alpha_Add": (mix2x_real, ALPHA_OPAQUE),
        "Combiners_Opaque_Mod2xNA_Alpha_3s": (mix2x_real, ALPHA_OPAQUE),
        "Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha": (mix2x_real, ALPHA_OPAQUE),
        "Combiners_Opaque_Mod2xNA_Alpha_Alpha": (mix2x_real, ALPHA_OPAQUE),
        "Combiners_Opaque_ModNA_Alpha": (modna_real, ALPHA_OPAQUE),
        "Combiners_Opaque_AddAlpha": (
            lambda t1c, t1a, t2c, t2a: add_alpha_weighted(t1c, t2c, t2a, "OpAddAlpha"),
            ALPHA_OPAQUE),
        "Combiners_Opaque_AddAlpha_Alpha": (
            lambda t1c, t1a, t2c, t2a: _mix_rgba(
                nodes, links, "OpAddAlphaAlpha", 'ADD', t1c,
                _scale_rgb(nodes, links, "OpAddAlphaAlphaWgt", t2c, _amul(nodes, links, "OpAddAlphaAlphaFac", t2a, _ainv(nodes, links, "OpAddAlphaAlphaInv", t1a)))),
            ALPHA_OPAQUE),
        "Combiners_Mod_AddAlpha": (
            lambda t1c, t1a, t2c, t2a: add_alpha_weighted(t1c, t2c, t2a, "ModAddAlpha"),
            lambda t1c, t1a, t2c, t2a: t1a),
        "Combiners_Opaque_Alpha_Alpha": (alpha_alpha_real, ALPHA_OPAQUE),
        "Combiners_Opaque_Alpha": (alpha_only, ALPHA_OPAQUE),
    }


def _env_map_uv(nodes, links):
    """Reflection-vector UV recipe for a `_Env`-suffixed vertex shader (real
    client formula, `M2/.skin.wiki`'s own `===Environment mapping===`
    section, not reconstructed by inference -- see
    `TODO/MULTI_TEXTURE_LAYER_TODO.md`'s own module comment for the derivation).
    Standard matcap-style technique: camera-space incoming vector, reflected
    off the shading normal, remapped from [-1,1] to [0,1] -- returns the
    Vector socket to feed into an Image Texture node's own Vector input in
    place of a normal UV Map node."""
    geometry = nodes.new("ShaderNodeNewGeometry")
    normal_input = nodes.new("ShaderNodeVectorTransform")
    normal_input.vector_type = 'NORMAL'
    normal_input.convert_from = 'WORLD'
    normal_input.convert_to = 'CAMERA'
    links.new(geometry.outputs["Normal"], normal_input.inputs["Vector"])

    incoming = nodes.new("ShaderNodeVectorTransform")
    incoming.vector_type = 'VECTOR'
    incoming.convert_from = 'WORLD'
    incoming.convert_to = 'CAMERA'
    links.new(geometry.outputs["Incoming"], incoming.inputs["Vector"])

    reflect = nodes.new("ShaderNodeVectorMath")
    reflect.operation = 'REFLECT'
    links.new(incoming.outputs["Vector"], reflect.inputs[0])
    links.new(normal_input.outputs["Vector"], reflect.inputs[1])

    scale = nodes.new("ShaderNodeVectorMath")
    scale.operation = 'MULTIPLY_ADD'
    scale.inputs[1].default_value = (0.5, 0.5, 0.5)
    scale.inputs[2].default_value = (0.5, 0.5, 0.5)
    links.new(reflect.outputs["Vector"], scale.inputs[0])
    return scale.outputs["Vector"]


def fix_multi_texture_layers() -> tuple[int, int]:
    """WoW's real fixed-function multi-texture-layer combiner math
    (`M2Batch::textureCount > 1`, ~79% of the real corpus per
    `WIKI_FINDINGS/M2/skin.md`) and env-mapped ("shiny metal") vertex
    shaders -- both real gaps `TODO/MULTI_TEXTURE_LAYER_TODO.md` tracked as
    "husk resolves the real formula name, nothing plays it back yet".
    husk already resolves and exports `pixel_shader`/`vertex_shader` names
    (`src/m2_shader_names.cpp`) plus each additional texture layer's own
    embedded image (`additional_textures` extras, `AdditionalTextureLayer`)
    -- this rebuilds the real combiner math as a small node graph feeding
    Base Color, same site/pattern as `fix_additive_materials` above.

    Skips any material `fix_additive_materials` already fully rebuilt
    (`blend_mode` 3/4 -- that fixup replaces the whole Surface shader,
    Principled BSDF included, so there is no live Base Color chain left
    here to extend) -- real, necessary ordering discipline, not a missed
    case, since Mod/Mod2x (5/6) materials are untouched by that fixup
    (`apply_multiply_blend_compositing` operates on the render's own beauty
    pass via the Compositor, never touches a material's own node graph) and
    stay fully eligible here.

    Returns (combiner_layers_fixed, env_maps_wired).
    """
    combiner_fixed = 0
    envmap_fixed = 0

    for mat in bpy.data.materials:
        if mat.get("blend_mode") in (3, 4):
            continue
        if not mat.use_nodes or mat.node_tree is None:
            continue
        principled = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if principled is None:
            continue
        nodes, links = mat.node_tree.nodes, mat.node_tree.links

        base_input = principled.inputs["Base Color"]
        tex1_node = base_input.links[0].from_node if base_input.is_linked else None
        if tex1_node is not None and tex1_node.type != "TEX_IMAGE":
            tex1_node = None  # unexpected shape (see module comment) -- skip rather than guess

        # Env-mapped vertex shader, single texture layer only -- the only
        # unambiguous case without a per-layer env flag from husk yet (see
        # module comment): wire the reflection UV straight onto tex1.
        vertex_shader = mat.get("vertex_shader") or ""
        layers = billboard_align._to_pyobj(mat.get("additional_textures")) or []
        if tex1_node is not None and vertex_shader in ("Diffuse_T1_Env", "Diffuse_Env") and not layers:
            env_vector = _env_map_uv(nodes, links)
            links.new(env_vector, tex1_node.inputs["Vector"])
            envmap_fixed += 1

        pixel_shader = mat.get("pixel_shader") or ""
        if tex1_node is None or not layers or not pixel_shader:
            continue
        table = _pixel_shader_formula_table(nodes, links)
        entry = table.get(pixel_shader)
        if entry is None:
            continue
        rgb_builder, alpha_spec = entry

        layer0 = layers[0]
        tex2_fdid = layer0.get("file_data_id")
        tex2_image = bpy.data.images.get(str(tex2_fdid)) if tex2_fdid else None
        if tex2_image is None:
            continue  # husk didn't have a --textures match for this layer -- nothing to combine

        tex2_node = nodes.new("ShaderNodeTexImage")
        tex2_node.name = f"HuskLayer2_{pixel_shader}"
        tex2_node.image = tex2_image

        t1c, t1a = tex1_node.outputs["Color"], tex1_node.outputs["Alpha"]
        t2c, t2a = tex2_node.outputs["Color"], tex2_node.outputs["Alpha"]

        result_color = rgb_builder(t1c, t1a, t2c, t2a)
        links.new(result_color, base_input)

        alpha_input = principled.inputs["Alpha"]
        if alpha_spec == ALPHA_OPAQUE:
            for link in list(alpha_input.links):
                links.remove(link)
            alpha_input.default_value = 1.0
        elif callable(alpha_spec):
            result_alpha = alpha_spec(t1c, t1a, t2c, t2a)
            if result_alpha is not None:
                links.new(result_alpha, alpha_input)

        combiner_fixed += 1

    return combiner_fixed, envmap_fixed


def _world_bbox(mesh_objs) -> tuple["mathutils.Vector", "mathutils.Vector"]:
    """Same technique main() uses for camera framing (obj.bound_box, live-
    updated by the current depsgraph state after a scene.frame_set()) --
    reused here to sample posed geometry at a handful of frames without the
    cost of a full per-vertex evaluated-mesh walk."""
    bbox_min = mathutils.Vector((math.inf, math.inf, math.inf))
    bbox_max = mathutils.Vector((-math.inf, -math.inf, -math.inf))
    for obj in mesh_objs:
        for corner in obj.bound_box:
            world_corner = obj.matrix_world @ mathutils.Vector(corner)
            bbox_min = mathutils.Vector(min(a, b) for a, b in zip(bbox_min, world_corner))
            bbox_max = mathutils.Vector(max(a, b) for a, b in zip(bbox_max, world_corner))
    return bbox_min, bbox_max


def _skeletal_action_visibly_animates(mesh_objs, action) -> bool:
    """Samples the posed world-space bounding box at 5 points across
    `action`'s own real frame range and compares them -- False when nothing
    measurably moves. A real, confirmed corpus case (not hypothetical):
    item/objectcomponents/head/helm_armor_dragonhawkrider_d_02_hu_m.m2 was
    detected as animated (a nonzero-duration `global_seq_1` action existed)
    and got a full ~500-frame WebM render, but every single rendered frame
    came back byte-identical -- the action drives a bone with nothing
    weighted to it (an attachment/light/particle anchor, not a deforming
    bone), so the whole render was pure wasted GPU time producing a video
    that looks exactly like one still. This check exists to catch that
    class of case generically, without needing to know which specific
    real-world bone/attachment shape triggers it.

    Threshold is relative to the model's own static bounding radius (1e-3
    of it), not an absolute epsilon -- corpus models range from tiny props
    to 20x-scaled bosses (see render_glb.py's own clip_end comment), so a
    fixed absolute drift threshold would be wrong at either extreme.
    """
    scene = bpy.context.scene
    start, end = action.frame_range
    if end <= start:
        return False

    scene.frame_set(round(start))
    ref_min, ref_max = _world_bbox(mesh_objs)
    ref_radius = max((ref_max - ref_min).length / 2, 0.01)

    moved = False
    for frac in (0.25, 0.5, 0.75, 1.0):
        scene.frame_set(round(start + (end - start) * frac))
        cur_min, cur_max = _world_bbox(mesh_objs)
        drift = (cur_min - ref_min).length + (cur_max - ref_max).length
        if drift > ref_radius * 1e-3:
            moved = True
            break

    scene.frame_set(scene.frame_start)
    return moved


def render_duration_seconds(armature_obj, materials, mesh_objs) -> tuple[float, "bpy.types.Action | None"]:
    """The model's own real, native animation duration, in seconds -- the
    longest of (a) its currently-active skeletal action (Blender's glTF
    importer already leaves `armature_obj.animation_data.action` set to
    glTF `animations[0]` for direct scene.frame_current scrubbing, no NLA-
    track juggling needed -- confirmed empirically, not assumed: real
    `wolf.m2`, 38 imported actions, `animation_data.action` already points
    at the array-order-first one post-import), *provided it actually moves
    any rendered geometry (`_skeletal_action_visibly_animates` above --
    otherwise it's discounted to 0, see that function's own doc comment for
    the real corpus case this closes)*, and (b) husk's own animated
    `texture_transform_animation`/`tint_animation`/`fade_animation` extras
    (`husk_blender_geoset_mask.apply_texture_transform_animation`/
    `apply_tint_fade_animation`, called here as a side effect -- both grow
    `scene.frame_end` to fit their own real curve duration, never shrink,
    so resetting frame_start/frame_end to a minimal (1, 1) range first
    means whatever they leave it at *is* their own real duration, not
    Blender's arbitrary default 250). Also returns the active skeletal
    action itself (or None), since the caller needs it again to decide
    whether it needs a real (Cyclic-modifier) loop -- see main()'s own
    "shorter than the render window" branch.

    Which of a model's several real skeletal sequences to prefer for the
    preview was an open question in ANIMATED_TEXTURE_EFFECTS_TODO.md ("idle?
    the first sequence?") -- answered here as "whichever one the importer
    already activates," i.e. `animations[0]` in husk's own export order,
    not a guess re-derived from scratch.
    """
    scene = bpy.context.scene
    action = None
    skeletal_frames = 0.0
    if armature_obj is not None and armature_obj.animation_data is not None:
        action = armature_obj.animation_data.action
        if action is not None:
            skeletal_frames = action.frame_range[1] - action.frame_range[0]
            if skeletal_frames > 0 and not _skeletal_action_visibly_animates(mesh_objs, action):
                skeletal_frames = 0.0

    scene.frame_start = 1
    scene.frame_end = 1
    billboard_align.apply_texture_transform_animation(materials)
    billboard_align.apply_tint_fade_animation(materials)
    extras_frames = scene.frame_end - scene.frame_start

    spf = billboard_align._scene_seconds_per_frame()
    return max(skeletal_frames, extras_frames) * spf, action


def loop_action_natively(action) -> None:
    """Adds a real Blender 'Cycles' F-curve modifier (FModifierCycles,
    default mode_before/after='REPEAT', the native glTF/animation-editor
    concept of a looping clip) to every F-curve in `action` -- makes a
    skeletal action shorter than the render window repeat natively under
    Blender's own pose evaluation during bpy.ops.render.render(animation=True),
    the same way husk_blender_geoset_mask's own `_update_texture_transform_animations`
    already loops its own curves via an explicit `t % duration` in Python.
    No Python-side per-frame work needed for this case -- Blender's
    animation system evaluates the modifier itself.

    `action.fcurves` doesn't exist on Blender 4.4+'s layered Action data
    model (confirmed empirically against real Blender 5.1 behavior, not
    assumed from older API docs/muscle memory) -- F-curves instead live
    under action.layers[].strips[].channelbags[].fcurves. A glTF-imported
    action from husk's own export always has exactly one layer/strip/
    channelbag in practice (confirmed against the real wolf.m2 fixture),
    but this walks the full structure regardless rather than assuming that
    shape.
    """
    for layer in action.layers:
        for strip in layer.strips:
            if strip.type != "KEYFRAME":
                continue
            for channelbag in strip.channelbags:
                for fc in channelbag.fcurves:
                    if not any(m.type == "CYCLES" for m in fc.modifiers):
                        fc.modifiers.new("CYCLES")


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:]
    in_glb, out_path = argv[0], argv[1]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    # disable_bone_shape=True: the importer's default per-armature "Icosphere"
    # bone-visualization mesh is a real MESH-type scene object with no relation
    # to husk's own exported geometry -- left enabled, it silently inflates the
    # combined bounding box used for camera framing below (Luna caught this
    # from a render where the character was tiny in-frame).
    bpy.ops.import_scene.gltf(filepath=in_glb, disable_bone_shape=True)
    fixed_additive = fix_additive_materials()
    multi_layer_fixed, envmap_fixed = fix_multi_texture_layers()

    mesh_objs = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not mesh_objs:
        # A real, expected shape for some corpus files (e.g. cameras/*.m2
        # cinematic camera-track models: 0 vertices by design, per `husk
        # info`) -- not a husk/Blender defect, so this is a distinct
        # "SKIPPED" sentinel, not a crash/failure.
        print("SKIPPED no mesh objects imported (file has 0 vertices -- camera/track-only model)")
        return

    # Same shared step husk_blender_geoset_mask.py's own interactive
    # main() uses (apply_geoset_switches) -- one real call, not a
    # reimplementation, so a corpus preview render can never diverge from
    # what a real user opening the file in Blender actually sees.
    # Deliberately not routed through main() itself: main() does its own
    # argv parsing and glTF import, both already done differently here.
    armature_obj = next((o for o in bpy.context.scene.objects if o.type == "ARMATURE"), None)
    if armature_obj is not None:
        billboard_align.apply_geoset_switches(mesh_objs, armature_obj)

    bbox_min = mathutils.Vector((math.inf, math.inf, math.inf))
    bbox_max = mathutils.Vector((-math.inf, -math.inf, -math.inf))
    for obj in mesh_objs:
        for corner in obj.bound_box:
            world_corner = obj.matrix_world @ mathutils.Vector(corner)
            bbox_min = mathutils.Vector(min(a, b) for a, b in zip(bbox_min, world_corner))
            bbox_max = mathutils.Vector(max(a, b) for a, b in zip(bbox_max, world_corner))

    center = (bbox_min + bbox_max) / 2
    radius = max((bbox_max - bbox_min).length / 2, 0.01)

    cam_data = bpy.data.cameras.new("cam")
    cam_obj = bpy.data.objects.new("cam", cam_data)
    bpy.context.scene.collection.objects.link(cam_obj)
    cam_data.lens = 35  # wide-ish, generous headroom over a tight fit
    cam_data.sensor_fit = "VERTICAL"  # most WoW models are tall/thin -- frame by height, not width
    half_fov = math.atan((cam_data.sensor_height / 2) / cam_data.lens)
    distance = (radius / math.sin(half_fov)) * 1.03  # 3% margin
    cam_dir = mathutils.Vector((1, -1.6, 0.5)).normalized()
    cam_obj.location = center + cam_dir * distance
    cam_obj.rotation_euler = (center - cam_obj.location).to_track_quat("-Z", "Y").to_euler()
    # Blender's camera default clip_end (1000) silently culls the entire
    # object -- a fully blank render, no error -- for any model whose real
    # posed bounding-box radius exceeds ~322 units (default clip_end / sin
    # half_fov). Real, not hypothetical: creature/dimensiusboss02.m2's root
    # bone carries a constant (every one of its 8 sequences agrees) 20x
    # scale, a genuine large-creature authoring pattern, pushing its own
    # camera distance to ~1601. clip_end must bracket the far side of the
    # bounding sphere from the camera, not just the default; clip_start
    # stays at its default since the near side is never the problem here.
    cam_data.clip_end = distance + radius * 1.1
    bpy.context.scene.camera = cam_obj

    # Billboard bones (husk's own `_billboard_<mode>` joint-name suffix,
    # gltf_skeleton.cpp) need a real Camera object to face -- this is the
    # earliest point in this script one exists, so alignment happens here,
    # after framing, not at import time. See husk_blender_geoset_mask.py's
    # module docstring for the full design (a direct per-bone matrix
    # computation, not a native constraint).
    #
    # view_layer.update() is required here, not optional: cam_obj's
    # rotation_euler was just set above by plain Python assignment, which
    # does NOT immediately refresh cam_obj.matrix_world -- that only
    # happens on the next depsgraph evaluation. Without this call,
    # apply_billboard_alignment reads a stale (pre-rotation) camera
    # matrix, producing a visibly skewed (trapezoid, not flat) billboard --
    # caught by actually rendering a real fixture and looking at the
    # image, not by the headless matrix-math verification alone (that
    # test's own camera object never had its rotation change after
    # creation, so it never exercised this staleness).
    bpy.context.view_layer.update()
    armature_obj = next((o for o in bpy.context.scene.objects if o.type == "ARMATURE"), None)
    billboards_aligned = 0
    if armature_obj is not None:
        billboards_aligned = billboard_align.apply_billboard_alignment(mesh_objs, armature_obj, cam_obj)

    sun_data = bpy.data.lights.new("sun", type="SUN")
    sun_data.energy = 5.0
    sun_obj = bpy.data.objects.new("sun", sun_data)
    sun_obj.rotation_euler = (math.radians(55), 0, math.radians(35))
    bpy.context.scene.collection.objects.link(sun_obj)

    fill_data = bpy.data.lights.new("fill", type="SUN")
    fill_data.energy = 1.2
    fill_obj = bpy.data.objects.new("fill", fill_data)
    fill_obj.rotation_euler = (math.radians(-40), 0, math.radians(-120))
    bpy.context.scene.collection.objects.link(fill_obj)

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    # See EEVEE_RENDER_SAMPLES's own doc comment for the measured speedup --
    # applied to both the still and animated cases (a still-image WebP
    # render is cheap either way, but there's no reason to pay full-quality
    # sampling for a flat-shaded QA thumbnail there either).
    scene.eevee.taa_render_samples = EEVEE_RENDER_SAMPLES
    scene.eevee.use_shadows = False
    scene.eevee.use_fast_gi = False
    scene.render.resolution_x = 640
    scene.render.resolution_y = 480
    scene.render.filepath = out_path
    # use_file_extension defaults to True (auto-appends the format's own
    # extension) -- disabled so the caller's own extension is trusted
    # exactly as given, never doubled up (e.g. "foo.webp" -> "foo.webp.webp").
    scene.render.use_file_extension = False
    scene.render.image_settings.file_format = "WEBP"
    scene.render.image_settings.quality = 80  # lossy: these are flat-shaded QA thumbnails, not archival
    scene.world = bpy.data.worlds.new("world")
    scene.world.color = (0.12, 0.12, 0.14)

    materials = {obj.material_slots[i].material
                 for obj in mesh_objs
                 for i in range(len(obj.material_slots))
                 if obj.material_slots[i].material is not None}
    multiply_blended = billboard_align.apply_multiply_blend_compositing(scene, list(materials))
    duration, action = render_duration_seconds(armature_obj, materials, mesh_objs)

    final_out_path = out_path
    if duration < MIN_ANIMATED_DURATION_SECONDS:
        bpy.ops.render.render(write_still=True)
        anim_note = ""
    else:
        # Closest, not the importer's default Linear -- avoids a real GPU
        # mip-blur artifact found while building this (a high-frequency
        # repeating texture pattern renders as a flat, unchanging color
        # across every frame regardless of true UV/pose state, purely a
        # render-time sampling quirk, not a data/logic bug -- see
        # example_exports/README.md's own writeup of the same finding).
        for mat in materials:
            if mat.node_tree is None:
                continue
            for n in mat.node_tree.nodes:
                if n.type == "TEX_IMAGE":
                    n.interpolation = "Closest"

        # A real skeletal action shorter than the render window needs a
        # real (native, Blender-evaluated) loop -- husk_blender_geoset_mask's
        # own texture-transform/tint-fade curves already loop themselves
        # (an explicit t % duration inside their own frame_change_pre
        # handler, unaffected by anything here), but Blender's own pose
        # evaluation holds the last frame past an action's range by default
        # (Constant extrapolation) rather than repeating it.
        window_seconds = min(duration, RENDER_MAX_WINDOW_SECONDS)
        window_frames = round(window_seconds * RENDER_FPS)
        if action is not None and 0 < (action.frame_range[1] - action.frame_range[0]) < window_frames:
            loop_action_natively(action)

        scene.render.fps = RENDER_FPS
        scene.frame_start = 1
        scene.frame_end = window_frames

        # Real container format, not the still-image WebP `out_path` was
        # named for -- an animated preview is a real video now, encoded
        # directly by Blender itself (WebM/VP9, the exact settings Luna
        # specified directly), not a PNG-per-frame sequence hand-stitched
        # into an animated WebP via Pillow afterward. That earlier PNG+PIL
        # approach worked but was a real, avoidable extra step-and-a-half:
        # Blender's own animation renderer writes the finished video in one
        # native call, with no intermediate files on disk at all.
        final_out_path = str(Path(out_path).with_suffix(".webm"))
        ifs = scene.render.image_settings
        ifs.media_type = "VIDEO"
        ifs.file_format = "FFMPEG"
        scene.render.ffmpeg.format = "WEBM"
        scene.render.ffmpeg.codec = "WEBM"  # VP9 -- the only codec Blender pairs with the WEBM container
        scene.render.ffmpeg.constant_rate_factor = "MEDIUM"
        scene.render.ffmpeg.ffmpeg_preset = "GOOD"
        scene.render.ffmpeg.audio_codec = "NONE"  # no scene audio exists; skip encoding a silent track
        scene.render.filepath = final_out_path
        scene.render.use_file_extension = False

        # One native call, not a manual per-frame Python loop -- Blender's
        # own animation renderer steps every frame itself and fires
        # bpy.app.handlers.frame_change_pre exactly like scene.frame_set()
        # does (confirmed empirically: 101 handler calls recorded for a
        # 100-frame animation=True render, real curve values changing every
        # one of them), so husk_blender_geoset_mask's own registered
        # handlers (from apply_texture_transform_animation/
        # apply_tint_fade_animation above) need no special-casing here.
        # Confirmed real output too, not just "didn't crash": ffprobe on a
        # real rendered file reports codec_name=vp9, the requested
        # resolution, and r_frame_rate=24/1 exactly.
        bpy.ops.render.render(animation=True)
        anim_note = (f", animated ({duration:.2f}s native, {window_frames} frames at "
                     f"{RENDER_FPS}fps over {window_seconds:.2f}s, WebM/VP9)")

    # Real glTF playback (skeletal poses natively, husk's own extras-driven
    # curves via tools/live_gallery/server.py's own future JS-side handling)
    # needs the actual .glb, not just this script's own baked preview --
    # saved as the preview's own sibling (same basename, .glb in place of
    # .webp/.webm) so the live gallery's interactive three.js viewer, which
    # derives its model URL the identical way, finds it with no extra
    # wiring. Prompted directly (2026-08-13): "given that they are next to
    # their export webp in the corpus reports tree."
    glb_path = Path(final_out_path).with_suffix(".glb")
    shutil.copyfile(in_glb, glb_path)

    extra = f", {fixed_additive} additive material(s) rebuilt" if fixed_additive else ""
    if multiply_blended:
        extra += f", {multiply_blended} Mod/Mod2x material(s) multiply-composited"
    if multi_layer_fixed:
        extra += f", {multi_layer_fixed} multi-texture-layer material(s) combined"
    if envmap_fixed:
        extra += f", {envmap_fixed} env-mapped material(s) wired"
    if billboards_aligned:
        extra += f", {billboards_aligned} billboard bone(s) aligned to camera"
    print(f"OK rendered {len(mesh_objs)} mesh object(s), bbox radius {radius:.3f}{extra}{anim_note} "
          f"-> {final_out_path} (+ {glb_path})")


if __name__ == "__main__":
    main()
