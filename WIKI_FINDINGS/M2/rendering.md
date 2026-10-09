# wowdev.wiki findings — M2 rendering (combiners, blend, particles)

Current, correct facts only, read from the 12.1.0 client's own compiled
shaders (`tools/export_shaders.py`'s `dx_5_0` assembly; container format in
`../BLS.md`). Nothing here was checked against the running client, and
everything below is **verified** against shader code unless tagged
otherwise. Full evidence trail: `../../WIKI_FINDINGS_HISTORY.md` §19.

Wiki pages: `Pixel_shader_logic_for_mixing_colors.md` (**P** below),
`M2/Rendering.md` (**R**), `M2/.skin.md` (shader enums), `M2.md`
(particles).

---

## The combiner ID is the pixel-shader enum index

The client compiles only a few combiner containers. Most combiner formulas
are chosen at runtime by `switch cb1[0].x`, and the case labels are exactly
the wiki's pixel-shader enum indices (`M2/.skin.md`'s 8.0.1+ table):

| Container | Combiner IDs |
|---|---|
| `combiners_opaque` | 0 |
| `combiners_mod` | 1 |
| `combiners_uber_2_2` | 2–5, 8–11, 13, 14, 16–18, 20–24, 29 |
| `combiners_uber_2_2_no_mod_fog_alpha` | 6, 7, 12 |
| `combiners_uber_3_3` | 15, 19, 25, 27, 35 |
| `combiners_mod_dual_crossfade` | 26 |
| `combiners_mod_masked_dual_crossfade` | 28 |
| `guild`, `guild_noborder`, `guild_opaque` | 30, 31, 32 |
| `combiners_mod_depth` | 33 |
| `illum` | 34 |
| `combiners_mod_mod_depth` | not in the wiki enum: `t0·t1`, edge-faded |
| `litsphere_additive_opaque` | not in the wiki enum |

Each case produces three values, not one colour:

- **diffuse**: multiplied by the vertex/material colour (`meshResColor`),
  then lit;
- **emissive**: added *after* lighting, not multiplied by `meshResColor`;
- **alpha**: alpha-tested, then multiplied by `meshResColor.a`.

The wiki writes `meshResColor` into each formula. To compare, read the
table as "diffuse × mesh, + emissive", alpha "× mesh.a".

## Formulas

`t0`–`t3` are the first to fourth textures; `luma` weights are
`(0.30, 0.59, 0.11)`. ✓/✗: agrees/disagrees with that wiki page; "—": the
page has no formula.

| ID | Name | Diffuse | Emissive | Alpha | P | R |
|---|---|---|---|---|---|---|
| 0 | Opaque | `t0` | — | 1 | ✓ | ✓ |
| 1 | Mod | `t0` | — | `t0.a` | ✓ | ✓ |
| 2 | Opaque_Mod | `t0·t1` | — | `t1.a` | ✓ | ✓ |
| 3 | Opaque_Mod2x | `2·t0·t1` | — | `2·t1.a` | ✓ | ✓ |
| 4 | Opaque_Mod2xNA | `2·t0·t1` | — | 1 | ✓ | ✓ |
| 5 | Opaque_Opaque | `t0·t1` | — | 1 | ✓ | ✓ |
| 6 | Mod_Mod | `t0·t1` | — | `t0.a·t1.a` | ✓ | ✓ |
| 7 | Mod_Mod2x | `2·t0·t1` | — | `2·t0.a·t1.a` | ✓ | ✓ |
| 8 | Mod_Add | `t0` | `t1` | `t0.a + t1.a` | ✗ (a) | ✗ (a) |
| 9 | Mod_Mod2xNA | `2·t0·t1` | — | `t0.a` | ✓ | ✓ |
| 10 | Mod_AddNA | `t0` | `t1` | `t0.a` | ✓ | ✓ |
| 11 | Mod_Opaque | `t0·t1` | — | `t0.a` | ✗ (b) | ✓ |
| 12 | Opaque_Mod2xNA_Alpha | `lerp(2·t0·t1, t0, t0.a)` | — | 1 | ✗ (c) | ✓ |
| 13 | Opaque_AddAlpha | `t0` | `t1·t1.a` | 1 | ✓ | ✓ |
| 14 | Opaque_AddAlpha_Alpha | `t0` | `t1·t1.a·(1−t0.a)` | 1 | ✓ | ✗ (d) |
| 15 | Opaque_Mod2xNA_Alpha_Add | `lerp(2·t0·t1, t0, t0.a)` | `t2·t2.a·cb0[6].z` | 1 | — | — |
| 16 | Mod_AddAlpha | `t0` | `t1·t1.a` | `t0.a` | ✓ | — |
| 17 | Mod_AddAlpha_Alpha | `t0` | `t1·t1.a·(1−t0.a)` | `t0.a + t1.a·luma(t1)` | ✓ (e) | — |
| 18 | Opaque_Alpha_Alpha | `lerp(lerp(t0, t1, t1.a), t0, t0.a)` | — | 1 | ✓ | — |
| 19 | Opaque_Mod2xNA_Alpha_3s | `lerp(2·t0·t1, t2, t2.a)` | — | 1 | — | — |
| 20 | Opaque_AddAlpha_Wgt | `t0` | `t1·t1.a·cb0[22].y` | 1 | — | — |
| 21 | Mod_Add_Alpha | `t0` | `t1·(1−t0.a)` | `t0.a + t1.a` | — | — |
| 22 | Opaque_ModNA_Alpha | `lerp(t0·t1, t0, t0.a)` | — | 1 | — | — |
| 23 | Mod_AddAlpha_Wgt | `t0` | `t1·t1.a·cb0[22].y` | `t0.a` | — | — |
| 24 | Opaque_Mod_Add_Wgt | `lerp(t0, t1, t1.a)` | `t0·t0.a·cb0[22].x` | 1 | — | — |
| 25 | Opaque_Mod2xNA_Alpha_UnshAlpha | `(1−k)·lerp(2·t0·t1, t0, t0.a)`, `k = saturate(t2.a·cb0[6].z)` | `k·t2` | 1 | — | — |
| 26 | Mod_Dual_Crossfade | `lerp(lerp(t0, t1, saturate(cb0[6].y)), t2, saturate(cb0[6].z))`, rgba | — | from the same lerp | — | — |
| 27 | Opaque_Mod2xNA_Alpha_Alpha | `lerp(lerp(2·t0·t1, t2, t2.a), t0, t0.a)` | — | 1 | — | — |
| 28 | Mod_Masked_Dual_Crossfade | as 26 | — | as 26, × mask `t3` | — | — |
| 29 | Opaque_Alpha | `lerp(t0, t1, t1.a)` | — | 1 | — | — |
| 30 | Guild | `t0·lerp(cb0[10], t1·cb0[11], t1.a)`, then lerp toward `t2·cb0[12]` by `t2.a` | — | `t0.a` | — | — |
| 31 | Guild_NoBorder | as 30 without the `t2` step | — | `t0.a` | — | — |
| 32 | Guild_Opaque | as 30 | — | 1 | — | — |
| 35 | Mod_Mod_Mod_Const | `t0·t1·t2·cb0[6]`, rgba | — | from the product | — | — |

