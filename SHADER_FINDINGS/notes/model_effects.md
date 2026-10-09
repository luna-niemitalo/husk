# M2 model effect shaders (512-slot pixel containers)

The export has no dedicated character shader. `avatar` and `skin` are M2 effect and
variant shaders like the others in this file. Character models are M2 models, so their
pixel shading most likely goes through the combiner family (`combiners.md`). That is an
inference: the shaders don't say which model types use which container. Two compute shaders
fit character work without naming it: `texturecompositing` (layer blend modes) and
`m2skincs` (GPU skinning); see `compute.md`.

Every container here uses 512 slots with 256 compiled. They fall into three keys.

| Key | Containers |
|---|---|
| A ("avatar key") | `avatar`, `gradientmask`, `litsphere_additive_transparent` (192 programs each) |
| B | `blueprint`, `edgeglow`, `shadowmonk`, `xrayhighlight` (128 programs each) |
| C | `skin` (74 programs) |

## Keys A and B

Bits 0–7 have the same meaning in both keys. Bit 8 differs.

| Bit | Effect | Combiner equivalent |
|---|---|---|
| 0 | Clustered forward lights. In key B only with bit 8 set | 2 |
| 1 | Shadows. In key B only with bit 8 set | 3 |
| 2 | Soft depth fade (`t10`) | 5 |
| 3 | Screen-door dither | 6 |
| 4 | With bit 0: discard plus view depth to `o1.x` | 8 |
| 5, 6 | Texture combine (below) | — |
| 7 | Set in every compiled slot | 9 |
| 8 | Key A: simple clamped lighting (combiner bit 10). Key B: turns lighting on; with it clear the program is unlit | — |

Bits 5 and 6 together choose the base texture combine. This was checked in `avatar` and
`shadowmonk`; the other containers declare the same textures for each value.

| Bits 6,5 | Textures | Combine |
|---|---|---|
| 00 | `t0` | `t0` |
| 01 | `t0`, `t1` (`uv1` = `v5.zw`) | `t0 * t1` |
| 10 | `t0`, `t1` | `2 * t0 * t1` |
| 11 | `t0`, `t1`, `t2` (`v6.xy`) | rgb `2 * t0 * t1 + t2`, alpha `saturate(2 * t0.a * t1.a + t2.a)` |

The rest of each program is the combiner forward pass (alpha test, light buffer or
clustered lights, `cb7` light, fog, froxel volume). The table below lists only what each
container adds to it.

| Container | Key | What it adds |
|---|---|---|
| `avatar` | A | Desaturate-and-tint: `lerp(c, luma(c) * cb0[6].rgb, 0.94)`. Blinn specular `pow(N·H, 18) * saturate(N·L) * cb0[7].rgb`, multiplied by texture alpha when `cb0[7].w` is 4 or 7 |
| `gradientmask` | A | The combiners' mask-and-palette recolour (mask `t15`, sRGB base `t17`, palette `t18` indexed by `cb0[6].rgb / 255`, enabled by `cb0[6].w`). Here the palette's alpha also multiplies the output alpha, including inside the MSAA coverage loop |
| `litsphere_additive_transparent` | A | Matcap: `t3` sampled at `v6.zw` (a view-normal UV from the vertex shader), times `cb0[6].rgb` and `t3.a`, added as emissive. Alpha is `luma(t0) * t0.a` |
| `blueprint` | B | Where `t0.a < 0.5`, outputs `t0` unchanged. Elsewhere: a screen-space outline from the view-normal buffer `t13` (pixel plus four neighbours offset by `cb6[0] * cb0[6].z`, threshold `cb0[6].y`) in colour `cb0[8].rgb * cb0[8].w`; otherwise `cb0[7].rgb * t0.a` with alpha 1. Same code as `uber`'s secondary effect 25 |
| `edgeglow` | B | Fresnel rim `pow(rim, cb0[6].w)` (or `pow(1 - rim, …)` when `cb0[7].x > 0`) times `cb0[6].rgb`. `cb0[7].y` scales alpha. `cb0[7].z` picks how the texture enters: 0 ignores it (colour `cb0[6].rgb`, alpha 1); 4 uses colour `cb0[6].rgb` with alpha 1 where `t0.rgb * t0.a` is bright and 0 where its squared length is below 0.1; other values scale colour and alpha by `t0.a` |
| `shadowmonk` | B | `c' = lerp(c, lerp(c, cb0[6].rgb, cb0[6].w) * luma(c), 0.89)`, plus a fresnel rim `× 4 × luma(c)` coloured `lerp(c, cb0[7].rgb, cb0[7].w)` |
| `xrayhighlight` | B | Nothing: the unlit base outputs `t0` with fog. Any x-ray look comes from pipeline state or the vertex shader |

