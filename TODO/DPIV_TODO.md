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

## This session's follow-up: a first real geometry cross-reference (step 1), plus a new elevation lead

Picked up "Concrete next steps" item 1 directly. Exported `pa_kite_lamp_
creature.m2` (single DPIV record, `field0=-0.0, field1=2.798,
field2=22.038`) to `.glb` via `husk export` and cross-referenced the DPIV
point against real vertex/bone positions (scratchpad-only Python, not
committed — parses the `.glb`'s own binary chunk directly, applies the
same `kWowToGltf` (x, z, -y) change-of-basis husk itself uses so the
comparison is apples-to-apples).

**Result: the point sits ~2.5-2.8 units outside the mesh's own bounding
box** — specifically 2.54 units below the lowest real vertex along one
axis (mesh Y range in glTF-space, i.e. WoW's own Z/height axis, is
24.58–32.28; the DPIV point's corresponding coordinate is 22.04) — not on
the mesh surface, not coincident with any vertex. Nearest bones
(`HandRight`/`HandLeft`/`SpellLeftHand`/`SpellRightHand`, all ~2.89 units
away) are *farther* than the nearest raw vertices (~2.77 units), so this
one file gives no evidence of a specific-bone attachment either — a real,
if narrow, negative result for the "pinned to a named bone" hypothesis.

**New, better lead, found by accident while picking a second file to
cross-check**: comparing `field1` (the coordinate this file's own doc
comment calls "y", the middle of the three point fields) across four
real files —

| file | real-world description | field1 |
|---|---|---|
| `pa_kite_lamp_creature.m2` | a lamp (elevated fixture) | **2.798** |
| `fx_breakscrollseal_precast.m2` | a ground-cast spell effect | 0.000 |
| `ao_banner02.m2` | a standing ground banner | 0.000 |
| `dr_bench_01_nosound.m2` | a ground-sitting bench | 0.000 |

— exactly `0.0` for every one of the three ground-level/ground-cast props,
and a real, non-zero, positive value for the one elevated fixture. Small
sample (4 files, picked by hand, not a corpus scan) but a clean,
falsifiable pattern: **`field1` may be a height/elevation value relative
to the object's own ground contact point**, consistent with (though not
identical in axis-labeling to) this file's own already-documented
"field 1 constant within a file, plausible ground-plane" observation
above — this adds *why* it might vary file-to-file (elevated vs. grounded
prop), which the original observation didn't have.

**Follow-up, same session: ran that test, and it corrected the hypothesis
rather than confirming it.** `husk info` already reports each M2's own
header-level `bounding_box` (`M2Bounds.extent`, no per-vertex export
needed) — pulled it for all four files above and checked each DPIV field
against its own axis's bbox range, not just whether `field1 == 0`:

| file | field vs. bbox-center offset (x / y / z) |
|---|---|
| `pa_kite_lamp_creature.m2` | 11.1% / **4.1%** / 83.0% |
| `fx_breakscrollseal_precast.m2` | 19.6% / **0.0%** / 23.0% |
| `ao_banner02.m2` | 36.5% / **0.0%** / 3.4% |
| `dr_bench_01_nosound.m2` | 2.9% / **0.3%** / 49.3% |

(percentage of that axis's own bbox width the field sits from the exact
midpoint.) **`field1` lands within 0–4.1% of the model's own Y-axis
bounding-box center in all four files** — including `pa_kite_lamp`, whose
`field1 = 2.798` is not "elevation off the ground" (its own Z bbox min is
24.58, nowhere near 2.798) but *is* almost exactly its Y-bbox midpoint
(2.958, 4.1% off). `field0`/`field2` show no comparable pattern — some
land near their axis's center, some near the min, most just "somewhere
within range," inconsistent file to file (see the two-Z-outlier cases,
`pa_kite_lamp` 83% and `dr_bench_01` 49%).

**Corrected reading**: the earlier "`field1` is a ground-relative
elevation value, `0` for grounded props" framing was a coincidence of
this specific 4-file sample (three ground props whose own Y-bbox happens
to straddle zero) — the real, better-supported pattern is **`field1` is
close to the model's own Y-axis bounding-box center**, which for a
symmetric ground prop *is* usually near zero anyway, without needing an
"elevation" mechanism at all. This is a more mechanically plausible
reading too: a per-model "center-ish anchor point," roughly inside the
model's own volume, fits a tool-authored placement/attachment point far
better than a hand-tuned elevation value would. `field0`/`field2`'s own
weaker, inconsistent bbox-relationship (sometimes center, sometimes near
an extreme) is still unexplained and worth investigating on its own,
possibly by axis-pair (are field0/field2 jointly closer to some other
real, non-center landmark, e.g. a specific bone's own local position?).

**Caveat, stated honestly**: the geometric cross-reference above assumes
DPIV's three point fields are in the *same* per-axis order/convention as
M2's own vertex positions (and thus subject to the same `kWowToGltf`
transform) — genuinely unconfirmed, since DPIV has no wowdev.wiki struct
to check this against. If that assumption is wrong, the "2.54 units below
the mesh" distance is still real (the raw-space math doesn't depend on
which axis is "up"), but which axis it's offset along could be
mislabeled.

## Concrete next steps (in rough order of expected payoff)

1. ~~Cross-reference DPIV points against the model's own geometry~~ —
   **partially done this session**, one real file (see above): not on the
   mesh surface, not closer to any named bone than to raw vertices. A
   single file isn't a corpus-scale answer — worth a few more hand-picked
   cross-checks (an elevated prop and a ground prop each) before treating
   "not on the surface" as general, but the negative "not bone-pinned"
   result and the elevation lead below are real enough to act on.
2. ~~Run the field1-vs-own-bounding-box test~~ — **done this session**,
   4-file hand sample: corrected the "elevation" hypothesis to "`field1`
   sits within ~4% of the model's own Y-axis bbox center" (see above) —
   much stronger fit, no exceptions in this sample. **New next step this
   unlocks**: verify the corrected "`field1` ≈ Y-bbox-center" hypothesis
   at real corpus scale (all 2,632 DPIV files) — cheap, `husk info`
   already exposes each file's own `bounding_box` with no `.glb` export
   needed; would also be the natural place to investigate `field0`/
   `field2`'s own weaker, inconsistent bbox relationship (center some
   files, near-extreme others) rather than leaving it as an open loose
   end.
3. **Check whether same-file point sets close into a polygon.** The
   partial constant-Y pattern (2/3 above) is suggestive of a flat footprint
   — check real inter-point distances/angles within a file to see if 3–4
   points form a plausible quad/triangle (a decal projection area, a
   trigger volume, a particle-spawn footprint) rather than being scattered.
4. **Re-examine field 3 as a category enum, not an index.** With only 4
   values (0–3) and no clean ordering, check for correlation with anything
   else per-record-position-independent — e.g. does a specific value always
   pair with a specific relative position (first vs. last point in a
   footprint), or with specific filename patterns (fire/torch vs. window vs.
   structure doodads, the same directory split that cracked `DETL`'s
   `flags` bit this session)?
5. **Full-name audit of all 2,632 hits.** Only skimmed a handful of
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
