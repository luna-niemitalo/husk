# AUDIT.md — what's actually duplicated, divergent, or counter-intuitive

Evidence inventory for `REFACTOR/README.md`'s target pipeline. Grouped by *kind
of drift* rather than by file, because the same disease shows up in C++, Python,
and Blender code that never touch each other.

**Unlike every other file here, this one describes the tree as it actually
stands** — it is the evidence, not the target. Every other `REFACTOR/` document
describes something not yet built.

Every claim below carries a file:line checked against the tree at write time
(2026-08-28). An item is removed from this file when it is fixed — git history
is the record, not this document.

---

## 1. Duplicated resolution

### 1.1 "Which real bytes does a texture slot resolve to" — the catalog and its remaining mirrors

(Was four; `missing_texture_task.py`, the 1-tier outlier, is deleted — see
`REFACTOR_LOG.md`'s "AUDIT.md §11 closed" entry, since §11 itself is gone
now that it's fixed.)

| Where | Tiers implemented | Notes |
|---|---|---|
| `src/sources/catalog.cpp` (`Catalog::texture()`) | 3 (literal → listfile → fuzzy same-basename pool, incl. claim-and-remove + ambiguity) | The real one, a single object owning the tier order. Built on `src/sources/texture_resolution.cpp`'s primitives (scan/filter/order/read), the shared implementation detail, not a second policy. |
| `tools/corpus_scan_tasks/unfillable_texture_task.py` | **Fixed 2026-08-29** — 0, consumes `husk resolve` | Was 3, hand-mirrored (its own docstring named the mirroring as deliberate). This task needs only resolution *metadata* (did the slot resolve, via which tier), which is exactly what the ledger carries — see `REFACTOR_LOG.md`'s newest entry for the measured delta table. Cost traded: `husk resolve --listfile` re-parses the full 148MB listfile per invocation (~0.5s of a ~0.7s call, isolated by varying only listfile size), against the removed mirror's near-zero marginal cost — see the since-deleted `TODO/CLEANUP_TODO.md` / `REFACTOR/RESOURCE_CATALOG.md`'s own measured-cost section. |
| `tools/corpus_scan_tasks/texture_dedup_collision_task.py`, `black_additive_task.py` | still 1-2, **not converted** | Both need resolved *bytes*, not metadata. A conversion was written and **reverted the same session**: it drove them from `husk resolve --textures-out`, which is not a per-slot byte export but a best-effort convenience copy of whatever husk happened to decode (`writeTextureOutCopy`, its own comment: "not the thing the export itself depends on"). Measured on `creature/bearice/bearice.m2`: 4 slots resolved, 2 files written, one of the two not a resolved slot at all. Under it the dedup task found 0 collisions where it previously found 3, and `black_additive` would silently skip any already-`.png` texture. Blocked until husk can hand back the bytes for a resolved slot — see below. |

**The tier order is not the deepest problem here — the candidate set is.**
Measured 2026-08-29 against ground truth (the 415 DB2-named textures on disk
for `bloodelffemale_hd`): the pool's `stem.startswith(model_basename)` gate
contains **66 of 415 — 15.9%** of the correct answers, so
`orderCandidatesForDefault` has been ranking a set that mostly excludes the
right file. Semantic filename tags reach 98.6% on the same ground truth. The
same rule fails in the opposite direction on the non-HD model (pool of 934, of
which 463 are `_hd` art that does not apply, and the area-based tiebreak
prefers it). Full numbers, repro and staged fix:
`TODO/TEXTURE_POOL_RECALL_TODO.md`. Consolidating the implementations, which
this section is about, does not touch it — one correct implementation of a
rule with 15.9% recall is still 15.9% recall.

This shape has already caused one real incident: tier 2 was silently dropped
from the Python mirror during a rewrite, and a real 18,742-file CASC
re-extraction moved the scan's flagged count by exactly zero, because the tier
that would have noticed was never running. Nothing failed; the number was just
quietly wrong for weeks.

`CLAUDE.md`'s own Hazards section already warns readers which of these to trust
— a documentation workaround for a structural problem.

**In progress**: the real C++ implementation is now one object,
`husk::sources::Catalog` (`src/sources/catalog.hpp`/`.cpp`,
`REFACTOR_LOG.md`'s newest entry) — tiers 1 (literal), 2 (listfile), and 3
(fuzzy same-basename pool, including the claim-and-remove step and the
genuine-ambiguity fan-out, previously split across
`resolveClaimedFuzzyPoolTextureBytes` + an explicit caller-side branch) all
resolve through one `texture(fdid, textureType, modelContext,
preferGlowVariant) -> Resolved<EncodedTexture>` call, with ambiguity riding
`Resolved<T>::alternates` rather than a separate success shape
(`RESOURCE_CATALOG.md`'s Settled section, "Tier 3's shape" — now fully
closed, not partial). The knowledge-base tier
(`resolveObjectSkinTextureFromKb`, `cmd_export.cpp` — `RESOURCE_CATALOG.md`'s
"Where the two unordered tiers sit" names it tier 5) stays a pre-step outside
`Catalog::texture()` by design (it answers "which fdid", not "given this
fdid, find bytes"), but its one real catalog-shaped duty — the sideways
`listfile.emplace(...)` mutation of the caller's own shared map — is now
`Catalog::registerPathOverride`. Tier 4
(parent-directory same-basename) stays a marked, deliberate gap in
`Catalog::texture()`'s own doc comment — Blender-script-only, not ported
this pass; porting it changes real resolution outcomes and needs its own
ledger run. Verified via a resolution-ledger diff (byte-identical output,
zero delta) on `bloodelffemale_hd`, `nightelffemale_hd` (a real
218-candidate ambiguous pool), `wolf.m2` (all LODs), and
`sword_1h_artifactskywall_d_06.m2` (13 real fuzzy/ambiguous matches), plus a
`--knowledge-db`-driven item exercising `registerPathOverride`. This closes
the real C++ side of this section; the Python mirrors named in the table
above keep it open (`CLI_AND_TOOLING.md` §3).

A later pass converted five other `tools/corpus_scan_tasks/*.py` modules
(`particle_only_task.py`, `detect_billboards.py`, `expansion_task.py`,
`example_texture_count.py`, and half of `black_additive_task.py`) from
`husk info` prose-scraping to `husk info --json`
(`REFACTOR_LOG.md`'s newest entry, `CLI_AND_TOOLING.md` §3) — a different
duplication than this section's own (structured-field scraping, not
texture-tier resolution), so it does not close §1.1. `black_additive_task.py`'s
own texture-*resolution* half (`_resolve_texture_path`, still shelling out
to `husk blp-export` for pixel bytes) is the same tier-mirroring class this
section's table already names for `unfillable_texture_task.py` — deliberately
left untouched by that pass, still a live instance of this section's problem
**at the time it was written**. **Partly closed 2026-08-29**: `unfillable_texture_task.py` now consumes
`husk resolve` (`src/cmd_resolve.cpp`,
`RESOURCE_CATALOG.md`'s "excavation escape hatch" table) instead of a
Python-side tier mirror — no tool in `tools/` hand-derives literal/
listfile/fuzzy tier order anymore. `black_additive_task.py`'s own `husk
blp-export` shell-out is gone too, not just its tier logic: `husk resolve
--textures-out` already hands back real decoded PNG bytes regardless of
source format, so there is nothing left to convert separately. Full delta
table and per-file cost measurements: `REFACTOR_LOG.md`'s newest entry.
`texture_type_collisions_task.py` (`tools/find_texture_type_collisions.py`)
was scoped for this same conversion pass and found **not to fit it**:
despite this section's own table implying otherwise, that task does no
texture-*file* resolution at all — it compares two purely M2/`.skin`-
internal facts (`textureLookup`'s reverse pick vs. a batch's own
`textureCombos`-resolved texture index) that `husk resolve`'s ledger
doesn't carry (no raw `textureLookup` array, no per-batch textureCombos
index) and that its own docstring already frames as a deliberate,
independent "second opinion" check on husk's M2 parsing — the same
considered-exception shape `shader_id_task.py`/`shader_names_task.py`
already document elsewhere in this project. Left unconverted, not
silently skipped; not re-added to this section's own table since it was
never a texture-*resolution* duplication in the first place.

**Tier 4 (parent-directory same-basename) is still outside the catalog.**
Real customization-choice textures can sit one directory *up* from the
model under another naming convention (`character/nightelf/
eyes00_00_3509222.blp`, not `character/nightelf/female/
nightelffemale_hd_*.blp`; 7 of 559 choice textures on `nightelffemale_hd`,
0 of 826 on `bloodelffemale_hd`, measured 2026-08-29). The only consumer
that searched there (a Blender script) is deleted; whether the catalog's
FileDataID tiers (listfile, store `_by_fdid/`) now reach every such file
is unmeasured.
---

## 2. Missing internal representation

### 2.2 `M2MaterialInputs` is a bag with a back-pointer

`src/m2_material_input.hpp:59-87` (`M2MaterialInputs`) gathers the parsed
arrays plus `const std::vector<uint8_t>* blob` — a raw pointer to the parse
layer's own bytes, held so the material layer can resolve M2Track offsets
itself. A parse-layer detail reaching two stages forward.

---


## 7. Stage 3's own coverage gaps

The legacy pipeline it was measured against (via the
since-deleted `--export-canon` convergence checker, `canon_diff.*`) is
deleted; the gaps it found are closed (`REFACTOR_LOG.md`, 2026-09-15/16).

**Scene records**: attachments, events, lights and ribbon/particle
emitters are in `canon::Model::scene` and the bundle, with every field and
curve. Open within it:

- `.skel` attachment tracks resolve inline only: their per-sequence
  external data lives in `.anim` `AFSA` chunks, which husk does not read.
- A ribbon's `texture_transform_lookup_index` is carried raw, not resolved
  into UV-animation curves.
- Event firing times (`M2Event`'s timestamp track) are unparsed, so events
  are placement only.
- `EXP2` extended particle data is not in `m2::Model`; the local corpus has
  no EXP2 files to test against.

**Also open:**

- `assembleMaterial`'s `sequenceIndex` for a `.skel`-sourced model's
  material tint/alpha-fade/UV-animation curves is used as-is, on a
  documented assumption (inline material M2Track arrays are sized to match
  whichever sequence source is in effect) — not independently verified
  against real data for the material-track case specifically.
- No visual/pixel check of the bundle across the real fixture set, and no
  whole-corpus structural gate (`REFACTOR/README.md` Migration order). The
  render-diff tool (`compare_canon_render.py`) is deleted;
  `tools/render_lean_glb.py` renders one `--glb`.
