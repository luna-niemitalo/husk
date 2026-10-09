# `material3_*` shaders

Eight containers share the `material3` prefix. `material3_wmo_ps` and `material3_wmo_vs`
are covered in `wmo.md`. Next to the combiners and `uber`, this family is most likely the M3 material system: M3
models (`material3_mesh_*`) and MOM3 WMO materials (`material3_wmo_*`). Her notes found a
`.mtl3lib` that references `material3_mesh_vs.bls` (`reconciliation.md` §3; inferred).

## `material3_mesh_ps` (64 slots, 32 compiled, 20 programs)

A one-texture lit material on the combiner forward code: alpha test with the MSAA coverage
loop, sphere-map UV option, lighting, `cb7` light, fog, froxel volume. `material3_wmo_ps` is
the same material with WMO interpolators.

| Bit | Effect |
|---|---|
| 0 | Shadows: screen-space mask `t12` in pass 01, cascaded shadow map `t6`/`cb9` in pass 11 |
| 1, 2 | Pass (below) |
| 3 | Simple clamped lighting |
| 4 | UV comes from `v6.xy` instead of `v5.xy` (interface variant) |
| 5 | Set in every compiled slot |

| Bits 2,1 | Pass |
|---|---|
| 00 | Depth/normal prepass (9 instructions) |
| 01 | Forward with the light buffer `t11` |
| 10 | Forward without local lights |
| 11 | Forward with clustered lights (`cb4`, `t21`, `t22`) |

`material3_mesh_vs` (64 slots, 32 programs) reads position, normal and colour (`v0`–`v2`)
and uses `cb0[4]` and `cb4[4]`. Its key is not decoded.

## `material3_ffx_ps` (1 program)

Unlit and opaque:
`o0 = (t0(sum_i(v4[i] * cb1[i].xy) - 0.5).rgb, 1)`, with `i` running 0–3. The UV is a
weighted sum of four 2D basis vectors. `material3_ffx_vs` (9 programs) writes `o1`–`o3`
from `cb1[20]` and `cb2[7]`; not decoded.

## `material3_particle_ps` / `_vs` (1 program each)

The vertex shader is the GPU-particle draw shader (`particles.md`): it reads the 48-byte
particle buffer `t0` by `4 * SV_InstanceID + SV_VertexID`.

The pixel shader declares no colour texture. It computes:
- a lighting value from the green channels of the scene ambient `cb8[0..2]` and the sun
  `cb8[6]`, with the wrap term, enabled by `cb0[12].z`;
- the `cb7` light;
- a soft depth fade (scene depth `t10` minus `v2.z`, shaped by exponents 1.2 and 0.7,
  enabled by `cb0[12].y`);
- alpha from `v1.w`, scaled by `cb0[2].x` and by 0.8 unless `cb0[12].w` is set.

The lighting ends up in the output's green channel, with red and blue 0. That pattern
suggests an intermediate lighting target rather than visible colour, but the shader
doesn't show where the output goes.
