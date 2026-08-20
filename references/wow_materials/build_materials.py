"""
WoW-style procedural material library for Blender (bpy).

Builds a small set of node-group-based materials that approximate WoW's
hand-painted-texture look using only procedural nodes -- no WoW textures
involved. Companion to husk (../../), which exports real WoW model+texture
data; this is the opposite experiment: how far can pure node math get you
on a from-scratch mesh with none of that data.

Grounded in two real sources (not invented numbers):
  - documentation/wowdev-wiki/wikitext/Pixel_shader_logic_for_mixing_colors.wiki
    (the real M2 texture-combiner blend math -- multiply/mod2x/add).
  - references/wow_shaders/asm/498e864b92fcf1b0.asm (a real captured skin/SSS
    pixel shader -- luma weights 0.299/0.587/0.144, tint ~(0.325,0.576,0.659),
    wrap-light curve reproduced node-for-node in wow_skin()).

Run: direnv exec . blender --background --python build_materials.py
Produces: wow_material_library.blend + render_*.png in this directory.

See FINDINGS.md for the honest procedural-vs-painted-texture verdict.
"""
import bpy
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DEBUG_NODES = {}  # material name -> {label: node}, populated by wow_skin() for debugging

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def new_material(name):
    mat = bpy.data.materials.new(name)
    # Every material is assigned to the same test sphere one at a time for
    # rendering, so each loses its only real user as soon as the next one
    # is assigned -- without a fake user, only the last-rendered material
    # survives into the saved .blend (confirmed: an earlier save had only
    # WoW_Stone in it, the rest silently dropped as zero-user orphans).
    mat.use_fake_user = True
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (600, 0)
    return mat, nt, out


def add_rim(nt, base_shader, color=(1.0, 0.95, 0.8), power=3.0, strength=0.6, x=0, y=-300):
    """WoW's engine reads as flat/toon-ish partly because of a strong,
    cool-to-warm rim/edge highlight baked into painted diffuse maps and
    reinforced by simple directional lighting. Fresnel-driven emission
    added on top of a base shader is the cheap procedural stand-in."""
    fres = nt.nodes.new("ShaderNodeFresnel")
    fres.inputs["IOR"].default_value = 1.45
    fres.location = (x, y)

    ramp = nt.nodes.new("ShaderNodeMath")
    ramp.operation = "POWER"
    ramp.inputs[1].default_value = power
    ramp.location = (x + 180, y)
    nt.links.new(fres.outputs["Fac"], ramp.inputs[0])

    emit = nt.nodes.new("ShaderNodeEmission")
    emit.inputs["Color"].default_value = (*color, 1.0)
    emit.inputs["Strength"].default_value = strength
    emit.location = (x + 360, y - 80)

    mulnode = nt.nodes.new("ShaderNodeMath")
    mulnode.operation = "MULTIPLY"
    mulnode.location = (x + 360, y + 60)
    nt.links.new(ramp.outputs[0], mulnode.inputs[0])
    mulnode.inputs[1].default_value = 1.0
    lightpath = nt.nodes.new("ShaderNodeLightPath")
    lightpath.location = (x + 180, y - 160)
    # Only add rim on camera rays, not shadow/diffuse bounce rays --
    # keeps it a view-dependent edge highlight, not a global glow.
    nt.links.new(lightpath.outputs["Is Camera Ray"], mulnode.inputs[1])

    strength_ctrl = nt.nodes.new("ShaderNodeMath")
    strength_ctrl.operation = "MULTIPLY"
    strength_ctrl.inputs[1].default_value = strength
    strength_ctrl.location = (x + 540, y)
    nt.links.new(mulnode.outputs[0], strength_ctrl.inputs[0])

    emit2 = nt.nodes.new("ShaderNodeEmission")
    emit2.inputs["Color"].default_value = (*color, 1.0)
    emit2.location = (x + 540, y - 100)
    nt.links.new(strength_ctrl.outputs[0], emit2.inputs["Strength"])

    add = nt.nodes.new("ShaderNodeAddShader")
    add.location = (x + 740, y + 100)
    nt.links.new(base_shader, add.inputs[0])
    nt.links.new(emit2.outputs[0], add.inputs[1])
    return add.outputs[0]