The rim term in `edgeglow`, `shadowmonk` and `skin` is the same expression:
`rim = (1 - saturate(N·V))² + (1 - saturate(V·(N * (0.05, 0.05, 1))))²`.

## Key C (`skin`)

| Bit | Effect | Combiner equivalent |
|---|---|---|
| 0 | Clustered forward lights | 2 |
| 1 | Shadows | 3 |
| 2 | Depth/normal prepass | 4 |
| 3 | Specular with exponent `t2.a * 128`. Only takes effect with bits 2 and 8 clear | — |
| 4 | Texture transition (below) | — |
| 5 | `cb2` overlay (gradient, rim, plane fade) | 7 |
| 6 | Discard plus view depth to `o1.x` | 8 |
| 7 | Set in every compiled slot | 9 |
| 8 | Simple clamped lighting; also removes the emissive `t2` and the rim | 10 |

Base combine: `colour = lerp(2 * t0 * t1, t0, t0.a)`. `t1` (on `uv1`) modulates only where
`t0.a` is 0. Emissive is `t2`. When `cb0[6].x > 0` a rim tinted `(0.05, 0, 0.4)` and scaled
by `pow(rim, 0.6) * cb0[6].x` is added.

Bit 4, the texture transition:
- `d = max(t4(v6 * cb0[7].zw + cb0[7].xy + cb0[6].zw) * cb0[6].y + 1 - cb0[6].x * (1 + cb0[6].y), 0)`.
  `cb0[6].x` is the progress, `cb0[6].y` the edge width, `t4` a noise texture.
- Where `d < 1` the colour is replaced by `t14` (on `uv0`) and a glow `d * 4 * t5 * t15` is
  added. `t5` and `t15` take their UVs from `v6` via `cb0[8]` and `cb0[9]`.
- For `1 ≤ d < 1.5` the glow is `(1 - 2(d - 1)) * 4 * t5 * t15`, a band that fades out.
- The glow is masked by `t3.y` and added to the emissive.

## `procedural` (1536 slots, 768 compiled, 128 programs)

`slot = low + 512 * hi`. `hi` 0 is never compiled, `hi` 1 has 256 compiled slots and
`hi` 2 has 512. Every program is unlit (no `cb8`).

The core is a noise dissolve with the same threshold formula as `skin` bit 4. Alpha is
multiplied by
`saturate(t3(v6.zw * cb0[7].zw + cb0[7].xy + cb0[6].zw) * cb0[6].y + 1 - cb0[6].x * (1 + cb0[6].y))`,
where `cb0[6].x` is the progress and `cb0[6].y` the edge width. The alpha test and the MSAA
coverage loop apply the same factor.

| Digit | Effect |
|---|---|
| `hi` = 2 | Base texture `t0` (plus `t1`/`t2` on the texture-combine bits) |
| `hi` = 1 | No base texture; colour comes from the effect layer alone |
| bit 5, bit 8 | Effect layer from `cb2`: none, a small `cb2[1]` form, or the full `cb2[5]` form |
| bits 6, 7 | Texture combine, as bits 5/6 in keys A and B |
| bit 1 | Depth/normal prepass |
| bit 4 | With bit 1: depth only (no normal output `o1`) |
| bit 2 | Screen-door dither |
| bit 0 | Clustered-light slot bit; on its own it changes nothing, because the shader is unlit |
| bit 3 | With bit 0: discard plus view depth to `o1.x` (as combiner bit 8 requires bit 2) |

The full `cb2[5]` layer combines `t4` and `t17` (times `cb2[1]`), picks channels through
one-hot rows of an immediate table indexed by `cb2[2]`, selects a UV mode by `cb2[0].z`,
and adds a fresnel term with power `cb2[0].y`, coloured `cb2[3] * cb2[4].z`. It is only
partly decoded.

## Vertex side

The matching vertex containers (`blueprint`, `gradientmask`, `litsphere_t1`,
`litsphere_transparent`, `shadowmonk`, `xrayhighlight`) use the M2 vertex key
(`m2_vertex.md`). `litsphere_transparent` writes the matcap UV that `litsphere_additive_transparent`
reads as `v6.zw`: the view-space normal's `xy * (0.5, -0.5) + 0.5`. The vertex programs were not read beyond their outputs.
