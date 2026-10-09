# Compute shaders

The GPU-particle stages are in `particles.md` and the volume-fog stages in `lighting.md`.
This file covers the rest. A role is marked *code* when it was read, *name* when it comes
from the container name plus its resources only.

## Decoded

### `texturecompositing` (1 program, 64 threads per group)

*code*. Composites a layer `t1` onto a base `t0` and writes `u2`. Source and destination
offsets are in `cb0[0..2]`. The thread count is limited by `cb0[0].y`. `cb0[0].x` selects
the operation; `b` is the base, `l` the layer, `a` the layer's alpha:

| Mode | Operation |
|---|---|
| 0 | Nothing |
| 1 | Copy `l` |
| 2 | Copy `l` where `a ≥ 0.5`, else `b` |
| 3 | `lerp(b, saturate(b + l), a)` |
| 4 | `lerp(b, saturate(b * l), a)` |
| 5 | `lerp(b, saturate(2 b l), a)` |
| 6 | Overlay (branch on `b < 0.5`), blended by `a` |
| 7 | Screen, `1 - (1 - b)(1 - l)`, blended by `a` |
| 8 | Hard light (branch on `l ≤ 0.5`), blended by `a` |
| 9 | `lerp(b, l, a)` |
| 10 | `saturate(b * l)` |
| 11 | `saturate(b.rgb * l.arg)` (layer swizzled `.wxy`) |
| 12 | Luminance with weights `(0.21, 0.72, 0.07)`; which value it is computed from was not traced |
| 13 | Converts a channel to an integer index (`* 255`); not followed further |

These are the blend modes needed to build a texture from layers. Character-texture
compositing would be a fit, but the shader doesn't say what it composites.

### `m2skincs` (3 programs, 64 threads per group)

*code*, program 0. For each vertex `cb0[0].x + threadID` (count `cb0[0].y`):
1. Read the position (offset 0) and the bone index (low byte of offset 16) from the 48-byte
   vertex buffer `t3`.
2. Transform by the bone's 3×4 matrix at `cb1[11 + 3 * bone]` (256 bones).
3. Transform by `cb1[0..2]`, then `cb1[3..5]`.
4. Write a `float3` to `u2` (12-byte stride).

Programs 1 and 2 were not read; they probably cover the other skinning modes.

### Clustered lighting

| Container | Programs | Resources | Role |
|---|---|---|---|
| `clusteredshading_froxelgrid` | 1 | `u1` | *name* — builds the cluster grid (68 instructions) |
| `clusteredshading_assignlights` | 1 | 192-byte light records `t1`, `t2` → list buffer `u3` | *name* — fills the per-cluster light lists that the pixel shaders read as `t21`/`t22` (`combiners.md`, bit 2) |

## Not decoded

| Container | Programs | Resources / threads | Role |
|---|---|---|---|
| `cacao_*` (10 containers) | 2–16 each | 2D-array textures, 8×8 threads | AMD FidelityFX CACAO (groupshared names, `dxil_names.md`) — compute ambient occlusion: prepare depths, mips and normals, generate, importance map, edge-sensitive blur, bilateral upscale, apply |
| `cmaa2_*` (5 containers) | 1–16 each | UAV edge buffers, raw dispatch arguments | Intel CMAA2 anti-aliasing (groupshared names) |
| `fsr` | 2 | `t1` → `u0`, 64 threads | AMD FSR 1 (*name*, `min16float` resources) — 822 instructions |
| `guidedfilter`, `guidedfilterh`, `guidedfilterv` | 8 each | DXIL only in `dx_5_0` | *name* |
| `fogdepth` | 1 | `t1` → 3D `u0`, 16×16 threads | *name* |
| `generatemips` | 1 | `t0` → `u1` | *name*, 7 instructions |
| `particulatecompute` | 2 | `t0` → `u1`, 128 threads | *name* |
| `texcoordexplode` | 3 | `t2`, 48-byte `t3` → 24-byte `u0` | *name*. Its three programs read `StructuredBuffer<M2Vertex>`, `<WMOVertex>` and `<M3Vertex>` and write `TriangleOut` (three UVs per triangle) |
| `animacompute` | 1 | 64-byte `u0` | *name*; `u0` is `RWStructuredBuffer<AnimaCurveNode>` |
| `simplecompute` | 1 | `u0` | 5 instructions |

## Only in `dx_6_0`

These need SM6 features and have only DXIL listings (`asm/*.ll`), so they have no GLSL.
Details are in `dxil_names.md`.

| Container | Identification |
|---|---|
| `fps_count_uint`, `fps_reducecount`, `fps_scanprefix`, `fps_scanadd`, `fps_scatter_uint`, `fps_setupindirectparameters` | AMD FidelityFX Parallel Sort (4-bit radix sort; type and groupshared names) |
| `valar`, `valar_lp` | Variable-rate-shading image from luminance contrast (Intel VALAR) |
| `shadowrt` (compute, pixel, ray-tracing library) | Ray-traced sun shadows: 8 blue-noise-rotated rays per pixel, interior and exterior instance masks blended by `Normal_Exteriorness`, alpha-tested and stippled shadow casters |
| `lightbufferomnishadow`, `lightbufferspotshadow` | Light-buffer passes with a `RayQuery` shadow test per light |
| `terrainlightmapomni`, `terrainlightmapspot` | Terrain light map with an acceleration structure at `t10` and `ShaderLight` records |
