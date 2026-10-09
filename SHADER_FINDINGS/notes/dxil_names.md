# Names from the `dx_6_0` DXIL listings

The `dx_6_0` listings (`asm/*.ll`) keep three kinds of HLSL names that the `dx_5_0`
bytecode loses:

- type names of constant buffers, structured buffers and their structs, with full layouts
  (`%struct.SceneData = type { [12 x %struct.PSFog], ... }`);
- the names of groupshared variables (`@"\01?neighborhood@@..."`);
- in ray-tracing libraries, the names of resources and entry points.

Resource variable names are stripped (`!""`) in ordinary shaders.

`SHADER_FINDINGS/scripts/dxil_types.py example_exports/shaders SHADER_FINDINGS/notes/dxil_types.json` (output gitignored) collects every binding (type
name, register, containers) and every struct body. Everything in this file is
**verified** as a name or layout. A link from a name to a role is marked **inferred**.

## Constant buffers

| Slot (`dx_5_0`) | Type name | Struct | Layout (registers) | Agrees with |
|---|---|---|---|---|
| pixel `cb5` | `cb_scene_data` | `SceneData` | `PSFog[12]` (14 each, 0–167), then 5 × `float4` (168–172) | `fog.md`: 12 records of 14 before `[168]` |
| pixel `cb6` | `cb_render_target_data` | `RenderTargetData` | 2 × `float4` | `cbuffers.md` |
| pixel `cb7` | `cb_light_flash` | `PSLightFlashData` | 2 × `float4` | Role in `lighting_model.md` §5 |
| pixel `cb8` | `cb_global_light` | `PSGlobalLight` | 11 × `float4` | `m2_draw_constants.md` |
| pixel `cb4` | `cb_light_data` | — | 36 bytes | clustered-light grid |
| pixel `cb9` | `cb_shadow_data` | `PSShadowData` → `ShadowMapInfo` | `ShadowMapCascade[5]` (`float4x4`, `float4`, 2 × `float`: 6 registers each, 0–29), `float4[16]` (30–45), `int4` (46) | `cbuffers.md`: 6 per cascade, PCF offsets from 30, count at 46. There are 5 cascades |
| pixel `cb0`, M2 | `cb_generic_pre` | `GenericPre` | 6 × `float4` (0–5), `float4[4]` (6–9), 3 × `float4` (10–12), `float4[2]` (13–14), 9 × `float4` (15–23), `int4` (24), `GenericPrePerInstance[256]` (8 × `float4` each, from 25) | `m2_draw_constants.md`. `[24]` is integer; `[6..9]` is one array (the material-specific block). Some containers use a `hostlayout` variant with `float4[2][2]` at 10–13 |
| pixel `cb1`, combiners | `cb_m2Uber` | `M2UberPSConstants` | `int4` | `cb1[0].x` is the integer material ID of the `switch` |
| pixel `cb2`, combiners | `cb_gradient` | `Gradient` | `float4[3]`, 4 × `float4` | the slot-bit-7 overlay is a gradient |
| pixel `cb2`, `uber` | `cb_uber` | `PixelUberConstants` | `float4[4]` (0–3), `int4` (4), `float4` (5), `float4[10]` (6–15) | `wmo.md`: `cb2[4].x` material and `.y` effect 25 are integers; `cb2[5]` tint |
| vertex `cb2`, `uber` | `cb_uber` | `VertexUberConstants` | 2 × `int4` | `wmo.md`: `cb2[0].x` material, `cb2[1]` UV modes are integers |
| pixel `cb1`, terrain | `cb_terrain` | `PSTerrain` | `float4[4]` (0–3), `float4` (4), `float4[4]` (5–8), `float4[4]` (9–12), `float4[4][4]` (13–28), 3 × `float4` (29–31), `float4[4][4]` (32–47), `float4[4]` (48–51), `float4` (52) | `terrain.md`: flags at 0, height scale from 5 and offset from 9 (one `float4` per group of 4 layers), layer transforms from 13, baked shadow and LUT enables at 29, screen transforms 30/31, distance curves from 32, explicit LODs from 48, control at 52 |
| pixel `cb2`, terrain | `cb_ps_batch` | `BatchConstants` | | |
| vertex `cb4` | `cb_viewproj` | `ViewProj` | 2 × `float4x4` | projection |
| vertex `cb0`, M2 | `cb_model2` | `VSModel2` | Seven layouts across permutations. All share 3 × `float4` (0–2), `float4[2][2]` (3–6), 2 × `float4` (7–8). Registers 9–14 are `float4[2]` + `float4` or `float4[3]` (9–11), then `float4[3]` or `int4` + `float4[2]` (12–14). Skinned layouts end with `float4[3][256]` bones from 15 (783 registers in all) | `m2_draw_constants.md`: header 0–14, UV transforms at 3–6, colours at 7–8, bones from 15. `color_proj_*` read 9–10 as a pair and 11 alone, as the type says |
| vertex `cb1`, `diffuse_edgefade_*` | `cb_model2_edge_fade` | `VSEdgeFade` | `float4` | |
| vertex `cb3`, particles | `cb_model2_fog` | `PSFog` | one fog record | `cbuffers.md` |
| `cb3`, `default_*` | `cb_model_render_data` | `ModelRenderData` | 4 × `float4` | |
| `detaildoodad` | `cb_detail_doodad_data`, `cb_world_shadow_data` | `PSWorldShadowData` | | |
| `particleupdate` `cb0` | `cb_0` | `ParticleUpdateCB` | `float4`, 2 × `float4x4`, 16 × `float4`, `float4[128]` = 153 registers | `particles.md`: curve table from 25 |
| `texturecompositing` | `cb_texture_compositiong` (sic) | `CS_TextureCompositing` | 3 × `int4` | `compute.md` |
| `waterfall` | `cb_waterfall` | `PS_Waterfall`, `VS_Waterfall` | | |
| `particulatecompute`, `particulatevolume` | `cb_particulate_volume` | `VS_ParticulateVolume` (mixed scalars) | | |
| `shadowrt` | `cb_RayGen`, `ShadowRT_InstanceConstants` | `ShadowRT_RayGenDeferredData`, `ShadowRT_InstanceData` | | below |

