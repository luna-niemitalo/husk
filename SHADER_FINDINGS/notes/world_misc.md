# Other world-object shaders (partly decoded)

These containers draw world objects outside the M2, WMO, terrain and liquid families. Only
`detaildoodad` has a decoded key structure. For the rest, this file records what each
declares and what each slot bit changes in the declarations. Roles marked *name* come from
the container name.

## `detaildoodad` (96 slots, 80 programs; *name*: ground clutter)

The slots form two halves of 48 (slot 48 and above). The halves differ by 3–4
instructions in every program. Within a half:

| Slots (mod 48) | Programs | Lighting | Textures beyond `t0`, `t16` |
|---|---|---|---|
| 0–15 | 16 | Lit (`cb8`, `cb7`) | `t1` (sampled at the interpolated `v2.xy`), or the clustered buffers `t10`/`t11` |
| 16–23 | 4 | Unlit (no `cb8`) | — |
| 24–39 | 16 | Lit | as 0–15, plus `t2`, `t3`, `t4` |
| 40–47 | 4 | Unlit | `t2`, `t3`, `t4` |

Within the lit blocks of 16:

| Bit | Effect |
|---|---|
| 0 | +1 instruction |
| 1 | Adds the clustered-light buffers (`cb4`, structured `t10` with 192-byte records, list `t11`) and drops `t1` and its UV `v2.xy`. What `t1` holds is not established |
| 2 | Cascaded shadows: shadow array `t5` (comparison sampler), `t6`, `t7`, `t8`, `cb9`, `cb0` |
| 3 | Adds `t2` and a second UV (`v5.zw` or `v4.zw`) |

In the unlit blocks of 8, slots `x` and `x + 4` share a program. Every program reads the
per-draw fog selection `cb3[3]` (`cbuffers.md`).

## Decals and impostors

| Container | Slots / programs | Declarations | Bits |
|---|---|---|---|
| `decal` | 32 / 32 | `t0`–`t2`, depth `t10`, light buffer `t11`, view normals `t13`, `t16`; `cb1[39]`; full lighting cbuffers | Decoded below |
| `edgedecal` | 48 / 32 | `t0`–`t2`, `t11`, `t16`; `cb1[40]` | Key decoded below |
| `flipbookimpostor` | 32 / 10 | `t0`, `t1`, `t11`, `t16`; UVs `v5`, `v6`; `v7.x` | Combiner layout: 0 clustered lights, 1 shadows, 2 prepass, 3 dither (`cb0`), 4 always set |
| `proj_single/two/three_texture`, `proj_add_single_texture`, `proj_opaque_single_texture` | 8 / 4 each | 1–3 textures, clustered lights always, `t16` | `proj_two_texture`: bit 0 +3, bit 2 −51 instructions; bit 1 never toggles |
| `projtex2d` | 8 / 7 | `t0`, `t4`; UVs `v3.xy`, `v3.zw` | Not analysed |

### `decal` (32 slots, 5 binary bits)

| Bit | Effect |
|---|---|
| 0 | Simple lighting: no hemisphere ambient and no `0.7 + 0.4·wrap` term (`cb8[10]` dropped; combiner bit 10 equivalent) |
| 1 | A second texture set `t3`–`t5` |
| 2 | Shadow mask `t12` (combiner bit 3 equivalent) |
| 3 | Reduced fog colour: the height-fog colour (`[6]`, `[13]`) and the sun-fog term (`[5]`, `[7]`) are not read; the fog colour is the cubic near/far gradient only |
| 4 | Each texture is sampled three times, with three UV and gradient sets (`sample_d` 3 → 9). Triplanar projection is the likely reading (**hypothesis**) |

Program 0 (1,086 instructions), `cb1[39]`:

1. **Position.** With `cb1[9].w > 0` the view position is rebuilt from depth (`t10` at
   the screen UV): `P = depth · (v3.xy / v3.z, 1)`. Otherwise `P = v3.xyz`, which makes it a
   mesh decal.
2. **Decal space.** `D = cb1[0..2] · P` and a world position `W = cb1[24..26] · P`.
   `cb1[7].yzw` scales `D`.
