# Particles and ribbons

Two separate systems exist. M2 particles are drawn as ordinary vertex buffers through the
`particle_*` vertex shaders and two pixel containers. GPU particles are simulated by compute
shaders and drawn through `computeparticlevs`/`computeparticleps`. Ribbons likewise have a
CPU path (`ribbon`) and a GPU path (`gpuribbon`).

## M2 particle pixel shaders

### Slot key

`particle_mod` (32 slots, 16 compiled) and `particle_3colortex_3alphatex` (64 slots, 32
compiled) share their upper bits. The three-texture container has one extra bit at the
bottom; the bits below are numbered for `particle_mod` and are one higher in the
three-texture container.

| Bit (`particle_mod`) | Effect | Combiner equivalent |
|---|---|---|
| 0 | Alpha-test discard | — |
| 1 | Soft particles: scene depth `t10` at the screen position, `saturate((scene - depth) * cb0[23].x) ^ cb0[23].y` into alpha. Moves `v5.z` to `v5.zw` | bit 5 |
| 2 | Discard near-invisible fragments, view depth to `o1.x` | bit 8 |
| 3 | Set in every compiled slot | bit 9 |
| 4 | Simple clamped lighting | bit 10 |

Every program declares the clustered-light buffers (`cb4`, `t21`, `t22`). Unlike the
combiners, particles use clustered lights in all variants and never read the light buffer.

### `particle_mod` base program

1. UV `v5.xy`, or a sphere map when `cb0[5].y > 0`, as in the combiners. Texture `t0`, rgb
   scaled by `cb0[7].y`.
2. Per-particle alpha cutoff: the fragment survives only if `t0.a >= v5.z`.
3. Near-camera fade when `cb0[6].y > 0`:
   `saturate(5 - 5 * v3.z * cb0[6].y * cb0[6].z) * saturate(v3.z * cb0[6].z / 16.5) ^ 0.6`.
4. Lighting as in the combiners, with clustered lights for local lights. The normal `v4` is
   fixed at `(0, 0, 1)` by the vertex shader.
5. Fog from the vertex shader: `v6.x` fog amount, `v6.y` fog-colour mix, `v6.z` distance.
   The froxel volume `t16` applies as usual.

### `particle_3colortex_3alphatex`

Base combine, with flags in `cb0[6].x`:

```
c  = t0(uv0) * t1(v6.xy)                          // rgba
c3 = c * t2(v6.zw)
rgb = (flags & 1) ? c3.rgb : c.rgb
a   = c3.a
rgb *= (flags & 1) ? 4 : 2;  a *= (flags & 2) ? 4 : 2
rgb *= cb0[7].x
```

Extra slot bit 0 adds UV remapping for `t1` and `t2`. Modes come from `cb0[14].x` (for `t1`)
and `cb0[14].y` (for `t2`):
- 0: none.
- 1: polar, `(atan2(uv - 0.5)/2π + 0.5, |uv - 0.5|)`.
- 2: polar on the tiled `frac(uv)`, with an `asin`-based radius.

The remapped UV is scaled by `cb0[10].x`/`.y` and offset per particle by `v6.zw` (`t1`) or
`v7.xy` (`t2`). Both are sampled with gradients from the original UV, so the polar seam
doesn't produce a mip artefact. Fog moves from `v7` to `v8`.

## M2 particle vertex shaders

The `particle_*` vertex containers use the M2 vertex key (`m2_vertex.md`), including the
per-vertex-fog digit. Which pixel container each one feeds follows from its outputs:

| Vertex container | Outputs beyond `o1`–`o4` | Feeds |
|---|---|---|
| `particle_color_t2`, `particle_cdiffuse_t2` | `o5.xy` UV, `o5.zw` from attribute `v3.xy` (`.x` is the alpha cutoff), `o6` fog | `particle_mod` |
| `particle_diffuse_t2` | `o5.xy` UV, `o6` fog | `particle_mod`; `v5.z` is not written, so the cutoff reads 0 and never discards |
| `particle_color_t4` | `o5`, `o6.xy`, `o6.zw` (three UVs), `o7` fog | `particle_3colortex_3alphatex`, bit 0 clear |
| `particle_color_t5` | as t4, plus `o7.xy`; fog in `o8` | `particle_3colortex_3alphatex`, bit 0 set |

