# wowdev.wiki findings — sky, light data, screen effects

Current, correct facts only, read from the 12.1.0 client's compiled
shaders (container format: `BLS.md`). Full evidence trail:
`HISTORY.md` §19, §20. husk exports none of this yet; it is
recorded for the world renderer (`WORLD_COMPLETENESS.md`).

## Confirmed — verified

- **Sky cone** (`Day_night_cycle.md`): the sky is a mesh coloured by
  per-vertex colours, as the wiki says. `dnsky` takes its colour from the
  vertex colour, plus a sun glow and dither.
- **Colour-grading LUT** (`DB/LightData.md`): the wiki's 1024×32 strip
  lookup with a lerp between slices is exactly what `ffxcolorgrading`
  and the M2 combiners' colour-grading permutation compute.
- **FFXGlow** (`Rendering/ScreenEffects.md`): `ffxglow` is exactly
  `mix(screen, blur, blurAmount.z) + blur²·blurAmount.w`.

## Fog — disagrees with the wiki's MoP model (`Day_night_cycle.md`)

The wiki's `s_fogParams` (linear fog from `fogEnd · fogScalar` to
`fogEnd`) is gone. Every forward shader (M2, WMO, terrain, liquids,
decals) evaluates the same fog, with these verified parts:

- A frame holds **12 fog records** of 14 `float4`s. The DX12 build types
  the buffer as `SceneData { PSFog[12], … }`.
- Each draw names **up to 4 records** (the count is clamped to 4), each
  with a weight. The results are summed by weight, so the client can
  blend zone lights. This is the shader side of `MOGP.fogIds[4]`.
- One record combines an exponential fog past a start distance, a height
  fog (a plane plus a cubic falloff, with its own density), two distance
  curves, a linear cutoff at the fog end, a near/far colour gradient, a
  height-fog colour and a sun-fog term with its own colour.
- A record type other than 0 selects a simple power fog instead.
- **Two-set mode** (doodads in WMOs): one set from the list, one single
  record, blended by the interior/exterior factor. The wiki
  (`Rendering/Lighting.md`) has doodads choose between two fog sets; the
  shader blends them. The flag that enables the mode is client-side.
- After the record fog, an optional **froxel volume** adds in-scattered
  light and transmittance (a 3D texture, square-root depth slices up to
  the fog end).

Mapping record fields to LightData columns (fog end, density, sun fog
colour, fog height colour, fog end colour) is **inferred**; the field
table is in `../SHADER_FINDINGS/notes/fog.md`.

### Zone fog density

`LightData.FogDensity` reaches the shader's `[0].z` (and `FogHeightDensity`
its `[4].x`), the `k` of `exp(−e·k)` with `e` in yards past the fog start,
as `k = 0.0005 · max(D, 0.9)` and `0.0005 · H`. No wiki page states this.
husk derives it as `fog_density_to_extinction` (`src/zone_fog_extinction.cpp`).

| Part | Grade | Evidence |
|---|---|---|
| `exp(−e·k)`, `e` in view-space yards past the start, `k` in `[0].z` / `[4].x` | verified | dumped shaders, `../SHADER_FINDINGS/notes/fog.md` |
| `k = D · 0.0005` | ref | WebWowViewerCpp `MapSceneRenderer.cpp` (`densityMultFix = 0.00050000002`) |
| main density floored at 0.9 after the zone-light blend | ref | WebWowViewerCpp `DayNightLightHolder.cpp` (`fmaxf(FogDensity, 0.89999998f)`) |
| `D ≤ 0` filled with `1.5 + 5.5·(1 − s/(min(farClip, 700) − 200))`, `s = max(FogEnd, 10)·(1 − clamp(FogScaler, −1, 1))`, 1.5 past the limit | ref | WebWowViewerCpp `fixLightTimedData`; noggit-red `Sky.cpp` computes the same ramp as 3.3.5's power-fog exponent (`_fog_rate`), an independent project |
| far clip ≥ 700 yards, so the limit is 500 | inferred | husk has no far clip; modern view distances are past 700 |
| `H = D` (filled, unfloored) when `FogHeightScaler` is 0 | ref | WebWowViewerCpp `fixLightTimedData` |
| a map with `Map.Flags[1]` 0x2 forces `D = 1` | ref, not applied | WebWowViewerCpp; 7 maps in 12.1.5 (1431, 1460, 1744, 1760, 2236, 2987, 3009), none a continent; the rule takes no map |

