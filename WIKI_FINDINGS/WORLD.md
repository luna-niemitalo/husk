# wowdev.wiki findings — WMO/WDT/WDL/PM4/PD4 (world-format expansion)

Current, correct facts only. Full evidence trail: `../WIKI_FINDINGS_HISTORY.md`
§15. The full struct listings, C++ data-model sketches, and test plans live in
the eleven companion `*_TODO.md` files under `TODO/WORLD/`. This page records
what would otherwise be lost once those are implemented and deleted. Formats
split out into their own file as their parsers land: ADT is in `ADT.md`
(`src/adt.cpp`); WMO, WDT, WDL, PM4 and PD4 stay here. WMO root/group parsing
exists (`src/wmo.cpp`, `tests/test_wmo.cpp`); nothing exports it yet.

---

## Chunk-tag byte reversal is universal — verified

WMO, ADT, WDT, and WDL all reverse chunk-tag bytes on disk — same
convention `.phys` uses (`PHYS.md`), opposite of M2's own inline chunks.
None of the relevant wiki pages state this explicitly; two independent
sibling investigations each caught a real scanner bug from forgetting it
before trusting their own results.

## WDT — verified

`_occ.wdt`'s occlusion heightmap is really `int16_t[17*17]` (578 bytes), not
the documented `(17*17+16*16)` (1,090 bytes) — confirmed across all 959
real `_occ.wdt` files. The root `.wdt`'s global single-WMO `MODF` record can
omit `MWMO` entirely when `flags & 0x8` is set (35/228 real global-WMO
files), with `nameId` then holding a real FileDataID directly. `MAI2`
(wiki: "unshipped") is real in 2 files.

## ADT — see `ADT.md`

Split out once `src/adt.cpp` landed: `ADT.md` holds every verified ADT fact,
including the planning-pass ones that used to live here.

## WMO — verified

- `GFID` (LOD-tier group-file resolution) is **row-major**:
  `index = lodTier * nGroups + groupIndex`, zero meaning "no file" — not
  stated as a formula on the wiki.
- `MOGX` is a fixed **256-byte** (64×`uint32_t`) chunk, not the wiki's
  stated 4-byte "one single value" — only the first slot is ever non-zero,
  the rest is padding. Both `MOGX` and `MOQG` are group-level, not
  root/group as an earlier internal note stated.
- `MOMX` (wiki: "just a guess," one named example) is present in **30.5%**
  of real root WMO files (3,931/12,869), not a rare one-off — its record
  count exactly equals `MOHD.nTextures`. Per-field semantics remain
  unresolved (not FileDataID references, despite the wiki's guess).
- `MODI`'s real entry count can **exceed** `MOHD.nDoodadNames` (14.5% of
  5,000 real roots) — always ≥, never less; size the array off the chunk's
  own byte length, never trust the header field alone.
- Shadowlands+ `MWDR`/`MWDS` doodad-set-activation indirection verified
  end-to-end against two real placements activating different multi-set
  combinations into the same `MODS` table.
- `MPBV`/`MPBP`/`MPBI`/`MPBG` confirmed genuinely absent corpus-wide (0 of
  71,929 real group files) — the wiki's rarity claim holds at full scale.
- `MOVT`/`MONR` are plain **(X, Y, Z), Z up**, the same frame as M2, despite
  the wiki's "(X,Z,-Y) order" note. `guardtower_001.wmo` spans ~28 × 28 on
  the first two components and 34 on the third; `MODD` rotations of the
  same building turn about the third axis. Group vertex ranges match the
  `MOHD`/`MOGP` bounding boxes component for component.
- `MOHD.nDoodadDefs` can disagree with `MODD`'s real length too
  (`guardtower.wmo`: header 173, `MODD` 170 records). Size every record array
  off its chunk.
- Multiple `MOTV` and `MOCV` chunks are common, not edge cases: 36% and 26%
  of 3,487 sampled group files (seeded 4,000-file sample of `world/wmo/`).
  All 4,000 parse cleanly with `src/wmo.cpp`, and per-triangle `MOPY`
  counts always equal `MOVI / 3`.
- `MOBA`'s `material_id_large` flag (0x02 at 0x16) must decide which field
  is the batch's material: flagged batches leave `material_id` at 0.

## Liquid/lighting/fog — verified

`MH2O` covers 67.1% of real ADT tiles (vs. WMO's own `MLIQ` at 2.1%) — clear
implementation priority. Legacy `MCLQ` confirmed genuinely absent (0/4,000).
`MOLS`/`MOLP`/`MLSS`/`MLSP`/`MPVR`/`MAVR`/`MBVR`/`MFVR`/`MNLR` are all
**group**-file chunks (nested in `MOGP`), not root-level, despite reading as
root-level candidates from the wiki's own page layout. A wholly undocumented
chunk, **`VFE2`** (176 bytes), was found in real `_fogs.wdt` files by two
independent investigations — real, present, absent from `WDT.md` entirely,
not yet reverse-engineered. `MPVD`'s struct remains unresolved.

## Collision & culling — verified

WMO's `MOBN`/`MOBR` BSP collision (48% of real group files) maps almost
directly onto husk's existing M2 collision-mesh pipeline, needing only
`MOBR`'s one indirection (triangle index into `MOVI`, not raw vertex).
Portal culling (`MOPV`/`MOPT`/`MOPR`/`MOPE`): **checked directly, not
assumed** — Blender has no cell-and-portal visibility-culling mechanism at
all (confirmed against Cycles' Holdout shader and Ray Visibility toggle,
both ruled out for specific documented reasons). `wow.export` itself has
zero code path for `MOBN`/`MOBR` — husk implementing this would be new
ground, not catching up to existing tooling.

## Gameplay/misc metadata — verified

Four items promoted from a blanket "n/a" to real `extras` candidates after
being checked for real value rather than dismissed as invisible-therefore-
unimportant: `MDAL` (WMO ambient-color override — affects rendered
lighting), `MCSE` (ADT sound-emitter placement — same shape as M2's own
ribbon/particle anchor), `MCSH` (ADT baked shadow bitmap — real bimodal
shadow-density signal), `MCMT` (ADT per-layer material-ID override). `MOQG`
(WMO per-face ground type) was reconsidered under the same lens and
confirmed to genuinely belong at n/a (audio-only, no visual consequence).

## PM4/PD4 — verified, structural negative

Genuinely never shipped to the client — not an extraction gap like
`EXP2`/`PFDC`. Zero `.pm4`/`.pd4` files anywhere in a full 3,190,909-file
live CASC storage sweep. Both wiki pages' "not supposed to be shipped to
the client" text is literally true. `wow.export` has zero PM4 handling and
only a raw-byte PD4 pass-through — real support here would be genuinely
novel. The "hidden by default" design question (glTF's `KHR_node_visibility`
vs. Blender's lack of support for it) is left open for a human decision.
