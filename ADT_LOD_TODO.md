# TODO: ADT terrain LOD — the Legion+ `ML*` chunk family

**Status: an open punch list, not a historical record.** Nothing here is
implemented yet. Once implementation starts, fixed items get removed
outright — git history is the record, not this file (same convention
`RO_COMPLETENESS_TODO.md`/`TODO_correctness.md` already use).

**Scope**: the third of three sibling docs expanding `WORLD_COMPLETENESS.md`'s
"Terrain geometry (ADT)" section. `WORLD_COMPLETENESS.md` explicitly
flagged this sub-area as needing "its own investigation before a per-chunk
breakdown would mean anything" — this file *is* that investigation, done
against `documentation/wowdev-wiki/md/ADTLodImplementation.md` (read first,
in full, per the task brief) plus every `ML*`/`MB*` chunk's own struct in
`ADT/v18.md`, cross-checked against real bytes. Covers: `_lod.adt`'s own
terrain/liquid LOD chunks (`MLHD`/`MLVH`/`MLVI`/`MLLL`/`MLND`/`MLSI`/
`MLLD`/`MLLN`/`MLLV`/`MLLI`, plus the MoP+ blend-mesh family `MBMH`/`MBBB`/
`MBNV`/`MBMI`/`MBMB`), and `_obj1.adt`'s own object-LOD chunks (`MLMD`/
`MLMX`/`MLDD`/`MLDX`/`MLDL`/`MLFD`/`MLMB`/`MLDB`, plus Shadowlands+
`MWDR`/`MWDS`). Does **not** cover `ADT_TERRAIN_TODO.md`'s own scope
(core `MCVT`/`MCNR`/holes/split-file resolution — this file assumes that
groundwork exists) or `WORLD_PLACEMENT_TODO.md`'s scope (the *non-LOD*
`MDDF`/`MODF` placement records this file's `MLDD`/`MLMD` are the
LOD-scale analogue of).

**Real-data verification discipline**: independent Python scripts against
`/media/luna/data/wow_export` (read-only), no reuse of a not-yet-written
husk parser. A random sample of 2,000 of 46,341 real `_lod.adt` files and
1,500 of 55,278 real `_obj1.adt` files (seeded, reproducible) — a first
full-corpus attempt at `_lod.adt` was killed after several minutes with
zero results on this session's storage, same characteristic
`ADT_TERRAIN_TODO.md`'s own Environment note already documents.

## `ADTLodImplementation.md`'s own model, read first (per the task brief)

