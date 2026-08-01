# TODO: ADT terrain mesh — heightmap, normals, holes, split-file structure

**Status: an open punch list, not a historical record.** Nothing here is
implemented yet. Once implementation starts, fixed items get removed
outright — git history is the record, not this file (same convention
`RO_COMPLETENESS_TODO.md`/`TODO_correctness.md` already use).

**Scope**: one of three sibling docs expanding `WORLD_COMPLETENESS.md`'s
"Terrain geometry (ADT)" section — per that file's own words, the heightmap
grid is "the single biggest ADT geometry item" in the whole WMO/ADT
expansion. This file covers: the ADT chunk container itself (`MVER`/
`MHDR`), the heightmap (`MCVT`) and normals (`MCNR`), terrain holes, and
the Cata+ split-file structure (root/`_obj0`/`_obj1`/`_tex0`/`_tex1`/`_lod`
+ `MHID`/`MDID`/`MWDR`/`MWDS`/`MCIN`). **It does not cover `MDDF`/`MODF`**
(doodad/WMO placement *onto* a tile) — that's `WORLD_PLACEMENT_TODO.md`'s
job, a sibling doc another session is writing in parallel; this file only
notes where those chunks live structurally (which split file, which MCNK
cross-reference) without planning their own data model. The Legion+
terrain-LOD `ML*` chunk family is `ADT_LOD_TODO.md`, not here. Liquid
(`MH2O`/`MCLQ`), texture layers (`MCLY`/`MCAL`/`MTEX`/`MDID`/`MHID`'s
*content*, as opposed to their presence), and every other `MCNK`
sub-chunk not named above are also out of this file's scope — this file is
specifically the terrain **mesh**, matching `WORLD_COMPLETENESS.md`'s own
"single biggest item" framing, not every byte an ADT file contains.

**Real-data verification discipline**: every claim below was checked
against `/media/luna/data/wow_export` (read-only) with new, independent
Python scripts — no reuse of a not-yet-written husk parser. Full-corpus
counts are used wherever the check is cheap (file existence, chunk
presence via a single top-level pass); a bounded random sample (2,500 of
55,279 real root `.adt` files) was used for the invasive per-`MCNK` byte
decode, because a first full-corpus attempt (all 55,279 files, full reads)
was killed after 13+ minutes with zero results on this storage — see the
Environment note at the end. 2,500 files is still a large, real sample
(640,000 real `MCNK` chunks decoded, of which the first 16 per file —
40,000 total — got full sub-chunk decode).

## Reversed chunk tags — same finding as the sibling `.wdt` doc

ADT's chunk tags are byte-reversed on disk, identical to `.wdt`/`.wdl`/
`.phys` (see `WDT_TODO.md`'s own callout for the `od`-verified receipts —
not re-derived here, same fact). `src/chunk.hpp`'s `readChunks`/`findChunk`
are directly reusable unmodified; compare against reversed literal tags,
`src/phys.cpp`'s established idiom.

## 1. `MVER`/`MHDR` — chunk container and header

**Current state**: `none`/`none`.

**Wiki citation**: `documentation/wowdev-wiki/md/ADT/v18.md`, `## MVER
chunk` (99–105), `## MHDR chunk` (107–132).

**Verified**: `MVER` = 18 on all 2,500 sampled real root files (100%).
`MHDR` (64 bytes: `flags`, `mcin`, `mtex`, `mmdx`, `mmid`, `mwmo`, `mwid`,
`mddf`, `modf`, `mfbo`, `mh2o`, `mtxf`, `mamp_value`+`padding[3]`,
`unused[3]`) decodes cleanly on all 2,500. `flags & 1` (`mhdr_MFBO`) is set
on 1,600/2,500 (64%) — **exactly matching** the real count of files with an
`MFBO` chunk present (also 1,600/2,500) — a clean, fully-self-consistent
cross-check between the flag bit and the chunk's real presence, not just a
plausible-looking value. `flags & 2` (`northrend` marker) set on 3/2,500 —
rare, matches the wiki's own "is set for some northrend ones" hedge.
`mhdr.mcin` (the pre-Cata `MCIN` chunk offset) is **0 on all 2,500 real
files** — this corpus is entirely post-Cata-split, consistent with every
other finding in this doc (see §4).

