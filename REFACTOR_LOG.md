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
