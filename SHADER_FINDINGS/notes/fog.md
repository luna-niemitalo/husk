# Fog model

Every forward-shaded family evaluates the same fog: combiners, model effects, `uber`,
terrain, liquids, decals, `detaildoodad`. Particles evaluate the visibility part per vertex
(`cb3`, see `particles.md`) and the colour part per pixel. Code references are to
`pixel/dx_5_0/combiners_mod/asm/0000.asm`. The other families use the same instructions,
with the selection fields in another buffer (`cbuffers.md`).

## Inputs

| Source | Content |
|---|---|
| `cb5[14·r + 0..13]` | Fog record `r`, 14 `vec4`s. 12 records fit before `cb5[168]` |
| `cb5[172].xyz` | Up vector (view space) |
| `cb0[0].x`, `cb0[2].xyz`, `cb0[2].w` | Fog list: up to 4 record indices, count in `cb0[2].w` (clamped to 4) |
| `cb0[0].z` (or `cb0[0].x` in two-set mode) | The record whose colours are used |
| `cb0[0].y` | The single record of the second set (two-set mode) |
| `cb0[24].z` | Two-set mode |
| `cb0[1].y ≠ 0` | Unfogged: the whole fog step is skipped (M2 material flag 0x2) |
| `cb0[23].z` | Per-draw fog-amount multiplier |
| `cb0[1].w ≠ 0` | Froxel volume enabled |
| `v3.xyz` (`P`) | View-space position; `d = |P|` and `V = P/d` |

Terrain takes the list and the unfogged flag from `cb3[0..2]`, liquids from `cb1[8..10]`
(`cbuffers.md`).

## Per-record visibility

For record `R` (all fields below are `cb5[R + i]`):

**Type** (`[1].x`). Any non-zero value selects a simple power fog:
`vis = min(pow(max(([1].y − d)·[1].z, 0), [1].w), 1)` and `h = 0`.

**Type 0 (standard fog).**

```
// vertical band: remove up to ±[8].w of the height difference
y   = dot(up, P)
Pb  = P − up · clamp(y, −max([8].w, 0), max([8].w, 0))
db  = |Pb|
e   = max(db − [0].w − [0].x, 0)                  // distance past the fog start

// height weight: distance to the plane [2], shaped by a cubic
h0  = 1 − saturate((dot([2].xyz, P) + [2].w) · [3].w)
h   = 1 − saturate([9].x·h0³ + [9].y·h0² + [9].z·h0 + [9].w)

// exponential fog: normal density [0].z, height density [4].x
fexp = lerp(exp(−e·[0].z), exp(−e·[4].x), h)
vis  = min(fexp, saturate((1 − db/[0].y) / 0.7))     // also forced to 0 at the fog end [0].y

// distance curves: [10] (normal) and [11] (height), between [12].y and [12].x
s   = saturate((db − [12].y) / ([12].x − [12].y))
C   = lerp(1 − sat(cubic(s, [10])), 1 − sat(cubic(s, [11])), h)
k   = saturate([0].y / [12].x) − 0.3
C2  = lerp(sat(cubic(k, [10])), sat(cubic(k, [11])), h)
u   = saturate((saturate(db/[12].x) − k) · 3.333)
q   = min(lerp(C, saturate(1 − lerp(C2, 1, u)), u), C)

vis = lerp(q, vis, [12].z)                         // blend curve fog with exponential fog
vis = 1 − (1 − vis) · cb0[23].z                     // per-draw fog multiplier
```

`cubic(x, c) = c.x·x³ + c.y·x² + c.z·x + c.w`.

## Fog colour

Taken from one record `B` (see Inputs), plus a sun term:

