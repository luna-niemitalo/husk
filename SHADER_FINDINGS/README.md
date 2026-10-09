# Shader findings

What the corpus asks the client to render, which client shaders that
needs, and how much of each formula we actually know. Scan date 2026-10-09.

- Corpus: `/media/luna/data/wow_export`, extracted 2026-08-22, **game
  version 12.1.0 (Midnight)**. That's the target version, so every count
  below is 12.1 data.
  - Evidence: 883 of 887 DB2 layout hashes match only 12.1.0 builds
    (68569 and later) in WoWDBDefs. No 11.x build matches more than 711.
  - **`buildinfo.json`'s `clientBuild: 61621` is stale.** That's an 11.x
    build number. Don't use it to date the corpus.
  - Installed client: 12.1.0.69933.
- Version numbers here are game versions. The wiki's `expansionlevel`
  template field follows the game's major version (`expansionlevel=10`
  is Dragonflight, 11 is TWW), not the zero-based expansion index (where
  11 is Midnight).
- Scan: `tools/corpus_scan_tasks/shader_inventory_task.py`. It reads
  496,027 files, of which 336,700 produce a row, with zero per-file
  errors. Regenerate with the command in the task's docstring (add
  `--resume`).
- Raw output goes to `scan/` (gitignored). `scan/shader_inventory_aggregate.json`
  holds every count below plus an example path for each.
- What the client shaders compute, read from their bytecode, is in
  `notes/` (index: `notes/README.md`; helper scripts in `scripts/`).
  Its verified findings are promoted into `WIKI_FINDINGS/`.

Evidence tiers used throughout:

| Tier | Meaning |
|---|---|
| **Validated** | Matched against real client shader bytecode, or confirmed structurally by this corpus scan |
| **Known, unvalidated** | A formula exists in wowdev.wiki or `reference/wow.export` (or WebWowViewerCpp via wow.export), but nobody has checked it against the client |
| **Unknown** | No formula in any source we have |

---

## 1. Which client shaders verification needs, and where they are

**They're already on disk.** `/media/luna/data/wow_export/shaders/` holds
877 `.bls` files: 420 DX50 + 442 DX60 GXSH v0x1000E, 10 GFAT wrappers
(`material3_*`, `model3skinshader_debug`) and 5 stale v0x1000C files.

All of them are already unpacked: `tools/export_shaders.py` writes every
distinct compiled program, with D3D, SPIR-V and GLSL listings and a
slot → program table, to `example_exports/shaders/`. Its `README.md`
covers the layout; `WIKI_FINDINGS/BLS.md` covers the container format.
`pixel/dx_5_0/illum.bls`, for example, has 2048 slots, 1024 compiled
permutations and 386 distinct programs. That makes it a full replacement for the session-scoped vkd3d capture in
`references/wow_shaders/`, which never caught `Illum`, the dual
crossfades or `Guild_NoBorder`.

**The modern build no longer ships one BLS per combiner.** Comparing the
listfile against the extracted dx_6_0 set:

| Kept as standalone BLS (12.1) | Dropped from the build (BfA-era FDIDs 2990730–2990761, not CASC-missing) |
|---|---|
| `combiners_opaque`, `combiners_mod`, `combiners_mod_depth`, `combiners_mod_mod_depth`, `combiners_mod_dual_crossfade`, `combiners_mod_masked_dual_crossfade`, `guild`, `guild_noborder`, `guild_opaque`, `illum`, `guildemblem` | every other `combiners_*` (27 names) and every `mapobj*` pixel/vertex shader |
| **New:** `combiners_uber_2_2`, `combiners_uber_2_2_no_mod_fog_alpha`, `combiners_uber_3_3` (FDIDs 5221415–7, a TWW-era range) | |

So the 27 two- and three-texture combiners that dropped out now live as
permutations of `combiners_uber_*`. WMO materials have lost their own
shader files too.

Verification therefore needs:

| Need | Source | Status |
|---|---|---|
| The 11 standalone combiner BLS | corpus `shaders/pixel/dx_5_0`, `dx_6_0` | **on disk**, extraction proven |
| `combiners_uber_2_2` / `_3_3` permutations | same | on disk; the combiner is not a permutation but a runtime `switch cb1[0].x` over the pixel-shader enum index (`WIKI_FINDINGS/M2/rendering.md`) |
| WMO pixel shaders | not a standalone BLS in 12.1 | `uber` draws every MOMT material, switching on the shader ID (`WIKI_FINDINGS/WORLD.md`); `material3_wmo_ps`'s role is open (likely the `MOM3` path, inferred) |
| Particle / ribbon | `particle_mod`, `particle_3colortex_3alphatex`, `ribbon`, `gpuribbon`, compute `particle*` | on disk |
| Terrain / liquid | `terrain*`, `prepassterrain*`, `water`, `procwater*`, `magma`, `procmagma`, `procswamp`, `procmercury`, `procfel`, `procleyline*`, `waterfall` | on disk |
| Vertex shaders | `vertex/dx_*/diffuse_*`, `diffuse_edgefade_*`, `color_t*`, `cdiffuse_*`, `particle_*` | on disk (`bw_diffuse_*` and `diffuse_t1_t1_t1` were dropped from the build) |
| Original wowdev.wiki | `documentation/wowdev-wiki/wikitext/` | local snapshot |

### Is 8.0.1 stale for 12.1?

**Yes, by 2 rows. wow.export's 36-row table is exactly sufficient for
12.1.0.**
- The highest `shader_id & 0x7FFF` seen across all 390,322 batches is
  **35**.
- Rows 33–35 (`Mod_Mod2x`/`EdgeFade_T1_T2`, `Mod`/`EdgeFade_T1`,
  `Mod_Mod_Depth`/`EdgeFade_T1_T2`) are past the end of the wiki's 8.0.1
  table (34 rows), and real content uses them: 7,169 batches.
- No index above 35 appears.
- The 12.1 `Wow.exe` no longer carries the `s_modelPixelShaders` name
  strings (only a `Diffuse_T1`/`Combiners_Opaque` fallback pair
  survives), so the table can't be read from the client directly.
- Rerun the scan after each patch: a max index above 35, or new
  `combiners_*` / `combiners_uber_*` BLS names, would mean it's stale
  again.

---

## 2. Bug this scan found in husk's own pipeline (fixed)

`src/m2_shader_names.cpp` used the wiki's **pre-8.0.1, 30-row** table. It
now uses the 36-row table: rows 0–33 from the wiki's 8.0.1 listing, rows
34–35 from wow.export. `shader_names_task.py` and
`shader_inventory_task.py` mirror it. What the old table did to real
corpus batches:

| Index | Old husk | Correct per the 8.0.1 table / wow.export | Batches | Files |
|---|---|---|---|---|
| 33 | unresolved | `Combiners_Mod_Mod2x` / `Diffuse_EdgeFade_T1_T2` | 5,584 | 2,217 |
| 34 | unresolved | `Combiners_Mod` / `Diffuse_EdgeFade_T1` | 1,018 | 746 |
| 35 | unresolved | `Combiners_Mod_Mod_Depth` / `Diffuse_EdgeFade_T1_T2` | 567 | 225 |
| 20 | `Combiners_Mod_AddAlpha_Alpha` | **`Combiners_Opaque_Mod2xNA_Alpha_Alpha`** | 365 | 214 |
| 18 | VS `Diffuse_T1_T1_T1` | VS `Diffuse_T1` | 478 | 85 |
| 22 | VS `Diffuse_T1_T1_T1_T2` | VS `Diffuse_T1_T2` | 278 | 54 |

That was 8,290 batches (2.1%) unresolved or misnamed. It's also why
`TODO/PIXEL_SHADER_FORMULAS_TODO.md` step 1 reported
`Combiners_Opaque_Mod2xNA_Alpha_Alpha` as "never resolved in this
corpus": it's real, in 365 batches, but sat under the wrong index in the
old table.

---

## 3. M2 model shaders

Usage below is batches from each M2's full-detail `00.skin` (390,322
batches in 128,640 models).

### 3.1 Pixel combiners

Every formula below is read from the client bytecode: IDs are the pixel-shader enum
indices that `combiners_uber_*` switch on, formulas in `WIKI_FINDINGS/M2/rendering.md`
(history §19, §20). The capture evidence is kept where it exists.

