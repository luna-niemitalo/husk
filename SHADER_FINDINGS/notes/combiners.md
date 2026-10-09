# Combiner pixel shaders (`pixel/dx_5_0/combiners_*` and relatives)

Slot indices, blob numbers and line numbers refer to `example_exports/shaders/pixel/dx_5_0/combiners_mod`
unless stated otherwise. Per-bit statistics come from `scripts/slot_bits.py <container dir>`.

Combiner formulas per ID, the ID → container map, and the `cb1[0].x` runtime dispatch in the
`combiners_uber_*` containers are in `reconciliation.md` §1.

## Containers that share one permutation key

These twelve containers have an identical slot→blob partition: 2048 slots, 1024 compiled,
386 distinct programs each, with zero programs shared between containers.

`combiners_mod`, `combiners_mod_dual_crossfade`, `combiners_mod_masked_dual_crossfade`,
`combiners_opaque`, `combiners_uber_2_2`, `combiners_uber_2_2_no_mod_fog_alpha`,
`combiners_uber_3_3`, `guild`, `guild_noborder`, `guild_opaque`, `illum`,
`litsphere_additive_opaque`.

The containers differ in their texture-combine stage (texture count, UV sets, how alpha is
formed). For example, `combiners_opaque` never reads `t0.a` and `combiners_uber_3_3` adds
`t1`, `t2`, `cb1`, `v5.zw` and `v6.xy`. Everything after the combine (lighting, fog,
shadows, output) is the same code selected by the same bits.

`combiners_mod_depth` and `combiners_mod_mod_depth` use the same compiled-slot set but a
different key. See "The `_depth` key" below.

## Slot bits

Bit 9 is set in every compiled slot. The 1024 slots with bit 9 clear are not compiled.
Nothing in the export identifies it. The slot hashes are per-program hashes (one per blob,
none for uncompiled slots). The 96-byte block headers hold binding masks: textures
`0x00078801` = t0/t11/t15–t18, samplers `0xe801`, cbuffers `0x1e1`. Neither encodes slot
features. The dx_6_0 container compiles the same slots. 34 of the partially compiled pixel
containers have exactly one bit set in every compiled slot, usually the second-highest,
at a position that depends on the slot count. The exception is `procedural`.

| Bit | Value | Effect | Evidence |
|---|---|---|---|
| 0 | 1 | Per-instance light set. Replaces the scene sun/ambient in `cb8[0..6]` with `cb0[25 + 8*round(v4.w)]`, 256 entries of 8 `vec4`s. The vertex shader writes `SV_InstanceID` to `v4.w`. | blob 0 → 1; `cb0[25]` becomes `cb0[2072], dynamicIndexed`; `v4.xyz` becomes `v4.xyzw` |
| 1 | 2 | Colour-grading LUT. A 32³ LUT in `t5`, laid out as a 1024×32 strip, is applied to lit colour before fog. With `cb0[5].w` set, alpha is also scaled by `min(luma*20, 1)`. | blob 0 → 2 |
| 2 | 4 | Clustered forward lighting replaces the screen-space light buffer `t11`. `cb4` holds the cluster grid (log depth slices), `t22` the per-cluster light lists (a count plus up to 32 indices, 33 uints per cluster), and `t21` the 192-byte light records (type 0 point, type 1 spot). | blob 0 → 4 |
| 3 | 8 | Shadow receiving. Without bit 2 it reads a screen-space shadow mask `t12` at `v0*cb5[169]`. With bit 2 it samples a cascaded shadow-map array `t6` (comparison sampler `s6`, cascades in `cb9`, PCF offsets in an immediate constant buffer). | blob 0 → 8; slot 524 → blob 12 |
| 4 | 16 | Depth/normal prepass. Outputs `o0.x = v3.z` (view depth) and `o1 = (N*0.5+0.5, 1 - v3.w)`, with alpha test on `t0.a` only. Only bit 6 still matters, so the 512 slots collapse to blobs 16 and 49. | `asm/0016.asm`, 22 lines |
| 5 | 32 | Soft depth fade. Scene depth `t10` at screen position, `saturate((scene - v3.z) * cb0[23].x) ^ cb0[23].y`, multiplied into the alpha factor. | blob 0 → 17 |
| 6 | 64 | Screen-door dither discard. A hash of `v0.xy` (`sin(dot(v0, (12.9898, 78.233))) * 43758.5`) mixed with a term from `v4.xy` is compared against `1 - cb0[4].y`. The alpha-test chain is marked `[precise]`, so prepass and colour pass produce identical coverage. | blob 0 → 33; blob 16 → 49 |
| 7 | 128 | Post-lighting overlay from `cb2[7]`: a 3-stop gradient indexed by luminance (`cb2[0..2]`, midpoint `cb2[2].w`), a fresnel rim toward `cb2[3].xyz` with power `cb2[3].w`, and an alpha fade by distance to a plane (`cb2[4]` point, `cb2[5]` normal). The result is blended with the original by `cb2[6].y`. | blob 0 → 66 |
| 8 | 256 | Only with bit 2. Discards fragments with alpha < 1/255 or near-black colour, and writes view depth `v3.z` to a second target `o1.x`. | slot 516 → 772, blob 4 → 130 |
| 9 | 512 | Always set. Meaning unknown. | |
| 10 | 1024 | Simpler lighting path. Flat ambient `cb8[0]`, plain N·L, `add_sat` clamp and an added `cb8[9]` replace the hemispheric ambient (sky/ground lerp on `cb8[10]`), the `0.7 + 0.4*(N·L*0.5+0.5)` wrap term, and the squared/`sqrt` light combine. | blob 0 → 194 |

