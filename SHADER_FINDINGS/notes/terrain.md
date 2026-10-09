# Terrain shaders

Slot and blob numbers refer to `example_exports/shaders/pixel/dx_5_0/terrain` unless stated otherwise.

## Pixel slot key (`pixel/dx_5_0/terrain`, 672 slots, all compiled, 316 programs)

The key is mixed-radix. Its middle digit is an enumeration of valid feature combinations,
not a product of bits:

```
slot = ctl + 2*m + 56*k + 168*baked + 336*smask
       0–1   0–27  0–2    0–1          0–1
```

This model was checked against all 672 slots. For every slot it predicts the control block,
lighting path, shadow mask, baked mode, blend mode, per-layer flags, group count and
cubemap presence. All 672 match.

| Digit | Values | Effect | Evidence |
|---|---|---|---|
| `ctl` | 0–1 | Adds a control block `cb1[48..52]`. `cb1[48]` holds per-layer explicit LODs, used when `cb1[52].x > 0`. `cb1[52].y` scales specular, `cb1[52].z` mixes hemispheric toward flat ambient, and `cb1[52].w < 1` outputs only the baked shadow term. | blob 0 → 1; baked blob 150 → 151 |
| `m` | 0–27 | Blend mode and options; see below | |
| `k` | 0–2 | Number of 4-layer groups: 1, 2 or 4 (4, 8 or 16 layers). Group `g` uses alpha map `t(17+17g)`, layers `t(18+17g)`–`t(21+17g)`, heights `t(22+17g)`–`t(25+17g)` and LUT/curve textures `t(26+17g)`–`t(33+17g)` | `t34`, `t51`, `t68` appear with `k` |
| `baked` | 0–1 | Replaces layer blending with one of four baked composites `t17`–`t20`, chosen per 2×2 quadrant of the chunk UV. Only `ctl`, the lighting path and the UV input register still vary, so the 336 baked slots collapse to 8 programs (150–157) | `asm/0150.asm` |
| `smask` | 0–1 | Screen-space shadow mask `t12` at `v0*cb1[30]`, combined with the baked shadow `t4` by `min` | blob 0 → 158 |

### The `m` digit

`m` 0–3 use the lerp chain: `x = m & 1`, `flags = m >> 1`.

`m` 4–27 form three blocks of eight. `i = (m - 4) % 8`, `x = i & 1`, `y = (i >> 1) & 1`,
`z = i >> 2`. The blend mode is `(m - 4) / 8`: 0 normalized, 1 height, 2 height+LUT.

| Option | Effect | Evidence |
|---|---|---|
| Lerp chain | `colour = lerp(lerp(lerp(L0, L1, a.r), L2, a.g), L3, a.b)` with alpha map `t17` | blob 0 |
| Normalized | `w0 = 1 - sum(a.rgb)`. Each weight is sharpened by `1 - saturate(max(w) - w)` and the weights are renormalized. Layers with zero weight skip their fetch | blob 8 |
| Height | Normalized, plus per-layer heights `t22`–`t25` scaled by `cb1[5]` (MTXP `heightScale`) and offset by `cb1[9]` (MTXP `heightOffset`). Weights are multiplied by height before sharpening | blob 22 |
| Height+LUT | Height, plus a per-layer 32³ colour LUT `t26`–`t29` (likely ADT MTCG `colorGradingFdid`; the curve below its `colorGradingRampFdid`). The LUT result is blended in by a distance curve from `t30`–`t33`, sampled at `log2(distance) * cb1[32+i].x + cb1[32+i].y`. Each layer is enabled by a bit in `cb1[29].w` | blob 36 |
| `x` | Simple lighting: flat ambient, saturated specular, no squared/`sqrt` combine. Same as combiner bit 10 | blob 0 → 2 |
| `flags` | Per-layer flags `cb1[0].xyzw` move each layer between the lerp chain and a weighted-sum chain. The specular mask is the alpha from both chains. Only partly decoded | blob 0 → 4 |
| `y`, `z` | Reflection. With full lighting, either bit adds a cubemap `t5` sampled at the reflection vector `v4.xyz`. The chunk UV moves to `v5.xy`. Layer 0's alpha becomes the reflection mask instead of contributing to specular. `y` + `z` also removes layer 1's alpha from specular, and `z` tints the reflection by the blended colour and scales specular by 8. With `x` set there is no cubemap, and `y` and `z` only remove layer alpha from specular (one bit alone gives the same program as the other alone) | blobs 12, 16, 18; 10, 14, 20 |

