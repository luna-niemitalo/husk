# RESOURCE_CATALOG.md — stage 2, the one resolution boundary

**Target, not current state.** See `REFACTOR/README.md` for the pipeline and the
invariants referenced by number below.

## The question this stage owns

> Given something husk knows it needs — a FileDataID, a sidecar kind, a DB2
> table — *where do the real bytes live, and which one wins when several
> candidates match?*

Today that question is answered in at least nine places across three languages,
with at least four different tier orders (`AUDIT.md` §1). Stage 2 exists so it
is answered once.

Everything above this stage (canon, writers) receives resolved bytes and never
asks where they came from. Everything below it (format parsers) receives bytes
and never goes looking for them.

## Surface

`husk::sources::Catalog`, constructed once per process — or once per
`--from-list` batch, never per model — from the inputs the CLI already collects:
model path, `--textures`, `--listfile` + `--listfile-root`, `--db2-dir`,
`--dbd-dir`, `--skin-dir`, `--anim`, `--bones-dir`.

```
textureBytes(fdid, textureType, modelContext) -> Resolved<bytes>
modelPath(fdid)                               -> Resolved<path>
fileDataIdForPath(path)                       -> Resolved<fdid>
sidecar(kind, fdid | sameBasenameConvention)  -> Resolved<bytes>
db2(tableName)                                -> const Table&      (cached)
describe()                                    -> the ledger, see I4
```

`Resolved<T>` is not a bare `optional`. It carries **how** the answer was
reached — which tier fired, which directory, which fallback, and for a name,
which source (I6). That is not diagnostics bolted on; it is what makes I4 true
and what makes the stage-1 migration gate affordable to run.

The type itself (`husk::sources::Resolved<T>`, `ResolutionTier`, `tierName`)
now exists — `src/sources/resolved.hpp`, landed as pure scaffolding ahead of
the `Catalog` object per `REFACTOR_LOG.md`'s 2026-08-28 "Resolved<T>
scaffolding" entry, the same "boring infrastructure first" pattern
`db2_cache.hpp` used.

Tiers 1 (literal `<texturesDir>/<FileDataID>.{png,blp}`) and 2 (listfile) now
report through it — `src/sources/texture_catalog.hpp`'s
`resolveLiteralTextureBytes`/`resolveListfileTextureBytes`,
`export_materials.cpp`'s real texture-tier call sites (both the primary
baseColorTexture resolution *and* the `additionalTextureLayers` loop's own
previously-separate copy, found while doing this) now call them instead of
duplicating the lookup inline (`REFACTOR_LOG.md`'s 2026-08-28 "first"/
"second real tier through Resolved<T>" entries, verbatim behavior, verified
against the full suite). Tier 3 (fuzzy same-basename pool) is not migrated
yet — it sits inside `export_materials.cpp`'s ambiguous-candidate/alternate-
texture-candidate logic, which needs more care than a mechanical wrapper
(see that log entry's "why this tier, not 2 or 3 next" for the reasoning,
now half-resolved). The Python/Blender mirrors are entirely untouched by
this — they can't call into `src/sources/` at all; that gap is what
`CLI_AND_TOOLING.md` §3's structured-output work is for.

A miss is a first-class answer with a reason, not an empty optional the caller
has to guess about. `FOREIGN_DATA.md` §2 already requires expected-vs-actual on
failure; today that requirement is met unevenly, per call site.

## Normative tier order

This document is the source of truth for the texture tiers.
`export_texture_resolution.cpp` becomes an *implementation* of what is written
here, rather than the place the rules are discovered by reading code.

1. **Literal** — `<texturesDir>/<FileDataID>.png`, then `.blp`. PNG wins when
   both exist (no redundant decode).
2. **Listfile** — `<listfileRoot>/<real content path>` for that FileDataID.
   Deterministic: real data, not a guess, so it outranks tier 3.
3. **Fuzzy same-basename pool** — files in `texturesDir` whose stem starts with
   the model's basename, filtered by texture type, claim-and-remove so one image
   cannot fill every unresolved slot on a model.

Two things the current implementations disagree about, to be settled explicitly
rather than inherited:

- **Parent-directory search.** Only the Blender script does it
  (`husk_blender_geoset_mask.py:1575-1596`), because real per-race customization
  assets live one level above the per-sex model folder. This is a real corpus
  fact, so it belongs in the catalog — as a named tier, not as one consumer's
  private extension.
- **Ordering among ambiguous candidates.** `orderCandidatesForDefault` uses four
  measured signals; the Python mirrors use none and the Blender script uses
  `sorted()[0]`. One of these is right and the others are silently different.

## What this stage deletes

