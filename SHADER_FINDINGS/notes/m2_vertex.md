# M2 vertex shaders (`vertex/dx_5_0`)

## Which vertex containers feed the combiners

The combiner pixel shaders read `v1`–`v4` (`xyzw`) and `v5.xy`, plus `v5.zw` and `v6.xy` in
the two- and three-texture materials. These vertex containers write that interface: `o1`–`o4`,
`o5.xy`, and `o5.zw`/`o6.xy` in the `_env`/multi-UV variants. All of them write
`o3.w = cb0[1].x`.

`cdiffuse_t1`, `cdiffuse_t2`, `color_t1`, `color_t1_t2_t3`, `color_t2`–`color_t5`,
`diffuse_edgefade_env`, `diffuse_edgefade_t1`, `diffuse_edgefade_t1_t2`, `diffuse_env`,
`diffuse_env_env`, `diffuse_env_t1`, `diffuse_t1`, `diffuse_t1_env`, `diffuse_t1_env_t1`,
`diffuse_t1_env_t2`, `diffuse_t1_t1`, `diffuse_t1_t2`, `diffuse_t1_t2_t1`, `diffuse_t1_t2_t3`,
`diffuse_t2`, `litsphere_t1`, `litsphere_transparent`, `solidcolor`, the `particle_*`
containers, `blueprint`, `gradientmask`, `procedural`, `shadowmonk`, `xrayhighlight`,
`overdraw`, `trianglearea`, `decal`, and the `*_proj_*_texture` containers.

Which vertex container the client pairs with which pixel container is decided by the
client, not stored in the shaders. Signature compatibility is all the export can show.

## Pixel interpolators, resolved

| Pixel input | Written as | Meaning |
|---|---|---|
| `v1` | `o1 = saturate(cb0[7] + cb0[8]) * (0.5, 0.5, 0.5, 1)` | Material colour, halved so the pixel shader's `*2` restores it |
| `v2.x` | `o2 = 1`, except in `diffuse_edgefade_*` | Edge fade: `pow(saturate((abs(N·V) - cb1[0].y) / (cb1[0].x - cb1[0].y)), cb1[0].z)` |
| `v3.xyz` | `o3.xyz` | View-space position |
| `v3.w` | `o3.w = cb0[1].x` | Per-draw constant blending scene ambient/fog with the draw's own (`cb0[18..21]` and the second fog list in the pixel shader) |
| `v4.xyz` | `o4.xyz` | View-space normal |
| `v4.w` | `o4.w = 1`, or `utof(SV_InstanceID)` in instanced variants | Index for pixel bit 0's per-instance light set |
| `v5.xy` | `o5.xy = cb0[3..4] · (uv0, 1)` | Transformed UV0 |
| `v5.zw`, `v6.xy` | `_env` and multi-texture variants | Second and third UV sets, including generated UVs (below) |

## Slot key

The slot index is mixed-radix:

```
slot = inst + 2*clip + 4*nocb8 + 8*skin + 24*b + 192*hi
         1      2        2        3        8      2 (only in 384-slot containers)
```

| Digit | Values | Effect | Evidence (`diffuse_t1` unless noted) |
|---|---|---|---|
| `inst` | 0–1 | Instancing: reads `SV_InstanceID` and writes it to `o4.w` | blob 0 → 1 |
| `clip` | 0–1 | Writes `SV_ClipDistance` `o6.x` | blob 0 → 2 |
| `nocb8` | 0–1 | Material colour from `cb0[7]` alone instead of `saturate(cb0[7] + cb0[8])` | blob 0 → 4 |
| `skin` | 0 | No bones: `cb0[15..17]` is the model matrix (`cb0[18]` declared) | blobs 0–7 |
| | 1 | Rigid: one bone, index in `v2.x` | blobs 8–15 |
| | 2 | 4-bone skinning: weights `v1.xyzw`, indices `v2.xyzw` | blobs 16–23 |
| `b` bit 0 (+24) | | Never changes the program | |
| `b` bit 1 (+48) | | Removes per-vertex fog. Clear: the shader evaluates one `cb3` fog record and writes it to `o6`. Only the particle, `litsphere_transparent`, `shadowmonk` and `blueprint` containers have it | `particle_color_t2` slot 0 → 48 |
| `b` bit 2 (+96) | | Pre-transformed vertices: no model or bone matrix, positions go straight through `cb0[12..14]`. With `skin` 1 or 2, the bone palette instead holds sway parameters: each axis is offset by `sin(pos + phase) * amplitude * v1.y`, with the palette entry indexed by `v2.y`. `skin` 2 collapses onto 1 | blobs 24, 32 |
| `hi` (+192) | | `diffuse_t1_env`: generated second UV `o6.xy`. `cb0[1].z` picks the mode: 0–2 planar (yz, xz, xy), 3–5 cylindrical around x, y or z (`atan2/π`), 6 a fixed oblique projection, 7 UV0 through the UV0 transform `cb0[3..4]`, 8 UV1 (`v3.xy`) through `cb0[5..6]`. Modes 0–6 take the raw vertex position, or with `cb0[1].w == 1` the model-transformed position multiplied by `cb0[9..11]`. In `diffuse_edgefade_t1_t2` the high half repeats the low half | `diffuse_t1_env` slot 0 → 192 |

21 containers share `diffuse_t1`'s slot partition exactly. These use the same digits but
use some only partly (`nocb8` changes the program in 32 of 96 pairs in `cdiffuse_*`, `overdraw` and `trianglearea`) or split
further (`b` bit 1 in the particle family).
