# Reconciliation with the husk documentation

This file compares three sources against each other (paths relative to the repo root):

1. **Shader code**: the 12.1.0 shader export in `example_exports/shaders/` and the other
   notes in this folder.
2. **Her documentation**:
   - `WIKI_FINDINGS/` (verified corrections to the wiki)
   - `documentation/wow-material-rendering.md`
   - `documentation/kek.md` (open questions)
3. **The wiki mirror** in `documentation/wowdev-wiki/md/`.

Proposed additions to her files are in the last section. The verified ones have since been
promoted into `WIKI_FINDINGS/` (`WIKI_FINDINGS_HISTORY.md` §19).

Confidence tags follow her convention:
- **verified**: checked against the shader code, with the evidence named.
- **inferred**: structurally justified, but a step depends on matching names or order.
- **hypothesis**: plausible, not confirmed.

Wiki line references point into the mirror (`md/...`).

## 1. M2 combiners

### The combiner ID is the pixel-shader enum — verified

The 12.1 client compiles only a few combiner containers. Most combiner formulas are
selected at runtime by `switch cb1[0].x` inside three containers. The case labels are
exactly the indices of the wiki's pixel-shader enum (`md/M2/.skin.md:409-486`):

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
| `combiners_mod_mod_depth` | not in the wiki enum (`kek.md`: "Mod_Mod_Depth, which is missing from the wiki's list") |
| `litsphere_additive_opaque` | not in the wiki enum |

Each case writes three values:
- a **diffuse** colour, multiplied by the vertex colour (`meshResColor`) and then lit;
- an **emissive** colour, added after lighting and not multiplied by the vertex colour;
- an **alpha**, which drives the 128/255 alpha test and is then multiplied by the vertex
  alpha.

The wiki's formulas include `meshResColor` explicitly. To compare them, read "diffuse ×
mesh, + emissive", with alpha "× mesh.a".

### Formulas from the code, compared with both wiki pages

`P` = `Pixel_shader_logic_for_mixing_colors.md`, `R` = `M2/Rendering.md`. ✓ means the code
agrees and ✗ means it disagrees. `t0`, `t1`, `t2` are the first, second and third texture.
`luma` uses weights `(0.30, 0.59, 0.11)`.

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
| 15 | Opaque_Mod2xNA_Alpha_Add | `lerp(2·t0·t1, t0, t0.a)` | `t2·t2.a·cb0[6].z` | 1 | empty | — |
| 16 | Mod_AddAlpha | `t0` | `t1·t1.a` | `t0.a` | ✓ | — |
| 17 | Mod_AddAlpha_Alpha | `t0` | `t1·t1.a·(1−t0.a)` | `t0.a + t1.a·luma(t1)` | ✓ (e) | — |
| 18 | Opaque_Alpha_Alpha | `lerp(lerp(t0, t1, t1.a), t0, t0.a)` | — | 1 | ✓ | — |
| 19 | Opaque_Mod2xNA_Alpha_3s | `lerp(2·t0·t1, t2, t2.a)` | — | 1 | empty | — |
| 20 | Opaque_AddAlpha_Wgt | `t0` | `t1·t1.a·cb0[22].y` | 1 | empty | — |
| 21 | Mod_Add_Alpha | `t0` | `t1·(1−t0.a)` | `t0.a + t1.a` | empty | — |
| 22 | Opaque_ModNA_Alpha | `lerp(t0·t1, t0, t0.a)` | — | 1 | empty | — |
| 23 | Mod_AddAlpha_Wgt | `t0` | `t1·t1.a·cb0[22].y` | `t0.a` | empty | — |
| 24 | Opaque_Mod_Add_Wgt | `lerp(t0, t1, t1.a)` | `t0·t0.a·cb0[22].x` | 1 | empty | — |
| 25 | Opaque_Mod2xNA_Alpha_UnshAlpha | `(1−k)·lerp(2·t0·t1, t0, t0.a)`, `k = saturate(t2.a·cb0[6].z)` | `k·t2` | 1 | empty | — |
| 26 | Mod_Dual_Crossfade | `lerp(lerp(t0, t1, saturate(cb0[6].y)), t2, saturate(cb0[6].z))` (rgba, one UV) | — | from the same lerp | empty | — |
| 27 | Opaque_Mod2xNA_Alpha_Alpha | `lerp(lerp(2·t0·t1, t2, t2.a), t0, t0.a)` | — | 1 | empty | — |
| 28 | Mod_Masked_Dual_Crossfade | as 26 | — | as 26, × mask `t3` (second UV) | empty | — |
| 29 | Opaque_Alpha | `lerp(t0, t1, t1.a)` | — | 1 | empty | — |
| 30 | Guild | `t0·lerp(cb0[10], t1·cb0[11], t1.a)`, then lerp toward `t2·cb0[12]` by `t2.a` | — | `t0.a` | empty | — |
| 31 | Guild_NoBorder | as 30 without the `t2` step | — | `t0.a` | empty | — |
| 32 | Guild_Opaque | as 30 | — | 1 | empty | — |
| 35 | Mod_Mod_Mod_Const | `t0·t1·t2·cb0[6]` (rgba) | — | from the product | not on P | — |

