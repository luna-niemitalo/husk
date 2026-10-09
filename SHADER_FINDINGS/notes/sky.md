# Sky, clouds, celestial bodies, sun glare and sun shafts

All of these are small, single- or two-program containers. Every program was read in full.

## `dnsky` (sky dome)

- **Vertex shader.** Position through `cb1[0..3]`, vertex colour passed through as `o1`,
  and a second transform `cb1[4..7]` to `o2.xyz` (the view direction). The model-space `z`
  goes to `o2.w`.
- **Pixel shader.**
  1. Start from the per-vertex sky colour `v1`.
  2. Sun glow: `g = saturate((dot(normalize(v2.xyz), normalize(cb1[1].xyz)) - cb1[0].x) / (1 - cb1[0].x))`.
     The glow colour is `v1` moved toward `cb1[2].rgb` by two amounts (`cb1[0].y` and
     `cb1[2].w`), chosen by `saturate(v2.w * 50)`: near the horizon versus above it. It is
     applied with weight `saturate(g³)`.
  3. Dither, when `cb1[3].x > 0`: the pixel position plus `cb1[3].yz`, taken modulo 128
     and permuted by `cb1[4]`/`cb1[5]`, loads a texel from noise texture `t0`. The texel is
     remapped to a triangular distribution, `sign(n) * (1 - sqrt(1 - |n|))`, scaled by
     `cb1[3].x` and added to rgb.

## `dnclouds`

`colour = t0(v3.xy) * v2`, plus forward scattering toward the sun:
`saturate(dot(v1, cb1[0].xyz))² * saturate(1 - t0.a * v2.a) * |cb1[0].w| * colour * cb1[1].rgb`.
The same dither as `dnsky` is applied (noise in `t2`, parameters in `cb1[2..4]`).

## `dnplanet` (2 programs)

- **Program 0:** `t0 * v1`.
- **Program 1** stacks masked layers on UV sets `v2.xy`, `v2.zw`, `v3.xy`, `v3.zw`:
  1. Base `t0`.
  2. `t2` blended in by `t4.a * t2.a`.
  3. `t3` blended in by `t7.a * t3.a`.
  4. A blend toward the vertex colour by `t1.r`.
  5. `t5` blended in by `t6.a * t5.a`.
  6. Alpha times `v1.w`.

## Sun glare

| Container | Code |
|---|---|
| `glare` program 0 | `t0 * v1` |
| `glare` program 1 | rgb `t0.rgb * v1.rgb`; alpha `t0.a * v1.a * 4 * t1(screen position)`. `t1` is a visibility texture read at the projected centre |
| `dnglarequery` | Outputs constant white, which suits an occlusion query |

## Sun shafts

| Container | Code |
|---|---|
| `sunshaftsmask` program 0 | Depth `t0` at `v1 * cb1[2].xy + cb1[2].zw`: 0 where depth `< cb1[1].x` (geometry), 1 elsewhere (sky) |
| `sunshaftsmask` program 1 | The same over a `gather4` of 2×2 depths, averaged: a half-resolution mask |
| `sunshaftspropagate` | Radial blur of the mask toward the sun's screen position `cb1[0].zw`. Taps step by multiples of `cb2[0].x` (1, 2, 3 … 8 and beyond), so repeated passes with growing `cb2[0].x` lengthen the shafts. Scales by `cb1[0].xy * cb1[2].x * 135` |

`vertex/dx_5_0/sunshafts` has 2 programs and was not read.