Corrections:

- **(a)** Both pages give `t1.a + t0.a·mesh.a`. The client computes
  `(t0.a + t1.a)·mesh.a`.
- **(b)** P drops `mesh.a`; the client multiplies `t0.a` by it, as R does.
- **(c)** The client doubles, as R does: `lerp(2·t0·t1, t0, t0.a)`. P has
  no `×2`. (wow.export's doubled formula is the right one.)
- **(d)** R uses `t0.a`; the client uses `(1 − t0.a)`, as P does.
- **(e)** P's formula is right, but its RGB line repeats the emissive term
  and has unbalanced parentheses. The client has the term once.

Notes:

- `*_Wgt` (20, 23, 24) scale their emissive by the per-draw `cb0[22].x`/
  `.y`. That it is the batch's texture weight is **inferred**.
- Guild (30–32): three per-draw colour constants `cb0[10..12]`; `t1` is
  the emblem, `t2` the border (`guild_noborder` drops both `t2` and
  `cb0[12]`). Mapping them to `M2.md`'s texture types 15–17 (background,
  emblem, border colour) is **inferred**.
- Crossfade (26, 28): all three colour textures are sampled at one UV and
  the mask `t3` at the second, matching `M2/.skin.md`'s pairing with
  `VS_Diffuse_T1` / `VS_Diffuse_T1_T2`.
- `Combiners_Mod_Depth` (33) and `combiners_mod_mod_depth` square the
  alpha factor `edgeFade·cb0[5].x`; `edgeFade` comes from the
  `diffuse_edgefade_*` vertex shaders it pairs with:
  `pow(saturate((|N·V| − lo) / (hi − lo)), power)`.
- Every 8.0.1 vertex-shader name exists as a `vertex/dx_5_0` container
  except `Diffuse_T1_T1_T1` and `BW_Diffuse_T1`/`_T1_T2`.
- Environment-mapped UVs are chosen per draw by `cb0[5].y`: 0 = UV0/UV1,
  1 = env for `t0`, 2 = env for `t1`, ≥3 = env for both. The sphere-map
  formula matches `M2/.skin.md`'s "Environment mapping". That the client
  derives `cb0[5].y` from the shader ID's env bits is **inferred**.

## Alpha test, blend, fog, lighting (`M2/Rendering.md`)

| Wiki | Client shader | |
|---|---|---|
| AlphaKey: `alphaRef = 128/255 · elementAlpha`, compare `≥` | Discards combiner alpha < 0.50196, *before* the vertex alpha multiply — the same condition | agrees |
| Other blend modes use `alphaRef = 1/255` | Transparent variants discard alpha < 1/255 | agrees |
| Fog colour black for `Add` | Blend class 4 premultiplies colour by alpha and fogs toward 0 | agrees |
| Fog white for `Mod`, grey for `Mod2x` | Not in any shader; the client must set it through the fog constants | client-side |
| Lighting off for mode 0 | `cb0[24].y`: 0 = unlit (`combiner × mesh × 2`), 1 = lit, 2 = lit + a baked-light term; > 2 behaves as 0 | agrees; mode 2 is new |

`cb0[24].x` is **not** an `EGxBlend` value but an internal blend class:
1 forces output alpha to 1; 2 and 5 alpha-test at 128/255; 3 tests per
sample under MSAA (alpha-to-coverage); 4 is additive with premultiplied
alpha. Which `M2Blend` value maps to which class is not visible in the
shaders.

## Particles and ribbons (`M2.md`)

- **Multi-texture particles** (`particle_3colortex_3alphatex`):
  `t0·t1 × 2`, or `t0·t1·t2` in three-colour mode; `× 4` instead of `× 2`
  when a bit of `cb0[6].x` is set. The third texture's alpha always enters
  alpha. That those bits are the wiki's `MultitexUseModx4` and
  three-colour flags is **inferred**.
- **Ribbons** (`ribbon`): `t0 × colour`, plus a soft-depth variant.
- Particle `blendingType` 5–7 stays open: particle shaders take the same
  internal blend class as the combiners, and nothing in them maps raw
  `blendingType` values.
