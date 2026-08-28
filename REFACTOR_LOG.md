# REFACTOR_LOG.md — running log of REFACTOR/ migration work

Each entry: what was done, why, what it deliberately did *not* touch, and how
it was verified. Punch-list items get removed from the `REFACTOR/*.md` files
they came from once done (see those files' own "closed items get removed"
convention) — this log is the narrative git history doesn't give you at a
glance, not a duplicate of the plan.

---

## 2026-08-28 — Stage 1/2 prep: DB2 parse/resolve cache (`src/sources/db2_cache`)

**What**: New `husk::sources` namespace (`src/sources/db2_cache.hpp/.cpp`) —
the first file under `src/sources/`, the home `REFACTOR/README.md`'s target
pipeline names for stage 2. `getParsedDb2(path, dbdDir, err)` caches, per
process, the result of reading a `.db2` file's bytes, running `db2::parse`,
and resolving its WoWDBDefs table/layout/inline-column names against
`dbdDir` — returning a `const ParsedDb2*` into the cache on every call after
the first for that `(path, dbdDir)` pair.

`src/db2table.cpp`'s `readNamedColumns`/`readNamedStringColumns` — previously
two independent implementations of "read file, parse WDC5, resolve DBD
layout" — now both call through this cache instead of repeating that work.

