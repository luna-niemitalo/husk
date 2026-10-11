# TODO: `.wdt`/`.wdl` map-root and map-level sidecar formats

**Status: an open punch list, not a historical record.** The root `.wdt`
is implemented (item 1 lists what's left); the sidecars below are plans.
This file gets worked
like `RO_COMPLETENESS_TODO.md`/`../INVESTIGATIONS_TODO.md` already are: fixed
items get removed outright once closed, git history is the record of what
was fixed and when, not this file.

**Scope**: this is one of three sibling TODO docs expanding
`../../WORLD_COMPLETENESS.md`'s "World structure & scene composition" and
"Sidecar & dependency formats" sections into implementation-ready plans.
This file covers the `.wdt` map-root container end to end — tile existence
(`MPHD`/`MAIN`/`MAID`/`MAI2`), the global single-WMO-map case (`MWMO`+
`MODF`), and the `_occ`/`_lgt` sidecar-of-a-sidecar files — plus `.wdl`
(coarse whole-map heightmap + reduced-detail placement), folded in here per
`../../WORLD_COMPLETENESS.md`'s own "Sidecar & dependency formats" framing of
`.wdt`/`.wdl` as one shared "map-level sidecar" concern. Two adjacent
`.wdt` sidecars — `_fogs` (`VFOG`/`VFEX`) and `_mpv` (`PVPD`/`PVMI`/`PVBD`)
— are **named and scoped here only**; their own chunk-level struct/
data-model work belongs to a sibling `FOG_VOLUMES_TODO.md` another session
is writing in parallel, so this file doesn't duplicate that depth (see
their own short sections below, which are pointers, not plans). ADT's own
terrain mesh and the doodad/WMO *placement* records (`MDDF`/`MODF` at ADT
scope) belong to `ADT_TERRAIN_TODO.md`/`WORLD_PLACEMENT_TODO.md`
respectively — not duplicated here either, beyond the one placement record
this file *does* own (the map-scope global-WMO `MODF`, which has no ADT
tile to live in).

**Real-data verification discipline**: every claim below was checked
against the real, pre-extracted corpus at `/media/luna/data/wow_export`
(read-only), using new, independent-from-husk Python scripts (no reuse of
a not-yet-written parser — same methodology `../../WIKI_FINDINGS.md`'s every
section already uses). Exact counts and file paths are cited per finding.
Scripts are throwaway, kept outside the repo tree per this session's own
instructions — not committed.

## A confirmed, previously-undocumented-here structural fact: reversed chunk tags

`.wdt`/`.wdl` (and ADT, covered in the sibling docs) are **not** like M2's
own inline chunks — chunk tags are stored **byte-reversed on disk**, the
same convention husk's own `src/phys.hpp` already documents for `.phys`
("the opposite of M2's own inline chunks"). Confirmed directly via `od -A x
-t x1z` against a real file
(`/media/luna/data/wow_export/world/maps/2211/2211.wdt`): the first bytes
are literally `52 45 56 4d` = `"REVM"` (the reverse of `"MVER"`), followed
by `44 48 50 4d` = `"DHPM"` (reverse of `"MPHD"`), then `4e 49 41 4d` =
`"NIAM"` (reverse of `"MAIN"`). None of `WDT.md`, `WDL/v18.md`, `ADT/v18.md`,
or `Chunk.md` state this explicitly (`Chunk.md` only says WDT/ADT/WMO/WDL/
M2 "follow a chunked structure similar to RIFF" — it doesn't call out the
reversal at all) — this is the same "the wiki doesn't say this, husk found
it by looking at real bytes" gap `PHYS.md`'s `HUSK_AMENDMENTS.md` entry
already flagged for `.phys`.

