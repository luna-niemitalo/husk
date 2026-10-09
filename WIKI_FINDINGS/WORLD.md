# wowdev.wiki findings — WMO/WDT/WDL/PM4/PD4 (world-format expansion)

Current, correct facts only. Full evidence trail: `../WIKI_FINDINGS_HISTORY.md`
§15. The full struct listings, C++ data-model sketches, and test plans live in
the eleven companion `*_TODO.md` files under `TODO/WORLD/`. This page records
what would otherwise be lost once those are implemented and deleted. Formats
split out into their own file as their parsers land: ADT is in `ADT.md`
(`src/adt.cpp`); WMO, WDT, WDL, PM4 and PD4 stay here. WMO root/group parsing
exists (`src/wmo.cpp`, `tests/test_wmo.cpp`); nothing exports it yet.

---

## Chunk-tag byte reversal is universal — verified

WMO, ADT, WDT, and WDL all reverse chunk-tag bytes on disk — same
convention `.phys` uses (`PHYS.md`), opposite of M2's own inline chunks.
None of the relevant wiki pages state this explicitly; two independent
sibling investigations each caught a real scanner bug from forgetting it
before trusting their own results.

## WDT — verified

`_occ.wdt`'s occlusion heightmap is really `int16_t[17*17]` (578 bytes), not
the documented `(17*17+16*16)` (1,090 bytes) — confirmed across all 959
real `_occ.wdt` files. The root `.wdt`'s global single-WMO `MODF` record can
omit `MWMO` entirely when `flags & 0x8` is set (35/228 real global-WMO
files), with `nameId` then holding a real FileDataID directly. `MAI2`
(wiki: "unshipped") is real in 2 files.

## ADT — see `ADT.md`

Split out once `src/adt.cpp` landed: `ADT.md` holds every verified ADT fact,
including the planning-pass ones that used to live here.

## WMO — verified

- `GFID` (LOD-tier group-file resolution) is **row-major**:
  `index = lodTier * nGroups + groupIndex`, zero meaning "no file" — not
  stated as a formula on the wiki.
- `MOGX` is a fixed **256-byte** (64×`uint32_t`) chunk, not the wiki's
  stated 4-byte "one single value" — only the first slot is ever non-zero,
  the rest is padding. Both `MOGX` and `MOQG` are group-level, not
  root/group as an earlier internal note stated.
- `MOMX` (wiki: "just a guess," one named example) is present in **30.5%**
  of real root WMO files (3,931/12,869), not a rare one-off — its record
  count exactly equals `MOHD.nTextures`. Per-field semantics remain
  unresolved (not FileDataID references, despite the wiki's guess).
- `MODI`'s real entry count can **exceed** `MOHD.nDoodadNames` (14.5% of
  5,000 real roots) — always ≥, never less; size the array off the chunk's
  own byte length, never trust the header field alone.
- Shadowlands+ `MWDR`/`MWDS` doodad-set-activation indirection verified
  end-to-end against two real placements activating different multi-set
  combinations into the same `MODS` table.
- `MPBV`/`MPBP`/`MPBI`/`MPBG` confirmed genuinely absent corpus-wide (0 of
  71,929 real group files) — the wiki's rarity claim holds at full scale.
- `MOVT`/`MONR` are plain **(X, Y, Z), Z up**, the same frame as M2, despite
  the wiki's "(X,Z,-Y) order" note. `guardtower_001.wmo` spans ~28 × 28 on
  the first two components and 34 on the third; `MODD` rotations of the
  same building turn about the third axis. Group vertex ranges match the
  `MOHD`/`MOGP` bounding boxes component for component.
- `MOHD.nDoodadDefs` can disagree with `MODD`'s real length too
  (`guardtower.wmo`: header 173, `MODD` 170 records). Size every record array
  off its chunk.
- Multiple `MOTV` and `MOCV` chunks are common, not edge cases: 36% and 26%
  of 3,487 sampled group files (seeded 4,000-file sample of `world/wmo/`).
  All 4,000 parse cleanly with `src/wmo.cpp`, and per-triangle `MOPY`
  counts always equal `MOVI / 3`.
- `MOBA`'s `material_id_large` flag (0x02 at 0x16) must decide which field
  is the batch's material: flagged batches leave `material_id` at 0.

## WMO materials in the client shaders (12.1.0) — verified