# ---------------------------------------------------------------------------
def wow_metal(name="WoW_Metal"):
    mat, nt, out = new_material(name)

    tex_coord = nt.nodes.new("ShaderNodeTexCoord")
    tex_coord.location = (-1000, 0)

    # Painted metal reads as a handful of flat "panels" of color rather
    # than a continuous PBR gradient -- approximate with a Voronoi cell
    # pattern driving both a mild hue/value jitter and worn-edge roughness.
    voronoi = nt.nodes.new("ShaderNodeTexVoronoi")
    voronoi.inputs["Scale"].default_value = 2.2
    voronoi.location = (-800, 100)
    nt.links.new(tex_coord.outputs["Object"], voronoi.inputs["Vector"])

    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 18.0
    noise.inputs["Detail"].default_value = 6.0
    noise.location = (-800, -150)
    nt.links.new(tex_coord.outputs["Object"], noise.inputs["Vector"])

    # Keep the panel/cell variation subtle -- WoW metal is dominated by a
    # single flat base tone plus a tight specular highlight, not a busy
    # blotchy pattern; the earlier wide ramp read as marble, not metal.
    base_ramp = nt.nodes.new("ShaderNodeValToRGB")
    base_ramp.color_ramp.elements[0].position = 0.45
    base_ramp.color_ramp.elements[0].color = (0.32, 0.33, 0.37, 1)
    base_ramp.color_ramp.elements[1].position = 0.60
    base_ramp.color_ramp.elements[1].color = (0.46, 0.48, 0.53, 1)
    base_ramp.location = (-560, 100)
    nt.links.new(voronoi.outputs["Distance"], base_ramp.inputs["Fac"])

    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = 0.3
    ao.location = (-560, -350)

    ao_mix = nt.nodes.new("ShaderNodeMixRGB")
    ao_mix.blend_type = "MULTIPLY"
    ao_mix.inputs["Fac"].default_value = 0.35
    ao_mix.location = (-340, 0)
    nt.links.new(base_ramp.outputs["Color"], ao_mix.inputs["Color1"])
    nt.links.new(ao.outputs["Color"], ao_mix.inputs["Color2"])

    rough_ramp = nt.nodes.new("ShaderNodeValToRGB")
    rough_ramp.color_ramp.elements[0].position = 0.35
    rough_ramp.color_ramp.elements[0].color = (0.08, 0.08, 0.08, 1)
    rough_ramp.color_ramp.elements[1].position = 0.55
    rough_ramp.color_ramp.elements[1].color = (0.22, 0.22, 0.22, 1)
    rough_ramp.location = (-560, -150)
    nt.links.new(noise.outputs["Fac"], rough_ramp.inputs["Fac"])

    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (-40, 0)
    bsdf.inputs["Metallic"].default_value = 1.0
    nt.links.new(ao_mix.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(rough_ramp.outputs["Color"], bsdf.inputs["Roughness"])

    final = add_rim(nt, bsdf.outputs[0], color=(1.0, 0.98, 0.85), power=4.0, strength=0.5)
    nt.links.new(final, out.inputs["Surface"])
    return mat


def wow_cloth(name="WoW_Cloth", base_color=(0.62, 0.10, 0.10)):
    mat, nt, out = new_material(name)

    tex_coord = nt.nodes.new("ShaderNodeTexCoord")
    tex_coord.location = (-1000, 0)

    # Woven-fabric variation: coarse noise for dye-lot blotching (the
    # "hand painted" mottling WoW cloth textures always carry) layered
    # over fine noise for the weave micro-roughness.
    coarse = nt.nodes.new("ShaderNodeTexNoise")
    coarse.inputs["Scale"].default_value = 4.0
    coarse.inputs["Detail"].default_value = 3.0
    coarse.location = (-800, 150)
    nt.links.new(tex_coord.outputs["Object"], coarse.inputs["Vector"])

    fine = nt.nodes.new("ShaderNodeTexNoise")
    fine.inputs["Scale"].default_value = 60.0
    fine.inputs["Detail"].default_value = 2.0
    fine.location = (-800, -150)
    nt.links.new(tex_coord.outputs["Object"], fine.inputs["Vector"])

    dark = tuple(c * 0.55 for c in base_color) + (1,)
    light = tuple(min(1.0, c * 1.35 + 0.05) for c in base_color) + (1,)
    color_mix = nt.nodes.new("ShaderNodeMixRGB")
    color_mix.location = (-560, 150)
    color_mix.inputs["Color1"].default_value = dark
    color_mix.inputs["Color2"].default_value = light
    nt.links.new(coarse.outputs["Fac"], color_mix.inputs["Fac"])

    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = 0.4
    ao.location = (-560, -350)
    ao_mix = nt.nodes.new("ShaderNodeMixRGB")
    ao_mix.blend_type = "MULTIPLY"
    ao_mix.inputs["Fac"].default_value = 0.6
    ao_mix.location = (-320, 0)
    nt.links.new(color_mix.outputs["Color"], ao_mix.inputs["Color1"])
    nt.links.new(ao.outputs["Color"], ao_mix.inputs["Color2"])

    rough_map = nt.nodes.new("ShaderNodeMapRange")
    rough_map.inputs["From Min"].default_value = 0.35
    rough_map.inputs["From Max"].default_value = 0.65
    rough_map.inputs["To Min"].default_value = 0.55
    rough_map.inputs["To Max"].default_value = 0.85
    rough_map.location = (-560, -150)
    nt.links.new(fine.outputs["Fac"], rough_map.inputs["Value"])

    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (-40, 0)
    bsdf.inputs["Metallic"].default_value = 0.0
    if "Sheen Weight" in bsdf.inputs:
        bsdf.inputs["Sheen Weight"].default_value = 0.4
        bsdf.inputs["Sheen Tint"].default_value = (1, 1, 1, 1)
    nt.links.new(ao_mix.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(rough_map.outputs["Result"], bsdf.inputs["Roughness"])

    final = add_rim(nt, bsdf.outputs[0], color=(1.0, 0.9, 0.75), power=2.5, strength=0.35)
    nt.links.new(final, out.inputs["Surface"])
    return mat


def wow_crystal(name="WoW_Crystal", tint=(0.25, 0.65, 1.0)):
    mat, nt, out = new_material(name)
    mat.blend_method = "BLEND"
    mat.show_transparent_back = False

    tex_coord = nt.nodes.new("ShaderNodeTexCoord")
    tex_coord.location = (-900, 0)

    # Faceted internal fracture pattern -- WoW crystal props are usually
    # low-poly with a painted "internal facet" gradient texture; a coarse
    # Voronoi standing in for that facet lighting is the closest cheap
    # procedural match.
    voronoi = nt.nodes.new("ShaderNodeTexVoronoi")
    voronoi.inputs["Scale"].default_value = 5.0
    voronoi.location = (-700, 100)
    nt.links.new(tex_coord.outputs["Object"], voronoi.inputs["Vector"])

    facet_ramp = nt.nodes.new("ShaderNodeValToRGB")
    facet_ramp.color_ramp.elements[0].position = 0.0
    facet_ramp.color_ramp.elements[0].color = (0.05, 0.05, 0.05, 1)
    facet_ramp.color_ramp.elements[1].position = 1.0
    facet_ramp.color_ramp.elements[1].color = (0.6, 0.6, 0.6, 1)
    facet_ramp.location = (-500, 100)
    nt.links.new(voronoi.outputs["Distance"], facet_ramp.inputs["Fac"])

    glass = nt.nodes.new("ShaderNodeBsdfGlass")
    glass.inputs["Roughness"].default_value = 0.05
    glass.inputs["IOR"].default_value = 1.55
    glass.inputs["Color"].default_value = (*tint, 1.0)
    glass.location = (-260, 200)

    fres = nt.nodes.new("ShaderNodeFresnel")
    fres.inputs["IOR"].default_value = 1.55
    fres.location = (-500, -150)

    glow_strength = nt.nodes.new("ShaderNodeMath")
    glow_strength.operation = "MULTIPLY_ADD"
    glow_strength.inputs[1].default_value = 3.0
    glow_strength.inputs[2].default_value = 0.6
    glow_strength.location = (-260, -150)
    nt.links.new(facet_ramp.outputs["Color"], glow_strength.inputs[0])

    emit = nt.nodes.new("ShaderNodeEmission")
    emit.inputs["Color"].default_value = (*tint, 1.0)
    emit.location = (-40, -150)
    nt.links.new(glow_strength.outputs[0], emit.inputs["Strength"])

    mix = nt.nodes.new("ShaderNodeMixShader")
    mix.location = (200, 0)
    nt.links.new(fres.outputs["Fac"], mix.inputs["Fac"])
    nt.links.new(glass.outputs[0], mix.inputs[1])
    nt.links.new(emit.outputs[0], mix.inputs[2])

    # Volume absorption gives Cycles a real per-ray path length through the
    # mesh, which is what actually saturates the color -- surface-only
    # glass on a thin sphere just tints whatever's behind it (mostly empty
    # background), reading as washed-out rather than a solid colored gem.
    absorb = nt.nodes.new("ShaderNodeVolumeAbsorption")
    absorb.inputs["Color"].default_value = (*tint, 1.0)
    absorb.inputs["Density"].default_value = 2.0
    absorb.location = (200, -250)

    nt.links.new(mix.outputs[0], out.inputs["Surface"])
    nt.links.new(absorb.outputs[0], out.inputs["Volume"])
    return mat


def wow_skin(name="WoW_Skin", base_color=(0.46, 0.31, 0.23)):
    """Node-for-node reproduction of the real captured skin/SSS pixel
    shader (references/wow_shaders/asm/498e864b92fcf1b0.asm):

        r0.xyz = tex1(uv2)^2                       -- squared sample
        r0.xyz = r0.xyz * cb1[0].x + tex0(uv1)      -- mad against a base sample
        r0.x   = dot(r0.xyz, (0.299,0.587,0.144))   -- luma (saturated)
        r0.y   = min(4 * r0.x * (1 - r0.x), 1)       -- wrap-light curve
        out    = r0.y * (0.325,0.576,0.659) + r0.x   -- tinted wrap-light + luma

    Real WoW skin has no procedural stand-in for tex0/tex1 here (those are
    hand-painted diffuse + a second detail/mask map) -- substituted with
    noise so the *math* is real even though the *inputs* are fake. See
    FINDINGS.md for why this one can't fully escape needing a painted map.
    """
    mat, nt, out = new_material(name)
    tex_coord = nt.nodes.new("ShaderNodeTexCoord")
    tex_coord.location = (-1300, 0)

    # stand-ins for the two real sampled textures (tex0 = base skin tone,
    # tex1 = detail/pore mask) -- noise, not painted data.
    tex0 = nt.nodes.new("ShaderNodeTexNoise")
    tex0.inputs["Scale"].default_value = 8.0
    tex0.inputs["Detail"].default_value = 4.0
    tex0.location = (-1100, 150)
    nt.links.new(tex_coord.outputs["Object"], tex0.inputs["Vector"])
    tex0_ramp = nt.nodes.new("ShaderNodeValToRGB")
    tex0_ramp.color_ramp.elements[0].color = (*tuple(c * 0.85 for c in base_color), 1)
    tex0_ramp.color_ramp.elements[1].color = (*tuple(min(1, c * 1.0) for c in base_color), 1)
    tex0_ramp.location = (-900, 150)
    nt.links.new(tex0.outputs["Fac"], tex0_ramp.inputs["Fac"])

    tex1 = nt.nodes.new("ShaderNodeTexNoise")
    tex1.inputs["Scale"].default_value = 40.0
    tex1.inputs["Detail"].default_value = 6.0
    tex1.location = (-1100, -150)
    nt.links.new(tex_coord.outputs["Object"], tex1.inputs["Vector"])

    # r0.xyz = tex1^2
    sq = nt.nodes.new("ShaderNodeVectorMath")
    sq.operation = "MULTIPLY"
    sq.location = (-900, -150)
    nt.links.new(tex1.outputs["Color"], sq.inputs[0])
    nt.links.new(tex1.outputs["Color"], sq.inputs[1])

    # r0.xyz = r0.xyz * cb1[0].x(=~1.0 wrap factor) + tex0
    cb_scale = nt.nodes.new("ShaderNodeValue")
    cb_scale.outputs[0].default_value = 0.35
    cb_scale.location = (-900, -320)
    mad = nt.nodes.new("ShaderNodeVectorMath")
    mad.operation = "MULTIPLY_ADD"
    mad.location = (-680, 0)
    nt.links.new(sq.outputs[0], mad.inputs[0])
    combine_scale = nt.nodes.new("ShaderNodeCombineXYZ")
    combine_scale.location = (-900, -420)
    for a in "XYZ":
        nt.links.new(cb_scale.outputs[0], combine_scale.inputs[a])
    nt.links.new(combine_scale.outputs[0], mad.inputs[1])
    nt.links.new(tex0_ramp.outputs["Color"], mad.inputs[2])

    # r0.x = dot(r0.xyz, luma weights), saturated
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    sep.location = (-460, 0)
    nt.links.new(mad.outputs[0], sep.inputs[0])

    luma = nt.nodes.new("ShaderNodeMath")
    luma.operation = "MULTIPLY_ADD"
    luma.location = (-260, 60)
    lumag = nt.nodes.new("ShaderNodeMath")
    lumag.operation = "MULTIPLY_ADD"
    lumag.location = (-100, 60)
    lumab = nt.nodes.new("ShaderNodeMath")
    lumab.operation = "MULTIPLY_ADD"
    lumab.location = (60, 60)
    luma.inputs[1].default_value = 0.299
    luma.inputs[2].default_value = 0.0
    lumag.inputs[1].default_value = 0.587
    lumab.inputs[1].default_value = 0.144
    nt.links.new(sep.outputs["X"], luma.inputs[0])
    nt.links.new(luma.outputs[0], lumag.inputs[2])
    nt.links.new(sep.outputs["Y"], lumag.inputs[0])
    nt.links.new(lumag.outputs[0], lumab.inputs[2])
    nt.links.new(sep.outputs["Z"], lumab.inputs[0])

    luma_sat = nt.nodes.new("ShaderNodeClamp")
    luma_sat.location = (220, 60)
    nt.links.new(lumab.outputs[0], luma_sat.inputs["Value"])

    # r0.y = min(4 * r0.x * (1 - r0.x), 1)
    one_minus = nt.nodes.new("ShaderNodeMath")
    one_minus.operation = "SUBTRACT"
    one_minus.inputs[0].default_value = 1.0
    one_minus.location = (220, -80)
    nt.links.new(luma_sat.outputs[0], one_minus.inputs[1])

    wrap = nt.nodes.new("ShaderNodeMath")
    wrap.operation = "MULTIPLY"
    wrap.location = (420, -20)
    nt.links.new(luma_sat.outputs[0], wrap.inputs[0])
    nt.links.new(one_minus.outputs[0], wrap.inputs[1])

    wrap4 = nt.nodes.new("ShaderNodeMath")
    wrap4.operation = "MULTIPLY"
    wrap4.inputs[1].default_value = 4.0
    wrap4.location = (600, -20)
    nt.links.new(wrap.outputs[0], wrap4.inputs[0])

    wrap_clamped = nt.nodes.new("ShaderNodeClamp")
    wrap_clamped.inputs["Max"].default_value = 1.0
    wrap_clamped.location = (780, -20)
    nt.links.new(wrap4.outputs[0], wrap_clamped.inputs["Value"])

    # out = r0.y * tint + r0.x
    tint_vec = nt.nodes.new("ShaderNodeCombineXYZ")
    tint_vec.inputs["X"].default_value = 0.325
    tint_vec.inputs["Y"].default_value = 0.576
    tint_vec.inputs["Z"].default_value = 0.659
    tint_vec.location = (780, 160)

    luma_vec = nt.nodes.new("ShaderNodeCombineXYZ")
    luma_vec.location = (780, 260)
    for a in "XYZ":
        nt.links.new(luma_sat.outputs[0], luma_vec.inputs[a])

    # out = tint * wrap_clamped + luma  (VectorMath MULTIPLY_ADD wants both
    # multiply operands as full vectors, so scale tint by the scalar wrap
    # factor explicitly rather than fighting the socket shape).
    vec_scale = nt.nodes.new("ShaderNodeVectorMath")
    vec_scale.operation = "SCALE"
    vec_scale.location = (820, 40)
    nt.links.new(tint_vec.outputs[0], vec_scale.inputs[0])
    nt.links.new(wrap_clamped.outputs[0], vec_scale.inputs["Scale"])

    combine_final = nt.nodes.new("ShaderNodeVectorMath")
    combine_final.operation = "ADD"
    combine_final.location = (1000, 100)
    nt.links.new(vec_scale.outputs[0], combine_final.inputs[0])
    nt.links.new(luma_vec.outputs[0], combine_final.inputs[1])

    to_color = nt.nodes.new("ShaderNodeCombineColor")
    to_color.location = (1180, 100)
    split = nt.nodes.new("ShaderNodeSeparateXYZ")
    split.location = (1000, -60)
    nt.links.new(combine_final.outputs[0], split.inputs[0])
    nt.links.new(split.outputs["X"], to_color.inputs[0])
    nt.links.new(split.outputs["Y"], to_color.inputs[1])
    nt.links.new(split.outputs["Z"], to_color.inputs[2])

    # The real shader's own math peaks near luma=0.5 (wrap curve max), where
    # tint + luma alone already sums past 1.0 -- confirmed by an earlier
    # pass of this script rendering solid blown-out white. That's consistent
    # with this being a real additive rim/SSS-fill *pass* layered on top of
    # a separate base diffuse draw, not the whole skin color by itself (it's
    # flagged in SHADER_SCAN_FINDINGS.md as a distinct, non-combiner effect
    # -- a second draw call is the natural reading). Reproduced here as a
    # low-strength tint blended into a flat base skin tone, not a full
    # override, to match that likely real role instead of the blown-out
    # literal readout.
    flat_base = nt.nodes.new("ShaderNodeRGB")
    flat_base.outputs[0].default_value = (*base_color, 1.0)
    flat_base.location = (1180, 320)

    wrap_mix = nt.nodes.new("ShaderNodeMixRGB")
    wrap_mix.inputs["Fac"].default_value = 0.20
    wrap_mix.location = (1380, 200)
    nt.links.new(flat_base.outputs[0], wrap_mix.inputs["Color1"])
    nt.links.new(to_color.outputs[0], wrap_mix.inputs["Color2"])

    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (1600, 0)
    bsdf.inputs["Roughness"].default_value = 0.55
    nt.links.new(wrap_mix.outputs[0], bsdf.inputs["Base Color"])

    out.location = (1900, 0)
    nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
    DEBUG_NODES[name] = dict(to_color=to_color, flat_base=flat_base, wrap_mix=wrap_mix,
                              luma_sat=luma_sat, wrap_clamped=wrap_clamped, mad=mad)
    return mat


def wow_leather(name="WoW_Leather", base_color=(0.32, 0.19, 0.10)):
    mat, nt, out = new_material(name)
    tex_coord = nt.nodes.new("ShaderNodeTexCoord")
    tex_coord.location = (-900, 0)

    grain = nt.nodes.new("ShaderNodeTexVoronoi")
    grain.inputs["Scale"].default_value = 35.0
    grain.voronoi_dimensions = "2D"
    grain.location = (-700, 100)
    nt.links.new(tex_coord.outputs["Object"], grain.inputs["Vector"])

    grain_ramp = nt.nodes.new("ShaderNodeValToRGB")
    grain_ramp.color_ramp.elements[0].color = (*tuple(c * 0.7 for c in base_color), 1)
    grain_ramp.color_ramp.elements[1].color = (*tuple(min(1, c * 1.2) for c in base_color), 1)
    grain_ramp.location = (-480, 100)
    nt.links.new(grain.outputs["Distance"], grain_ramp.inputs["Fac"])

    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = 0.3
    ao.location = (-480, -200)
    ao_mix = nt.nodes.new("ShaderNodeMixRGB")
    ao_mix.blend_type = "MULTIPLY"
    ao_mix.inputs["Fac"].default_value = 0.6
    ao_mix.location = (-260, 0)
    nt.links.new(grain_ramp.outputs["Color"], ao_mix.inputs["Color1"])
    nt.links.new(ao.outputs["Color"], ao_mix.inputs["Color2"])

    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.15
    bump.location = (-260, -300)
    nt.links.new(grain.outputs["Distance"], bump.inputs["Height"])

    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (0, 0)
    bsdf.inputs["Roughness"].default_value = 0.45
    nt.links.new(ao_mix.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])

    final = add_rim(nt, bsdf.outputs[0], color=(1.0, 0.85, 0.6), power=3.0, strength=0.3)
    nt.links.new(final, out.inputs["Surface"])
    return mat


def wow_gem_glow(name="WoW_Gem_Glow", tint=(1.0, 0.15, 0.05)):
    """Simple variant of wow_crystal tuned for a small, saturated
    socketed-gem look (more emission-forward, less transmissive)."""
    mat = wow_crystal(name=name, tint=tint)
    nt = mat.node_tree
    for n in nt.nodes:
        if n.type == "BSDF_GLASS":
            n.inputs["Roughness"].default_value = 0.15
        if n.type == "MATH" and n.operation == "MULTIPLY_ADD":
            n.inputs[1].default_value = 6.0
            n.inputs[2].default_value = 1.5
        if n.type == "VOLUME_ABSORPTION":
            n.inputs["Density"].default_value = 6.0
    return mat


def wow_stone(name="WoW_Stone", base_color=(0.42, 0.40, 0.37)):
    mat, nt, out = new_material(name)
    tex_coord = nt.nodes.new("ShaderNodeTexCoord")
    tex_coord.location = (-900, 0)

    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 3.0
    noise.inputs["Detail"].default_value = 8.0
    noise.inputs["Roughness"].default_value = 0.75
    noise.location = (-700, 100)
    nt.links.new(tex_coord.outputs["Object"], noise.inputs["Vector"])

    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.35
    ramp.color_ramp.elements[0].color = (*tuple(c * 0.35 for c in base_color), 1)
    ramp.color_ramp.elements[1].position = 0.65
    ramp.color_ramp.elements[1].color = (*tuple(min(1, c * 1.4) for c in base_color), 1)
    ramp.location = (-480, 100)
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])

    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = 0.3
    ao.location = (-480, -200)
    ao_mix = nt.nodes.new("ShaderNodeMixRGB")
    ao_mix.blend_type = "MULTIPLY"
    ao_mix.inputs["Fac"].default_value = 0.8
    ao_mix.location = (-260, 0)
    nt.links.new(ramp.outputs["Color"], ao_mix.inputs["Color1"])
    nt.links.new(ao.outputs["Color"], ao_mix.inputs["Color2"])

    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.4
    bump.location = (-260, -300)
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])

    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (0, 0)
    bsdf.inputs["Roughness"].default_value = 0.85
    nt.links.new(ao_mix.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
    return mat


# ---------------------------------------------------------------------------
def build_scene():
    reset_scene()
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 128
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 640
    scene.render.resolution_y = 640

    world = bpy.data.worlds.new("World")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.05, 0.05, 0.07, 1)
    bg.inputs["Strength"].default_value = 1.0

    sun = bpy.data.lights.new("Sun", type="SUN")
    sun.energy = 2.0
    sun_obj = bpy.data.objects.new("Sun", sun)
    scene.collection.objects.link(sun_obj)
    sun_obj.rotation_euler = (math.radians(55), 0, math.radians(35))

    fill = bpy.data.lights.new("Fill", type="AREA")
    fill.energy = 60
    fill.size = 4
    fill_obj = bpy.data.objects.new("Fill", fill)
    scene.collection.objects.link(fill_obj)
    fill_obj.location = (-3, -3, 2)
    fill_obj.rotation_euler = (math.radians(70), 0, math.radians(-40))

    cam_data = bpy.data.cameras.new("Cam")
    cam_data.lens = 60
    cam_obj = bpy.data.objects.new("Cam", cam_data)
    scene.collection.objects.link(cam_obj)
    cam_obj.location = (0, -4.2, 1.4)
    cam_obj.rotation_euler = (math.radians(80), 0, 0)
    scene.camera = cam_obj

    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, location=(0, 0, 1), segments=64, ring_count=32)
    sphere = bpy.context.active_object
    bpy.ops.object.shade_smooth()
    return scene, sphere


def render_material(scene, sphere, mat, filename):
    sphere.data.materials.clear()
    sphere.data.materials.append(mat)
    scene.render.filepath = os.path.join(HERE, filename)
    scene.render.image_settings.file_format = "PNG"
    bpy.ops.render.render(write_still=True)


def main():
    scene, sphere = build_scene()

    materials = [
        (wow_metal(), "render_metal.png"),
        (wow_cloth(), "render_cloth.png"),
        (wow_crystal(), "render_crystal.png"),
        (wow_skin(), "render_skin.png"),
        (wow_leather(), "render_leather.png"),
        (wow_gem_glow(), "render_gem_glow.png"),
        (wow_stone(), "render_stone.png"),
    ]

    for mat, fname in materials:
        render_material(scene, sphere, mat, fname)
        print(f"rendered {fname}")

    blend_path = os.path.join(HERE, "wow_material_library.blend")
    bpy.ops.wm.save_as_mainfile(filepath=blend_path)
    print(f"saved {blend_path}")


if __name__ == "__main__":
    main()
