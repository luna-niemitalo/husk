# WMO shaders (`uber`, `material3_wmo_*`)

This build has no `MapObj*` shaders. WMO materials are drawn by `pixel/dx_5_0/uber`, which
selects the material at runtime from a constant. `material3_wmo_ps` is a one-texture
container with the same permutation key. Its role next to `uber` is not established.

## Pixel slot key (`uber` and `material3_wmo_ps`, 128 slots, 64 compiled, 26 programs each)

Both containers have the same slot partition. The bits are a subset of the combiner key
(`combiners.md`) with the same code behind them:

| Bit | Combiner bit | Effect |
|---|---|---|
| 0 | 2 | Clustered forward lighting (`cb4`, `t21`, `t22`) instead of the light buffer `t11` |
| 1 | 3 | Shadow receiving: screen-space mask `t12`, or cascaded shadow map `t6` with bit 0 |
| 2 | 4 | Depth/normal prepass. All slots with it collapse to two programs (4 and 9) |
| 3 | 6 | Screen-door dither discard (same `sin(dot(v0, (12.9898, 78.233)))` hash) |
| 4 | 8 | Only with bit 0: discard near-invisible fragments, view depth to `o1.x` |
| 5 | 9 | Set in every compiled slot |
| 6 | 10 | Simple clamped lighting |

## Material switch (`uber`, base program slot 32 / blob 0, 1672 lines)

`cb2[4].x` holds the material ID. A `switch` on it chooses the texture combine and fills
three values: diffuse colour, emissive colour, and alpha source. A second switch on the same
value chooses the alpha-test texture inside the MSAA coverage loop. The rest (lighting,
fog, froxel volume) is the shared combiner code.

Names are from the wowdev.wiki WMO page (MOMT shader table, build 26522, fetched
2026-10-09). The behaviour column is from the asm. The UV sets are `uv0` = `v7.xy`,
`uv1` = `v7.zw`, `uv2` = `v8.xy` (the vertex shader generates them; see below).

| ID | Wiki name | Behaviour in `uber` |
|---|---|---|
| 0 | Diffuse | `t0`, alpha from `t0.a` |
| 1 | Specular | `t0` rgb, alpha 1 |
| 2 | Metal | Same code as 1 |
| 3 | Env | `t0`; emissive `t0.a * t1(uv1)` |
| 4 | Opaque | `t0` rgb, alpha 1 |
| 5 | EnvMetal | `t0`; emissive `t0.rgb * t0.a * t1(uv1)` |
| 6 | TwoLayerDiffuse | `lerp(t0, t1, t1.a)`, then lerp back toward `t0` by `v3.w` |
| 7 | TwoLayerEnvMetal | `lerp(t1, t0, v3.w)`; emissive `mix.rgb * mix.a * t2(uv2)` |
| 8 | TwoLayerTerrain | `lerp(t1, t0, v3.w)` |
| 9 | DiffuseEmissive | `t0`; emissive `t1.rgb * t1.a * v3.w` |
| 10 | waterWindow | No output (wiki: drawn by `FFXWaterWindow`) |
| 11 | MaskedEnvMetal | `t0`, `t1`, `t2`: `2*t0*t1` blended toward `t2` by `saturate(t2.a * v3.w)`, then by `t0.a` |
| 12 | EnvMetalEmissive | Emissive `t0.rgb*t0.a*t1 + t2.rgb*t2.a*v3.w` |
| 13 | TwoLayerDiffuseOpaque | `lerp(t1, t0, v3.w)`, alpha 1 |
| 14 | submarineWindow | No output (wiki: drawn by `FFXSubmarineWindow`) |
| 15 | TwoLayerDiffuseEmissive | Two-layer blend; emissive `t1.rgb * t1.a * (1 - v3.w)` |
| 16 | DiffuseTerrain | Same code as 0 |
| 17 | AdditiveMaskedEnvMetal | `2*t0*t1 + t2 * saturate(t2.a * v3.w)`, blended by `t0.a` |
| 18 | TwoLayerDiffuseMod2x | Two-layer blend (as 6), then `* t2(uv2) * 2` |
| 19 | TwoLayerDiffuseMod2xNA | `lerp(t0, 2*t0*t1, v3.w)` |
| 20 | TwoLayerDiffuseAlpha | As 18, with `t2.a` in place of `v3.w` |
| 21 | Lod | `t0` diffuse. `2 * t1` replaces the baked vertex-lighting term; `t2` is emissive |
| 22 | Parallax | Height `t17`, tangent frame from screen derivatives, parallax-offset fetches of `t3`/`t4`, blended with `t2`, and with `t0` by `v3.w` when `v3.w > 0` |
| 23 | (wiki: DF and later) | 4-layer height blend: layers `t1`–`t4`, heights `t17`–`t20` (min 0.004), weights `v9.xyz` with `1 - sum` for the first. Same sharpen-and-normalize as terrain. Emissive from a sphere-mapped `t0`; colour blended toward `cb2[5]` by `v9.w` |
| 24 | Not on the wiki | EnvMetal (5) with a separate opacity texture: alpha and alpha test from `t2.a`. See below |
| 34583 | — | Sentinel: no colour, alpha 1 |