- Four texture-resolution implementations → one (`AUDIT.md` §1.1).
- Four FileDataID→path implementations → one (§1.2).
- The `chrrace::load` ×3 / `texturefiledata::load` ×2 / same-file-parsed-twice
  DB2 pattern → one cache. **Done** (`src/sources/db2_cache.hpp`, see
  `REFACTOR_LOG.md`'s 2026-08-28 entries) — this was the free half of stage
  2, no semantic change, just the table being read once; it landed ahead of
  the `Catalog` object itself since it needed no resolution-policy design.
- `resolveObjectSkinTextureFromKb`'s listfile-map injection
  (`cmd_export.cpp:973-975`) — a feature reaching sideways into another
  feature's data structure because there was no shared place to put an answer.

## The excavation escape hatch

Luna's own scoping, recorded because it is easy to over-apply I2:

> A corpus scan that doesn't need to do data excavation can just use the
> resource catalog. But if it is intended to find, say, unknown fields, it is
> allowed to read the raw dataset and parse it how it pleases.

**The test**: is the task consuming husk's understanding of the data, or
interrogating the data *behind* husk's understanding?

- Consuming → catalog (via structured husk output; see `CLI_AND_TOOLING.md` §3).
  A mirrored reimplementation here is a bug waiting to happen, and has already
  been one.
- Interrogating → raw bytes, deliberately. A second implementation *is the
  point* when the job is finding what husk gets wrong, doesn't parse, or has
  never seen.

Applied to all 18 modules under `tools/corpus_scan_tasks/` (15 scan tasks plus
3 render drivers):

| Task | Verdict |
|---|---|
| `unfillable_texture_task.py` | **Catalog.** The headline conversion — this is the tier mirror that already broke once. |
| `black_additive_task.py` | **Catalog.** Needs resolved texture bytes; currently shells out to `blp-export` itself. |
| `texture_dedup_collision_task.py` | **Catalog.** Needs resolved bytes to compare them. |
| `texture_type_collisions_task.py` | **Catalog.** Consumes resolved slots. |
| `m2_full_validation_task.py` | **Catalog.** Drives a real export already. |
| `animated_texture_effects_task.py` | **Structured output.** Consumes husk's own track resolution. |
| `expansion_task.py` | **Structured output.** Version/expansion off the header. |
| `particle_only_task.py` | **Structured output.** Currently regex-scrapes `husk info`. |
| `detect_billboards.py` | **Structured output.** Same. |
| `example_texture_count.py` | **Structured output.** It is the copy-paste template, so it must model the right pattern. |
| `casc_size_mismatch_task.py` | **Mixed.** Catalog for FileDataID↔path; the size comparison against CASC's own report is genuinely external, keep it raw. |
| `dangling_references_task.py` | **Mixed.** Excavation by purpose (finding broken internal references), but its `_find_same_basename_skins` duplicates sidecar resolution — that half moves to the catalog. |
| `shader_id_task.py` | **Re-checked, still raw for now.** Its stale docstring (said husk parses no `shader_id`) is fixed. Reclassifying to structured output turned out premature: husk parses `shaderId`/resolves its name internally (`skin.hpp`/`m2_shader_names.hpp`) but neither `husk info` nor `dump-chunks` exposes either anywhere yet — no structured output exists to consume. Docstring now states this explicitly (the excavation-escape-hatch rule this section itself sets), so the raw read is a considered exception, not an unconverted leftover; revisit once `CLI_AND_TOOLING.md` §3's `husk info --json`/`husk resolve` lands. |
| `shader_names_task.py` | Same underlying fact (`m2_shader_names.hpp` resolves these internally, nothing structured to consume yet) — but its own docstring already said so before this pass; no fix needed. |
| `missing_texture_task.py` | **Done.** Deleted — was superseded by `unfillable_texture_task.py` by its own admission; every reference to it elsewhere (`CORPUS_SCANS.md`, `TOOLS.md`, sibling task comments) repointed or historicized. |
| `build_render_sample.py` / `render_sample_driver.py` | Drivers, not scan tasks — their `CORPUS_ROOT`/`HUSK_BIN`/`LISTFILE` copies are `CLI_AND_TOOLING.md` §4's problem, not this file's. |
| `render_glb.py` | Neither — a headless Blender render script. Becomes an addon-driven preview, see `BLENDER_ADDON.md`. |

The rule to write into `tools/CORPUS_SCANS.md` alongside this: a task that reads
raw bytes must say in its own docstring **which** husk understanding it is
deliberately going behind, so the next reader can tell a considered exception
from an unconverted leftover.

## Open questions

- Does the catalog own *reading* (returning bytes) or only *locating* (returning
  paths)? Returning bytes lets it cache decoded BLP→PNG once — which is exactly
  what the Blender script currently reimplements as a temp-dir cache keyed by
  FileDataID. Leaning: bytes, with the decode cache inside.
- `--listfile-root` defaults to `--textures` today. Under the catalog that
  coupling can be stated once instead of re-derived at
  `cmd_export.cpp:881`; whether it should survive at all is a separate call.
