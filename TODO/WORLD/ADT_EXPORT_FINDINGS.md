# ADT terrain → canonical bundle: WIP export, findings

**Status: WIP testing ground, not the real implementation.** `husk
export-terrain` exists to get ADT tiles into canonical form (`canon::Terrain`
→ a terrain bundle) so a consumer can be built against real data now. It
will be redone properly later; this file records what was learned so that
redo starts from verified facts. Scratch-doc lifecycle: fold the findings
into `ADT_TERRAIN_TODO.md`/`LIQUID_TODO.md`/`WORLD_PLACEMENT_TODO.md`/
`../../WIKI_FINDINGS/WORLD.md` when the real implementation lands, then
delete this file. Structures (WMO geometry) are out of scope by decision;
WMO placements are carried as identity-only references.

## What exists

| Piece | File | Scope |
|---|---|---|
| Parser | `src/adt.hpp`/`.cpp` (`husk::adt`) | root: `MCNK` header + `MCVT`/`MCNR`, `MH2O`; `_obj0`: `MDDF`/`MODF`; `_tex0`: `MDID`/`MHID`/`MTXP`/`MCLY`/`MCAL`; WDT `MPHD` flags |
| Canon types | `src/canon_terrain.hpp` | `Terrain`, `TerrainChunk`, `TerrainTexture`, `TerrainLayer`, `GroundEffect`, `LiquidSurface`, `Placement` |
| Input module | `src/adt_canon_input.hpp`/`.cpp` (`husk::adtinput`) | every on-disk convention resolved here; pure (textures/DB2 rows arrive pre-resolved) |
| Writer | `src/writers/terrain_bundle_writer.hpp`/`.cpp` | schema in the header's doc comment |
| Shared writer bits | `src/writers/bundle_common.hpp`/`.cpp` | `BufferSlice`/`writeRef`/`writeTextureRef`/timestamp, moved verbatim out of `bundle_writer.cpp` |
| CLI | `src/cmd_export_terrain.cpp` | `husk export-terrain <tile.adt> <out> [--obj/--tex/--wdt] [--listfile/--listfile-root] [--db2-dir/--dbd-dir] [--models-dir DIR] [--list-models]` |
| World batch | `husk export-world <maps-root> <out> --listfile F --listfile-root D [--map M]... [--jobs N] [--skip-models]` (`cmd_export_terrain.cpp`) | in-process, thread pool sized to the hardware. Three passes: collect model IDs (`_obj0` + every `GroundEffectDoodad`), export each model once to `models/`, export tiles to `maps/<map>/` with terrain textures written once to shared `textures/`. Resumable (an existing `manifest.json` means done; every writer writes it last). Failures go to `errors.log` (`phase<TAB>item<TAB>message`; retried and re-logged on rerun). Model and texture refs are named from the listfile. Measured: near-linear scaling warm (4 → 16 → 32 threads: 21 s → 6.0 s → 4.4 s on a 4-tile map with 2,560 models); 16- and 32-thread outputs identical. A cold first pass is I/O-bound |
| Scene orchestrator | `tools/export_terrain_scene.nu` (hand-picked tiles; `export-world --map` covers whole maps) | `<out> ...tiles`: tile bundles + one shared `models/` of `husk export --bundle-only` bundles, linked by relative `uri` |
| Bundle-only model export | `husk export --bundle-only` (`cmd_export.cpp`, `writeCanonBundleOnly` in `cmd_export_canon.cpp`) | canon bundle as the only output: no legacy/lean `.glb`, no deviation report, no legacy animation build. Byte-identical bundles to `--export-canon` (4 models checked). Still runs the legacy in-memory mesh/material pass, because canon's texture resolution and the has-geometry gate share it; untangling that is Stage 3 work. Per-model time is dominated by process startup and texture resolution (0.06–0.25 s either way), so an in-process batch mode is the real speed lever |
| Consumer probe | `tests/terrain_bundle_import_check.py` | Blender: splatted terrain, water, instanced models, ground-cover scatter, `HUSK_PROBE` checks, EEVEE `--render` |

Example output + a consumer-facing schema guide (one-off, gitignored):
`example_exports/elwynn_terrain/` (Elwynn 3×3, tiles 31–33 × 48–50).

## Canonical shape decisions