Before any per-chunk breakdown, the implementation guide's own worked
description (by "Zee," the same author wowdev.wiki's page credits) gives
the shape everything below has to fit:

- **One shared vertex buffer per tile**, laid out as: `[129×129]` outer
  "square" vertices, followed by `[128×128]` inner "center" vertices
  (offset half a cell from the outer grid, same interleaving idea `MCVT`'s
  own 9×9/8×8 already uses, just at the whole-16×16-`MCNK`-tile scale
  instead of per-`MCNK`), followed by **`[x]` "thickness"/skirt
  vertices** — duplicates of border vertices, moved down 32 units (196 for
  `.wdl`), to hide seams where differently-detailed LOD chunks meet.
- **A quad-tree** (`MLND`) is walked per-tile based on camera distance;
  each visited node either contributes its own index range (from `MLVI`,
  via `MLND.index`/`.length`) or descends into its 4 children.
- **`MLLL`** maps a quad-tree depth to a `lod` band value and a second
  index range (also into `MLVI`) used for the border/skirt triangles at
  that specific LOD depth.
- **`MLSI`** lists which vertices (by index into the *outer* 129×129 part
  of `MLVH` only) get a duplicated, dropped-down "skirt" copy appended to
  the vertex buffer.

## 1. `MLHD`/`MLVH`/`MLVI` — the shared per-tile vertex/index buffers

**Current state**: `none`/`none`.

**Wiki citation**: `ADT/v18.md`, `## MLHD (Legion+)` (1701–1716), `## MLVH
(Legion+)` (1717–1734), `## MLVI (Legion+)` (1735–1748).

**Verified**:

- `MLHD` (`uint32_t unknown` + `float[6]` = 28 bytes): **2,000/2,000
  sampled files decode a clean 28-byte chunk**, matching the wiki's
  documented size exactly (`some_kind_of_bounding[6]` — plausibly a
  min/max AABB pair, same shape as `CAaBox`, not independently confirmed
  by field-level semantics this session).
- `MLVH` (`float ml_v_heightData[129*129 + 128*128 + additional]`): the
  wiki's own "+additional" hedge is real and directly confirmed. **The
  minimum real float count across the whole 2,000-file sample is exactly
  33,025 = 129×129 + 128×128** (the two fixed grids, zero "additional"
  data) — 399 real files hit this floor exactly. The next most common
  value, 33,089 (693 files), is exactly `33025 + 64`. Every other observed
  value sits between 33,025 and 33,370 (the sample's real maximum) — the
  "additional" tail is a genuine, per-tile-variable skirt-vertex count,
  not a fixed constant, consistent with `ADTLodImplementation.md`'s "`[x]`
  thickness vertices" framing (`x` = however many entries `MLSI` lists for
  that tile, since each skirt vertex is a *duplicate* of an existing
  vertex, not new data).
- `MLVI` (`uint16_t ml_v_indices[]`): **max index value observed across
  the whole sample is 33,370** — exactly equal to the sample's own
  observed `MLVH` maximum float count, and never higher. This is a real,
  clean confirmation that indices never reference past the real vertex
  buffer's own extent (base grids + that file's own real skirt tail),
  checked at scale (2,000 files), not asserted from the wiki text alone.

### Data model sketch

```cpp
namespace husk::adt_lod {

struct Header {  // MLHD, 28 bytes
    uint32_t unknown = 0;
    std::array<float, 6> bounding{};  // plausibly min/max C3Vector -- unconfirmed field-level semantics
};

// One tile's full LOD vertex buffer: 129x129 outer + 128x128 inner +
// however many skirt duplicates MLSI lists for this tile (real range
// observed: 0 to ~345 extra floats, i.e. up to ~345 skirt vertices).
struct VertexBuffer {
    std::array<float, 129 * 129> outerHeights{};
    std::array<float, 128 * 128> innerHeights{};
    std::vector<float> skirtHeights;  // one per MLSI entry, same file
};

using IndexBuffer = std::vector<uint16_t>;  // MLVI, GL_TRIANGLES-shaped per the wiki

}  // namespace husk::adt_lod
```

### Parse / Consumption / glTF ceiling

| Concept | Parse | Consumption | glTF ceiling |
|---|---|---|---|
| `MLHD`/`MLVH`/`MLVI` | `full` | `native`, as a distinct lower-detail LOD mesh | `native-possible, not done` — same shape as ADT's own core `MCVT`/`MCNR` (`ADT_TERRAIN_TODO.md` §2), just a coarser, whole-tile-scale grid instead of per-`MCNK` |

## 2. `MLLL`/`MLND`/`MLSI` — LOD bands and the quad-tree

**Current state**: `none`/`none`.

**Wiki citation**: `ADT/v18.md`, `## MLLL (Legion+)` (1749–1799), `## MLND
(Legion+)` (1800–1820), `## MLSI (Legion+)` (1821–1834).

**Verified**:

- `MLLL` (20-byte entries: `float lod`, `uint32_t height_index/height_length/
  mapAreaLow_length/mapAreaLow_index`): **every one of the 2,000 sampled
  files has exactly 4 entries** — a fully consistent count, not "usually
  4." **A real, worth-flagging deviation from `ADTLodImplementation.md`'s
  own prose**: that page's "Some thoughts" section states "Least detailed
  level is 32.0 and most detailed level is 2" (implying 5 real bands: 2,
  4, 8, 16, 32) — but **the real, distinct `lod` float values seen across
  all 2,000 files are exactly `{4.0, 8.0, 16.0, 32.0}`, never 2.0**. This
  is not a contradiction once you read the rest of that same page's own
  text carefully: "lod 2 essentially is the same as rendering the terrain
  from MCNK of main ADT" — i.e., the finest detail level is the *base
  ADT's own* `MCVT`/`MCNR` mesh (`ADT_TERRAIN_TODO.md`'s scope), not a
  separate `MLLL` entry at all. `_lod.adt` only needs to store the 4
  *reduced*-detail bands; "full detail" is simply "don't use the LOD file,
  render the real tile." Implementers should treat `MLLL`'s real band set
  as `{4, 8, 16, 32}` and fall back to the base ADT tile below that,
  rather than expecting a 5th, most-detailed `MLLL` entry that no real
  file in this corpus (2,000-file sample) actually has.
- `MLND` (24-byte quad-tree node: `index`/`length`/`_2`/`_3` as
  `uint32_t` + `indices[4]` as `uint16_t` = 16+8=24): **all 2,000 sampled
  files are internally consistent at exactly this 24-byte stride, zero
  exceptions** — confirms the wiki's struct is exactly right, no
  undocumented padding/variant the way `.phys`'s `PLYT`/`SHOJ` had.
