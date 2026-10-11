# TODO: WMO/ADT liquid surfaces (`MLIQ`/`MH2O`/`MCLQ`)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed (see `../INVESTIGATIONS_TODO.md`'s own convention) —
git history is the record of what was fixed and when, not this file. This is
one of three sibling documents produced from the same investigation pass
(`LIGHTING_TODO.md`, `FOG_VOLUMES_TODO.md` cover the rest of
`../../WORLD_COMPLETENESS.md`'s WMO/ADT slice); each was written against real
corpus bytes this session, independently of the other two.

Scope: `../../WORLD_COMPLETENESS.md`'s **Liquid / water** section — WMO group-file
liquid surfaces (`MLIQ`), ADT's modern per-tile liquid (`MH2O`, WotLK+), and
ADT's legacy liquid (`MCLQ`, pre-Cata, kept only for reading old files). All
three currently read `none`/`none` in that table; this document is the
implementation-ready plan for turning them into real code, not another
survey — every claim below was checked against real files in this session's
own corpus scan (`/media/luna/data/wow_export`, read-only), not copied from
wowdev.wiki uncritically.

**Why liquid is the highest-priority item across all three sibling docs**:
unlike lighting (whose real client-visible effect is disputed even by the
wiki itself, see `LIGHTING_TODO.md`) or fog/particulate volumes (genuinely
inert metadata, no core-glTF shape at all), liquid is unambiguous, real,
load-bearing render geometry — water/lava/slime surfaces are visually
obvious in-game, structurally a plain textured mesh (no bones, no curves,
no DBC dependency to get the *shape* right), and husk already has the exact
precedent to reuse: the M2 collision-mesh translation (`src/cmd_export.cpp`,
`gltf::NamedMesh::isCollision`) is "parse positions/indices from a
non-render-but-still-geometric M2 array, emit a plain unskinned `NamedMesh`,
tag it in `extras` so a renderer can tell it apart" — liquid is the same
shape, tagged `"liquid": true` instead of `"collision": true`. This is the
one item in this whole 3-document set that can realistically reach
`native — 100%`, not just `extras`/`node-possible`.

**Non-goal, restated from `../../WORLD_COMPLETENESS.md` and `../../DESIGN.md`**:
`LiquidType.dbc`/`LiquidMaterial.dbc`-driven shader/material identity
(which specific water-shader variant, scrolling-normal-map behavior, etc.)
is external DB2 data husk has no access to and never will, per this
project's standing CASC/DBC non-goal. The target here is a real, simplified
**renderable water-plane mesh** — positions, a flat/interpolated height
surface, an "this is liquid, treat accordingly" tag, and (where cheaply
derivable, as `MH2O`'s depth-carrying layouts already are) a depth value — not shader-accurate
water. A future consumer wanting real WoW water shading needs its own
DBC-driven material layer on top; husk's job stops at correct geometry.

---

## 1. WMO group-file liquid (`MLIQ`) — implemented

Read by `src/wmo.cpp` (`parseLiquid`) and exported by `husk export-wmo` as
the WMO bundle's `resources.geometry.liquids` (`canon::LiquidGrid`, schema in
`schema/model.schema.json`); verified facts in
`../../WIKI_FINDINGS/WORLD.md`. 154 of 4,000 sampled group files carry it.
Open:

- **Legacy green lava** (`MOGP.groupLiquid` 15 in a root without MOHD flag
  0x4) has no `liquid_type`: the client picks per tile by a rule the wiki
  marks unverified. 34 of 602 liquids in a 120-root sample.
- Whether `vertex_data` is water flow or magma UV depends on the liquid
  type's material (`LiquidType` -> `LiquidMaterial`), not resolved.
- The implicit whole-group liquid (flag 0x1000 with no `MLIQ`) is not
  built; it never occurred in the sample (flag and chunk always together).
- No glTF projection.

---

## 2. ADT modern liquid (`MH2O`, WotLK+) — implemented

Read by `src/adt.cpp` (`parseMh2o`, all four vertex layouts, inferred from
block gaps without DB2s) and exported as `liquids` by `husk export-terrain`;
verified facts in `../../WIKI_FINDINGS/ADT.md`. 67.1% of root tiles carry
it (2,683/4,000 sampled). With `--db2-dir`/`--dbd-dir`, terrain and WMO
documents list each named liquid type (`resources.liquid_types`: name,
material, texture slots with resolved frames) and terrain its liquid
objects (flow, fishable). The layout inference was checked against the DB2s
(WIKI_FINDINGS/ADT.md). Open:

- `MH2O.offset_attributes` (per-chunk fishable/deep masks) is not read.
- Which texture slot a liquid shader samples for what (diffuse, normal,
  foam, ...) depends on the material and is not resolved; slots are
  carried in `LiquidType` order.
- The 6 `LiquidMaterial` rows the table lacks (3, 4, 5, 12, 14, 18).

## 3. ADT legacy liquid (`MCLQ`, pre-Cata)

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/ADT/v18.md`, `### MCLQ
sub-chunk` (lines 1276–1344) — an `MCNK` sub-chunk, not a top-level ADT
chunk. The wiki's own text is explicit that this is deprecated, "not
really used anymore," and its own struct documentation is hedged ("This
information is old and incomplete as well as maybe wrong").

### Real-data verification (this session)

This is the one case in this document where the real-data check produced a
clean **negative** result, not a struct confirmation. Scanned 4,000 real
ADT root tiles (a distinct, broader sample than the `MH2O` scan above,
built the same root-tile-only way) directly for `MCNK.sizeLiquid > 8`
(the wiki's own documented "only read if > 8" gate at the header's
`0x64` offset, which is the same location in both the pre-~5.3 and
`high_res_holes` header layouts since that variant only swaps the
*meaning* of an earlier 8-byte region, not the struct's total length):
**0/4,000 (0.0%) real files have a populated `MCLQ`.** This modern retail
extraction simply doesn't carry legacy liquid data any more — consistent
with `MH2O` being universal (WotLK+, 15+ years old at this point) and
Blizzard having long since re-baked or dropped pre-WotLK terrain from live
retail data.

**Recommendation: do not implement `MCLQ` unless/until a real file
surfaces.** This is the same disposition `RO_COMPLETENESS_TODO.md` item 1
(`blp/`'s DXT3/JPEG) and `../../WIKI_FINDINGS.md` §10 (`WFV1`/`WFV2`/`DPIV`/
`AFRA` in the *original*, buggy scan) already established for this
project: a real corpus-wide zero is worth recording and moving on from,
not guessing at from wiki prose alone, especially prose the wiki itself
flags as "old and incomplete as well as maybe wrong." If a genuinely older
ADT file ever needs reading (a very old private-server dataset, say), this
section has the wiki's struct transcribed above and ready — it's simply
not worth the implementation/test cost against zero real evidence today.

- **Parse**: `n/a` (no real data to implement against).
- **Consumption**: `n/a`.
- **glTF ceiling**: `n/a, superseded` — matches `../../WORLD_COMPLETENESS.md`'s
  existing note exactly; this session's corpus check makes it a confirmed
  fact rather than an inference.

---

## Priority order

1. **`MCLQ`** (item 3): do not implement; watch only, per the corpus's real
   0/4,000 finding above.

---

## Test plan

**Real candidate fixtures** (paths from this session's own corpus scan,
`/media/luna/data/wow_export`, read-only — copy into `test_data/` under
this project's existing "personal WoW extraction, gitignored" convention
before use, same as every other real fixture in this repo):

- `MLIQ`: `ruinedkeep_crypt2_015.wmo` is in use (`tests/test_wmo.cpp`).
- `MH2O`: covered by `tests/test_integration_terrain.cpp` (real-data,
  `test_data/`-gated). **Still wanted**: a real tile with
  `0 < liquid_object_or_lvf < 42` to pin the layout inference on a
  non-ocean case.
- `MCLQ`: none — real 0/4,000, don't build a synthetic fixture for a
  format with zero real corpus evidence in this project's own data.

**Synthetic-vs-real split**: struct-level unit tests (`MLIQ` header
field decode) should use small hand-built synthetic fixtures
first (matching `tests/test_phys.cpp`'s own precedent — deliberately
non-contiguous offsets, to prove the parser follows real offsets rather
than assuming sequential layout, the same lesson `.phys`'s `PLYT` chunk
already taught this codebase); real-data regression tests
(`tests/test_integration.cpp`-shaped, `doctest::skip()`-gated on the
fixtures above) should assert exact vertex/tile counts and spot-check a
few real decoded height values against this document's own worked
examples, not just "doesn't crash."

---

## References

- **wowdev.wiki**: `documentation/wowdev-wiki/md/WMO.md` (`## MLIQ chunk`,
  lines 1912–2079; `## MOGP chunk`, 1059–1130, for the header-size/nesting
  fact); `documentation/wowdev-wiki/md/ADT/v18.md` (`## MH2O chunk
  (WotLK+)`, 512–685; `### MCLQ sub-chunk`, 1276–1344; `### MCVT
  sub-chunk`, 773ff, for `MCNK`'s own header-offset conventions reused
  when locating `sizeLiquid`).
