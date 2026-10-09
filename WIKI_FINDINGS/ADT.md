# wowdev.wiki findings — ADT (terrain tiles)

Current, correct facts only, split out of `WORLD.md` once `src/adt.cpp`
landed (that file's own split rule). Every fact here is checked against
real tiles from the local corpus. The code that relies on each one is
`src/adt.cpp` (raw layout) or `src/adt_canon_input.cpp` (meaning), and
`tests/test_adt*.cpp` pins it with synthetic fixtures.

## Corrections to the wiki

- `MCNR` entries are **X, Y, Z** on disk, not the wiki's "X, Z, Y". Mean
  cosine 0.9995 (X, Y, Z) vs 0.21 (X, Z, Y) against finite-difference
  heightfield normals; 0.993–0.995 against triangle winding on three tiles.
- `MCNK.flags` `0x10000` (`high_res_holes`) is set on every sampled MCNK
  (30,720/30,720 across 120 Azeroth/Kalimdor tiles), `holes_low_res` is
  always 0, and `0x200` is never set. This contradicts
  `TODO/WORLD/ADT_TERRAIN_TODO.md` §4's "never set" (which checked bit 9).
  Unresolved whether that was a wrong-bit read or the data changed (the
  corpus was re-extracted 2026-08-22). The low-res expansion path is
  wiki-only and unexercised by real data.
- `MDDF.flags` `0x200` occurs (~18% of Elwynn placements) and is not in the
  wiki enum.
- A `_tex0` without `MDID` exists: untextured (ocean) tiles carry an `MTEX`
  holding only an empty string (23/400 sampled, e.g. `kultiras_33_45`). So
  `ADT_TERRAIN_TODO.md`'s "MDID in 1,500/1,500" does not hold for the
  current corpus.
- `MH2O` instances with no vertex data are always `LiquidType` 2 at
  `min_height_level` 0 (12,909/12,909 in a 200-tile sample), so the wiki's
  two conflicting rules ("flat at min height" vs "LVF 2, height 0") agree on
  all real data.
- `MH2O` instance rects are always the full 8×8 (38,236/38,236), including
  instances with `liquid_object_or_lvf` < 42.
- `GroundEffectTexture.DoodadWeight<8>[4]` and `SplatDensity<8>[4]` decode
  to values like 1286/1362 and 1280 (`0x500`) through both husk DB2 paths
  (`db2table` and `db2-export`), where the wiki says a row's weights total
  ~16. Either the WoWDBDefs layout for this build or husk's WDC5 array
  decode is off. Weights are only used as ratios; not chased further.

## Terrain shader (client `terrain` pixel shader, 12.1.0)

Read from the client's compiled shaders, not from tiles
(`../WIKI_FINDINGS_HISTORY.md` §19). husk exports the inputs, not this
blend.

- The wiki's height-blend pseudocode (`ADT/v18.md`, "MTXP") is exactly the
  client's height mode: layer 0 weight `1 − Σ`,
  `pct = w·(h·heightScale + heightOffset)`, `pct·(1 − clamp(max − pct))`,
  normalised. `heightScale`/`heightOffset` arrive as `cb1[5]`/`cb1[9]`.
- The same excerpt's comment "pt_layerX: MCAL data" is wrong: the shader
  samples `pt_layerX` as the diffuse layer textures.
- Its `metalBlend = w0.a + w1.a; specBlend = w2.a + w3.a` is one of two
  client modes: with both reflection and specular options on, the cubemap
  reflection is masked by `w0·a0 + w1·a1` and specular by `w2·a2 + w3·a3`.
  With reflection only, layer 0 alone masks the reflection and layers 1–3
  feed specular.
- `MCLY` `0x400` cube-map reflection is masked by layer 0's weight and
  alpha, as the wiki says ("always affect the ground layer").
- `MCCV` is applied as `rgb × 2`, as the wiki says.
- The shader is compiled for 1, 2 or 4 groups of 4 layers, so up to 16
  layers per chunk. The wiki's 8-layer cap may still hold client-side;
  the shader doesn't impose it.

## Verified conventions

| Fact | Evidence |
|---|---|
| `MCVT`/`MCNR` stay in the root file across the Cata+ split, never in a sidecar | 2,500 sampled files |
| The corpus is entirely post-Cata split: `_obj0`/`_tex0`/`_obj1` beside 55,277/55,279 root tiles, `_lod` beside 46,340 (83.8%), `_tex1` beside none (the client stopped loading it once WDTs gained `MAID`) | full-corpus file-existence scan |
| `MCIN` (pre-Cata chunk index) is absent (`MHDR.mcin == 0`) | 2,500 sampled root files |
| `MWDR`/`MWDS` (Shadowlands+ per-`MODF` doodad-set ranges) occur in 5.1% of `_obj0` files, every range inside `MWDS` | 77/1,500 sampled `_obj0` files |
| `MCNK.position` = (X north, Y west, Z base); `IndexX` steps −Y, `IndexY` −X; MCNKs stored `IndexY*16+IndexX` | neighbouring chunk edges agree to 0.0 (≤2e-6 on Theramore); order checked on every export |
| Tile `(x, y)` in the file name ↔ world `Y = 17066.67 − x·533.33`, `X = 17066.67 − y·533.33` | checked against MCNK #0 on every export; 396/396 sampled agree |
| Each quad is a centre fan, counter-clockwise from +Z | face vs stored normals: mean cosine ≥ 0.993 |
| `MDDF` position: `X = 17066.67 − p[2]`, `Y = 17066.67 − p[0]`, `Z = p[1]` | median \|dz\| to terrain 0.14–0.31 yd, 87% within 1 yd (1177/1359, Crystal Lake); swapped axes land nothing on the tile; every doodad on a liquid quad is water-appropriate by listfile name |
| `MDDF` rotation is the wiki's `createPlacementMatrix` chain; its `Rx(90)·Ry(90)` prefix maps exactly onto the world frame | on 752 tilted placements, model-up vs terrain normal: median cosine 0.952 for the wiki chain vs 0.886–0.908 for sign/axis-flipped variants; upright trees come out as pure-Z quaternions. **Yaw sign is wiki-only** (no landmark check yet) |
| `MH2O` vertex layout is inferable without DB2s from the gap to the next data block (5/8/1/9 bytes per vertex) | every instance in 396 sampled tiles resolves to one of the four layouts; all four occur |
| `MH2O` exists bitmap is LSB-first, row-major | clean river/lake edges; 74–98% of liquid quads over lower terrain |
| Alpha maps are row-major, rows stepping −X like vertices | mean \|Δalpha\| across chunk borders 0.068 as decoded vs 0.265 transposed (Crystal Lake) |
| Layer weights partition exactly (`base = 1 − Σ alpha`) | splat weight sum min/mean/max 1.000/1.000/1.000 over a whole tile |
| Elwynn: `MPHD` 0x3ca (8-bit alpha), layers flagged `0x300` (RLE), `MCNK` 0x8000 everywhere, no `MTXP` | byte dump; renders match expectation |
| Modern tiles carry `MDID`/`MHID`, no `MTEX`; `MHID` ids are 0 on Elwynn | byte dump |
| Splat layers per chunk range 0–8: on all 341,760 `azeroth` chunks, 9% none, 82% 1–4, 4% 5–8 | MantleCore's map index, 2026-10-09 |

## Seeded corpus samples (root tiles, seed 1234, of 55,496)

- Geometry, liquid and placements (400 tiles): 396 exported cleanly; the 4
  failures were name-table (`MMDX`) placements, which husk now reads
  (e.g. `lordaeronblight_34_28`). 126 tiles have holes, all via the 64-bit
  map. 38,236 liquid instances; height source: 25,860 none/flat, 10,042
  depth-only, 2,334 heightmap.
- Texture layers and ground effects (400 tiles, `--obj none`): 377 exported
  cleanly; the 23 failures were the empty-`MTEX` tiles above, which husk now
  reads as untextured.
