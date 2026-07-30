# husk corpus findings — 130k .m2 export sweep

## Context

`husk` (M2 → glTF converter) was run against the full WoW export corpus at
`/media/luna/data/wow_export` — 130,575 `.m2` files. A parser script scans the
resulting `*.status.json` files (one per input, produced by the test harness'
`cc.test_*` steps: header, export, dump-chunks, fidelity, finite,
mesh_completeness) and produces four output files. This doc explains what's in
each file, how they relate, and what's actually worth fixing vs. what's
probably permanent.

**Headline result:** ~126–130k / 130,575 files pass depending on the test —
export specifically sits around 126,098 / 130,575 (~96.6%) against a binary
format that is not fully documented. The failures cluster into a small number
of root causes, not 4,477 independent bugs.

## The four output files

- **`summary.txt`** — pass/failed/total counts per test, and per-assert
  underneath each test (e.g. `export: 126098 | 4477 | 130575` broken down
  into `exit_code_0`, `glb_written`, `glb_nonempty`, `glb_magic`). Start here
  for the numeric overview. Note: child assert totals can be lower than the
  parent test's total — that's expected, not a bug, when a test short-circuits
  after an earlier assert fails (e.g. a bad header means `export` never gets
  far enough to populate every assert).

- **`failure_codes.txt`** — the actual bug index. Every failing file's set of
  `(test, assert, code, detail)` tuples is hashed into a signature; identical
  signatures collapse into one `FAIL-00NN` entry, ranked by frequency
  (`FAIL-0001` = most files affected). Each entry shows the full detail
  message once plus an example file path. **This is the file to read to
  understand what's actually broken.** Detail strings have the triggering
  file's own path/dirname/stem normalized out to `<path>` first, so failures
  that differ only by which file they happened to (e.g. every ".skin not
  found" case) still collapse together instead of each getting its own code.

- **`failures.txt`** — one line per failing file: `FAILED: "<path>"
  [FAIL-00NN]`. This is the map from file → bug class, for when you're fixing
  `FAIL-0003` and need a pile of real files to test against.

- **`failures_unique.txt`** — full inline detail for every failure whose
  signature occurred exactly once (no shared code assigned, since there's
  nowhere else to look it up). This is where the genuinely one-off,
  needs-individual-eyeballing cases live — mostly turned out to be
  off-by-one-style vertex/index mismatches and a few isolated animation-track
  issues (see below).

Caveat: `FAIL-00NN` numbering is **not stable across runs** — it's
frequency-ranked per run, not content-hashed. Don't diff code numbers between
two sweeps; diff the detail text instead.

## What's actually worth fixing (ranked by impact)

### 1. `FAIL-0001` — 3,807 files — "mesh 0's primitive 0 indices must not be empty"
By far the largest bucket. Byte-identical error across ~3,800 real assets
strongly suggests a systemic, fixable pattern rather than 3,807 corrupted
files — most likely a legitimate submesh convention (placeholder/collision-only/
degenerate zero-triangle primitives) that `writeGlbMulti` currently treats as
fatal instead of skipping. **Highest leverage fix in the whole corpus** — if
confirmed, patching `writeGlbMulti` to skip empty primitives instead of
erroring could clear this entire bucket in one change.

### 2. `.skin`-file-not-found cluster — `FAIL-0003` through `FAIL-0016`+ (~550 files total)
These are the **same underlying bug**, but currently split across ~15
different `FAIL-00NN` codes because the error message gets truncated at
inconsistent lengths right before the trailing `'auto'` — compare the tails:
`...instead of 'auto'`, `...instead of '`, `...instead of `, `...instead of
'a`, `...instead of 'au`, etc. This is very likely a fixed-size buffer in
`husk`'s error-formatting path (`snprintf`/`strncpy`-into-stack-buffer without
checking the return value or truncation) — check wherever this specific
message string gets constructed; a longer file path pushing the buffer past
its limit would produce exactly this symptom. **Action: find and fix the
buffer bug first** — that alone should merge ~14 of these codes back into
one, making the real "can't auto-resolve .skin" bug count much clearer to
size and address separately.