Other struct names, without a binding we decoded: `LightBuffer` (`PointLight`),
`LightBufferSpotlight` (`Spotlight`), `LightBufferMatrices`, `TerrainLightMap*`,
`Lightning`, `LightningStrike`, `LightningT1T2T3`, `WaterFogPoly`, `VolumeFogData`,
`FogDepth`, `CSVolumeFog`, `DirLight`, `PSConstantsLighting`, `PSConstantsFog`,
`AnimaCB`, `DetailDoodad`, `ShadowMapTerrain`, `ShadowMapSL`, `PrepassTerrainLow`,
`ObjectFillBones` (`float4[3][256]`, the bone palette), `UI_PS_AlphaGradientData`,
`SlugVertexConstants`.

## Structured buffers and vertex formats

| Type | Layout | Agrees with |
|---|---|---|
| `ShaderLight` (`t21`; 34 containers) | `float4x4` (bytes 0–63), 7 × `float4` (64–175), 4 × `int` (176–191) | `lighting_model.md` record. Bytes 0–63 are a matrix; the type field at 176 is an integer |
| `Buffer<uint>` (`t22`) | | the clustered light lists |
| `ComputeParticle` (`u1`) | 5 × `float4` = 80 bytes | `particles.md`: 80-byte particle state |
| `ParticleVertex` | 3 × `float4` = 48 bytes | `particles.md`: four 48-byte vertices per particle |
| `ParticlePresortData` | 7 × `float4` = 112 bytes | the 112-byte sort record |
| `M2Vertex` | `float3` pos, `uint` bone weights, `uint` bone indices, `float3` normal, `float2[2]` UV | The wiki's `M2Vertex` (`md/M2.md:1555`), with the 4 × `uint8` arrays packed in a `uint` |
| `GxVertexPBNT2` | same as `M2Vertex` | `m2skincs` input |
| `GxVertexP` | `float3` | `m2skincs` output (position only) |
| `WMOVertex` | `float3` pos, `float3` normal, `uint[2]` colours, `float2[4]` UV, `uint` | MOVT, MONR, MOCV and the second MOCV (`CVERTS2`), up to four MOTV sets (the wiki's client array has 3), and a final `uint` (MOC2 blend weights, read by material 23; **inferred**) |
| `M3Vertex` | `float2[1]` (as declared in `texcoordexplode`) | |
| `PS3UnivVertexFmt` | `float3`, `uint`, `uint`, `float3`, `uint` | particle custom-mesh emission |
| `AnimaCurveNode` | 4 × `float4` | `anima`, `animacompute` |
| `BoneTransform` | `float4x4` | `material3_mesh_vs` |
| `TriangleOut`, `ShadowRT_TriangleTexCoords` | `float2[3]` | |

## Third-party code identified by names

| Containers | Library | Evidence |
|---|---|---|
| `fps_count_uint`, `fps_reducecount`, `fps_scanprefix`, `fps_scanadd`, `fps_scatter_uint`, `fps_setupindirectparameters` | AMD FidelityFX Parallel Sort (4-bit radix sort) | `FFX_ParallelSortCB`, `gs_FFX_PARALLELSORT_Histogram`, `..._LDSSums`, `..._LocalHistogram`; digit `(key >> shift) & 15` |
| `cacao_*` | AMD FidelityFX CACAO | name; `s_BlurF16Front_4`, `s_PrepareDepthsAndMipsBuffer`, `s_BilateralUpscaleBuffer` |
| `fsr` | AMD FidelityFX Super Resolution 1 | name, `min16float` textures |
| `assao_*` | Intel ASSAO | `SSAOConstantsBuffer` |
| `cmaa2_*` | Intel CMAA2 | `g_groupShared2x2FracEdgesH`, `g_groupSharedBlendItems` |
| `valar`, `valar_lp` | Intel VALAR (variable-rate shading from luminance) | below |
| `slug` | Slug text rendering | `SlugVertexConstants`, `ParamStruct` |
| `guidedfilter*` | Guided filter | `GuideLocal`, `SignalLocal`, `XYLocal` |

## `valar` (16×16 threads, `RWTexture2D<uint>` output)

Per pixel: luma `= dot(rgb², (0.2126, 0.7152, 0.0722))`, and the absolute differences to
the left and upper neighbours. With `CB0[1].w ≠ 0` each difference is divided by
`min(luma, neighbour) + CB0[1].z · (1 − saturate(50·minNeighbourhood − 2.5))`
(groupshared `neighborhood`). Wave and group sums (`waveLumaSum`, `waveLumaSumX`, `…Y`)
give tile means. The tile threshold is `T = (CB0[1].x + meanLuma) · CB0[0].w`. Per axis:
`sqrt(meanErr) ≥ T` → full rate, `< T` → 2×, `CB0[1].y · sqrt(meanErr) < T` → 4×. The Y
rate is then adjusted so that the pair is a valid D3D12 rate (no 1×4 or 4×1). The stored
value is `(rateX << 2) | rateY`, the D3D12 shading-rate encoding. `valar_lp` loads 9
texels per thread and uses no wave operations; it was not read further.

## Ray-traced shadows (`raytracing/dx_6_0/shadowrt`)

Named resources: `SceneBVH`, `LinearDepth`, `Normal_Exteriorness`, `BlueNoise`, `Output`,
`DiffuseAlpha`, `DiffuseAlphaSampler`, `TexCoords`. Entry points: `RayGen`,
`ShadowCasterMiss`, `AlphaTestShadowCasterHit`, `StippleShadowCasterHit`,
`IgnoreShadowCasterHit`.

`RayGen`, per pixel:
1. Position from `LinearDepth`; geometric normal from the cross product of the
   neighbouring positions. The origin moves `0.001` along the normal, on the side given by
   the sign of `dot(n, L) − 0.1·dot(n, P/|P|)`.
2. `e` = `Normal_Exteriorness.a`. If `e < 1`, trace the instance mask 2; if `e > 0`,
   trace mask 1. The result is `lerp(result2, result1, e)`.
3. Each trace casts up to 8 rays, flags 12 (accept first hit, skip closest hit),
   `tmax = 10000`. The directions are a 20-point table `SamplePattern`, rotated by a
   `64×64` blue-noise angle and mapped through the light basis `cb_RayGen[2..4]` (cone
   spread in `.xy`, direction in `.z`). After 3 rays, if all 3 agree, it stops with 0 or 1.
   Otherwise the result is misses / 8.

The hit shaders:
- `AlphaTestShadowCasterHit` samples `DiffuseAlpha` at the interpolated `TexCoords` and
  ignores the hit below 128/255.
- `StippleShadowCasterHit` ignores the hit where a per-instance fade is below the
  blue-noise value (screen-door fade), then alpha-tests.
- `IgnoreShadowCasterHit` always ignores.
- `ShadowCasterMiss` sets the lit bit.

`compute/dx_6_0/shadowrt` and `pixel/dx_6_0/shadowrt` run the same algorithm with inline
`RayQuery`. `lightbufferomnishadow` and `lightbufferspotshadow` are the light-buffer
passes (`lighting.md`) with a `RayQuery` shadow test per light. `terrainlightmapomni`,
`terrainlightmapspot` and `terrainlightmap` bind an acceleration structure at `t10` and
`ShaderLight` records. These match the wiki's MPL3 flag "cast (raytraced?) shadows (on
D3D12 + min shadowrt level 2)" (**inferred**).

Mask 2 and mask 1 split the scene by the `Normal_Exteriorness` weight. Mask 2 is taken
for interior pixels and mask 1 for exterior pixels; which geometry each mask holds is
not visible in the shaders.
