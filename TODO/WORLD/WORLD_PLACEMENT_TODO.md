# TODO: world placement -- what actually populates a rendered world

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed (see `../TODO_correctness.md`'s own convention) --
git history is the record of what was fixed and when, not this file. Nothing
in `src/` reads an ADT/WMO placement record yet; this file is the
implementation-ready plan for `../../WORLD_COMPLETENESS.md`'s own framing of this
as **the single most important row in that whole file**: "what actually
populates a rendered world with M2s/WMOs."

**Why this is one document, not several**: `MDDF` (doodads onto an ADT
tile), `MODF` (WMOs onto an ADT tile), and `MODS`/`MODD` (a WMO's own
internal doodad set, placing M2s *inside* a building) are the same concept
at three different scopes -- an ID/name reference, a position, a rotation,
a scale, resolved into "put this model here." `../../WORLD_COMPLETENESS.md`'s own
text makes this explicit: `MODS`/`MODD` is "the same placement concept as
ADT's `MDDF`, just scoped inside one WMO." A real implementation reuses one
placement-resolution code path for both scopes, not two -- tracking them in
separate documents would mean flipping between files constantly for what is
structurally one problem. The map-level "whole map is one WMO"
`.wdt`-scoped `MWMO`+`MODF` case is folded in here too, for the same reason
(same `MODF` record shape, just one map-wide instance instead of per-tile) --
see the dedicated section below for the explicit hand-off boundary with the
sibling `WDT_TODO.md`, which owns everything else about `.wdt` structure.

**Explicitly out of scope for this file**: WMO's own static mesh/material
identity (`WMO_GEOMETRY_TODO.md`, a sibling document from this same session)
and ADT terrain geometry (a sibling ADT-terrain document) -- this document
only covers *placement records*, never the geometry they place. Collision/
portal-culling data referenced from placement (e.g. `MODF`'s own `extents`
field, an AABB) is mentioned only where it's part of a placement record's
own byte layout, not expanded on -- that's `COLLISION_CULLING_TODO.md`'s
scope.

## The three axes

Identical to `../../WORLD_COMPLETENESS.md`'s own definitions -- repeated per
section below, not redefined here.

## Shared foundation: coordinate systems and the placement matrix

Every section below builds a 4x4 world/parent-relative transform from a
placement record's own position/rotation/scale fields -- worth establishing
once, since `MDDF`/`MODF`/`MODD` each use a **different** coordinate
convention on disk, confirmed this session by direct byte inspection, not
just the wiki's own (very good, and independently corroborated) table.

**Verified via real bytes**: `ADT/v18.md`'s own "Coordinate System
Translation" table (line ~304) states `MDDF`/`MODF` positions are stored in
a rotated frame relative to the ADT/terrain system, with the transform
`x' = 32*TILESIZE - x; z' = 32*TILESIZE - z` (`TILESIZE = 533.333...`,
`32*TILESIZE = 17066.66...`, matching the "top-left corner has X=17066"
fact from the same page's coordinate-system intro). This session confirmed
the raw, untransformed values really do come out in the expected pre-
transform range: a real `MDDF` entry from `world/maps/ruinsoftheramore/
ruinsoftheramore_40_39_obj0.adt` has `position=(21328.27, 6.46, 20818.59)` --
applying `32*TILESIZE - x` to the X and Z components lands both well inside
the documented ±17066 map extent (`17066.66*2 = 34133.33`, and
`21328.27`/`20818.59` are each less than that full span), consistent with
the transform being real and necessary, not a documentation artifact. This
session did **not** independently re-derive the exact matrix math (the
`createPlacementMatrix` JS snippets on the wiki page, both `MDDF`'s and
`MODF`'s, were read and cited but not re-implemented/re-tested against a
known landmark) -- flagged as verified-by-consistency-check, not verified-
by-independent-re-derivation, the same honesty level `WMO_GEOMETRY_TODO.md`'s
own coordinate-system note uses.

**`MODD` (WMO-internal doodad placement) uses yet another convention**,
confirmed by the wiki's own explicit text (not independently re-derived
this session): its position is `(X,Z,-Y)`, the *WMO's own local, Z-up*
space -- explicitly **not** the same rotated frame `MDDF`/`MODF` use ("It's
Z-up already, that differs it from Y-up in MODF(ADT)... and MDDF"). The
wiki's own worked example (`createPlacementMatrix(modd, wmoPlacementMatrix)`,
WMO.md line ~640) makes the composition explicit: a `MODD` doodad's world
position needs the *owning* `MODF`/global-`MODF`'s placement matrix
multiplied by `MODD`'s own local matrix -- two independent transforms
chained, not one flat coordinate space. Any husk implementation needs three
distinct matrix-builder functions (`mddfPlacementMatrix`, `modfPlacementMatrix`,
`moddLocalMatrix`), not one shared one -- despite the three record shapes
looking superficially similar (position + rotation + optional scale), their
wire coordinate conventions are genuinely different and this document's own
"one code path" framing above refers to the *placement-resolution*
concept (ID → real file → instantiate), not literally one matrix formula.

## ADT doodad (M2) placement (`MDDF` + `MMDX`/`MMID`)

**Current state**: `none`.

**Wiki citation**: `documentation/wowdev-wiki/md/ADT/v18.md`, `## MMDX
chunk` (line 207), `## MMID chunk` (line 216), `## MDDF chunk` (line 243).
Split-file location: `obj` (Cata+, i.e. `_obj0.adt`/`_obj1.adt`).

**Verified struct** (`MDDF`, 36 bytes/entry, `count = size/36`):

```
offset  field      type     note
0x00    nameId     uint32   MMID index, OR a direct FileDataID if
                            mddf_entry_is_filedata_id (flag 0x40) is set
0x04    uniqueId   uint32   should be globally unique across all loaded ADTs
0x08    position   3xfloat  see the coordinate-system section above
0x14    rotation   3xfloat  degrees, own convention (not the ADT terrain's own axes)
0x20    scale      uint16   1024 == 1.0 (unconditional, unlike MODF's scale below)
0x22    flags      uint16   MDDFFlags bitfield
```

**Real-data verification**:
- `world/maps/ruinsoftheramore/ruinsoftheramore_40_39_obj0.adt`: 739 real
  `MDDF` entries, **every single one** with `mddf_entry_is_filedata_id`
  (`0x40`) set -- `nameId` is a real, direct FileDataID in 100% of this
  file's entries (`nameId=190719` cross-checked against the community
  listfile -> `world/critter/birds/bird01.m2`, confirmed present in the
  local corpus at that exact path). `MMDX`/`MMID` are both empty (0 bytes)
  in this file -- consistent with a fully-FileDataID-mode file having no
  name table at all.
