# TODO: cleanup & follow-ups

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

1. **Three `tests/test_cli_*.cpp` files still cite the now-deleted
   `CHAR_TEXTURE_COMPOSITING_TODO.md` by name.** Every other citation across
   the repo (~30 sites in `DESIGN.md`, `README.md`, `TOOL_COMPARISON.md`,
   `EYES_ON_FINDINGS.md`, `WIKI_FINDINGS/M2.md`, `CLAUDE.md`, several
   `TODO/*.md` files, `tools/husk_blender_geoset_mask.py`, and doc comments
   across `src/`) was retargeted or removed and the stub file deleted
   2026-08-29 — see git history. `tests/test_cli_db2.cpp:619`,
   `tests/test_cli_appearance.cpp:7,184`, and
   `tests/test_cli_chrcustomization.cpp:541` were left untouched because
   `tests/` was off-limits to that session (a sibling session was actively
   adding tests there); same treatment applies — most are historical-
   narrative comments that can just be deleted, one at line 184 names the
   file inside a test's own description string. `CLAUDE_HISTORY.md` and
   `WIKI_FINDINGS_HISTORY.md` are exempt, same "don't rewrite history"
   treatment git commits get.

2. **`corpus_scan_tasks/m2_full_validation_task.py` genuinely hangs on a
   full-corpus run (not just slow).** Found 2026-08-22 during the
   post-patch corpus-scan re-run (`corpus_reports/corpus_scan_22_08/`):
   against the real 132,863-file corpus it printed `found 132863 files`
   then produced **zero** `tqdm` progress updates for a full hour before
   the driver's own `timeout 3600` killed it — not a single batch of its
   `BATCH_SIZE=4` completed. Bounded reproductions against the same real
   root (`--limit 40`, and an earlier `--limit 60` against `creature/`
   only) both ran cleanly in seconds, so this only shows up at real
   corpus scale, not in any of this task's own logic. No orphaned
   `husk`/`corpus_scan_framework` processes were left behind after the
   `timeout` kill, so whatever hung was cleaned up with it — ruling out a
   simple "runaway process still holding a lock" explanation, but not
   ruling out a transient one (e.g. a `subprocess.run(..., timeout=60)`
   grandchild that kept a stdout/stderr pipe open past the parent's kill,
   which would make Python's own post-timeout blocking `communicate()`
   retry hang indefinitely — plausible given `_rich_export`'s `husk
   export --anim auto` auto-discovers every sidecar per file, but not
   confirmed). Every other task in this session's batch (`casc_size_
   mismatch`, `dangling_references`, `unfillable_texture`, `shader_id`,
   `shader_names`, `texture_type_collisions`, `black_additive`,
   `particle_only`, `expansion`) completed cleanly against the same
   fresh corpus in the same run. Not investigated further this session
   (effort-scoped); next step is a real full-corpus run with a per-batch
   heartbeat/hang-detector (or `strace -f`/`py-spy dump` on a worker
   mid-hang) to catch it live instead of guessing from a killed run's
   silence.

3. **`husk` should ingest `community-listfile.csv` once into a cached,
   fast-to-load format instead of re-parsing ~148MB of CSV on every
   invocation of anything that reads `--listfile`.** Found 2026-08-29
   converting `unfillable_texture_task.py`/`texture_dedup_collision_task.py`/
   `black_additive_task.py` onto `husk resolve`
   (`REFACTOR/RESOURCE_CATALOG.md`'s "excavation escape hatch" table,
   `REFACTOR_LOG.md`'s newest entry). Controlled measurement (n=8 runs
   each, min/median/mean, `HUSK_CONFIG=/dev/null` to rule out
   `~/.config/husk/config.toml` silently autodiscovering its own
   `listfile`/`listfile-root` regardless of a CLI flag — a real gotcha hit
   live while benchmarking this, see the note below) found two separate,
   additive per-invocation costs, not one:
   - `--listfile` itself: `husk::loadListfile` re-parses the full
     ~148MB/2.2M-row CSV from scratch on every invocation, no persistent
     cache the way the deleted Python-side `functools.lru_cache`d mirrors
     had (those paid it once per *worker process*, not once per *file*).
     Isolated by holding everything else constant and varying only
     listfile size: a 1-row listfile costs ~0.2s/invocation, the real
     2.2M-row one costs ~0.7s — **the parse itself is ~0.5s, roughly 70%
     of a real invocation's cost**, confirming this is the dominant
     factor, not a minor one. Consistent with the earlier per-model
     measurement: a small item model (`cape_special_explorer_b_03.m2`)
     went from 76.3ms median without `--listfile` to 776.5ms with it (a
     ~700ms jump); a large character model (`bloodelffemale_hd.m2`) from
     2989.9ms to 3893.2ms (a ~900ms jump) — the same order-of-magnitude
     fixed cost regardless of model complexity.
   - Resolution itself, independent of `--listfile`: **scales with the
     model's own same-basename fuzzy-pool size**, and can dwarf the
     listfile cost on customization-heavy character models —
     `bloodelffemale_hd.m2` (real character customization directory, many
     candidate textures) measured 2989.9ms median even with `--listfile`
     omitted entirely, vs. 76.3ms for the simple item model above (both
     `--listfile`-less). `husk info --json` on the same two files: 2.5ms
     and 44.1ms respectively — resolve costs roughly 30-70x `husk info`
     even before `--listfile` enters the picture, worse the larger the
     fuzzy pool.

   For the bulk of the real corpus (simple items/creatures, small or no
   fuzzy pool — the large majority of `item/objectcomponents`, this
   project's own largest directory), the listfile parse is the dominant,
   avoidable cost; for a minority of customization-heavy character
   models, fuzzy-pool resolution already costs seconds on its own. At
   full-corpus scale (~130k files) either way is tens of thousands of
   seconds paid per scan run, which these three tasks' own stated design
   constraint ("a ~10-minute scan, not a multi-hour one",
   `unfillable_texture_task.py`'s own docstring) cannot absorb as-is, so
   **do not run any of the three converted tasks at full-corpus scale
   until this is addressed** (a bounded/subdirectory-scale run is fine —
   the correctness of the conversion itself is independently verified,
   see `REFACTOR_LOG.md`).

   **The fix**: give `husk` itself a cached, pre-ingested representation
   of the listfile instead of re-parsing raw CSV every time — the same
   "ingest the slow source once, cache it in a fast format" move `husk
   db2-build`'s own knowledge base already makes for DB2 data, and the
   same move `tools/corpus_scan_tasks/casc_size_mismatch_task.py`'s
   `_load_casc_sizes` makes for `casc-tool list`'s output in Python (a
   double-checked-locked, process-wide cache — read that function's own
   doc comment for why a bare `functools.lru_cache` wasn't enough under
   its `PARALLEL_MODE = "thread"`; Luna's own measure of that fix: several
   hours down to ~11 seconds). That Python precedent doesn't transfer
   directly, though, and this has to be a `src/` fix, not a `tools/` one:
   `casc_size_mismatch_task.py` can cache in-process because it runs
   `PARALLEL_MODE = "thread"` — every worker thread shares one Python
   process's memory. `unfillable_texture_task.py` and friends run
   `PARALLEL_MODE = "process"`, and regardless of mode the actual CSV
   parse happens inside a **separate `husk` subprocess per file** — no
   Python-side cache, in any parallel mode, can reach across that process
   boundary. The 0.5s lives inside `husk` itself, so the fix has to too.
   This would also speed up `husk export`, not just `resolve` — both pay
   the identical `loadListfile` parse today. A `--from-list` batch mode on
   `resolve` (mirroring `export`'s own, `cmd_export.cpp`'s
   `exportOneModel`) is a weaker, narrower variant of the same idea worth
   considering as a complement, not a replacement — it would amortize the
   parse across one process's whole batch, but the real fix is not
   re-parsing 148MB of CSV at all once a cache exists. Not fixed this
   session — out of a tools/-only pass's scope. Working around it in
   Python (re-adding a listfile cache on the caller's side) would just
   resurrect the duplication this conversion exists to delete.