**Why this item first**: `REFACTOR/AUDIT.md` §1.3 names this exact
duplication ("the same file from scratch to get int columns and string
columns separately") and calls the fix "free: no semantic change, just the
table being read once." It's real, scoped, and carries no resolution-policy
risk — a good first slice precisely because it's boring. It's also
infrastructure the rest of stage 2 (`RESOURCE_CATALOG.md`'s `db2(tableName)
-> const Table&`) will want regardless of how the bigger texture-resolution
consolidation (§1.1/§1.2, still not started) ends up shaped.

**What this deliberately did not touch**: the actual resolution-boundary
object (`sources::Catalog`) `RESOURCE_CATALOG.md` describes — this is one
cache the catalog will eventually own, not the catalog itself. Texture
resolution (§1.1), FileDataID→path (§1.2), and the `chrrace::load` /
`texturefiledata::load` per-call-site duplication (§1.3's other two bullets)
are all still duplicated exactly as `AUDIT.md` describes them today — this
only fixed the "same file, two readers" half of §1.3, the smallest
independently-mergeable piece. Nothing in `chrrace_db2.cpp`,
`chrcustomization_db2.cpp`, `texturefiledata_db2.cpp`, or any call site above
`db2table.cpp` changed at all; they get the caching for free on their next
call into `readNamedColumns`/`readNamedStringColumns` without knowing it
happened.

**Correctness note for future test authors**: the cache key is
`(path, dbdDir)`, held for the lifetime of the process. `tests/test_db2table.cpp`
writes synthetic fixtures to unique-per-`TEST_CASE` temp directories
(`TestDbdDir`'s own `name` argument), so no two test cases ever reuse the
same path with different content within one `husk-tests` run — verified by
inspection, not just by the suite passing. A future test that reuses a fixed
temp path across cases with *different* db2 bytes would now read stale
cached content; `husk::sources::clearDb2CacheForTests()` exists for exactly
that case and should be called at the top of any test that needs a fresh
read of a path it has written to before.

**Verified**: full suite green, 694/694 (693 prior + this session's 1 new
implicit exercise via existing `test_db2table.cpp` cases, 0 regressions).
No behavior change intended or observed — `husk export`/`husk db2-export`
CLI-tier tests exercising the DB2 chains (`chrmodel`, `chrcustomization`,
`chrrace`, `texturefiledata`, `animationdata`) all pass unchanged.

---

## 2026-08-28 — Stage 1/2 follow-up: `attachCharTextureLayout`'s own double `chrrace::load`

**What**: Traced whether last entry's cache actually closed `AUDIT.md`
§1.3 in full, per that section's own claim ("`chrrace::load` runs from
three call sites... each call re-parses"). Confirmed every DB2 consumer in
this codebase goes through `db2table::readNamedColumns`/
`readNamedStringColumns` (grepped all nine `*_db2.cpp` modules), so the
raw-parse duplication is now genuinely gone everywhere, not just for
`chrrace`. But found one real duplication the cache doesn't reach:
`attachCharTextureLayout` (`src/export_extras.cpp:656`) calls
`tryDeriveChrModelId` — which loads a full `chrrace::Data` internally
just to derive one ID and then discards it — and then, three lines later,
calls `chrrace::load` *again* on the same `(db2Dir, dbdDir)` to get the
data back. Two `chrrace::Data` builds (vector construction, joins) back to
back in one function call, every time `--chr-model-id auto` (the default)
resolves for `--char-layout-id` auto-derivation.

Fixed by giving `tryDeriveChrModelId` an optional out-param
(`std::optional<chrrace::Data>* raceDataOut = nullptr`, default keeps the
other two call sites — `attachCustomizationChoices`' two branches —
untouched) that hands back the `chrrace::Data` it already built.
`attachCharTextureLayout` now only calls `chrrace::load` itself in the
explicit-`--chr-model-id`-value branch, where `tryDeriveChrModelId` was
never called at all.

**Why this one, not §1.1/§1.2 next**: `RESOURCE_CATALOG.md`'s real
texture-resolution/FileDataID-path consolidation (§1.1/§1.2) needs the
`sources::Catalog` object itself designed first — a multi-file, semantics-
touching change this project's own gate ("resolution ledger diff on real
fixtures") says shouldn't be rushed. This was a same-shape, same-file,
mechanically verifiable follow-up already flagged by the audit, cheap to
verify in isolation.

**Verified**: full suite green, 694/694, identical assertion count to the
pre-change baseline (0 regressions). Real end-to-end smoke test against
local data (`husk export test_data/character/bloodelf/female/
bloodelffemale_hd.m2 --db2-dir /media/luna/data/wow_export/dbfilesclient
--dbd-dir reference/WoWDBDefs`): output unchanged from the historical
baseline recorded in `CLAUDE.md`'s Resume section — ChrModelID 20, layout
122, 17 default choices — confirming the removed reload was genuinely
redundant, not silently load-bearing.

---

## 2026-08-28 — Stage 1/2: closed §1.3 in `AUDIT.md`; moved the reverse FileDataID↔path lookup into `src/sources/`

**What, part 1 (doc)**: Verified §1.3's claim is now fully addressed —
every `*_db2.cpp` module goes through `db2table::readNamedColumns`/
`readNamedStringColumns` (checked all nine modules), which share the
`db2_cache` from two commits ago, and `attachCharTextureLayout`'s own
extra `chrrace::load` (previous entry) is gone too. Removed §1.3 from
`AUDIT.md` outright, per that file's own "an item is removed when it's
fixed" convention, and updated `RESOURCE_CATALOG.md`'s dangling `§1.3`
citation to point at this log instead of a section that no longer exists.

**What, part 2 (code)**: `RESOURCE_CATALOG.md`'s stage-2 surface names
`fileDataIdForPath(path) -> Resolved<fdid>` as one of the catalog's real
jobs. `AUDIT.md` §1.2 already named the one existing implementation of
that direction — `findFileDataIdForModelPath`, a file-private static in
`export_extras.cpp:312`, a linear reverse scan over the `--listfile` map.
Moved it verbatim (no logic change) to `src/sources/listfile_catalog.hpp/
.cpp` as `sources::fileDataIdForPath`, the first inhabitant of what
`RESOURCE_CATALOG.md` calls `husk::sources::Catalog`. New
`tests/test_sources_catalog.cpp` (4 cases) — this function had zero direct
unit coverage before, only reachable through the `--chr-model-id auto`
CLI path (`tests/test_cli_chrmodel_id.cpp`).

**Real bug caught in the new test, not the moved code**: the first test
draft used `/root` as a fixture `listfileRoot`. `std::filesystem::relative`
internally canonicalizes both paths, and `/root` is a real, permission-
restricted directory on this machine — the call failed with EACCES
unrelated to any logic under test, initially read as a regression. Fixed
by using a `/tmp`-rooted fixture path instead; worth noting since any
future test in this codebase building similar path fixtures should avoid
`/root`, `/boot`, and other real restricted system paths as stand-ins for
"a directory that doesn't need to exist."

**What this deliberately did not touch**: the *forward* FileDataID→path
direction (`AUDIT.md` §1.2's other three implementations —
`export_materials.cpp`'s texture-tier lookup, `exportGearAuxItemModels`,
`resolveObjectSkinTextureFromKb`'s knowledge-base path plus its listfile-
map injection). Each returns a different shape (a texture stem to try
multiple extensions against vs. a full existence-checked model path) and
consolidating them for real needs the actual `sources::Catalog` object's
tier-order design (`RESOURCE_CATALOG.md`'s "Normative tier order"), not a
superficial merge. §1.2 stays open in `AUDIT.md`, now missing only its
first bullet.

**Verified**: full suite green, 698/698 (694 prior + 4 new, 0
regressions). `tests/test_cli_chrmodel_id.cpp`'s existing FileDataID-
primary-path coverage (the Dracthyr disambiguation case) passed unchanged,
confirming the move preserved behavior at the CLI level too, not just in
the new unit test.

---

## 2026-08-28 — `CLI_AND_TOOLING.md` §2: the missing `none` state for `--db2-dir`/`--dbd-dir`/`--listfile`/`--listfile-root`

**What**: `AUDIT.md` §7 and `CLI_AND_TOOLING.md` §2 both named a real,
scoped gap distinct from the "three grammars" observation they otherwise
just explain: since `--config`/`$HUSK_CONFIG` TOML defaults landed, a
config-supplied `db2-dir`/`dbd-dir`/`listfile`/`listfile-root` on `husk
export` had **no per-invocation opt-out** — the evidence cited was that
`tests/run_husk.hpp` has to blank the *entire* config
(`HUSK_CONFIG=/dev/null`) on every subprocess spawn just to get a clean
run. Added the literal value `'none'` to all four, same convention
`--textures`/`--skin-dir`/`--bones-dir` already use: `db2Dir`/
`dbdDirForChr` (`cmd_export.cpp`) now clear to empty on `'none'`, the
`--listfile` load is skipped outright, `listfileRoot` clears too. Fixed
the stale `--db2-dir`/`--dbd-dir` comment `AUDIT.md` §9 also flagged
("husk has no way to derive a layout ID on its own" — untrue since
2026-08-21's `--char-layout-id` auto-derivation) in the same edit, since
it sat directly above the code this touched.

**Scope decision**: `--db2-dir`/`--dbd-dir` are also registered on
`db2-build` (all three `->required()` there — no off-state is meaningful
for a command that can't do anything without them), `db2-info`, and
`appearance-string` (neither of which ever got `--config` wiring in the
first place, so neither has a config-supplied default to opt back out
of). Only `export`'s own instances gained `'none'` — extending it to the
others would be solving a gap that doesn't exist for them.

**Shell completions**: `bashValueCompletion`/`zshValueAction`
(`src/main.cpp`) are keyed only by flag *name*, shared across every
subcommand's own flag table — naively adding `'none'` there would have
offered it as a completion for `db2-build --db2-dir` too, where it isn't
actually honored. Made both functions take the subcommand name as a
second parameter so the `'none'`-offering branch can check `subName ==
"export"` before firing; `db2-build`'s own `--db2-dir`/`--dbd-dir`/
`--listfile` completions are unchanged (directory/file completion only,
no `'none'` word). `completions/husk.{bash,zsh}` regenerated via
`--print-completion`.

**New tests** (`tests/test_cli_config.cpp`, 2 cases): config supplies
`db2-dir`/`dbd-dir` pointing at real-but-empty directories, asserting the
customization-DB2-resolution attempt fires (a real "...DB2 data resolved
from..." stderr note) without `'none'` and doesn't fire with `--db2-dir
none --dbd-dir none`; same shape for `--listfile none` against a
config-supplied listfile path. **A real gotcha found writing these**: the
first draft used `tinyValidM2()` alone, which has zero inline bones —
`attachCustomizationChoices`/`attachCharTextureLayout` are both gated
behind `cmd_export.cpp`'s `if (!bones.empty())` block, so neither ever
ran and the assertion silently checked the wrong thing (a `MESSAGE()`
dump of the real subprocess output caught this, not guesswork). Fixed by
adding a same-basename `.skel` sidecar (`boneCorrectionSkel()`,
`tests/test_cli_fixtures_scenes.hpp` — the exact fixture
`tests/test_cli_chrmodel_id.cpp`'s own DB2 tests already use for the same
reason).

**Verified**: full suite green, 700/700 (698 prior + 2 new, 0
regressions). Real end-to-end smoke test against `bloodelffemale_hd.m2`
with real local DB2 data: `--db2-dir none --dbd-dir <real dir>` produces
the exact same three "--db2-dir/--dbd-dir are required... skipping"
notes a fully-unset run would, and a clean export (exit 0, 195498
vertices/45418 triangles/245 bones/338 clips) — confirming `'none'`
disables the feature even when a *different* real flag in the same
group is still given, not just when both are absent.

---

## 2026-08-28 — Three subagent tasks: README.md's stale Blender-addon
claim fixed, `AUDIT.md` §9/§10 closed/flagged, §1 flag grouping in
progress

Spawned three isolated-worktree subagents in parallel on independent
`AUDIT.md` items, per Luna's own request, then reviewed and merged each
by hand rather than trusting their own "done" claims.

**Agent 1 (DESIGN.md/README.md non-goal fix) — landed, half redundant**:
the agent's worktree was branched from `19e3ab6`, several commits behind
`master` — it never saw `31cfff7` ("Add REFACTOR/..."), which turned out
to have *already* fixed `DESIGN.md`'s stale "No Blender addon" non-goal
that same day, with different (strikethrough + "Superseded, 2026-08-28")
phrasing. The agent's `DESIGN.md` diff would have duplicated/conflicted
with that, so it was **discarded**. Its `README.md` fix (the Roadmap
section's identical stale claim — "husk doesn't write a Blender addon
itself... Blender-side concerns... are explicitly out of scope" — which
`31cfff7` never touched) was still genuinely needed and **was applied**,
by hand, adapted to point at `DESIGN.md`'s now-different phrasing rather
than cherry-picking the agent's commit verbatim. `AUDIT.md` §9 (both
bullets — the Blender-addon claim and the separately-already-fixed
`cmd_export.cpp` layout-ID comment from two commits ago) is now fully
resolved and removed.

**Agent 2 (file-size ceiling exception comments) — correctly refused,
real blocker found**: the task asked for one-line exception comments per
`FILE_SIZE.md` §4's format. The agent discovered `FILE_SIZE.md` **does
not exist anywhere in this repo's history** (`git log --all
--diff-filter=A -- FILE_SIZE.md` — never committed, on any branch) —
despite being cited as real by `AUDIT.md` §10 itself,
`REFACTOR/BLENDER_ADDON.md:46`, and two `tests/test_integration*.cpp`
files. Rather than fabricate a plausible-sounding exception-category
taxonomy that might contradict whatever was actually intended, the agent
stopped and reported the blocker — the right call. `AUDIT.md` §10 updated
to state this explicitly: the *policy* (1000-line ceiling, §4 comment
requirement) is real and consistently referenced, but the document that
was supposed to define it never got written. This item can't close via
comment-writing alone; it needs `FILE_SIZE.md` authored first, which is
Luna's call, not something to guess at autonomously.

**Agent 3 (`->group()` flag taxonomy, `CLI_AND_TOOLING.md` §1) — reapplied
by hand, not merged**: same root problem as Agent 1 — this worktree also
branched from `19e3ab6`, so its `src/cmd_export.cpp` predates every commit
this session made to that file (`ae04e39`'s `'none'` support and help-text
updates, most recently). Rather than resolve a diverged three-way merge,
took the agent's own grouping decisions (verified correct against
`CLI_AND_TOOLING.md` §1's table) and reapplied `->group("...")` to all 27
call sites (26 `add_option`/`add_flag` + the `--config` `set_config()`) by
hand on the current file. Two real discrepancies the agent found and
flagged, both confirmed independently before trusting them: `--textures`
exists in code but isn't in the table at all (placed under "Input /
output" — the agent's own judgment call, kept, since it sits with
`--textures-out`/`--slim-textures` and controls I/O-adjacent behavior, not
resolution policy the way `--skin-dir`/`--bones-dir` do); `--format` is in
the table but doesn't exist anywhere in the code (left alone — not
invented).

**Verified independently** (not just the agent's own claims): full suite
green, 700/700 in the real environment (694 + tonight's various additions,
0 regressions) — the agent's own reported "643/643, 52 skipped" was a
fixture-availability artifact of running from an isolated worktree missing
several `HUSK_TEST_*`-relevant symlinks/env, not a real discrepancy.
`husk export --help` shows all 7 group headers (`Diagnostics`, `Input /
output`, `Batch`, `Model sidecars`, `Game data`, `Character`, `Appearance /
gear`) in registration order. Diffed `--print-completion=bash`/`=zsh`
output byte-for-byte before and after — confirmed identical, so
`completions/` correctly needed no regeneration (the generator walks
`get_options()` directly, not the formatted `--help` groups).

**Overall verified**: full suite green, 700/700, after all of this
entry's changes (the README.md fix, both `AUDIT.md` trims, and the flag
grouping) landed together.

---

## 2026-08-28 — `AUDIT.md` §10 unblocked: `FILE_SIZE.md` symlinked in,
real per-file exception comments added

Luna corrected the "`FILE_SIZE.md` doesn't exist" finding from the
subagent above: it exists at `~/nix/claude-rules/FILE_SIZE.md`, the same
home this repo's own `.claude/rules/nix.md` already symlinks from for
Nix conventions. Symlinked `FILE_SIZE.md` (repo root) →
`~/nix/claude-rules/FILE_SIZE.md`, matching that established convention
— every citation in this codebase (`AUDIT.md`, `BLENDER_ADDON.md`,
`tests/test_integration*.cpp`) already refers to it as a bare
repo-root-relative name.

With the real policy doc in hand, read it properly (its §3 names three
genuine exceptions: a chain-not-hub-and-spoke split, one sequential
process with no real seams, or a flat list of many small identically-
shaped items — and its §4 is explicit that the required comment is "not
a paragraph — a pointer") and checked each of the four oversized files'
actual structure against those three categories rather than writing a
generic placeholder for all four:

- `src/export_extras.cpp` — genuinely fits §3's third exception: ~9
  independent, identically-shaped `attachX()` enrichment functions, each
  readable alone.
- `src/cmd_export.cpp` — a real mix, not one clean fit: `addExportOptions`/
  the `resolve*` helpers are §3's third exception (flat, independent),
  but `exportOneModel` itself is §3's second (one genuinely sequential
  per-model pipeline). Said so explicitly rather than picking one
  arbitrarily.
- `tools/husk_blender_geoset_mask.py` / `tools/corpus_scan_tasks/render_glb.py`
  — neither fits any of the three genuine exceptions honestly; both are
  oversized because real addon-module packaging (`REFACTOR/BLENDER_ADDON.md`)
  hasn't happened yet. Said that plainly instead of forcing a false
  exception claim — `FILE_SIZE.md` §2's own default ("if none of these
  genuinely apply... split it") makes clear an exception should never be
  claimed just to satisfy the ceiling-comment requirement.

One-line comments (not paragraphs, per §4) added near the top of all
four files, each naming its real disposition and, where relevant, which
`REFACTOR/` stage is expected to resolve it.

**Verified**: full suite green, 700/700, 0 regressions (C++ files
rebuilt and tested; both Python files independently confirmed to still
parse via `ast.parse` through `tools/venv`'s own interpreter). `AUDIT.md`
§10 removed outright now that it's genuinely closed, not just
worked around.

---

## 2026-08-28 — `AUDIT.md` §11 closed: `missing_texture_task.py` deleted,
`shader_id_task.py`'s stale claim corrected (partially — real blocker
found)

**`missing_texture_task.py`**: superseded by `unfillable_texture_task.py`
per its own module doc, but still present and runnable — the exact
"future session picks the wrong one" risk `CLAUDE.md`'s own Hazards
section already had to warn about. Moved to `./trash/` (not `rm`, per
this loop's own instruction). Verified no code depends on it (grep across
`tools/` found only comment-level "same cost profile as"/"matches this
convention" mentions, no imports) before moving it, then fixed every
surviving reference rather than leaving them dangling: `tools/CORPUS_SCANS.md`'s
task table (row removed) and its own "rule going forward" paragraph
(rephrased to cite the deleted file as historical precedent, not a live
example); `particle_only_task.py`/`casc_size_mismatch_task.py`'s
comparison comments repointed at `unfillable_texture_task.py` (dropped
one comparison entirely rather than inventing a citation
`unfillable_texture_task.py` doesn't actually support — the `_unresolved/`
FileDataID convention isn't documented in either survivor, only casc-tool's
own README); `unfillable_texture_task.py`'s own docstring updated to
describe superseding a deleted file rather than differing from a live
one; `TOOLS.md`'s tool catalog (entry removed, it's current-state
documentation, unlike `CLAUDE.md`'s session-narrative mentions which were
left alone as accurate history); `TODO/RENDER_QUALITY_TODO.md`'s one
cross-reference repointed.

**`shader_id_task.py`**: confirmed the stale claim directly — its
docstring said *"husk currently parses no field of M2Batch's on-disk
shader_id"*; `src/skin.hpp:70`/`skin.cpp:195` (`Batch::shaderId`) and
`src/m2_shader_names.hpp` (`resolveShaderNames`) both do, and the exact
byte offsets the Python task hand-parses (`_SHADER_ID_OFFSET = 0x02`,
`_TEXTURE_COUNT_OFFSET = 0x0E`, `_BATCH_STRIDE = 0x18`) were checked
directly against `skin.cpp`'s real `parseBatches` and confirmed to match.
Fixed the false claim. **But the reclassification `RESOURCE_CATALOG.md`
itself predicted ("almost certainly structured output") turned out
premature, not just a rename**: grepped `cmd_info.cpp`/`cmd_dump.cpp` for
any consumer of `shaderId`/`resolveShaderNames` and found none — both are
used *only* internally by `export_materials.cpp`. Neither `husk info` nor
`husk dump-chunks` exposes this field or its resolved name anywhere in
their own output today, so there is currently nothing structured for the
corpus task to consume instead of reading raw bytes. Rewrote the
docstring to state this explicitly, satisfying `RESOURCE_CATALOG.md`'s
own excavation-escape-hatch rule ("a task that reads raw bytes must say
in its own docstring which husk understanding it is deliberately going
behind") — the raw read is now a documented, considered exception, not
an unconverted leftover, pending `CLI_AND_TOOLING.md` §3's structured-
output work (`husk info --json`/`husk resolve`, not started).
`shader_names_task.py` was checked too (same underlying fact) and found
to already document this correctly — no fix needed there, confirmed by
reading rather than assumed from `RESOURCE_CATALOG.md`'s table alone.

`RESOURCE_CATALOG.md`'s own corpus-task classification table and
`CLI_AND_TOOLING.md` §4's "Also" bullets updated to match the real,
partial disposition (docstring fixed; reclassification blocked on real,
cited, unstarted work) rather than left claiming a clean "Delete"/
"Reclassify" that oversimplifies what's actually true now. `AUDIT.md`
§11 removed outright, both items genuinely resolved.

**Verified**: full suite green, 700/700 (unaffected by this entry --
C++ untouched). All four edited Python files independently confirmed to
still parse via `ast.parse`. Swept for dangling `missing_texture_task`
references across the whole repo afterward; every remaining hit is a
deliberate historical/explanatory mention, not a live dependency.

---

## 2026-08-28 -- close AUDIT.md §8: corpus-tooling CORPUS_ROOT/HUSK_BIN/LISTFILE duplication

**What**: `corpus_scan_framework.py` now exposes `ROOT`, `LISTFILE`, and
`HUSK_BIN` as single, dynamically-read module-level values instead of each
task hardcoding its own copy. `ROOT`/`LISTFILE` are set once per run by
`_init_worker` (from the real `--root`/`--listfile` the run was launched
with -- `--listfile` is a new framework CLI flag, previously nonexistent);
`HUSK_BIN` is computed once at import time. `black_additive_task.py`,
`casc_size_mismatch_task.py`, `unfillable_texture_task.py`,
`texture_dedup_collision_task.py`, `m2_full_validation_task.py`, and
`particle_only_task.py` had their own `CORPUS_ROOT`/`HUSK_BIN`/`LISTFILE`
constants deleted and now read `corpus_scan_framework.ROOT`/`.LISTFILE`/
`.HUSK_BIN` (aliased as `csf` at import) instead.
`corpus_checks.py`'s own separate `HUSK_BIN` default (used only by
`m2_full_validation_task.py` and `corpus_checks_example.py`) was fixed at
its one real source rather than overridden per-caller.

**Why**: `CLI_AND_TOOLING.md` §4 named this as three distinct duplication
bugs (`CORPUS_ROOT` in 6 modules, `HUSK_BIN` in 7, `LISTFILE` in 5) and
prescribed subtraction, not relocation -- a task should never declare its
own copy of something the framework already knows.

**Two real bugs found and fixed while implementing this, neither
hypothetical -- both caught by a real smoke-test run against 40 live
corpus files, not assumed from reading the doc**:

1. `CLI_AND_TOOLING.md` §4's own prescribed fix for `HUSK_BIN` (a bare
   `"husk"`, trusting its own claim that the flake dev shell puts husk on
   `PATH`) fails outright on the real environment -- verified live,
   `.direnv/bin` carries no `husk` symlink. Luna's own correction after
   this landed: not a dev-shell bug -- installing the flake as a package
   (`nix profile install`/`nix run`) does put `husk` on `PATH`, that's
   just not the dev-shell environment this corpus tooling actually runs
   under (a dev shell hands you the build tools, not the project's own
   not-yet-rebuilt output, by design). Fixed with
   `shutil.which("husk") or str(REPO_ROOT / "build" / "husk")`, the same
   `shutil.which`-first idiom `corpus_checks.py` already used for
   `GLTF_VALIDATOR_BIN` -- PATH first (correct for a package install),
   falling back to the known-good local build path (correct in the dev
   shell today).
2. Every task module's own docstring documents running
   `corpus_scan_framework.py` directly as a script
   (`python tools/corpus_scan_framework.py --task ...`), which loads it as
   `__main__` -- a *different* module object from the
   `corpus_scan_framework` a task gets via its own
   `import corpus_scan_framework as csf`, with independent globals.
   `_init_worker` was setting `ROOT`/`LISTFILE` on whichever identity
   actually ran, while every task read them off the other, untouched,
   still-`None` one (`AttributeError: 'NoneType' object has no attribute
   'exists'`). Fixed with one `sys.modules.setdefault("corpus_scan_framework",
   sys.modules[__name__])` at module load, so both names always resolve to
   the same object regardless of which one loaded first. `_init_worker`
   was also reordered to set `ROOT`/`LISTFILE` *before* importing the task
   module (a task's own module-level code, e.g. `m2_full_validation_task.py`'s
   `cc.CORPUS_ROOT` reconciliation, can only see the real value if it's
   already set by then) -- though `m2_full_validation_task.py` itself was
   further changed to set `cc.CORPUS_ROOT` lazily inside `analyze()`
   rather than at module import time regardless, since under
   `ProcessPoolExecutor`'s default fork start method a forked worker
   inherits the parent's already-executed top-level code rather than
   re-running it, so import-time assignment alone isn't reliable across
   start methods.

**What was deliberately not touched**: `render_sample_driver.py` -- a
driver script with its own argv (not a `ScanTask` invoked through
`corpus_scan_framework.py --task`), and part of the render pipeline this
project has repeatedly treated as human-gated (`CLAUDE.md` Hazards,
`feedback_knowledge_base_render_human_gated` memory). It still has its own
`CORPUS_ROOT`/`HUSK_BIN`/`LISTFILE` copies -- a real follow-up if that
pipeline is ever folded into this same mechanism, not attempted here.
`verify_appearance_string_pipeline.py` and `corpus_test.py`'s own
hardcoded `HUSK_BIN` copies were also left alone: the former is a
standalone build-check script with an intentionally different design (a
friendly `.exists()` check plus a build hint, not the same disease), and
the latter is `corpus_checks.py`'s own docstring-documented "kept as-is,
untouched, reference for its own now-superseded approach" predecessor.

**Verified**: full suite green, 700/700 (C++ untouched by this entry).
All touched Python files confirmed to still parse via `ast.parse`.
Real end-to-end smoke tests against live local corpus data (not just
syntax checks) for every touched task, each with `--limit` against
`/media/luna/data/wow_export`, zero errors in every case:
`unfillable_texture_task` (40 files), `black_additive_task` (40),
`casc_size_mismatch_task` (40, thread-mode, exercises the real
`casc-tool list` subprocess), `texture_dedup_collision_task` (30),
`particle_only_task` (30), `m2_full_validation_task` (10, the task with
CLEANUP_TODO.md's own known full-corpus-scale hang -- deliberately kept
to a small `--limit`, consistent with that item's own note that small
bounded runs are clean). Also re-ran one untouched task
(`expansion_task`, 20 files) through the now-modified shared framework to
confirm the framework-level changes didn't regress a task that never
touched `ROOT`/`LISTFILE`/`HUSK_BIN` at all. `AUDIT.md` §8 and
`CLI_AND_TOOLING.md` §4 updated to reflect the real, verified disposition
(including both bugs found, and the deliberate `render_sample_driver.py`
exception) rather than the original speculative fix description.

---

## 2026-08-28 -- correct HUSK_BIN doc wording per Luna's live correction

**What**: Luna corrected the previous entry's framing immediately after it
landed: "the flake does put the husk in the path, if the flake is
installed, not so in the dev shell." The previous entry's docs described
`CLI_AND_TOOLING.md` §4's original "the flake dev shell already puts husk
on PATH" claim as simply wrong -- overstated. It's accurate that
installing the flake as a package (`nix profile install`/`nix run`) does
put `husk` on `PATH` via the normal Nix mechanism; what's actually true
(and what the live smoke test caught) is narrower: the *dev shell*
(`direnv exec .` / `nix develop`) doesn't, by design -- a dev shell hands
you the tools to build the project, not the project's own not-yet-rebuilt
output. Corrected the framing in `AUDIT.md` §8, `CLI_AND_TOOLING.md` §4,
and `corpus_scan_framework.py`'s own `HUSK_BIN` comment. This file's
previous entry is left as-is (historical record, not edited after the
fact).

**Why**: precision matters here specifically because the wrong framing
would have pointed a future reader at "fix the flake's dev shell" as the
real fix, when that's not actually broken for its own purpose --
the `shutil.which`-first fallback already committed is correct exactly
because both cases (package-installed vs. dev-shell-only) are real and
need to be handled, not because one of them is a bug to fix upstream.

**Verified**: comment/doc-only + one code-comment change in
`corpus_scan_framework.py`; re-parsed via `ast.parse`, no behavior change
(the `shutil.which(...) or <build path>` logic itself is untouched).

---

## 2026-08-28 -- close AUDIT.md §7/CLI_AND_TOOLING.md §5: `--knowledge-db`'s known-wrongness now surfaces at point of use

**What**: `src/cmd_export.cpp`'s `--knowledge-db` handling now prints a
real `std::cerr` warning at the exact point a knowledge-base resolution
is about to be used (naming the resolved texture FileDataID and pointing
at `TODO/KNOWLEDGE_BASE_DESIGN.md`), instead of the flag's documented
known-wrongness (same-slot cross-item collisions, 15/15 real spot checks
wrong-item) living only in `CLAUDE.md`'s Hazards section -- a note a
caller of this flag might never read. First CLI-tier test coverage this
flag has ever had: `tests/test_cli_knowledge_db.cpp`, a new file with a
minimal synthetic SQLite fixture built directly against the exact three
raw tables/columns `resolveObjectSkinTextureFromKb` queries (not `husk
db2-build`'s real view/join machinery, which the runtime code doesn't
care about), covering both a real resolution (warning printed, names the
right FileDataID) and a miss (no warning, matching the function's own
"empty/0 is a real, expected case" contract).

**Why**: `CLI_AND_TOOLING.md` §5 framed this as needing an explicit
decision -- retire the flag, or keep it with its known-wrongness surfaced
at point of use (I4). Checked `TODO/KNOWLEDGE_BASE_DESIGN.md` first
rather than deciding fresh: that file already made this exact call
("kept as diagnostic/future-work infrastructure, not load-bearing";
`render_sample_driver.py` deliberately never passes `--knowledge-db`).
Retiring the flag here would have silently reopened an already-settled
decision instead of executing it -- the actual gap was narrower than "no
decision has been made," it was "the decision that *was* made was never
wired into the CLI's own output."

**What was deliberately not touched**: the flag still mutates the shared
listfile map mid-export to let the embed path pick up its answer
(`cmd_export.cpp`'s `listfile.emplace(...)` call) -- a real instance of
`AUDIT.md` §1.2's FileDataID→path duplication. Fixing that means the
catalog (`RESOURCE_CATALOG.md`, not built), not a local patch here.

**Verified**: full suite green, 702/702 (700 + 2 new, 0 regressions).
Both new tests independently confirmed passing in isolation
(`-tc="*knowledge-db*"`) before the full-suite run. `AUDIT.md` §7 and
`CLI_AND_TOOLING.md` §5 updated to the real, verified disposition.

---

## 2026-08-28 -- partial AUDIT.md §1.2: forward FileDataID -> path consolidation (2 of 4 sites)

**What**: `src/sources/listfile_catalog.hpp`/`.cpp` (already home to
`fileDataIdForPath`, the reverse-direction lookup from an earlier
session's §1.3 close) gained `pathForFileDataId(listfile, listfileRoot,
fdid) -> optional<path>` -- the forward direction. Two of the four sites
`AUDIT.md` §1.2 named now call it instead of duplicating `listfile.find(fdid)`
+ a `listfileRoot` join inline: `export_materials.cpp`'s texture-tier
`--listfile` fallback, and `cmd_export.cpp`'s `exportGearAuxItemModels`.
Each caller's own post-lookup behavior (extension stripping in one,
an explicit existence check + diagnostic text in the other) stayed
exactly where it was -- only the identical "look it up" step moved.

**Why**: continuing the same incremental pattern §1.3's reverse-direction
move already established, rather than waiting for the full
`sources::Catalog` object `RESOURCE_CATALOG.md` describes (multi-session
work, not a single loop tick) -- this is explicitly framed there as the
"free half of stage 2" category db2_cache.hpp already used: consolidating
duplicated *mechanism* now, leaving duplicated *policy* (ranking,
provenance/`Resolved<T>`, caching) for when the real catalog object is
built.

**A real behavior-preservation subtlety caught before landing, not
assumed**: the two call sites disagreed on whether an empty
`listfileRoot` was even checked. `exportGearAuxItemModels` already
early-returns before ever reaching the lookup if `listfileRoot` is empty;
`export_materials.cpp`'s site never checked that at all, relying on
`std::filesystem::path("") / rel` acting as an identity join for a caller
that passes `--listfile` without `--listfile-root`. A first draft of
`pathForFileDataId` early-returned `nullopt` on an empty `listfileRoot`
unconditionally -- which would have been a real, if obscure, behavior
change for that specific edge case (the fallback tier silently stops
firing instead of trying a CWD-relative path). Fixed by dropping that
check from the shared helper entirely (only `listfile.empty()` is
checked) -- `exportGearAuxItemModels` still gets the same effective
guarantee via its own existing early return, and `export_materials.cpp`
gets its exact original behavior back.

**Deliberately left alone**: `resolveObjectSkinTextureFromKb`'s
knowledge-base SQLite lookup (a genuinely different backing store, not
the same duplication) and its listfile-map injection
(`cmd_export.cpp`'s `listfile.emplace(...)` after a KB hit -- itself
duplication-adjacent surface, but a real catalog-object question, not a
verbatim-move one); `unfillable_texture_task.py`'s own Python
`_load_listfile`, outside this C++ consolidation's reach entirely.

**Verified**: full suite green, 706/706 (702 + 4 new, 0 regressions) --
4 new unit tests in `tests/test_sources_catalog.cpp` for
`pathForFileDataId` itself (a real join, empty-listfile miss, unknown-fdid
miss, and the empty-listfileRoot identity-join case specifically,
regression-testing the subtlety above), plus the full existing
`test_cli_textures.cpp`/`test_cli_gear_export.cpp` suites (both call
sites' own real CLI-tier coverage) passing unchanged -- confirms
behavior preservation end-to-end, not just at the new unit's own level.