Common to all of them: `o1 = colour * (0.5, 0.5, 0.5, 1)` from vertex attribute `v1`,
`o2 = 1`, `o3.w = cb0[1].x`, `o4 = (0, 0, 1, 1)`.

## Ribbons

| Container | Slots | Pixel | Vertex |
|---|---|---|---|
| `ribbon` | 2 | `t0 * colour`. Slot 1 adds a soft depth fade: scene depth `t1` at the screen position derived from `v3`, minus `v4.z` | `cb1[0..3]` transform, colour `v1`, UV `v2` |
| `gpuribbon` | 4 | Slot 0 `t0 * colour`. Bit 0 adds the soft depth fade (`t3`). Bit 1 multiplies `t0(uv0) * t1(uv1) * t2(uv2)` by the colour and by 4 | No vertex buffer: the position comes from `SV_VertexID` |

`gpuribbon` vertex shader:
- `SV_VertexID & 1` picks the ribbon edge and the rest picks the segment.
- Each segment's edges are cubic polynomials in `t`: `c0 + c1 t + c2 t² + c3 t³`, with
  coefficients at `cb1[8*seg + 1..4]` and `cb1[8*seg + 5..8]`.
- `cb1[0].z == 1` selects a single-curve mode with coefficients at `cb1[4*k + 1..4]`.
- Values at `cb1[185 + seg]` are interpolated between neighbouring segments. Their `.w` scales `t`, `.y` drives the colour gradient and `.z` offsets the position.
- Colour runs from `cb0[12]` to `cb0[11]` along the ribbon.

## GPU particles

### Draw shaders

`computeparticlevs` draws 4 vertices per instance. It reads vertex `4*instance + vertexID`
from a 48-byte structured buffer `t0`:

| Offset | Content |
|---|---|
| 0 | position `xyz` (transformed by `cb1[0..3]`), `w` → `o5.z` |
| 16 | colour |
| 32 | `.x` flipbook frame, `.yz` → `o5.yw`, `.w` → `o6.x` |

The flipbook cell is `(frame & cb1[9].x, frame >> cb1[9].y)` scaled by `cb1[7].zw`, plus
the quad corner from an immediate table.

`computeparticleps` outputs `t0(uv) * colour`, with alpha zeroed when `v5.x > 1`. The vertex
shader always writes `v5.x = 1`.

### Compute stages (all `dx_5_0`, also present in `dx_6_0`)

| Container | Programs | Resources | Role |
|---|---|---|---|
| `particleinitialize` | 1 | `u0` (4-byte) | Fills an index list (8 instructions) |
| `particleemit` | 2 | Particle state `u1` (80-byte), index lists `u2`, `u3`, counters `u4` | Spawns particles. Program 1 emits from a mesh surface: triangles `t8` (36-byte), area tables `t9`–`t11`, `cb0[1033]` |
| `particleupdate` | 4 | `u1`–`u3` as above, plus output `u4` | Simulates (about 1390 instructions; per-emitter parameters at `cb0[25 + …]`). Bit 0 selects the output: 48-byte draw vertices directly, or 112-byte records plus `u5`/`u6` for sorting. Bit 1 adds an `atomic_imax` into `u3[7]`, a high-water mark |
| `particlebasicsort` | 1 | `t0`, `u1`, `u2` | Sort pass, 256 threads per group |
| `particlebuildsorted` | 1 | `u1` 112-byte records → `u3` 48-byte vertices | Writes draw vertices in sorted order |
| `particlecustommesharea` | 1 | `t8`, `t9` → `u1` | Triangle areas for mesh emission |
| `particlecustommeshskin` | 1 | — | Empty (2 instructions) in this build |

The internals of `particleupdate` (forces, curves, collisions) are not decoded.
`material3_particle_ps`/`_vs` are a second GPU-particle draw pair; see `material3.md`.
