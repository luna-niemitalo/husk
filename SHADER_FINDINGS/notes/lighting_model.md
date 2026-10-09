# Lighting model (forward shaders)

The full per-pixel lighting of the combiner forward pass. Model effects, `uber`, particles
and (with its own material terms) terrain run the same code. Code references are to
`pixel/dx_5_0/combiners_mod/asm/0000.asm` (base) and the slot-bit variants named.
Constants are described in `m2_draw_constants.md`.

## Inputs

| Symbol | Value |
|---|---|
| `albedo` | Combiner diffuse colour × `v1.rgb` × 2 (material colour), after recolour and LUT |
| `tex` | Combiner diffuse colour before the vertex colour (used by the light flash) |
| `N` | `normalize(v4)` (view space) |
| `P` | `v3.xyz` (view space) |
| `w` | `v3.w` interior/exterior blend; `cb0[24].w` enables it |
| `mode` | `cb0[24].y` (0 unlit, 1 lit, 2 lit) |
| `S`, `O` | Shadow mask `t12.x`, occlusion `t12.z` (slot bit 3; otherwise 1) |

## 1. Light set

```
sky     = lerp(cb8[0], cb0[18], w)     // only when cb0[24].w; otherwise the cb8 value
horizon = lerp(cb8[1], cb0[20], w)
ground  = lerp(cb8[2], cb0[19], w)
dirCol  = lerp(cb8[6], cb0[21], w)
L       = −cb8[3]
att     = cb8[5].z > 0 ? 1 − saturate((|cb8[4] − P| − cb8[5].x) · cb8[5].y) : 1
```

With slot bit 0, `cb8[0..6]` come from the per-instance set instead.

## 2. Ambient and direct

```
if mode == 0:
    amb = 0.7 + 0.3·S          // unlit; S = 1 without slot bit 3
    dir = 0
else:
    u    = dot(N, cb8[10])
    hemi = u ≥ 0 ? lerp(horizon, sky, u) : lerp(horizon, ground, −u)
    amb  = hemi · (0.7 + 0.4 · (dot(N, L)·0.5 + 0.5)) · O
    dir  = saturate(dot(v4, L)) · dirCol · S · att
lit = (amb + dir) · albedo
```

`uber` (WMO), lighting mode 2, adds the baked vertex light inside `amb`:
`hemi += 2·MOCV · cb8[8].w` (`wmo.md`).

The simple-lighting variant (combiner bit 10 and its equivalents) replaces §2–§4 with
`light = saturate(v1·2 · saturate(lb + sky + saturate(dot(v4, L))·dirCol·att) + cb8[9])`,
with `lb` the unsquared light buffer and `sky` the (interior-blended) sky ambient. The
result is multiplied by the texture, or decal-combined with it (`cb0[5].z`). There is no
hemisphere, no wrap and no gamma combine.

## 3. Local lights

```
k  = cb8[8].z
lb = mode ≥ 1 ? localLight · (O·k − min(k, 1) + 1) : 0
```

`localLight` is either the light buffer `t11` at `v0·cb5[168]` (default) or the clustered
sum (slot bit 2; the formula also uses `cb8[8].z`):

```
for each light in the cluster's list (≤ 32):
    Lv  = rec.pos − P;  d = |Lv|
    s   = smoothstep(saturate((d − rec.B.w) / (rec.A.w − rec.B.w)))
    col = lerp(rec.B.rgb, rec.A.rgb, s)                 // colour gradient over distance
    point: a = 1 − saturate(rec.f.x · (d − rec.r0))
    spot:  a = 1 − saturate(rec.f.x · (dot(rec.dir, −Lv) − rec.r0))   // axial distance
           c = dot(rec.dir, −Lv/d)
           a *= c < cone.y ? 0 : c ≤ cone.x ? pow(|(c − cone.y)·cone.z|, rec.f.y) : 1
    localLight += (a · col)² · max(dot(Lv/d, N), 0)
```

The light-buffer pass (`lightbufferomni`, `lighting.md`) stores the same quantity per light:
`(attenuation · colour)² · max(N·L, 0)`.

### Clustered light record (192 bytes, structured buffer)

| Offset | Field | Read by |
|---|---|---|
| 64 | Position `xyz` | pixel, `clusteredshading_assignlights` |
| 80 | Spot direction `xyz` | pixel, assign |
| 96 | Colour A `rgb`, distance `A.w` | pixel |
| 112 | Colour B `rgb`, distance `B.w` | pixel |
| 128 | `.x` attenuation start `r0`; `.y` range (culling) | pixel; assign (`.y`) |
| 136 | `.x` falloff scale `f.x`; `.y` spot exponent `f.y` | pixel |
| 160 | Cone: `.x` inner cosine, `.y` outer cosine, `.z` scale | pixel; assign (`.y`) |
| 176 | Type: 0 point, 1 spot | pixel, assign |
| 0–63, 144–159, 180–191 | Not read by these shaders | |

### Data source: `_lgt.wdt` lights

The wiki's map lights (`documentation/wowdev-wiki/md/WDT.md`: MPL2/MPL3 point lights,
MSLT spot lights, MLTA animation) match the record (**inferred**; offsets from the code,
fields from the wiki):

