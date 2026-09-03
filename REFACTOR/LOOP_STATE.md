# LOOP_STATE.md — supervisor loop state for the REFACTOR/ migration

Live state, not a log — `REFACTOR_LOG.md` is the narrative record. Update
this file in place; don't let entries pile up past `verified`/`stalled`
history a few iterations deep (trim old verified/stalled rows once they're
reflected in `REFACTOR_LOG.md` and no longer useful as a map).

Loop opened 2026-09-03. Running Migration order **stage 2** (`m2::Model`).

| Task | Source | Status | Assigned | Commit(s) | Notes |
|---|---|---|---|---|---|
| `stage1-gate-audit` | README.md Migration order 1 | verified | 2026-09-03 | (no code) | README asks whether stage 1's gate was actually *run*, not just built. It was: `AUDIT.md` §1.1 records a resolution-ledger diff, byte-identical/zero-delta, on `bloodelffemale_hd`, `nightelffemale_hd` (218-candidate ambiguous pool), `wolf.m2` (incl. `--lod all`), `sword_1h_artifactskywall_d_06.m2` (13 fuzzy/ambiguous matches), plus a `--knowledge-db` item exercising `registerPathOverride`. Four real fixtures + the override case. Gate met; stage 1 closed. |
| `red-baseline-fuzzy-pool` | baseline failure (not AUDIT.md) | **verified** | 2026-09-03 | `9bdd0ed` | Stale test, not a regression. `feed145`'s own commit message states it deliberately stopped dumping the full candidate list to stderr ("already embedded as alternate_textures extras on the .glb"), and it touched only `cmd_export.cpp`'s printing — clearing the other suspect, `dfabdd2`'s pool admission, which never excluded the candidate. Supervisor checks: diff scope is 2 files, no `src/`; the removed stderr grep was replaced by a **stronger** `.glb` assertion (both filenames looked up by content in `alternate_textures`, previously only `ArrayLen() == 2`, plus `images.size() == 3`); `feed145`'s intent confirmed by reading it directly, not from the report; full suite rebuilt and re-run from scratch by me. |
| `m2-model-aggregate` | AUDIT.md §2.1 | **verified (with defect)** | 2026-09-03 | `4115f20` | Split 1/4 landed. Supervisor checks: diff is 6 files, all additions, no `cmd_*.cpp` and no existing parser touched; `parseCollisionMesh`'s 4-arg order checked against its real declaration; `kMinVerifiedParticleVersion` confirmed pre-existing (`m2_scene.hpp:237`), and its own doc requires exactly the caller-side gate used. Full rebuild + suite re-run by me: **794/794, 0 failed, 1 skipped, 6500 assertions** (baseline 788/6323). Per-field isolation contract is genuinely tested (a malformed file yields 25 *independent* recorded failures). **Defect found, not reported by the subagent** — see `m2-model-loadfile-dup` below. |
| `m2-model-loadfile-dup` | supervisor review of `4115f20` | **verified** | 2026-09-03 | `1e50e21` | Deleted outright, no shared helper minted (nothing needs one yet). Original finding kept for the record: **Corrective, small.** `m2::loadModelFile` (`m2_model.cpp`) is a verbatim third copy of file-read logic already in `m2::loadFile` (`m2_primitives.cpp:420-433`) and `cmd_info.cpp`'s `readFileBytes` — identical errno reset, ifstream, `istreambuf_iterator`, and both error strings, differing only in the final call. It also has **zero callers** outside its own test. That is an I2 violation ("one implementation per resolution question") introduced by the very commit meant to cure §2.1, plus the speculative generality the brief forbade. Fix: delete it (splits 2-4 add a real read path when a real caller exists), or factor the shared read out. Prefer deletion — nothing needs it yet. |
| `m2-model-adopt-info` | AUDIT.md §2.1 | **verified** | 2026-09-03 | `9f85bc8` | **Stage 2's gate met for `husk info`.** Supervisor checks: read every hunk of the `cmd_info.cpp` diff — all are pure `parseFoo(blob, h.foo)` → `model.foo` substitutions with every `count > 0` output guard intact as unchanged context, so the named trap was avoided. Rebuilt the **pre-migration binary myself** from `1e50e21` in a throwaway worktree and ran my own diff on a *different* sample than theirs (56 real files across `creature`/`character`/`item`/`world`, both `info` and `info --json`, `HUSK_CONFIG=/dev/null`): **112/112 byte-identical incl. exit codes** — a genuine second opinion confirming their seeded 402/402, not a replay. Full suite re-run: **795/795, 0 failed, 6518 assertions**. Their one flagged deviation (test placed in `test_cli_errors.cpp`, not the file my brief named) was **correct and my brief was wrong** — that file's own doc comment reserves it for exactly this malformed-content class. Split 2/4. Migrate `cmd_info.cpp` **and** `cmd_info_json.cpp` onto `m2::Model`. Carries stage 2's own gate for `husk info`: before/after output diffed on a seeded random real-corpus sample, every difference attributed. Assigned together with `m2-model-loadfile-dup` (same subsystem, separate commits) to avoid spending a whole iteration on a 3-line deletion. **Named trap**: `cmd_info.cpp` gates prints on `count > 0` for arrays `loadModel()` now always parses — the *print* guards must survive even though the data is unconditionally present, or output changes silently. |
| `m2-model-adopt-dump` | AUDIT.md §2.1 | ready | - | - | Split 3/4. Migrate `cmd_dump.cpp`. Carries stage 2's gate for `dump-chunks`. |
| `m2-model-adopt-export` | AUDIT.md §2.1 | ready | - | - | Split 4/4. Migrate `cmd_export.cpp` (11 parse kinds + `M2MaterialInputs`). Largest; re-split when reached. |
| `audit-2.1-table-stale` | AUDIT.md §2.1 | **verified** | 2026-09-03 | `9f85bc8` | Closed as part of split 2/4: §2.1's table now names `cmd_info_json.cpp` as the fourth view (10 kinds / 15 call sites) and says "four different answers", with the section correctly left **open** pending `cmd_dump`/`cmd_export`. Original finding: doc-only. §2.1's table lists **three** commands; the tree has **four** hand-assembled views — `cmd_info_json.cpp` (15 parse call sites) is absent from it. Fold into split 2/4 rather than assigning separately. |
| `audit-8-closed` | AUDIT.md §8 | ready | - | - | Doc-only, small. §8 is marked "Done" for every real `ScanTask` module, with `render_sample_driver.py` a stated deliberate exclusion. Per this file's own "closed items get removed outright" convention the section should go, with the exclusion moved to `CLI_AND_TOOLING.md` §4. |

