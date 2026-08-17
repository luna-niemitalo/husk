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

**Follow-up, same session: ran the real corpus-scale test this section's
own "next step" called for** (all 2,632 DPIV files, all 2,943 records —
`husk info`'s already-verified `bounding_box` field, offset `0x0A0` in the
MD20 blob, parsed directly rather than shelling out per file; zero
unreadable/unparseable files). The 4-file hand sample generalizes, and
sharpens into a real, coherent shape:

- **X and Y are both tightly centered on the model's own footprint**: Y
  (field1) has a median offset of just 0.2% from its own bbox center,
  91.4% of records within 10%; X (field0) median 2.0%, 77.0% within 10%.
  Both far stronger at corpus scale than the 4-file sample suggested for
  X specifically.
- **Z (field2) is not centered at all** — median 45.4% off-center, only
  13.3% within 10%. Confirms the earlier geometric cross-reference's
  `pa_kite_lamp` result (a real outlier on the *center* test) was the
  corpus-wide norm, not a fluke.
- **Z instead sits consistently near the model's own base**: re-measured
  as offset from Z-*min* (not center) — median **6.1%** of the model's
  own Z-range above its own lowest point, 65.9% of records within ±20% of
  the base, and **10.1% sit fully below the model's own bounding box
  entirely** (negative offset) — the exact shape `pa_kite_lamp`'s own
  single-record cross-reference found by hand (2.5 units below its own
  mesh).

**Real, corpus-validated shape, not just a lead anymore**: DPIV's point
is `(X-center, Y-center, near-or-below-base-Z)` — i.e. a point roughly
below the object's own horizontal (footprint) center, at or near ground
level. That's exactly the profile a **ground-contact / shadow-projection
anchor point** would have (raised as a hypothesis earlier in this file,
now with real corpus-wide support, not just plausibility). Field-semantic
question for `field0`/`field1` themselves is now close to settled (bbox-
center coordinates, not independently meaningful positions); `field2`
remains the interesting one — a real placement value, not a coordinate
that reduces to bbox geometry alone.

**Caveat, stated honestly**: the geometric cross-reference above assumes
DPIV's three point fields are in the *same* per-axis order/convention as
M2's own vertex positions (and thus subject to the same `kWowToGltf`
transform) — genuinely unconfirmed, since DPIV has no wowdev.wiki struct
to check this against. If that assumption is wrong, the "2.54 units below
the mesh" distance is still real (the raw-space math doesn't depend on
which axis is "up"), but which axis it's offset along could be
mislabeled.

**Follow-up, same session: the same-file-point-sets-close-into-a-polygon
question run at corpus scale, plus a real structural finding it depended
on.** Sampled 6 real multi-record files by
hand first and immediately noticed something the "polygon footprint"
framing hadn't accounted for: several files have one record that's
*exactly* `(0.0, 0.0, 0.0)` while a sibling record in the same file is a
real, distinct point — not two real geometric points at all, one of them
a placeholder. Checked at full corpus scale (all 260 multi-record files):

- **41/260 have *every* record exactly `(0,0,0)`** — fully degenerate,
  no real point data at all (same "genuinely empty, not corrupted" shape
  `fields 4–7` already showed).
- **83/260 (32%) have *some but not all* records exactly zero** — a real
  placeholder pattern, skewed toward record 0 being the zero one (64/83)
  but not exclusively (position 1: 18/83, position 2: 4/83) — so "record
  0 is always the placeholder" isn't quite right either, just the most
  common shape.
- **136/260 (52%) have no zero records at all** — genuinely all-real
  multi-point data, the population item 3's own polygon question is
  actually about (mixing in the placeholder-bearing files would have
  corrupted any inter-point-distance measurement).

Ran the polygon-footprint check on exactly those 136 real-multi-point
files: for each, `max(inter-point distance) / model's-own-bbox-diagonal`.
**Median 30.3% of the model's own diagonal; 0% ever exceed 100%** (points
never scatter wider than the model itself) but only 19.9% land under 10%
(a genuinely tight cluster) and 43.4% under 25%. **Real finding, but it
doesn't cleanly support the tight "3–4 points form a footprint quad"
picture** — points stay bounded within the model's own volume (consistent
with the ground-anchor reading above) but are moderately, not tightly,
spread — more consistent with "several independent placement points
scattered around the object" (e.g. one per torch/light/attachment on a
multi-part prop) than one small decal/trigger footprint polygon.

## Concrete next steps (in rough order of expected payoff)

1. **Histogram `field2`'s offset-from-Z-min distribution directly**, rather
   than the two summary buckets used so far (median 6.1% above Z-min,
   65.9% within ±20%, 10.1% fully below) — is it *always* within some
   small fixed tolerance of the base for a "pinned to the ground" subset,
   with a separate, different pattern (fixed offset, or bone-relative
   instead of bbox-relative) for the outliers? Cross-reference outliers
   against filenames/content type the way `DETL`'s `flags` bit was
   cracked.
2. **Check whether the zero-placeholder-record pattern correlates with
   `field3`'s own value** (e.g. "`field3 == 0` means unused slot") —
   cheap to check against the already-identified 83-file placeholder set,
   and would help interpret `field3` (item 3 below) at the same time.
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
