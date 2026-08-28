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
texture(fdid, textureType, modelContext)      -> Resolved<EncodedTexture>
modelPath(fdid)                               -> Resolved<path>
fileDataIdForPath(path)                       -> Resolved<fdid>
sidecar(kind, fdid | sameBasenameConvention)  -> Resolved<bytes>
db2(tableName)                                -> const Table&      (cached)
describe()                                    -> the ledger, see I4
```

`EncodedTexture` is `{bytes, encoding}`, never bare pixels — I8 and
`BUNDLE_FORMAT.md`'s "Texture encoding": the catalog hands back the payload it
was given, tagged, and a caller that wants PNG asks for the transcode explicitly.
Decoding on the way *in* would spend the source payload irreversibly to save a
call, which is the one thing that decision forbids. The tag describes the payload
only; choosing its open container (DDS) is a *writer* concern, so the catalog
never needs to know about one.

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
against the full suite). Tier 3 (fuzzy same-basename pool) is **partially**
migrated — only its deterministic "read what was already claimed" half
(`resolveClaimedFuzzyPoolTextureBytes`); the claim-and-remove step
(`claimSoleFuzzyTextureCandidate`) and the genuine-ambiguity fan-out
(`filterCandidatesForType`/`orderCandidatesForDefault`,
`AlternateTextureCandidate`) deliberately stay outside `Resolved<T>` for now
— see the Settled section below ("Tier 3's shape") for why folding all three
of tier 3's real outcomes into one hit/miss shape isn't a safe mechanical move
the way tiers 1/2 were, and for what the finished shape is instead. The Python/Blender mirrors are entirely untouched by this — they
can't call into `src/sources/` at all; that gap is what `CLI_AND_TOOLING.md`
§3's structured-output work is for.

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

- Three texture-resolution implementations → one (`AUDIT.md` §1.1, in
  progress — tiers 1/2 of the real C++ implementation done, see below).
- Five FileDataID→path/name implementations → one. **Done**
  (`husk::sources::pathForFileDataId`/`contentNameForFileDataId`,
  `src/sources/listfile_catalog.hpp`/`.cpp` — see `REFACTOR_LOG.md`'s
  2026-08-28 entries; the two-more-than-originally-counted knowledge-base
  and Python-mirror cases stay deliberately separate, different backing
  store/language, not the same duplication).
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

## Settled

Three questions this document carried as open are answered below. Each was
answerable from `README.md`'s own invariants rather than from taste, which is
why they were settled rather than escalated. A fourth — what **encoding** the
resolved bytes are in — was escalated instead, because it constrains this file's
surface and `BUNDLE_FORMAT.md`'s payload convention at the same time; Luna
settled it the same day, and the reasoning lives in that file's "Texture
encoding" section. The consequence here is that the texture surface returns
`EncodedTexture`, not pixels (see Surface, above).

### The catalog owns *reading*, not only *locating*

`texture(...)` returns bytes rather than a path, and the transcode cache lives
inside the catalog — which is exactly what deletes the Blender script's private
temp-dir BLP→PNG cache keyed by FileDataID. It is a *transcode* cache, not a
decode-on-ingest step: the source encoding is what the catalog holds, and pixels
are produced only when a caller asks for them (I8).

The resolved path travels as **provenance on `Resolved<T>`**, not as a second
`texturePath()` surface. A parallel locating API would immediately need its own
tier order to answer "which path", and two tier orders for one question is the
original I2 violation reappearing under a new name. A caller that needs the path
(`--slim-textures` writing a file, a diagnostic naming what it read) reads it off
the same answer that produced the bytes.

`modelPath(fdid)` stays a separate surface, and is not an exception to that: a
model path is handed onward to another pipeline as a *baked relative reference*
(`GearItem::auxGlbPath`, I3's precedent), never opened by the catalog itself.

### `--listfile-root` keeps defaulting to `--textures`

Kept, and stated once. It is a real convenience for the actual workflow (one
extracted corpus tree serving as both), and dropping it would break every
existing config file and invocation for no gain. What is wrong today is that the
coupling is re-derived in two places (`cmd_export.cpp:918`, and the
`listfileRoot.empty() ? texturesDir : listfileRoot` fallback at
`cmd_export.cpp:355`), so a reader cannot tell which is authoritative. The
catalog constructor resolves it once, and `describe()` names the effective root
(I4) — a read operation, not something inferred from behavior.

### Tier 3's shape: the three-way branch is catalog-internal, not a `Resolved<T>` question

Neither of the two resolutions previously proposed, because both accept a premise
that stops being true once the `Catalog` object exists: that the *caller* has to
see three outcomes at all.

Under the target surface, `texture(fdid, textureType, modelContext)` runs
every tier internally — including the claim-and-remove step, which mutates
catalog-owned pool state and was therefore never legitimately
`export_materials.cpp`'s business. The caller sees one `Resolved<T>`. The
distinction that made a naive collapse unsafe ("zero candidates, go scan the
pool" vs. "claimed, then failed to decode — do not re-scan the now-depleted
pool") is *sequencing inside one function*; it only looks like a type problem
because the two steps are currently split across an API boundary.

What `Resolved<T>` gains is **not** a `variant`. It gains an alternates field:

```
template <typename T> struct Resolved {
    std::optional<T>       value;       // the default pick, already chosen
    ResolutionTier         tier;
    std::string            reason;
    std::vector<Alternate> alternates;  // non-empty == genuinely ambiguous
};
```

Ambiguity *is* provenance, which is what I4 already says `Resolved<T>` carries —
"2+ type-compatible candidates matched, `orderCandidatesForDefault` picked this
one" is the same kind of statement as "tier 2 fired from this root". The
ambiguous branch already produces a single chosen default
(`alternateTextureCandidates.front()`, `export_materials.cpp`), so it is not a
disjoint success shape: it is a hit that knows it was a coin toss.
`std::variant<T, AmbiguousCandidates>` would instead force every caller of every
catalog surface to handle a case exactly one tier can ever produce — a generic
container earned by one occurrence.

The narrow wrap already landed (`resolveClaimedFuzzyPoolTextureBytes`, read step
only, caller keeps its explicit branch) is the correct interim: it is the subset
of this answer that is true *today*, before the `Catalog` object exists to own
the claim step. It does not need revisiting when the rest lands — it gets
absorbed.

## Where the two unordered tiers sit

`ResolutionTier` (`src/sources/resolved.hpp`) already names five tiers while the
normative order above lists three. The two extras are ordered here so the
migration is not the place that discovers it has to decide:

4. **Parent-directory same-basename** — after tier 3, because it searches a
   strictly broader pool than the model's own directory, and a more specific
   match must never lose to a less specific one. A real corpus fact today living
   as one consumer's private extension (`husk_blender_geoset_mask.py`).
5. **Knowledge base** — last, and only when `--knowledge-db` is given. It is
   documented known-wrong (`TODO/KNOWLEDGE_BASE_DESIGN.md`), so nothing able to
   answer from real data should ever lose to it.

Both orderings change real resolution outcomes for real files — which is exactly
what stage 1's resolution-ledger gate exists to catch: every delta attributed,
never assumed away.
