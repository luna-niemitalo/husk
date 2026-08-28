# CLI_AND_TOOLING.md — flag surface, structured output, corpus tooling

**Target, not current state.** See `REFACTOR/README.md` for the pipeline and the
invariants referenced by number below.

The goal Luna stated for the tool as a whole:

> simple to pick up and use — it does the thing you'd want with minimal params,
> but if one has a more specific use case, params can be provided to express
> intent.

The `auto | <value> | none` convention is that goal made concrete, and it already
works. The problems below are that it isn't applied consistently, that the flag
list has no taxonomy, and that the tooling around husk re-derives what husk
already knows.

---

## 1. Taxonomy: 26 flags, no groups

**Done** (`src/cmd_export.cpp`'s `addExportOptions`, see `REFACTOR_LOG.md`'s
2026-08-28 entry) — every option below now carries a real `->group(...)`
call; `husk export --help` shows the 7 headers below instead of one flat
list. Two real discrepancies found while doing this, neither invented:
`--textures` exists in code but was missing from the table entirely (placed
under "Input / output"); `--format` is in the table but doesn't exist in
code at all (left alone). `--print-completion` output is confirmed
byte-identical before/after, since the completion generator walks
`get_options()` directly rather than the formatted `--help` groups — so
`completions/` needed no regeneration for this change specifically.

`export` used to register 26 options (`cmd_export.cpp:491-673`) with **zero
`->group()` calls** — `CLI.md` §1's flat-namespace failure verbatim: one
undifferentiated list the user must hold entirely in memory or re-search.

The grouping actually used, each nameable in under three words (`CLI.md`
§2.1):

| Group | Flags |
|---|---|
| Input / output | `--input`, `--output`, `--format`, `--slim-textures`, `--textures-out` |
| Model sidecars | `--skin`, `--skin-dir`, `--skel`, `--anim`, `--bones-dir`, `--phys`, `--lod`, `--collision` |
| Game data | `--db2-dir`, `--dbd-dir`, `--listfile`, `--listfile-root` |
| Character | `--chr-model-id`, `--char-layout-id`, `--customization-choice-ids`, `--creature-display-id` |
| Appearance / gear | `--appearance`, `--object-skin-texture-id` |
| Batch | `--from-list`, `--output-dir` |
| Diagnostics | `--knowledge-db`, `--config` |

Grouping *is* documentation, and it must reach `completions/` — regenerated via
`--print-completion`, never hand-edited.

---

## 2. One grammar, and the honest reason it isn't universal

Three grammars for one shape of question today (`AUDIT.md` §7):

- `--anim` — four-state: `auto` / `inline` / `none` / path
- `--skin`, `--textures`, `--skin-dir`, `--skel`, `--bones-dir`, `--phys` —
  three-state
- `--db2-dir`, `--dbd-dir`, `--listfile`, `--listfile-root` — **was**
  two-state; now three (`value` / unset / `'none'`), see "The state that
  *is* missing" below — **done**.

### Why `auto` is not universal — write this down

The two-state group is **not** an oversight, and the reason belongs in
`DESIGN.md`'s three-state section, which currently states the rule without its
precondition:

> `auto` is only honest when the input describes where the thing is.

An M2's `SFID` / `SKID` / `AFID` / `BFID` chunks genuinely say which
`.skin`/`.skel`/`.anim`/`.bone` files belong to it — `auto` there is derivation
from real data. `--db2-dir`, `--dbd-dir`, `--listfile` and `--listfile-root`
point at external tools and checkouts with no canonical location; an `auto`
there would be husk guessing at someone's filesystem layout and being
confidently wrong. Guessing is worse than asking.

### The state that *was* missing is `none`

