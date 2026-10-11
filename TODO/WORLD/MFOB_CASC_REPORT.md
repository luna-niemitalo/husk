# MFOB: full-corpus negative — report for a CASC-side check

**For:** Luna, to hand to the casc tooling side.
**Date:** 2026-10-11.

## What MFOB is

A WMO root chunk the wiki (`WMO.md`, "MFOB" section) version-gates to
**≥ Midnight (12.1.0.68209)** and documents with **no struct at all** —
just the version box, then the page jumps to the next heading. It sits in
the fog family (`MFOG`/`MFED`/`MFOB`), so it is presumably per-fog-entry
extension data, but nothing about its shape is public.

## What was checked

A full-corpus chunk-table walk over **every `.wmo` file in
`/media/luna/data/wow_export` — 86,134 files** (roots and groups,
including sub-chunks nested inside `MOGP`), via
`tools/corpus_scan_tasks/rare_wmo_chunk_task.py` on the corpus scan
framework. The same scan simultaneously found 2,736 `MOLP`-bearing and
218 `MOLS`-bearing files, so the walker demonstrably reaches rare chunks;
a zero is a real zero, not a scanner miss.

## Result

**`MFOB`: 0 of 86,134 files.** Same result as the earlier 4,000-file
sample, now at full-corpus strength.

## Why this is surprising

This corpus **is** a Midnight extraction: game version 12.1.0, proven by
its DB2 layout hashes against WoWDBDefs (see the project hazard note —
`buildinfo.json`'s `clientBuild: 61621` is stale and was not trusted).
The wiki's version gate (12.1.0.68209) is satisfied by the corpus's
nominal version, so "the corpus simply predates the chunk" is not an
obvious explanation unless the chunk landed in a *later 12.1.0 build*
than this extraction (the gate names a specific build number; the
extraction's true build is not independently pinned beyond "12.1.0").

## What CASC could settle

1. **Does any file in the live 12.1.x (or PTR/beta) CDN contain the
   `MFOB` tag at all?** A tag-grep over root WMOs in a current
   extraction newer than this corpus's snapshot would answer whether the
   chunk shipped after this extraction was taken.
2. If hits exist: pull 2–3 of them so husk can do the usual stride/
   field decode (the same method that just resolved `MPVD`/`PVMI`/
   `VFE2`).
3. If the live CDN also has zero hits: the wiki's section is
   documentation of an unshipped/internal feature, and this report plus
   a `WIKI_FINDINGS/WORLD.md` note closes the item as
   "watch-only, confirmed absent through 12.1.0".

## Answer from the casc-tool store (2026-10-11)

Question 1 is settled. Build 12.1.0.69933 (`/media/luna/wow_export/live`):
**225 of 12,824 root WMOs carry an `MFOB` tag, and all 225 are 0 bytes.**
So the chunk did ship after the old extraction, but as an empty
placeholder (e.g. `world/wmo/expansion11/troll/12tr_amani_balcony01.wmo`,
`world/wmo/dungeon/lifepoolsdungeon/10du_lifepoolsdungeon.wmo`). Question
2 has nothing to pull yet.

## Disposition in husk

No parser work possible until a build gives `MFOB` a body; husk skips the
empty chunk and those roots export clean. Re-run the root tag-grep on each
new stored build. Tracked in `FOG_VOLUMES_TODO.md` item 1 (watch-only).
