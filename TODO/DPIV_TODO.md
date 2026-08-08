# TODO: crack `DPIV`'s real field semantics

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file. See `../WIKI_FINDINGS_HISTORY.md` §10 and
`../WIKI_FINDINGS/M2.md`'s Legion+ misc-chunk section for what's already
confirmed; this file only tracks the open semantic gap.

## Background

`DPIV` (>= War Within 11.1.7.60520) has no wowdev.wiki struct at all — the
wiki's own text is just "Unknown, seemingly always 32 bytes, mostly empty."
`dumpDpiv` (`src/cmd_dump.cpp`) already parses it structurally and
correctly (verified against all 2,632 real corpus hits,
`dpiv_files_for_exploration.txt`): a real record array, `chunk.size / 32`
records, each record 8×float32, fields 4–7 always zero in every real
record seen. That part is done. **What the first four fields actually
represent is not** — `dumpDpiv`'s own doc comment still says "not yet
field-mapped," correctly.

Size distribution across the 2,632 files (from `m2_unknown_chunks_report.json`):
32 bytes/1 record (2,372 files), 64/2 records (209), 96/3 records (43),
128/4 records (8) — so this is overwhelmingly a 1–4-record array, not
unbounded.

## What this session's investigation found (real corpus data, not guesses)

Decoded every field across all 2,632 files / 2,943 records directly from
`m2_unknown_chunks_report.json`'s stored hex:

- **Fields 0–2 read as plausible 3D points**, not junk: real magnitudes
  (roughly ±40 units), not denormals or obviously-wrong bit patterns.
- **Field 3 is not a real float** — every one of 2,943 records has a raw
  uint32 value in `{0, 1, 2, 3}` (bit patterns 0/1/2/3, which decode as
  float denormals near zero — `dumpDpiv`'s doc comment already flagged this
  suspicion; confirmed here). This reads as a small integer tag/type field
  mistyped as float in whatever tool generated the wiki's stub entry, not a
  genuine float.
- **Field 3 is *not* a clean sequential per-record index**, though — tested
  directly: only 56/260 multi-record files have `field_3 == [0, 1, 2, ...]`
  in record order; 204/260 don't (e.g. `[1, 0, 1]`, `[0, 2, 1]`, `[2, 0,
  3]`). So it's closer to a small category/type enum (0–3) than an ordinal
  index — the earlier "maybe it's an index" read from inspection alone
  doesn't survive a full-corpus check.
- **Partial pattern**: in 183/260 multi-record files (~70%), field 1 (`y`)
  is identical across every record in that file while fields 0/2 (`x`/`z`)
  vary — consistent with a set of points on one ground-plane/height per
  file, but not universal (77/260 break it), so this is a lead, not a
  confirmed shape.
- Fields 4–7 are `0.0` in all 2,943 records without exception — real,
  reserved-looking padding, not silently-corrupted data.
- Filenames skew toward `models/unknown/unk_exp11_*` (new/still-unreleased
  content) and decorative doodad/spell-FX models (a chandelier, a crystal-
  tuning-fork spell effect, an undead campfire) — consistent with recent
  content still missing wiki documentation, not a parsing gap on husk's
  side.
- `reference/wow.export` doesn't parse `DPIV` at all (zero references) —
  no independent corroboration available from that source either; this
  file's own findings rest entirely on real corpus data.

## Concrete next steps (in rough order of expected payoff)

1. **Cross-reference DPIV points against the model's own geometry.**
   Transform each record's `(field0, field1, field2)` into the same space
   as the model's vertex positions / bone positions and check proximity —
   do these points sit on the mesh surface, at a bone pivot, or off in
   space entirely? A hit against a specific bone or geoset would be a much
   stronger lead than field statistics alone.
2. **Check whether same-file point sets close into a polygon.** The
   partial constant-Y pattern (2/3 above) is suggestive of a flat footprint
   — check real inter-point distances/angles within a file to see if 3–4
   points form a plausible quad/triangle (a decal projection area, a
   trigger volume, a particle-spawn footprint) rather than being scattered.
3. **Re-examine field 3 as a category enum, not an index.** With only 4
   values (0–3) and no clean ordering, check for correlation with anything
   else per-record-position-independent — e.g. does a specific value always
   pair with a specific relative position (first vs. last point in a
   footprint), or with specific filename patterns (fire/torch vs. window vs.
   structure doodads, the same directory split that cracked `DETL`'s
   `flags` bit this session)?
4. **Full-name audit of all 2,632 hits.** Only skimmed a handful of
   filenames this session (`dpiv_files_for_exploration.txt` has the full
   list) — a systematic pass grouping by directory/naming convention (the
   same method that found `DETL`'s flags-vs-light-prop correlation) might
   turn up a content-type split worth checking against field 3's values.

## Artifacts already on hand (don't need to be regenerated)

- `dpiv_files_for_exploration.txt` — full list of 2,632 real `DPIV`-bearing
  file paths.
- `m2_unknown_chunks_report.json` — per-file hex dump (`WFV1`/`WFV2`/`DPIV`/
  `AFRA`/`PCOL`), first 96 bytes of each hit; enough to redo the field
  decode above without re-scanning the corpus.