- **Frame**: WoW world space, yards, +X north, +Y west, +Z up — the frame
  `MCNK.position` is already in. Placements (position *and* rotation) are
  converted into it; nothing downstream sees a per-chunk-type frame.
- **Terrain is a heightfield, not a mesh**: absolute heights, per-chunk
  `(X, Y)` origin, constant quad size, the interleaved 9×9+8×8 layout;
  triangulation named in the manifest (`chunk_topology`), not stored.
- **One hole mask** shape; the low-res 16-bit map is expanded into it.
- **One alpha-map shape**: 64×64 8-bit. RLE-compressed, 4096 and 2048
  (4-bit, "fixed" 63×63) on-disk forms collapse into it in the input module.
- **Texture repeat is data** (`repeats_per_chunk`, 8 / (1 << MTXP
  texture_scale)) rather than a renderer convention a consumer must know.
- **Ground cover is a rule, not instances** (density + weighted doodad set
  per effect, per-quad dominant layer + suppression mask) — the client
  generates instances at runtime.
- **Liquid heights always materialized**, `height_source` records provenance.
- **Placements and ground-effect doodads are references**; model payloads
  are separate canon model bundles (shared across tiles), linked by `uri`
  when produced, identity-only otherwise.
- **Texture payloads** follow the settled bundle rule: DDS-housed source
  blocks, PNG only when the BLP can't be rehoused.

## Verified against real data

Fixtures: `azeroth_32_48` (Northshire), `azeroth_32_49` (Crystal Lake),
`ruinsoftheramore_40_38` (open ocean), plus the seeded samples below.

| Fact | Evidence |
|---|---|
| `MCNK.position` = (X north, Y west, Z base); `IndexX` steps −Y, `IndexY` −X; MCNKs stored `IndexY*16+IndexX` | neighbouring chunk edges agree to 0.0 (≤2e-6 Theramore); order checked on every export |
| Tile `(x, y)` in the file name ↔ world `Y = 17066.67 − x·533.33`, `X = 17066.67 − y·533.33` | checked against MCNK #0 on every export; 396/396 sampled agree |
| **`MCNR` on disk is X, Y, Z** — not the wiki's "X, Z, Y" | mean cos vs finite-difference normals 0.9995 (X,Y,Z) vs 0.21 (X,Z,Y); 0.993–0.995 vs triangle winding on three tiles |
| Quad-centre fan, counter-clockwise from +Z | face vs stored normals mean cos ≥ 0.993 |
| `MDDF` position: `X = 17066.67 − p[2]`, `Y = 17066.67 − p[0]`, `Z = p[1]` | median |dz| to terrain 0.14–0.31 yd; 87% within 1 yd of the interpolated surface (1177/1359, Crystal Lake); swapped axes land nothing on the tile; every doodad on a liquid quad is water-appropriate per listfile (lily pads, swamp plants, murloc huts, dock) |
| **`MDDF` rotation = the wiki `createPlacementMatrix` chain**, whose `Rx(90)·Ry(90)` prefix maps exactly onto the world frame | on 752 tilted placements, rotated model-up vs terrain normal: median cos 0.952 for the wiki chain vs 0.886–0.908 for sign/axis-flipped variants; upright trees come out as pure-Z quaternions; trees render upright in Blender. **Yaw sign is wiki-only** (no landmark check) |
| `MH2O` vertex format inferable without DB2s from the gap to the next data offset | 5 B/vertex (Elwynn), 1 B (ocean); all instances in 396 sampled tiles resolve to one of the 4 formats |
| `MH2O` exists bitmap LSB-first, row-major | clean river/lake edges; 74–98% of liquid quads over lower terrain |
| **No-vertex-data liquid** is always `LiquidType` 2 (ocean) at `min_height_level == 0` | 12,909/12,909 instances in a 200-tile sample — the wiki's two conflicting rules agree on all real data |
| Alpha maps: row-major, rows step −X like vertices | mean |Δalpha| across chunk borders 0.068 as decoded vs 0.265 transposed (Crystal Lake) |
| Layer blend weights (`base = 1 − Σ alpha`) partition exactly | splat weight sum min/mean/max 1.000/1.000/1.000 over the whole tile |
| Elwynn: `MPHD` 0x3ca (8-bit alpha), layers flagged `0x300` (RLE), `MCNK` 0x8000 everywhere; no `MTXP`, so `_h` height blending is off | byte dump; renders match expectation (grass, dirt shores, rock outcrops) |
| `MDID`/`MHID` present, no `MTEX`, on textured modern tiles; `MHID` ids are 0 on Elwynn | byte dump |

