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
| `m2-model-adopt-export-b` | AUDIT.md §2.1 | **stalled** | 2026-09-04 | - | **Subagent hit an account session rate limit mid-task** (resets 03:10 Europe/Helsinki). No commits made. It left a header-only edit to `src/export_extras.hpp` that **did not compile** — reverted by me, tree restored and re-verified green. The partial diff is saved at `scratchpad/partial_4b_export_extras_hpp.patch` and is **worth reading before retrying**: its design work was sound (see the `rethrowIfParseFailed` note below). Retry from scratch. **Split 4b/4c.** `export_extras.cpp`'s 6 sites (`:96` ribbons, `:105` particles, `:174` attachments, `:188` events, `:196` lights, `:898` collision) take the model. Mechanically the same shape as `dumpEmitters` in split 3/4 — reuse that as the template. |
| `m2-model-adopt-export-c` | AUDIT.md §2.1 | **ready** (never started) | - | - | Was assigned with 4b; the subagent was cut off before reaching it. **Split 4c/4c — the subtle one, do last.** `resolveBones` (`:116`) and `buildAnimationsForModel` (`:166`). **Hazard**: `resolveBones` derives `bonesAreInline` from `parseBones(...).empty()`. Under `loadModel`, a *malformed* inline bones array yields an empty vector plus a recorded `FieldParseFailure` instead of throwing — which would be silently misread as "no inline bones, fall back to the external `.skel`". That is a real silent-misread risk and must be handled explicitly, not inherited. |
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
