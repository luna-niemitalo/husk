# TOOLS.md — tools/

Standalone Python scripts for exploring the real WoW corpus at
`/media/luna/data/wow_export` — the same "second opinion" discipline
`WIKI_FINDINGS.md` describes: these read M2/`.skin`/DB2 bytes directly,
independent of husk's own C++ parsing, to verify a wowdev.wiki claim or
find a real-corpus edge case before (or after) husk's own code handles it.
Run inside the project's Python venv: `direnv exec . tools/venv/bin/python
tools/<script>.py`.

## Parallel corpus scanning: `corpus_scan_framework.py`

The generalized driver for "run one independent check over every file in
the corpus." A one-off script only needs to write a `ScanTask` — discover,
parallelism, CSV/log output, and live worker-count tuning are the
framework's job, not the task's:

```python
class MyTask:
    GLOB_PATTERNS = ["*.m2"]           # root.rglob() patterns
    FIELDNAMES = ["some_field"]        # CSV columns; "path" is added automatically
    PARALLEL_MODE = "process"          # "process" (default, real CPU work) or "thread" (cheap/GIL-light)
    BATCH_SIZE = 1                     # raise only if per-file IPC dispatch, not I/O or CPU, is the bottleneck

    @staticmethod
    def analyze(path: Path) -> dict | None:
        ...                            # return None to skip the file

    @staticmethod
    def summarize(rows, total_files) -> list[str]:
        ...                            # human-readable summary lines
```

See `corpus_scan_tasks/example_texture_count.py` for a minimal real one
and `corpus_scan_tasks/texture_type_collisions_task.py` for a task that
wraps an existing one-off script's `analyze()`/`summarize()` unchanged
rather than re-deriving its parsing.

Run from the command line:

```
direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \
    --task corpus_scan_tasks.example_texture_count:TextureCountTask \
    --root /media/luna/data/wow_export --output-stem texture_count