Notes on the ✗ marks:
- **(a)** Both pages give `t1.a + t0.a·mesh.a`. The code multiplies the whole sum by
  `mesh.a`: `(t0.a + t1.a)·mesh.a`.
- **(b)** P omits `mesh.a`. The code, like R, multiplies `t0.a` by it.
- **(c)** The code has the `×2`, like R. This settles `kek.md`'s open point: the client
  matches wow.export's doubled formula. **verified.**
- **(d)** R uses `t0.a`. The code uses `(1 − t0.a)`, like P.
- **(e)** P's RGB line has the emissive term twice and unbalanced parentheses. The code has
  it once.

### Further combiner findings

- **Weights.** The `*_Wgt` variants (20, 23, 24) scale their emissive by `cb0[22].x` or
  `cb0[22].y`. **verified** (code); presumably the batch's texture weight. *inferred*
- **Guild tint colours** (`kek.md`, "no known solution"). The three colours are per-draw
  constants `cb0[10]`, `cb0[11]` and `cb0[12]`. `t1` carries the emblem and `t2` the border:
  `guild_noborder` drops `t2` and the `cb0[12]` step. That fits texture types 15–18
  (background colour, emblem colour, border colour, emblem; `md/M2.md:1720-1723`) as
  `cb0[10]` = background, `cb0[11]` = emblem colour, `cb0[12]` = border colour. Formula
  **verified**; mapping **inferred**. The data source the client fills them from is not
  visible in the shaders.
- **Crossfade UVs.** The 8.0.1 table pairs `Mod_Dual_Crossfade` with `VS_Diffuse_T1` and
  `Mod_Masked_Dual_Crossfade` with `VS_Diffuse_T1_T2` (`md/M2/.skin.md:592`, `:596`). The
  code samples all three crossfade textures at one UV and the mask at the second UV.
  **verified**
- **`Combiners_Mod_Depth` (33).** It pairs with the edge-fade vertex shaders
  (`md/M2/.skin.md:552`, `:558`). The `_depth` containers square the alpha factor
  `v2.x·cb0[5].x`, where `v2.x` is the edge fade (`m2_vertex.md`). **verified**
- **Vertex shaders.** Every 8.0.1 vertex-shader name exists in the export except
  `Diffuse_T1_T1_T1` and `BW_Diffuse_T1`/`_T1_T2`. `Diffuse_T1_T1_T1_T2`, absent from the
  8.0.1 enum, is absent here too. **verified**
- **Env-map UVs.** The pixel shader picks environment mapping per draw from `cb0[5].y`:
  0 = UV0 and UV1; 1 = env for texture 0; 2 = env for texture 1; 3 or more = env for both.
  The sphere-map formula matches `md/M2/.skin.md:330-339`. This fits `kek.md`'s "UV-set
  choice comes only from the shader ID", if the client turns the ID's env bits
  (`md/M2/Loading.md:31-45`) into this constant. Code **verified**; client step **inferred**.

## 2. M2 blend, alpha test, fog and lighting

