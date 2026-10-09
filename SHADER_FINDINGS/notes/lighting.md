# Lighting, shadow and volume-fog passes

These passes produce the screen-space inputs that the forward shaders read: the light
buffer `t11` (at `v0 * cb5[168]`), the shadow mask `t12` (at `v0 * cb5[169]`), and the
froxel fog volume `t16`.

## Light buffer

### `lightbufferomni` (64 slots, 64 programs)

`slot = b0 + 2*b1 + 4*n + 32*b5`, where `n` runs 0–7.

| Digit | Effect |
|---|---|
| `n` | Lights per pass, `n + 1` (1–8). `cb1` grows by 8 registers per light |
| `b0` | Removes the colour gradient: no `cb1[8]`, a single colour |
| `b1` | Multiplies each light by a cookie cubemap `t3`…`t10`, sampled with the light vector rotated by `cb2[0..2]` |
| `b5` | Multiplies the result by a full-resolution mask `t0` loaded at the pixel |

Per light (base program, slot 0):
1. View position = view ray `v2 * depth(t2) * cb1[3].x`; `L = cb1[6] - position`.
2. Attenuation `1 - saturate((|L| - cb1[7].x) * cb1[7].z)`.
3. Colour: lerp from `cb1[5]` to `cb1[4]` by `smoothstep(cb1[8].y, cb1[8].x, |L|)`.
4. `(attenuation * colour)²`, times `max(N·L̂, 0)` with the normal from `t1.xyz * 2 - 1`.
5. Times `t1.a` when `cb1[7].w ≠ 0`. Alpha out is 1.

`lightbufferomnioverdraw` (64 slots, 1 program) is the overdraw-visualisation variant.

### `lightbuffer_spotlight` (8 slots, 8 programs)

| Bit | Effect |
|---|---|
| 0 | Reads `v1.xy` instead of `v1.xyw`: drops a perspective divide of the screen UV |
| 1 | Adds a 2D cookie texture `t3` |
| 2 | Multiplies by the full-resolution mask `t0`, as omni bit 5 |

`lightbuffer_spotlightoverdraw` has 8 slots and 2 programs. The vertex shader is
`vertex/dx_5_0/lightbuffer` (1 program).

## Shadows

### `shadowmask` (1 program)

Resolves the screen-space shadow mask:
1. Normal `t0` and depth `t1`; the position comes from the view ray `v2`.
2. The cascade is the first `k` with `cb1[6k + 5].x ≥ view depth`. The cascade count is in
   `cb1[46]`.
3. The position, offset along the normal by `cb1[49]`, is projected with `cb1[6k + 0..3]`.
4. The cascaded shadow array `t5` is sampled with comparison, using a 5-tap or 32-tap
   kernel picked by `cb1[48].x` (5 or 32).
5. Optional inputs: `t2`, enabled by `cb1[47].x`, and `t3`, enabled by `cb1[47].y`. `t3` is
   applied as `lerp(1, t3, w)`, where `w` falls to 0 within 10% of the projected cascade's
   edges.

`t6`–`t8` are declared but were not traced.

### Shadow-map rendering

| Container | Programs | Content |
|---|---|---|
| `pixel/shadowmapsl` | 4 | 0: empty (depth only). 1: alpha test on `t0.a`. 2: screen-door dither discard (same hash as combiner bit 6; normal from screen derivatives). 3: both |
| `vertex/shadowmap` | 24 slots, 16 programs | Not decoded |
| `vertex/shadowmapterrain` | 2 | Not decoded |
| `raytracing/shadowrt` | DXIL only (no slot table) | Not decoded |

## Depth/normal prepass

Containers: `prepassmapobj`, `prepassmodel2`, `prepasssolid`, `prepassterrain*`,
`terrainlightmapprepass`, `particulatevolumeprepass` (pixel, 1–3 programs each), plus their
vertex shaders (`prepassmodel2`: 48 slots, 40 programs). None of these programs were read.
Their declarations (a depth input such as `v1.z`, a normal input, outputs `o0` and `o1`,
9–15 instructions) match the prepass variants inside the material containers. Those write
view depth to `o0.x` and the packed normal `N * 0.5 + 0.5` to `o1` (`combiners.md`, bit 4).

## Volume fog

The froxel volume `t16` is built in compute and applied in the forward shaders (and by
`volumefogcomposite`). Stage roles are inferred from each shader's resources.

| Container | Programs | Resources | Role |
|---|---|---|---|
| `compute/volumefogmaterialinjection` | 8 | `cb1[27]`; `t3`; optional 3D noise `t2`; optional shadow array `t5` + `cb2[47]` (the cascade layout of pixel `cb9`); output 3D `u0`; 4×4×4 threads | Writes fog density/colour into the volume. The three optional inputs make the 8 variants |
| `compute/volumefogglobalfog` | 2 | `t3`, optional `t5` + `cb2[47]`, `u0` | Global (height) fog into the volume |
| `compute/volumefoglightscatter` | 1 | Clustered lights `t21`/`t22`, `u0` | Scattering from local lights |
| `compute/volumefogtemporalblend` | 1 | Two 3D inputs `t1`, `t2` → `u0` | Blend with the previous frame |
| `compute/volumefogcompute` | 1 | `cb0[23]`, `u0`, `u2` | Integration along the view direction (48 instructions) |
| `compute/volumefogcomputeold` | 1 | `cb0[886]` | Older single-pass version |
| `pixel/volumefogcomposite` | 1 | Depth `t0`, volume `t16` | Applies the volume to the screen. Depth → slice is `sqrt(saturate(depth / cb1[8].x))`, as in the forward shaders |
| `pixel/volumefoggauss9color` | 2 | `t0`, `t1`, `cb2[1]` | 9-tap Gaussian blur |
| `pixel/particulatevolume` | 2 | `t1`, `t2`, `cb1[2]` | Not decoded |

## Other

`gbufferdownsample` and `ssao_normals` (1 program each) are not decoded. The ambient
occlusion containers are in `postprocess.md`.