### 3. Vertex-count mismatches — two genuinely different sub-patterns, don't conflate them
- **Exact off-by-one** (`FAIL-0017`–`0020`, `0025`, `0026`, and others):
  `references vertex N but only has N vertices` — index equals count exactly.
  Classic `>=` vs `>` boundary bug. Strong, consistent signature. **Check
  husk's own indexing logic first** — this smells like a husk bug, not bad
  data, given how mechanically consistent the "off by exactly 1" pattern is
  across unrelated assets (bloodelf quest helms specifically).
- **Larger gaps** (`helm_leather_raidroguenerubian_d_02_*` cluster, gaps of
  4–32 vertices): not an indexing bug — this is a genuinely mismatched
  `.skin`/`.m2` pairing, likely on Blizzard's side. Don't lump these in with
  the off-by-one cluster; the fix path is different (probably "accept this is
  bad source data" rather than "fix husk").

### 4. Duplicate-timestamp animation keyframes (`failures_unique.txt`, small count)
`bone N's rotation keyframe K's timestamp isn't strictly greater than
keyframe K-1's` — hits both world bosses (`yoggsaronbrain`) and base
character rigs (`gnomemale_hd`, `mechagnomemale`, `mechagnomemale`). Leading
theory: **this is real, shipped Blizzard animation data**, not corruption —
specifically plausible as the known subtle animation-loop hitch on some
character idle animations (a duplicated/near-duplicate keyframe at the loop
seam causing near-zero-duration interpolation). If confirmed, the fix is in
`husk`'s tolerance, not a rejection: detect a zero-delta keyframe pair and
either collapse it to one keyframe or nudge the second timestamp by 1ms so
export can proceed and reproduce the same (subtle, real) in-game behavior.
Compare against another known-good M2 reader's handling of equal-timestamp
keyframes before deciding whether to relax vs. keep flagging this.

### 5. `materialIndex`/`textureComboIndex` out-of-range scatter, and one `dump-chunks` chunk-size failure (9 files)
No systemic pattern found — looks like genuine independent one-off data
issues. Low priority, spot-check individually if time allows.

## Design principle for husk itself (for future error-handling work)

Two different classes of "husk had a problem" currently look identical from
outside but should be treated differently:

- **Class A — cosmetic ambiguity, no data is actually missing** (e.g.
  duplicate keyframe timestamp). There's an obvious, lossless repair. Should
  never block output.
- **Class B — structurally invalid reference, data genuinely doesn't exist**
  (e.g. `materialIndex 3 out of range for 3 materials`). Any "recovery" here
  invents a value that isn't recoverable from the source. Output should still
  be written (usability for end users comes first), but loudly — never
  silently.

For an end user just running `husk` standalone: non-fatal, write the file
regardless, dump anomalies to stderr — they want the `.glb` no matter what
state it's in.

For this test corpus / husk development: **the opposite** — any repair or
anomaly, however benign, should count as a test failure and get full detail
dumped into `failure_codes.txt` / `failures_unique.txt`, same as a hard
crash. The goal here is completeness and correctness — every deviation is a
todo item, nothing gets to be quietly "fine." husk's own reporting today
already emits detailed anomaly messages (unexpected headers, mismatched data
lengths, missing textures, etc.) without an explicit severity/result field —
the parser currently infers "failed" purely from the test harness' own
`exit_code_0`/`glb_written`/etc. asserts, not from husk's internal severity
classification, because husk doesn't have one yet.

## Suggested next steps for Claude Code

1. Read `failure_codes.txt` top-to-bottom (already ranked by impact).
2. Start with `FAIL-0001` (empty-primitive skip) — highest file count,
   plausibly a small, contained fix.
3. Find the `.skin`-not-found error string construction in husk and check for
   truncation (fixed buffer / unchecked `snprintf`/`strncpy`) — fixing this
   collapses ~14 codes into 1 and clarifies the real bug's true size.
4. Investigate husk's vertex-index bounds check for the exact-off-by-one
   cluster (`>=` vs `>`).
5. Cross-reference the duplicate-keyframe cluster against known M2-reading
   tools' tolerance for equal timestamps before deciding to relax vs. keep
   flagging.
6. Use `failures.txt` to pull real example files per `FAIL-00NN` for testing
   any fix.