3. **Shape** (`cb1[28].y`), with clipping by `discard`:
   - 0, 2, 4: cylinder. Radius `|D.xy|` and height `|D.z|` against 0.5;
     `cb1[28].z == 1` switches the radial test from `D` to the world distance from
     `cb1[4]`.
   - 1: box, `|D| ≤ 0.5` on all three axes.
   - 3: up to four planes `cb1[29..32]` (signed distances, `|.w|` as offset). The nearest
     plane gives the edge distance.
4. **UVs.** `t0`, `t1`, `t2` use the 2×3 transforms `cb1[10..11]`, `[12..13]` and
   `[14..15]`. The source is decal-space `D.xy`, world `W.xy · 0.1` (`cb1[28].x`), or a
   polar mapping built from `atan2` and `acos` of the decal-space direction (`cb1[38].x`,
   `cb1[28].z`). Gradients come from the screen UV and `D`, which avoids seams at depth
   edges.
5. **Colour.** `t0 · t1 · t2 · cb1[8]`. The rgb channels are then re-selected through the
   identity `icb` rows indexed by `cb1[5].xyz` (channel swizzle).
6. **Fades.**
   - Edge fade: the distance to the shape edge, shaped by the cubic `cb1[36]`.
   - Height fade: shaped by the cubic `cb1[37]`, start `cb1[9].z`.
   - Angle fade: with view normals (`t13`, `cb1[35].w > 0`),
     `saturate((dot(cb1[35].xyz, N) − 0.7) · 3.33)`.
   - Opacity `cb1[6].w`.
   - Blend class from `cb0[24].x` as in the combiners (alpha test at 128/255).
7. **Lighting and fog.** The combiner forward path follows (`lighting_model.md`, `fog.md`):
   `cb0[18..24]`, the light buffer `t11`, `cb7`, the fog records and the froxel volume
   `t16`.

### `edgedecal` (48 slots)

`slot = lit + 2·e + 4·layer + 8·shadow + 16·fog`, with blob sharing:

| Digit | Values | Effect |
|---|---|---|
| `lit` | 2 | View normals `t13`, depth `t10`, `cb7` and `v4.xyz` |
| `layer`, `e` | 2 × 2 | `layer`: second texture set `t3`, `t4`, `t7`. `e`: adds `t8`, only with `layer` (without it, `e` changes nothing) |
| `shadow` | 2 | Shadow mask `t12` |
| `fog` | 3 | 0: full fog colour. 1, 2: the reduced fog colour of `decal` bit 3. 2 differs from 1 only with `layer` |

## Zone and spell surfaces

| Container | Slots / programs | Declarations |
|---|---|---|
| `fel`, `azerite` | 4 / 4, 2 / 2 | `t0`, `t1`, `t16`; `cb1[13]`; two UV pairs; centroid `v3.y` (the same layout as the liquid shaders) |
| `procfel` | 12 / 12 | `t0`–`t4`, `t16`; `cb1[16]` |
| `procazerite` | 4 / 4 | `t0`–`t2`, `t4`–`t9`, `t16`; `cb1[17]` |
| `anima` | 1 | `t0`–`t3`, `t16`; `cb8`, `cb1[8]`, fog selection `cb3[3]` |
| `barrier` | 1 | `t0`, `t1`; `cb1[1]` |
| `lightning`, `lightningstrike`, `lightningt1_t2_t3` | 1, 1, 2 | 4–61 instructions |
| `footprint` | 2 / 2 | `t0`, `t1`; `cb8`, `cb3[3]` |
| `snowpoint`, `sandpoint` | 2, 1 | 3–4 instructions |
| `perturbdisplmap`, `perturbdisplmapmesh`, `propagatedisplmap` | 1 each | 4–20 instructions; *name*: a displacement-map simulation |

## Fixed-function-style and tooling shaders

`default_p`, `_pc`, `_pct`, `_pn`, `_pnc`, `_pnc2t`, `_pnc2t2`, `_pnc2t3`, `_pnct`, `_pnt`,
`_pt` (2 programs each, 145 instructions, fog through `cb3`); `solidcolor`; `debugdraw`;
`grid`; `placementgrid` (373 instructions); `manipulator`; `objectfill`; `highlight_fill`;
`overdraw`; `trianglearea`; `model3skinshader_debug`; `genericmodel3` (3 programs of
2 instructions); `ui`, `ui_alphatest`, `imgui`, `font`, `texteffect`, `slug` (24 programs,
168 instructions; *name*: a GPU text renderer), `loadingscreen2`, `minimap`, `guildemblem`.
None were analysed.
