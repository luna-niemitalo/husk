# Post-processing and screen-space passes

Full-screen passes, mostly 1–4 programs each. A role is marked *code* when it was read
from the program, *name* when it comes from the container name plus its declarations only.

## Colour and tone

| Container | Programs | Role |
|---|---|---|
| `ffxcolorgrading` | 4 | *code* — `saturate(t0)`, then a 32³ LUT in `t1` (1024×32 strip, the same layout as combiner bit 1) unless `cb0[0].x ≠ 0` |
| `ffxcolorcorrection` | 8 | *code* — program 0 is `saturate(t0)`. Program 7: contrast/brightness/gamma, `pow(saturate(cb1[0].x * (c - 0.5) + cb1[0].y + 0.5), cb1[0].z)`. The other 6 programs were not read |
| `ffxcolorshift` | 2 | *code* — a 3×3 colour matrix (`cb1[0..2]`) blended with identity by `cb1[3].x` |
| `ffxfreesync2` | 1 | *code* — when `cb1[0].z > 0`: `pow(saturate(c), 2.2)` remapped to the range `[cb1[0].x, cb1[0].y]`. Otherwise a pass-through |
| `ffxdeath` | 2 | *code* — scene `t0` plus `cb1[0].x * t1²` (glow), reduced to luminance `L`, output `L + min(4L(1 - L), 1) * (0.325, 0.576, 0.659)`: a blue-grey monochrome |
| `desaturate` | 1 | *name*, 6 instructions |

## Glow and bloom

| Container | Programs | Role |
|---|---|---|
| `ffxglow` | 1 | *code* — `lerp(scene, glow, cb1[0].z) + glow² * cb1[0].w`, with scene `t0` and blurred glow `t1` |
| `ffxglowfade` | 1 | *code* — as `ffxglow`, then a lerp toward the colour `cb1[0].rgb` by `cb1[1].x` |
| `ffxglowwave` | 1 | *name* — 4 textures |
| `ffxgauss4`, `ffxbox4`, `fourtapblur`, `gauss5x5` | 1–2 | *name* — blur kernels (4 taps, or 5×5 with weights in `cb1[10]`) |
| `animabloom` | 1 | *name*, 4 instructions |

## Fog

| Container | Programs | Role |
|---|---|---|
| `ffxfogseed` | 1 | *code* — `t0(uv) * colour`: seeds the fog target |
| `ffxpropagatefog` | 1 | *code* — average of four taps (`t0`–`t3`), alpha reduced by `cb1[0].x` per pass |
| `ffxfogcombine` | 1 | *code* — polar lookup of `t0` around the screen centre (`atan2` and radius, scaled and offset by `cb1[1]`). Scene `t1` is desaturated by `cb1[0].x` and lightened toward white by `cb1[0].y`, then the fog colour is blended over it by its alpha |

## Depth of field

| Container | Programs | Role |
|---|---|---|
| `ffxdepthoffielddownsample` | 1 | *name* |
| `ffxdepthoffieldblur` | 1 | *name*, 134 instructions |
| `ffxdepthoffieldcombine` | 2 | *code* — blend factor `saturate(cb1[0].x * depth(t2) + cb1[0].y) * cb1[1].x` between the sharp `t0` and the blurred `t1`. The blurred image is desaturated toward luminance by `cb1[0].z` |

## Anti-aliasing and upscaling

| Container | Programs | Role |
|---|---|---|
| `fxaa` | 2 | *name* — 53 instructions |
| `ffxcmaaedge0`, `ffxcmaaedge1`, `ffxcmaaedgecombine`, `ffxcmaaprocessandapply` | 1, 1, 1, 2 | *name* — CMAA (edge detection, edge combine, process-and-apply) |
| `fsr` | 4 | *name* — 207 instructions, reads `t1`, `cb0[5]` |

## Ambient occlusion

| Container | Programs | Role |
|---|---|---|
| `assao_preparedepths*`, `assao_preparedepthmip` | 1 each | *name* — depth (and normal) preparation, full and half resolution |
| `assao_generateq0`…`q3`, `q3base` | 1 each | *name* — AO generation at four quality levels |
| `assao_generateimportancemap`, `assao_postprocessimportancemapa/b` | 1 each | *name* — adaptive importance map |
| `assao_smartblur`, `assao_smartblurwide`, `assao_nonsmartblur` | 1 each | *name* — blur |
| `assao_apply`, `assao_nonsmartapply`, `assao_nonsmarthalfapply` | 1 each | *name* — apply |
| `compute/cacao_*` | several | *name* — the compute AO path (see `compute.md`) |

## Special-effect screen passes

| Container | Programs | Role |
|---|---|---|
| `ffxnetherblur`, `ffxnethercombine` | 1, 1 | *code* (combine) — `t0 * (1.5 - k) + t1 * k` (`k = cb1[1].x`), pulled halfway toward its mean, tinted by `cb1[0].rgb` and blended by `cb1[0].w` |
| `ffxspectralsight` | 1 | *name* — 93 instructions, 4 textures |
| `ffxsketchedgedetect`, `ffxsketchlowquality`, `ffxsketchoverlay`, `ffxsketchtonemap` | 1, 3, 3, 2 | *name* — a sketch/drawn-look effect |
| `ffxcustom` | 2 | *name* — 125 instructions, 5 textures |
| `ffxunderwaterhigh`, `ffxunderwaterlow`, `ffxwaterwindow`, `ffxsubmarinewindow` | 2, 2, 3, 3 | *name* — listed in `liquids.md` |
| `refraction`, `refractionapply` | 32 slots / 4 programs, 1 | *name* |

## Outlines

| Container | Programs | Role |
|---|---|---|
| `outline_fill` | 10 | *name* — 4 instructions, constant colour: writes the outline mask |
| `outline_horzblur`, `outline_vertblur` | 2, 2 | *name* — separable blur of the mask |
| `outline_apply` | 2 | *name* — composites with `cb1[68]` |

## Copies and utilities

`copytex` (3), `resample` (2), `blendtextures` (15), `stencildownsample`, `depthremap` (5),
`depthrange`, `gbufferdownsample`, `positiondump` (5), `normaldump`, `i420` (YUV video),
`guidedfiltercolorh`/`v`, `invisiblequad`, `occlusionquery`, `dnglarequery`: all *name*,
3–80 instructions.