### Material 23 in detail

Material 23 is the only `uber` material that blends four layers. Its code differs between
the two lighting paths:

| Programs | Slot bit 6 | Case-23 body |
|---|---|---|
| 0–3, 5–8, 10–13 | 0 (full lighting) | 50 lines in the normal path, 35 in the MSAA path: layer blend, tint, reflection |
| 14–25 | 1 (simple lighting) | 31–35 lines: layer blend and tint only, no reflection term |
| 4, 9 (prepass) | — | Alpha 1, no alpha test |

**Vertex side.** When `cb2[0].x == 23` the vertex shader skips UV generation. It passes
the four UV attributes `v4`–`v7` through unchanged to `o7.xy`, `o7.zw`, `o8.xy`, `o8.zw`,
with no `cb1[0..3]` transform. It also passes the 4-component attribute `v8` unchanged to
`o9`. No other material reads `v8`.

**Pixel side**, normal path of program 0 (`asm/0000.asm`, the `case l(23)` after the
`switch cb2[4].x` that follows the MSAA branch):

```
uv1 = v7.xy  uv2 = v7.zw  uv3 = v8.xy  uv4 = v8.zw       // uv1/uv2 replaced by the sphere map when cb0[5].y > 0
W   = (v9.x, v9.y, v9.z, 1 - saturate(v9.x + v9.y + v9.z))
L_i = t_i(uv_i)                     i = 1..4, sampler s_i, rgba
h_i = max(t_(16+i)(uv_i).a, 0.004)  height in the alpha of t17..t20, same sampler s_i
p_i = W_i * h_i
w_i = p_i * (1 - saturate(max(p) - p_i))
w  /= sum(w)
C   = sum(w_i * L_i)                // rgba
diffuse  = lerp(C.rgb, cb2[5].rgb, v9.w)
emissive = t0(sphereUV(reflect(view, N))).rgb * C.rgb * C.a     // full-lighting programs only
alpha    = 1
```

After the switch:
- `diffuse` is multiplied by the vertex colour `v2 * 2` and lit by the shared code
  (hemispheric ambient, sun, light buffer or clustered lights, `cb7`, fog).
- `emissive` is added after lighting. It is scaled by `saturate(v5.z * cb1[8].x + cb1[8].y)`,
  a view-depth fade. Materials 3, 5, 7, 22, 23 and 24 enable this fade; the others don't.
- In lighting mode 2 the baked vertex lighting `v3.xyz` is added unscaled. Materials
  0–9, 11–13, 15–20 and the default case scale it by `cb8[8].w`. 21 replaces it with
  `2 * t1`. 10, 14, 22, 24 and the sentinel leave it unscaled, as 23 does.

The weight sharpening is the same algorithm as terrain's height blend (`terrain.md`),
without the per-layer height scale and offset. There is one other difference: here the
fourth layer takes the remainder weight `1 - sum`, while terrain gives it to the first.

The MSAA path's 35-line body computes the same thing. It reuses the sphere-map UV
computed before its switch instead of recomputing it inside the case.

**What the data side supplies** (wowdev.wiki WMO page, fetched 2026-10-09):
- `MOC2` (DF and later) is a second BGRA vertex-colour set, "used in math for PARALLAX
  and UNK_DF_SHADER_23". That fits `v8` → `v9`: `xyz` layer weights, `w` tint amount
  toward `cb2[5]`. Nothing in the shaders names the attribute, so the match is inferred.
- DF supports up to 4 `MOTV` UV sets, which fits the four raw UVs.
- For shader 23, `MOMT` can carry extra texture file IDs in `color_2`, `flags_2` and
  `runTimeData`. Those are the textures bound to `t0`–`t4` and `t17`–`t20`, but the
  shaders can't show which field fills which slot.
- `cb2[5]`, the tint colour, has no counterpart on the wiki page. It may come from the
  material's colour fields; that is unconfirmed.

### Material 24 in detail

Every program carries the same recipe. The vertex shader has no case for 24, so its UV
sets come from the normal `cb2[1]` modes, like every other material except 23.