| Wiki / her claim | Code | Status |
|---|---|---|
| AlphaKey `alphaRef = 128/255 · element alpha` (Cata+), compare `≥` (`md/M2/Rendering.md:107-138`) | Discard where the combiner alpha < 0.50196. The vertex alpha is applied after the test, which is the same condition as comparing `alpha·elementAlpha` with `128/255·elementAlpha` | **verified** |
| Other blend modes use `alphaRef = 1/255` | The transparent forward variants (combiner bit 8) discard alpha < 1/255 (`3.92e-3`) | **verified** |
| Fog colour black for `Add` (fog mode 2) | Blend class 4 (`cb0[24].x == 4`) premultiplies colour by alpha and fogs toward 0 | **verified** |
| Fog white for `Mod`, grey for `Mod2x` (modes 3, 4) | No such code in any shader. The client must change the fog colour constants | **inferred** |
| Lighting mode 0 disables lighting; `Mod`/`Mod2x` and material flag 0x1 force it (`md/M2/Rendering.md:94-105`) | `cb0[24].y`: 0 = unlit (`colour = combiner × mesh × 2`), 1 = lit, 2 = lit plus a baked-light term; values above 2 are treated as 0 | **verified** (modes 0/1); mode 2 is new (§3) |
| `cb0[24].x` is `EGxBlend`? | No. It is an internal blend class: 1 forces output alpha to 1; 2 and 5 alpha-test at 128/255; 3 alpha-tests per sample under MSAA (alpha-to-coverage); 4 is additive with premultiplied alpha | **verified** (behaviour); mapping to `M2BLEND`/`EGxBlend` values not determined |

## 3. WMO (`uber`, `material3_wmo_*`)

| Wiki / her claim | Code | Status |
|---|---|---|
| Shader table 0–23 (`md/WMO.md:249-276`) | `uber` switches on `cb2[4].x` over 0–24. Behaviour matches every name, including 10 and 14 producing nothing (drawn by `FFXWaterWindow`/`FFXSubmarineWindow`) and 22 Parallax | **verified** (`wmo.md`) |
| "WMO surfaces are mostly lit by baked-in vertex colours, not dynamic lighting" (`wow-material-rendering.md` §2) | `uber` runs the full dynamic path: sun, hemispheric ambient, light buffer or clustered lights, shadows. MOCV enters as a diffuse multiplier and, in lighting mode 2, as an added ambient term | **disagrees** for this client. The claim describes the classic WotLK path (`md/WMO/Rendering.md:416` says Cataclysm changed it) |
| FixColorVertexAlpha sets MOCV alpha to 255 (exterior) or 0 (interior), "later used for blending between exterior and interior lighting in the shader" (`md/WMO/Rendering.md:332`) | The vertex shader writes `o5.w = 1 − MOCV.a`. The pixel shader lerps the scene light (`cb8`, day/night) toward a per-draw set `cb0[18..21]` by it, when `cb0[24].w` is set | **verified** |
| QueryLighting doubles the MOCV RGB, "not sure why?" (`md/WMO/Rendering.md:117-262`) | The vertex shader writes `o3.xyz = 2 × MOCV` as the baked-light term | **verified** |
| Unified path: the MOHD colour is subtracted from MOCV and "later added back in the lighting formula" (`md/WMO/Rendering.md:326`) | Lighting mode 2 adds the baked term to the ambient (scaled by `cb8[8].w`, except materials 10, 14 and 22–24) | **inferred**: mode 2 is the unified/interior path |
| Lighting modes unlit / ext lit / window lit / int lit (`md/WMO/Rendering.md:439-443`) | The shader has three modes (0, 1, 2). Ext/window/int differ only in which ambient and direct colours the client uploads, so they reach the shader as constants | **inferred** |
| Second MOCV (CVERTS2): "only the alpha values … are used (to blend the textures)" (`md/WMO.md:1151`) | The vertex shader passes vertex attribute `v3.w` through as `o3.w`. The two-layer materials blend their layers by it (6, 7, 8, 13, 18 and 19, for example) | **verified** (code) / **inferred** (attribute = MOCV2) |
| MOC2 "used in math for PARALLAX and UNK_DF_SHADER_23" (`md/WMO.md:1910`) | Material 23 reads the 4-component attribute `v8`: `xyz` layer weights, `w` tint toward `cb2[5]`. Parallax (22) does not read it in this build; its extra UV comes from the normal UV generator | **verified** for 23; **disagrees** for 22 in this build |
| Up to 3 MOTV (4 from DF) | Material 23 passes four raw UV attributes `v4`–`v7` | **verified** |
| Shader 23 textures come from `color_2`, `flags_2`, `runTimeData` | Material 23 binds 9 textures: `t0`–`t4`, `t17`–`t20` | consistent, field-to-slot mapping not visible |
| `MOGP.fogIds[4]` | The fog code loops over at most 4 fog records per draw (`umin 4`) | **verified** (shared with M2 and terrain) |
| MOM3 replaces MOMT with M3 `m3SI` materials (`md/WMO.md:337-349`); a `.mtl3lib` references `material3_mesh_vs.bls` (`wow-material-rendering.md` §3.3) | `material3_mesh_*` and `material3_wmo_*` are the material system for M3 models and for MOM3 WMOs. That answers the open question in `material3.md` | **inferred** (her verified FileDataID link plus the names) |