## Baseline (2026-09-03, pre-loop, clean tree at `7f7c49a`)

`cmake --build build` clean. Full suite via
`HUSK_TEST_M2=test_data/bloodelffemale.m2 HUSK_TEST_SKIN=test_data/bloodelffemale00.skin ./build/husk-tests`:

**788 test cases — 787 passed, 1 failed, 1 skipped; 6321 assertions.**

The failure was pre-existing (`red-baseline-fuzzy-pool` above), not introduced
by this loop. The skip is `test_listfile_mmap_real.cpp` wanting
`HUSK_TEST_REAL_LISTFILE`.

**Current green baseline, independently re-run at `9f85bc8`: 795 cases — 795
passed, 0 failed, 1 skipped; 6518 assertions.** That is what every later verify
iteration is measured against. A verify must **beat or hold** it, never merely
"look green" — and an assertion count that *drops* is a signal to inspect, since
coverage can be removed without any test failing.

Progression so far: 788/6323 (`9bdd0ed`) → 794/6500 (`4115f20`) → 795/6518
(`9f85bc8`, net of one test removed with `loadModelFile`).

## Environment notes for briefs (from `SUPERVISOR_LOOP.md`'s NOTES)

- **Never `rm`** — it prompts interactively and stalls the whole toolchain.
  Move to a scratch dir instead. Binds subagents too.
- No global `python`/`python3`. Python is `uv` inside `tools/venv`.
- Everything runs under `direnv exec . <cmd>` from the repo root.
- Chaining several shell commands: write an ephemeral scratch-dir script and run
  that, rather than a long `&&` chain.

## Supervisor decisions (recorded so they're reviewable, not silent)

**`m2::Model` lands at `src/m2_model.{hpp,cpp}`, not `src/formats/`.**
`README.md`'s target pipeline puts M2 parsing in `src/formats/`, and that
directory still doesn't exist. Creating it as part of this task would bundle a
mechanical relocation of the whole `m2_*` / `skin` / `skel` / `bone` / `phys` /
`blp` / `db2` set in with a semantic change, making both harder to review and
to revert independently. So: `m2_model.hpp` joins the five existing siblings
that `m2.hpp` already aggregates, and the directory move stays a separate,
purely mechanical task for later in stage 2/6. This is a sequencing call, not a
change to the Migration order.

**The eager-vs-lazy parse contract is this task's real design content.**
`cmd_info.cpp`'s sub-parses are *conditional* and *unguarded*: e.g.
`parseSequences` runs only `if (h.sequenceLookup.count > 0)`, and a throw from
it propagates out (only `readFileBytes`/`parseHeader`/`extractBlob` sit inside a
try/catch, `cmd_info.cpp:102-116`). An eager whole-file `loadModel()` therefore
parses arrays the current commands skip, and can newly throw on a file where a
*skipped* array is malformed. Across a real 132k-file corpus that is a genuine
regression risk, and it is the reason splits 2-4 are separate: split 1 changes
no behaviour at all, so the contract can be decided and tested before any
consumer depends on it.

## Findings for Luna (surfaced by the loop, not scheduled work)

**`husk info` can already abort uncaught on a malformed array, today.**
Confirmed while reviewing `4115f20`'s contract reasoning, by reading the code
rather than taking the subagent's word: `cmd_info.cpp` wraps only
`readFileBytes`/`parseHeader`/`extractBlob` in a try/catch (`:102-116`); its
`bones`/`attachments`/`events`/`lights`/`ribbons` loops sit outside it, and
`src/main.cpp` has **no** outer catch around command dispatch (only around
`tryPrintCompletion`, `:398`). So a file whose header parses but whose `bones`
array is malformed terminates via an unhandled exception instead of printing
the clean `husk: couldn't read '<path>'` message the header-level path gives.

Not fixed here — out of this task's scope, and it is pre-existing, not caused
by the loop. Worth knowing that **split 2/4 fixes it as a side effect**:
`loadModel()`'s per-field isolation turns that abort into a recorded
`FieldParseFailure`. That makes split 2/4 more valuable than "the same output,
refactored", and its gate should check this case explicitly rather than only
diffing well-formed fixtures.
