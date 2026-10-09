# TODO: ADT terrain — open items after `husk export-terrain`

**Status: an open punch list, not a historical record.** Fixed items get
removed outright; git history is the record (same convention as
`../INVESTIGATIONS_TODO.md`).

The terrain itself is implemented: root/`_obj0`/`_tex0` parsing
(`src/adt.cpp`), the canonical tile (`src/canon_terrain.hpp`,
`src/adt_canon_input.cpp`), the terrain bundle
(`src/writers/terrain_bundle_writer.cpp`), and `husk export-terrain`/
`export-world` (`src/cmd_export_terrain.cpp`). Every layout fact and
convention it relies on is in `../../WIKI_FINDINGS/ADT.md`, and the bundle's
consumer-facing description is `../../REFACTOR/BUNDLE_FORMAT.md`'s "Terrain
tile bundles". Placement (`MDDF`/`MODF`) open items are in
`WORLD_PLACEMENT_TODO.md`, liquid in `LIQUID_TODO.md`, `.wdt` in
`WDT_TODO.md`, terrain LOD in `ADT_LOD_TODO.md`.

**Working with real tiles**: reading every root `.adt` in full was killed
after 13+ minutes with no output on the corpus storage (disk-wait bound). Use
a seeded random sample for anything that reads whole tile bodies; keep
full-corpus passes to existence checks.

## 1. Ground-cover scatter places nothing on plainly grassy quads

A Blender probe (`tests/terrain_bundle_import_check.py`) scattering
`ground_effects` over Crystal Lake (`azeroth_32_49`) placed ~25k points, but
none near the close-up camera on plain grass. The doodad models themselves
are fine (8-vertex crossed cards, 0.4–1.6 yd). Suspects, all unverified:

- `dominant_layer` unpacking: 2 bits per quad, LSB-first, row-major is
  assumed (`adtinput::unpackDominantLayer`);
- `ground_effect_suppressed_rows` (`MCNK.noEffectDoodad`) orientation;
- the density unit (treated as per quad);
- the `DoodadWeight` scale (`../../WIKI_FINDINGS/ADT.md`).

Abandoned after three attempts on the time box. The next step is a byte
dump of one grassy chunk's `predominantTexture`/`noEffectDoodad` next to its
alpha maps, to see which unpacking agrees with where the grass layer is.

## 2. Tile-edge seams: source data or husk?

MantleCore's `world_loading_check` over all of `azeroth` (2026-10-09) found
six cracks of 0.03–0.70 yd on south tile edges (40_53, 41_52, 42_45, 43_34,
44_34, 45_34) and a 13.68 yd step between two flat ocean-floor tiles
(23_36/24_36). husk's only transform on a height is `MCNK.position.z +
MCVT`, so these are expected to be in the source bytes. Confirm by reading
both tiles' shared edge row straight from the files (chunk row 15, vertex
row 8 of the northern tile vs chunk row 0, vertex row 0 of the southern one),
and record the result in `../../WIKI_FINDINGS/ADT.md`. If the source agrees
with itself, the fix is a consumer-side weld, and BUNDLE_FORMAT.md's seam
note stays as is.

## 3. Unproven orientations

- **Hole rows/columns**: visually plausible (Echo Ridge Mine), not proven.
  A hole quad should sit over a cave mouth or WMO entrance in the
  placements.
- **`high_res_holes`**: set on every sampled MCNK today, where the
  2026-08-01 survey reported the 64-bit path unused. The low-res 16-bit
  expansion (`adtinput::expandLowResHoles`) is wiki-only; find a real tile
  that uses it before trusting it.

## 4. Not read yet

- `MCCV` per-vertex colours (44/256 Crystal Lake chunks carry one).
- `MCSH` baked shadows, and the client's alpha × 0.7 shadow rule.
- `MTXP` height blending: parsed and carried, but no Elwynn tile has `MTXP`,
  so the blend itself is unexercised.
- Layer UV-animation playback (`animation.direction_degrees`/`speed` are
  carried; the speed unit is unknown).
- `MTXF` texture flags.
- DB2 names for `AreaTable`, `LiquidType`/`LiquidObject` and
  `GroundEffectTexture` rows: carried as bare `Db2Row` identities.
- `MHDR` itself (offsets only; the chunk walk doesn't need them).
