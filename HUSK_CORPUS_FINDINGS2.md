# husk corpus findings, round 2 — 130k .m2 re-sweep (post-fix verification)

## Context

Same methodology as `HUSK_CORPUS_FINDINGS.md` (kept as-is for reference, not
rewritten) — `husk` run against the full WoW export corpus at
`/media/luna/data/wow_export`, 130,575 `.m2` files, via `tools/corpus_test.py`
+ `tools/corpus_summarizer.py`, producing `summary.txt`/`failure_codes.txt`/
`failures.txt`/`failures_unique.txt`. See the old doc's "The four output
files" section for what each one means — that explanation still applies
unchanged and isn't repeated here.

Timing matters for reading this doc correctly: `HUSK_CORPUS_FINDINGS.md` is
dated 2026-07-30 20:11; this sweep's four output files are all dated
2026-07-31 07:23 — i.e. **this is the sweep that ran after the fixes the old
doc's own "what's actually worth fixing" section drove** (`CORPUS_TODO.md`'s
items 1, 3b, 4, and the `.skin`-not-found buffer-truncation bug, all closed
per `CLAUDE.md`'s Resume log). This doc reports what's left now, not a new
independent investigation.

(`corpus_report.json` in the repo root is a stray file from an earlier,
unrelated run — dated 2026-07-27 — and isn't part of this sweep; ignored.)

## Headline: the fixes worked

| test | old (2026-07-30) | new (2026-07-31) | change |
|---|---|---|---|
| `export` pass | 126,098 / 130,575 (96.6%) | 129,912 / 130,575 (99.49%) | failures 4,477 → 663 (6.8x fewer) |
| `dump-chunks` pass | not separately reported | 130,241 / 130,575 | 334 failures, same 0-byte-file cause as `export`/`header` |
| `header` pass | not separately reported | 130,241 / 130,575 | 334 failures, same cause |

`fidelity`/`finite`/`mesh_completeness` (129,914–129,915 files each) only run
against files that exported successfully, so their denominators are slightly
below `export`'s pass count — expected, not a bug (same short-circuit
behavior the old doc's `summary.txt` note already explains). All three sit at
0 failures.

## Confirmed fixed: all four of the old doc's top action items

1. **`FAIL-0001` old (3,807 files, "mesh 0's primitive 0 indices must not be
   empty")** — zero occurrences anywhere in this sweep's failure set. Matches
   the "zero meshes" fix (`writeGlbMulti` now allows an empty mesh list when
   a real skeleton exists) already recorded in `DESIGN.md`/`CLAUDE.md`.
2. **`.skin`-not-found message truncation** — the old doc found ~14 different
   `FAIL-00NN` codes for what was really one bug, differing only in where a
   fixed-size buffer cut the string. This sweep's equivalent bucket is a
   single code (`FAIL-0002`, below) with one full, untruncated message —
   confirms the buffer bug is gone, not just coincidentally absent this run.
3. **Exact off-by-one vertex-index bug** (`references vertex N but only has N
   vertices`, index == count exactly) — zero occurrences. Consistent with the
   2-digit-suffix same-basename `.skin`-matching fix (`CORPUS_TODO.md` #3b)
   removing the collision that produced this pattern.
4. **Duplicate/near-duplicate-timestamp animation keyframes** (the loop-seam
   shape the nudge-repair targeted) — zero occurrences of that specific
   pattern. Fixed.

## What's left: 663 export failures, fully accounted for

Every one of the 663 failures this sweep found falls into a bucket already
described in `README.md`'s "known extraction gap, not a husk bug" paragraph
(lines ~158–182) — cross-checked failure by failure, not just by category
name:

| bucket | count (real corpus) | count (test_data fixtures) | total |
|---|---:|---:|---:|
| 0-byte `.m2` files | 334 | 0 | 334 |
| `SFID`-declared `.skin` never extracted | 266 | 1 (`trade_archaeology_gemmeddrinkingcup_b.m2`) | 267 |
| `materialIndex`/`textureComboIndex` one past the end | 21 | 1 (`polearm_2h_dragondungeon_c_01.m2`) | 22 |
| `.skin`/`.m2` vertex-count mismatch (quest-helm/raid-helm families) | 37 | 0 | 37 |
| new/unclassified animation anomalies (see below) | 3 | 0 | 3 |
| **total** | **661** | **2** | **663** |

That total reconciles exactly against `summary.txt`'s `export: 129912 | 663`.

- **334 zero-byte files** — matches `README.md`'s own stated "334 genuinely
  0-byte `.m2` files" exactly. Extraction-completeness gap, not a husk bug.
- **267 unresolvable `.skin`** — matches `README.md`'s "~267" almost exactly
  (266 real + the 1 test fixture below = 267 on the nose). Same known
  extraction gap.
- **22 `materialIndex`/`textureComboIndex` out-of-range** — `README.md`
  currently says "confirmed uniformly across 16 real collections/recolor item
  variants"; this sweep finds 21 real-corpus files (16 `materialIndex` +
  5 more, 3 of them `textureComboIndex`, that weren't in the original
  16-file check) plus 1 test fixture. Same bug class and same "genuinely bad,
  stale shared batch data" conclusion — worth a small `README.md` number
  refresh (16 → 21) next time that section is touched, but not a new
  finding or a husk defect.
- **37 vertex-out-of-range `.skin`/`.m2` mismatches**, two model families:
  `helm_cloth_questbloodelf_b_01_*` (16 coded + 7 singleton, all referencing
  up to vertex 1129 against ~600 real vertices — roughly double) and
  `helm_leather_raidroguenerubian_d_02_*`/`_d_01_*` (14 singleton, similar
  relative gaps, plus 2 `textureComboIndex` singletons on the `_d_01_*`
  siblings). Matches `README.md`'s "confirmed on real quest-helm and
  raid-helm variant files, roughly double the model's real vertex count"
  description. One outlier worth a note, not a new bug: `helm_cloth_
  questbloodelf_b_01_dr_m.m2` references up to vertex 1129 against only 368
  real vertices (a ~3x gap, not ~2x like its siblings) — same failure shape,
  just a worse mismatch on this particular variant.

**Two of the 663 are husk's own `test_data/` fixtures, not real corpus
data**, and printed with relative paths (`./test_data/...`) rather than
absolute `/media/luna/data/wow_export/...` ones — the giveaway that they
fell outside `--root`'s `wow_export` tree despite `discover_files`'s
`root.rglob(...)` walk (`tools/corpus_test.py`) only being supposed to find
files under `--root`. Not investigated further since both fall cleanly into
already-known bug classes either way (skin-not-found, materialIndex
out-of-range) and change no conclusion — but if a future sweep's file count
starts drifting, this is worth checking (a symlink under the corpus root, or
a stale `--root` argument).

## Genuinely new: 3 files with a different animation-data shape

Three singleton entries in `failures_unique.txt` are **not** the
already-fixed "near-duplicate timestamp at a loop seam" shape — they're a
different failure mode entirely, previously undocumented:

- `creature/garrosh2/garrosh2.m2` — bone 62's scale keyframe 9 is a
  non-finite (NaN/Inf) value.
- `creature/harronirchildmale/harronirchildmale.m2` — bone 1's translation
  keyframe 3 is non-finite.
- `creature/amanitrollchildfemale/amanitrollchildfemale.m2` — bone 2's
  rotation keyframe 1 timestamp (990,834,699 ms) is *less than* keyframe 0's
  (3,183,150,484 ms) — a ~2.2-billion-ms backward jump, not a same-or-
  near-timestamp nudge case. The nudge-repair (`CORPUS_TODO.md` #4) only
  handles equal/near-equal timestamps; this is genuinely out of order by a
  huge margin, and doesn't fit that fix's premise at all.

These read as real corrupted/garbage keyframe data (NaN, or a timestamp
value that looks like unrelated memory/an unrelated field misread as a
timestamp) rather than benign authored data — but that's a hypothesis, not
confirmed against the raw bytes this session. Low priority given only 3
files out of 130,575, but flagged here since they're a distinct shape from
anything the old doc's item 4 covered, and aren't yet in any TODO file.

## Bottom line

No further husk fixes are indicated by this sweep — every one of the 660
"real corpus" failures (663 minus the 3 new animation anomalies) is already
covered by `README.md`'s documented extraction-gap/bad-source-data
paragraph, just with slightly refreshed counts (materialIndex/
textureComboIndex: 16 → 21). The only real open item this sweep surfaced is
the 3-file NaN/backward-timestamp animation anomaly above, small enough that
it doesn't currently warrant its own TODO file — worth a look if animation
robustness work resumes, otherwise fine to leave as a known, tiny, low-impact
gap.
