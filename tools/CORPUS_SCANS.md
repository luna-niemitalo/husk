# Corpus scans: how to run one, how to write one

`corpus_scan_framework.py` is the shared driver behind every full-corpus
sweep in this project (missing textures, structural validation, shader
usage, this file's own examples). It owns discovery, parallelism,
CSV/log output, and adaptive concurrency tuning. A task module owns
exactly one thing: what to do with a single file.

## Running an existing scan

```
direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \
    --task corpus_scan_tasks.<module>:<ClassName> \
    --root /media/luna/data/wow_export \
    --output-stem <name>
```

Always smoke-test on a bounded sample before a full run:

```
... --output-stem <name>_smoketest --limit 300
```

`--limit 300` takes the *first* 300 files in traversal order, not a
random sample — traversal order isn't alphabetical and isn't guaranteed
representative of the whole corpus (see Gotcha #1 below). A bounded
smoke test proves the task runs and its logic is correct; it does not
prove the *full* run will behave the same way.

Output, written to `--output-dir` (default: repo root — pass
`--output-dir corpus_reports` to keep it with everything else):

- `<stem>.csv` — one row per file `analyze()` returned non-`None` for
- `<stem>_errors.csv` — path + full traceback for any file whose
  `analyze()` raised (a scan never aborts on one bad file)
- `<stem>_scan.log` — the task's own `summarize()` output

**Never pass `--max-workers` or `--initial-workers` to force a
concurrency number.** `AdaptiveConcurrency` finds the real ceiling
itself (TCP-AIMD-style: grow while throughput improves, back off on the
first real drop) — overriding it defeats the point and reintroduces the
exact guessing problem it was built to remove.

## Writing a new task

Copy `corpus_scan_tasks/example_texture_count.py` — it's deliberately
minimal and is the actual template, not just a reference:

```python
class MyTask:
    GLOB_PATTERNS = ["*.m2"]        # passed to root.rglob() per pattern
    FIELDNAMES = ["some_field"]     # CSV columns; "path" is added automatically
    PARALLEL_MODE = "process"       # or "thread" -- see below
    BATCH_SIZE = 1                  # see Gotcha #2 -- start at 1, justify anything higher

    @staticmethod
    def analyze(path: Path) -> dict | None:
        ...                          # return None to skip this file
        return {"some_field": ...}   # keys must match FIELDNAMES exactly

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        return [f"{len(rows)} / {total_files} files matched"]
```

`analyze()` must be a pure function of `path`'s own bytes/filesystem
state — no shared state, no cross-file writes. That's what makes
per-file parallelism safe without a lock.

**`PARALLEL_MODE`**: `"process"` for anything that shells out (`husk`,
`gltf_validator`, `blender`) or does real CPU work (struct-unpack over
big buffers) — real parallelism, no GIL contention. `"thread"` only for
genuinely lightweight tasks (a few field reads) where per-file process
pickling/IPC would cost more than the work itself.

## Two real gotchas, both hit in the same session (2026-08-15)

### 1. `--limit` samples the front of traversal order, not the corpus

A "smoke test" run against an entire subdirectory (`--root
.../item/objectcomponents`) was mistaken for a small sample — it was
actually 61,735 of 130,576 files (47% of the corpus). Separately, a
`--limit 4000` benchmark against the corpus root happened to land
entirely inside one alphabetically-early, well-behaved directory and
missed a real performance bug that only showed up ~25% into the full
run. **A bounded sample proves correctness, not performance** — for
timing/backoff behavior, benchmark against the specific directory or
file pattern you're worried about, not just "the first N".

### 2. `BATCH_SIZE > 1` amplifies tail latency under `AdaptiveConcurrency`

The controller reacts to per-*completion* latency, but a batch's
completion is gated by its single slowest member. One slow file in a
batch of 8 stalls the other 7, so the controller sees "one slow
completion" that's actually gating 8 files' worth of latency, and its
backoff reacts as if the whole batch were that slow.

Real incident: a task's per-file fuzzy-match check called `Path.glob()`
directly against `item/objectcomponents/collections` — 110,337 files,
the corpus's largest single directory — measuring ~70ms per call (no
cross-call cache, so every glob re-lists the whole directory). With
`BATCH_SIZE=8` this produced visible window oscillation (10↔13) and
~4x slowdown on the real full-corpus run; a `--limit 4000` bench
against an easier part of the corpus showed nothing because it never
touched that directory.

Root cause was the missing cache, not batching itself — fixed with a
`functools.lru_cache`-wrapped `os.scandir()` per directory (pay the
~70ms once per directory, not once per file in it). Once fixed and
**re-benchmarked against the actual pathological directory**,
`BATCH_SIZE=8` measured faster than `1` with zero backoffs — the
framework's own rationale (amortize IPC dispatch once real per-file
cost is small and uniform) held once the real variance source was gone.

**Rule going forward**: default `BATCH_SIZE=1`. Only raise it with a
measured before/after (backoff count + files/sec, against a directory
or sample actually representative of the corpus's worst case, not just
whatever `--limit N` happens to grab) written into a comment next to
the value. Never copy a batch size from another task's file without
checking the new task's own latency profile — `missing_texture_task.py`
and `unfillable_texture_task.py` do superficially similar work
(`husk info` per file) but have very different per-file cost shapes
once one of them also does directory-listing work.

## Existing tasks, for reference

| Task | What it checks |
|---|---|
| `missing_texture_task.py` | Texture FileDataIDs with no literal same-directory `.blp`/`.png` (over-flags anything husk's fuzzy fallback would actually resolve — see `unfillable_texture_task.py` for the corrected version) |
| `unfillable_texture_task.py` | Actually-used texture slots (via `texture_lookup`) with no literal *and* no fuzzy-same-basename local file — nothing husk's real resolution could find |
| `m2_full_validation_task.py` | Full per-file structural validation: header parse, rich export, dump-chunks, fidelity/finite/mesh-completeness checks |
| `shader_names_task.py` / `shader_id_task.py` | Pixel/vertex shader usage frequency across real batches |
| `texture_type_collisions_task.py` | Files where more than one texture type maps to the same texture index |
| `particle_only_task.py` | Blank-render candidates explained by particle-driven-only geometry |
| `dangling_references_task.py` | Per-reference-kind corpus-wide rate of internal cross-references that don't resolve (`bone_lookup`/`sequence_lookup`/`attachment_lookup`/`camera_lookup`/`texture_lookup`, plus `.skin`-batch-driven `material_index`/`color_index`/`texture_combo`/`texture_weight_combo`/`texture_transform_combo`) — the counterweight to presence-only completeness metrics, see `TODO/CLEANUP_TODO.md` |
| `render_sample_driver.py` + `render_glb.py` | The actual Blender render pipeline (not a `ScanTask` — a two-stage subprocess pipeline: `husk export` then `blender --background`) |