## 4. Terrain

| Wiki / her claim | Code | Status |
|---|---|---|
| Height blend pseudocode (`md/ADT/v18.md:1557-1592`) | Identical to the terrain height mode: layer 0 = `1 − Σ`, `pct = w·(h·heightScale + heightOffset)`, `pct·(1 − clamp(max − pct))`, normalise. `cb1[5]` = MTXP `heightScale`, `cb1[9]` = `heightOffset` | **verified** (closes `kek.md` "Terrain height-texture blending") |
| The excerpt's comment "pt_layerX: MCAL data" | The code samples `pt_layerX` as the diffuse layers | **wiki comment wrong** (the agent extract flagged it as well) |
| `metalBlend = w0.a + w1.a; specBlend = w2.a + w3.a` (same excerpt) | With both terrain options `y` and `z` set, the cubemap reflection is masked by `w0·a0 + w1·a1` and specular by `w2·a2 + w3·a3`: the excerpt exactly. With only `y`, layer 0 alone is the reflection mask and layers 1–3 feed specular | **verified**; explains the `y`/`z` asymmetry left open in `terrain.md` |
| Cube-map reflection (MCLY 0x400): "always affect the ground layer" (`md/ADT/v18.md:1037-1045`) | The reflection is masked by layer 0's weight and alpha | **verified** (closes `kek.md` "Terrain cube-map reflection", shader side) |
| MCCV, `× 2.0 because mccv goes from 0.0 to 1.0` | `v1.rgb × 2` | **verified** |
| MCSH static shadow map | `t4` behind `cb1[29].x` | **inferred** |
| MTCG: per-texture colour-grading LUT, ramp and `startDistance` (`md/ADT/v18.md:1604-1612`) | Option "Height+LUT": per-layer 32³ LUT `t26`–`t29`, blended in by a distance curve `t30`–`t33` sampled at `log2(distance)` | **inferred**: LUT = `colorGradingFdid`, curve = `colorGradingRampFdid` |
| Layer limit: 4, or 8 from Midnight ("client ignores any layer after the 8th"); her data has up to 8 | The terrain shader is compiled for 1, 2 or 4 groups of 4 layers, so up to 16 | **disagrees** on the shader side; the client cap at 8 may still hold |
| Map flags 0x4 "Weighted Blend", 0x800000 "Weighted Height Blend" (`md/DB/Map.md`) | The terrain blend modes: lerp chain, normalised weights, height-weighted | **hypothesis**: Weighted Blend = normalised, Weighted Height Blend = height mode |
| Pre-WoD formula `tex0·(1 − Σα) + Σ texᵢ·αᵢ` | The lerp-chain mode is a different formula (successive alpha blends). The per-layer `flags` option moves layers into a weighted-sum chain | **hypothesis**: the per-layer `flags` choose between the two classic formulas |
| `TerrainMaterial.m_shader`, `m_envMapPath` (`kek.md`, no known solution) | The env map is the cubemap `t5`. The shader field plausibly selects the blend-mode digit | **hypothesis** |