Read from the client's compiled shaders (`../WIKI_FINDINGS_HISTORY.md`
§19), not from WMO files. This build has no `MapObj*` shaders: every
`MOMT` material is drawn by one container, `pixel/dx_5_0/uber`, which
switches on the material's shader ID (`cb2[4].x`, values 0–24; `cb2[4].y`
case 25 is an outline effect). Behaviour per ID, names from `WMO.md`'s
shader table (`uv0`/`uv1`/`uv2` are the vertex shader's generated UVs,
`v3.w` the second vertex-colour alpha, below):

| ID | Wiki name | Client behaviour |
|---|---|---|
| 0, 16 | Diffuse, DiffuseTerrain | `t0`, alpha `t0.a` |
| 1, 2, 4 | Specular, Metal, Opaque | `t0` rgb, alpha 1 |
| 3 | Env | `t0`; emissive `t0.a·t1(uv1)` |
| 5 | EnvMetal | `t0`; emissive `t0.rgb·t0.a·t1(uv1)` |
| 6 | TwoLayerDiffuse | `lerp(t0, t1, t1.a)`, then back toward `t0` by `v3.w` |
| 7 | TwoLayerEnvMetal | `lerp(t1, t0, v3.w)`; emissive `mix.rgb·mix.a·t2(uv2)` |
| 8 | TwoLayerTerrain | `lerp(t1, t0, v3.w)` |
| 9 | DiffuseEmissive | `t0`; emissive `t1.rgb·t1.a·v3.w` |
| 10, 14 | waterWindow, submarineWindow | no output (drawn by `FFXWaterWindow`/`FFXSubmarineWindow`, as the wiki says) |
| 11 | MaskedEnvMetal | `2·t0·t1` toward `t2` by `saturate(t2.a·v3.w)`, then by `t0.a` |
| 12 | EnvMetalEmissive | emissive `t0.rgb·t0.a·t1 + t2.rgb·t2.a·v3.w` |
| 13 | TwoLayerDiffuseOpaque | `lerp(t1, t0, v3.w)`, alpha 1 |
| 15 | TwoLayerDiffuseEmissive | two-layer blend; emissive `t1.rgb·t1.a·(1 − v3.w)` |
| 17 | AdditiveMaskedEnvMetal | `2·t0·t1 + t2·saturate(t2.a·v3.w)`, blended by `t0.a` |
| 18 | TwoLayerDiffuseMod2x | as 6, then `·t2(uv2)·2` |
| 19 | TwoLayerDiffuseMod2xNA | `lerp(t0, 2·t0·t1, v3.w)` |
| 20 | TwoLayerDiffuseAlpha | as 18, with `t2.a` in place of `v3.w` |
| 21 | Lod | `t0`; `2·t1` replaces the baked vertex light; `t2` emissive |
| 22 | Parallax | height `t17`, screen-derivative tangent frame, parallax-offset `t3`/`t4`, blended with `t2`, and with `t0` by `v3.w` |
| 23 | (DF+) | 4-layer height blend like terrain's: layers `t1`–`t4`, heights `t17`–`t20`; sphere-mapped `t0` emissive |
| 24 | **not on the wiki** | EnvMetal (5) with a separate opacity texture: alpha and alpha test from `t2.a` |

Against `WMO.md` / `WMO/Rendering.md`:

- `QueryLighting` doubles `MOCV` rgb ("not sure why?"): the vertex shader
  writes `2·MOCV` as the baked-light term.
- `FixColorVertexAlpha`'s 0/255 alpha is the interior/exterior light
  blend, as the wiki suspects: the shader lerps the scene light toward a
  per-draw light set by `1 − MOCV.a`.
- The two-layer materials blend by a second vertex alpha (`v3.w` above);
  that this attribute is `CVERTS2`/second `MOCV` is **inferred**.
- `MOC2` "used in math for PARALLAX and UNK_DF_SHADER_23": only 23 reads
  it (`xyz` layer weights, `w` tint toward a per-draw colour). Parallax
  (22) does not in this build.
- Material 23 takes four raw UV attributes (the DF+ fourth `MOTV`).
- `MOGP.fogIds[4]`: the fog code loops over at most 4 fog records per
  draw (same code for M2 and terrain).
- 12.1 WMOs run the full dynamic-lighting path (sun, hemispheric ambient,
  clustered lights, shadows), with `MOCV` as a diffuse multiplier and,
  in lighting mode 2, an added ambient term — not the classic "mostly
  baked vertex colour" path.
- The vertex shader generates each UV set by a per-draw mode (`cb2[1]`,
  an integer): 0 UV0 through the first texture transform, 1 UV1 through
  the second, 2 sphere map, 3 `reflect(V, N).xy`, 4 zero, 5 a planar
  projection of the position, 6 UV2 as-is, 7 UV0 as-is, 8 UV2 through the
  first transform. Material 23 skips generation and passes four raw UVs.
