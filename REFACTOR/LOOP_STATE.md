# LOOP_STATE.md — supervisor loop state for the REFACTOR/ migration

Live state, not a log — `REFACTOR_LOG.md` is the narrative record. Update
this file in place; don't let entries pile up past `verified`/`stalled`
history a few iterations deep (trim old verified/stalled rows once they're
reflected in `REFACTOR_LOG.md` and no longer useful as a map).

Loop opened 2026-09-03. Running Migration order **stage 2** (`m2::Model`).

| Task | Source | Status | Assigned | Commit(s) | Notes |
|---|---|---|---|---|---|
| `stage1-gate-audit` | README.md Migration order 1 | verified | 2026-09-03 | (no code) | README asks whether stage 1's gate was actually *run*, not just built. It was: `AUDIT.md` §1.1 records a resolution-ledger diff, byte-identical/zero-delta, on `bloodelffemale_hd`, `nightelffemale_hd` (218-candidate ambiguous pool), `wolf.m2` (incl. `--lod all`), `sword_1h_artifactskywall_d_06.m2` (13 fuzzy/ambiguous matches), plus a `--knowledge-db` item exercising `registerPathOverride`. Four real fixtures + the override case. Gate met; stage 1 closed. |
| `red-baseline-fuzzy-pool` | baseline failure (not AUDIT.md) | in-progress | 2026-09-03 | - | **Blocks every other verify.** `tests/test_cli_textures.cpp:252` fails on a clean tree at `7f7c49a`: the two-basename-matching-candidates case finds `fuzzytexfaceupper00_00.png` in the output but not `fuzzytexskin00_00.png`. Suspects, in order: `feed145` (regrouped the console warning by candidate set), `dfabdd2` (constrained the widened pool for untagged texture types). Must end green **or** with the test corrected against a stated, deliberate behaviour change — never deleted or `doctest::skip`-ed to get green. |
| `m2-model-aggregate` | AUDIT.md §2.1 | ready | - | - | Split 1/4. Introduce `m2::Model` + `m2::loadModel()` as a **pure addition** — the whole-file aggregate, no consumer migrated. Type + loader + tests only. |
| `m2-model-adopt-info` | AUDIT.md §2.1 | ready | - | - | Split 2/4. Migrate `cmd_info.cpp` **and** `cmd_info_json.cpp` onto `m2::Model`. Carries stage 2's own gate for `husk info`: before/after output diffed on real fixtures, every difference attributed. |
| `m2-model-adopt-dump` | AUDIT.md §2.1 | ready | - | - | Split 3/4. Migrate `cmd_dump.cpp`. Carries stage 2's gate for `dump-chunks`. |
| `m2-model-adopt-export` | AUDIT.md §2.1 | ready | - | - | Split 4/4. Migrate `cmd_export.cpp` (11 parse kinds + `M2MaterialInputs`). Largest; re-split when reached. |
| `audit-2.1-table-stale` | AUDIT.md §2.1 | ready | - | - | Doc-only. §2.1's table lists **three** commands; the tree has **four** hand-assembled views — `cmd_info_json.cpp` (15 parse call sites) is absent from it. Fold into split 2/4 rather than assigning separately. |
| `audit-8-closed` | AUDIT.md §8 | ready | - | - | Doc-only, small. §8 is marked "Done" for every real `ScanTask` module, with `render_sample_driver.py` a stated deliberate exclusion. Per this file's own "closed items get removed outright" convention the section should go, with the exclusion moved to `CLI_AND_TOOLING.md` §4. |

## Baseline (2026-09-03, pre-loop, clean tree at `7f7c49a`)

`cmake --build build` clean. Full suite via
`HUSK_TEST_M2=test_data/bloodelffemale.m2 HUSK_TEST_SKIN=test_data/bloodelffemale00.skin ./build/husk-tests`:

**788 test cases — 787 passed, 1 failed, 1 skipped; 6321 assertions.**

The failure is pre-existing (`red-baseline-fuzzy-pool` above), not introduced by
this loop. The skip is `test_listfile_mmap_real.cpp` wanting
`HUSK_TEST_REAL_LISTFILE`. A verify iteration must **beat** this baseline, not
merely match it.

## Environment notes for briefs (from `SUPERVISOR_LOOP.md`'s NOTES)

- **Never `rm`** — it prompts interactively and stalls the whole toolchain.
  Move to a scratch dir instead. Binds subagents too.
- No global `python`/`python3`. Python is `uv` inside `tools/venv`.
- Everything runs under `direnv exec . <cmd>` from the repo root.
- Chaining several shell commands: write an ephemeral scratch-dir script and run
  that, rather than a long `&&` chain.