- **wow.export** (design ideas only, no code copied):
  `reference/wow.export/src/js/3D/loaders/WMOLoader.js` (its own `MLIQ`
  parser, confirms the same field layout, decodes the per-vertex union as
  an opaque `uint32+float` pair rather than resolving water-vs-magma —
  the same "expose raw, don't guess the union variant" choice this
  document's own `LiquidVertex::raw` field makes);
  `reference/wow.export/src/js/3D/exporters/WMOExporter.js`,
  `reference/wow.export/src/js/3D/exporters/ADTExporter.js` — **notable
  finding**: neither exporter builds real liquid *geometry* for its own
  glTF/OBJ output; both write liquid data as a separate JSON sidecar only
  (`WMOExporter.js` folds `group.liquid` straight into its scene-info
  JSON; `ADTExporter.js` writes a standalone `liquid_<tileID>.json` with
  world-space-remapped instance positions). This is real, citable
  precedent that even the most mature community WoW-export tool treats
  liquid as metadata, not mesh — but it's also a deliberate case *for*
  husk going further: `wow.export` targets DCC re-placement workflows
  (Maya/3ds Max artists who'd rather re-author water by hand), while husk's
  own established philosophy (`../../DESIGN.md`'s "closest possible 1:1
  representation," the collision-mesh precedent) is to translate real
  geometry directly wherever a translation exists — and one clearly does
  here.
- **husk `src/`**: `src/phys.hpp` (data-model idiom followed above —
  documented struct, verified-against-real-files framing, explicit
  unverified-floor pattern); `src/chunk.hpp` (`readChunks`/`findChunk`,
  reused unmodified — WMO/ADT's byte-reversed chunk tags need only
  reversed-literal tag constants, no core-chunk-reader change, same as
  `src/phys.cpp`'s own approach); `src/cmd_export.cpp` (collision-mesh
  `NamedMesh`/`isCollision` block — the direct precedent for both `MLIQ`'s
  and `MH2O`'s consumption plan above); `src/gltf.hpp`'s `NamedMesh`
  struct (`isCollision` bool → the same shape a new `isLiquid` bool, or a
  more general `extras`-only string-keyed tag, would take).
- **Real corpus counts, this session** (`/media/luna/data/wow_export`,
  read-only, scratch scripts in `/media/luna/work/cache/tmp/.../scratchpad/`,
  not committed): `MLIQ` 41/2,000 real WMO group files (2.1%); `MH2O`
  2,683/4,000 real ADT root tiles (67.1%); `MCLQ` (via `MCNK.sizeLiquid >
  8`) 0/4,000 real ADT root tiles (0.0%).