| Shader | Batches | Tier | Evidence / source |
|---|---|---|---|
| `Combiners_Opaque_Mod2xNA_Alpha` | 116,708 | **Validated** (bytecode) | ID 12; doubled, as wow.export; corrects `Pixel_shader_logic`. Also matches 42 captured shaders (`combiner_hunt/SUMMARY.md`). |
| `Combiners_Mod` | 84,449 | **Validated** (bytecode) | ID 1. Capture match `93dd1d60a5da7d7f`. |
| `Combiners_Opaque` | 66,162 | **Validated** (bytecode) | ID 0. |
| `Combiners_Mod_Mod` | 41,492 | **Validated** (bytecode) | ID 6. Capture match `dea0d96a0e1d054e`. |
| `Combiners_Mod_Mod2x` | 39,775 | **Validated** (bytecode) | ID 7. |
| `Combiners_Opaque_Mod2xNA_Alpha_Add` | 10,927 | **Validated** (bytecode) | ID 15. Emissive `t2·t2.a·cb0[6].z`, added after lighting. Real layer-2 textures are `*glow*`/`*emissive*`. |
| `Combiners_Opaque_AddAlpha_Alpha` | 6,508 | **Validated** (bytecode) | ID 14; corrects `M2/Rendering` (`1 − t0.a`). |
| `Combiners_Mod_Mod2xNA` | 4,938 | **Validated** (bytecode) | ID 9. |
| `Combiners_Opaque_Mod2xNA` | 4,844 | **Validated** (bytecode) | ID 4. |
| `Combiners_Opaque_AddAlpha` | 3,361 | **Validated** (bytecode) | ID 13. |
| `Combiners_Mod_Opaque` | 1,335 | **Validated** (bytecode) | ID 11; corrects `Pixel_shader_logic` (alpha × mesh). |
| `Combiners_Mod_Add` | 1,278 | **Validated** (bytecode) | ID 8; corrects both wiki pages (alpha `(t0.a + t1.a)·mesh.a`). |
| `Combiners_Mod_AddNA` | 977 | **Validated** (bytecode) | ID 10. |
| `Combiners_Opaque_AddAlpha_Wgt` | 944 | **Validated** (bytecode) | ID 20. Emissive scaled by per-draw `cb0[22].y` (texture weight, inferred). |
| `Combiners_Opaque_Alpha` | 882 | **Validated** (bytecode) | ID 29. |
| `Combiners_Mod_Depth` | 880 | **Validated** (bytecode) | ID 33. Standalone `combiners_mod_depth`; alpha factor squared, edge fade from the `diffuse_edgefade_*` vertex shaders. |
| `Combiners_Opaque_ModNA_Alpha` | 842 | **Validated** (bytecode) | ID 22. |
| `Combiners_Mod_AddAlpha` | 712 | **Validated** (bytecode) | ID 16. |
| `Combiners_Mod_Mod_Depth` | 567 | **Validated** (bytecode) | Standalone `combiners_mod_mod_depth`: `t0·t1`, edge-faded. **Missing from the wiki's pixel-shader enum.** |
| `Combiners_Opaque_Mod` | 504 | **Validated** (bytecode) | ID 2. |
| `Combiners_Mod_Dual_Crossfade` | 478 | **Validated** (bytecode) | ID 26. Standalone BLS. Real layers are sky `day`/`night`/`sunset`/`dusk`, i.e. a time-of-day crossfade (weights `cb0[6].y`/`.z`). |
| `Combiners_Opaque_Mod2xNA_Alpha_Alpha` | 365 | **Validated** (bytecode) | ID 27. Also 9 exact captured matches. |
| `Combiners_Mod_Add_Alpha` | 292 | **Validated** (bytecode) | ID 21. |
| `Combiners_Mod_Masked_Dual_Crossfade` | 278 | **Validated** (bytecode) | ID 28. Standalone BLS; as 26 with mask `t3`. Used on 4-texture skyboxes (`environments/stars`). |
| `Combiners_Mod_AddAlpha_Wgt` | 212 | **Validated** (bytecode) | ID 23. Emissive scaled by per-draw `cb0[22].y`. |
| `Combiners_Opaque_Opaque` | 205 | **Validated** (bytecode) | ID 5. |
| `Combiners_Mod_AddAlpha_Alpha` | 165 | **Validated** (bytecode) | ID 17; `Pixel_shader_logic`'s RGB line duplicates a term. |
| `Guild_Opaque` | 113 | **Validated** (bytecode) | ID 32. The tint is three per-draw colours `cb0[10..12]` (background, emblem, border); mapping to texture types 15–17 is inferred. 6-texture batches on character models. |
| `Combiners_Opaque_Alpha_Alpha` | 44 | **Validated** (bytecode) | ID 18. |
| `Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha` | 41 | **Validated** (bytecode) | ID 25. |
| `Combiners_Opaque_Mod2xNA_Alpha_3s` | 27 | **Validated** (bytecode) | ID 19. Also 9 exact captured matches. |
| `Guild` | 16 | **Validated** (bytecode) | ID 30. Tint as `Guild_Opaque`. |
| `Combiners_Opaque_Mod_Add_Wgt` | 1 | **Validated** (bytecode) | ID 24. Emissive scaled by per-draw `cb0[22].x`. |
| `Guild_NoBorder`, `Illum`, `Combiners_Mod_Mod_Mod_Const` | **0** | **Validated** (bytecode) | Never referenced by 12.1 content. Formulas: 31 as `Guild` without the border, 34 `Illum` with a gloss/F0 specular map, 35 `t0·t1·t2·cb0[6]`. Table rows 27, 29, 30, 31, 32 are unused; row 32 is `Combiners_Opaque`/`Diffuse_T1`, which is still reached through the runtime formula path. |

