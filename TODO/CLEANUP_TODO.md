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

3. **The listfile cache's warm path is only marginally faster than the
   uncached baseline (~4% on a simple model, unmeasurable on a
   fuzzy-pool-heavy one) -- much smaller than hoped.** Built 2026-08-29
   (`src/listfile_cache.hpp`/`.cpp`, `DESIGN.md`'s "Listfile cache"
   section has the full real-vs-expected numbers). Root cause, measured
   not assumed: building the final `unordered_map<uint32_t, std::string>`
   (2.2M string allocations + hash insertions) dominates real cost, not
   the CSV text-scan this cache eliminates -- and every current caller
   still needs that full map materialized (`export_extras.hpp`,
   `export_materials.hpp`, `sources::Catalog`/`ListfileCatalog`/
   `TextureCatalog`, ~20 sites total). The cache's on-disk format is
   already a sorted `FileDataID` array + string blob, directly
   binary-searchable -- closing this gap for real means migrating callers
   to point lookups against that array instead of a fully materialized
   map, a separate, correctness-neutral but real refactor. Not attempted
   this session (out of a caching-only pass's scope).

