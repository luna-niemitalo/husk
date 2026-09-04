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
- `corpus_scan_tasks/casc_size_mismatch_task.py` — checks every real file's
  on-disk byte count against CASC's own reported size for that FileDataID
  (one `casc-tool list` dump loaded into memory, then a cheap per-file dict
  lookup + `os.stat()` — `PARALLEL_MODE = "thread"` since there's no real
  per-file CPU cost).
- `corpus_scan_tasks/dangling_references_task.py` — the completeness
  counterweight: of the internal cross-references husk already knows how to
  resolve (bone/sequence/attachment/camera/texture lookups, plus `.skin`-
  dependent kinds), how many actually point at something real, corpus-wide,
  per reference kind — not just "is this field present."
- `corpus_scan_tasks/unfillable_texture_task.py` — real files whose actually-
  used texture slots resolve to nothing local under any of husk's own three
  tiers (literal FileDataID, `--listfile`, same-basename fuzzy). Shells out
  to `husk resolve` rather than re-deriving the tier order in Python — a
  hand-mirrored copy of this logic silently broke once for real (see
  `CLAUDE_HISTORY.md`'s 2026-08-15/16 entries). Supersedes the deleted
  `missing_texture_task.py`.
- `corpus_scan_tasks/black_additive_task.py` — flags models likely to render
  as flat background color: every material additive-family blend mode *and*
  the resolved primary texture is genuinely black pixel content (decoded via
  `husk blp-export`, cached by FileDataID). The non-particle-driven sibling
  of `particle_only_task.py` below.
- `corpus_scan_tasks/particle_only_task.py` — heuristic candidate list for
  models whose only real visible content is an M2Particle emitter husk can't
  bake into geometry: every material additive-family blend mode *and* at
  least one real particle emitter. A structural signal, not a confirmed-blank
  proof — treat the output as a human-spot-check list.
- `corpus_scan_tasks/shader_id_task.py` — real-corpus `M2Batch::shader_id`
  scan for `TODO/MULTI_TEXTURE_LAYER_TODO.md`: how often the 0x8000
  table-lookup path fires, how often `textureCount > 1`, how often a
  multi-texture batch has `shader_id == 0` (unresolvable either way).
- `corpus_scan_tasks/shader_names_task.py` — goes one step past
  `shader_id_task.py`: resolves each batch's `(shaderId, textureCount)` to
  its real `{pixel, vertex}` shader name pair (mirroring
  `m2::resolveShaderNames` in Python), answering which undocumented
  `Combiners_*` pixel shaders actually get exercised by real files.
- `corpus_scan_tasks/texture_dedup_collision_task.py` — how often a real
  multi-texture-layer file has two texture slots whose resolved byte content
  is identical — the shape that silently broke Blender's fdid->Image lookup
  in the `ladywaycrest` bug (commit `cd11c85`).
- `corpus_scan_tasks/animated_texture_effects_task.py` — how many real files
  have a genuinely-animated (not merely constant) texture-transform/color-
  tint/alpha-fade/texture-weight track, quantifying the "spinning sigil /
  pulsing rune" visual gap before investing in playback infrastructure for it.
- `corpus_scan_tasks/detect_billboards.py` — scans for the `billboard`
  property on bone records via `husk info --json`, for investigating bone
  counts and hierarchy position of billboarded bones.
- `corpus_scan_tasks/m2_full_validation_task.py` — the heaviest per-file
  check: independent header parse cross-check, a rich `husk export`
  (textures deliberately off, kept disk/speed-bounded), `dump-chunks`, and
  fidelity/finite/mesh-completeness glb-content checks. Deliberately skips
  `gltf_validator` (Dart VM per-invocation cost across 130k+ files) — that
  runs against the smaller Blender-sample set instead.
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
  `live_gallery/server.py` given that null result -- see this task's own
  module docstring.
- `live_gallery/server.py` — a small stdlib-only HTTP server (not a
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
  page. Also serves `/review`, a fast keyboard-driven ("captcha style")
  triage page for blasting through the whole corpus flagging bad renders —
  numpad-mapped 3x3 grid, one Enter per page, decisions appended to a
  resumable JSONL log — reachable via the "tag review" link in the main
  page's header (opens in its own tab). Formerly a separate standalone
  server (`tag_review_server.py`); merged into `live_gallery/server.py`
  2026-08-14 so tagging and live-browsing share one process/one `--root`
  instead of two servers that could drift out of sync. `--review-out`
  controls the decision log path (defaults to `<root>_review.jsonl`, same
  convention the standalone server used); `categorize_flagged_renders.py`/
  `auto_flag_detected_failures.py` read/write that same JSONL log,
  unchanged.

## Shader-formula pattern search: `shader_pattern_search/`

Parses vkd3d-compiler `d3d-asm` text (`references/wow_shaders/asm/*.asm`)
into a def-use dataflow graph and searches it for known shader-math shapes,
for the `PIXEL_SHADER_FORMULAS_TODO.md`/`SHADER_SCAN_FINDINGS.md` combiner
hunt — see its own `README.md` for full detail. Two independent approaches:

- **Textual pattern matching** (`ir.py`/`patterns.py`/`scan.py`) — parses
  into a dataflow graph, matches registered instruction-shape patterns
  against it. Fast, but only recognizes the exact instruction encoding each
  pattern was written against.
- **Invariant-based matching** (`decompose.py`/`eval_engine.py`/
  `invariants.py`/`formula_specs.py`/`equivalence.py`/`constant_output.py`)
  — decomposes every shader into standalone blocks, then concretely
  *evaluates* each against sampled inputs and tests numeric properties true
  of the target math regardless of encoding (e.g. "is this affine in
  alpha"). Built after the textual approach was confirmed to miss real
  matches; stronger, but only covers formula families with invariants
  actually written for them. `equivalence.py` does full black-box numeric
  equivalence testing against `formula_specs.py`'s candidate formulas;
  `constant_output.py` covers the separate case of a hardcoded-constant
  output (e.g. `Illum`, always-black) that equivalence testing can't
  express.

Every match from either approach is structural/numeric, not semantic — a
survivor still needs a manual read before it counts as a real finding.

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
- `husk_blender_options_panel.py` + `test_husk_blender_options_panel.py` —
  a live, persistent Blender N-panel (deliberately separate from
  `husk_blender_geoset_mask.py`, which runs once at import time and is
  done) for browsing/editing a character's real customization-choice menu
  after import: one row per `ChrCustomizationOption`, a dropdown of its
  real `Choice`s, driving the texture-switch node graphs
  `apply_customization_texture_switch` built. Meant to travel with the
  `.blend` file itself as a registered embedded `Text` datablock — see
  `BLENDER_OPTIONS_PANEL.md` for the design writeup and
  `test_husk_blender_options_panel.py` for its own headless verification
  (`blender --background --factory-startup --python
  tools/test_husk_blender_options_panel.py`).
- `derive_texture_tag_vocabulary.py` — a pure filename walk (no husk
  subprocess, no `corpus_scan_framework`) deriving a texture-tag vocabulary
  and its co-occurrence structure off the real `.blp` corpus, for
  `TODO/TEXTURE_POOL_RECALL_TODO.md` step 1.
- `full_render.py` — runs the full `husk export` → Blender render pipeline
  over the whole corpus via fresh directory discovery (not a stale
  pre-generated file list), honoring a `.renderignore` file (gitignore-style
  subset) at the repo root. Resume-safe, same convention as
  `corpus_scan_tasks/render_sample_driver.py`.
- `export_hd_characters.nu` — batch-exports every real `*_hd.m2` player
  character model to `.glb` via a loop over `husk export` (husk itself has
  no batch mode built in yet for this shape).
- `playwright_mcp_launch.nu` — launch shim registered with `claude mcp add`
  that resolves `PLAYWRIGHT_BROWSERS_PATH` via `direnv exec` at launch time
  instead of a hardcoded `/nix/store/<hash>-...` path, so a nixpkgs bump
  doesn't silently rot the registered MCP server path.
- `shader_dump_watcher.nu` — watches a vkd3d-proton `VKD3D_SHADER_DUMP_PATH`
  directory for newly captured shaders and fires a desktop notification per
  new hash (deduped on `.spv` creation only), to correlate a live capture
  session against in-game context.

### Battle.net Profile API tools

- `blizzard_profile_fetch.py` — fetches a character's public Battle.net
  Profile API data (summary, equipment/transmog, appearance, character-media
  render URLs) via OAuth client-credentials (no user login needed), saving
  each payload as JSON. `--race`/`--sex` also emits a `husk-appearance/1`
  string in the same run via the next tool below.
- `blizzard_profile_to_appearance_string.py` — converts Blizzard Character
  Equipment/Appearance Summary API JSON into a `husk-appearance/1` string
  (`src/appearance_string.hpp`'s grammar). Field-path assumptions live in
  one `ASSUMED_PATHS` table, verified against a real payload rather than
  guessed from Blizzard's docs.
- `verify_blizzard_api_schema.py` — live-checks `ASSUMED_PATHS` above
  against a real Blizzard Profile API response, printing expected-vs-got
  per field.
- `verify_appearance_string_pipeline.py` — end-to-end pipeline check
  (synthetic Blizzard JSON → `blizzard_profile_to_appearance_string.py` →
  `husk appearance-string --validate`), printing expected-vs-got at each
  step.

### NOTES

* Do NOT use RM, nor let subagents use it, that will immidiately prompt me and stuck the whole toolchain
* python via UV in tools/venv
* listfile in ~/Downloads/community-listfile.csv (did not spell check the name)
* current casc export /media/luna/data/wow_export
* current wow /media/luna/games/World of Warcraft
* casc tool ~/dev/casc-tool
* if you or subagent needs to chain several commands in bash, write a ephemereal scratch dir tool script
* NO ROOT FINDS
