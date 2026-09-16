# TODO: cleanup & follow-ups

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

**Scope: investigative/no pipeline dependency.** The one remaining item is
a corpus-scan tooling hang, independent of which pipeline ships.

1. **`corpus_scan_tasks/m2_full_validation_task.py` genuinely hangs on a
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