**Done** (`src/cmd_export.cpp`, `REFACTOR_LOG.md`'s 2026-08-28 entry) —
`--db2-dir`/`--dbd-dir`/`--listfile`/`--listfile-root` all accept `'none'`
now, explicitly overriding a `--config`/`$HUSK_CONFIG`-supplied value for a
single invocation. Deliberately scoped to `export` only — `db2-build`'s own
same-named flags are all `->required()` (no off-state is meaningful when
the command can't do anything without them), and `db2-info`/
`appearance-string` never got `--config` wiring in the first place, so
they have no config default to opt back out of. Shell completions made
subcommand-aware (`bashValueCompletion`/`zshValueAction` now take the
subcommand name) so `'none'` is only suggested where it's actually
honored.

Previously: since config-file defaults landed, a configured `listfile` /
`db2-dir` couldn't be switched off for a single invocation at all — no
per-flag opt-out existed. The evidence this was a real gap rather than a
theoretical one: `tests/run_husk.hpp` has to blank the **entire** config
(`HUSK_CONFIG=/dev/null`) on every subprocess spawn to get a clean run —
because three tests were silently picking up this machine's real
`~/.config/husk/config.toml` and failing for reasons unrelated to what
they were testing. (`run_husk.hpp`'s blanket `HUSK_CONFIG=/dev/null` stays
as-is even after this fix — it's still the right blanket safety net for
every *other* test that doesn't care about config behavior specifically;
only the tests exercising `'none'` itself pass `--config` explicitly to
work around it.)

---

## 3. Structured output

`husk info` emits human-readable text only. `dump-chunks` emits JSON. Eight
corpus tasks regex-scrape the former, with patterns like
`^\s*particle_emitters: (\d+) ` compiled against prose husk never promised to
keep stable.

Add `husk info --json` at minimum, and consider a `husk resolve` verb exposing
the catalog's own answers (`RESOURCE_CATALOG.md`) — that is what lets a corpus
task consume husk's resolution instead of reimplementing it.

`describe()` / `--explain` output serves double duty here: it satisfies I4 for
interactive use *and* it is what makes the stage-1 migration gate in
`REFACTOR/README.md` affordable to run.

---

## 4. Corpus tooling constants: subtraction, not relocation

The obvious fix — move `HUSK_BIN` / `CORPUS_ROOT` / `LISTFILE` into the shared
framework, sourced from husk's config file — is the wrong one. It would preserve
all three underlying problems in a tidier box, and couple verification tooling to
the configuration of the thing it is meant to independently verify.

**Done** for every real `ScanTask` module (`REFACTOR_LOG.md`'s 2026-08-28
entry): `corpus_scan_framework.py` now exposes `ROOT`/`LISTFILE`/`HUSK_BIN`
as single, dynamically-read values (`corpus_scan_framework.ROOT`, etc.) set
once per run by `_init_worker`, not per-module hardcoded constants.
`black_additive_task.py`, `casc_size_mismatch_task.py`,
`unfillable_texture_task.py`, `texture_dedup_collision_task.py`,
`m2_full_validation_task.py`, and `particle_only_task.py` all had their own
copies deleted; `corpus_checks.py`'s own `HUSK_BIN` default was fixed at
its one real source instead of overridden per-caller.
`render_sample_driver.py` was deliberately left alone — a driver script
with its own argv, not a `ScanTask`, and part of the render pipeline this
project already treats as human-gated (`CLAUDE.md` Hazards); it still has
its own `CORPUS_ROOT`/`HUSK_BIN`/`LISTFILE` copies, a real follow-up if
that pipeline is ever brought into this same mechanism.

Each constant was a distinct bug with its own fix:

### `CORPUS_ROOT` — a value the framework already receives

`corpus_scan_framework.py:572` takes `--root` as a real argument. Yet
`CORPUS_ROOT` is re-declared in six task modules
(`black_additive_task.py:47`, `casc_size_mismatch_task.py:53`,
`unfillable_texture_task.py:59`, `texture_dedup_collision_task.py:56`,
`render_sample_driver.py:54`, `m2_full_validation_task.py:29`) — seven copies
counting the framework's own hardcoded default.

A task can therefore silently disagree with the root it is actually being run
against. `m2_full_validation_task.py:34-37` reconciles the copies by reaching
into *another module's* globals (`cc.CORPUS_ROOT = ...`, `cc.HUSK_BIN = ...`,
`cc.TIMEOUT = ...`) — a workaround for a problem that only exists because the
value was duplicated in the first place.

**Fix, as shipped**: a task never declares a root. It reads
`corpus_scan_framework.ROOT`, set once by `_init_worker` from the `--root`
the run was actually launched with.

### `HUSK_BIN` — the dev shell doesn't provide this; installing the flake package does

Hardcoded to `/home/luna/dev/husk/build/husk` in seven modules. This
document previously claimed the flake dev shell already puts `husk` on
`PATH` and that the fix was simply to rely on that — **verified false
live** while implementing this: `.direnv/bin` carries no `husk` symlink, so
a bare `"husk"` subprocess call fails outright (caught by a real smoke test
run, not assumed). Luna's own correction: this isn't a dev-shell bug —
installing the flake as a package (`nix profile install`/`nix run`) *does*
put `husk` on `PATH`, that's just not the environment this corpus tooling
actually runs in. A dev shell hands you the tools to build the project,
not the project's own not-yet-(re)built output, by design.

**Fix, as shipped**: `corpus_scan_framework.HUSK_BIN = shutil.which("husk")
or str(REPO_ROOT / "build" / "husk")` — PATH first (correct when husk is
installed as a flake package), falling back to the known-good local build
path (correct in the dev shell this tooling runs under today). Same
`shutil.which`-first idiom `corpus_checks.py`
already used for `GLTF_VALIDATOR_BIN`. One computed value, read by every
consumer instead of each hardcoding its own copy.

### `LISTFILE` — consolidated, not yet eliminated

Declared in five modules. The catalog's eventual answer (a task that needs
*resolved* data asks husk instead of knowing a listfile path at all,
`RESOURCE_CATALOG.md`) is still not built, so this doesn't yet retire the
constant for these tasks — they still shell out to `husk info`/`blp-export`
and need a real listfile path to pass through. What's fixed now is the
duplication itself: one `--listfile` CLI flag on the framework, exposed as
`corpus_scan_framework.LISTFILE`, instead of five separately hardcoded
paths that could drift from each other and from what the framework's own
`--root` was pointed at.