- **Corpus-scale sample** (500 real `_obj0.adt` files, randomly sampled out
  of 55,278 total in the local corpus, seed 42, independent Python decoder,
  not husk code): **110,995 total `MDDF` placements**. 110,082 (99.2%) use
  `mddf_entry_is_filedata_id` -- **100% of those** resolve through the
  community listfile to a path that exists in the local corpus. The
  remaining 913 (0.8%) use the legacy `MMID`/`MMDX` name-table path.
- **The 913 name-table entries are concentrated in exactly 3 of the 500
  sampled files** (`world/maps/kalimdor 2/...`, `world/maps/argus 1/...`,
  `world/maps/nightmareraid/...`) -- all three are internal QA/test-map
  duplicates (note the literal space in the directory name, e.g.
  `"kalimdor 2"` sitting alongside the real `"kalimdor"` map), not ordinary
  shipped content. Every name-table `MMDX` string in these files is a
  legacy, pre-FileDataID-era path, several in **ALL CAPS**
  (`WORLD\EXPANSION03\DOODADS\SKYWALL\BIGWIND\SKYWALL_BIGWIND_01.M2`) -- a
  naive case-sensitive path match against the local corpus's own
  lowercase-normalized paths fails (this session's own first-pass script
  found "0/913 resolve," which looked like a real absence until direct
  inspection of one file confirmed the referenced file **does** exist,
  case-insensitively: `world/expansion03/doodads/skywall/bigwind/
  skywall_bigwind_01.m2`). **This is a real, worth-documenting gotcha**: WoW
  paths are canonically case-insensitive (matching CASC's own semantics),
  and any local-directory-based resolution husk builds for name-table-mode
  placements must match case-insensitively, not assume the extraction tool
  already lowercased every reference consistently with its own directory
  layout.

**C++ data-model sketch** (mirrors `src/phys.hpp`'s idiom):

```cpp
// src/adt_placement.hpp -- shared by MDDF (below) and MODF (next section),
// since both are "resolve an ID to a real model, then place it" records
// differing mainly in stride/field meaning, not in shape.
namespace husk::world {

struct Vec3 { float x = 0, y = 0, z = 0; };

struct DoodadPlacement {
    uint32_t nameId = 0;      // MMID index, or a direct FileDataID (see isFileDataId)
    uint32_t uniqueId = 0;
    Vec3 position;            // raw on-disk MDDF coordinate convention, see doc comment above
    Vec3 rotationDegrees;
    float scale = 1.0f;       // already divided by 1024
    uint16_t flags = 0;
    bool isFileDataId = false;  // mddf_entry_is_filedata_id (0x40)
};

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Parses MMDX/MMID/MDDF from an already-chunk-split _obj0.adt buffer (ADT
// chunk tags are byte-reversed on disk, same WMO/.phys convention -- see
// WMO_GEOMETRY_TODO.md's own confirmation of this for ADT specifically, and
// src/chunk.hpp's existing doc comment). Throws ParseError if MDDF's size
// isn't a multiple of 36, or a non-FileDataID entry's MMID index is out of
// range for the name table.
std::vector<DoodadPlacement> parseDoodadPlacements(
    const std::vector<Chunk>& objChunks);

// Resolves a non-FileDataID DoodadPlacement's `nameId` (an MMID index) to
// its MMDX filename string. Callers needing a real file must then match
// case-insensitively against whatever local directory they're given (see
// this document's own real-data verification above for why -- legacy
// name-table paths are frequently ALL CAPS on disk in-game, but a local
// extraction's own directory layout is very likely lowercase-normalized).
std::string resolveDoodadName(const std::vector<DoodadPlacement>& placements,
                                size_t index, std::string_view mmdxBlob);

}  // namespace husk::world
```

**Recommendation**: `full` / `native` (once instanced, see the Scene
composition section below) / `native-possible, not done` -- a direct glTF
node (mesh reference + TRS) per placement, exactly `../../WORLD_COMPLETENESS.md`'s
own framing. The FileDataID-mode path (99.2% of real placements) is the one
to build first; the name-table path needs the case-insensitive-resolution
gotcha above handled explicitly, not an afterthought.

## ADT WMO placement (`MODF` + `MWMO`/`MWID`)

**Current state**: `none`.

**Wiki citation**: `ADT/v18.md`, `## MWMO chunk` (line 225), `## MWID
chunk` (line 234), `## MODF chunk` (line 428). Split-file location: `obj`.

**Verified struct** (`MODF`, 64 bytes/entry, `count = size/64`):

```
offset  field      type     note
0x00    nameId     uint32   MWID index, OR a direct FileDataID if
                            modf_entry_is_filedata_id (flag 0x8) is set
0x04    uniqueId   uint32
0x08    position   3xfloat  same coordinate convention as MDDF's own
0x14    rotation   3xfloat  degrees
0x20    extents    2xC3Vec  position-transformed AABB (lower+upper), 24 bytes
0x38    flags      uint16   MODFFlags bitfield
0x3A    doodadSet  uint16   which WMO doodad set to activate -- see the
                            internal-doodad-set section below for both the
                            plain-MODS-index case and the MWDR/MWDS
                            (Shadowlands+) indirection case
0x3C    nameSet    uint16   which WMO *name* set to use (renaming without
                            re-modeling, e.g. Goldshire Inn / Northshire Inn)
0x3E    scale      uint16   Legion+: real if modf_unk_has_scale (0x4) is set,
                            otherwise the field is meaningless and scale is
                            fixed at 1.0 -- unlike MDDF's own scale, which
                            is unconditional
```

**Real-data verification**:
- Same `ruinsoftheramore_40_39_obj0.adt` file: 29 real `MODF` entries, every
  one with `modf_entry_is_filedata_id` (`0x8`) **and** `modf_unk_has_scale`
  (`0x4`) both set (`flags=0x000c` uniformly) -- `scale=1024` on every entry,
  confirming the "real if the flag is set" case decodes to exactly 1.0x on
  real data (no entry in this file actually scales a WMO instance up/down).
  Every `nameId` (FileDataID) cross-checked against the community listfile
  and confirmed present in the local corpus (e.g. `113271` ->
  `world/wmo/kalimdor/collidabledoodads/dustwallowmarsh/theramoredocks/
  theramoredocks.wmo`).
- **Corpus-scale sample** (same 500-file random sample as `MDDF` above):
  6,702 total `MODF` placements, 6,676 (99.6%) FileDataID-mode, **100% of
  those** resolve to a real local file via the listfile. 26 (0.4%)
  name-table-mode, same 3 test-map files as `MDDF`'s own name-table
  entries, same case-sensitivity resolution gotcha applies identically.
- **A real `MODF.doodadSet` cross-references a real `MODS` table** --
  `ruinsoftheramore_40_39_obj0.adt`'s entry `[8]` (FileDataID `106872`,
  `world/wmo/azeroth/buildings/guardtower/guardtower_damaged.wmo`) has
  `doodadSet=1`; that real WMO's own root file has exactly 4 `MODS` entries
  (`Set_$DefaultGlobal`/`Set_Damaged_pieces`/`Set_Damaged_ambient`/
  `Set_Damaged_hit`), so `doodadSet=1` selects the real `Set_Damaged_pieces`
  set -- a genuine, in-range, semantically plausible cross-reference (a
  guard tower placed in-world using its "damaged" doodad variant), not just
  a structurally-valid-looking index.

**C++ data-model sketch**:

```cpp
struct WmoPlacement {
    uint32_t nameId = 0;
    uint32_t uniqueId = 0;
    Vec3 position;
    Vec3 rotationDegrees;
    Vec3 extentsMin, extentsMax;  // pre-transformed AABB, diagnostic/collision use
    uint16_t flags = 0;
    uint16_t doodadSet = 0;   // see the internal-doodad-set section for how this resolves
    uint16_t nameSet = 0;
    float scale = 1.0f;       // 1.0 unless modf_unk_has_scale is set, then real/1024
    bool isFileDataId = false;
    bool usesMwdsIndirection = false;  // modf_use_sets_from_mwds (0x80), Shadowlands+
};

std::vector<WmoPlacement> parseWmoPlacements(const std::vector<Chunk>& objChunks);
```

**Recommendation**: `full` / `native` / `native-possible, not done` -- same
shape as `MDDF` above, one level up in scope (places a whole WMO instance).

## WMO's own internal doodad set (`MODS`/`MODN`/`MODI`/`MODD`/`MDDI`, `MWDR`/`MWDS`)

**Current state**: `none`.

**Wiki citation**: `WMO.md`, `## MODS chunk` (line 560), `## MODN chunk`
(line 579), `## MODI chunk` (line 587), `## MODD chunk` (line 597), `## MDDI`
(line 723), `ADT/v18.md`'s `## MWDR (Shadowlands+)` / `## MWDS
(Shadowlands+)` (lines ~2083-2109).

**Verified struct**:

```
MODS (32 bytes/set, count = size/32):
offset  field       type      note
0x00    name        char[20]  informational, zero-terminated within the 20 bytes
0x14    startIndex  uint32    first MODD entry index for this set
0x18    count       uint32    number of MODD entries in this set
0x1C    pad         char[4]

MODN: zero-padded, zero-terminated M2 filename blob (pre-8.1 only)
MODI (8.1+, replaces MODN): flat uint32 FileDataID array

MODD (40 bytes/instance -- "not always nDoodads entries, divide chunk
      length by 40" per the wiki's own explicit warning):
offset  field         type       note
0x00    nameIndex:24  bitfield   MODN byte offset, or MODI array index
0x03    flags:8       bitfield  (upper byte of the same uint32 -- 4 real bits
                                 + 4 unused, see WMO.md line ~603)
0x04    position      3xfloat    (X,Z,-Y), WMO-local space, see coordinate section
0x10    orientation   4xfloat    quaternion (X,Y,Z,W) -- NOT euler like MDDF/MODF
0x20    scale         float
0x24    color         4xuint8    BGRA, overrides sun color; alpha has a special
                                 "index into MOLT" meaning when 0 < alpha < 255
                                 (a real, if narrow, cross-reference into the
                                 sibling lighting document's own MOLT table --
                                 noted here since it's part of MODD's own byte
                                 layout, not expanded on further)

MDDI (8.3.0+, additional-info sidecar, count = same as MODD):
  float colorMult[nDoodads];  -- multiplies MODD's own `color` field

MWDR (Shadowlands+, ADT obj-file, count = arbitrary):
  struct { uint32_t begin, end; }  -- begin/end are INCLUSIVE indices into MWDS

MWDS (Shadowlands+, ADT obj-file, flat array):
  uint16_t[]  -- each entry is a MODS index to activate
```

**Real-data verification**:
- **A real `MODS.count > 1` case, specifically searched for and found**:
  `world/wmo/azeroth/buildings/guardtower/guardtower_damaged.wmo` (4 sets,
  detailed above) and `world/wmo/dungeon/torghast/
  9du_torghast_modular_chambercap01.wmo` (**37** sets -- `DefaultGlobal`
  plus 36 mutually-exclusive named variants like `CF_Cages01`/
  `SC_Rubble01`/`C_Bones01`, `startIndex`/`count` pairs that tile the 181-
  entry `MODD` array exactly contiguously with no gaps or overlaps when
  sorted by `startIndex`). This second file also has a real, populated
  `MDDI` chunk (181 `colorMult` floats, mostly `0.0` in the sampled
  entries) -- confirms `MDDI`'s count matches `MODD`'s count exactly on a
  real file.
- **`MODI`'s real element count does NOT always match `MOHD.nDoodadNames`**
  -- checked across 5,000 real WMO root files with a `MODI` chunk: 4,274
  (85.5%) have `nDoodadNames == len(MODI)`, but 726 (14.5%) don't. Every
  mismatch found has `len(MODI) > nDoodadNames` (never less), e.g.
  `guardtower_damaged.wmo` itself: `nDoodadNames=3` in `MOHD`, but `MODI`
  has 6 real entries (2 of which are `0`, i.e. an unused/placeholder
  FileDataID slot -- `MODD`'s own 3 real instances reference `MODI` indices
  `{0, 1, 3}`, skipping the zero-valued slots `{2, 4, 5}`). **A husk parser
  must size `MODI` off the chunk's own byte length, never trust
  `MOHD.nDoodadNames`** -- this mirrors the exact same lesson `M2_
  COMPLETENESS.md`'s own header-array handling already learned for other
  M2 lookup tables (`src/m2.cpp`'s own array-count-vs-chunk-size floor
  precedent, e.g. `DETL`'s alignment padding).
- **`MWDR`/`MWDS`'s real two-level indirection, fully verified end to end
  against a real WMO's own `MODS` table**: found in 89 of a 3,000-file
  random `_obj0.adt` sample (seed 7); 390 of 33,103 sampled `MODF` entries
  (1.2%) set `modf_use_sets_from_mwds` (`0x80`). Decoded one real example
  (`world/maps/2694/2694_33_33_obj0.adt`): `MWDR` has 2 entries,
  `(begin=0,end=1)` and `(begin=2,end=3)`; `MWDS` is `[2, 8, 5, 6]`. Two
  real `MODF` placements of the **same** WMO (FileDataID `6684011`,
  resolved via the listfile to `world/wmo/expansion11/exterior/
  12xp_cave13.wmo`, confirmed present locally) have `doodadSet=0` and
  `doodadSet=1` respectively -- these are **not** direct `MODS` indices in
  this mode, they're `MWDR` indices: `doodadSet=0` -> `MWDR[0]=(0,1)` ->
  `MWDS[0:2]=[2,8]`; `doodadSet=1` -> `MWDR[1]=(2,3)` -> `MWDS[2:4]=[5,6]`.
  The real target WMO's own `MODS` table has exactly 9 entries
  (`DefaultGlobal`/`Nature01`/`Biolum01`/`Spore01`/`Shrine01`/`Nature02`/
  `Paintings01`/`Haranir01`/`Battle01`) -- indices `{2,8}` (`Biolum01`+
  `Battle01`) and `{5,6}` (`Nature02`+`Paintings01`) are both fully in
  range and semantically plausible as two different visual variants of one
  cave WMO placed twice in the same tile. **This confirms the wiki's own
  "up to 8 doodad sets can be enabled at the same time" claim is real and
  exercised on real data** -- a real placement can activate *multiple*
  `MODS` sets simultaneously via this indirection, not just one.
- **Design precedent from `wow.export`** (design reference only, no code
  copied): `ADTExporter.js`'s real doodad-set resolution logic (around line
  1429) always activates `MODS[0]` (`Set_$DefaultGlobal`, additive per the
  wiki's own text) **plus** either `MODF.doodadSet` alone, or -- when
  `modf_use_sets_from_mwds`/`useADTSets` is true -- every index the
  `MWDR`/`MWDS` indirection resolves to. This confirms the wiki's own
  "the very first one... is additive and is always displayed" framing
  matches a real, shipped tool's own resolution logic, not just prose.

**C++ data-model sketch**:

```cpp
struct DoodadSet {
    std::string name;      // MODS, up to 20 bytes
    uint32_t startIndex = 0, count = 0;
};

struct DoodadInstance {  // MODD
    uint32_t nameIndex = 0;   // MODN byte offset OR MODI array index, version-dependent
    uint8_t flags = 0;
    Vec3 position;             // WMO-local (X,Z,-Y), see coordinate section
    struct Quat { float x=0,y=0,z=0,w=1; } orientation;
    float scale = 1.0f;
    uint8_t colorBgra[4] = {};
};

struct RootFile {
    // ... header/groupNames/groupInfos/groupFileDataIds/materials above
    //     (see WMO_GEOMETRY_TODO.md) ...
    std::vector<DoodadSet> doodadSets;         // MODS
    std::string doodadNameBlob;                // MODN, pre-8.1 only
    std::vector<uint32_t> doodadFileDataIds;   // MODI, 8.1+ -- size from chunk
                                                // bytes, NEVER from header.nDoodadNames
    std::vector<DoodadInstance> doodadInstances;  // MODD
    std::vector<float> doodadColorMultipliers;    // MDDI, empty if absent
};

// Resolves which MODS indices a given MODF placement activates: always
// includes index 0 (Set_$DefaultGlobal, additive) plus either
// placement.doodadSet directly, or -- if placement.usesMwdsIndirection --
// every MWDS[begin..end] entry from MWDR[placement.doodadSet]. Mirrors
// wow.export's ADTExporter.js's own resolution logic (design reference,
// not copied).
std::vector<uint32_t> resolveActiveDoodadSets(
    const WmoPlacement& placement,
    const std::vector<std::pair<uint32_t,uint32_t>>& mwdr,  // (begin,end) inclusive
    const std::vector<uint16_t>& mwds);
```

**Recommendation**: `full` / `native` (once instanced) / `native-possible,
not done` -- same placement-node shape as `MDDF`, one level down in scope
(furniture inside a building rather than onto a terrain tile). The
`MWDR`/`MWDS` indirection and the "always-include-set-0" rule are both real,
non-optional pieces of correct resolution, not edge cases to defer.

**Decision, confirmed by Luna 2026-08-01**: export every doodad set as
separate, individually-toggleable glTF nodes (mirroring `--lod all`'s own
"every tier as its own node" precedent, `gltf::writeGlbMulti`'s existing
doc comment) rather than baking in one fixed selection -- the same
"which variant" answer M2's own geoset selection already uses
(`../../M2_COMPLETENESS.md`'s Core geometry row, `skinSectionId` extras): no
external DB2/customization data to ground a "correct" choice in, so export
every set as inert, taggable data and let a Blender script or custom
renderer pick. The size/time-multiplier concern this document originally
flagged (a world-scene context has many instanced buildings, not one hero
model) is accepted as a real cost of this choice, not a reason to deviate
from husk's own established precedent.

## Map-level global WMO placement (`.wdt`'s `MWMO` + `MODF`)

**Current state**: `none`.

**Ownership note, per this document's own opening framing**: this section
covers the `MODF` *record shape* only, since it's byte-identical to ADT's
own `MODF` above (see the wiki's own "Refer to MODF(ADT)" pointer,
`WDT.md` line ~190) -- `WDT_TODO.md` (a sibling document from this same
session) owns everything else about `.wdt` structure (`MPHD`/`MAIN`/`MAID`
tile-existence tables, the `_occ`/`_lgt`/`_fogs`/`_mpv` sidecar family).
Flagged explicitly here, per the task brief's own instruction, so neither
document duplicates this depth.

**Wiki citation**: `documentation/wowdev-wiki/md/WDT.md`, `## MWMO, MODF
chunks` (line 151).

**Verified struct**: identical 64-byte `MODF` layout to the ADT section
above, with two real differences confirmed on a real file: (1) `nameId` is
explicitly documented as unused ("always uses MWMO's content instead" --
`MWMO` here holds the literal filename, not a name-table index, since
there's only ever one WMO for the whole map); (2) exactly one entry is
legal ("The MODF chunk is limited to one entry").

**Real-data verification**: found 5 real global-map-obj `.wdt` files by
scanning for `MPHD.flags & wdt_uses_global_map_obj (0x1)` across the local
corpus's `.wdt` files (garrison building/pet-stable instances, a "blood
totem cavern" phased scenario). Decoded
`world/maps/bloodtotemcaverntaurenphase/bloodtotemcaverntaurenphase.wdt` in
full: tags are exactly `['MVER', 'MPHD', 'MAIN', 'MWMO', 'MODF']` (no
per-tile ADT files exist for this map at all, consistent with "for worlds
with terrain, parsing ends here" -- this one has none); `MWMO` holds the
literal string `world\wmo\brokenisles\highmountain\7hm_cave_medium02.wmo`
(confirmed present in the local corpus at that exact path, case-sensitive
match this time -- a modern, lowercase-normalized real path, unlike the
legacy-era name-table entries in the ADT section above); `MODF` has exactly
1 entry, `nameId=0` (confirmed unused, matches the wiki's own claim),
identity position/rotation, `flags=0x0000` (`modf_unk_has_scale` unset,
confirming `scale=0`'s raw on-disk value is correctly ignored -- real scale
is 1.0 per the flag-gated rule established in the ADT `MODF` section above).

**C++ data-model sketch**: reuses `WmoPlacement` from the ADT section
directly -- the only difference is where the single instance comes from
(a `.wdt`'s own `MWMO`/`MODF` pair, one entry, `nameId` ignored) versus an
ADT tile's `MWID`-indexed array. No new struct needed, just a different
parse entry point (`parseGlobalWmoPlacement(wdtChunks) -> optional<WmoPlacement>`,
`nullopt` if `MPHD.flags & 0x1` is unset).

**Recommendation**: `full` / `native` / `native-possible, not done` -- same
ceiling as ADT's own `MODF`, structurally identical once parsed.

## Scene-composition CLI surface -- needs a real design pass, not a snap decision

**Direction confirmed by Luna, 2026-08-01 (still not a full CLI-shape
decision -- see below for what remains genuinely open):** explicitly
**not** "cascade-export the whole game" as a goal -- that's too big a
chore to build (or trust) as one monolithic operation. Build small,
individually testable/exportable primitives first (single-WMO export,
single-ADT-tile terrain export, a placement-record resolver that's
correct and tested on its own), then layer thin **helper**
chaining/composition on top of those primitives once they exist --
explicitly the same shape husk's own M2 pipeline actually grew: `export`
started as "read one `.m2`, write one `.glb`," and `.skel`/`.phys`/`.anim`
sidecar resolution were each pulled in later, incrementally, as their own
independently-testable pieces, never as a single big-bang "resolve
everything" design. This reframes items 1-4 below from "which shape does
the *one* CLI surface take" to "what are the *individually testable* units
this eventually composes from" -- the actual subcommand/flag shape for the
*composing* layer is still explicitly deferred to its own design pass
once those units exist, per the recommendation at the end of this
section, which still stands.

**A further, explicitly speculative direction flagged by Luna, not decided,
recorded here so a future session doesn't have to rediscover it**: whether
world assembly's real end target should be a genuine **scene-descriptor
file format with dynamic loading** (an index/manifest referencing many
separately-exported `.glb`s, loaded on demand -- closer to how a game
engine actually streams a world) rather than one large pre-baked/chunked
`.glb` per tile or map. This is framed explicitly as a **potential
exploration goal**, not a requirement blocking any of the placement-parsing
work in this document -- worth a dedicated investigation (what such a
descriptor would need to contain, whether an existing convention like
glTF's own multi-file/`.gltf`-plus-external-buffers shape already covers
part of this, or whether a wholly custom manifest is warranted) once the
individually-testable primitives above exist and there's real data on how
big a "cascade the whole game" scene actually gets.

This is flagged explicitly per the task brief's own instruction: **the CLI
shape for turning placement records into one assembled scene is a much
bigger decision than any prior husk feature, and this document does not
decide it.**

Every prior husk feature (`export`, `info`, `dump-chunks`) operates on
**one** input file (an `.m2` plus its sidecars) and produces **one** output
artifact. World placement is structurally different: `MDDF`/`MODF` each
reference *many* other files (M2s, WMOs, and WMOs' own further M2
references via `MODD`), each of which itself needs its own full export
pipeline (mesh/skeleton/animation for M2s, the whole of `WMO_GEOMETRY_TODO.md`
for WMOs) before a placement record means anything renderable. Concretely
unresolved questions, none decided here:

1. **Is this a new `husk` subcommand** (e.g. `husk assemble-scene <tile.adt>
   --models-dir <dir> --wmos-dir <dir> -o scene.glb`), or does `export`
   itself grow a mode for "export a tile/map, not a single model"? The
   existing `export`/`info`/`dump-chunks` three-subcommand shape
   (`src/main.cpp`) has no precedent for "one input resolves to N other
   inputs, each independently exported" -- this is a materially different
   shape from every three-/four-state (`--skin`/`--textures`/etc.) flag
   `../../DESIGN.md`'s CLI grammar section already documents.
2. **What does the input directory convention look like?** Every existing
   sidecar flag (`--textures`/`--skin-dir`/`--anim`/`--bones-dir`/`--phys`)
   assumes a flat, FileDataID-named directory the user populated ahead of
   time (`../../DESIGN.md`'s Non-goals: no CASC access, ever). World assembly
   needs *directory trees full of already-exported `.glb`s* (or raw `.m2`/
   `.wmo` files husk exports on the fly?) referenced by FileDataID --
   does husk export referenced models on demand (invoking its own `export`
   pipeline internally, once per unique FileDataID, memoized), or does it
   require every referenced model to already be pre-exported to `.glb`
   and just wire up node references? The former is more convenient but a
   much bigger scope increase (world assembly becomes a superset of every
   other husk feature, always in scope); the latter is more composable
   with `export`'s own existing per-model flags (`--lod`, `--bones-dir`,
   etc.) but pushes a real burden onto the user to pre-export everything
   correctly.
3. **What granularity is "a scene"?** One ADT tile? A full 64x64 map (up to
   4,096 tiles, most maps sparse but some -- Kalimdor/Eastern Kingdoms --
   genuinely large)? `../../WORLD_COMPLETENESS.md`'s own `.wdt`/`MAIN`/`MAID`
   tile-existence table (`WDT_TODO.md`'s scope) is what would drive "which
   tiles exist to even consider" -- this document's own placement-
   resolution logic is agnostic to that question, but the CLI surface
   built on top of it is not.
4. **Instancing**: does one glTF scene get one node per placement (the
   straightforward, `gltf::writeGlbMulti`-precedented shape -- many nodes,
   each referencing a shared mesh/skin via glTF's own instancing-by-
   reference, which is already how `writeGlbMulti` structures multiple LOD
   tiers of one M2 sharing one skeleton) or does real-world placement
   density (a torghast-style WMO alone has 181 internal doodad instances;
   a single ADT tile can have 739+ `MDDF` placements per this session's own
   real sample) call for `EXT_mesh_gpu_instancing` or a similar batching
   extension to keep file sizes/node counts sane at map scale? Not
   researched this session -- flagged as a real scaling question, not
   assumed either way.

**This document's recommendation is to treat the above as its own plan-mode
design pass** (matching the precedent `PHYS_TODO.md`'s own "architecture
recommendation... confirmed directly by the user mid-session" set, and
`MULTIROOT_SKELETON_TODO.md`'s own "Decision" section before implementation
started) **once placement-record parsing itself (the sections above) is
implemented and battle-tested against real data** -- the record-level parse
logic (this document's real deliverable) is independent of and doesn't
block on this CLI question being settled.

## Test plan

**Synthetic fixtures**:
- `MDDF`/`MODF` happy-path parse (both FileDataID-mode and name-table-mode,
  each field individually asserted), truncated-chunk throws, out-of-range
  `MMID`/`MWID` index throws.
- `MODS`/`MODD`/`MODI` round-trip, including a deliberate `MODI`-count-vs-
  `nDoodadNames` mismatch (real-data-motivated, see the verification above)
  to prove sizing comes from chunk bytes, not the header field.
- `resolveActiveDoodadSets`: plain-index case, `MWDR`/`MWDS`-indirection
  case (multi-set activation, mirroring the real `{2,8}`/`{5,6}` example),
  and the "index 0 is always additionally active" rule.
- Case-insensitive name-table resolution helper: an ALL-CAPS synthetic path
  against a lowercase synthetic directory listing, proving the match
  succeeds where a naive `==` comparison wouldn't.

**Real-data fixtures** (candidate paths, all confirmed to exist and decode
cleanly this session):
- `world/maps/ruinsoftheramore/ruinsoftheramore_40_39_obj0.adt` -- 739 real
  `MDDF` + 29 real `MODF` entries, 100% FileDataID-mode, this document's
  primary real fixture. Exact placement counts and a handful of exact
  position/rotation/FileDataID values (already transcribed above) make good
  real-data regression assertions, matching this project's own "exact
  values, not loose bounds" discipline.
- `world/wmo/azeroth/buildings/guardtower/guardtower_damaged.wmo` -- the
  real 4-doodad-set fixture cross-referenced by `ruinsoftheramore_40_39
  _obj0.adt`'s own `MODF[8]` entry (`doodadSet=1`) -- a genuine, real
  placement-to-doodad-set cross-file chain, worth a dedicated integration
  test proving the full resolution (FileDataID -> WMO root -> MODS[1]).
- `world/wmo/dungeon/torghast/9du_torghast_modular_chambercap01.wmo` --
  37-doodad-set fixture (shared with `WMO_GEOMETRY_TODO.md`'s own test
  plan), real populated `MDDI`.
- `world/maps/2694/2694_33_33_obj0.adt` + `world/wmo/expansion11/exterior/
  12xp_cave13.wmo` -- the real, fully-verified `MWDR`/`MWDS` two-level-
  indirection chain (two placements of the same WMO activating different
  doodad-set combinations), the single most valuable real fixture for this
  document's most subtle piece of logic.
- `world/maps/bloodtotemcaverntaurenphase/bloodtotemcaverntaurenphase.wdt`
  -- the real global-single-WMO-map fixture (no ADT tiles at all).
- One of the 3 real name-table-mode files found this session (e.g.
  `world/maps/nightmareraid/nightmareraid_53_10_obj0.adt`, the smallest of
  the three) -- exercises the legacy `MMDX`/`MMID` path and the case-
  insensitive resolution gotcha against genuinely real (if rare) data,
  rather than only a synthetic case.

## References

- **wowdev.wiki**: `documentation/wowdev-wiki/md/ADT/v18.md` (`## MMDX`
  through `## MODF`, lines 207-511, including the Coordinate System
  Translation table), `documentation/wowdev-wiki/md/WMO.md` (`## MODS`
  through `## MODD`, lines 560-655), `documentation/wowdev-wiki/md/WDT.md`
  (`## MWMO, MODF chunks`, line 151). No `HUSK_AMENDMENTS.md` entries exist
  for any of these pages yet -- every finding in this document (the
  `MODI`-count mismatch, the case-insensitive name-table gotcha, the full
  `MWDR`/`MWDS` cross-reference chain) is new this session, not yet folded
  into a wiki amendment.
- **wow.export** (design reference only, no code copied):
  `reference/wow.export/src/js/3D/exporters/ADTExporter.js` (`MDDF`/`MODF`
  resolution around lines 1310-1490, the `useADTSets`/doodad-set-mask
  logic around lines 1428-1450), `reference/wow.export/src/js/3D/exporters/
  WMOExporter.js` (`doodadSetMask` handling, lines 49, 360-400, 520-545).
- **husk `src/`**: `src/phys.hpp`/`phys.cpp` (data-model idiom), `src/
  chunk.hpp`/`chunk.cpp` (reused directly, same reversed-tag convention as
  `WMO_GEOMETRY_TODO.md`), `src/gltf.hpp`'s `writeGlbMulti` (existing
  multi-node/shared-skeleton scene-assembly precedent, directly relevant to
  the CLI-surface design question above).