## 5. Liquids, sky, screen effects, particles, ribbons

| Wiki / her claim | Code | Status |
|---|---|---|
| LiquidMaterial classes Water (1, 3), Magma (2, 4), Mercury (5), Fog (10), Debug (8) (`md/DB/LiquidMaterial.md:23-41`) | Containers `water`/`procwater*`, `magma`/`procmagma`, `mercury`/`procmercury`, `liquidfog`, `liquiddebug` | **inferred** (names). `procswamp`, `procleyline*` and the `medium*` tier have no wiki counterpart |
| `Liquid::s_waterDetail`: 0 old water, 1 screen-space reflection, 2 dynamic reflection | `procwaterabove` digit `b`: 1 = sky texture only, 0 = projected reflection with sky fallback, 2 = projected reflection only | **hypothesis**: `b` 1/0/2 ↔ detail 0/1/2 |
| LightData ocean/river close and far colours, shallow/deep alphas | `procwaterabove` lerps `cb1[20]` → `cb1[21]` and uses `.w` as the blend over refraction | **hypothesis** |
| The sky cone is a mesh with vertex colours from LightData (`md/Day_night_cycle.md:285-320`) | `dnsky` colours come from `v1`, plus a sun glow and dither | **verified** |
| LightData 23 "Horizon Ambient Color", 24 "Ground Ambient Color" | Hemispheric ambient in `cb8[0..2]` (lerp across the up vector `cb8[10]`) | **inferred**: `cb8[0]` sky, `cb8[1]` horizon, `cb8[2]` ground |
| Colour-grading LUT shader (`md/DB/LightData.md:104-134`) | Same 1024×32 strip and slice lerp in `ffxcolorgrading` and combiner bit 1 | **verified** |
| FFXGlow `mix(screen, blur, blurAmount.z) + blur²·blurAmount.w` (`md/Rendering/ScreenEffects.md:185-225`) | `ffxglow` is this exactly (`cb1[0].z`, `cb1[0].w`) | **verified** |
| ScreenEffect swirling fog: PassFogSeed tint, PassPropagateFog `_2/255·0.9`, PassFogCombine `_3/100` | `ffxfogseed` = texture × colour; `ffxpropagatefog` subtracts `cb1[0].x` from alpha; `ffxfogcombine` uses `cb1[0].x`/`.y` | **inferred** |
| Unconscious → PassGlowFade | `ffxglowfade` = glow, then a lerp toward `cb1[0].rgb` by `cb1[1].x` | **inferred** |
| Multi-texture particles (`kek.md`, no known solution): "MultitexUseModx4 … instead of Modx2", "3 colors instead of 2" (`md/M2.md:1964-1997`) | `particle_3colortex_3alphatex`: `t0·t1` (and `·t2` in three-colour mode) × 2, or × 4 when the flag in `cb0[6].x` is set. The third texture's alpha always enters alpha | **verified** (formula); flag-to-bit mapping **inferred** |
| Ribbon shader (`kek.md`, no known solution) | `ribbon` = `t0 × colour`, with a soft-depth variant. `gpuribbon` is described in `particles.md` | **verified** |
| Particle `blendingType` 5–7 (`kek.md`) | Particle shaders take the same internal blend class as the combiners; nothing maps raw `blendingType` values | still open |

## 6. Shader containers (her `WIKI_FINDINGS/BLS.md`)

| Her claim | Code | Status |
|---|---|---|
| "Blizzard isn't shipping standard DXBC … no `DXBC`/`RDEF` magic anywhere inside" (`wow-material-rendering.md` §3.3) | The compressed GXSH stream holds standard DXBC containers once inflated (9,742 SM5 blobs). The parts are `ISGN`/`OSGN`/`SHEX` only: `RDEF` is stripped | **corrected**: DXBC is present; reflection names are not, so her conclusion stands |
| `header.nPermutations` is 40 for illum, meaning unknown | 40 in all 430 `0x1000E` DX50 files, 28 in the other five. A format constant | **verified** |
| Slot hash input unknown; not the DXBC checksum or MD5 of blob/DXBC | One hash per distinct program within a file, and different between DX50 and DX60 for every one of 27,743 compiled slots. So it identifies the compiled program, not the permutation. Also not MD5/SHA-1/SHA-256/BLAKE2/SHA3 of the DXBC, its shader code, or the whole block | **extended**; input still unknown |
| The 96-byte block header is opaque | Decoded fields below | **new** |