### Data model sketch

```cpp
namespace husk::adt {

struct Header {  // MHDR
    uint32_t flags = 0;         // bit0: has MFBO, bit1: "northrend" marker
    uint32_t mcinOffset = 0;    // pre-Cata only, 0 in every real file this session sampled
    uint32_t mtexOffset = 0, mmdxOffset = 0, mmidOffset = 0;
    uint32_t mwmoOffset = 0, mwidOffset = 0;
    uint32_t mddfOffset = 0, modfOffset = 0;  // see WORLD_PLACEMENT_TODO.md -- not derefed here
    uint32_t mfboOffset = 0;    // only meaningful if flags & 1
    uint32_t mh2oOffset = 0, mtxfOffset = 0;
    uint8_t ampValue = 0;       // Cata+, texture_size = 64 / (2^ampValue), MAMP overrides if present
};

}  // namespace husk::adt
```

### Parse / Consumption / glTF ceiling

| Concept | Parse | Consumption | glTF ceiling |
|---|---|---|---|
| `MVER`/`MHDR` | `full` | `diagnostic` | `n/a`, infrastructure |

## 2. Heightmap grid (`MCVT`) — "the single biggest ADT geometry item"

**Current state**: `none`/`none`.

**Wiki citation**: `ADT/v18.md`, `### MCVT sub-chunk` (773–930), `## MCNK
chunk` (687–759, the parent header carrying `MCVT`'s implicit position
offset).

### Verified struct shape