| `_lgt.wdt` field | Shader side |
|---|---|
| `position` | record 64 |
| `attenuationStart` | record 128.x (`r0`) |
| `attenuationEnd` | record 128.y (culling range) and 136.x (`1 / (end − start)`) |
| `color`, `intensity` | record 96/112 colours (one colour when A = B) |
| MSLT `rotation` | record 80 (spot direction) |
| MSLT `innerAngle`, `outerAngle` | record 160 `.x`/`.y` as cosines, `.z` = `1 / (cos inner − cos outer)` |
| MPL3 `rotation`, `textureIndex` (light cookie) | Not used by the clustered path. The light-buffer passes have the cookies: `lightbufferomni` bit 1 (cube cookie rotated by `cb2[0..2]`) and `lightbuffer_spotlight` bit 1 (2D cookie `t3`) |
| MPL3 `flags & 1` "cast (raytraced?) shadows (on D3D12 + min shadowrt level 2)" | The `dx_6_0`-only `lightbufferomnishadow`, `lightbufferspotshadow` and `raytracing/shadowrt` |
| MLTA amplitude/frequency/function | Not in shaders: the client animates intensity |

Record bytes 0–63 are not read by any pixel shader or by the assignment pass. The DXIL type
`ShaderLight` declares them as a `float4x4` and the fields at 176 as integers
(`dxil_names.md`). The matrix's use is not visible in these shaders; a cookie projection is
one candidate.

## 4. Combine (gamma 2.0)

```
rgb = sqrt(lit² + lb · albedo²)
```

Sun and ambient are computed on gamma-space values and squared. Local lights are already
squared per light. The sum is taken back with `sqrt`. This is a gamma-2.0 approximation of
linear-space accumulation (**inferred** interpretation; the math is **verified**).

## 5. Light flash (`cb7`)

Added after the combine, scaled by `(1 − w)²` (exterior weight):

```
I = cb7[0].w;  L7 = cb7[0].xyz;  C7 = cb7[1].rgb;  T = cb7[1].w
if I ≥ 0:  add = tex · C7 · I · saturate(dot(v4, L7))                    // front light
else:      b   = saturate(−dot(v4, L7)) · (1 − (saturate(alpha)·T)²)
           add = tex · C7 · (−I) · b                                    // back-light / transmission
           alpha *= 1 − saturate(−I · (0.8 − 0.75·T)) · saturate(5b)    // backlit surfaces turn transparent
rgb   += add · (1 − w)²
alpha  = lerp(alpha, alpha', (1 − w)²)
```

The DXIL names the buffer `cb_light_flash` (struct `PSLightFlashData`, `dxil_names.md`).
A flash is a short-lived
directional light. Weather lightning is the likely source: the corpus also has
`LightningStrike` and `Lightning` structs for drawing the bolts (**inferred**). The wiki
has no counterpart. With a negative intensity the flash lights back faces and makes them
more transparent, which suits a flash behind thin geometry.

## 6. Post-lighting terms

In order:
1. **Spherical highlight** (`cb0[16].w > 0`):
   `t = (max(4·cb0[15].w − |P − cb0[15].xyz|², 0) / (4·cb0[15].w))^20 · cb0[16].w`, then
   `rgb = lerp(rgb, 3 · rgb · cb0[16].rgb · sqrt(1 − luma(lit)), t)`.
2. **Light flash** (§5).
3. **Desaturate**: `rgb = lerp(rgb, dot(rgb, cb0[17].xyz), cb0[17].w)`.
4. **Blend class 4**: `rgb *= cb0[5].x · v2.x` (premultiplied additive).
5. **Fog and froxel volume** (`fog.md`).
6. **Alpha**: `combinerAlpha · v1.w · v2.x · cb0[5].x` (the light flash adjusts it as above).

## Specular

The combiners have no specular term. Specular appears only in these families:

| Family | Term |
|---|---|
| Terrain | Blinn `pow(N·H, 20) · cb8[7] · att · S · cb8[8].y`, masked by the blended layer alpha (`specBlend`) |
| `avatar` | `pow(N·H, 18) · saturate(N·L) · cb0[7].rgb` |
| `skin` (bit 3) | exponent `t2.a · 128` |
| Water | Blinn with exponent `cb1[5].w`, colour `cb1[5].rgb` (`liquids.md`) |
| `illum` (combiner ID 34) | Normalised Blinn-Phong with Schlick Fresnel, below |

`H` uses the direct light direction `cb8[3]` throughout.

### `illum` (`pixel/dx_5_0/illum/asm/0000.asm`)

`t0` is the diffuse texture
(alpha = alpha), sampled at UV0 or the sphere map like the combiners (`cb0[5].y`). `t1`
holds gloss in `.x` and specular reflectance `F0` in `.y`, at the same UV.

```
n    = 64 · t1.x
spec = pow(sat(N·H), n) · (n + 2) / 8                       // normalised Blinn-Phong
F    = t1.y + (1 − t1.y) · (1 − sat(L·H))⁵                  // Schlick
rgb  = sqrt(lit² + lb · albedo²) + spec · F · specCol · sat(N·L) · 0.8 · att
```

`specCol` is `cb8[7]`, blended toward `cb0[21]` by `v3.w` when `cb0[24].w` is set.
The diffuse part also differs from §2:
- Mode 1: `dir = sat(0.5·N·L + 0.5) · dirCol · att · 0.8` (half-Lambert).
- Mode 2: ambient only, with no direct term.