**Implementation consequence**: husk's existing `src::readChunks`/
`findChunk` (`src/chunk.hpp`/`chunk.cpp`) are **directly reusable, unmodified**
— they split a buffer into raw `(tag, data, size)` records with no
assumption about tag spelling. The only thing a `.wdt`/`.wdl`/ADT parser
needs to do differently from `m2.cpp` is compare against the
**already-reversed** literal tag spelling when calling `findChunk`, exactly
`src/phys.cpp`'s own established idiom (reversed constants, e.g. `"SYHP"`
for `"PHYS"`) — not a new chunk-walker.

## 1. `.wdt` map root — what's left

Done (2026-10-11): `adt::parseWdt` reads `MPHD.flags`, `MAIN`, `MAID` and
the global `MWMO`/`MODF`, each optional chunk checked against its `MPHD`
flag; the world document's grid holds the tiles `MAIN` lists, takes tile
FileDataIDs from `MAID`, and places a terrain-less map's WMO
(`REFACTOR/BUNDLE_FORMAT.md` "World documents"). Verified facts are in
`WIKI_FINDINGS/WORLD.md` "WDT".

Open:
- `MAI2` (real in `azeroth.wdt`/`kalimdor.wdt`, 4096×32 bytes): not
  parsed; only its first field is known to hold FileDataIDs.
- `MPHD`'s sidecar FileDataIDs (`lgt`/`occ`/`fogs`/`mpv`/`tex`/`wdl`/
  `pd4`): not parsed; the sidecars are found by name.
- `MAIN` bits beyond 0x1 (0x2 "all water"): not read.

## 3. `_occ`/`_lgt` sidecars (WoD+)

**Current state**: `none`/`none`.

**Wiki citation**: `WDT.md`, `# \_occ, \_lgt` section (lines 407–583).

### `_occ.wdt` — occlusion heightmap probe data

**Verified struct shape, with a real, corpus-wide-confirmed wiki
correction**: `MAOI` entries (12 bytes: `tile_x`/`tile_y`/`offset`/`size`,
all confirmed byte-for-byte) — the wiki states `size` is "always
`(17*17+16*16)*2`" = 1090 bytes (545 `int16_t` samples, the same shape as
`.wdl`'s own `MARE` chunk, which the wiki explicitly cross-references:
"Same content as WDL#MARE_chunks"). **Real data disagrees**: decoding
`world/maps/2972/2972_occ.wdt` directly (286 real `MAOI` entries) found
every single `size` field is **578**, not 1090 — and every `offset` field
increments by exactly 578 between consecutive entries (`offset = i * 578`
for entry `i`), with `MAOH`'s own total byte length (165308) dividing
*exactly* by 578 (286 entries × 578 = 165308, zero remainder). **Verified
across the full real corpus**: all 959/959 `_occ.wdt` files, 43,833 total
real `MAOI`/`MAOH` tile entries, are internally consistent at a **578-byte
(289×`int16_t`) stride, zero exceptions**. 578 = `17*17*2` exactly — the
**outer 17×17 grid only**, with the inner 16×16 points (which `.wdl`'s own
`MARE` genuinely does carry, confirmed separately, see §4 below) simply
absent from `_occ`'s own heightmap. The wiki's "same content as
`WDL#MARE`" claim is wrong for the *size*, even though the general
"interleaved outer-heightmap grid" *shape* is the same idea at a coarser
resolution. Flagging this precisely as a future `../../WIKI_FINDINGS.md` entry
(not written this session): **`_occ.wdt`'s `MAOH` per-tile record is
`int16_t heightmap[17*17]` (578 bytes), not `(17*17+16*16)` (1090 bytes)**.

### `_lgt.wdt` — level-designer point/spot lights

**Verified struct shapes** (all confirmed clean, corpus-wide, real files —
no discrepancies found): `MVER` (18 for ≤Legion-early, 20 for later —
corpus shows both `{18, 20}`), `MPL3` (56 bytes/`0x38`, Shadowlands+,
420/959 files carry a non-empty one), `MSLT` (64 bytes/`0x40`, Legion+
spotlights, 420/959), `MLTA` (12 bytes, "map light texture animation,"
250/959). **Zero real files in this corpus carry the older `MPLT` or
`MPL2` chunks** — consistent with this being a modern-build corpus where
every light-bearing file has already migrated to `MPL3`. `MTEX`
(Legion+ lightcookie texture FileDataIDs) present in 74/959 files.