## Base program (slot 0, blob 0)

1. Chunk UV from `v4.xy` (position) via `cb2[1]` (origin) and `cb2[0]` (alpha-map
   transform).
2. Alpha map `t17` and layers `t18`–`t21`. Layer UVs are scaled per layer by
   `cb1[13..16]` and offset by `cb2[2..5]`.
3. Multiply by vertex colour `v1.rgb * 2`. `v1.w` becomes output alpha.
4. Baked chunk shadow `t4` (`cb1[29].x`). Light buffer `t11` at `v0*cb1[31]`.
5. Point light `cb8[4..5]`, sun `cb8[3]`/`cb8[6]`, Blinn specular with exponent 20 and
   colour `cb8[7]`, masked by the blended layer alpha and scaled by `cb8[8].y`.
6. Hemispheric ambient `cb8[0..2]` with the 0.7/0.4 wrap term, as in the combiners.
7. Light flash `cb7`.
8. Fog: `cb3` selects records in `cb5`, as `cb0[0..2]` does for the combiners.

## Vertex shader (`vertex/dx_5_0/terrain`, 8 slots)

Inputs: `v0` position, `v1` normal, `v2` vertex colour, `v3.x` geomorph target height.

| Bit | Effect |
|---|---|
| 0 | Clip distance from the plane in `cb3[10]` |
| 1 | Reflection variant: `o4` = world-space reflection vector (`cb3[4..6]` rotates view to world), position moves to `o5` |
| 2 | No geomorph. Without it, `z` lerps toward `v3.x` by `saturate((distance(pos.xy, cb3[8].xy) - cb3[8].z) * cb3[8].w)` |

Outputs: `o1` vertex colour (0.5 when `cb3[9].x` is 0), `o2` view position, `o3` view
normal, `o4` (or `o5`) the position the pixel shader turns into chunk UVs.

## Related containers

| Container | Slots / programs | Role |
|---|---|---|
| `terrainblendbake` | 18 / 18 | Renders the layer blend into the composites that baked mode samples. Blob 0 is the lerp chain in 19 instructions. Other variants add the second and fourth groups (`t34`… in 12 programs, `t51`/`t68`… in 6) and height maps (`t22`… in 6) |
| `terrainlow`, `terrainblendlow` | 24 / 13, 8 / 6 | Not decoded. Declares `t0`/`t1` and `t2` respectively; the role is inferred from the names |
| `terraintexture` | 9 / 6 | Not decoded. Declares `t0` only |
| `terrainlightmap`, `…omni`, `…spot` | 108 / 34, 96 / 32, 48 / 16 | Lightmap passes; `terrainlightmap` reads the clustered-light buffers (`t2` 192-byte records, `t3` lists) |
| `prepassterrain*`, `terrainlightmapprepass` | 1–3 slots | Depth/normal prepass (`o0`, `o1`, 9–15 instructions) |
| `terraindebug` | 1 / 1 | Outputs `v1.w` |

The smaller containers' keys are not decoded.

## Open

- The exact meaning of the `flags` option (hypothesis in `reconciliation.md` §4).
- `y`/`z`: with both set, reflection mask = layers 0+1 alpha, specular = layers 2+3, which is the wiki's
  `metalBlend`/`specBlend` (`reconciliation.md` §4). `z` alone still needs a name.
- Keys of the smaller terrain containers.
