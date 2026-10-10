# Particulate volumes (`particulatecompute`, `particulatevolume`)

The renderer behind WMO `MPVD` / `_mpv.wdt` `PVMI` records
(`WIKI_FINDINGS/WORLD.md`'s "Particulate volumes" section). Code references:
`compute/dx_5_0/particulatecompute/glsl/0000.glsl` (simulation, 128 threads)
and `vertex|pixel/dx_5_0/particulatevolume` (draw). The dx_6_0 DXIL binds
the same buffer as `cb_particulate_volume` / `VS_ParticulateVolume`.

## The constant buffer

One volume = one 944-byte CB (59 vec4s; `cb59_struct` in the dx_5_0 GLSL,
exactly the DXIL type's computed size). `[i]` below = vec4 slot i (byte
offset 16·i). Roles read from the simulation code; **none of the names are
from reflection** (DXIL keeps no member names, the DXBC blobs are stripped).

| Slot | Role in code |
|---|---|
| `[0].xyz`, `[0].w` | wrap-domain phase/center and scale: positions recycle through `fract((p + [14].xyz − [0].xyz + 1)/2)·2 + [0].xyz − 1`, velocities divide by `[0].w` |
| `[1].y`, `[1].z` | feature flags (≠ 0 tests) |
| `[5].xyz` + rand·`[6].xyz` | spawn box: base + extent; `[5].w` a fade margin (`[0].w − [5].w`) |
| `[7].xyz`, `[7].w` | attractor/target position; `[7].w` a mode scalar (`> −0.5` test, `5 − [7].w`) |
| `[8].xy`, `[8].z`, `[8].w` | wind direction (normalized in-shader) and speed; `[8].w` a height for vertical fade |
| `[9].xyz` | eye position (distance fades) — runtime, not record data |
| `[10].xyz`, `[10].w` | edge-fade box size; `[10].w` time/phase — runtime |
| `[11].x/.y/.z/.w` | fade margins: edge, depth threshold, camera near/far |
| `[12].w` | twinkle threshold (`abs(rand) ≥ [12].w`) |
| `[14].xyz`, `[14].w` | wrap offset (camera-relative recycling); `[14].w` flag |
| `[15].x`, `[15].y` | flipbook tilesX, tilesY (floats; UV divisor in the VS) |
| `[15].z` | flipbook **frame count** (uint; `frameIndex % [15].z` in the VS) |
| `[15].w` | **max particle count** (uint; thread-id bounds check) |
| `[16..19]` | per-frame UV table (the `float4[4]` at CB +0x100), indexed by frame |
| `[20].y/.z/.w`, `[21]` | lifetime-ish scalars; `[21].x/.y` = size min/max (`([21].y−[21].x)·|rand| + [21].x`) |
| `[22].w` | behavior-mode flag (selects position/velocity source) |
| `[23]` | banding/stripe fade (`fract(z/[23].x)` against `[23].y/.z/.w`) |
| `[24].y` | int mode compared against {1,2,3}; `[24].z/.w`, `[25].x` ground-fade params |
| `[25].y` | ground-normal-fade flag; `[26].w` steepness threshold |
| `[25].z`, `[25].w` | clip-plane counts (two sets: max-of and min-of) |
| `[27..38]` | up to 12 clip planes (`float4[12]` at CB +0x1B0), looped over both sets |
| `[39..41]` | world transform rows |
| `[47..49]` | projection into `t0`, a terrain **heightmap** — ground collision/clamp (`r5.w = height − z`) |
| `[55..57]` | second transform (position warp path, used when `r11.x` selects it) |

Matrices at slots 43 and 51 are not read by the compute shader (likely
VS-only or inverse transforms; not traced).

## Simulation shape

Particles live in `u1` (r32ui image, 6 texels of packed u16 pairs per
particle, 128/group): position (wrapped), velocity, a per-particle random
basis (four chained LCG+TEA-style hashes), age/size. Spawn in the
`[5]/[6]` box, advect by wind `[8]` rotated through the per-particle basis,
die/clamp against the heightmap and clip planes, fade by camera distance,
box edges, depth and banding. The pixel shader is a trivial textured
blend (`t1`, `t2`, `cb1[2]` — 2 programs).

## Record ↔ CB

The CPU fills the CB from the 4,272-byte volume record; the mapping is not
byte-identical (the CB mixes record fields with runtime state like eye
position and time). Confirmed pairs so far: record +0x34 (count) →
`[15].w`; record emitter ints (tilesX, tilesY, frameCount) → `[15].xyz`;
record extent/keyframe/color fields plausibly feed `[6]`/`[10]`, the
envelope, and the draw color, untraced.