## Base program (slot 512, blob 0)

Interpolators (sources in `m2_vertex.md`): `v1` material colour, halved by the vertex shader
and doubled here; `v2.x` edge fade, 1 except with `diffuse_edgefade_*`, multiplied by
`cb0[5].x`; `v3.xyz` view-space position (fog uses its length, depth uses `.z`); `v3.w` the
per-draw vertex constant `cb0[1].x`, blending scene ambient/fog with the draw's own;
`v4.xyz` view-space normal; `v5.xy` transformed UV0.

Stages, in order:

1. Alpha test at 0.502 (128/255) on `t0.a`. With an MSAA mode in `cb0[24].x`, it evaluates
   each sample and writes `oMask`. Optional sphere-map UVs from the reflected view vector
   (`cb0[5].y`).
2. Mask-and-palette recolour (`cb0[6].w > 0`). A 3-channel mask `t15`, an sRGB base `t17`,
   and a palette `t18` indexed by `cb0[6].xyz/255`.
3. Diffuse lighting. Scene light in `cb8`: sun direction `cb8[3]`, point light
   `cb8[4..5]`, ambient colours `cb8[0..2]` and `cb8[6]` lerped toward `cb0[18..21]` by
   `v3.w`. The light buffer `t11` is read at `v0*cb5[168]`.
4. The light flash from `cb7` (direction and sign in `cb7[0]`, colour in `cb7[1]`).
   A negative `cb7[0].w` takes a back-facing branch.
5. A spherical highlight around `cb0[15].xyz` (radius `cb0[15].w`, colour `cb0[16]`), then
   desaturation toward `cb0[17].xyz` by `cb0[17].w`.
6. Height fog. Up to four fog records from `cb5`, 14 `vec4`s each, with up vector
   `cb5[172]`. Two fog lists are lerped by `v3.w`.
7. Froxel fog volume `t16` (3D) at the screen position and `sqrt(v3.z / far)`, enabled by
   `cb0[1].w`.

## The `_depth` key

`combiners_mod_depth` and `combiners_mod_mod_depth` share a slot partition: 29 programs each.
`combiners_mod_mod_depth` adds a second texture `t1` on `v5.zw`. Their base program (slot 512,
blob 0, 252 lines) is a reduced version of the main family's. It has no MSAA coverage loop,
no sphere-map UVs, no recolour block, no `cb7` light, no highlight or desaturation, and a
single fog list. `v3` is `xyz` only, so no `v3.w` blend. The alpha factor `v2.x * cb0[5].x` is squared.

| Bit | Effect | Evidence |
|---|---|---|
| 0, 1, 6 | No effect | |
| 2 | Clustered forward lighting, as in the main key | blob 0 → 1 |
| 3 | Shadow receiving, as in the main key | blob 0 → 2 |
| 4 | Depth/normal prepass, as in the main key | blob 0 → 4 |
| 5 | Soft depth fade, as in the main key | blob 0 → 5 |
| 7 | Unlit: removes `cb8`, the normal `v4`, the light buffer and the clustered lights | blob 0 → 9 |
| 8 | Only with bit 2: discard and depth to `o1.x`, as in the main key | |
| 9 | Always set | |
| 10 | Hue-preserving clamp: when the largest colour channel exceeds 1, the colour is divided by it | blob 0 → 17 |

## Open

- Bit 9 (evidence above). The client's slot-selection code or a frame capture would settle it.
- What the client sets `cb0[1].x` (vertex), and therefore `v3.w`, to. Likely the doodad's
  interior/exterior lighting blend from WMO `QueryLighting` (`reconciliation.md` §3, inferred).