- The DX12 build's vertex struct `WMOVertex` is `{ float3 position,
  float3 normal, uint colours[2], float2 uv[4], uint }`: two vertex
  colours (`MOCV` and the second `MOCV` of `CVERTS2`), four UV sets
  where the wiki's client array holds three `MOTV`, and one more `uint`.
  The layout is verified; that the last `uint` is `MOC2` is **inferred**.

## WDT `_lgt.wdt` lights in the client shaders (12.1.0)

Clustered lighting reads a 192-byte `ShaderLight` record per light (the
DX12 type: `float4x4`, seven `float4`, four `int`). Offsets used by the
shaders: 64 position, 80 spot direction, 96/112 two colours with a
distance gradient between them, 128 attenuation start and culling range,
136 falloff scale and spot exponent, 160 inner/outer cone cosines and
scale, 176 type (0 point, 1 spot). Layout and offsets are verified.
Mapping to `MPL2`/`MPL3`/`MSLT` fields (`position`, `attenuationStart`,
`attenuationEnd`, `color`·`intensity`, spot `rotation`, `innerAngle`/
`outerAngle`) is **inferred**. The `float4x4` at offset 0 is read by no
shader in this build.

`MPL3`'s flag 1, "cast (raytraced?) shadows (on D3D12 + min shadowrt
level 2)", matches DX12-only shaders: light-buffer passes with a
per-light `RayQuery` shadow test (`lightbufferomnishadow`,
`lightbufferspotshadow`) and a ray-traced sun-shadow library `shadowrt`
(**inferred** link; the shaders are verified). `MPL3`'s cookie fields
match the light-buffer passes' cookie variants (cube cookie for point
lights, 2D for spots; **inferred**). `MLTA` animation is not in any
shader, so the client animates intensity.

## Liquid/lighting/fog — verified

`MH2O` covers 67.1% of real ADT tiles (vs. WMO's own `MLIQ` at 2.1%) — clear
implementation priority. Legacy `MCLQ` confirmed genuinely absent (0/4,000).
`MOLS`/`MOLP`/`MLSS`/`MLSP`/`MPVR`/`MAVR`/`MBVR`/`MFVR`/`MNLR` are all
**group**-file chunks (nested in `MOGP`), not root-level, despite reading as
root-level candidates from the wiki's own page layout. A wholly undocumented
chunk, **`VFE2`** (176 bytes), was found in real `_fogs.wdt` files by two
independent investigations — real, present, absent from `WDT.md` entirely,
not yet reverse-engineered. `MPVD`'s struct remains unresolved.

## Collision & culling — verified

WMO's `MOBN`/`MOBR` BSP collision (48% of real group files) maps almost
directly onto husk's existing M2 collision-mesh pipeline, needing only
`MOBR`'s one indirection (triangle index into `MOVI`, not raw vertex).
Portal culling (`MOPV`/`MOPT`/`MOPR`/`MOPE`): **checked directly, not
assumed** — Blender has no cell-and-portal visibility-culling mechanism at
all (confirmed against Cycles' Holdout shader and Ray Visibility toggle,
both ruled out for specific documented reasons). `wow.export` itself has
zero code path for `MOBN`/`MOBR` — husk implementing this would be new
ground, not catching up to existing tooling.

## Gameplay/misc metadata — verified

Four items promoted from a blanket "n/a" to real `extras` candidates after
being checked for real value rather than dismissed as invisible-therefore-
unimportant: `MDAL` (WMO ambient-color override — affects rendered
lighting), `MCSE` (ADT sound-emitter placement — same shape as M2's own
ribbon/particle anchor), `MCSH` (ADT baked shadow bitmap — real bimodal
shadow-density signal), `MCMT` (ADT per-layer material-ID override). `MOQG`
(WMO per-face ground type) was reconsidered under the same lens and
confirmed to genuinely belong at n/a (audio-only, no visual consequence).

## PM4/PD4 — verified, structural negative

Genuinely never shipped to the client — not an extraction gap like
`EXP2`/`PFDC`. Zero `.pm4`/`.pd4` files anywhere in a full 3,190,909-file
live CASC storage sweep. Both wiki pages' "not supposed to be shipped to
the client" text is literally true. `wow.export` has zero PM4 handling and
only a raw-byte PD4 pass-through — real support here would be genuinely
novel. The "hidden by default" design question (glTF's `KHR_node_visibility`
vs. Blender's lack of support for it) is left open for a human decision.
