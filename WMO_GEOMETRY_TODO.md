# TODO: WMO static mesh/material identity (root+group split, geometry, materials)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed (see `TODO_correctness.md`'s own convention) --
git history is the record of what was fixed and when, not this file. Nothing
in `src/` reads a WMO byte yet; this file is the implementation-ready plan
for the "WMO static geometry & materials" and "root+group file split,
skybox, chunk container" rows of `WORLD_COMPLETENESS.md` (see that file's
own "World structure & scene composition" and "WMO static geometry &
materials" sections for the scope this document narrows down from).

**Why this file, not another investigation document**: `WORLD_COMPLETENESS.md`
already did the broad survey (every WMO/ADT concept, `none`/`none` across the
board). This document goes one level deeper for WMO's own static-mesh-and-
material identity specifically -- every struct below has been verified
against real bytes this session (never just transcribed from the wiki), with
exact corpus paths/counts, so a future implementation session can start
writing `src/wmo.hpp`/`wmo.cpp` directly rather than re-deriving byte layouts
from wowdev.wiki from scratch.

**Explicitly out of scope for this file** (owned by sibling documents from
the same WMO/ADT expansion pass, so their depth isn't duplicated here):
- **Collision** (`MOBN`/`MOBR` BSP tree, `MCVP` convex volumes, `MOPL`
  terrain-cutting planes, `MOPY`/`MPY2`'s *collision* bit-half) --
  `COLLISION_CULLING_TODO.md`. This document covers `MOPY`/`MPY2`'s
  *material*-index half only (see that section below for the exact split).
- **Portals/visibility culling** (`MOPV`/`MOPT`/`MOPR`/`MOPE`, `MOVV`/`MOVB`)
  -- same sibling document.
- **Placement** (`MODS`/`MODN`/`MODI`/`MODD`/`MDDI`, ADT's `MDDF`/`MODF`) --
  `WORLD_PLACEMENT_TODO.md`. A WMO's own doodad *set* definitions live there
  (they're a placement concept, not a geometry/material one), even though
  `MODS` is a root-file chunk sitting right next to `MOHD`/`MOMT` covered
  here.
- **Liquid** (`MLIQ`), **lighting** (`MOLT`/`MOLR`/`MOLS`/`MOLP`/`MNLD`/...),
  **fog** (`MFOG`/`MFED`/`MFOB`, `MPVD`/`MAVG`/`MAVD`/`MBVD`) -- a sibling
  liquid/lighting/fog document.
- **ADT terrain** (`MCVT`/`MCNR`, terrain holes, `MH2O`, ADT LOD) -- a
  sibling ADT-terrain document.
- **`.wdt`/`.wdl`** structure beyond the one placement case
  `WORLD_PLACEMENT_TODO.md` covers -- a sibling `WDT_TODO.md`.

## The three axes

Identical to `WORLD_COMPLETENESS.md`'s own definitions (Parse depth:
`none`/`descriptor`/`deref`/`full`; Consumption: `none`/`diagnostic`/`extras`/
`native`; glTF ceiling: see that file's table) -- repeated in each section's
recommendation line below, not redefined here.

## Chunk-tag reversal (applies to every section below)

**Real, confirmed this session, not just transcribed from `src/phys.hpp`'s
doc comment**: WMO chunk tags are byte-reversed on disk, same convention as
`.phys` (WMO/ADT-style), the *opposite* of M2's own inline chunks (see
`src/chunk.hpp`'s doc comment, which already states this correctly). A tag
written as `MVER` in the wiki is stored as the literal bytes `R`,`E`,`V`,`M`
in the file. Confirmed independently this session: a first-pass scanner that
read tags without reversing produced `REVM`/`FDOM`/`FDDM`/`KNCM` instead of
`MVER`/`MODF`/`MDDF`/`MCNK` for both real `.wmo` and real `_obj0.adt` files --
the reversal is real for **both** formats, not just `.phys`. `src/chunk.cpp`'s
`readChunks` itself is tag-agnostic (it doesn't reverse or un-reverse
anything -- see `src/phys.cpp`'s own already-reversed tag constants for the
existing precedent) so WMO parsing can reuse `husk::readChunks`/`findChunk`
directly, exactly the way `phys.cpp` does, with a `kTagMver = "REVM"`-style
table of pre-reversed literals.

## Chunk container / magic / header (`MVER`, `MOHD`, `MOGP` header)

**Current state**: `none` -- no WMO byte is read at all.

**Wiki citation**: `documentation/wowdev-wiki/md/WMO.md`, `# MVER` (line 37),
`## MOHD chunk` (line 72), `## MOGP chunk` (line 1059, the group file's own
68-byte header -- **not** a separate top-level chunk, see the group-file
section below for why this matters).

**Verified struct** (`MOHD`, 64 bytes, byte-exact against real files):

```
offset  field                    type
0x00    nTextures                uint32
0x04    nGroups                  uint32
0x08    nPortals                 uint32
0x0C    nLights                  uint32
0x10    nDoodadNames             uint32   -- see the MODI section in
                                             WORLD_PLACEMENT_TODO.md: this
                                             field is NOT a reliable count of
                                             MODI's real element count once
                                             MODN was replaced by MODI
0x14    nDoodadDefs              uint32
0x18    nDoodadSets              uint32
0x1C    ambColor                 CArgb (uint32)
0x20    wmoID                    uint32 (WMOAreaTable.dbc foreign key)
0x24    bounding_box             CAaBox (2x C3Vector, 24 bytes)
0x3C    flags                    uint16 (bitfield, see wiki lines 84-92)
0x3E    numLod                   uint16
```

**Real-data verification**: decoded via an independent, from-scratch Python
chunk-walker (`wmo_root_decode.py`, scratchpad, not committed -- same
discipline every prior husk corpus check has used) against
`world/wmo/cameron.wmo` (392 bytes -- the smallest real root file this
session found) and `world/wmo/dungeon/torghast/9du_torghast_modular_
chambercap01.wmo` (17,872 bytes, a real multi-group/multi-doodad-set file,
see below). Every `MOHD` field decoded cleanly and self-consistently
(`nGroups` matches `MOGI`'s own real record count on both files; `numLod`
correctly predicts `GFID`'s real array length, see the GFID section).
`flags`/`bounding_box` weren't independently re-derived beyond "decodes to
plausible finite floats" -- no husk-specific semantic claim rests on them
yet, unlike `nGroups`/`numLod` which gate real array-length math elsewhere
in this document.

**C++ data-model sketch** (mirrors `src/phys.hpp`'s idiom -- flat
descriptor structs, chunk-tag-dispatch parse function, real bounds-checked
reads):

```cpp
// src/wmo.hpp
namespace husk::wmo {

struct Vec3 { float x = 0, y = 0, z = 0; };
struct AaBox { Vec3 min, max; };

struct RootHeader {
    uint32_t nTextures = 0, nGroups = 0, nPortals = 0, nLights = 0;
    uint32_t nDoodadNames = 0, nDoodadDefs = 0, nDoodadSets = 0;
    uint32_t ambColor = 0;
    uint32_t wmoAreaTableId = 0;
    AaBox boundingBox;
    uint16_t flags = 0;
    uint16_t numLod = 0;  // 0 on pre-Legion files -- GFID absent/1-entry, see GFID section
};

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Parses a complete root .wmo file already read into memory. Throws
// ParseError if MVER/MOHD are missing or MOHD is short of 64 bytes.
struct RootFile {
    uint32_t version = 0;
    RootHeader header;
    // ... MOTX/MOMT/MOGN/MOGI/GFID/MOSB/MOSI -- see their own sections below
};
RootFile parseRoot(const std::vector<uint8_t>& fileBytes);

}  // namespace husk::wmo
```

**Group file's `MOGP` header is NOT a top-level chunk** -- it's a 68-byte
header immediately followed by every other chunk *as MOGP's own sub-chunk
payload* (WMO.md's own "IMPORTANT: This chunk contains all other chunks!").
Confirmed directly: `world/wmo/cameron_000.wmo` (38,884 bytes) has exactly
two top-level chunks, `MVER` and one `MOGP`, and every other real chunk
(`MOPY`/`MOVI`/`MOVT`/`MONR`/`MOTV`/`MOBA`/`MOBN`/`MOBR`/`MOCV`) lives inside
`MOGP`'s own payload, starting at byte offset `0x44` (68) relative to that
payload's start -- re-walking `mogpPayload[0x44:]` with the same
`husk::readChunks` produces exactly those 9 sub-chunks in the wiki's own
documented fixed order. A husk parser needs a **nested** chunk walk for
group files, not a flat one: `readChunks(fileBytes)` for the top level, then
`readChunks(mogpChunk.data + 0x44, mogpChunk.size - 0x44)` for the group's
real content.

```cpp
// One group file's fixed 68-byte MOGP header (offsets per WMO.md line
// 1059's struct -- verified byte-for-byte against cameron_000.wmo and
// 9du_torghast_modular_chambercap01_000.wmo).
struct GroupHeader {
    uint32_t groupNameOffset = 0;         // 0x00, into root's MOGN
    uint32_t descriptiveGroupNameOffset = 0;  // 0x04
    uint32_t flags = 0;                   // 0x08
    AaBox boundingBox;                    // 0x0C
    uint16_t portalStart = 0, portalCount = 0;  // 0x24, 0x26
    uint16_t transBatchCount = 0, intBatchCount = 0, extBatchCount = 0;  // 0x28,0x2A,0x2C
    uint8_t fogIds[4] = {};               // 0x30
    uint32_t groupLiquid = 0;             // 0x34
    uint32_t uniqueId = 0;                // 0x38 (WMOAreaTable.dbc foreign key)
    uint32_t flags2 = 0;                  // 0x3C
    int16_t parentOrFirstChildSplitGroupIndex = 0, nextSplitChildGroupIndex = 0;  // 0x40, 0x42
};  // 0x44 = 68 bytes total, matches every real file checked
```

**Recommendation**: `parse` / `diagnostic` (via `husk info`-equivalent) /
`n/a, infrastructure` -- same as `M2_COMPLETENESS.md`'s own `MD20`/header row.
Drives every other section's parsing; no renderable shape of its own.

**Open design question**: none -- this is the least ambiguous section in
this document, fully nailed down by real bytes.

## Root+group file split, group resolution (`MOGN`, `MOGI`, `GFID`)

**Current state**: `none`.

**Wiki citation**: WMO.md `## MOGN chunk` (line 373), `## MOGI chunk` (line
385), `## GFID` (line 707).

**Verified struct**:

```
MOGN: a contiguous block of zero-terminated strings (windows-1252, almost
      always plain ASCII per real corpus data). Referenced by byte offset
      from MOGI's own nameoffset field and from each group file's own MOGP
      header (groupNameOffset/descriptiveGroupNameOffset).

MOGI (32 bytes/group, MOHD.nGroups entries):
offset  field         type
0x00    flags         uint32 (same bit meanings as MOGP's own group flags)
0x04    bounding_box  CAaBox (24 bytes)
0x1C    nameoffset    int32 (-1 = no name; else byte offset into MOGN)

GFID (Legion+ only, absent on older files):
  uint32_t groupFileDataIDs[nGroups * rowCount];
  rowCount = !flag_lod ? 1 : (numLod ? numLod : 3)
  -- row-major: index = lodTier * nGroups + groupIndex, 0 = "no file for
     this (lodTier, group) pair" (see real-data verification below -- this
     row-major layout is NOT spelled out explicitly on the wiki page, only
     the total array size formula is).
```

**Real-data verification**:
- `world/wmo/cameron.wmo`: `nGroups=1`, `flag_lod` unset (`flags=0x0000`),
  `GFID` present with exactly 1 entry (`108237`) -- matches the `!flag_lod ?
  1` branch of the size formula. `MOGN` holds `"testbox"` plus 4 padding/
  empty strings (matches WMO.md's own "not always nGroups entries... extra
  empty strings" caveat); `MOGI`'s one entry has `nameoffset=-1` (no name for
  this particular group, real and legal per the wiki).
- **The GFID row-major layout, confirmed via a real multi-LOD case**:
  `world/wmo/dungeon/torghast/9du_torghast_modular_chambercap01.wmo` has
  `nGroups=3`, `numLod=4`, `flag_lod` set -- `GFID` is exactly 12 entries
  (`4*3`), matching `(2991660, 3171241, 3574067, 3171239, 3171242, 0,
  3171240, 3171243, 0, 0, 3544636, 0)`. Resolving every nonzero entry through
  the community listfile (`/media/luna/work/tinker/dev/wow_modding/m2mod/
  mappings/listfile.csv`, read-only reference, not husk's own data) and
  checking against the local corpus confirms **all 8 nonzero entries** land
  on real, existing files whose names exactly match the row-major
  hypothesis: row 0 (base) = `..._000.wmo`/`..._001.wmo`/`..._002.wmo`; row 1
  (lod1) = `..._000_lod1.wmo`/`..._001_lod1.wmo`/(0, group 2 has no lod1);
  row 2 (lod2) = `..._000_lod2.wmo`/`..._001_lod2.wmo`/(0); row 3 (lod3) =
  (0, group 0 has no lod3)/`..._001_lod3.wmo`/(0). This exactly matches
  which group-LOD files actually exist on disk for this WMO (verified via a
  plain directory listing) -- a zero entry means "this LOD tier genuinely
  has no file for this group," not a parse gap.
- **Local-directory naming convention differs from the real FileDataID/CASC
  path**: the local pre-extracted corpus names group files
  `<rootBasename>_<NNN>[_lod<N>].wmo` (matching the root file's own basename,
  a `wow.export`-style convention), *not* `<FileDataID>.wmo` the way M2
  sidecars (`.skin`/`.anim`/`.bone`/`.phys`) resolve in husk today. This
  session confirmed the naming pattern empirically (`cameron.wmo` +
  `cameron_000.wmo`; `9du_torghast_modular_chambercap01.wmo` +
  `_000`/`_001`/`_002[_lodN]` siblings) -- a real, different resolution
  convention from every M2 sidecar husk already implements, flagged as an
  open design question below.

**C++ data-model sketch**:

```cpp
struct GroupInfo {
    uint32_t flags = 0;
    AaBox boundingBox;
    int32_t nameOffset = -1;  // -1 = no name; else byte offset into MOGN
};

struct RootFile {
    // ... header (above) ...
    std::string groupNames;              // raw MOGN blob, zero-terminated runs
    std::vector<GroupInfo> groupInfos;    // MOGI, header.nGroups entries
    // Row-major [lodTier][groupIndex], row count per the formula above;
    // empty if GFID is absent (pre-Legion file -- group resolution then
    // has no FileDataID path at all, see the open design question below).
    std::vector<uint32_t> groupFileDataIds;
    size_t groupFileDataIdRowCount = 0;   // 0, 1, or numLod
};

// Returns groupFileDataIds[lodTier * header.nGroups + groupIndex], or 0 if
// out of range for groupFileDataIdRowCount/nGroups -- 0 means "no real file
// for this (lodTier, group) pair," matching GFID's own real-data semantics
// above (not a parse error).
uint32_t resolveGroupFileDataId(const RootFile& root, uint32_t lodTier, uint32_t groupIndex);
```

**Recommendation**: `full` (struct-level) / `diagnostic` initially (`husk
info`-equivalent: "N groups, M with a real GFID entry") / `n/a,
infrastructure` -- resolving a group file is what makes every other section
below possible, not a renderable concept on its own.

**Open design question, flagged for a human decision, not silently
resolved**: husk's existing M2 sidecars (`--skin-dir`/`--textures`/`--anim`/
`--bones-dir`/`--phys`) all resolve `<dir>/<FileDataID>.<ext>` by convention
-- but this session's real corpus uses `<rootBasename>_<NNN>[_lodN].wmo`
instead, matching `wow.export`'s own on-disk naming rather than a raw
FileDataID. A real WMO importer needs **both** paths supported (a user might
have either a FileDataID-named extraction or a `wow.export`-style one,
exactly like husk already treats `.skin` files' "same-basename fallback,"
see `CLAUDE.md`'s own `ANIM_TODO.md` history for the M2 precedent) -- which
one is primary, and whether `GFID`'s row-major LOD indexing should surface
as its own `--wmo-lod` flag mirroring `husk export --lod` are both real,
undecided questions a future implementation session needs to settle before
writing `resolveGroupFile`, not something this document should guess at.

## Mesh geometry (`MOVT`/`MOVI`/`MOBA`, `MORI`/`MORB` variant)

**Current state**: `none`.

**Wiki citation**: WMO.md `## MOVT chunk` (line 1328), `## MOVI chunk` (line
1312), `## MOVX chunk` (line ~1324 -- uint32 index variant, not verified
against a real file this session, see below), `## MOBA chunk` (line 1380),
`## MORI`/`## MORB` (line 2079/2083).

**Verified struct**:

```
MOVT: count = size/12, 3x float32 per vertex, (X,Z,-Y) order on disk (WMO's
      own native coordinate convention -- NOT the same axis order M2 uses,
      see the Coordinate System Translation table in ADT/v18.md, cited in
      WORLD_PLACEMENT_TODO.md, for the full cross-format comparison).

MOVI: count = size/2, uint16 triangle-corner indices into MOVT/MONR/MOTV,
      three per triangle. Real-file note: right-handed winding on disk (WMO.md's
      own text) -- a glTF exporter (right-handed by convention) can use
      MOVI's index order directly, unlike some legacy-format conventions
      that need a 2nd/3rd-index swap.

MOBA (24 bytes/batch, Legion+ layout -- pre-Legion has a different, smaller
      int16-bounding-box-based layout, not verified against a real file this
      session, see below):
offset  field                  type
0x00    unknown[0xA]           uint8[10]  (pre-Legion: bx,by,bz,tx,ty,tz int16 culling box)
0x0A    material_id_large      uint16     -- used when flag_use_material_id_large set (see below)
0x0C    startIndex             uint32     -- first MOVI index this batch uses
0x10    count                  uint16     -- number of MOVI indices
0x12    minIndex               uint16     -- first MOVT vertex used
0x14    maxIndex               uint16     -- last MOVT vertex used (inclusive)
0x16    flag_unknown_1:1, flag_use_material_id_large:1 (bitfield, same byte)
0x17    material_id            uint8      -- index into MOMT, used UNLESS flag_use_material_id_large set
```

**Real-data verification**:
- `world/wmo/cameron_000.wmo` (smallest real group, 1 batch, 154 vertices/
  236 triangles): `MOVI` has exactly `236*3=708` uint16 indices, all in
  range for 154 `MOVT` vertices; `MOBA` has 1 batch, `startIndex=0
  count=672` -- wait, real value is `count=672` for 236 triangles (`236*3 =
  708`, not 672) -- **the single batch's `count` (672) is less than the
  chunk's total index count (708)**, i.e. `MOBA` batches don't have to
  cover every `MOVI` index (consistent with WMO.md's own text: "the vertex
  buffer... can contain vertices that aren't used by the batch"). This is
  real, not a parser bug -- a husk implementation must draw exactly
  `startIndex..startIndex+count` per batch, never assume batches partition
  the whole `MOVI` array.
- **`material_id_large` is load-bearing on real files, not a rare edge
  case**: `world/wmo/dungeon/torghast/9du_torghast_modular_
  chambercap01_000.wmo` has 9 real batches, **every one** with
  `flag_use_material_id_large` set (`flagByte=0x02` on all 9). The plain
  `material_id` (uint8, offset 0x17) is **uniformly 0** for all 9 batches --
  reading it without checking the flag would silently render every batch
  with material 0. `material_id_large` (uint16, offset 0x0A) decodes to
  `{2,3,4,6,8,9,1,5,7}` -- all in range for the file's real 14 `MOMT`
  materials, and each batch's material differs from its neighbors (a real,
  varied per-batch material assignment, not degenerate). **A husk parser
  must check the flag bit and pick the right field, or every Legion+-era
  WMO group with more than ~256 materials worth of batch variety (or simply
  one that happens to set this bit, which real content does even well under
  256) silently renders with the wrong texture on every batch.**
- `MORI`/`MORB` (the triangle-strip variant, Cata+): **zero real hits**
  found in this session's corpus sampling (checked every group file loaded
  during this session's other verification passes, plus a byte-signature
  grep across `world/wmo/dungeon/torghast/`'s files) -- not confirmed absent
  corpus-wide via an exhaustive sweep (that would need a dedicated scan
  session didn't run, given the time budget), but no real example was found
  to verify field-level layout against. Flagged unverified, same "wiki
  offsets transcribed, never confirmed real" disposition `src/phys.hpp`
  already uses for `BOXS`/`SPHJ`/etc.
- `MOVX` (uint32 index variant, "spotted in 9.0"): **not checked this
  session at all** -- no real file was found carrying it during this
  session's sampling, and no dedicated scan was run to look specifically.
  Flagged as a genuine open item, not a confirmed absence.

**C++ data-model sketch**:

```cpp
struct Batch {
    uint16_t materialIdLarge = 0;   // offset 0x0A -- only meaningful if useMaterialIdLarge
    uint32_t startIndex = 0;
    uint16_t count = 0;
    uint16_t minIndex = 0, maxIndex = 0;
    bool useMaterialIdLarge = false;  // flag bit 0x02 at offset 0x16
    uint8_t materialId = 0;           // offset 0x17 -- ignored if useMaterialIdLarge
    // Real per-batch material index -- picks materialIdLarge or materialId
    // per the flag, the one piece of logic every consumer needs and
    // shouldn't reimplement per-callsite.
    uint32_t resolvedMaterialId() const {
        return useMaterialIdLarge ? materialIdLarge : materialId;
    }
};

struct GroupMesh {
    std::vector<Vec3> positions;   // MOVT, raw on-disk (X,Z,-Y) order
    std::vector<uint16_t> indices; // MOVI, flat triangle-corner list
    std::vector<Batch> batches;    // MOBA
    // MORI/MORB (triangle-strip variant) -- unverified against real bytes
    // this session, see doc comment above. Parse defensively (bounds-check
    // same as everything else) but flag with the same "verified floor"
    // warning kMinVerifiedParticleVersion-style pattern src/m2.hpp already
    // uses, since no real file confirmed the exact byte layout.
};
```

**Recommendation**: `full` / `native` / `native-possible, not done` -- a
straightforward one-primitive-per-`MOBA`-batch glTF mesh (positions/indices
sliced by `startIndex`/`count`, one glTF material per batch via
`resolvedMaterialId()`), no bone weights (WMO has no skeleton) -- structurally
closer to husk's own existing `gltf::NamedMesh` **collision**-mesh handling
(`src/cmd_export.cpp`'s collision-mesh block, `mesh.skinning` left empty)
than to its skinned M2 mesh path. `gltf::Primitive::materialIndex` already
exists for exactly this per-batch-material shape.

**Open design question**: none for the core mesh path. `MORI`/`MORB`/`MOVX`
being unverified is a real data gap, not a design question -- flagged for a
future session to find real corpus hits before implementing, same
"verified floor, don't guess" policy as `.phys`'s never-observed shape
variants.

## Normals (`MONR`)

**Current state**: `none`.

**Wiki citation**: WMO.md `## MONR chunk` (line 1334).

**Verified struct**: `count = size/12`, 3x float32 per normal, same
`(X,Z,-Y)` on-disk order as `MOVT`, one-to-one with `MOVT`'s own vertex
order.

**Real-data verification**: `world/wmo/cameron_000.wmo` has 154 `MONR`
entries, exactly matching `MOVT`'s own 154 vertices -- confirmed on every
group file this session decoded (paired counts, never mismatched).

**C++ data-model sketch**: `std::vector<Vec3> normals;` on `GroupMesh`
(above), same shape/order as `positions`.

**Recommendation**: `full` / `native` / `native-possible, not done` -- direct
`NORMAL` accessor, no conversion beyond the shared coordinate-system flip
every position/normal in this document needs (see the Coordinate system
note below).

## UV / texture coordinates (`MOTV`)

**Current state**: `none`.

**Wiki citation**: WMO.md `## MOTV chunk` (line 1340).

**Verified struct**: `count = size/8`, 2x float32 per vertex, `(X,Y)` order,
one-to-one with `MOVT`. **Real, group-flag-gated multiplicity**: `MOGP`
group flags `0x2000000` (`TVERTS2`) and `0x40000000` (`TVERTS3`) mean a
group has 2 or 3 separate `MOTV` chunks (used by two-layer/`MOM3` shader
setups), matching the fixed sub-chunk order WMO.md documents (`MOTV` appears
twice in the group file's own chunk-order list, once early and once late,
see the group file's own "always present in this order" list, WMO.md line
1020-1050).

**Real-data verification**: every real group file this session decoded had
exactly one `MOTV` chunk with a count matching `MOVT`'s own vertex count
(`world/wmo/cameron_000.wmo`: 154/154; `world/wmo/dungeon/torghast/
9du_torghast_modular_chambercap01_000.wmo`: 1348/1348). No real file with
`TVERTS2`/`TVERTS3` set was found and decoded this session -- flagged
unverified, same disposition as `MORI`/`MORB` above. The wiki's own
"client bug" note (multiple `MOTV` chunks loaded into a fixed 3-entry array,
counter overwrite risk past 3) is transcribed as-is, not independently
re-derived.

**C++ data-model sketch**: `std::vector<std::vector<Vec2>> texCoordSets;`
on `GroupMesh` (0, 1, or up to 3 entries depending on the `TVERTS2`/
`TVERTS3` flags) -- mirrors `gltf::Mesh::texCoords`/`texCoords2`'s existing
two-slot shape, but needs a third slot (`texCoords3`?) if `TVERTS3` content
is ever confirmed real, an open extension `gltf.hpp` doesn't have yet.

**Recommendation**: `full` / `native` / `native-possible, not done` -- direct
`TEXCOORD_0`(/`_1`/`_2`) accessors, same shape M2's own dual-UV-set handling
already established (`gltf::Mesh::texCoords`/`texCoords2`).

**Open design question**: does `gltf.hpp` need a `texCoords3` slot, or is
`TVERTS3` rare enough (only cited for `MOM3`/shader-18 WMOs, a narrow real
case) to defer until a real 3-UV-set file is found? Flagged, not decided --
depends on how common shader 18 (`TwoLayerDiffuseMod2x`) turns out to be in
practice.

## Tangents (`MOTA`)

**Current state**: `none`.

**Wiki citation**: WMO.md `## MOTA chunk` (line 2104).

**Verified struct**: per-batch `first_index` (uint16, into a flat tangent
array) plus a flat `C4Vector tangents[]` array, populated only for batches
whose material uses shader 10 or 14, and explicitly documented as
**client-auto-generated** when absent ("Is auto generated, if there are
batches with shaders 10 or 14, but no tangents").

**Real-data verification**: not independently checked this session (no
shader-10/14 real file was sampled) -- but the recommendation doesn't depend
on verifying it, since this is the same "runtime-computed, not authored"
situation `M2_COMPLETENESS.md`'s own Tangents row already documents for M2
(`n/a` -- "not in the documented base header at all").

**Recommendation**: `n/a` / `n/a` / `n/a` -- glTF's own tangent accessor
exists (`TANGENT`), but WMO's own tangents are runtime-synthesized from
normals+UVs by the client shader compiler in the common case, not stable
authored data worth extracting even when present. Same treatment M2 already
gets; not planned for implementation.

## Vertex colors / baked lighting (`MOCV`/`MOC2`)

**Current state**: `none`.

**Wiki citation**: WMO.md `## MOCV chunk` (line 1678), `### CMapObjGroup
::FixColorVertexAlpha` (line 1692), `## MOC2 chunk` (line 1904).

**Verified struct**: `MOCV`/`MOC2` are both `count = size/4`, `CImVector`
(BGRA byte order) per vertex, one-to-one with `MOVT`. A group can have 0, 1,
or 2 `MOCV` chunks (gated by `MOGP` flags `0x4` and `0x1000000`/`CVERTS2`),
plus an independent `MOC2` (used for `Parallax`/shader-23 math per the
wiki, not lighting).

**Real-data verification**: `world/wmo/cameron_000.wmo`'s single `MOCV`
chunk decodes to 154 real BGRA entries, count matching `MOVT`; sampled
values are plausible mid-range colors (e.g. `(110,147,144,0)`), not garbage.
**`FixColorVertexAlpha`'s decompiled fixup logic itself was not
independently re-derived or re-verified this session** -- the wiki page
already includes real decompiled client code (WMO.md lines 1727-1799,
build 18179) for this, cited and trusted as-is per this document's policy
(wiki structs get verified against real bytes; wiki-provided *decompiled
client logic* is a different class of claim this session didn't attempt to
re-derive independently, same as `PHYS.md`'s own decompiled snippets
`src/phys.cpp`'s doc comments already cite without re-deriving).

**C++ data-model sketch**:

```cpp
struct Color4b { uint8_t b = 0, g = 0, r = 0, a = 0; };  // wire order, BGRA

struct GroupMesh {
    // ... positions/indices/batches/normals/texCoordSets above ...
    std::vector<Color4b> vertexColors;   // first MOCV, empty if absent
    std::vector<Color4b> vertexColors2;  // second MOCV (CVERTS2 flag), empty if absent
    std::vector<Color4b> mocv2;          // MOC2 (parallax weight data, NOT lighting)
};
```

**Recommendation**: `full` / `native` / `native-possible, not done` -- glTF's
`COLOR_0` accessor is a direct, well-supported target (Blender imports
`COLOR_0` as a vertex-color-attribute layer with zero extra tooling). The
`FixColorVertexAlpha`/`AttenTransVerts` shader-side fixup math is a real
open question (see below) -- exporting the **raw** `MOCV` bytes into
`COLOR_0` is unambiguous and matches this project's own "expose the data,
don't guess at derived semantics" policy; baking the fixup in is a separate,
bigger decision.

**Open design question, flagged for a human decision**: should husk apply
`FixColorVertexAlpha`/`AttenTransVerts`'s math before writing `COLOR_0`, or
export the raw bytes and note in `extras` whether the fixup would normally
apply (client version + `MOHD`/`MOGP` flag-dependent, real logic, not
guesswork -- but baking derived, version-conditional math into an exported
asset is a bigger step than every other translation in this document, which
all export source bytes as directly as glTF allows)? The wiki's own
decompiled code is real and detailed enough to implement either way -- this
is a scope/fidelity decision, not a missing-information one.

## Per-face material info (`MOPY`/`MPY2`) -- materials half only

**Current state**: `none`.

**Wiki citation**: WMO.md `## MOPY chunk` (line 1258), `## MPY2 chunk` (line
1294).

**Verified struct**:

```
MOPY (2 bytes/triangle, count = size/2):
  byte 0: flags bitfield (F_UNK_0x01, F_NOCAMCOLLIDE, F_DETAIL, F_COLLISION,
          F_HINT, F_RENDER, F_CULL_OBJECTS, F_COLLIDE_HIT)
  byte 1: material_id (uint8 index into MOMT, 0xFF = collision-only face)

MPY2 (4 bytes/triangle, DF+ replacement):
  uint16 flags; uint16 materialId;  -- same semantics, wider fields
```

**This document's own scope boundary**: the *collision*-relevant bits
(`F_NOCAMCOLLIDE`/`F_COLLISION`/`F_HINT`/`F_CULL_OBJECTS`/`F_COLLIDE_HIT`)
belong to `COLLISION_CULLING_TODO.md`, not here -- this section covers only
`material_id`/`materialId`, the per-triangle material-index half, and the
`0xFF`/sentinel "collision-only, not rendered" case as it affects **which
triangles a render mesh should include at all** (a materials/geometry
concern: a collision-only face shouldn't get a render-mesh triangle,
regardless of which document tracks the collision-mesh side of the same
data).

**Real-data verification**: `world/wmo/cameron_000.wmo`'s 236 real `MOPY`
entries decode to `flags=0x64` uniformly (a plausible real combination:
`F_DETAIL|F_RENDER|F_CULL_OBJECTS`, i.e. `0x04|0x20|0x40`) and
`material_id=0`, in range for the file's single real material. `MPY2`: not
found in this session's sampling (all sampled files are old enough not to
carry it, or simply didn't happen to) -- flagged unverified same as
`MORI`/`MORB`.

**C++ data-model sketch**:

```cpp
struct TriangleMaterial {
    uint16_t flags = 0;       // MOPY: 8 real bits; MPY2: 16, upper 8 unverified
    uint16_t materialId = 0;  // 0xFF (MOPY) or 0xFFFF (MPY2?) == collision-only
    bool isCollisionOnly() const { return materialId == 0xFF; }  // MPY2's own sentinel unverified
};
// GroupMesh::triangleMaterials, one entry per MOVI/3 triangle -- feeds both
// per-batch primitive assembly (cross-checked against MOBA's own
// resolvedMaterialId(), see the Mesh geometry section) and the
// render/collision-only triangle split.
```

**Recommendation**: `full` / `native` (via `Primitive::materialIndex`, same
mechanism M2's own per-batch material index already uses) / `native — 100%`
once implemented -- this is the same "index picks a material" relationship
`gltf::Primitive::materialIndex` already models for M2, no new glTF concept
needed. Note MOBA's own `resolvedMaterialId()` is very likely the primary
signal a real exporter should key primitives on (a batch already groups
triangles by material for rendering); `MOPY`/`MPY2`'s per-triangle
`material_id` is a redundant/cross-checking signal in the common case, worth
confirming they always agree per-batch before trusting one over the other
in an implementation (not yet cross-checked this session).

## Materials (`MOMT`/`MOM3`/`MOTX`)

**Current state**: `none`.

**Wiki citation**: WMO.md `## MOMT chunk` (line 125), `### Shader types`
subsections (194-336), `## MOM3` (line 337), `## MOTX chunk` (line 102).

**Verified struct** (`MOMT`, 64 bytes/material):

```
offset  field           type      note
0x00    flags           uint32    F_UNLIT/F_UNFOGGED/F_UNCULLED/F_EXTLIGHT/
                                   F_SIDN/F_WINDOW/F_CLAMP_S/F_CLAMP_T
0x04    shader          uint32    index into the shader-type table (version-dependent)
0x08    blendMode       uint32    EGxBlend (see Rendering.md#EGxBlend)
0x0C    texture_1       uint32    MOTX byte offset (pre-8.1) OR direct FileDataID (8.1+)
0x10    sidnColor       CImVector
0x14    frameSidnColor  CImVector (runtime-computed, not authored)
0x18    texture_2       uint32    same MOTX-offset-or-FileDataID duality
0x1C    diffColor       CImVector
0x20    ground_type     uint32    TerrainType.dbc foreign key
0x24    texture_3       uint32    same duality (post-0.6.0.3592 layout only)
0x28    color_2         uint32    shader-23: can be a texture FileDataID
0x2C    flags_2         uint32    shader-23: can be a texture FileDataID
0x30    runTimeData[4]  uint32[4] explicitly nulled at load, shader-23 textures
```

`MOTX` presence itself is the version signal (wiki: "To check if texture
references in MOMT are file data ids, simply check if MOTX exists in the
file") -- absent means `texture_1`/`_2`/`_3` are raw FileDataIDs directly,
present means they're byte offsets into `MOTX`'s own zero-terminated-string
blob.

`MOM3` (War Within 11.0+): wholly replaces `MOMT` when present (client uses
`MOM3`'s materials instead, not alongside) -- an `m3SI`-tagged blob, same
shape as an M3 file's own `M3SI` chunk (`M3.md#M3SI` -- out of this
project's current scope, M3 itself is a non-goal per `WORLD_COMPLETENESS.md`'s
own "Related, explicitly out of scope" section).

**Real-data verification**:
- `world/wmo/cameron.wmo`: 1 material, `tex1=130069`, **no `MOTX` chunk at
  all** -- confirms direct-FileDataID mode for this file.
- **Corpus-wide, both `MOTX` and `MOM3` are real-zero in this local
  corpus**: a full scan of all 12,867 real WMO root files found under
  `world/wmo/` (identified via a `_NNN[_lodN].wmo` basename-suffix filter to
  exclude group files -- see the root/group-split section above) found
  **zero** files with a `MOTX` chunk and **zero** with `MOM3`. Every real
  root file in this corpus uses direct-FileDataID `MOMT` texture references
  exclusively. This is a genuinely useful negative result (not a scanner
  bug -- same "checked, zero real hits" disposition `WIKI_FINDINGS.md`'s own
  prior sessions have used for other tags): `MOTX`/`MOM3` parsing can be
  built defensively (the byte layout is simple enough either way) but
  neither is verified against a single real file this session, and `MOM3`
  in particular (a whole M3-shaped sub-format) shouldn't be prioritized
  until a real hit surfaces.
- **`material_id_large`'s real-file range** (see the Mesh geometry section
  above) cross-checks cleanly against a real 14-material `MOMT` array on
  the same file (`9du_torghast_modular_chambercap01.wmo`) -- every observed
  `material_id_large` value (1-9) is in range.

**C++ data-model sketch**:

```cpp
struct Material {
    uint32_t flags = 0;
    uint32_t shader = 0;
    uint32_t blendMode = 0;
    // Resolved, not raw -- true FileDataID when MOTX is absent; when MOTX
    // is present, the caller must have already resolved the MOTX byte
    // offset to a filename (husk has no CASC/listfile access, so a
    // MOTX-only WMO's texture slots would need a --textures-dir-style
    // filename match, same non-goal boundary DESIGN.md's Non-goals already
    // draws for M2). 0 == no texture in this slot.
    uint32_t texture1FileDataId = 0, texture2FileDataId = 0, texture3FileDataId = 0;
    uint32_t groundType = 0;  // TerrainType.dbc id, diagnostic-only (no local DBC access)
};

struct RootFile {
    // ... header/groupNames/groupInfos/groupFileDataIds above ...
    std::string motxBlob;         // raw MOTX bytes if present, empty otherwise
    std::vector<Material> materials;  // MOMT, or resolved from MOM3 if present (unimplemented -- see below)
};
```

**Recommendation**: `full` (MOMT path) / `deref` (MOM3, unimplemented --
zero real files to verify against) / `native` (base material: blend mode →
`alphaMode`, `F_UNCULLED` → `doubleSided`, `F_UNLIT` → `KHR_materials_
unlit`, textures → `baseColorTexture` via `--textures <dir>`, all directly
mirroring M2's own already-shipped translation in `gltf::Material`) /
`native — 100%` for the base material once implemented, matching
`M2_COMPLETENESS.md`'s own "Base material" row. Shader-driven multi-texture
blending (shaders needing 2-3 textures, `### Shader types` tables) has the
same `extras`-only ceiling M2's own multi-texture-layer row already
documents (`gltf::Material::additionalTextureLayers` -- no core-glTF slot
for WoW's fixed-function combiner math either way).

**Open design question**: should `MOM3` block implementation at all until a
real file is found (same "don't implement blind" policy `RO_COMPLETENESS_TODO.md`
Item 1 already applied to BLP2 DXT3/JPEG), or is the struct simple/stable
enough (a bare M3SI passthrough, out of scope to decode further per the M3
non-goal) to implement as an inert byte-passthrough regardless? Leaning
toward the former (this document's own "verify before shipping" discipline)
but flagged, not decided.

## Animated texture UV (`MOUV`)

**Current state**: `none`.

**Wiki citation**: WMO.md `## MOUV` (line 351).

**Verified struct**: `count(materials)` entries, 16 bytes each --
`C2Vector translation_speed[2]` (two independently-animatable texture
layers per material). Absent means "all zero" for every material (wiki:
"repeating those zeros for materials not using any transformation").

**Real-data verification**: found in 138 of the same 12,867 real root files
(a real, non-trivial minority, not a corpus-wide zero like `MOTX`/`MOM3`
above). Decoded `world/wmo/brokenisles/7xp_sargerassword.wmo` in full: 22
materials, `MOUV` chunk is exactly `22*16 = 352` bytes (count matches
material count exactly, confirming the wiki's "same count as materials"
claim on a real file) -- 9 of the 22 entries have real nonzero
`translation_speed` values (e.g. `(0.0, 0.035)`/`(0.0, 0.0)`, plausible
scroll speeds), the other 13 are exactly zero. Confirms both the struct
layout and the "absent/zero == no animation" semantics on real data.

**C++ data-model sketch**:

```cpp
struct Vec2 { float x = 0, y = 0; };

struct TextureUvAnimation {
    Vec2 translationSpeed[2];  // two independently animated layers
};
// RootFile::textureUvAnimations -- empty if MOUV absent (all-zero default),
// else exactly materials.size() entries, index-parallel with `materials`.
std::vector<TextureUvAnimation> textureUvAnimations;
```

**Recommendation**: `full` / `extras` / `extras-capped, permanent` -- same
wall M2's own animated-texture-transform row already hits
(`M2_COMPLETENESS.md`'s Materials & textures section): `KHR_texture_transform`
has no animation-channel target, so this is inert `extras` data
(`texture_uv_animation`, mirroring `gltf::Material::textureTransform`'s
existing shape) for a custom renderer/Blender script, never a real glTF
animation clip.

## Skybox (`MOSB`/`MOSI`)

**Current state**: `none`.

**Wiki citation**: WMO.md `## MOSB chunk` (line 405, marked
`wowdev-unverified` on the wiki itself), `## MOSI (optional)` (line 411).

**Verified struct**: `MOSB` is a zero-terminated skybox M2 filename
(padded to 4-byte alignment), first-byte-zero meaning "no skybox, clear the
skybox flag on every `MOGI` entry." `MOSI` (BfA+) is the FileDataID
equivalent, "client supports reading both for now."

**Real-data verification**: `world/wmo/cameron.wmo`'s `MOSB` chunk is
present but empty (4 padding-only bytes, first byte 0) -- confirms the
"no skybox" empty-string case on a real file. No real file with a genuinely
populated `MOSB` or a real `MOSI` was found/decoded this session (this
document's corpus sampling skewed toward small/simple fixtures for the
mesh/material sections above, not skybox-bearing large outdoor zones) --
flagged unverified for the *populated* case, though the struct itself
(a filename or a FileDataID, matching every other husk sidecar-resolution
precedent) is about as simple as this document covers.

**C++ data-model sketch**: `uint32_t skyboxFileDataId = 0; std::string
skyboxFileName;` on `RootFile` -- resolution mirrors M2's own `--textures`-
style FileDataID lookup once implemented, no new pattern needed.

**Recommendation**: `full` / `diagnostic` initially, `native-possible, not
done` for the ceiling -- structurally trivial once M2 placement/embedding
exists elsewhere in husk (it's just another M2 reference + a boolean
per-group flag), matching `WORLD_COMPLETENESS.md`'s own note verbatim.

## Coordinate system note (applies to every position/normal above)

WMO's own on-disk convention is `(X,Z,-Y)` per `MOVT`/`MONR`'s wiki text --
**not** the same axis order M2 uses (`(X,-Z,Y)`, per `gltf::zUpToYUp`'s own
doc comment). Both conventions land on the same real-world "Z-up, WMO-local-
space" coordinate system; the wire *order* the three floats are stored in
differs between the two formats. A `gltf::zUpToYUp`-equivalent for WMO
vertices needs its own conversion function (`wmoZUpToYUp`?), not a reuse of
M2's -- this session confirmed the wiki's textual claim but did not
independently re-derive it against real geometry (e.g. by comparing a WMO's
placed-in-world position against a known landmark) -- flagged as verified-
by-citation, not verified-by-independent-derivation, unlike the byte-layout
claims above.

## Test plan

**Synthetic fixtures** (mirroring `tests/test_phys.cpp`'s own style --
hand-built minimal chunk buffers, one per parser unit):
- `MVER`/`MOHD` happy path + truncated-header throw.
- `GFID` row-major resolution: a 2-group/3-numLod synthetic file with a
  deliberate zero hole, asserting `resolveGroupFileDataId` returns 0 for the
  hole and the right value for every populated cell.
- `MOBA`'s `resolvedMaterialId()`: both flag states (plain `material_id` and
  `material_id_large`), asserting the right field wins.
- `MOPY`/`MPY2` sentinel handling (`0xFF`/collision-only).
- Nested `MOGP` sub-chunk walk: a synthetic group file proving the `0x44`-
  offset re-walk finds every sub-chunk, including a deliberately out-of-
  wiki-order chunk (should this throw, or just parse tolerantly? -- an open
  question the synthetic test should pin down one way, not leave ambiguous).

**Real-data fixtures** (candidate paths, all already confirmed to exist and
decode cleanly this session -- copy into `test_data/` the same gitignored-
personal-extraction way every other real husk fixture already does, see
`CLAUDE.md`'s own conventions):
- `world/wmo/cameron.wmo` + `world/wmo/cameron_000.wmo` -- smallest real
  root+group pair (392 + 38,884 bytes), single group, single material, no
  `GFID` LOD complexity, no doodad sets beyond the empty default. The
  simplest possible "does the whole pipeline work at all" fixture.
- `world/wmo/azeroth/buildings/guardtower/guardtower_damaged.wmo` (+ its
  `_000.wmo` group, 1,152-byte root) -- real multi-doodad-set case (4 sets:
  `Set_$DefaultGlobal`/`Set_Damaged_pieces`/`Set_Damaged_ambient`/
  `Set_Damaged_hit`), small enough to hand-verify every field. Belongs to
  `WORLD_PLACEMENT_TODO.md`'s test plan too (shared fixture, this document's
  own use is materials/`MODI`-count-mismatch verification only).
- `world/wmo/dungeon/torghast/9du_torghast_modular_chambercap01.wmo` (+ its
  3 group files + LOD siblings) -- the GFID row-major/`material_id_large`/
  37-doodad-set fixture this session's verification leaned on most heavily.
  17,872-byte root, largest group file ~88KB -- still small enough to commit.
- `world/wmo/brokenisles/7xp_sargerassword.wmo` -- real, populated `MOUV`
  fixture (22 materials, 9 with nonzero animation).

**Real-data assertions to write** (exact values, not loose bounds, per this
project's own "don't just assert `> 100`" discipline, see `CLAUDE.md`'s
`ANIM_TODO.md` history): `cameron`'s exact vertex/triangle/material counts;
`chambercap01`'s exact `GFID` row-major table (all 12 entries, including the
4 zero holes); `chambercap01_000`'s exact 9-batch `material_id_large`
sequence; `sargerassword`'s exact 9-of-22 nonzero `MOUV` entries.

## References

- **wowdev.wiki** (`documentation/wowdev-wiki/md/WMO.md`, `wiki_revision`
  per that file's own frontmatter): `# MVER`, `# WMO root file` through
  `## MOSI` (lines 37-423) for the root-file sections; `# WMO group file`
  through `## MOTA` (lines 1020-2124) for the group-file sections. No
  `HUSK_AMENDMENTS.md` entry exists for this page yet -- this document's own
  findings (GFID row-major layout, `material_id_large`'s real load-bearing
  status, `MODI`-count-vs-`nDoodadNames` mismatch noted in the header
  section) are new, not yet folded into a wiki amendment.
- **wow.export** (`reference/wow.export/src/js/3D/loaders/WMOLoader.js`,
  `.../exporters/WMOExporter.js`) -- design reference only, consulted for
  how an established tool structures WMO parsing; no code copied.
- **husk `src/`**: `src/phys.hpp`/`phys.cpp` (chunk-tag-dispatch idiom,
  reversed-tag-literal convention this document's sketches mirror directly),
  `src/chunk.hpp`/`chunk.cpp` (`readChunks`/`findChunk`, directly reusable
  for both the root file's top-level chunks and the group file's nested
  `MOGP`-payload walk), `src/gltf.hpp` (`NamedMesh`/`Material`/`Primitive`/
  `extras` construction pattern), `src/cmd_export.cpp`'s collision-mesh
  block (closest existing precedent for an unskinned `NamedMesh`).
- **Real corpus** (`/media/luna/data/wow_export`, read-only): exact paths
  and counts cited inline per claim above. Community listfile used for
  FileDataID→path cross-checks: `/media/luna/work/tinker/dev/wow_modding/
  m2mod/mappings/listfile.csv` (read-only reference, 2,206,299 entries) --
  not husk's own data, consulted the same way `casc-tool`'s own listfile
  usage already is elsewhere in this project's history.

## Priority order

1. **Chunk container + root/group split** (`MVER`/`MOHD`/`MOGN`/`MOGI`/
   `GFID`, nested `MOGP` walk) -- everything else depends on this being
   right first; fully verified, zero open data gaps.
2. **Mesh geometry + normals + UVs** (`MOVT`/`MOVI`/`MOBA`/`MONR`/`MOTV`) --
   the actual renderable payload; `material_id_large`'s real load-bearing
   status makes this more than a trivial transcription.
3. **Materials** (`MOMT`/`MOTX`) -- needed before mesh export can attach
   real materials; `MOM3` deliberately deprioritized (zero real files).
4. **Vertex colors** (`MOCV`/`MOC2`) -- real, `native`-reachable data, but
   the `FixColorVertexAlpha` fidelity question needs a decision first.
5. **Per-face material info** (`MOPY`/`MPY2`, materials half) -- cross-
   checks batch-level material assignment; low effort once 1-3 exist.
6. **Skybox** (`MOSB`/`MOSI`) -- trivial once M2 placement/embedding exists,
   genuinely low priority on its own.
7. **Animated texture UV** (`MOUV`) -- `extras`-only ceiling, real but
   cosmetic; lowest priority of the confirmed-real items.
8. **Tangents** (`MOTA`) -- not planned, `n/a` ceiling, listed only for
   completeness against `WORLD_COMPLETENESS.md`'s own row.