### Data model sketch

```cpp
namespace husk::wdt {

struct OcclusionTile {  // MAOI entry + its MAOH payload
    uint16_t tileX = 0, tileY = 0;
    std::array<int16_t, 17 * 17> heights{};  // corrected 289-sample shape, see finding above
};

struct PointLight {  // MPL3 -- the modern (SL+) shape; MPLT/MPL2 unverified, zero real hits this corpus
    uint32_t lightIndex = 0;
    uint32_t colorBgra = 0;
    Vec3 position;
    float attenuationStart = 0, attenuationEnd = 0, intensity = 0;
    Vec3 rotation;
    uint16_t tileX = 0, tileY = 0;
    int16_t mltaIndex = -1, textureIndex = -1;
    uint16_t flags = 0;
    float scale = 0;  // half-float on disk, same decode as M2's own fixed16/half-float tracks
};

struct SpotLight {  // MSLT
    uint32_t id = 0;
    uint32_t colorBgra = 0;
    Vec3 position;
    float attenuationStart = 0, attenuationEnd = 0, intensity = 0;
    Vec3 rotation;
    float spotlightRadius = 0, innerAngle = 0, outerAngle = 0;
    uint16_t tileX = 0, tileY = 0;
    int16_t mltaIndex = -1, textureIndex = -1;
};

struct LightAnimation { float amplitude, frequency; uint32_t function; };  // MLTA

}  // namespace husk::wdt
```

### Parse / Consumption / glTF ceiling

