# LOOP_STATE.md — supervisor loop state for the REFACTOR/ migration

Live state, not a log — `REFACTOR_LOG.md` is the narrative record. Update
this file in place; don't let entries pile up past `verified`/`stalled`
history a few iterations deep (trim old verified/stalled rows once they're
reflected in `REFACTOR_LOG.md` and no longer useful as a map).

Loop opened 2026-09-03. Running Migration order **stage 2** (`m2::Model`).
**Stage 2 is 3 of 4 migrated**: `cmd_info`, `cmd_info_json`, `cmd_dump` done;
`cmd_export` remains, re-split into 4a/4b/4c below.

| Task | Source | Status | Assigned | Commit(s) | Notes |
|---|---|---|---|---|---|
| `stage1-gate-audit` | README.md order 1 | verified | 2026-09-03 | (no code) | Stage 1's gate *was* run and recorded (`AUDIT.md` §1.1: ledger diff, zero delta, 4 real fixtures + a `registerPathOverride` case). Stage 1 closed. |
| `red-baseline-fuzzy-pool` | baseline failure | verified | 2026-09-03 | `9bdd0ed` | Stale test assertion, not a regression; replaced with a stronger `.glb` content check. |
| `m2-model-aggregate` | AUDIT.md §2.1 | verified | 2026-09-03 | `4115f20` | Split 1/4: `m2::Model` + `loadModel()`, per-field failure isolation. One defect found in review → `m2-model-loadfile-dup`. |
| `m2-model-loadfile-dup` | supervisor review | verified | 2026-09-03 | `1e50e21` | I2 violation (third verbatim copy of a file read, zero callers). Deleted outright. |
| `m2-model-adopt-info` | AUDIT.md §2.1 | verified | 2026-09-03 | `9f85bc8` | Split 2/4. Gate met: their seeded 402/402 confirmed by my own independent 112/112 on a different sample. |
| `m2-model-adopt-dump` | AUDIT.md §2.1 | verified | 2026-09-04 | `6367a4a` | Split 3/4. Gate met: their 420/420 confirmed by my own independent 190/190 on a different sample. |
| `audit-2.1-table-stale` | AUDIT.md §2.1 | verified | 2026-09-03 | `9f85bc8` | §2.1's table now names `cmd_info_json.cpp` as the fourth view; section correctly left open. |
| `m2-model-adopt-export-a` | AUDIT.md §2.1 | **verified** | 2026-09-04 | `06a08f8` | Split 4a done. Their 35/35 confirmed by my own independent gate on a different sample: **55/55 `.glb` byte-identical and 55/55 console output identical** (paths normalized — my first script compared its own scratch dirs and reported a false 55 differences; the artifact was mine, not husk's). Suite holds at 796/796, 6528. `.glb` output confirmed deterministic run-to-run, so byte comparison is a valid gate. Checked the rethrow type substitution myself: it throws `std::runtime_error` where the original threw `m2::ParseError`, but `ParseError` derives from it and **no catch site in `src/` or `tests/` discriminates**, so it is behaviourally invisible — a fidelity nit, not a defect. |
| `m2-model-adopt-export-b` | AUDIT.md §2.1 (now removed) | **verified** | 2026-09-04 | `517c2a8` | Split 4b. `export_extras.cpp`'s 6 sites read the model; `rethrowIfParseFailed` promoted to a shared function (6 call sites clears the third-occurrence bar). Extras path correctly made fail-fast, not degrading. |
| `m2-model-adopt-export-c` | AUDIT.md §2.1 (now removed) | **verified** | 2026-09-04 | `9d291f1b` | Split 4c, the subtle one. Rethrow fires at the exact position the old parse ran, so a malformed file still reports the same field first; the `haveSkel` branch's own `skel::parseSequences` correctly left untouched. Supervisor checks: their 426/426 confirmed by my own focused gate on the highest-risk path — 42 models incl. the real `.skel`-sourced `bloodtick.m2`, **42/42 `.glb` and console identical**. Suite **801/801, 6559**. Verified no real `m2::parse*` call remains in any of the four commands (only comments). |

| `comment-discipline-cleanup` | Luna, 2026-09-04 | **deferred — END OF RUN** | - | - | **Supersedes the mis-scoped `audit-2.1-dangling-cites`.** The stranded §2.1 citations were a *symptom*; `~/nix/claude-rules/CODE_COMMENTS.md` says the citations shouldn't exist at all. This loop added **354 comment lines against 310 code lines** to `src/` (more comment than code — litmus #5, density), including 21 references to `AUDIT.md`/`TODO/`/`REFACTOR_LOG`/"split 4b" (litmus #4, cross-cutting → belongs in docs; and "historical narrative → belongs in commit message"). Worst case: `rethrowIfParseFailed`'s 16-line doc comment on a ~4-line body, opening "Promoted from cmd_export.cpp's split 4a (git log 06a08f8b), where this started as..." — the named anti-pattern verbatim. **Scope: only what this loop added** (`git diff 7f7c49a..HEAD -- src/`). Strip doc/TODO references and historical narrative; keep local why-facts, invariants, gotchas, boundary contracts; move anything genuinely architectural to `DESIGN.md`. The migration rationale is already in `REFACTOR_LOG.md` and the commit messages — that is where it belongs. |
| `audit-8-closed` | AUDIT.md §8 | ready | - | - | Doc-only, small. §8 is "Done" for every real `ScanTask` module with `render_sample_driver.py` a stated deliberate exclusion; per the file's own convention the section should go, exclusion moved to `CLI_AND_TOOLING.md` §4. |

## Baseline

Full suite: `direnv exec . env HUSK_TEST_M2=test_data/bloodelffemale.m2 HUSK_TEST_SKIN=test_data/bloodelffemale00.skin ./build/husk-tests`

**Current green baseline, independently re-run at `6367a4a`: 796 cases — 796
passed, 0 failed, 1 skipped; 6528 assertions.** Every later verify is measured
against this. A verify must **beat or hold** it, never merely "look green" — a
*dropping* assertion count is a signal to inspect, since coverage can be removed
without any test failing.

Progression: 788/6323 (pre-loop, 1 red) → 794/6500 → 795/6518 → 796/6528.

## Environment notes for briefs (from `SUPERVISOR_LOOP.md`'s NOTES)

- **Never `rm`** — it prompts interactively and stalls the whole toolchain.
  Move to a scratch dir instead. Binds subagents too.
- No global `python`/`python3`. Python is `uv` inside `tools/venv`. `awk`/`sort`/
  `diff` are available and are what the diff gates have used.
- Everything runs under `direnv exec . <cmd>` from the repo root.
- Chaining several shell commands: write an ephemeral scratch-dir script.

## Supervisor decisions (recorded so they're reviewable, not silent)

**`m2::Model` lands at `src/m2_model.{hpp,cpp}`, not `src/formats/`.** Creating
that directory now would bundle a mechanical relocation of every format parser
in with a semantic change, making both harder to review and revert. The
directory move stays a separate task. A sequencing call, not a change to the
Migration order.

**Split 4 is three tasks, not one.** `cmd_export.cpp` has 14 parse sites and
`export_extras.cpp` 6 — 20 across 2 files, past this loop's own "~5 call sites"
splitting threshold. Ordered mechanical-first, subtle-last, so the risky
`bonesAreInline` semantics land alone and reviewable rather than buried in a
wide diff.

## Findings for Luna (surfaced by the loop, not scheduled work)

**`husk info`/`dump-chunks` used to abort uncaught on a malformed array.**
Their per-array loops sat outside the only try/catch and `src/main.cpp` has no
outer catch around dispatch. Both are now fixed as a side effect of migrating
onto `loadModel()` — a clean `parse_failures` diagnostic, exit 0, pinned by
regression tests. `cmd_export` still has the old behaviour until split 4 lands.

**The local corpus contains no pre-Legion flat MD20 files at all.** Found while
gating split 3/4, independently re-checked on a separate stride (every 37th
file, 3,590 sampled): **100% `MD21`, zero `MD20`**. husk's pre-Legion branches
therefore have **no real-data coverage on this machine**, only synthetic
fixtures. Not a bug — but it means "verified against the corpus" silently
excludes that whole branch, on any task, not just this one.

**Two commands, two deliberate parse-failure contracts — worth a design look
in stage 3.** `cmd_info`/`dump-chunks` degrade gracefully on a malformed field
(record it, print a `parse_failures` diagnostic, carry on); `cmd_export` fails
fast (rethrows the first matching failure, in the original parse order). Both
are right for what they are — a diagnostic command should show as much of a
broken file as it can, while an exporter must not quietly write a `.glb` with
a silently-empty vertex array. This was found the hard way, not assumed: the
first attempt let isolation flow through and a real regression test
(`test_cli_errors.cpp`'s corrupted-vertex-count case) caught it.

The note for later: right now that policy split lives as a hand-written
`rethrowIfParseFailed` call per field at one consumer. When `canon::` is built
(stage 3), the contract should be stated once in the model layer — "this
consumer tolerates partial data, that one does not" — rather than re-derived
per command. Not actionable now; recorded so stage 3 doesn't rediscover it.

**Salvage from the stalled 4b attempt: `rethrowIfParseFailed` should be shared,
not re-derived.** The cut-off subagent's header-only diff (saved at
`scratchpad/partial_4b_export_extras_hpp.patch`) reached a conclusion worth
keeping. Split 4a introduced `rethrowIfParseFailed` as a *local lambda* inside
`exportOneModel`. Once 4b and 4c land, the same need appears at **six real call
sites across two files** — `exportOneModel`/`resolveBones`/
`resolveAnimationsForModel` in `cmd_export.cpp`, and `attachEmitterAnchors`/
`attachPlacementNodes`/`appendCollisionMesh` in `export_extras.cpp`. That is
past this project's own "third occurrence" bar, so promoting it to a real
shared function (declared in `export_extras.hpp`) is *earned*, not speculative.

Its other correct observation: the extras path is not exempt from fail-fast.
A malformed `attachments`/`events`/`lights`/`ribbons`/`collision` array would
otherwise silently attach **fewer placement anchors than the file actually
has**, with no error — the same silent-misread class as 4c's `bonesAreInline`,
just quieter. Whoever retries 4b should decide this deliberately rather than
assuming "extras are diagnostic, so degrading is fine".

## STAGE 2 IS COMPLETE (2026-09-04)

All four commands now consume one `m2::Model`. `AUDIT.md` §2.1 removed per that
file's own convention. Verified by me: no real `m2::parse*` call remains in
`cmd_info`/`cmd_info_json`/`cmd_dump`/`dump_emitters`/`cmd_export`/
`export_extras` — every remaining mention is inside a comment. Suite **801/801,
6559 assertions** (from 788/6323 with one red at loop start).

**Stage 2's gate (README Migration order, item 2) is met**: `husk info` and
`dump-chunks` output diffed on real fixtures with every difference attributed —
splits 2 and 3, each confirmed by an independent supervisor diff on a different
sample. Export was gated the same way though the README did not require it.

### Escalation for Luna before stage 3

`SUPERVISOR_LOOP.md` puts stage 3 (`canon::`) in scope once stage 2's gate
passes. It now has. **But `REFACTOR/README.md` says stage 3 "should be driven
with you steering it live, not delegated wholesale"** — and this loop's whole
mechanism is delegation to Sonnet subagents. Those two instructions conflict,
and resolving it is a judgment call with real downstream cost (stage 3 is the
"expensive stage that must not be shortcut", and this project's own feedback
pattern is that wide refactors started mid-design get thrown away).

**So the loop is not starting stage 3 on its own.** It continues on the smaller
independently-biteable items still in scope (`audit-8-closed`,
`audit-8-closed`, and `AUDIT.md` §2.2/§2.3/§2.4, §4, §5, §6), which do not
block each other or stage 3. Luna's call on how stage 3 should be driven.

**Comment discipline — a rule this loop broke, corrected by Luna 2026-09-04.**
I first reported the stranded `AUDIT.md §2.1` citations as a "convention
tension" between "closed items get removed outright" and comments citing
section numbers. That framing was wrong. `~/nix/claude-rules/CODE_COMMENTS.md`
already settles it: code comments should not reference TODO/AUDIT items at all.
Its litmus #4 predicts this exact failure — *"would updating this comment
require touching unrelated code elsewhere? → it's cross-cutting, belongs in
docs"* — and removing one section required touching 12 call sites.

Measured, not asserted: this loop added **354 comment lines against 310 code
lines** in `src/`, with 21 doc/TODO references and several passages of pure
historical narrative ("Promoted from ... split 4a, where this started as ...").
Both are explicitly named as not belonging inline. I reviewed all five commits,
had this rule in memory, and instead praised the comments as thorough — the
review checklist below now covers it so it cannot recur silently.

**Wider pattern, Luna's call, not queued:** `src/` carries **77** such doc/TODO
references across **28 files**, most predating this loop. `comment-discipline-cleanup`
is deliberately scoped to this loop's own additions only. Whether the
pre-existing 56 get the same treatment is a separate decision.

## Review checklist for every subagent commit (added after the above)

Beyond build/tests/gate/scope, check the *diff's comments*:
1. Any reference to `AUDIT.md`/`TODO/`/`REFACTOR_LOG`/a split number? → must go.
2. Any "we used to do X, then switched to Y"? → commit message, not code.
3. More comment lines than code lines in the hunk? → density failure, push to a doc.
4. A doc comment needing a paragraph? → it needs a document, not a `//`.

## End-of-run verification pass (Luna's instruction, 2026-09-04)

`comment-discipline-cleanup` is **deliberately deferred to the end of the run**,
not done as it arises. Reason: cleaning comments mid-run churns files that later
splits touch again, and one pass at the end sees everything the run added rather
than only what has landed so far.

So the final iteration of this loop is a **verification pass**, not another
migration step. It covers, over `git diff <loop-start>..HEAD -- src/`:

1. **Comment discipline** — the four checks in the review checklist above
   (no plan-item references, no historical narrative, comment:code density,
   no paragraph-length doc comments). Currently 354 comment lines vs 310 code
   lines and 21 doc/TODO references to answer for.
2. Anything else the per-iteration reviews deferred rather than resolved.

Scope stays this loop's own additions. The ~56 pre-existing doc/TODO references
elsewhere in `src/` are Luna's separate call, not this pass's to sweep.
