"""Reference formulas for the combiner hunt (PIXEL_SHADER_FORMULAS_TODO.md).

Each formula is transcribed directly from `reference/wow.export/src/shaders/
m2.fragment.shader`'s `switch (u_pixel_shader)` (17 undocumented cases) or
`Pixel_shader_logic_for_mixing_colors.wiki` (the already-documented ones,
used here purely to validate the testing methodology against known-good
real matches before trusting it on the 17 unknowns). Diffuse-only: none of
these model the separate specular accumulation wow.export's shader also
computes for several cases -- see each entry's `note` for what's dropped
and why that's usually safe (most specular terms don't feed back into the
diffuse `mat_diffuse` this hunts for).

Every fn(tex, const) -> vec3 takes `tex` as a list of vec4s (rgb+alpha,
one per texture role in formula order) and `const` as a list of vec3s (one
per constant-tint role, only Guild-family formulas use these). Equivalence
testing (equivalence.py) fits an unmodeled overall tint/scale rather than
requiring these to predict absolute brightness -- see that module.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable


def mix3(a, b, t):
    return [a[i] * (1 - t) + b[i] * t for i in range(3)]


def mul3(a, b):
    return [a[i] * b[i] for i in range(3)]


def clamp01(v):
    return min(1.0, max(0.0, v))


@dataclass
class Formula:
    name: str
    n_tex: int
    n_const: int
    fn: Callable
    documented: bool
    note: str = ""


def _combiners_mod(tex, const):
    return tex[0][:3]


def _combiners_mod_mod(tex, const):
    return mul3(tex[0][:3], tex[1][:3])


def _mod2xna_alpha_wiki(tex, const):
    # wiki: meshColor * ((tex1 - tex1*tex2)*tex1.a + tex1*tex2)
    t1, t2, a = tex[0][:3], tex[1][:3], tex[0][3]
    return [(t1[i] - t1[i] * t2[i]) * a + t1[i] * t2[i] for i in range(3)]


def _mod2xna_alpha_wowexport(tex, const):
    # wow.export: meshColor * mix(tex1*tex2*2, tex1, tex1.a) -- disputed
    # factor-of-2 vs the wiki version above; see PIXEL_SHADER_FORMULAS_TODO.md.
    t1, t2, a = tex[0][:3], tex[1][:3], tex[0][3]
    return mix3([t1[i] * t2[i] * 2 for i in range(3)], t1, a)


def _mod2xna_alpha_add(tex, const):
    # case 15. diffuse-only: identical core to the wow.export Mod2xNA_Alpha
    # formula above -- the only difference (an added specular term using a
    # 3rd texture) isn't part of what's tested. A 2-texture match against
    # this formula is genuinely ambiguous with plain Mod2xNA_Alpha
    # (wow.export variant) at the diffuse-only level; only resolvable by
    # also confirming a 3rd texture is used for something specular-shaped
    # elsewhere in the same shader, not attempted here.
    return _mod2xna_alpha_wowexport(tex, const)


def _mod2xna_alpha_3s(tex, const):
    t1, t2, t3, a3 = tex[0][:3], tex[1][:3], tex[2][:3], tex[2][3]
    return mix3([t1[i] * t2[i] * 2 for i in range(3)], t3, a3)


def _opaque_addalpha_wgt(tex, const):
    # case 20. diffuse = mesh*tex1, weight only scales a separate specular
    # term -- diffuse itself needs no weight search.
    return tex[0][:3]


def _mod_add_alpha(tex, const):
    # case 21. diffuse = mesh*tex1; tex2/weight only affect discard+specular.
    return tex[0][:3]


def _modna_alpha(tex, const):
    t1, t2, a = tex[0][:3], tex[1][:3], tex[0][3]
    return mix3(mul3(t1, t2), t1, a)


def _mod_addalpha_wgt(tex, const):
    # case 23. diffuse = mesh*tex1, same shape as _mod_add_alpha's diffuse.
    return tex[0][:3]


def _opaque_mod_add_wgt(tex, const):
    # case 24. diffuse = mesh*mix(tex1,tex2,tex2.a); weight only scales specular.
    t1, t2, a = tex[0][:3], tex[1][:3], tex[1][3]
    return mix3(t1, t2, a)


def _mod2xna_alpha_unshalpha(tex, const):
    # case 25. Approximate: real formula scales glow_opacity by an external
    # weight.b this doesn't search for -- assumes weight.b == 1, i.e. treats
    # glow_opacity as plain tex3.a. Flagged, not silently precise.
    t1, t2, t3 = tex[0][:3], tex[1][:3], tex[2][:3]
    a1, a3 = tex[0][3], tex[2][3]
    glow = clamp01(a3)
    inner = mix3([t1[i] * t2[i] * 2 for i in range(3)], t1, a1)
    return [inner[i] * (1 - glow) for i in range(3)]


def _mod2xna_alpha_alpha(tex, const):
    t1, t2, t3 = tex[0][:3], tex[1][:3], tex[2][:3]
    a1, a3 = tex[0][3], tex[2][3]
    inner = mix3([t1[i] * t2[i] * 2 for i in range(3)], t3, a3)
    return mix3(inner, t1, a1)


def _opaque_alpha(tex, const):
    t1, t2, a = tex[0][:3], tex[1][:3], tex[1][3]
    return mix3(t1, t2, a)


def _opaque_alpha_alpha(tex, const):
    # documented: meshResColor * mix(mix(tex1,tex2,tex2.a), tex1, tex1.a)
    t1, t2 = tex[0][:3], tex[1][:3]
    a1, a2 = tex[0][3], tex[1][3]
    inner = mix3(t1, t2, a2)
    return mix3(inner, t1, a1)


def _guild(tex, const):
    # case 30/32 (Guild/Guild_Opaque -- identical color math, differ only
    # by an alpha-discard neither tested here). tex=[tex1,tex2,tex3],
    # const=[generic0,generic1,generic2].
    t1, t2, t3 = tex[0][:3], tex[1][:3], tex[2][:3]
    a2, a3 = tex[1][3], tex[2][3]
    g0, g1, g2 = const[0], const[1], const[2]
    inner = mix3(g0, mul3(t2, g1), a2)
    left = mul3(t1, inner)
    right = mul3(t3, g2)
    return mix3(left, right, a3)


def _guild_noborder(tex, const):
    t1, t2 = tex[0][:3], tex[1][:3]
    a2 = tex[1][3]
    g0, g1 = const[0], const[1]
    inner = mix3(g0, mul3(t2, g1), a2)
    return mul3(t1, inner)


def _mod_depth(tex, const):
    # case 33. Same core shape as Combiners_Mod -- a match here is
    # inherently ambiguous with plain Combiners_Mod at the diffuse-only
    # level; real distinction (a depth-only/alpha-test pass) isn't visible
    # from color math alone.
    return tex[0][:3]


FORMULAS: list[Formula] = [
    Formula("Combiners_Mod / Combiners_Opaque / Combiners_Mod2x", 1, 0, _combiners_mod, documented=True,
            note="rgb-only equivalence class: these 3 wiki formulas are all just "
                 "'tex1 times an unknown uniform scale' -- indistinguishable from "
                 "each other without also testing alpha, which this tester doesn't. "
                 "Combiners_Mod itself already confirmed matched by hand."),
    Formula("Combiners_Mod_Mod / Opaque_Opaque / Opaque_Mod2x / Opaque_Mod2xNA / "
            "Opaque_Mod / Mod_Opaque / Mod_Mod2x / Mod_Mod2xNA / Mod2x_Mod2x",
            2, 0, _combiners_mod_mod, documented=True,
            note="rgb-only equivalence class: 9 wiki formulas all reduce to "
                 "'tex1*tex2 times an unknown uniform scale' -- same reasoning as "
                 "the Combiners_Mod class above, just with 2 textures. "
                 "Combiners_Mod_Mod itself already confirmed matched by hand."),
    Formula("Combiners_Opaque_Alpha_Alpha", 2, 0, _opaque_alpha_alpha, documented=True,
            note="documented; double-mix shape, genuinely distinct from the two classes above"),
    Formula("Combiners_Opaque_Mod2xNA_Alpha (wiki)", 2, 0, _mod2xna_alpha_wiki, documented=True,
            note="documented, but disputed vs. the wow.export version below -- TODO step 2"),
    Formula("Combiners_Opaque_Mod2xNA_Alpha (wow.export)", 2, 0, _mod2xna_alpha_wowexport, documented=True,
            note="documented, but disputed vs. the wiki version above -- TODO step 2"),
    Formula("Combiners_Opaque_Mod2xNA_Alpha_Add", 2, 0, _mod2xna_alpha_add, documented=False,
            note="diffuse-only; ambiguous vs. plain Mod2xNA_Alpha at this level, see fn docstring"),
    Formula("Combiners_Opaque_Mod2xNA_Alpha_3s", 3, 0, _mod2xna_alpha_3s, documented=False),
    Formula("Combiners_Opaque_AddAlpha_Wgt", 1, 0, _opaque_addalpha_wgt, documented=False,
            note="diffuse-only, trivial shape -- ambiguous vs. Combiners_Mod/Mod_Depth"),
    Formula("Combiners_Mod_Add_Alpha", 1, 0, _mod_add_alpha, documented=False,
            note="diffuse-only, trivial shape -- ambiguous vs. Combiners_Mod/Mod_Depth"),
    Formula("Combiners_Opaque_ModNA_Alpha", 2, 0, _modna_alpha, documented=False),
    Formula("Combiners_Mod_AddAlpha_Wgt", 1, 0, _mod_addalpha_wgt, documented=False,
            note="diffuse-only, trivial shape -- ambiguous vs. Combiners_Mod/Mod_Depth"),
    Formula("Combiners_Opaque_Mod_Add_Wgt", 2, 0, _opaque_mod_add_wgt, documented=False),
    Formula("Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha", 3, 0, _mod2xna_alpha_unshalpha, documented=False,
            note="approximate: assumes external weight.b == 1, see fn docstring"),
    Formula("Combiners_Opaque_Mod2xNA_Alpha_Alpha", 3, 0, _mod2xna_alpha_alpha, documented=False),
    Formula("Combiners_Opaque_Alpha", 2, 0, _opaque_alpha, documented=False),
    Formula("Guild / Guild_Opaque", 3, 3, _guild, documented=False,
            note="color math identical for both names, differ only by a discard not tested here"),
    Formula("Guild_NoBorder", 2, 2, _guild_noborder, documented=False),
    Formula("Combiners_Mod_Depth", 1, 0, _mod_depth, documented=False,
            note="diffuse-only, trivial shape -- ambiguous vs. Combiners_Mod/AddAlpha_Wgt family"),
    # Not implemented: Combiners_Mod_Dual_Crossfade / Combiners_Mod_Masked_
    # Dual_Crossfade need an external weight.g/weight.b scalar as the
    # blend factor itself (not a texture alpha), which this tester's
    # role-search doesn't cover yet -- see equivalence.py's module
    # docstring. Illum (always-black diffuse) needs a different kind of
    # test entirely (constant-output check, not formula equivalence) --
    # also not implemented here.
]