| Concept | Parse | Consumption | glTF ceiling |
|---|---|---|---|
| `_occ` heightmap | `full` (corrected stride) | `diagnostic` | `n/a`, infrastructure (occludes rendering, isn't itself geometry) |
| `_lgt` point/spot lights | `full` | `extras` or `native` | `native-possible` — glTF's real `KHR_lights_punctual` extension exists; same open question `../../WORLD_COMPLETENESS.md`'s Lighting section already flags for WMO's `MOLT` — worth resolving once, shared across both sources |

### Open design question

Whether `_lgt.wdt` lights map to real `KHR_lights_punctual` glTF nodes
(`native`) or stay `extras`-only is the same open question
`../../WORLD_COMPLETENESS.md`'s Lighting section already raises for WMO's own
`MOLT` point lights — recommend resolving it once (probably in whichever
lighting-scoped sibling doc/session handles WMO lighting) and reusing the
answer here, rather than deciding it twice independently.

## 4. `.wdl` — coarse whole-map heightmap + reduced-detail placement

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/WDL/v18.md`, entire page.

### Verified struct shapes

All confirmed clean across the full real corpus (959 files, one `.wdl` per
map):

- `MVER` = 18 on all 959 files.
- `MAOF` (`uint32_t areaLowOffsets[4096]`, 16384 bytes): exact size match
  on 959/959.
- `MARE` (per-tile heightmap): **43,833 real chunks, every one exactly
  1090 bytes** = `(17*17+16*16)*2` int16 samples — confirming the wiki's
  stated size *is* correct for `.wdl` itself (in contrast to `_occ.wdt`'s
  `MAOI`/`MAOH`, which is NOT the same size despite the wiki claiming
  parity — see §3's finding above; cross-referencing these two results
  against each other is what made the `_occ` discrepancy obvious).
- `MAHO` (per-tile hole bitmask, `uint16_t[16]`): 43,833 real chunks, every
  one exactly 32 bytes.
- `MAOE` (Legion+ ocean mask, 0x20-byte fixed blob per wiki): 22,074 real
  instances found.
- Legion+ silhouette placement: `MLDD`/`MLDX` (753 files each — M2
  silhouette placement, `SMDoodadDef`-shaped per the wiki, confirmed a real
  36-byte example at `ruinsoftheramore.wdl`), `MLMD`/`MLMX` (753 files
  each — WMO silhouette placement). A rarer `MLDF`/`MLDB`/`MLDL`/`MSLI`/
  `MSLD` family also appears (32/1/3/1/1 files respectively) — small
  enough samples that husk should parse them structurally (offsets are
  simple/self-describing) but flag as unverified-by-volume, same
  "verified floor" treatment `.phys`'s rarer joint/shape variants already
  get in `src/phys.hpp`.
- Pre-Legion silhouette: `MWMO`/`MWID`/`MODF` (206 files) — confirmed
  present exactly on files *without* `MLDD`/`MLMD` (i.e., these are
  mutually exclusive by client version, matching the wiki's own "in Legion
  it seems MWMO, MWID and MODF were removed... MLDD, MLDX, MLMD, MLMX were
  added instead" framing) — not exhaustively cross-tabulated this session
  (753+206 ≠ 959 exactly by a small remainder — some maps may have
  neither, e.g. tiny instance-only maps with no silhouette content at all
  — plausible, not investigated further, low priority).
- Skybox-related family (`MSSN`/`MSSC`/`MSSO`/`MSSF`): present in 23/13/23/4
  files respectively — small samples, matches the wiki's own "at a cursory
  glance" unverified framing; husk should parse structurally but not treat
  field semantics beyond what's already named as confirmed.
- `MLMB` (BfA+ blend-mesh-related, opaque `char[]` per wiki): 10 files.

### Data model sketch

```cpp
namespace husk::wdl {

struct LowResTile {
    std::array<int16_t, 17 * 17 + 16 * 16> heights{};  // MARE, confirmed 545-sample/1090-byte
    std::array<uint16_t, 16> holes{};                  // MAHO, confirmed 16-uint16/32-byte
    std::optional<std::array<uint8_t, 0x20>> oceanMask;  // MAOE, Legion+
};

// Legion+ silhouette placement -- structurally identical to ADT's own
// MLDD/MLMD (see ADT_LOD_TODO.md), just at whole-map scope instead of
// per-tile. Reuse that data model rather than redefining it here.
struct File {
    std::array<std::optional<LowResTile>, 64 * 64> tiles{};  // indexed via MAOF, 0 offset = absent
    // Pre-Legion: globalSilhouetteWmoFilename + placements (MWMO/MWID/MODF)
    // Legion+: doodad/wmo silhouette placement (MLDD/MLDX/MLMD/MLMX) --
    // see ADT_LOD_TODO.md's MLDD/MLMD structs, reused verbatim.
};

File parse(const std::vector<uint8_t>& fileBytes);

}  // namespace husk::wdl
```

### Parse / Consumption / glTF ceiling

| Concept | Parse | Consumption | glTF ceiling |
|---|---|---|---|
| `MARE`/`MAHO` low-res heightmap | `full` | `diagnostic`, maybe `native` as a coarse background mesh | `native-possible, not done` — a 17×17/33×33 grid is a real, trivial mesh translation, same shape as ADT's own `MCVT`; genuinely optional/low-priority since it's a rendering-distance optimization, not primary content |
| Legion+ silhouette placement | `full` | depends on M2/WMO export existing | `native-possible, not done` — same shape as `MDDF`/`MODF`, just coarser instances; low priority, this is a distant-LOD optimization layer |
| Skybox/blend-mesh family | `descriptor` | `diagnostic` | `n/a`/`node-possible, unclaimed` — small samples, low priority |

## 5. `_fogs`/`_mpv` sidecars — named and scoped only, not planned here

**Do not duplicate `FOG_VOLUMES_TODO.md`'s depth here** — this section
exists only so this file's own coverage of `WDT.md` is complete, per
`../../WORLD_COMPLETENESS.md`'s framing of `.wdt`'s sidecar family as one unit.

- **`_fogs.wdt`** (`WDT.md`, `# \_fogs` section): map-level volumetric fog
  (`VFOG`/`VFEX`). Real corpus: 753/753 `_fogs.wdt` files present (matching
  `MPHD.fogsFileDataID`-eligible tiles), `MVER == 2` on all 753 (the
  War Within+ version, not the Legion-era 1). **82/753 files have a
  non-empty `VFOG`** (the rest are the wiki's own documented "empty,
  merged by accident" case, still real per-file). All `VFOG` chunks are an
  exact multiple of 0x68 (104 bytes, matching the documented struct) —
  confirmed clean. **A real chunk the wiki page doesn't document at all**:
  every file with `VFEX` (1,496 real instances, always exactly 0x60/96
  bytes as individual per-chunk instances, not one array chunk) also has an
  equal count of a **`VFE2`** chunk (1,496 instances) immediately
  following each `VFEX` — e.g. `world/maps/2656/2656_fogs.wdt` has 6
  `VFOG` entries, then 6 separate `VFEX` chunk instances (96 bytes each,
  one `VFEX` record per chunk, not a packed array), then 6 separate
  `VFE2` chunk instances (176 bytes each). **Flagging `VFE2` for
  `FOG_VOLUMES_TODO.md`'s own investigation** — not derived further here,
  out of this file's scope, but worth knowing it exists before that doc's
  own struct-level pass starts from `WDT.md`'s text alone.
- **`_mpv.wdt`** (`WDT.md`, `# \_mpv` section): map-level particulate
  volumes (`PVPD`/`PVMI`/`PVBD`). Real corpus: 753/753 files present,
  `MVER == 3` on all 753 (`mpv_version_3`). 619/753 have real
  `PVPD`/`PVMI`/`PVBD` content (the remainder presumably version-0-shaped
  empties per the wiki's own text — not individually confirmed this
  session, out of scope). No struct-level decode attempted here — full
  depth belongs to `FOG_VOLUMES_TODO.md`.

## 6. A genuinely separate, undocumented file family found in the same directories — explicitly out of scope

While walking `world/maps/*` for real `.wdt` files, this session's first
survey pass also matched **8 real `<mapid>_preload.wdt` files**
(`2847`, `3027`, `2954`, `2837`, `2703`, `2834`, `3026`, `2851`) — not
mentioned anywhere in `WDT.md`. These are a **wholly different container**:
`MVER` payload is `1`, not `18`, and the only other chunks present are
`MHDR` (a 4-byte payload, not the documented 32-byte `.wdt` `MPHD` shape —
a name collision only, not the same struct) and a single large, undocumented
`MMFE` chunk (10,580 bytes in the one file inspected). This is the same
class of finding as `../../WORLD_COMPLETENESS.md`'s own already-noted M3
discovery — **explicitly out of scope, noted so a future session doesn't
conflate it with the real root `.wdt` format** while iterating this same
directory tree. Not investigated further.

## Test plan

**Real fixture candidates** (all under `/media/luna/data/wow_export`, to be
copied into `test_data/` — gitignored, same "personal WoW extraction,
never committed" convention every other `test_data/` fixture here already
follows):

- `world/maps/2211/2211.wdt` — small, clean, tile-bearing terrain map, no
  global WMO.
- `world/maps/garrison_alliance_salvageyard3/garrison_alliance_salvageyard3.wdt`
  — small global-WMO map, real `MWMO` present, `MODF.flags == 0`.
- `world/maps/uldaman/uldaman.wdt` — global-WMO map *without* `MWMO`
  (`MODF.flags & 0x8`, FileDataID-direct case) — the more interesting of
  the two global-WMO shapes, must be covered by a real test, not just the
  common case.
- `world/maps/2972/2972_occ.wdt` — real, multi-tile `_occ.wdt` (286
  entries) for the corrected 578-byte-stride test.
- Any one real `_lgt.wdt` with non-empty `MPL3`/`MSLT` (e.g. re-derive one
  from the 420-file set above at implementation time — none pinned by name
  this session, low cost to find again via the same script).
- `world/maps/kalimdor/kalimdor.wdt` or `azeroth/azeroth.wdt` — the only
  two real `MAI2` fixtures known to exist; large files, so consider
  extracting just the relevant chunk bytes into a smaller synthetic
  fixture rather than committing the whole file.
- Any `.wdl` sibling of the above (`ruinsoftheramore.wdl` confirmed to have
  a real, non-trivial `MLDD` entry).

**Synthetic fixtures should cover**: malformed/truncated chunk sizes
(matching `ChunkError`'s existing throw-on-truncation discipline),
`MAIN`'s `Flag_HasADT` bit decode in isolation, the `MODF.flags & 0x8`
FileDataID-vs-filename branch (both directions, since it's a real
either/or with no reliable synthetic default), and `MAOI`'s corrected
578-byte stride explicitly (a regression test that would have caught this
session's own finding immediately if it existed before implementation).

## References

- **wowdev.wiki**: `documentation/wowdev-wiki/md/WDT.md` (entire page —
  `MPHD`/`MAIN`/`MAID`/`MAI2`/`MWMO`/`MODF`/`_occ`/`_lgt`/`_fogs`/`_mpv`
  sections), `documentation/wowdev-wiki/md/WDL/v18.md` (entire page).
- **wow.export** (design ideas only, never code):
  `reference/wow.export/src/js/3D/loaders/WDTLoader.js` — confirms the
  same `MPHD`/`MAIN`/`MAID`/`MWMO`/`MODF` chunk set is what a real,
  shipped community tool reads for map-root purposes (its own chunk-ID
  constants are the *non*-reversed spelling read as a little-endian
  `uint32_t`, e.g. `0x4D504844` for `"MPHD"` — consistent with this file's
  own reversed-bytes finding once you account for x86 endianness, not a
  contradiction). No `.wdl` loader exists in `wow.export`'s tree (checked
  `reference/wow.export/src/js/3D/loaders/` — not present), consistent
  with `.wdl` being a lower-priority, distant-rendering-only format for
  that tool too.
- **husk `src/` to reuse**: `src/chunk.hpp`/`chunk.cpp` (`readChunks`/
  `findChunk`, directly reusable unmodified — see this file's own opening
  callout), `src/phys.hpp`/`phys.cpp` (the chunk-tag-dispatch/bounds-
  checked-reader idiom to mirror, and the precedent for reversed on-disk
  tags), `src/gltf.hpp`'s `Skeleton::EmitterAnchor`/`CorrectionSet` (the
  "minimal anchor in extras, full data in `dump-chunks`" pattern, relevant
  once `_lgt`'s lights or `.wdl`'s silhouette placement get a glTF-side
  home).
- **Real corpus evidence**: `/media/luna/data/wow_export/world/maps/**`
  — 959 root `.wdt`, 959 `_occ.wdt`, 959 `_lgt.wdt`, 753 `_fogs.wdt`, 753
  `_mpv.wdt`, 959 `.wdl` (full-corpus scans, not samples, for every count
  in this file except the MHID/MDID-style deep per-record checks noted as
  samples in the sibling ADT docs). Exact file paths cited inline per
  finding above.

## Priority order

1. **§3 (`_occ`/`_lgt`)** — `_occ`'s corrected stride is now known, no
   remaining ambiguity; `_lgt` is fully clean. Medium priority — genuinely
   useful (`_lgt` lights are real, renderable data with a plausible
   `KHR_lights_punctual` glTF ceiling) but not blocking anything else here.
2. **§4 (`.wdl`)** — lowest priority of the four real formats in this
   file: a distant-rendering optimization layer, not primary content: `MAIN`
   tile existence and ADT terrain itself matter far more for "does the
   world look right" than a coarse background heightmap does.
5. **§5/§6** — not this file's implementation work; §5 is a pointer for a
   sibling doc, §6 is an explicit non-goal note.