```
g    = saturate((d − cb5[R+13].w) / cb5[R+6].w)          // R: the record being evaluated
cA   = lerp(cb5[B+6].rgb, cb5[B+13].rgb, g)             // linear gradient
cB   = lerp(cb5[B+3].rgb, cb5[B+4].yzw, g³)             // cubic gradient
col  = lerp(cB, cA, h)                                   // h from the height weight above
t    = saturate(dot(V, cb5[R+8].xyz)) − cb5[R+5].x
if t > 0: col += ((t / (1 − cb5[R+5].x))³) · (cb5[B+5].yzw − col) · cb5[R+7].x
```

## Combining records

```
fogged_r = (blend class 4) ? colour · vis      // additive: fade to zero (particles.md)
                           : lerp(col, colour, vis)
result   = Σ_r fogged_r · cb5[r + 12].w          // per-record weight
```

The weights let the client blend several light/fog records. The wiki describes the camera
interpolating between two LightData entries across zone-light polygons
(`documentation/wowdev-wiki/md/Day_night_cycle.md:289-291`); the four-entry list
matches WMO `MOGP.fogIds[4]` (`reconciliation.md` §3).

**Two-set mode** (`cb0[24].z`). The list above gives set 1. Record `cb0[0].y` alone gives
set 2. The result is `lerp(set1, set2, v3.w)`, where `v3.w` is the per-draw
interior/exterior blend (`m2_vertex.md`, `wmo.md`). The wiki describes doodads choosing
between two fog sets (`md/Rendering/Lighting.md:145-165`); the shader blends them instead.

## Froxel volume

After the record fog, when `cb0[1].w ≠ 0`:

```
uv    = v0.xy · cb6[1].xy + cb6[1].zw
slice = sqrt(saturate(v3.z / cb5[B + 0].y))      // square-root depth distribution up to the fog end
f     = t16(uv, slice)                            // rgb: in-scattered light, a: 1 − transmittance
rgb   = rgb · (1 − f.a) + f.rgb                   // blend class 4: rgba · (1 − f.a), no in-scatter
```

The volume is built by the `volumefog*` compute chain (`lighting.md`).

## Field roles and candidate data sources

Roles come from the code. Names in the last column are matches with the wiki's 7.0+
LightData fields (`md/DB/LightData.md`, `md/EnumeratedString.md:3996-4026`) and
LightParams; they are **inferred** unless marked.

| Field | Role in code | Candidate source |
|---|---|---|
| `[0].x`, `[0].w` | Fog start offsets (subtracted from distance) | Fog start / scaler |
| `[0].y` | Fog end: linear cutoff and froxel depth range | `m_fogEnd` |
| `[0].z` | Exponential density | `m_fogDensity` |
| `[1].x`, `[1].yzw` | Fog type; power-fog parameters | — |
| `[2]`, `[3].w` | Height plane and height scale | LightParams 0x40 "Height fog above Plane" |
| `[4].x` | Exponential density inside the height fog | — |
| `[9]` | Height falloff cubic | — |
| `[10]`, `[11]` | Distance curves (normal / height) | — |
| `[12].x`, `[12].y` | Curve far and near distances | — |
| `[12].z` | Curve fog vs exponential fog | — |
| `[12].w` | Record weight | zone-light interpolation |
| `[8].w` | Vertical band ignored by distance | — |
| `[3].rgb` → `[4].yzw` | Fog colour, near → far (cubic) | 18 Fog, 25 Fog End Color |
| `[6].rgb` → `[13].rgb` | Height-fog colour, near → far (linear) | 27 Fog Height Color |
| `[13].w`, `[6].w` | Colour gradient start and length | — |
| `[5].yzw` | Sun fog colour | 26 Sun Fog Color |
| `[8].xyz` | Sun direction (view space) | — |
| `[5].x`, `[7].x` | Sun-fog angle threshold and strength | — |

The wiki's MoP formula (`s_fogParams`: linear fog from `fogEnd · fogScalar` to `fogEnd`,
`md/Day_night_cycle.md:259-283`) is gone. This client keeps a linear cutoff at the fog end
and adds exponential, height and curve terms.