## Seeded corpus samples (root tiles, seed 1234, of 55,496)

- **Geometry/liquid/placements (400 tiles)**: 396 clean; 4 hit the
  not-implemented stub for name-table `MDDF` placements (e.g.
  `lordaeronblight_34_28`). 126 tiles have holes, all via the 64-bit map.
  38,236 liquid instances, **every one a full 8×8 rect**; height source:
  25,860 none/flat, 10,042 depth-only, 2,334 heightmap; all four vertex
  layouts occur.
- **Texture layers + ground effects (400 tiles, `--obj none`)**: 377 clean;
  23 failed on `_tex0` files with **no `MDID`, only an empty `MTEX`**
  (e.g. `kultiras_33_45`, 4 KB, untextured/ocean tiles) — now handled as
  "no textures". `ADT_TERRAIN_TODO.md`'s "MDID in 1,500/1,500" does not hold
  for the current corpus. Non-empty `MTEX` still throws (not seen).

## Wiki / prior-doc discrepancies

- `MCNR` "X, Z, Y" comment — real order X, Y, Z.
- `MCNK.flags.high_res_holes` (bit 16, `0x10000`) is set on every sampled
  MCNK (30,720/30,720 across 120 Azeroth/Kalimdor tiles), `0x200` never,
  `holes_low_res` always 0. `ADT_TERRAIN_TODO.md` §4 (2026-08-01) reported
  "bit 9 … 0 on all 40,000" and concluded the 64-bit path was unused.
  Unresolved whether that was a wrong-bit read or the data changed (corpus
  re-extracted 2026-08-22; terrain has been patched since). The low-res
  expansion path is wiki-only and unexercised.
- `MDDF.flags` `0x200` (~18% of Elwynn placements) is not in the wiki enum.
- `GroundEffectTexture.DoodadWeight<8>[4]` / `SplatDensity<8>[4]` decode to
  values like 1286/1362 and 1280 (`0x500`) through **both** husk DB2 paths
  (`db2table` and `db2-export`), while the wiki says weights total ~16.
  Either the WoWDBDefs layout for this build or husk's WDC5 array-field
  decode is off. Weights are used relatively only; not chased further.

## Open questions, roughly in blocking order

1. **Ground-cover scatter places nothing in visibly grassy areas.** The
   Blender probe scatters ~25k points on Crystal Lake, but zero near the
   close-up camera on plain grass base layer (models themselves are fine:
   8-vertex crossed cards, 0.4–1.6 yd). Suspects, all unverified:
   `dominant_layer` unpacking (2-bit LSB-first row-major assumed),
   `noEffectDoodad` orientation, and the density unit (treated as per-quad).
   Abandoned after three attempts per Luna's time box.
2. **Model bundles carry no M2 render blend mode** (opaque / alpha-key /
   blend) — only the texture-combiner op. Foliage needs alpha-key; the probe
   alpha-tests everything as a stopgap. Model-side canon gap, not terrain.
3. **Placement yaw sign** — needs a landmark (an asymmetric doodad with a
   known in-game facing).
4. **`MODF` transform** shares the `MDDF` math per the wiki; WMOs are out
   of scope, so it is unverified and unrendered.
5. **Name-table placements / non-empty `MTEX`** (~1% of tiles): blocked on
   `canon::NameSource` being M2-named (`m2_embedded`) while canon is meant to
   be format-agnostic — no honest tag for "a path embedded in an ADT".
6. **Hole row/column orientation** — visually plausible (Echo Ridge Mine),
   not proven.
7. Not attempted: `MCCV` vertex colours (44/256 Crystal Lake chunks),
   `MCSH` shadows + the alpha×0.7 shadow rule, `MTXP` height blending (no
   Elwynn data exercises it), layer UV animation playback, `_lod`, `.wdt`
   tile discovery, DB2 names for area/liquid/ground-effect rows, a map-level
   bundle that references tile bundles.