`145 float` values (9×9 outer + 8×8 inner, interleaved-row order exactly as
the wiki's own numbered diagram shows) per `MCNK`. **Verified on all 40,000
sampled real `MCNK` sub-chunks (2,500 files × first 16 `MCNK`s each): 100%
decode as exactly 580 bytes (145×4), zero size mismatches.** Height values
across the full sample range `[-6382.35, 6382.48]` — a plausible real-world
range (the wiki's own "Height" section notes the practical range stays
within roughly ±32k, consistent).

**A real structural clarification the wiki doesn't state plainly**:
`MCVT`/`MCNR` **stay in the root file even after the Cata+ split** — only
texture (`MCLY`/`MCAL`, → `_tex0`) and object-reference (`MCRF`/`MCRD`/
`MCRW`, → `_obj0`/`_obj1`) sub-chunks move out. Confirmed directly:
**all 2,500 sampled root files have a real `MCVT` sub-chunk inside their
root-file `MCNK`s** (not zero, which is what a naive reading of "Cata+
splits ADT into multiple files" might lead someone to assume) — the split
is about *which* sub-chunks move, not a wholesale move of the terrain mesh
itself. Worth stating explicitly in any implementation, since getting this
wrong would mean looking for terrain height data in the wrong file for
every Cata+ tile in the game (i.e., every tile in this entire corpus).

**Indices are implicit, not stored** (confirmed no separate index array
exists anywhere in `MCNK` or its sub-chunks) — a fixed 9×9-outer/8×8-inner
topology, same "ROAM"-style triangle-fan-around-center-point mesh the wiki
diagrams at length (lines 802–927, several competing stripification
attempts from different wiki contributors — **all describing the same
fixed topology**, not different real shapes; an implementer should pick
one topology (e.g. the triangle-fan form, 4 triangles per outer quad
sharing the inner point) rather than trying to reconcile the wiki's several
historical attempts as if they disagree on the *data*, only on the
*traversal order*).

### Data model sketch

```cpp
namespace husk::adt {

// One MCNK's heightmap. 9x9 outer (row-major) + 8x8 inner (row-major),
// exactly the wiki's own MCVT ordering -- position 0 is the northwest
// corner of this MapChunk cell, matching MCNK.position (world-space
// offset for this 33.333-yard cell).
struct HeightGrid {
    std::array<float, 9 * 9> outer{};
    std::array<float, 8 * 8> inner{};
};

// Fixed 8x8-quad topology shared by every MCNK -- generate once, reuse
// for every tile (positions differ, indices never do). 4 triangles per
// quad (fan around the inner center point) x 8x8 quads = 256 triangles
// per MCNK, 65536 per full 16x16-MCNK tile.
std::vector<std::array<int, 3>> heightGridTriangleFan();  // returns indices into a flattened outer+inner vertex buffer

}  // namespace husk::adt
```

### Parse / Consumption / glTF ceiling

| Concept | Parse | Consumption | glTF ceiling |
|---|---|---|---|
| `MCVT` heightmap | `full` | `native` | `native-possible, not done` — a direct `POSITION` accessor + a fixed, precomputed index buffer, structurally identical in kind to how husk's own M2 collision-mesh export already handles a "positions + implicit/derived topology" mesh (`src/gltf.hpp`'s `NamedMesh`, `M2_COMPLETENESS.md`'s Collision section) |

### Open design question

**One mesh per `MCNK`, or one mesh per full 16×16-`MCNK` tile?** A real ADT
tile is 256 separate `MCNK`s; each is independently positioned
(`MCNK.position`) and independently holed (§3). Recommend **one glTF mesh
per tile** (not per `MCNK`), built by concatenating all 256 `MCNK`s'
vertex data with a world-space offset baked into each `MCNK`'s own vertex
positions before the merge — this matches how husk already treats a whole
M2 as one mesh rather than one mesh per submesh/batch, and avoids 256
tiny glTF primitives per tile for no rendering benefit. Not decided,
flagged for a real design pass since it affects the whole export shape.

## 3. Normals (`MCNR`)

**Current state**: `none`/`none`.

**Wiki citation**: `ADT/v18.md`, `### MCNR sub-chunk` (974–990).

**Verified**: 145×`int8_t[3]` (normalized, X/Z/Y order per the wiki's own
field comment) + 13 bytes of undocumented trailing data. **All 40,000
sampled `MCNR` sub-chunks are exactly 448 bytes** (145×3 + 13 = 448) — the
wiki's own text distinguishes this from a pre-Cata 435-byte (no padding)
variant; **this corpus (entirely Cata+ per §4) shows only the 448-byte
shape, zero exceptions** — consistent, not a new finding, but a clean
confirmation that husk should gate on this size difference by *version/
split-era*, not attempt to detect both from a single struct guess.
**Z-component (up-axis) sanity**: 39,984/40,000 (99.96%) of sampled normals
have `Z >= -5` in raw `int8_t` units (i.e., very close to "always
non-negative," the wiki's own claim, with the same small numeric noise the
wiki itself documents as a truncation artifact — not a parser bug).

### Data model sketch

```cpp
namespace husk::adt {

struct NormalGrid {
    std::array<std::array<int8_t, 3>, 9 * 9> outer{};  // X, Z, Y order (matches MCVT's own X/Y/Z-vs-height framing -- verify sign convention against a real render, not just decode)
    std::array<std::array<int8_t, 3>, 8 * 8> inner{};
    // 13 trailing bytes: wiki says "purpose unknown," "not derived from
    // the normals," client doesn't appear to read it either -- husk
    // should read and discard, not attempt to interpret.
};

}  // namespace husk::adt
```

### Parse / Consumption / glTF ceiling

| Concept | Parse | Consumption | glTF ceiling |
|---|---|---|---|
| `MCNR` normals | `full` | `native` | `native-possible, not done` — a direct `NORMAL` accessor paired 1:1 with `MCVT`'s `POSITION`, same shape M2's own vertex normals already get |

## 4. Terrain holes

**Current state**: `none`/`none`.

**Wiki citation**: `ADT/v18.md`, `### Terrain Holes` (761–771), `MCNK`
header's `holes_low_res`/`holes_high_res`/`high_res_holes` flag (offsets
0x3C / conditionally 0x14, per the header struct at lines 701–759).

**Verified**: `MCNK.flags` bit 9 (`high_res_holes`, ≥~5.3) is **0 across
every one of the 40,000 sampled `MCNK` headers** — this real corpus's
terrain tiles are all using the low-resolution 16-bit hole bitmask, not
the 64-bit one, at least in this sample. `holes_low_res` (the 16-bit
bitmask at header offset 0x3C) is **non-zero on 263/40,000 (0.66%)** of
sampled `MCNK`s — real, sparse, but genuinely present terrain holes (caves,
etc.) — confirms this isn't a purely theoretical feature in this corpus.
The high-res 64-bit path (header bytes at 0x14, which double as
`ofsHeight`/`ofsNormal` in the low-res case — a real union, not two
separate fields) was **not exercised by this sample** (zero real hits) —
husk should implement it per the wiki's documented shape but flag it
unverified-by-real-bytes until a real high-res-holes file is found (a
targeted search, not attempted this session, would be the fastest way to
find one — try newer, more geometrically complex zones first).

### Data model sketch

```cpp
namespace husk::adt {

// A 4x4 grid of "hole" flags -- one bit per hole = one 2x2 group of the
// 8x8 inner-quad grid is entirely absent (wiki's own bit-layout diagram,
// ADT/v18.md lines 763-768).
struct Holes {
    bool highRes = false;
    uint64_t bitsHighRes = 0;   // only meaningful if highRes
    uint16_t bitsLowRes = 0;    // only meaningful if !highRes
    bool isHole(int col, int row) const;  // col,row in [0,3] -- see wiki's bit-position diagram
};

}  // namespace husk::adt
```

Application happens at mesh-*generation* time (skip the 2 triangles for a
held quad), not as a separate glTF concept — matches
`WORLD_COMPLETENESS.md`'s own framing exactly.

### Parse / Consumption / glTF ceiling

| Concept | Parse | Consumption | glTF ceiling |
|---|---|---|---|
| Terrain holes | `full` | applied at mesh-build time (no separate glTF representation) | `n/a` as a standalone concept — folds into `MCVT`'s own `native-possible` ceiling above |

## 5. Cata+ split-file structure

**Current state**: `none`/`none`.

**Wiki citation**: `ADT/v18.md`, `## split files (Cata+)` (81–97), plus the
per-chunk "split files: root/obj/tex/lod" annotations scattered throughout
the rest of the page (already cited per-chunk above).

### Verified real-file structure

- **55,277/55,279 (99.996%) real root `.adt` files have a matching
  `_obj0.adt` and `_tex0.adt`** — i.e., this corpus is essentially 100%
  post-Cata-split; the 2 exceptions weren't individually chased down
  (plausible extraction gaps, same class of thing `CORPUS_TODO.md`'s own
  #2/#5 already documented for M2's own corpus).
- **`_obj1.adt` exists for 55,277/55,279** — i.e., essentially always
  present, not a rare/optional file. **`_tex1.adt` exists for
  0/55,279 — zero real files, corpus-wide.** This exactly matches the
  wiki's own text: "`_tex1.adt` files are no longer loaded since the
  introduction of WDT's `MAID`" — this corpus is entirely `MAID`-bearing
  (see `WDT_TODO.md` §1), so the client-side reason `_tex1` stopped being
  written applies uniformly here. **Implementation consequence: don't
  build a `_tex1.adt` reader path at all for a `MAID`-only-targeting tool**
  — real zero, not "rare."
- **`_lod.adt` exists for 46,340/55,279 (83.8%)** — matches
  `WORLD_COMPLETENESS.md`'s own rough corpus count; see `ADT_LOD_TODO.md`
  for what's inside.
- **`MHID`/`MDID` (BfA+ height/diffuse-texture FileDataID tables,
  `_tex0`-resident)**: present, with **matching array lengths, in all
  1,500 sampled `_tex0.adt` files (100%)** — this corpus is uniformly
  past the 8.1.0.27826 cutover, `MTEX` (the older string-filename chunk)
  is presumably still present alongside per the wiki's own text but wasn't
  independently re-checked this session (low priority: `MHID`/`MDID` are
  strictly the modern, FileDataID-native path this tool should prefer).
- **`MWDR`/`MWDS` (Shadowlands+ doodad-set-selection-by-`MODF`, `_obj0`/
  `_obj1`-resident)**: real, but genuinely rare — **77/1,500 (5.1%)**
  sampled `_obj0.adt` files carry them, all 77 with internally consistent
  `(begin, end)` ranges into `MWDS` (`0 <= begin <= end < mwds.size()`,
  zero violations across all 77) — confirms the wiki's documented
  `MWDR`→`MWDS` index-range relationship holds cleanly on every real
  instance found. This is `WORLD_PLACEMENT_TODO.md`'s concern
  structurally (it resolves *which* WMO doodad set a specific `MODF`
  entry uses, per `ADT/v18.md`'s own `modf_use_sets_from_mwds` flag) —
  noted here only because it's part of this file's "what lives in which
  split file" inventory.
- **`MCIN` (pre-Cata chunk index)**: confirmed **absent** (`mhdr.mcin ==
  0`) on all 2,500 sampled root files — superseded structurally by the
  split-file layout, exactly as `WORLD_COMPLETENESS.md` already states.
  Husk should still parse it when present (any pre-Cata fixture found
  later) for completeness, but it's dead weight for this corpus.

### Data model sketch

```cpp
namespace husk::adt {

// One logical ADT tile, resolved across up to 6 real files. Mirrors
// M2's own multi-sidecar resolution pattern (--skin-dir/--anim/--bones-dir
// three-state auto/explicit/none), not a new convention.
struct TileFiles {
    std::vector<uint8_t> root;             // MVER/MHDR/MCNK (MCVT/MCNR/MCSH/MCSE/MCLQ/MFBO/MH2O)
    std::optional<std::vector<uint8_t>> obj0;  // MMDX/MMID/MWMO/MWID/MDDF/MODF/MCRD/MCRW, MWDR/MWDS (SL+)
    std::optional<std::vector<uint8_t>> obj1;  // Legion+ obj-LOD side, see ADT_LOD_TODO.md
    std::optional<std::vector<uint8_t>> tex0;  // MTEX/MDID/MHID/MCLY/MCAL/MTXF/MTXP, MCIN (pre-Cata only, root-resident there)
    std::optional<std::vector<uint8_t>> tex1;  // real corpus: always absent post-MAID, parse if present anyway
    std::optional<std::vector<uint8_t>> lod;   // see ADT_LOD_TODO.md
};

TileFiles resolveTileFiles(const std::string& rootPath);  // same-basename sibling probing, local filesystem only -- never CASC (DESIGN.md's Non-goals)

}  // namespace husk::adt
```

### Parse / Consumption / glTF ceiling

| Concept | Parse | Consumption | glTF ceiling |
|---|---|---|---|
| Split-file resolution | `full` (resolve which files exist) | `n/a`, infrastructure | `n/a`, infrastructure |
| `MHID`/`MDID` | `descriptor` (FileDataID arrays) | `diagnostic` | `n/a` — feeds into material/texture resolution, `WORLD_COMPLETENESS.md`'s own WMO/ADT-materials scope, not this file's |
| `MWDR`/`MWDS` | `descriptor` | `diagnostic` here; real consumption belongs to `WORLD_PLACEMENT_TODO.md` | `n/a`, cross-references `MODF` |
| `MCIN` | `full` (when present) | `diagnostic` | `n/a`, infrastructure, superseded |

### Open design question

**Same-basename sibling resolution vs. `MAID`-driven FileDataID
resolution**: given a root `.adt` path, should husk probe for
`<basename>_obj0.adt` etc. on disk directly (matching this project's
existing `--skin-dir`/`--bones-dir` same-basename convention), or require
the `.wdt`'s `MAID` table to resolve each sibling by FileDataID first
(more "correct" for a FileDataID-native modern client, but requires the
`.wdt` to be available and parsed first, a real added dependency for a
single-tile export use case)? Recommend **same-basename filesystem
probing as the default** (matches this whole project's "local directory,
user-populated, no CASC" convention, and this corpus shows a real ADT
tile's sibling files always share its exact basename anyway), with an
optional `MAID`-driven override for a future whole-map export mode —
flagged as a recommendation, not decided.

## Test plan

**Real fixture candidates** (`/media/luna/data/wow_export`, to copy into
`test_data/`, gitignored — same convention every other fixture here uses):

- `world/maps/ruinsoftheramore/ruinsoftheramore_40_38.adt` (+ its
  `_obj0`/`_obj1`/`_tex0`/`_lod` siblings, confirmed as a real, complete
  6-file set this session) — a good default "everything present" fixture.
- Any one of the 263 real `MCNK`s with a non-zero `holes_low_res` found in
  the 2,500-file sample (not individually pinned by path this session —
  cheap to re-derive with the same script at implementation time) — a real
  terrain-hole fixture is more valuable than a synthetic one here, since
  hole *application* (skip 2 triangles per held quad) is easy to get
  subtly wrong on which quad index maps to which bit.
- A real `MWDR`/`MWDS`-bearing `_obj0.adt` (77 candidates found this
  session, not individually pinned — re-derive via the same script) if
  `WORLD_PLACEMENT_TODO.md`'s own test plan doesn't already claim one.

**Synthetic fixtures should cover**: `MCVT`/`MCNR` exact-size mismatches
(malformed-input throw path, matching this project's Foreign Data
discipline), the `high_res_holes` 64-bit path (**zero real fixtures found
this session** — must be synthetic-only until a real one turns up, flagged
the same way `PCOL`/`EXP2` were before real files were found for those),
and the fixed 8×8-quad triangle topology in isolation (a pure-geometry unit
test, no file parsing involved).

## References

- **wowdev.wiki**: `documentation/wowdev-wiki/md/ADT/v18.md` — `## MVER
  chunk`, `## MHDR chunk`, `## MCIN chunk`, `## split files (Cata+)`,
  `## MCNK chunk` + `### MCVT sub-chunk` + `### MCNR sub-chunk` +
  `### Terrain Holes`, `## MDID`, `## MHID`, `## MWDR (Shadowlands+)`, `##
  MWDS (Shadowlands+)`.
- **wow.export** (design ideas only, never code):
  `reference/wow.export/src/js/3D/loaders/ADTLoader.js` (573 lines) and
  `reference/wow.export/src/js/3D/exporters/ADTExporter.js` (1,648 lines)
  — both confirm the same split-file resolution shape (root+obj+tex+lod
  read as one logical unit) a real, shipped tool already settled on;
  worth a closer read at implementation time for how it merges the
  256-`MCNK` grid into fewer draw calls, without copying its code.
- **husk `src/` to reuse**: `src/chunk.hpp`/`chunk.cpp` (chunk walker,
  reused unmodified), `src/phys.hpp`/`phys.cpp` (chunk-tag-dispatch idiom,
  reversed-tag precedent), `src/gltf.hpp`'s `NamedMesh`/collision-mesh
  precedent (`M2_COMPLETENESS.md`'s Collision section — closest existing
  analogue to "positions + a fixed/derived index buffer, no bone
  weights").
- **Real corpus evidence**: `/media/luna/data/wow_export/world/maps/**`
  — 55,279 root `.adt` files (full-corpus counts for existence/split-file
  checks), 2,500-file random sample (seed 1234, reproducible) for the
  invasive per-`MCNK` byte decode (640,000 real `MCNK` chunks seen, 40,000
  given full sub-chunk decode). Exact counts cited inline per finding
  above.

## Environment note

The first attempt at this survey read every one of the 55,279 real root
`.adt` files in full — killed after 13+ minutes with zero output on this
session's storage (large files, `D`-state/disk-wait-bound, not CPU-bound).
Switched to `random.sample(all_files, 2500)` (Python stdlib, seeded for
reproducibility) for the per-`MCNK` decode pass, keeping full-corpus
`os.path.exists`-only checks (cheap stat calls, no file-content reads) for
the split-file existence counts in §5. A future implementation session
doing its own broader verification should expect the same storage
characteristic and budget accordingly — sample, don't full-scan, for any
check that reads whole ADT file bodies.

## Priority order

1. **§1/§2 (`MVER`/`MHDR`/`MCVT`)** — the actual geometry, the reason this
   file exists at all per `WORLD_COMPLETENESS.md`'s own framing.
2. **§3 (`MCNR`)** — trivial once §2 exists (same sub-chunk walk, paired
   1:1), no reason to sequence it later.
3. **§5 (split-file resolution)** — must exist before §2 can find its own
   data reliably in a real Cata+ install, even though `MCVT`/`MCNR`
   themselves stay root-resident (see §2's own clarification) — sequencing
   note, not a priority inversion: resolve the file set first, then read
   from the right one.
4. **§4 (terrain holes)** — small, but easy to get subtly wrong (wrong
   quad-to-bit mapping silently renders an extra triangle instead of
   throwing) — worth its own explicit test before considering §2 "done."
