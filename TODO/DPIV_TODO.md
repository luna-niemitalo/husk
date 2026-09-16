# TODO: crack `DPIV`'s real field semantics

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file. See `../WIKI_FINDINGS_HISTORY.md` §10 and
`../WIKI_FINDINGS/M2.md`'s Legion+ misc-chunk section for what's already
confirmed; this file only tracks the open semantic gap.

**Scope: investigative/no pipeline dependency.** Cracking an undocumented
chunk's field semantics via corpus statistics — independent of which
pipeline ships; `DPIV` is diagnostic-only regardless.

## Background

`DPIV` (>= War Within 11.1.7.60520) has no wowdev.wiki struct at all — the
wiki's own text is just "Unknown, seemingly always 32 bytes, mostly empty."
`dumpDpiv` (`src/cmd_dump.cpp`) already parses it structurally and
correctly (verified against all 2,632 real corpus hits): a real record
array, `chunk.size / 32` records, each record 8×float32, fields 4–7 always
zero in every real record seen. `DPIV` is diagnostic-only (`husk
dump-chunks`), never consumed by `husk export` — this is a real but
low-priority open question, not a stalled dependency.

Full field-cracking investigation (how fields 0-3 were narrowed down,
every hypothesis tried and falsified along the way): `CLAUDE_HISTORY.md`.

## Current resolution: what's settled, what's still genuinely open

**Settled, not worth re-investigating**:
- Struct shape: 8×float32 records, `chunk.size / 32` count, fields 4-7
  always zero (real padding).
- `field0`/`field1` (x/y): the model's own bounding-box-center X/Y
  coordinates — corpus-wide, tight (median 2.0%/0.2% off-center). Not
  independently meaningful positions; they reduce to geometry husk
  already has elsewhere.
- The zero-record ("placeholder") pattern is real and pervasive
  (61.6% of all records corpus-wide) but doesn't correlate with asset
  category/directory — nothing left to mine there.

**Still genuinely open, not just unattempted**:
- `field2` (z): a real placement value that does *not* reduce to
  bbox geometry the way field0/field1 do — sits near-or-below the
  model's own base (median 9.4% above Z-min, 15.9% fully below the
  bbox), consistent with a ground-contact/shadow-projection anchor, but
  *why* it varies the way it does (continuous distribution, no clean
  bimodal split, no directory correlation) is unexplained.
- `field3`: a small integer tag (0-3), weakly correlated with the
  placeholder pattern and with record position (never `3` at position
  0), but no clean rule found for what it actually encodes.

**Why there's no further corpus-*statistics* next step queued**: every
lever available — per-file, per-record, per-directory, per-position
statistical correlation — has been pulled, and the remaining unknowns
(`field2`'s exact role, `field3`'s tag semantics) didn't yield to any of
them. Closing this further needs a different evidence source than corpus
statistics alone can supply — item 1 below is exactly that.

1. **A visual grid render, categorized by `field2` and by `field3`, for
   Luna's own side-by-side eyeballing.** Corpus statistics found real
   distributions for both fields but no rule that explains them — the
   next real evidence source is a human looking at what these files
   actually *are*, grouped by category, rather than another number. Two
   separate grids (same method, different split):

   - **By `field2`** (the ground-contact-anchor lead): split into two
     screen segments — left: files where `field2` sits **fully below the
     model's own bounding box** (the 15.9% negative-offset group); right:
     files where it sits **at or above the base** (the remaining 84.1%,
     spanning the near-base cluster out through the long tail). Pick a
     representative sample per side (e.g. 12-20 files each, not the full
     2,632) and render each via the existing `husk export` + Blender
     render pipeline (`tools/corpus_scan_tasks/render_glb.py` + its
     `render_glb.blend` template), with the real `DPIV` point(s)
     themselves visualized as an overlay marker (an empty/gizmo at the
     point's own world-space position, same change-of-basis as husk's
     own `kWowToGltf`) so the point's placement relative to the mesh is
     visible in the same shot — overlay-marker support doesn't exist in
     the render script today and would need adding. Arrange each side's
     sample into one grid image (thumbnail montage) so the two
     categories are directly comparable at a glance.
   - **By `field3`** (the small integer tag, 0-3): same method, but one
     grid segment per real value seen (`0`/`1`/`2`/`3`) instead of a
     two-way split — four columns/quadrants, a representative sample per
     value, same point-overlay rendering, same montage-per-category
     assembly.

   **Gate: human-gated** — this step's whole point is Luna's own visual
   read of the grids, same discipline every other "does this look right"
   step in this project uses (billboard ground-truth, the animation
   visual pass, etc.); assembling the renders/grids themselves is
   independent, scriptable work, but the actual pattern-spotting isn't.

## Artifacts already on hand (don't need to be regenerated)

- `dpiv_files_for_exploration.txt` — full list of 2,632 real `DPIV`-bearing
  file paths.
- `m2_unknown_chunks_report.json` — per-file hex dump (`WFV1`/`WFV2`/`DPIV`/
  `AFRA`/`PCOL`), first 96 bytes of each hit; enough to redo the field
  decode above without re-scanning the corpus.
