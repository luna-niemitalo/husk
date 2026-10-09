# M2 per-draw constants

Per-draw constant layout (DXIL type `cb_generic_pre` / `GenericPre`, `dxil_names.md`) for the M2 families: combiners, `guild*`, `illum`, `litsphere*`,
model effects, particles. Every role below comes from the code. "Source" columns are
**inferred** links to the M2 format (wiki names; `reconciliation.md` explains the
evidence). Registers that no shader reads are omitted.

## Pixel `cb0` (25 registers; 2072 with per-instance lights)

| Register | Role | Families | Source |
|---|---|---|---|
| `[0].x` | Fog list entry 0; colour record in two-set mode | all forward | zone light / fog |
| `[0].y` | Fog record of the second set (two-set mode) | all forward | interior fog |
| `[0].z` | Fog colour record | all forward, particles | |
| `[1].y` | ≠ 0: unfogged, skip fog | all forward | `M2Material` flag 0x2 |
| `[1].w` | ≠ 0: apply the froxel volume | all forward, particles | |
| `[2].xyz`, `[2].w` | Fog list entries 1–3, list count | all forward | |
| `[4].x` | MSAA sample count for the per-sample alpha test | all forward | |
| `[4].y` | Dither opacity (slot bit 6: discard where the hash < `1 − [4].y`) | all forward | model fade / LOD crossfade |
| `[5].x` | Transparency factor, × edge fade `v2.x`; multiplies alpha | all forward, particles | `M2TextureWeight` (element alpha) |
| `[5].y` | Env-map mode: 0 none, 1 texture 0, 2 texture 1, ≥3 both | combiners, particles | shader-ID env bits |
| `[5].z` | > 0, simple-lighting variants: decal combine `lerp(lit vertex colour, texture, alpha)` | combiners | `GxTexOp_Decal` |
| `[5].w` | ≠ 0 with the colour LUT: alpha × `min(luma · 20, 1)` | combiners | |
| `[6]` | Material-specific, see below (`[6..9]` is one `float4[4]` array in the type) | combiners, effects, particles | |
| `[7]`, `[8]`, `[9]` | Material-specific (effects, particles) | effects, particles | |
| `[10]`, `[11]`, `[12]` | Guild background, emblem and border colours (`guild*`); particle UV scales (`[10]`) | guild, particles | texture types 15–17 |
| `[14]` | Particle UV-remap modes | particles | |
| `[15]`, `[16]` | Spherical highlight: centre and size; colour and strength | all forward | |
| `[17]` | Desaturate: target weights `.xyz`, amount `.w` | all forward | |
| `[18]`, `[19]`, `[20]`, `[21]` | Interior light set: sky, ground, horizon ambient; direct colour | all forward | WMO interior lighting (MOHD ambient / MAVD) |
| `[22].x`, `[22].y` | `*_Wgt` combiner weights | combiners | texture weight |
| `[23].x`, `[23].y` | Soft-fade scale and exponent (slot bit 5) | all forward | |
| `[23].z` | Fog-amount multiplier | all forward | |
| `[24].x` | (`[24]` is an `int4`.) Blend class: 1 alpha → 1; 2, 5 alpha test; 3 alpha test per sample (MSAA); 4 premultiplied additive | all forward | `M2Material.blending_mode` via the client |
| `[24].y` | Lighting mode: 0 unlit, 1 lit, 2 lit (`uber`: + baked light); > 2 → 0 | all forward | `M2Material` flag 0x1, blend mode |
| `[24].z` | Two-set fog mode | all forward | doodad in a WMO |
| `[24].w` | Interior lighting: blend the scene light toward `[18..21]` by `v3.w` | all forward | doodad in a WMO interior |
| `[25 + 8i ..]` | Per-instance light sets (slot bit 0), indexed by `v4.w` = `SV_InstanceID`; same layout as `cb8[0..6]` below | combiners | instanced doodads |

`[6]` by container:

| Container | `[6]` |
|---|---|
| combiners (any) | `.xyz` palette indices / 255 and `.w` enable for the mask-and-palette recolour (`t15`, `t17`, `t18`) |
| `combiners_uber_3_3` | `.z`: emissive weight in 15 and 25; all of `[6]`: constant colour in 35 |
| `combiners_mod_*dual_crossfade` | `.y`, `.z`: crossfade weights |
| `litsphere_additive_*` | `.rgb`: matcap tint |
| `avatar` | `.rgb`: tint colour (`[7]`: specular colour) |
| `skin` | `.x`: rim strength and transition progress; `.y`: edge width; `.zw`: noise offset |
| `procedural` | `.x` progress, `.y` edge, `.zw` noise offset (`[7]`: noise UV transform) |
| `edgeglow`, `shadowmonk` | rim and tint colours (`[7]`: modes) |
| `blueprint` | `.y` outline threshold, `.z` outline width (`[7]` fill, `[8]` outline colour) |
| particles | `.x` combine flags (three-texture), `.y`, `.z` near-camera fade (`[7]` intensity) |

## Vertex `cb0` (M2 vertex shaders)

| Register | Role | Source |
|---|---|---|
| `[0]` | Clip plane (`o6.x = dot([0].xyz, P) + [0].w`, clip variants) | reflection / water clip |
| `[1].x` | → `v3.w`: interior/exterior blend | WMO `QueryLighting` alpha for doodads (inferred) |
| `[1].y`, `[2].x` | Depth clamp: view `z = max(z, [1].y)` when `[2].x > 0` | |
| `[1].z`, `[1].w` | Generated-UV mode and source space (`_env` variants) | |
| `[3..4]`, `[5..6]` | UV0 and UV1 transforms (2×3) | `M2TextureTransform` |
| `[7]` | Material colour (×0.5 into `v1`, ×2 back in the pixel shader) | `meshResColor` (`M2Color` × diffuse × transparency) |
| `[8]` | Additive colour (dropped by the `nocb8` digit) | |
| `[9..11]` | Position transform for generated UVs | |
| `[12..14]` | Model/world → view (3×4) | |
| `[15 + 3·i]` | Bone `i` (3×4), 256 bones | bone palette |

## Scene light (`cb8` = `cb_global_light` / `PSGlobalLight`, pixel; also the per-instance set)

| Register | Role |
|---|---|
| `[0]` | Sky ambient |
| `[1]` | Horizon ambient |
| `[2]` | Ground ambient |
| `[3]` | Direct light direction (`L = −[3]`, view space) |
| `[4]` | Direct-light position, for the distance falloff |
| `[5]` | Falloff start `.x`, `1/range` `.y`, enable `.z > 0` |
| `[6]` | Direct light colour |
| `[7]` | Specular colour (terrain) |
| `[8]` | `.y` specular scale (terrain); `.z` light-buffer and AO strength; `.w` baked-light scale (`uber`) |
| `[9]` | Additive term in the simple-lighting path |
| `[10]` | Up vector (hemisphere axis) |

`[0..2]` match LightData's "Horizon Ambient Color" and "Ground Ambient Color" fields and
the ambient colour (**inferred**). The falloff in `[4..5]` makes the "sun" slot a
positional light when enabled. That fits the wiki's WMO doodad light, where `MODD.color`
alpha selects a MOLT light or a group-centred direction (**inferred**).