### Block header (96 bytes), as 24 little-endian `u32`

| Index | Content | Evidence (9,742 DX50 programs) |
|---|---|---|
| 0 | 3 | constant |
| 1 | DXBC size + 56 | all |
| 2 | 40 | constant |
| 5 | output register count | 9,693 match |
| 6 | instruction count | 7,657 exact |
| 7, 8, 9 | unknown small counts | — |
| 10 | 3 | constant |
| 12 | UAV count (0 outside compute) | 9,688 match |
| 14–15 | texture binding mask (`u64`) | 9,364 match |
| 16 | 0 except in compute; possibly a UAV mask | not verified |
| 18–19 | sampler binding mask (`u64`) | all |
| 20–21 | constant-buffer binding mask (`u64`) | all |
| 22 | DXBC size | all |
| 23 | 4 | constant |
| 3, 4, 11, 13, 17, 19, 21 | 0 | constant |

## 7. `kek.md` checklist

| `kek.md` item | Now |
|---|---|
| Opaque_Mod2xNA_Alpha: doubled formula "leans one way" | Settled: doubled (§1) |
| "Remaining wiki and wow.export combiner formulas" unvalidated | All IDs 0–35 have formulas from code (§1); five wiki formulas corrected |
| Mod_Mod_Depth missing from the wiki | Present as `combiners_mod_mod_depth`: `t0·t1`, edge-faded |
| 23 WMO shader types (from wow.export) | Behaviour of all 25 values from code (`wmo.md`) |
| M2 blend modes 0–7 | Alpha test, 1/255 threshold and Add fog verified in shader; Mod/Mod2x fog is client-side (§2) |
| Terrain height-texture blending | Verified (§4) |
| Particle blendingType 5–7 | Open |
| Multi-texture particle combine | Formula from code (§5) |
| Ribbon shader | From code (§5) |
| WMO shader 23 | From code (`wmo.md`), including MOC2 weights and 9 textures |
| Terrain cube-map reflection; TerrainMaterial Shader field | Shader side verified (§4); field meaning hypothesis |
| MPTX chunk | Not visible in shaders. The terrain shader supports 16 layers, so MPTX's extra predominant-texture factors serve the >4-layer case on the CPU side |
| How a liquid type picks its shader | Material class → container family by name (§5); exact per-type choice is client data |
| Eight M2 material flags, texture types 19–26 | Not visible in shaders |
| Guild tint colour source | Per-draw constants `cb0[10..12]`; mapping to types 15–17 inferred (§1) |
| UI, post-processing and sky shaders | Sky and post decoded (`sky.md`, `postprocess.md`); UI not |

## 8. Proposed additions to her files

These were suggestions. The `WIKI_FINDINGS/` ones are done (§19 there); the
`documentation/` and wiki-mirror ones are not.

- **`WIKI_FINDINGS/BLS.md`:** §6 here: DXBC present but `RDEF` stripped; `nPermutations`
  is a constant; slot hashes are per program and per API; the block-header table.
- **`documentation/wow-material-rendering.md` §3.3:** the "no DXBC magic" statement came
  from searching compressed bytes. §2's "WMO surfaces are mostly lit by baked vertex
  colours" describes the classic path, not 12.1 (§3 here).
- **`documentation/kek.md`:** the status column of §7.
- **Wiki-facing (`HUSK_AMENDMENTS` style), with code evidence:**
  - the five combiner formula corrections in §1;
  - the terrain excerpt comment;
  - the `metalBlend`/cubemap explanation;
  - FFXGlow confirmed;
  - the 12.1 combiner-ID dispatch through `cb1[0].x`.