| Path | Code |
|---|---|
| Colour, full lighting (programs 0–3, 5–8, 10–13) | `diffuse = t0(uv0).rgb`; `emissive = t0.rgb * t0.a * t1(uv1).rgb`; `alpha = t2(uv0).a`; emissive depth fade enabled |
| Colour, simple lighting (programs 14–25) | `diffuse = t0(uv0).rgb`; `alpha = t2(uv0).a`; no emissive |
| MSAA coverage loop | Per-sample alpha test on `t2.a` at the sample-evaluated `uv0` |
| Prepass (programs 4, 9) | Alpha test on `t2.a` at `v6.xy`, the prepass UV |

`t2` is sampled with sampler `s1`, the sampler `t1` uses, not `s2`.

Compared with 5 (EnvMetal), the colour and emissive math is identical. Three things differ:

| | 5 EnvMetal | 24 |
|---|---|---|
| Alpha | 1 | `t2.a` |
| Alpha test | None (alpha-test case returns 1) | `t2.a` against 128/255 in all three alpha paths |
| Baked vertex lighting in lighting mode 2 | Scaled by `cb8[8].w` | Unscaled |

The shaders alone show 24 as an env-metal material with a cut-out mask from a third
texture. No public source found names it (wowdev.wiki's table stops at 23; a web search
on 2026-10-09 found nothing).

`cb2[4].y == 25` adds a secondary effect: a screen-space outline. It loads view normals
from `t13` at the pixel and four neighbours offset by `cb6[0] * cb2[8].z`. Where the normals
diverge past `cb2[8].y` it writes the outline colour `cb2[7].rgb * cb2[7].w`. Otherwise it
blends the emissive toward `cb2[6].rgb` by `cb2[6].w`. Pixels beyond the depth range
`cb2[8].x`/`.w` (compared with `v1.z`) are skipped.

Lighting differences from the combiners: in lighting mode 2 (`cb0[24].y`), the baked vertex
lighting `v3.xyz` scaled by `cb8[8].w` is added to the ambient term, scaled by
`saturate(v4.w + v5.w)`.

## Vertex shader (`vertex/dx_5_0/uber`, 16 slots, 8 programs)

`material3_wmo_vs` has the same slot partition and nearly the same code. It writes `o3`
and `o4` in full where `uber` writes `o3.x` and `o4.xw`.

| Bit | Effect |
|---|---|
| 0 | Clip distance |
| 1 | Instancing: per-instance transforms in `cb1[1029]` indexed by `SV_InstanceID` |
| 2 | No effect |
| 3 | Reduced outputs for the prepass (fewer UV sets, no vertex colour) |

Constants: `cb1[5..7]` model→view, `cb4[0..3]` projection, `cb4[4..7]` a second
projection-like matrix (output `o1`; the pixel shader reads only `v1.z`), `cb1[0..3]` UV0
and UV1 transforms, `cb1[8]` per-draw values (`.z` → `o4.w`, `.xy` → `o10`), `cb2[0].x`
material ID, `cb2[1]` UV-generation modes, `cb2[1].w == 1` enables vertex colour.

| Output | Pixel input | Meaning |
|---|---|---|
| `o2.xyz` | `v2` | Vertex colour, or 0.5 when `cb2[1].w != 1`. The pixel shader doubles it |
| `o3.xyz` | `v3.xyz` | `2 *` vertex colour: the baked vertex-lighting term |
| `o3.w` | `v3.w` | Second vertex colour's alpha: the two-layer blend factor |
| `o4.w` | `v4.w` | `cb1[8].z`, added to `v5.w` to scale the baked lighting |
| `o5.xyz` | `v5.xyz` | View-space position |
| `o5.w` | `v5.w` | `1 - vertex alpha`; blends scene ambient/fog with the draw's own, the role `v3.w` has in the combiners |
| `o6.xyz` | `v6` | View-space normal |
| `o7`, `o8`, `o9` | `v7`–`v9` | Up to six UV sets. Each set's source is chosen by a mode in `cb2[1]`: 0 UV0 through `cb1[0..1]`, 1 UV1 through `cb1[2..3]`, 2 sphere map, 3 reflection, 4–8 further modes (not decoded). For material 23 the raw UVs `v4`–`v7` pass through and `o9` carries the layer weights from `v8` |

## Related

- `prepassmapobj` (pixel 2 slots, vertex 8 slots): not decoded.
- The other `material3_*` containers: see `material3.md`.

## Open

- What `material3_wmo_ps` is used for when `uber` covers the WMO materials.
- UV-generation modes 4–8.
- The sentinel 34583 (0x8717).