- `MLSI` (`uint16_t[]`, indices into `MLVH`): **max referenced index
  across the whole 2,000-file sample is exactly 16,640 = 129×129 − 1** —
  a clean, corpus-scale (not "so far I saw," the wiki's own hedge)
  confirmation that `MLSI` only ever references the **outer** 129×129
  part of `MLVH`, never the inner 128×128 grid or an already-appended
  skirt vertex. Worth stating as confirmed, not speculative, in any
  implementation and in a future `WIKI_FINDINGS.md` follow-up (not written
  this session).

### Data model sketch

```cpp
namespace husk::adt_lod {

struct LodBand {  // one MLLL entry
    float lod = 0;              // real values seen: {4.0, 8.0, 16.0, 32.0} -- see finding above, no 2.0 band exists
    uint32_t heightIndex = 0, heightLength = 0;      // range into MLVI, terrain triangles at this band
    uint32_t mapAreaLowIndex = 0, mapAreaLowLength = 0;  // range into MLVI, border/skirt triangles at this band
};

struct QuadTreeNode {  // one MLND entry, 24 bytes
    uint32_t index = 0, length = 0;  // range into MLVI
    uint32_t unk2 = 0, unk3 = 0;
    std::array<uint16_t, 4> childIndices{};  // indices into the same MLND array, 0xFFFF-ish sentinel for "leaf" -- confirm exact sentinel value against real data before relying on it
};

}  // namespace husk::adt_lod
```

### Parse / Consumption / glTF ceiling

| Concept | Parse | Consumption | glTF ceiling |
|---|---|---|---|
| `MLLL`/`MLND`/`MLSI` | `full` | feeds mesh selection (which triangles a given LOD mesh includes) | folds into §1's `native-possible, not done` — this is index-selection logic, not separate geometry |

### Open design question

**A quad-tree has no single obvious "one mesh" answer** the way `MCVT`'s
fixed grid does. Recommend exporting **one glTF mesh per `MLLL` band**
(4 real bands per tile, confirmed above) rather than trying to reproduce
the client's own runtime camera-distance-driven quad-tree descent — a
static export has no camera, so "pick one band per distance" doesn't
apply; exporting all 4 as separate, named (`lod_4`/`lod_8`/`lod_16`/
`lod_32`) alternative meshes (mirroring how `husk export --lod` already
handles M2's own per-model LOD tiers, `M2_COMPLETENESS.md`) is the more
consistent fit with this project's existing multi-LOD precedent. Not
decided — flagged for a real design pass.

## 3. Liquid LOD (`MLLD`/`MLLN`/`MLLV`/`MLLI`)

**Current state**: `none`/`none`.

**Wiki citation**: `ADT/v18.md`, `## MLLD (Legion+)` (1835–1892), `## MLLN
(Legion+)` (1893–1915), `## MLLV (Legion+)` (1916–1927), `## MLLI
(Legion+)` (1928–1939).

**Verified**: `MLLD`/`MLLN`/`MLLI`/`MLLV` are all present together, with
**identical counts, in 1,043/2,000 sampled files (52%)** — real,
common, not a rare edge case, but genuinely absent from roughly half of
real tiles (consistent with "not every tile has a body of water").
`MLLN`'s 20-byte struct decodes cleanly (`_0`/`num_indices`/`_2`/`_3a`/
`_3b`/`_4`/`_5`) wherever present — `num_indices` field values weren't
independently cross-checked against the paired `MLLI` chunk's own real
byte length this session (a concrete, cheap follow-up for whoever
implements this). `MLLD`'s own compressed depth/alpha texture format
(RLE-style run-length blob, per the wiki's pseudocode) was **not**
byte-decoded this session — it's a genuinely more involved compressed
format (the wiki gives raw decompiled pseudocode, not a clean struct) and
lower priority than the terrain mesh itself; flagged as unverified, not
skipped by oversight.

### Data model sketch

```cpp
namespace husk::adt_lod {

struct LiquidLodHeader {  // MLLD, per-instance header
    uint32_t flags = 0;   // bit0: has tile data, bit1: depth compressed, bit2: alpha compressed
    uint16_t sizeDepth = 0, sizeAlphaIsh = 0;
    std::vector<uint8_t> depthTextureRaw;  // RLE-compressed if flags & 2, see wiki pseudocode -- unverified against real bytes this session
    std::vector<uint8_t> alphaTextureRaw;  // expands to 16384 bytes per the wiki's decompiled routine -- unverified this session
};

struct LiquidMesh {  // MLLN + its MLLI/MLLV pair, order-dependent per the wiki
    uint32_t unk0 = 0, unk2 = 0, unk4 = 0, unk5 = 0;
    uint16_t unk3a = 0, unk3b = 0;
    std::vector<Vec3> vertices;               // MLLV
    std::vector<std::array<uint16_t, 3>> indices;  // MLLI, 3 shorts into vertices -- C3sVector-shaped, confirm signed-vs-unsigned against real data (wiki says C3sVector = int16_t, but these are vertex-buffer indices, so treat as unsigned in practice unless a real negative value turns up)
};

}  // namespace husk::adt_lod
```

### Parse / Consumption / glTF ceiling

| Concept | Parse | Consumption | glTF ceiling |
|---|---|---|---|
| `MLLN`/`MLLV`/`MLLI` (liquid mesh) | `full` | `native` | `native-possible, not done` — a plain triangle mesh, no different in kind from the terrain mesh itself |
| `MLLD` (compressed depth/alpha) | `descriptor`/unverified | `diagnostic` at best until decoded | `n/a` until the compression format is verified against real bytes — lower priority, flagged not skipped |

## 4. MoP+ blend-mesh family (`MBMH`/`MBBB`/`MBNV`/`MBMI`/`MBMB`)

**Current state**: `none`/`none`.

**Wiki citation**: `ADT/v18.md`, `## MBMH (MoP+)` (1614–1634), `## MBBB
(MoP+)` (1636–1652), `## MBNV (MoP+)` (1653–1670), `## MBMI (MoP+)`
(1671–1685), `## MBMB (Legion+)` (2045–2058, `_lod`-resident per the
wiki's own "split files" annotation, unlike the other four which are
"root, lod").

**Verified**: `MBMH`/`MBBB`/`MBNV`/`MBMI`/`MBMB` all present together,
matching counts, in **110/2,000 sampled `_lod.adt` files (5.5%)** — real
but genuinely rare in this corpus. Not independently byte-decoded this
session beyond confirming co-presence — the wiki's own struct comments
already flag several fields as pure guesses (`MBMB`: "It's related to
blend meshes - BlendMeshBatches" is itself tagged unverified), and 110
real files is a small enough sample that a deeper verification pass is a
worthwhile but separate follow-up, not blocking the higher-priority items
above.

### Parse / Consumption / glTF ceiling

| Concept | Parse | Consumption | glTF ceiling |
|---|---|---|---|
| Blend-mesh family | `descriptor` | `diagnostic` | `n/a`/`node-possible, unclaimed` — genuinely under-verified upstream, low real-file volume, low priority |

## 5. `_obj1.adt` — Legion+ object LOD placement

**Current state**: `none`/`none`.

**Wiki citation**: `ADT/v18.md`, `## MLMD (Legion+)` (1940–1961), `## MLMX
(Legion+)` (1962–1984), `## MLDD (Legion+)` (1985–1996), `## MLDX
(Legion+)` (1997–2014), `## MLDL (Legion+)` (2015–2026), `## MLFD
(Legion+)` (2027–2044), `## MLMB (BfA+)` (2059–2070), `## MLDB
(Shadowlands+)` (2071–2082), `## MWDR (Shadowlands+)` (2083–2098), `## MWDS
(Shadowlands+)` (2099–2110).

### Verified

- **`MLMD`/`MLMX` (WMO LOD placement + extents) and `MLDD`/`MLDX` (M2 LOD
  placement + extents) are present in all 1,500 sampled `_obj1.adt`
  files (100%)** — i.e., unlike the liquid/blend-mesh families above,
  every real `_obj1.adt` in this sample carries object-LOD placement data,
  not just a fraction. Entry-count parity confirmed on all 1,500 both
  ways: `len(MLMD)/40 == len(MLMX)/28` (40 bytes = `SMMapObjDef`-minus-
  bounding-box per the wiki's own comment "same as MODF but without
  bounding box"; 28 bytes = `CAaBox`(24) + `float radius`(4)), and
  `len(MLDD)/36 == len(MLDX)/28` (36 bytes = `SMDoodadDef`'s real stride,
  same struct `MDDF` itself uses).
- **`MLFD`** (per-3-LOD-band offset/length into `MLDD`/`MLMD`) is present
  in only **332/1,500 (22%)** — real, but genuinely optional on top of
  `MLDD`/`MLMD` themselves always being present. This means an
  implementation cannot assume `MLFD` exists just because `MLDD`/`MLMD`
  do — it should have its own fallback (treat all of `MLDD`/`MLMD` as one
  flat, non-LOD-banded list when `MLFD` is absent).
- **`MLDL`** (Legion+, "if the corresponding `MLDD` has flag 0x8, this has
  a value, else 0"): present in 116/1,500 (7.7%).
- **`MWDR`/`MWDS`** (Shadowlands+ doodad-set-selection): present in
  47/1,500 (3.1%) — consistent, independently, with `ADT_TERRAIN_TODO.md`
  §5's own separate measurement on `_obj0.adt` (77/1,500, 5.1%, a
  different random sample) — two independent samples landing in the same
  small-single-digit-percent ballpark, a real, rare-but-genuine feature,
  not a fluke of one sample.
- **`MLMB`** (BfA+, opaque blob per the wiki): 89/1,500 (5.9%).
- **`MLDB`** (Shadowlands+, opaque blob): 30/1,500 (2%).

**Two real findings beyond the task's own named chunk list**:

1. **`MMDX`/`MMID`/`MWMO`/`MWID` (the M2/WMO filename tables) sometimes
   appear inside `_obj1.adt` itself** — 56/1,500 (3.7%) of sampled
   `_obj1.adt` files carry their own copy. `ADT/v18.md`'s own per-chunk
   annotations for these four only say `"split files: obj"` (no `0`/`1`
   suffix, unlike `MLMD`/`MLDD`'s explicit `"split files: obj1"`) — this
   real finding confirms the ambiguous "obj" tag genuinely means "can be
   either," not a wiki omission that always resolves to `obj0`. This
   matters for `WORLD_PLACEMENT_TODO.md`'s own resolution logic (not this
   file's job to plan, but worth flagging so that sibling doc's own
   `MDDF`/`MODF`-name-table resolution checks both `_obj0` and `_obj1`,
   not just `_obj0`).
2. **A genuinely undocumented chunk pair, `MDLI`/`MDLD`**, appears in
   23/1,500 (1.5%) sampled `_obj1.adt` files — not present anywhere in
   `ADT/v18.md` (checked via `grep` across the full page, zero hits for
   either tag). Not investigated further this session (small sample, out
   of the task's named scope) — flagged here as a genuine "the wiki
   doesn't have this yet" finding for a future targeted investigation,
   same disposition `WORLD_COMPLETENESS.md`'s own M3 discovery already
   got.

### Data model sketch

```cpp
namespace husk::adt_lod {

struct WmoLodPlacement {  // MLMD, 40 bytes -- SMMapObjDef minus the bounding box
    uint32_t nameId = 0, uniqueId = 0;
    Vec3 position, rotation;
    uint16_t flags = 0, doodadSet = 0, nameSet = 0, scale = 0;
};
struct WmoLodExtent { CAaBox bounding; float radius = 0; };  // MLMX, 28 bytes, same count as MLMD

// MLDD reuses MDDF's own SMDoodadDef verbatim (WORLD_PLACEMENT_TODO.md's
// data model, not redefined here) -- 36 bytes.
struct DoodadLodExtent { CAaBox bounding; float radius = 0; };  // MLDX, 28 bytes, same count as MLDD

struct LodObjectRange {  // MLFD, per-tile, optional (only 22% of real files)
    std::array<uint32_t, 3> m2LodOffset{}, m2LodLength{};   // into MLDD, per LOD band
    std::array<uint32_t, 3> wmoLodOffset{}, wmoLodLength{}; // into MLMD, per LOD band
};

// MWDR/MWDS -- shared shape with ADT_TERRAIN_TODO.md's own obj0-scoped
// finding; same struct, don't redefine independently in both docs.

}  // namespace husk::adt_lod
```

### Parse / Consumption / glTF ceiling

| Concept | Parse | Consumption | glTF ceiling |
|---|---|---|---|
| `MLMD`/`MLMX`/`MLDD`/`MLDX` | `full` | depends on M2/WMO export existing | `native-possible, not done` — same shape as `MDDF`/`MODF` (`WORLD_PLACEMENT_TODO.md`), just at LOD/distant-rendering scale |
| `MLFD` | `full` | `diagnostic`/feeds mesh selection | folds into the above — index-selection metadata, not separate geometry |
| `MLDL`/`MLMB`/`MLDB` | `descriptor` | `diagnostic` | `n/a` — opaque per the wiki itself, low priority |

### Open design question

Whether LOD object placement (`MLDD`/`MLMD`) becomes its own separate set
of glTF nodes (tagged, e.g., `"lod_tier"` extras) alongside the base
`MDDF`/`MODF`-driven placement, or is skipped entirely in a first-pass
world export (base placement matters far more for "does the world look
right" up close) — recommend **skipping in a first pass**, matching
`ADT_TERRAIN_TODO.md`'s own priority ordering (base terrain/placement
before any LOD concern) — not decided, flagged for whoever picks this up
alongside `WORLD_PLACEMENT_TODO.md`.

## Test plan

**Real fixture candidates** (`/media/luna/data/wow_export`, to copy into
`test_data/`, gitignored):

- `world/maps/azeroth/azeroth_27_25_lod.adt` — confirmed real, has `MLHD`
  and (per the sample) is on a well-populated continent likely to have
  liquid/blend-mesh content too; re-verify its exact chunk set at
  implementation time rather than assuming from this session's aggregate
  counts alone.
- Any `_obj1.adt` from the same tile — confirmed 100% real `MLMD`/`MLDD`
  presence in this corpus, so any tile with a committed `_lod.adt` fixture
  should also have a usable `_obj1.adt` sibling.
- A real `MLLD`/`MLLN`-bearing tile for the liquid-LOD path (1,043/2,000
  sampled files qualify — not individually pinned by path this session,
  cheap to re-derive with the same script).
- A real `MWDR`/`MWDS`-bearing `_obj1.adt` (47 candidates found — same
  re-derivation note).

**Synthetic fixtures should cover**: the `MLLL` 4-band assumption
explicitly (a regression test asserting exactly `{4, 8, 16, 32}`, no 2.0
band, so a future corpus surprise is caught immediately rather than
silently mis-generalized), `MLND`'s quad-tree leaf-vs-branch distinction
(needs a real or carefully-constructed synthetic sentinel value — **not
confirmed this session** what value `childIndices` holds for a leaf node;
a first concrete follow-up before relying on this struct), and `MLFD`'s
absence path (22% real presence means the common case in a small test
suite could accidentally always include it unless deliberately tested
without).

## References

- **wowdev.wiki**: `documentation/wowdev-wiki/md/ADTLodImplementation.md`
  (entire page, read first per the task brief — the algorithmic model
  every struct below has to fit), `documentation/wowdev-wiki/md/ADT/v18.md`
  (`## MLHD` through `## MWDS` sections, all Legion+/BfA+/SL+ `ML*`/`MB*`
  chunks, cited individually above).
- **wow.export** (design ideas only, never code): checked
  `reference/wow.export/src/js/3D/loaders/ADTLoader.js` and
  `ADTExporter.js` — neither implements the `ML*` LOD family at all (grep
  for `MLHD`/`MLVH`/`MLND` in both files: zero hits) — i.e., this
  established community tool doesn't attempt terrain LOD export either,
  consistent with `WORLD_COMPLETENESS.md`'s own framing of this as the
  least-trodden sub-area of the whole WMO/ADT expansion. No design
  precedent to borrow from that tool for this specific file.
- **husk `src/` to reuse**: `src/chunk.hpp`/`chunk.cpp` (chunk walker,
  reused unmodified, same reversed-tag caveat as the sibling docs),
  `ADT_TERRAIN_TODO.md`'s own `HeightGrid`/`Holes` structs (the base-tier
  terrain this file's LOD tiers sit alongside — `husk export --lod`'s
  existing M2 multi-tier precedent, `M2_COMPLETENESS.md`, is the closest
  existing analogue for how multiple detail tiers of the same geometry
  already get exported).
- **Real corpus evidence**: `/media/luna/data/wow_export/world/maps/**`
  — 2,000/46,341 real `_lod.adt` files and 1,500/55,278 real `_obj1.adt`
  files (both random samples, seeded/reproducible — seeds 5678 and the
  script's own default, respectively). Exact counts cited inline per
  finding above.

## Priority order

1. **§1/§2 (`MLHD`/`MLVH`/`MLVI`/`MLLL`/`MLND`/`MLSI`)** — the actual LOD
   terrain geometry, fully verified this session, no open struct
   questions beyond `MLND`'s leaf-sentinel value.
2. **§5 (`MLMD`/`MLDD` object LOD placement)** — real, always-present
   (100% of sampled `_obj1.adt` files), structurally simple (reuses
   `MDDF`'s own struct), but genuinely lower value than the terrain mesh
   itself for "does the world look right" — distant-object placement is a
   secondary concern relative to distant terrain.
3. **§3 (liquid LOD)** — real and common (52%) but a real, unverified
   compression format (`MLLD`'s depth/alpha blob) sits between "parse the
   header" and "render the water" — budget for that separately.
4. **§4 (blend-mesh family)** — rare (5.5%), upstream-under-verified even
   on the wiki itself, lowest priority of the five items in this file.