### 3.2 Vertex shaders and UV routing (Validated structurally)

- `textureCoordCombos` is **empty in 132,860 of 132,863 M2s**. The
  T1/T2/Env choice comes only from the vertex-shader name (`shader_id`).
- The global combiner-combo flag (`0x08`) is set in **0 files**, so the
  wiki's "shader_id indexes textureCombinerCombos" path never fires on
  12.1 content.
- The `_Env` layers carry `armorreflect*` / `envmap*` / `orbreflect*`
  textures, which is consistent with sphere-map reflections.
- Usage: `Diffuse_T1` 148k, `Diffuse_T1_Env` 132k, `Diffuse_T1_T2` 71k,
  `Diffuse_EdgeFade_T1_T2` 15k, `Diffuse_T1_Env_T1` 11k, `Diffuse_T1_T1`
  8k, `Diffuse_EdgeFade_T1` 1.9k, `Diffuse_Env` 1.7k, and a long tail.
- EdgeFade (validated, bytecode): the `diffuse_edgefade_*` vertex shaders
  compute `pow(saturate((|N·V| − lo) / (hi − lo)), power)` from per-draw
  constants. **Unknown:** how the `EDGF` chunk's fields fill them, and
  what the `color_t*`/`cdiffuse_*` vertex shaders are selected by.

### 3.3 Blend, opacity and other material state

