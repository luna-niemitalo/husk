# wowdev.wiki findings — sky, light data, screen effects

Current, correct facts only, read from the 12.1.0 client's compiled
shaders (container format: `BLS.md`). Full evidence trail:
`../WIKI_FINDINGS_HISTORY.md` §19, §20. husk exports none of this yet; it is
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
