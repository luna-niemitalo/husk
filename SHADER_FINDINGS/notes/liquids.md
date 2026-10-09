# Liquid shaders

Liquids come in three tiers, judging by names and texture sets: `proc*` (full),
`medium*` and the plain names (`water`, `magma`, `mercury`, `leyline`). Most liquids don't
read the scene-light buffer `cb8`. They take their lighting from their own parameter block
in `cb1`. `procswamp` and `procmercury` are the exceptions; they read `cb8[0..5]`.

## `procwaterabove` (54 slots, 54 programs)

### Slot key

`slot = a + 3b + 9c + 27d`. `a`, `b` and `c` take values 0–2; `d` takes 0–1.

| Digit | Values | Effect |
|---|---|---|
| `a` | 0 | The water-colour gradient parameter comes from the vertex value `v5.y` |
| | 1 | It comes from `saturate(cb1[11].x + cb1[11].y * t14(v5.zw) + …)`, with `t15` at `v5.zw` as well |
| | 2 | As 1, plus `t13`, sampled at the world XY through `cb1[23]` (`(cb1[23].z - (xy + cb1[23].xy)) * cb1[23].w`), weighted by `cb1[11].w` |
| `b` | 0 | Reflection from the projected reflection texture `t3`, falling back to the sky texture `t1` |
| | 1 | Sky texture `t1` only |
| | 2 | Projected reflection `t3` only |
| `c` | 0 | Normal from the `t5` normal map only |
| | 1 | Adds detail normals from height map `t4` by finite differences (offsets `cb1[37]`, blend `cb1[39].x`) |
| | 2 | Also a second height map `t8` (offsets `cb1[38]`, blend `cb1[39].y`) |
| `d` | 1 | Discard fragments with alpha < 1/255 and write view depth `v1.z` to `o1.x` (combiner bit 8) |

### Base program (slot 0)

1. **Normal.** The normal map `t5` is sampled four times (`v2.xy`, `v2.zw`, `v3.xy`,
   `v3.zw`), as two layers blended by distance. Strength comes from `cb1[32].w` and
   `cb1[32].y`.
2. **Refraction.** The scene colour `t0` is read at the projected screen position
   (`v7.xy / v7.w`, through `cb1[35]`) plus a normal-based offset. The offset is scaled by
   the water depth: scene depth `t6` minus `v1.z`. It is kept only when the offset point is
   still behind the water surface.
3. **Absorption colour.** Lerp from `cb1[20]` to `cb1[21]` by the gradient parameter
   (`v5.y`, or the `a`-digit textures), multiplied by the water lighting
   `(cb1[3] + cb1[4] * sun) * v8.xyz`. The colour's `.w` blends it over the refraction.
4. **Reflection.** The reflected view ray is projected through `cb1[16..19]` into the
   reflection texture `t3`. Where the projection leaves the screen, it falls back to the
   sky texture `t1`. Mixed in by fresnel `(1 - N·V)² * 0.5 + (0.1, 0.2)`.
5. **Specular.** Blinn: `pow(saturate(N·H), cb1[5].w) * cb1[5].rgb`, with `H` from the
   direction `cb1[0]` and the view vector. It is attenuated by the point light in `cb1[1..2]`
   and multiplied by fresnel.
6. **Shore foam.** `t7` is sampled at `v4.xy` and `v4.zw` and fades in where the water is
   shallow (smoothstep on the depth difference), scaled by `cb1[24].y`.
7. **Secondary light** `cb1[6..7]` and fog through `cb1[8..10]` → `cb5` (the fog-selection
   fields that the combiners hold in `cb0[0..2]`), then the froxel volume `t16`.

## The other liquid containers

Not decoded beyond their declarations. "Instr." is the base program's instruction count.

| Container | Slots / programs | Instr. | Textures | Parameters |
|---|---|---|---|---|
| `procwaterbelow` | 18 / 18 | 237 | `t0`, `t5`, `t6`, `t16` | `cb1[37]` |
| `procwaterabovefog` | 3 / 3 | 43 | `t0`, `t6` | `cb1[36]` |
| `procswamp` | 108 / 48 | 189 | `t9`–`t11`, `t16` | `cb1[29]`, `cb8` (6 registers) |
| `procswampabovefog` | 18 / 12 | 65 | `t0`, `t6`, `t9`–`t11` | `cb1[39]` |
| `procmercury` | 72 / 72 | 263 | `t1`, `t3`, `t5`, `t16` | `cb1[29]`, `cb8` (6 registers) |
| `procmagma` | 4 / 4 | 181 | `t0`–`t3`, `t16` | `cb1[13]` |
| `procleylineabove`, `procleylinebelow` | 24 / 24, 12 / 12 | 231, 225 | `t0`–`t3`, `t6`, `t16` | `cb1[36]` |
| `procleylineabovefog` | 2 / 2 | 42 | `t3`, `t6` | `cb1[26]` |
| `mediumwater`, `mediumwaterbelow` | 6 / 6 each | 220, 223 | `t5`, `t16` | `cb1[37]` |
| `mediummercury` | 4 / 4 | 195 | `t5`, `t16` | `cb1[26]` |
| `mediumleyline`, `mediumleylinebelow` | 4 / 4 each | 209, 215 | `t0`–`t2`, `t16` | `cb1[36]` |
| `water` | 6 / 6 | 196 | `t1`, `t16` | `cb1[45]` |
| `magma` | 4 / 4 | 163 | `t0`, `t16` | `cb1[11]` |
| `mercury` | 4 / 4 | 189 | `t1`, `t16` | `cb1[11]` |
| `leyline` | 4 / 4 | 171 | `t0`, `t1`, `t16` | `cb1[29]` |
| `waterfall` | 2 / 2 | 517 | `t0`–`t5`, `t16` | `cb1[18]` |
| `liquidfog` | 8 / 8 | 167 | `t1`, `t16` | `cb1[19]` |
| `liquiddebug` | 1 | 171 | — | `cb3[3]` (fog selection) |
| `waterfogpoly` | 1 | 3 | — | — |
| `waterripples` | 1 | 5 | `t0` | — |
| `liquidminimap` | 3 / 3 | 6 | — | — |
| `ffxunderwaterhigh`, `ffxunderwaterlow` | 2 / 2, 2 / 1 | 102, 8 | `t0`–`t2`, `t4`; `t0`, `t4` | `cb1[14]`, `cb1[13]` |
| `ffxwaterwindow` | 3 / 3 | 8 | `t0`, `t1` | `cb6[2]` |

The `*abovefog` containers are short passes that read only the scene colour/depth (`t0`,
`t6`) and their parameters, so they look like fog-only passes applied above the surface.
The `*below` containers are the views from under the surface. Both readings come from the
names and declarations.

The vertex side (`vertex/dx_5_0/procwater`, `procswamp`, `procmercury`, `procleyline`,
`water`, `waterfall`, …) is not decoded.