| Item | Usage | Tier |
|---|---|---|
| `M2Material.blendMode` 0–7 | 0: 149k, 2: 136k, 4: 71k, 1: 33k, 7: 13k, 6: 2.6k, 5: 1k | Known, unvalidated (`Rendering#EGxBlend`). Shader side validated: an internal blend class (alpha → 1, alpha test, per-sample test, premultiplied additive with fog to zero); the mode → class mapping is client-side (`WIKI_FINDINGS/M2/rendering.md`) |
| Material flags 0x1/0x2/0x4/0x10 | common | Known (wiki) |
| Material flags 0x40, 0x80 | 20k / 486 | wiki: "shadow batch related ???" |
| Material flags **0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000** | 19k, 866, 57, 172, 17k, 178, 173, 155 | **Unknown** |
| Texture weight (transparency) | constant 374k, animated 16k, **constant zero 525** (the wiki's never-rendered case) | Known, unvalidated |
| UV transform anim | 36,586 batches | Known |
| Colour/alpha anim (`colorIndex`) | 70,531 batches | Known |
| Batch `flags2` 0x1, 0x2 (projected), 0x4, 0x8 (EDGF), 0x10–0x80 | 27k, 20k, 11k, 21k, small | 0x2 and 0x8 known; **the rest unknown** |
| Texture types 19, 20, 24, 25, 26 | 59, 22, 834, 8, 2 | **Unknown** (not in the wiki's type list) |

### 3.4 Particles and ribbons

- **Particle `blendingType` goes up to 7**, but the wiki documents only
  0–4, and its table contradicts itself (row 1 is named "AlphaBlend" with
  an additive blend function). Plausibly it now shares the
  `M2Material.blendMode` enum, but **that's unvalidated**. The particle
  shaders implement only the alpha → 1 and premultiplied-additive paths;
  a corpus data test that can decide 7 is in
  `notes/reconciliation.md` §5.
  - 4: 65,778 emitters
  - 2: 28,782
  - **7: 22,717**
  - 1: 275
  - **6: 3**
  - **5: 2**
- **Multi-texture particles:** 41,123 emitters set flag `0x10000000`. The
  wiki names the Modx4 (`0x20000000`) and 3-colour (`0x40000000`)
  variants, and they're common (66k each). The combine is validated from
  bytecode: `t0·t1 × 2` (or `t0·t1·t2` in three-colour mode), `× 4` when a
  `cb0[6].x` bit is set (`WIKI_FINDINGS/M2/rendering.md`). Real slot-0 textures are
  `*_blend`/`*_blendadd`; slots 1 and 2 are `*_mod4x`/`*_desat`/flat
  greys.
- **Ribbons:** blend comes from their materials (4: 3,344, 2: 2,554,
  7: 700, 0/1/6: small), with up to 3 textures each. The ribbon pixel
  shader is `t0 × colour`, plus a soft-depth variant (validated, bytecode).

---

## 4. World shaders

### 4.1 WMO (`MOMT.shader`, 12,926 root WMOs)

| Shader | Materials | Tier |
|---|---|---|
| 0 Diffuse | 46,735 | **Validated** (bytecode, `uber`) |
| **23 (wiki: "UnkDFShader")** | **36,824** | **Validated** (bytecode). 4-layer height blend like terrain's: layers `t1`–`t4`, heights `t17`–`t20`, weights from `MOC2`, sphere-mapped `t0` emissive (`WIKI_FINDINGS/WORLD.md`) |
| 13 TwoLayerDiffuseOpaque | 30,681 | **Validated** (bytecode, `uber`) |
| 4 Opaque | 30,392 | **Validated** (bytecode, `uber`) |
| 7 TwoLayerEnvMetal | 15,332 | **Validated** (bytecode, `uber`) |
| 21 Lod | 11,940 | **Validated** (bytecode, `uber`) |
| 5 EnvMetal | 10,401 | **Validated** (bytecode, `uber`) |
| 9, 6, 12, 15, 1, 16, 3, 18, 2, 22, 19, 11, 10, 8, 20, 17, 14 | 2,945 down to 9 | **Validated** (bytecode, `uber`); 10 and 14 draw nothing there (window effects) |

- Every root WMO uses FileDataID textures (no `MOTX`), and **none carries
  `MOM3`**, so the `material3` path is unused by 12.1 content.
- `MOUV` (per-material UV animation) appears in 142 WMOs.
- Group headers: two vertex-colour sets (`0x1000000`) in 21,639 groups,
  two UV sets (`0x2000000`) in 21,516, three UV sets (`0x40000000`) in 584.
- Group liquids are LiquidType IDs, e.g. 15 Green Lava 2,997, 5 Slow
  Water 566, plus about 40 expansion-specific types.
- `uber` renders every MOMT material (IDs 0–24, 24 not on the wiki),
  since `mapobj*` is gone from the build (`WIKI_FINDINGS/WORLD.md`).

### 4.2 Terrain (56,191 `_tex0` + 56,192 root ADT)

| Feature | Usage | Tier |
|---|---|---|
| Height-texture blending (`MTXP`, `MHID`) | 24,663 tiles with `MTXP`; 159,755 layers with non-zero height scale/offset | **Validated** (bytecode): the wiki's pseudocode exactly (`WIKI_FINDINGS/ADT.md`) |
| `MTXP` flags 0x10, 0x20, 0x40, 0x1 | 85k, 69k, 1.6k, 186 | **Unknown** meaning |
| `MCLY` 0x100 (uses alpha map), 0x200 (compressed alpha) | 10.7M, 9.7M | Known |
| `MCLY` 0x40 + rotation/speed (animated layers) | 781 layers | Known (wiki), unvalidated |
| `MCLY` 0x80 overbright | 2,697 | Known flag, **formula unknown** |
| `MCLY` 0x400 cube-map reflection | 4,375 layers, 182 tiles | **Validated** (bytecode): masked by layer 0's weight and alpha (`WIKI_FINDINGS/ADT.md`) |
| `MCMT` → TerrainMaterial (`Shader` u8 0–3, EnvMapDiffuse/Specular FDIDs) | 60+ distinct material IDs | **Unknown**: the wiki documents only the 6.0.1 record, with no shader semantics |
| `MCCV` vertex shading | 4.9M chunks, 28,304 tiles | Known |
| `MCLV` chunk lighting | 1,015 chunks, 26 tiles | Known, unvalidated |
| `MTCG` | 386 tiles | Partially documented (wiki ADT v18, Shadowlands+). The shader has a per-layer 32³ colour LUT blended in by a distance curve; that these are `MTCG`'s LUT and ramp is inferred (`notes/terrain.md`) |
| **`MPTX`** (MCNK subchunk) | 84,358 chunks, 1,505 tiles | **Unknown**: not in the wiki at all |
| WDT `MPHD` 0x80 height texturing, 0x2 MCCV, 0x4 big alpha | 591, 692, 151 maps | Known (wiki `WDT.wiki`) |
| WDT `MPHD` 0x40 | **969 maps** | **Unknown**: the wiki says "only found on Firelands2.wdt", which the corpus contradicts |

### 4.3 Liquids

- MH2O instance types: 4.66M Ocean (2), then 947 Kul Tiras Ocean, 5 Slow
  Water, 1145 N'Zoth Magma, 1100 Ocean (Scroll 0.8), 1 Water, 1142,
  1177, and a long tail of expansion-specific types.
- LiquidType's class column (0 water, 1 ocean, 2 magma, 3 slime) and
  LiquidMaterial (5 rows: Flags, LVF) are readable via `husk db2-export`.
- How a LiquidType picks its shader, partly answered from bytecode: the
  family follows the LiquidMaterial class by name (inferred), and every
  permutation digit inside a family is a render option (reflection
  source, height-map detail, depth texture, discard plus depth), never a
  liquid type. Per-type data arrive as a parameter block
  (`WIKI_FINDINGS/RENDERING.md`, `notes/liquids.md`). **Unknown:** which
  LiquidType/LiquidObject columns fill which register. Most instances
  reference a LiquidObject (≥ 42) rather than an inline LVF.

---

## 5. Non-world shaders in the client set (inventory only)

These are on disk; this pass had no asset-side references to cross-check.

- **UI and text:** `ui`, `ui_alphatest`, `font`, `slug`, `texteffect`,
  `imgui`, `minimap`.
- **Post / FFX:** `ffx*` (glow, DOF, colour grading/correction, death,
  nether, spectral sight, underwater, sketch), `fxaa`, `fsr`, CMAA, ASSAO,
  CACAO, `sunshafts*`, `volumefog*`.
- **Sky:** `dn*` (sky, clouds, planet, glare).
- **Outline / highlight:** `outline_*`, `xrayhighlight`, `highlight_fill`,
  `objectfill`.
- **Special materials:** `skin` (subsurface; `498e864b92fcf1b0` in the
  capture), `anima*`, `azerite`, `fel`, `leyline`, `mercury`, `magma`,
  `barrier`, `lightning*`, `edgeglow`, `litsphere_*`.
- **Decals:** `decal`, `edgedecal`, `proj_*`, `projtex2d`.

The asset → shader links for these go through DB2s (SpellVisualKit,
ScreenEffect, LightParams, and so on), which this scan doesn't follow.
Many are now read from bytecode in `notes/`: sky (`sky.md`), post and FFX
passes (`postprocess.md`), the model effects `skin`, `edgeglow`,
`litsphere_*`, `blueprint`, `procedural` (`model_effects.md`), decals
(`world_misc.md`), and the third-party passes FSR, CACAO, ASSAO, CMAA2,
FidelityFX Parallel Sort, VALAR and Slug (`dxil_names.md`). UI, `anima*`,
`lightning*`, `barrier` and the long post passes are still unread.

---

## 6. Next steps, by payoff

Done: the standalone combiners, the `combiners_uber_*` indexing (a
runtime switch, not permutations), the multi-texture particle combine and
the ribbon formula (`WIKI_FINDINGS_HISTORY.md` §19, §20). Open:

1. Particle `blendingType` 5–7: run the corpus data test in
   `notes/reconciliation.md` §5 (texture premultiplication, co-occurrence
   with material blend modes, the 3/4/7 distribution).
2. The LightData ↔ fog-record and LiquidType ↔ liquid-parameter mappings
   (`notes/fog.md`, `notes/liquids.md`): needs DB2 rows next to a captured
   frame's constants.
3. `TerrainMaterial.Shader`, the unknown material flags, batch `flags2`
   bits and texture types 19, 20, 24–26: not visible in shaders; client-side or DB2.
4. Combiner slot bit 9 (`notes/combiners.md`).
