# TODO: WMO/ADT liquid surfaces (`MLIQ`/`MH2O`/`MCLQ`)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed (see `TODO_correctness.md`'s own convention) —
git history is the record of what was fixed and when, not this file. This is
one of three sibling documents produced from the same investigation pass
(`LIGHTING_TODO.md`, `FOG_VOLUMES_TODO.md` cover the rest of
`WORLD_COMPLETENESS.md`'s WMO/ADT slice); each was written against real
corpus bytes this session, independently of the other two.

Scope: `WORLD_COMPLETENESS.md`'s **Liquid / water** section — WMO group-file
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

**Non-goal, restated from `WORLD_COMPLETENESS.md` and `DESIGN.md`**:
`LiquidType.dbc`/`LiquidMaterial.dbc`-driven shader/material identity
(which specific water-shader variant, scrolling-normal-map behavior, etc.)
is external DB2 data husk has no access to and never will, per this
project's standing CASC/DBC non-goal. The target here is a real, simplified
**renderable water-plane mesh** — positions, a flat/interpolated height
surface, an "this is liquid, treat accordingly" tag, and (where cheaply
derivable, see MH2O Case 2/3 below) a depth value — not shader-accurate
water. A future consumer wanting real WoW water shading needs its own
DBC-driven material layer on top; husk's job stops at correct geometry.

---

## 1. WMO group-file liquid (`MLIQ`)

**Current state**: `none`/`none`. Husk has no WMO parsing of any kind yet
(`DESIGN.md`'s Non-goals — WMO tracked, not started).

**Wiki citation**: `documentation/wowdev-wiki/md/WMO.md`, `## MLIQ chunk`
(lines 1912–2079, group-file section — confirmed by heading level, see
"Scoping correction" below).

### Struct, as documented

```
struct header {
    C2iVector liquidVerts;   // (x, y) vertex counts
    C2iVector liquidTiles;   // (x, y) tile counts = verts - 1
    C3Vector  liquidCorner;  // base position
    uint16_t  liquidMtlId;   // index into root MOMT
};                            // 30 bytes total

struct SMOLVert {             // xverts*yverts of these, 8 bytes each
    union {
        struct { uint8_t flow1, flow2, flow1Pct, filler; float height; } waterVert;
        struct { int16_t s, t; float height; } magmaVert;  // s/t are UVs, *3.0/256 (ADT) or *1.0/256 (WMO)
    };
};

struct SMOLTile {              // xtiles*ytiles of these, 1 byte each
    uint8_t legacyLiquidType : 4;
    uint8_t unknown1 : 1;
    uint8_t unknown2 : 1;
    uint8_t fishable : 1;
    uint8_t shared : 1;
};
```

Total chunk size = `30 + xverts*yverts*8 + xtiles*ytiles`.

A real subtlety already in the wiki, reconfirmed: **a group can have
liquid even with no `MLIQ` chunk present at all** — if `MOGP.groupLiquid`
is set (group flag `0x1000`, `SMOGroup::LIQUIDSURFACE`) but `MLIQ` is
absent or `xtiles`/`ytiles` are 0, the *entire group* is flat liquid at
`MOGP.boundingBox.max.z`. Any real implementation has to handle this
"implicit full-group liquid" case alongside the explicit vertex-grid case,
or it will silently miss liquid on some real files — flagged here so it
isn't rediscovered mid-implementation.

### Real-data verification (this session)

Scanned 2,000 real WMO **group** files (`/media/luna/data/wow_export/world/wmo/**/*_NNN.wmo`,
random sample across `dungeon/`, `expansion06/`, etc.) via an independent
Python chunk-walker (scratch, not committed — see References). **41/2000
(2.1%) carry a real `MLIQ` chunk.** `MOGP`'s own header is confirmed **68
bytes (`0x44`)** on every one of the 2,000 files checked (the `## MOGP
chunk` struct's modern/post-alpha layout) before its sub-chunks begin — this
matters because `MLIQ` (and every other group-scoped chunk) is *nested
inside* `MOGP`'s payload, not a top-level chunk of the group file (see
"Scoping correction" below).

**Full byte-for-byte confirmation, no discrepancy found**, on
`world/wmo/dungeon/md_ruinedkeep/ruinedkeep_crypt2_015.wmo`'s `MLIQ` chunk
(1178 bytes): `liquidVerts=(10,13)`, `liquidTiles=(9,12)`,
`corner=(91.67, 75.0, -80.36)`, `liquidMtlId=46`. Computed expected total
size = `30 + (10*13*8) + (9*12) = 30 + 1040 + 108 = 1178` — **exact match**
against the chunk's real declared size, the same full-accounting discipline
`.phys`'s `PLYT` verification used. Decoded vertex 0's `height` = -80.36,
matching `corner.z` closely (a flat water surface near the group's base
height, as expected); decoded tile 0's byte `0x40` → `legacyLiquidType=0,
fishable=1, shared=0` — all in-range, plausible.

**Real vertex-count-vs-chunk-size scan across all 41 hits**: not yet
individually re-verified byte-for-byte beyond the one file above (time
budget for this session went to breadth across all three sibling docs) —
recommended as the first sanity step before writing the real parser (cheap:
just the size-accounting formula above, no field semantics to get wrong).

**Scoping correction, worth stating explicitly** (see wowdev.wiki heading
structure: `# WMO group file` is a top-level heading at line 1020, and
*everything* from `## MOGP chunk` (1059) through the file's end — `MOVT`,
`MONR`, `MOLR`, `MLIQ`, `MOBN`, `MOCV`, `MOLS`/`MOLP`/lightset family,
`MPVR`/`MAVR`/`MBVR`/`MFVR`, etc. — is nested *inside* that section, i.e.
inside `MOGP`'s own payload, not top-level chunks of the group file
container). This session confirmed it empirically too: `MLIQ`/`MOLR` were
*not found at all* when searched as top-level group-file chunks; both were
found immediately once the search descended into `MOGP`'s payload past its
68-byte header. Any parser (husk's own, or a re-derivation from the wiki
cold) has to know this before writing a single line — see
`LIGHTING_TODO.md`'s own "Scoping correction" section for the same finding
applied to the lightset/point-light chunks.

### C++ data-model sketch

Mirrors `src/phys.hpp`'s idiom (real, documented struct; verified against
real files; explicit "unverified below this" floor where applicable — not
needed here, `MLIQ`'s one real sample matched perfectly):

```cpp
// src/wmo.hpp (new) -- WMO root + group file parsing. Chunk tags are
// byte-reversed on disk, same WMO/ADT convention src/phys.hpp already
// documents -- husk::readChunks/findChunk work unmodified, same as
// phys.cpp, passing the already-reversed literal tag constants.
namespace husk::wmo {

struct Vec3 { float x = 0, y = 0, z = 0; };

// One SMOLVert -- decoded generically as raw bytes + height, since the
// union's real interpretation (water vs magma) depends on the group's
// liquidMtlId -> MOMT -> shader family, not decodable from MLIQ alone.
struct LiquidVertex {
    uint8_t raw[4]{};  // flow1/flow2/flow1Pct/filler (water) or s/t as 2x int16 (magma) -- exposed raw
    float height = 0;
};

struct LiquidTile {
    uint8_t legacyLiquidType : 4 = 0;
    bool unknown1 : 1 = false;
    bool unknown2 : 1 = false;
    bool fishable : 1 = false;
    bool shared : 1 = false;
};

// One group's real MLIQ chunk, dereferenced. `vertices`/`tiles` are
// row-major per the wiki (WMO's own MLIQ is read column-major per one
// wiki contributor's own note -- re-verify against a real asymmetric
// (vertX != vertY) file before trusting either order, see Test plan).
struct LiquidSurface {
    int vertsX = 0, vertsY = 0, tilesX = 0, tilesY = 0;
    Vec3 corner;
    uint16_t materialId = 0;
    std::vector<LiquidVertex> vertices;  // vertsX*vertsY
    std::vector<LiquidTile> tiles;       // tilesX*tilesY
};

// The "implicit whole-group liquid" case (MOGP.groupLiquid set, no MLIQ
// chunk, or xtiles/ytiles == 0) -- caller synthesizes a flat single-quad
// surface at MOGP's own boundingBox.max.z instead of calling parseLiquid.
struct ImplicitLiquid {
    uint32_t groupLiquidType = 0;  // MOGP::groupLiquid, pre-resolution
    float flatHeight = 0;          // MOGP.boundingBox.max.z
};

}  // namespace husk::wmo
```

### Consumption plan

Following the collision-mesh precedent (`gltf::NamedMesh::isCollision`,
`src/cmd_export.cpp`'s `buildCollisionMesh`-equivalent block): a real WMO
liquid surface becomes a plain, unskinned `gltf::NamedMesh` per group
(name `liquid_<groupIndex>`), with a flat grid mesh built from
`liquidVerts`/`liquidTiles` (each tile → 2 triangles, standard grid
triangulation, skipping tiles whose `SMOLTile` flags indicate "don't
render" if such a flag is ever confirmed — the wiki doesn't currently
document one for `MLIQ` the way `MH2O`'s `offset_exists_bitmap` does, so
default to rendering every tile until real data says otherwise), tagged
`{"liquid": true, "liquid_type": <resolved-or-raw-int>, "fishable": bool}`
in glTF `extras` — same "tag it, don't guess at shading" treatment
`isCollision` already established, just a second boolean key on the same
node-extras object shape.

- **Parse**: `full` (once implemented against the confirmed struct).
- **Consumption**: `native` (a real glTF mesh + accessors), plus `extras`
  for the tag/type/fishable metadata.
- **glTF ceiling**: `native — 100%` for geometry; the *shader identity*
  (`LiquidType.dbc`) stays `n/a`, external dependency, by design.

---

## 2. ADT modern liquid (`MH2O`, WotLK+)

**Current state**: `none`/`none`. No ADT parsing of any kind yet.

**Wiki citation**: `documentation/wowdev-wiki/md/ADT/v18.md`, `## MH2O
chunk (WotLK+)` (lines 512–685) — includes a full worked example (the
"river crossing a chunk" case) already read closely per this investigation's
brief.

### Struct, as documented

Three logically separate regions inside one `MH2O` chunk, connected by
byte offsets relative to the chunk's own data start (not sequential
sub-chunks — no chunk tags inside `MH2O` at all, just raw offset-addressed
structs):

```
struct SMLiquidChunk {          // fixed 256-entry (16x16 MCNK grid) header
    uint32_t offset_instances;   // -> SMLiquidInstance[layer_count]
    uint32_t layer_count;         // 0 = no liquid in this MCNK
    uint32_t offset_attributes;    // -> mh2o_chunk_attributes (optional)
} chunks[256];                      // 12 bytes each, 3072 bytes total, always first

struct mh2o_chunk_attributes {
    uint64_t fishable;   // 8x8 bitmask
    uint64_t deep;       // 8x8 bitmask
};

struct SMLiquidInstance {           // 24 bytes
    uint16_t liquid_type;            // <=WotLK: LiquidType.dbc id directly
    uint16_t liquid_object_or_lvf;    // >=Cata: LiquidObject.dbc id if >=42, else direct LVF
    float min_height_level, max_height_level;
    uint8_t x_offset, y_offset, width, height;  // liquid rectangle within the 8x8 MCNK quad grid
    uint32_t offset_exists_bitmap;    // (width*height+7)/8 bytes, 0 = all-exist
    uint32_t offset_vertex_data;       // format depends on resolved LVF, see cases below
};

// vertex data, (width+1)*(height+1) entries, shape depends on LVF:
//   case 0 (LVF unresolved/<=MoP typical): { float heightmap[]; char depthmap[]; }
//   case 1: { float heightmap[]; uv_map_entry uvmap[]; }       // uv_map_entry = 2x uint16
//   case 2: { char depthmap[]; }                                // height always 0.0
//   case 3: { float heightmap[]; uv_map_entry uvmap[]; char depthmap[]; }
```

**LVF (LiquidVertexFormat) resolution without DBC access** — this is the
one place `MH2O` genuinely needs client-side data (`LiquidObject.dbc` →
`LiquidType.dbc` → `LiquidMaterial.dbc`) to resolve *correctly* when
`liquid_object_or_lvf >= 42`, per the wiki's own text. The wiki documents a
DBC-free fallback (**"Alternate case determination"**, `v18.md` lines
584–617): walk every `SMLiquidInstance` across the whole `MH2O` chunk in
address order, sort all real `SMLiquidData` offsets encountered
(`offset_attributes`/`offset_exists_bitmap`/`offset_vertex_data`), and for
each instance's `offset_vertex_data`, the byte distance to the *next*
sorted offset divided by `(width+1)*(height+1)` gives a multiplier that
maps directly to a case (5→case 0, 8→case 1, 1→case 2, 9→case 3) — no DBC
needed. **This is the resolution strategy husk should use**, consistent
with the project's hard CASC/DBC non-goal; it was not re-derived from
scratch this session (time budget went to confirming the struct against
real bytes instead, see below) but is a real, wiki-documented, mechanical
algorithm, not a guess — flagged as the first concrete implementation step.

### Real-data verification (this session)

Scanned 4,000 real ADT **root** tiles (`/media/luna/data/wow_export/world/maps/**/​<map>_<x>_<y>.adt`,
explicitly excluding Cata+ split-file variants — `_obj0`/`_obj1`/`_tex0`/
`_tex1`/`_lod` — which don't carry `MH2O` at all, it's root-file-only).
**2,683/4,000 (67.1%) carry a real `MH2O` chunk.** This is overwhelmingly
the common case, not a rare feature — most terrain tiles in this corpus
have at least one liquid-bearing `MCNK` entry.

Full worked-example decode against a real file
(`world/maps/ruinsoftheramore/ruinsoftheramore_40_38.adt`, `MH2O` chunk,
28,576 bytes): 208 of 256 `SMLiquidChunk` header entries have
`layer_count > 0`. Every decoded `SMLiquidInstance` in this file has
`liquid_type=2` (real `LiquidType.dbc` id — plausible, a real value, not
sanity-checked against the DBC itself since that's out of scope),
`liquid_object_or_lvf=42` (the documented "ocean, always case-2-ish"
sentinel), `width=height=8` (whole-`MCNK`-tile liquid, no partial
rectangle in this sample), `min_height_level=max_height_level=0.0` (a real,
plausible ocean/flat-water case), and a real, non-zero
`offset_vertex_data` in every instance — `vertex_count_expected =
(8+1)*(8+1) = 81`, matching the formula exactly. 42 of the 208 instances
also carry a real `offset_exists_bitmap` (the rest are all-exist, offset
0) — both shapes the wiki describes are present in this one file, a good
single-file cross-section of the format's real variability.

**Not yet verified this session**: the actual vertex-data bytes at
`offset_vertex_data` were not decoded down to individual
height/depth/uv values (time budget went to the header/instance structure
and the presence/prevalence numbers) — this is the concrete next step
before implementation, using the "Alternate case determination" algorithm
above on this same real file (all `liquid_object_or_lvf=42` here, so real
LVF resolution needs a file with mixed/lower values — worth widening the
sample to find one, see Test plan). No discrepancy found in anything that
*was* checked — the header/instance struct as documented matches this real
file exactly, byte-for-byte, same "expected total == actual chunk size"
accounting style as `MLIQ` above (the instance count derived from the
formula is self-consistent within the file, though a full offset-sort
cross-check wasn't performed this session).

### C++ data-model sketch

```cpp
// src/adt.hpp (new)
namespace husk::adt {

struct Vec3 { float x = 0, y = 0, z = 0; };

struct LiquidChunkAttributes {
    uint64_t fishable = 0;  // 8x8 bitmask
    uint64_t deep = 0;
};

// One SMLiquidInstance, fully resolved (vertex data decoded per its own
// determined case -- see MH2O_LVF_CASE_ALGORITHM in adt.cpp, the
// wiki's own "Alternate case determination", not a DBC lookup).
struct LiquidInstance {
    uint16_t liquidType = 0;
    uint16_t liquidObjectOrLvf = 0;
    float minHeightLevel = 0, maxHeightLevel = 0;
    uint8_t xOffset = 0, yOffset = 0, width = 1, height = 1;
    std::optional<LiquidChunkAttributes> attributes;  // per-MCNK, shared by every layer at that index
    std::vector<bool> existsBitmap;  // width*height, empty == all-exist
    // Exactly one of these is populated, per the resolved case (0/1/2/3):
    std::vector<float> heightmap;    // (width+1)*(height+1), cases 0/1/3
    std::vector<uint8_t> depthmap;   // (width+1)*(height+1), cases 0/2/3
    std::vector<std::pair<uint16_t, uint16_t>> uvmap;  // cases 1/3
};

// One MCNK's worth of liquid layers (usually 0 or 1, occasionally more).
struct LiquidChunkEntry {
    int mcnkIndex = 0;  // 0..255, row-major (matches MCNK's own row-major layout)
    std::vector<LiquidInstance> layers;
};

struct LiquidData {
    std::vector<LiquidChunkEntry> chunks;  // only entries with layer_count > 0
};

}  // namespace husk::adt
```

### Consumption plan

Same "plain unskinned `NamedMesh`, tagged in `extras`" shape as `MLIQ`
above — one mesh per contiguous liquid region (or, simpler and matching
`MCNK`'s own grid: one small mesh per `MCNK` entry that has liquid,
positioned via the tile's own world-space offset the way `MDDF`/`MODF`
placement will need to compute anyway once ADT terrain exists). Each
quad's height comes from the resolved heightmap (or `min/max_height_level`
uniformly, when no heightmap is present — the wiki's own documented
fallback); `depthmap`, when present, is exposed as `extras` (a per-vertex
depth array) rather than folded into vertex color, since there's no
established "this is depth, not color" glTF convention to lean on and this
project's own "tag it, don't guess at semantics" precedent applies
directly. `extras`: `{"liquid": true, "liquid_type": <int>, "mcnk_index":
<int>}`.

- **Parse**: `full` once the LVF-resolution algorithm and vertex-data
  decode are implemented (the two concrete remaining steps).
- **Consumption**: `native` (mesh) + `extras` (depth/type metadata).
- **glTF ceiling**: `native — 100%` for geometry — this is the single
  highest-value, most-tractable row in all of `WORLD_COMPLETENESS.md`'s
  currently-`none` table.

---

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
(`blp/`'s DXT3/JPEG) and `WIKI_FINDINGS.md` §10 (`WFV1`/`WFV2`/`DPIV`/
`AFRA` in the *original*, buggy scan) already established for this
project: a real corpus-wide zero is worth recording and moving on from,
not guessing at from wiki prose alone, especially prose the wiki itself
flags as "old and incomplete as well as maybe wrong." If a genuinely older
ADT file ever needs reading (a very old private-server dataset, say), this
section has the wiki's struct transcribed above and ready — it's simply
not worth the implementation/test cost against zero real evidence today.

- **Parse**: `n/a` (no real data to implement against).
- **Consumption**: `n/a`.
- **glTF ceiling**: `n/a, superseded` — matches `WORLD_COMPLETENESS.md`'s
  existing note exactly; this session's corpus check makes it a confirmed
  fact rather than an inference.

---

## Priority order

1. **`MH2O`** (item 2) — highest value: 67% real-file prevalence, the
   single most load-bearing liquid format in the game, a clean glTF
   translation target, and the struct is already almost fully verified
   (only the vertex-data case-resolution algorithm and per-vertex decode
   remain as concrete next steps, not open questions).
2. **`MLIQ`** (item 1) — second: real, byte-perfect verified struct,
   direct reuse of the collision-mesh `NamedMesh`/`extras` pattern, but
   blocked on WMO root+group parsing existing at all first (`MOMT` for
   material resolution, `MOGP` header for the implicit-liquid fallback
   case) — a smaller piece of a larger WMO-parsing effort, not
   standalone.
3. **`MCLQ`** (item 3) — do not implement; watch only, per the corpus's
   real 0/4,000 finding above.

---

## Test plan

**Real candidate fixtures** (paths from this session's own corpus scan,
`/media/luna/data/wow_export`, read-only — copy into `test_data/` under
this project's existing "personal WoW extraction, gitignored" convention
before use, same as every other real fixture in this repo):

- `MLIQ`: `world/wmo/dungeon/md_ruinedkeep/ruinedkeep_crypt2_015.wmo`
  (already byte-verified above, small — 40 vertices/74 triangle-equivalent
  tiles), plus `world/wmo/dungeon/9du_plaguefalldungeon_sanctum01_000.wmo`
  (12,346-byte `MLIQ`, a bigger real example) and
  `world/wmo/dungeon/thor_modan/thor_modan_001.wmo` (23,338 bytes, the
  largest found in-sample) for a size-range spread. Needs the matching
  *root* file too (`ruinedkeep_crypt2.wmo` etc.) for `MOMT`/`MOGP` context
  once WMO root parsing exists.
- `MH2O`: `world/maps/ruinsoftheramore/ruinsoftheramore_40_38.adt` (the
  worked-example file above, 208/256 populated `MCNK` entries, both
  exists-bitmap shapes present) plus
  `world/maps/ruinsoftheramore/ruinsoftheramore_40_39.adt`/`_39_39.adt`/
  `_39_38.adt` (adjacent tiles, same map, for a same-body-of-water
  multi-tile sanity check) and `world/maps/2972/2972_22_37.adt` (a
  different map, cross-check the struct isn't ruinsoftheramore-specific).
  **Still needed**: a file with `liquid_object_or_lvf < 42` (this
  session's one worked example was uniformly 42/ocean) to exercise the
  DBC-free LVF-resolution algorithm on a real non-trivial case — not yet
  found, a real next-session task (widen the scan specifically filtering
  for `0 < liquid_object_or_lvf < 42` before picking a fixture).
- `MCLQ`: none — real 0/4,000, don't build a synthetic fixture for a
  format with zero real corpus evidence in this project's own data.

**Synthetic-vs-real split**: struct-level unit tests (`MLIQ` header
field decode, `MH2O` header/instance-array offset walking, the LVF
case-multiplier algorithm) should use small hand-built synthetic fixtures
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
  own established philosophy (`DESIGN.md`'s "closest possible 1:1
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