- **Real corpus** (`/media/luna/data/wow_export`, read-only): exact paths/
  counts cited inline. Community listfile
  (`/media/luna/work/tinker/dev/wow_modding/m2mod/mappings/listfile.csv`,
  2,206,299 entries, read-only reference) used for every FileDataID->path
  cross-check in this document.

## Priority order

1. **`MDDF`/`MODF` FileDataID-mode parsing** -- 99%+ of real placements,
   fully verified, no open data questions. The highest-value item in this
   entire WMO/ADT expansion per `../../WORLD_COMPLETENESS.md`'s own framing.
2. **`MODS`/`MODN`/`MODI`/`MODD`/`MDDI` (WMO-internal placement)** -- needed
   before a placed WMO looks structurally complete (buildings need their
   own furniture); the `MODI`-count and coordinate-convention gotchas are
   real, must-handle correctness issues, not edge cases to defer.
3. **`MWDR`/`MWDS` indirection** -- real (1.2% of sampled `MODF` entries),
   fully verified end-to-end against a real cross-file example, but a
   genuinely separate code path from plain `MODS`-index resolution; lower
   priority than the base case but not optional once WMO placement exists,
   since skipping it silently mis-renders every Shadowlands+ multi-set
   instance.
4. **Global single-WMO map placement** (`.wdt`'s `MWMO`/`MODF`) -- trivial
   once ADT `MODF` parsing exists (same struct, one entry, `nameId` ignored);
   low implementation cost, low priority only because it's a narrow case
   (5 real files found this session, all garrison/scenario instances).
5. **Name-table-mode (`MMDX`/`MMID`/`MWMO`/`MWID`) fallback path** -- real
   but rare (0.8% of sampled placements, concentrated in internal test-map
   duplicates rather than shipped content); worth implementing for
   completeness and because the case-insensitive-resolution lesson
   generalizes to any other name-table husk might encounter, but genuinely
   lower priority than the FileDataID path given how lopsided the real
   corpus distribution is.
6. **Scene-composition CLI surface** -- explicit direction confirmed
   (build individually-testable primitives first, thin helper-chaining on
   top, no monolithic whole-game-cascade export -- see its own section
   above), but the actual subcommand/flag shape for the composing layer is
   still deferred, same as before; the real prerequisite work is items
   1-5, this is the follow-up design pass once they exist.