Why the reimplementation is taken as close to the client: its per-record
struct is named `PSFog`, the DX12 build's own type name
(`../SHADER_FINDINGS/notes/dxil_names.md`), its 14 `vec4`s carry the
roles the dumped shader gives `[0]`–`[13]` field for field, and its
constants are the decimal expansions a decompiler prints for floats
(`0.00050000002`, `0.89999998f`, `0.00000011920929f` = `FLT_EPSILON`). It
cites no client address. wow.export's map viewer (`FogDataProvider.js`,
`DENSITY_MULT = 0.0005`, same fill and floor) is a copy, not a second
source.

Value range (12.1.5 `LightData`, 19,881 keys; `D > 0` on 19,701): `D`
5th/50th/95th percentile 1.5 / 4.5 / 25, most common 4.5 (3,674 keys);
`H > 0` median 3, 90th percentile 10. Half fog (`ln 2 / k` past the
start) lands at 924 yards for `D` = 1.5, 308 for 4.5, 55 for 25, 11 for
130; 95% fog at 4.0k, 1.3k, 240 and 46 yards. Copying `D` as `k` would
reach half fog within 0.2 yards at the median. Outliers: `D` ≥ 200 (up
to 32,974) on 71 keys of 20 parameter sets (557, 1341, 1905, 2069, 3265,
…, 7174), fog opaque within a few yards: what scenes use them is
unchecked. 180 keys have `D = 0`,
nearly all with `FogEnd` 0, which the fill turns into 6.89
(`k` = 0.0034). `FogHeightScaler` is 0 on 10,202 keys, so they take
`H = D`; 371 keys have a height scaler and `H = 0`, no extinction inside
their height fog.

Static analysis of the client executable (finding the `0.0005f` constant
`0x3A03126F` where `cb_scene_data` is filled) would turn every ref row
above into verified; it needs a disassembler that is not in husk's flake.

## Scene constants — names from the DX12 build

The `dx_6_0` DXIL keeps HLSL type names (`BLS.md`). The shared pixel
constant buffers are `cb_scene_data` (fog records and screen
transforms), `cb_global_light` (scene light: hemispheric ambient, sun
direction and colour, specular), `cb_light_flash`, `cb_shadow_data`
(5 shadow cascades) and `cb_light_data` (clustered-light grid). Full
list: `../SHADER_FINDINGS/notes/dxil_names.md`.

## Liquids

- Liquid shader families follow `LiquidMaterial`'s classes by name
  (`water`/`procwater*`, `magma`/`procmagma`, `mercury`/`procmercury`,
  `liquidfog`, `liquiddebug`), plus `procswamp`, `procleyline*` and a
  `medium*` tier with no wiki counterpart (**inferred**, from names).
- Inside a family, every permutation digit is a render option: the
  reflection source, height-map detail, a depth texture, discard plus
  depth. None encodes a liquid type. Per-type data reach the shader as a
  parameter block: absorption colours near and far, specular colour and
  exponent, normal strengths, a 2×2 UV rotation and a tile scale in the
  vertex shader. Which LiquidType/LightData columns fill which register
  is **hypothesis** (`../SHADER_FINDINGS/notes/liquids.md`).

## Third-party passes in the client set

Identified by type and groupshared names in the DX12 DXIL: AMD FidelityFX
Parallel Sort (`fps_*`), FidelityFX CACAO (`cacao_*`), FSR 1 (`fsr`),
Intel ASSAO (`assao_*`), Intel CMAA2 (`cmaa2_*`), Intel VALAR
variable-rate shading (`valar`, `valar_lp`), and Slug text rendering
(`slug`).

M2 combiner, particle and ribbon formulas: `M2/rendering.md`. Terrain
blend: `ADT.md`. WMO materials: `WORLD.md`.
