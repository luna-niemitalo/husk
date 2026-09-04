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

The real `husk::sources::Catalog` object now exists — `src/sources/
catalog.hpp`/`.cpp`, `REFACTOR_LOG.md`'s newest entry. `texture(fdid,
textureType, modelContext, preferGlowVariant)` owns tiers 1 (literal), 2
(listfile), and 3 (fuzzy same-basename pool, **including** the
claim-and-remove step and the genuine-ambiguity fan-out — the three-way
branch `export_materials.cpp` used to see is gone; the caller now gets one
`Resolved<EncodedTexture>` per slot, ambiguity riding `alternates` per the
Settled section below) in the documented order. Memoized per (modelPath,
textureSlotIndex) inside the catalog, replacing the file-local
`fuzzyResolutionByTextureIndex` cache `export_materials.cpp` used to keep
for the identical reason (two batches referencing one M2 texture-array entry
must agree on one answer). `registerPathOverride(fdid, path)` replaces the
`--knowledge-db` object-skin tier's sideways `listfile.emplace(...)` mutation
with a catalog-owned override, never touching the caller's own `--listfile`
map (which the caller may share with other consumers). `preferGlowVariant` is a fourth parameter
beyond the three named in this document's own pseudocode above — a real
per-batch signal (`M2Material::blendMode > 2`) `orderCandidatesForDefault`'s
ambiguity ranking needs and no other part of `modelContext` legitimately
carries; flagged here rather than silently added.

Deliberately **not** owned by `texture()`: tier 4 (parent-directory
same-basename — still Blender-script-only, a marked gap in `texture()`'s own
doc comment, not ported this pass) and tier 5 (knowledge base — stays a
pre-step the caller runs once per model and feeds back through the normal
`fdid` parameter, since "which fdid should stand in for this slot" is a
different question than "given this fdid, find bytes"; see `catalog.hpp`'s
own doc comment for the full reasoning). `modelPath(fdid)`/
`fileDataIdForPath(path)`/`sidecar(...)`/`db2(...)` are still the FileDataID<->
path surface (`AUDIT.md` §1.2, closed separately) and future sidecar/DB2
work — not part of this pass, which was scoped to the texture half only.

Verified via a resolution-ledger diff on real fixtures rather than by reading
`describe()`'s own output: `bloodelffemale_hd`, `nightelffemale_hd` (a real
218-candidate ambiguous pool), a creature (`wolf.m2`, incl. `--lod all`'s
2-tier case), an item (`sword_1h_artifactskywall_d_06.m2`, 13 real fuzzy/
ambiguous matches), and a `--knowledge-db`-driven item exercising
`registerPathOverride` end to end — every export byte-identical before vs.
after this object existed. Zero delta, honestly: the four tiers already
agreed with each other before this pass (unlike the Python/Blender mirrors,
`AUDIT.md` §1.1's real remaining drift), so consolidating them into one
object didn't change any resolution outcome, only where the tier order lives.
The Python/Blender mirrors are entirely untouched by this — they can't call
into `src/sources/` at all; that gap is what `CLI_AND_TOOLING.md` §3's
structured-output work is for.

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
  progress — the real C++ implementation is now one object
  (`sources::Catalog`, all three tiers), the Python/Blender mirrors are
  still separate, see below).
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
- `resolveObjectSkinTextureFromKb`'s listfile-map injection. **Done** —
  replaced by `Catalog::registerPathOverride`, see above; verified
  end to end against a real `--knowledge-db` hit
  (`item/objectcomponents/shoulder/lshoulder_robe_d_01.m2`), byte-identical
  output before and after the mechanism swap.

## Open question: persisted cross-patch reference-integrity index

Design-review pass, 2026-09-03. Not covered by anything above:
`dangling_references_task.py` (excavation table below) finds broken
references, but as a one-shot corpus scan with no memory across patches — it
can tell you a reference doesn't resolve *today*, not that it *used to* and
now doesn't. `--knowledge-db`/`knowledge.sqlite` (`CLI_AND_TOOLING.md` §5) is
persisted but is a cache of resolution *answers*, not an edge/provenance log,
and isn't versioned for cross-patch diffing either.

**The claim**: a separate, husk-maintained, patch-versioned join table
recording observed references — `(from_kind, from_id, to_kind, to_id,
first_seen_patch, last_seen_patch)` — indexed on both `from` and `to`, so
"what does X point at" and "who points at X" are equally cheap. Same shape as
a bookmarks table: neither endpoint owns the edge, a third table does.

**Why not node-embedded backpointers (considered and rejected)**: storing
"referenced by" at the target node couples the edge fact's survival to that
node's own survival — if the target is what gets dropped or corrupted in a
later patch, its backpointer is lost with it, the same single point of
failure moved one hop. A third, independent, append-only store survives the
loss of *either* endpoint, which is the actual property wanted: detecting
that a middle node in a multi-hop resolution chain (e.g. the real
`ItemModifiedAppearance → ItemAppearance → ItemDisplayInfo → …` chain,
`CLAUDE.md`'s Resume history) has silently disappeared between patches, even
when neither surviving fragment alone would show the hole.

**Why this doesn't fold into `dangling_references_task.py` as-is**: that
task's output is ephemeral (`corpus_reports/`, overwritten per run). This
needs to persist and diff against a prior snapshot to detect *drift*, not
just validate *current* consistency — closer in shape to `knowledge.sqlite`'s
persisted-store precedent than to a scan task's report.

**Left to investigate, not solved by the above**:

- Storage backend — a new store, or an extension of `knowledge.sqlite`'s
  existing pattern.
- What populates it: every `husk resolve`/export run opportunistically, or a
  dedicated corpus-wide pass (closer to the excavation tasks below)?
- Retention/growth policy — this is append-only across every patch this
  project ever scans; unbounded growth needs a stated bound or pruning rule.
- Relationship to I2 ("one implementation per resolution question") — this is
  explicitly a derived *observation log* of catalog resolutions, not a second
  resolver. Worth stating that distinction as sharply in the eventual
  write-up as it's stated here, so it isn't flagged as an I2 violation later.

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
| `unfillable_texture_task.py` | **Done, 2026-08-29.** Consumes `husk resolve` (`corpus_scan_framework.husk_resolve_json`) — the tier mirror that already broke once is deleted outright, not patched. |
| `black_additive_task.py` | **Done, 2026-08-29** (its remaining half). Its `husk info` prose regexes were already converted to `husk info --json` (earlier pass, unchanged by this one). Its texture-*resolution* half now also consumes `husk resolve --textures-out`, which deletes the `husk blp-export` shell-out too, not just the tier mirror — `--textures-out` already hands back a real decoded PNG regardless of source format, so there is no `.blp` left for this task to convert itself. |
| `texture_dedup_collision_task.py` | **Done, 2026-08-29.** Candidate FileDataIDs and resolved bytes both come from `husk resolve --textures-out` now. Its per-batch `textureCount > 1` gate and same-basename `.skin` sidecar lookup stay raw, deliberately (see its own docstring) — neither is texture *resolution*, and the ledger has no per-batch field for the former. **Real, deliberate narrowing found along the way**: the pre-conversion version sourced candidate FileDataIDs from every M2Texture record `husk info` showed a FileDataID for, regardless of whether any batch actually references it; `husk resolve`'s ledger only reports slots a real batch uses — *more* correct for this task's own motivating bug (an unreferenced texture can't be embedded, so it can't hit the Blender-Image-merge collision this task exists to quantify). See `REFACTOR_LOG.md`'s delta table for the real corpus numbers this changed. |
| `texture_type_collisions_task.py` | **Re-checked, does not fit this verdict.** Read closely rather than assumed: `find_texture_type_collisions.py` (this task's own backing implementation) does no texture-*file* resolution at all — it compares two purely M2/`.skin`-internal facts (the raw `textureLookup` reverse-pick array vs. a batch's own `textureCombos`-resolved texture index), neither of which `husk resolve`'s ledger carries. Its own docstring already frames it as a deliberate, husk-independent "second opinion" on husk's own M2 parsing — the same considered-exception shape `shader_id_task.py`/`shader_names_task.py` already have below. Left unconverted; not a texture-resolution duplication to begin with, so this was a wrong verdict, not an unclosed one. |
| `m2_full_validation_task.py` | **Catalog.** Drives a real export already. |
| `animated_texture_effects_task.py` | **Re-checked, still raw for now.** This verdict presupposed a structured field that doesn't exist: `husk info --json`'s schema has no `colors`/`textureWeights` arrays at all, and `texture_transforms` is count/offset only, no per-record animated-vs-constant determination — husk resolves this internally (`resolveAnimatedColorCurve`/`resolveAnimatedFixed16Curve`, `src/export_materials.cpp`) but exposes none of it. Same considered-exception shape as `shader_id_task.py` below; its own docstring now says so explicitly. Not converted — blocked on new C++ work, out of scope for a tools/-only pass. |
| `expansion_task.py` | **Done.** Converted to `husk info --json`'s `format`/`version`/`expansion`/`record_stride_version_verified` fields — no longer a hand-transcribed second copy of `expansionForVersion`'s table. |
| `particle_only_task.py` | **Done.** Converted to `husk info --json`'s `vertices.count`/`materials[].blend_mode`/`particle_emitters.count`. |
| `detect_billboards.py` | **Done.** Converted to `husk info --json`'s `bones.count`/`bones.billboard_bones[]`; also picked up `corpus_scan_framework.HUSK_BIN` for free (was hardcoding an absolute build path). |
| `example_texture_count.py` | **Done.** Converted to `husk info --json`'s `textures.count` — it is the copy-paste template, so it now models the structured-output pattern new tasks should start from. |
| `casc_size_mismatch_task.py` | **Mixed.** Catalog for FileDataID↔path; the size comparison against CASC's own report is genuinely external, keep it raw. |
| `dangling_references_task.py` | **Mixed.** Excavation by purpose (finding broken internal references), but its `_find_same_basename_skins` duplicates sidecar resolution — that half moves to the catalog. |
| `shader_id_task.py` | **Re-checked, still raw for now.** Its stale docstring (said husk parses no `shader_id`) is fixed. Reclassifying to structured output turned out premature: husk parses `shaderId`/resolves its name internally (`skin.hpp`/`m2_shader_names.hpp`) but neither `husk info` nor `dump-chunks` exposes either anywhere yet — no structured output exists to consume. Docstring now states this explicitly (the excavation-escape-hatch rule this section itself sets), so the raw read is a considered exception, not an unconverted leftover; revisit once `CLI_AND_TOOLING.md` §3's `husk info --json`/`husk resolve` lands. |
| `shader_names_task.py` | Same underlying fact (`m2_shader_names.hpp` resolves these internally, nothing structured to consume yet) — but its own docstring already said so before this pass; no fix needed. |
| `missing_texture_task.py` | **Done.** Deleted — was superseded by `unfillable_texture_task.py` by its own admission; every reference to it elsewhere (`CORPUS_SCANS.md`, `TOOLS.md`, sibling task comments) repointed or historicized. |
| `build_render_sample.py` / `render_sample_driver.py` | Drivers, not scan tasks — their `CORPUS_ROOT`/`HUSK_BIN`/`LISTFILE` copies are `CLI_AND_TOOLING.md` §4's problem, not this file's. |
| `render_glb.py` | Neither — a headless Blender render script. Becomes an addon-driven preview, see `BLENDER_ADDON.md`. |

**The "Catalog" verdicts' own prerequisite is now built**, and three of the
four tasks it named are now actually converted onto it (the fourth,
`texture_type_collisions_task.py`, turned out on closer reading not to be
a texture-resolution duplication at all — see its corrected row above;
`m2_full_validation_task.py` stays open, out of scope for a tools/-only
pass and carrying its own open hang bug, `TODO/CLEANUP_TODO.md`).
`CLI_AND_TOOLING.md` §3's `husk resolve` verb (`src/cmd_resolve.cpp`,
`REFACTOR_LOG.md`'s 2026-08-29 "`husk resolve`, a new verb" entry) is the
mechanism: one JSON document per model, one entry per texture slot,
naming the tier/fdid/resolved name/alternate count/miss reason
`sources::Catalog::texture()` already computed, plus `--textures-out` for
the two tasks that needed real resolved *bytes*, not just metadata
(`texture_dedup_collision_task.py`, `black_additive_task.py`) — built
specifically so `unfillable_texture_task.py`/`texture_dedup_collision_
task.py` (which deliberately avoid a real `husk export` per file today, at
132k-file corpus scale — see that task's own doc comment) don't have to
start paying that cost just to stop re-deriving resolution by hand.

**A real, measured cost this conversion pass surfaced, not papered over**
(re-measured twice more after the first single-shot estimate was
challenged, once with a flawed comparison that wrongly concluded
`--listfile` was cheap — see TODO/CLEANUP_TODO.md item 3's own note on
`~/.config/husk/config.toml` silently autodiscovering a listfile even
when `--listfile` isn't passed, which is exactly what made that
comparison compare the same listfile against itself; the original
finding held once isolated properly). `husk resolve` carries two
separate, additive costs against the `husk info`-based tasks it replaces
(~2-44ms/file, model-size-dependent). `--listfile` re-parses the entire
~148MB/2.2M-row community-listfile.csv from scratch on every single
invocation (no process-lifetime cache the way the deleted Python-side
`functools.lru_cache`d mirrors had) — isolated by holding everything else
constant and varying only listfile size, a 1-row listfile costs
~0.2s/invocation, the real 2.2M-row one costs ~0.7s: **the parse itself
is ~0.5s, ~70% of a real invocation's cost.** Consistent with the
per-model numbers: a small item model, 76ms → 776ms; a large character
model, 2990ms → 3893ms — same ~700-900ms fixed delta, wildly different
base. Resolution *itself*, independent of `--listfile`, is the second
cost, and it scales with the model's own same-basename fuzzy-pool size —
the bloodelffemale_hd measurement above already costs ~3s with
`--listfile` omitted entirely, dwarfing the listfile cost for that one
file. Which cost dominates depends on the file: `--listfile` dominates
for the bulk of the corpus (small/no fuzzy pool, the common case);
fuzzy-pool resolution dominates for a minority of customization-heavy
character models. Either way, at full-corpus scale that is tens of
thousands of seconds per scan, which the tasks' own stated design
constraint ("a ~10-minute scan, not a multi-hour one") cannot absorb
as-is. The real fix is a `src/`-level one: husk should ingest the
listfile once into a cached, fast-to-load format instead of re-parsing
raw CSV every invocation — the same "ingest the slow source, cache it"
move `husk db2-build`'s own knowledge base already makes for DB2 data,
which would also speed up `husk export` (identical `loadListfile` cost
paid there today, not just in `resolve`). A `--from-list` batch mode on
`resolve` (mirroring `export`'s own `cmd_export.cpp`'s `exportOneModel`)
is a weaker, complementary idea, not a substitute — it only amortizes the
parse within one process's batch, not across every invocation. Out of a
tools/-only pass's scope to fix, flagged in `TODO/CLEANUP_TODO.md` item 3
rather than worked around here by re-adding the Python-side listfile
cache this conversion exists to delete. See `REFACTOR_LOG.md`'s
2026-08-29 entry for the full delta table and timings.

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

**Done.** `sources::Catalog::texture()` (`src/sources/catalog.cpp`) owns the
claim step exactly as described above: `filterCandidatesForType` runs once
(the pre-`Catalog` call site ran it twice — once inside
`claimSoleFuzzyTextureCandidate`, once again in the caller's own ambiguity
branch — always producing the identical set, since nothing mutated the pool
between the two calls, so this is a real, harmless simplification, not a
behavior change), and the exactly-one/zero/2-or-more split becomes one
`Resolved<EncodedTexture>` with `alternates` populated only in the last case.
The narrow wrap (`resolveClaimedFuzzyPoolTextureBytes`, read step only) that
was the correct interim before this landed is now absorbed — its own
Resolved<T>-reporting logic lives inline in `Catalog::resolveFuzzyTier`
instead, and `export_materials.cpp` no longer calls it or
`claimSoleFuzzyTextureCandidate`/`filterCandidatesForType`/
`orderCandidatesForDefault` directly at all.

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
