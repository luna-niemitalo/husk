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
| `decal` | 32 / 32 | `t0`–`t2`, light buffer `t11`, view normals `t13`, `t10`, `t16`; `cb1[39]`; full lighting cbuffers; 1087 instructions in program 0 | 0: −48 instructions. 1: adds `t3`–`t5` (+235 to +719). 2: adds the shadow mask `t12`. 3: −91. 4: +506 to +990 with no new declarations |
| `edgedecal` | 48 / 32 | `t0`–`t2`, `t11`, `t16`; `cb1[40]` | 0: adds `cb7`, `t10`, `t13`, `v4.xyz` (+163). 1: adds `t8`. 2: adds `t3`, `t4`, `t7`. 3: adds the shadow mask `t12`. 4, 5: about −90 each |
| `flipbookimpostor` | 32 / 10 | `t0`, `t1`, `t11`, `t16`; UVs `v5`, `v6`; `v7.x` | Combiner layout: 0 clustered lights, 1 shadows, 2 prepass, 3 dither (`cb0`), 4 always set |
| `proj_single/two/three_texture`, `proj_add_single_texture`, `proj_opaque_single_texture` | 8 / 4 each | 1–3 textures, clustered lights always, `t16` | `proj_two_texture`: bit 0 +3, bit 2 −51 instructions; bit 1 never toggles |
| `projtex2d` | 8 / 7 | `t0`, `t4`; UVs `v3.xy`, `v3.zw` | Not analysed |

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