```

**Concurrency is live-tuned, not guessed up front.** `AdaptiveConcurrency`
runs a TCP-AIMD-style controller during the scan: start near a cheap
topology-based seed, grow the in-flight task budget while measured
throughput improves, cut it by ~20% the moment throughput drops (the real
signal of disk seek-thrashing, GIL contention, or any other saturation —
found empirically this session: a GIL-bound thread-pool task and a
seek-bound spinning-disk task both trigger the same backoff correctly),
then resume slow re-probing. This self-corrects for a warm cache
automatically — no static "is this an HDD or an L2ARC-cached run" guess
is ever made. `--max-workers` is a ceiling (default: CPU count),
`--initial-workers` a ramp-up seed (default: auto, from `zpool status`
topology if the corpus root is on a recognizable ZFS pool). The end-of-run
report prints the actual window-size trace, backoff count, per-device
disk MB/s, and ARC/L2ARC hit rate — real evidence for *why* a run
converged where it did, not a guess.

If a task's per-file cost is heavily heavy-tailed (confirmed this session
for `.m2`/`.skin`: file sizes span 0 bytes to 38MB, and batch counts per
file can reach the hundreds), the default tick/threshold settings
(`tick_seconds=6.0`, `min_samples_per_tick=15`,
`AdaptiveConcurrency.backoff_threshold=0.25`) already account for it —
see `corpus_scan_framework.py`'s own docstrings for the reasoning if they
need retuning for a differently-shaped task.

`tools/benchmark_texture_type_collisions.py` is the reference example for
A/B-testing a new task against a plain single-threaded baseline before
trusting the framework's speedup on a real multi-hour corpus run.

## Full-corpus visual render pipeline

A separate track from the parsing-correctness scanners above: these
actually run `husk export` + headless Blender over real corpus files and
produce something a human looks at, not a CSV.

- `corpus_scan_tasks/build_render_sample.py` — builds a stratified
  (proportional-by-sqrt-per-category) sample of `.m2` paths for a quick
  visual spot-check, plus a forced-include of every file with a co-located
  `.phys` sidecar. Writes a plain one-path-per-line file
  (`corpus_reports/render_sample.txt` by convention).
- `corpus_scan_tasks/render_glb.py` — the actual headless-Blender worker:
  imports one `.glb`, rebuilds a real additive shader (Transparent BSDF +
  Emission via Add Shader) for any material carrying a `blend_mode` extras
  value of 3/4 (WoW's additive blend modes -- no core-glTF equivalent, see
  `README.md`'s Materials section; handles both the plain-Principled and
  `KHR_materials_unlit`-imported node shapes, which Blender's importer
  builds completely differently), frames the camera to its bounding box,
  renders one WebP image (quality 80, lossy — these are flat-shaded QA
  thumbnails, not archival output). Prints a `SKIPPED` sentinel (not a
  crash) for a real 0-vertex camera/track-only model. Invoked as `blender
  --background --factory-startup --python render_glb.py -- <in.glb>
  <out.webp>`, never run standalone.
- `corpus_scan_tasks/render_sample_driver.py` — drives the two steps above
  (real `husk export` with full sidecar auto-discovery, then
  `render_glb.py`) in parallel across a file list, either the sample above
  or a full corpus file list. Resume-safe by design (checks for an
  existing output image — either extension, `.webp` current or `.png`
  legacy — before doing any work, so a crash mid-run costs nothing but
  wall-clock time on restart) — this is the tool that was actually resumed
  after a real machine crash mid-130k-file run (see `CLAUDE_HISTORY.md`'s
  2026-08-09 entries for the full incident). Passes `--listfile`/
  `--listfile-root` to `husk export` automatically whenever a real
  `community-listfile.csv` is present locally (path hardcoded to this
  machine's own download location, gitignored, never fetched by this
  script). Every result (pass/fail/skip) is appended to a live-tailable log
  the instant it's known, not batched to the end.
- `corpus_scan_tasks/missing_texture_task.py` — a `corpus_scan_framework`
  `ScanTask` (see above): flags every `.m2` referencing a FileDataID-named
  texture slot with no exact `<FileDataID>.{blp,png}` file next to the
  model. On its own this is mostly a false-positive detector (a real
  extraction commonly keeps files under their real name elsewhere in the
  tree, which isn't a bug) — see this task's own module docstring and
  `casc-tool`'s `FAILURES.md` item 13 for the real cross-referencing
  methodology (against a real listfile *and* a full local `.blp` path
  index) needed to separate a genuine gap from a naming-convention
  mismatch. Motivated `husk export --listfile`/`--listfile-root`.
- `corpus_scan_tasks/expansion_task.py` — a `corpus_scan_framework`
  `ScanTask` tagging every `.m2` with its real M2 version, wowdev.wiki
  expansion label, and a coarse support tier (unsupported/sketchy/
  supported, per `DESIGN.md`'s own stated Legion+ target). **Real, but
  found to carry no useful signal** on a modern retail corpus -- 130,242 of
  130,576 real files are already version 272 or 274 (both Legion+ chunked),
  since the client re-saves every M2 in the current format regardless of
  the content's original expansion. Kept as a real, correct tool (and a
  second-opinion cross-check of `src/m2_primitives.cpp`'s own
  `expansionForVersion` table) but deliberately not wired into
  `live_gallery_server.py` given that null result -- see this task's own
  module docstring.
- `live_gallery_server.py` — a small stdlib-only HTTP server (not a
  corpus-scan task) that live-rescans a render output directory and serves
  a filterable, infinite-scroll gallery page, updating in real time via
  Server-Sent Events as new images land. Generic (works on any growing
  image directory, not husk-specific) but built for exactly this render
  pipeline's output shape — see its own module docstring for the full
  design and `--log`/`--listfile`-adjacent flags. Also filters by
  `world/expansionNN` era (a real WoW corpus folder convention, confirmed
  against actual zone content per folder -- covers `world/` doodad content
  only, not creature/character/item/spells, see `expansion_task.py`'s own
  entry above for why a file-version-based filter didn't work instead).
  Live updates diff and prepend newly-landed images instead of rebuilding
  the whole grid, so an active multi-hour render job doesn't stutter the
  page.

## One-off exploration scripts

Each of these is self-contained and documented in its own top-of-file
docstring — read the script for the real detail, not this list:

- `find_texture_type_collisions.py` — `M2Texture.type` collisions and
  whether `textureLookup` agrees with the real per-batch `textureCombos`
  resolution (see `TODO/WORLD/TEXTURE_TYPE_COLLISIONS_REPORT.md` for the full-corpus
  result).
- `find_texture_transform_files.py` — real files with a constant,
  texture-center-pivoted `M2TextureTransform` (the `KHR_texture_transform`
  math's source fixtures).
- `find_multiroot_skeletons.py` — real files with more than one root bone
  (the multi-root synthesized-parent-node feature's source fixtures).
- `find_m2_unknown_chunks.py` — chunk tags present in real files with no
  wowdev.wiki struct.
- `check_alias_next.py` / `check_detl_stride.py` — targeted single-field
  verifications (`M2Sequence::aliasNext`, `DETL` record stride).
- `corpus_checks.py` (+ `corpus_checks_example.py`) — a library of
  idempotent, per-file *husk-invoking* checks (runs the real `husk`
  binary and validates its output), not a standalone parser like the
  scripts above; own per-file status-JSON convention, own parallel-safety
  docstring.
- `corpus_test.py` — superseded by `corpus_checks.py`; kept as a
  reference for its own now-abandoned threading/tqdm/batching approach.
- `corpus_summarizer.py` — aggregates `corpus_checks.py`'s per-file status
  JSONs into a report.
- `husk_blender_geoset_mask.py` — not a corpus scanner: a `bpy` script
  building the geoset-selection Geometry Nodes graph in Blender itself,
  plus the `chr_texture_layout` overlay (see its own module docstring for
  the full mechanism).
