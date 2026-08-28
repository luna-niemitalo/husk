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

Each constant is a distinct bug with its own fix:

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

**Fix**: a task never declares a root. It receives the one it is being run with.
Delete all seven declarations, the framework's default included — an unspecified
root should be an error, not a silent guess at one machine's layout.

### `HUSK_BIN` — a binary the environment already provides

Hardcoded to `/home/luna/dev/husk/build/husk` in seven modules, while the flake
dev shell already puts `husk` on `PATH`. This silently pins every scan to
whatever stale build happens to sit in `build/`, and it is the same disease as
the Blender script's `_find_husk_binary`.

**Fix**: run inside the env (`direnv exec .`, as `CORPUS_SCANS.md` already
documents) and use the tool the env provides.

### `LISTFILE` — a resolution input that shouldn't be visible

Declared in five modules. Under the catalog, a task that needs *resolved* data
has no business knowing a listfile path at all — it asks husk
(`RESOURCE_CATALOG.md`).

**Fix**: the constant ceases to exist for catalog-using tasks. It survives only
where an excavation task genuinely treats the listfile itself as its subject
matter.

### Also

- `missing_texture_task.py` is superseded by `unfillable_texture_task.py` by its
  own module doc, yet remains runnable — delete it, rather than continuing to
  warn about it in `CLAUDE.md`'s Hazards.
- `shader_id_task.py` / `shader_names_task.py` justify their own raw parsers
  with a claim that is no longer true (husk parses `Batch::shaderId` at
  `skin.cpp:195`). Re-check, then convert.

---

## 5. `--knowledge-db`

Documented as known-wrong and unusable for real output (`CLAUDE.md` Hazards,
`TODO/KNOWLEDGE_BASE_DESIGN.md`: same-slot cross-item collisions, 15/15 spot
checks wrong-item), yet still a live flag — and one that mutates the shared
listfile map mid-export (`cmd_export.cpp:973-975`) so another subsystem picks up
its answer.

Decide explicitly rather than leaving it: retire it, or keep it behind the
catalog with its known-wrongness surfaced at point of use (I4) instead of only
in a hazards note a caller may never read.
