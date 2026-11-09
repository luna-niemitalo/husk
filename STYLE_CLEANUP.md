# STYLE_CLEANUP.md

Open punch list from style/compliance audits against `~/docs` (Luna's
rules: READABILITY, FOREIGN_DATA, CODE_COMMENTS, FILE_SIZE, CLI).
Fixed items get removed outright — git history is the record, not this
file. Renamed from `CLEANUP.md` 2026-09-16 — the old name collided with
the unrelated `TODO/CLEANUP_TODO.md` (a project-specific code-hygiene/
follow-up list, since deleted and merged into `TODO/INVESTIGATIONS_TODO.md`
once it was down to one item), causing real confusion about which file a
"cleanup" ask meant. No content relationship between the two.

2. **`husk db2-build` / `knowledge.sqlite` — open design decision, not yet
   actionable.** A SQLite store ingesting 7 DB2 tables was built to
   consolidate scattered DB2 lookups, but the triggering problem
   (object-skin texture resolution) was fixed by a 20-line local fallback
   tier before the database was finished. It sits disabled today (no real
   export path passes `--knowledge-db`) and is documented as producing
   known-wrong same-slot collisions. Not a clean delete-candidate: the
   disambiguator it was missing (`ItemAppearance` existence-checking a
   candidate `ItemDisplayInfoID`) now exists in the tree via the unrelated
   `itemappearance_db2.cpp` (built later for equipped-gear resolution). See
   `TODO/TEXTURE_POOL_RECALL_TODO.md`'s "cross-validate DB2 object-skin
   candidates against the fuzzy pool" open item (merged in from the
   now-deleted `KNOWLEDGE_BASE_DESIGN.md`) for the cross-validation design
   that could make this join trustworthy instead of discarding it. Until
   that's built (or the idea is rejected), the subsystem stays exactly as
   disabled/non-default as it is now — don't wire `--knowledge-db` into any
   real export path on the strength of the idea alone.

3. **Finish the `// TODO: Remove` sweep.** Each marker is history
   narrative: delete it, keep any why/hazard, move a fact found nowhere
   else into WIKI_FINDINGS/DESIGN.md (`git grep -n "TODO: Remove" -- src
   tests`). Remaining: `cmd_dump.cpp` (1), `cmd_info.cpp` (3),
   `tests/test_gltf_math.cpp` (3).

4. **Foreign-data boundary gaps (FOREIGN_DATA.md), found in the 2026-11
   contract pass, each larger than a local fix:**
   - `db2.cpp` `decodeField`/`decodeOffsetMapRecord` re-check per record
     what `parse` should check once (fieldStructures coverage, element
     width, `readBits` 1..64); `db2.hpp` says nothing downstream re-checks.
     Moving them into `parse` changes when a bad file fails.
   - `db2.cpp` `rowIdForRecord`/`recordId` fall back to the record index
     when `header.idIndex` is out of range; `idIndex` is never validated.
   - `db2.cpp` `decodeOffsetMapRecord`'s CommonData case scans `commonData`
     linearly instead of using `commonDataByField`: two implementations of
     one lookup.
   - `dbd.cpp` `resolveFieldNames` returns a bare nullopt on a layout
     mismatch, no expected/actual (needs a return-type change).
   - `listfile_mmap_index.cpp`: `cumOffsets_`, hash slot offsets/lengths
     and `pathOrder_` rows are never checked against the blob or entry
     count at load; a corrupt cache can read out of bounds (an O(n) scan in
     `tryLoad` would fix it). Its header's `MmapFile` doc says an open
     failure is an I/O error, but `tryLoad` treats it as a cache miss.
   - `adt.cpp`: MWDR ranges are validated lazily in `activeDoodadSets`, not
     in `parseObj`; in `parseMh2o`, `end - off` can underflow for a vertex
     offset past the chunk (still throws, with a misleading message).
   - `listfile.cpp` skips malformed lines silently (DESIGN.md: intended);
     FOREIGN_DATA's no-silent-tolerance rule wants at least a count.
   - `m2_canon_input.cpp:246`: an interior re-check of what `assembleMesh`
     already threw on.