**Real bug found and fixed while wiring this in**: every task module's own
docstring documents running `corpus_scan_framework.py` directly as a
script (`python tools/corpus_scan_framework.py --task ...`), which loads it
as `__main__` — a *different* module object from the `corpus_scan_framework`
a task gets via its own `import corpus_scan_framework as csf`, with
independent globals. `_init_worker` was setting `ROOT`/`LISTFILE` on
whichever identity happened to run, while every task read them off the
other, untouched, still-`None` one. Caught by a real smoke-test run against
40 live corpus files (`AttributeError: 'NoneType' object has no attribute
'exists'`), fixed with `sys.modules.setdefault("corpus_scan_framework",
sys.modules[__name__])` at module load, so both names always resolve to one
shared object. Re-verified clean afterward against 5 real tasks
(`unfillable_texture_task`, `black_additive_task`, `casc_size_mismatch_task`,
`texture_dedup_collision_task`, `m2_full_validation_task`), each run
against real local corpus files with `--limit`, zero errors.

### Also

Both done — see `REFACTOR_LOG.md`'s entry closing `AUDIT.md` §11:
`missing_texture_task.py` deleted (was superseded by
`unfillable_texture_task.py` by its own module doc); `shader_id_task.py`'s
stale "husk parses no shader_id" claim corrected (husk does parse it,
`skin.hpp`/`m2_shader_names.hpp`, but nothing exposes it structurally yet —
converting the task itself stays blocked on `CLI_AND_TOOLING.md` §3's
structured-output work below, not started).

---

## 5. `--knowledge-db`

**Decided and done** (`REFACTOR_LOG.md`'s 2026-08-28 entry): keep, not
retire — `TODO/KNOWLEDGE_BASE_DESIGN.md` already made this call explicitly
("kept as diagnostic/future-work infrastructure, not load-bearing";
disabled by default in `render_sample_driver.py`), so retiring the flag
here would have reopened a decision already made rather than executing
it. What was actually missing was the I4 half: `cmd_export.cpp` now
prints a real warning at the exact point a `--knowledge-db` answer is
about to be used (naming the resolved FileDataID and pointing at
`TODO/KNOWLEDGE_BASE_DESIGN.md`), instead of the known-wrongness living
only in a hazards note a caller of this flag might never read. First
CLI-tier coverage this flag has ever had
(`tests/test_cli_knowledge_db.cpp`: a real resolution prints the warning,
a miss prints nothing).

Still true, not attempted here: this flag mutates the shared listfile map
mid-export (`cmd_export.cpp`, `if (objectSkinTextureFileDataId != 0 &&
!kbResolution.texturePath.empty()) listfile.emplace(...)`) so another
subsystem picks up its answer — a real instance of the FileDataID→path
duplication `AUDIT.md`'s now-closed §1.2 named (`REFACTOR_LOG.md`), left
for the catalog (`RESOURCE_CATALOG.md`) rather than patched around here.
