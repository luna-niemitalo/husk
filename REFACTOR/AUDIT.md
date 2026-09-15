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

### 1.1 "Which real bytes does a texture slot resolve to" — three implementations

(Was four; `missing_texture_task.py`, the 1-tier outlier, is deleted — see
`REFACTOR_LOG.md`'s "AUDIT.md §11 closed" entry, since §11 itself is gone
now that it's fixed.)

| Where | Tiers implemented | Notes |
|---|---|---|
| `src/sources/catalog.cpp` (`Catalog::texture()`) | 3 (literal → listfile → fuzzy same-basename pool, incl. claim-and-remove + ambiguity) | The real one, now a single object owning the tier order — `export_materials.cpp`'s own three-way branch is gone, replaced by one `catalog.texture(...)` call. Built on `src/export_texture_resolution.cpp`'s primitives (scan/filter/order/read), which stay the shared implementation detail, not a second policy. |
| `tools/corpus_scan_tasks/unfillable_texture_task.py` | **Fixed 2026-08-29** — 0, consumes `husk resolve` | Was 3, hand-mirrored (its own docstring named the mirroring as deliberate). This task needs only resolution *metadata* (did the slot resolve, via which tier), which is exactly what the ledger carries — see `REFACTOR_LOG.md`'s newest entry for the measured delta table. Cost traded: `husk resolve --listfile` re-parses the full 148MB listfile per invocation (~0.5s of a ~0.7s call, isolated by varying only listfile size), against the removed mirror's near-zero marginal cost — `TODO/CLEANUP_TODO.md`. |
| `tools/corpus_scan_tasks/texture_dedup_collision_task.py`, `black_additive_task.py` | still 1-2, **not converted** | Both need resolved *bytes*, not metadata. A conversion was written and **reverted the same session**: it drove them from `husk resolve --textures-out`, which is not a per-slot byte export but a best-effort convenience copy of whatever husk happened to decode (`writeTextureOutCopy`, its own comment: "not the thing the export itself depends on"). Measured on `creature/bearice/bearice.m2`: 4 slots resolved, 2 files written, one of the two not a resolved slot at all. Under it the dedup task found 0 collisions where it previously found 3, and `black_additive` would silently skip any already-`.png` texture. Blocked until husk can hand back the bytes for a resolved slot — see below. |
| `tools/husk_blender_geoset_mask.py` (`_resolve_customization_texture_path`) | 1 (PNG-only filesystem search, textures dir + parent) | **Fixed 2026-08-29** — was 2 tiers + parent-dir glob + a `husk blp-export` subprocess (PATH/`../build/husk` lookup); now a single, honestly-scoped fallback for real customization-choice textures husk's own export doesn't embed (see below), with the subprocess and PATH lookup removed outright. |

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
218-candidate ambiguous pool), `wolf.m2` (incl. `--lod all`), and
`sword_1h_artifactskywall_d_06.m2` (13 real fuzzy/ambiguous matches), plus a
`--knowledge-db`-driven item exercising `registerPathOverride`. This closes
the real C++ side of this section; it does not yet touch the Python/Blender
mirrors named in the table above, so this section stays open until those are
addressed too (`CLI_AND_TOOLING.md` §3).

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
`tools/husk_blender_geoset_mask.py`'s mirror closed the same day, above.
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

**`tools/husk_blender_geoset_mask.py` closed 2026-08-29**: commit `a86b07f`
had already made husk's own same-basename ambiguity-pool candidates embed
directly in the `.glb` (`export_texture_resolution.cpp`), which
`_find_embedded_customization_image` reads via
`import_unused_materials=True` — measured as the primary path on a real
`nightelffemale_hd` export (81/559 customization-choice textures
considered, 5 materials switched) before this fix, leaving
`_resolve_customization_texture_path`'s filesystem mirror (2 tiers +
parent-dir glob + `husk blp-export` subprocess) as an unmeasured
fallback whose real firing rate this section already flagged as open.
Measured it directly (instrumented, real headless Blender runs, both
real fixtures, `--textures` given and omitted): the fallback fires for a
real, narrow, identifiable class — **7 of 559 (1.25%) on
`nightelffemale_hd`, 0 of 826 on `bloodelffemale_hd`**, every hit a real
eye-color customization choice (e.g. `eyes00_00_3509222.blp`) that husk's
export never embeds, because it carries a real, distinct FileDataID and
so never enters the same-basename ambiguity pool
`_find_embedded_customization_image` reads from (that pool only exists
for hardcoded slots with *no* FileDataID to disambiguate by). This is a
real gap in husk's own export, not a reason to keep a private resolver —
closing it for good belongs in `src/` (embed every real per-choice
texture, not only same-basename-ambiguous ones), out of this session's
scope (`src/` was off-limits, peer agents working there).

**This gap already has a name, established independently while verifying
the above**: those files sit one directory *up* from the model, under a
different naming convention — `character/nightelf/eyes00_00_3509222.blp`,
not `character/nightelf/female/nightelffemale_hd_*.blp`. That is exactly
`RESOURCE_CATALOG.md`'s **tier 4** (parent-directory same-basename), the
tier that document already says "is a real corpus fact, so it belongs in
the catalog — as a named tier, not as one consumer's private extension,"
and that `Catalog::texture()` still carries as a marked, deliberate gap.
So the `src/` fix is not open-ended: **port tier 4 into the catalog, and
embed what it resolves.** husk already knows these FileDataIDs — they
travel today in `chr_customization_options` extras (`materials[].
file_data_id`), which is how the Blender script knows to look for them at
all; only their *bytes* are missing from the `.glb`. Doing that makes the
Blender fallback genuinely dead code rather than a documented-narrow one,
and closes the regression noted below in the same move.

**Regression to close, stated as one rather than folded into the design
win**: auto-conversion of a `.blp`-only match was not incidental — it was
added specifically in response to Luna's pushback on workflow ceremony,
and `CLAUDE.md`'s Resume records the result as "no manual conversion step
at all anymore." Removing the subprocess is right (I3), but the honest
accounting is that it traded a user-facing capability for an invariant,
and the trade is only temporary if tier 4 lands. Until then a real
nightelf export switches 4 of 5 materials instead of 5 unless the caller
runs `husk blp-export --dir` first.

Per I3, the `husk blp-export` subprocess and its `PATH`/`../build/husk`
binary lookup are removed outright regardless — a `.glb` is not
guaranteed to travel with a `husk` binary (the same portability argument
that already moved `.phys` data into the export itself, see `CLAUDE.md`'s
Resume). The filesystem fallback itself is kept (documented, narrow,
still fires for the real class above) but reduced to PNG-only lookup; a
`.blp`-only match is now reported and skipped, not auto-converted — the
caller runs `husk blp-export --dir <dir> <out-dir>` once, ahead of time
(output filenames mirror each input's own basename, so the same
suffix-glob still finds them). Verified end to end: with the real
eye-color `.blp`s left unconverted, 4/5 materials switch (the 5th needs
those choices, loudly reported by name — no silent drop); with them
pre-converted via `husk blp-export --dir`, 5/5 materials switch again,
identical to before this fix, all 7 fallback-loaded images confirmed to
carry distinct real pixel content (not placeholders) via a headless
pixel-hash check. Confirmed via code inspection and a real headless run
that no `husk` binary is reachable or invoked anywhere in the script
anymore (`subprocess`/`shutil`/`tempfile` imports removed, zero call
sites remain).

---

## 2. Missing internal representation

### 2.2 `M2MaterialInputs` is a bag with a back-pointer

`src/export_materials.hpp:123-145` gathers ten parsed arrays plus
`const std::vector<uint8_t>* blob` — a raw pointer to the parse layer's own
bytes, held so the material layer can resolve M2Track offsets itself. A
parse-layer detail reaching two stages forward.

### 2.3 `gltf::Skeleton` is the canonical model wearing a consumer's name

633 lines (`src/gltf_skeleton.hpp`) holding `customizationOptions`,
`charTextureLayout`, `enabledMaterials`, `physicsBodies`, `physicsJoints`,
`gearItems`, `gearSectionOverlays`, `creatureEnabledGeosets`, `correctionSets`,
`attachments`, `events`, `lights` — none of which are glTF concepts.

Mixed in with them, transport-only fields that exist purely to get semantics
through glTF: `GearItem::auxGlbPath` (`:496`) and `geosetTags` (`:541`).

### 2.4 Population order is a hidden contract

`src/export_extras.hpp:70-80` documents, in prose, that
`attachCustomizationChoices` **must** run after `attachBoneCorrections` because
it mutates entries the latter attached. That ordering constraint exists only
because there is no model in which to state the relationship declaratively —
`exportOneModel` (`cmd_export.cpp:1038-1054`) is a hand-sequenced list of nine
`attach*` calls whose order is load-bearing but unenforced.

---

## 3. glTF impedance the bundle removes

- **Geosets shipped as fake bones.** `Skeleton::GeosetTag`
  (`gltf_skeleton.hpp:538`) mints one inert joint per distinct geoset ID purely
  so Blender's importer materializes it as a vertex group. Canonical Data Model §13
  names this exact pattern as the thing a canonical model exists to stop doing.
  It also inflates `skin.joints` well past real-time skinning budgets, which the
  Blender script then has to undo (`delete_geoset_tag_bones`).
- **Extras carried on a heuristically-located bone.** Blender drops
  `skin.extras` entirely, so all 13 skin-level keys are merged onto the skin's
  root joint node (`gltf_skeleton.cpp:530-539`). The Blender side then finds
  that carrier by *scanning bones for known husk key names*
  (`_root_joint_extras`), because post-import bone order does not match glTF
  joint order (242/358 mismatches measured on a real 245-bone character).
---

## 4. Blender-side coupling

**`render_glb.py` renders a pipeline that isn't the pipeline** — tracked in
full at `TODO/RENDER_PIPELINE_DRIFT_TODO.md` item 1, not duplicated here.

---

## 5. Slot-as-identity (I7)

Gear is keyed by equipment slot at every layer:
`GearSectionOverlay::slot` (`gltf_skeleton.hpp:448`), `GearItem::slot` (`:474`),
the `gear_items` / `gear_section_overlays` extras arrays, and the on-disk
naming `aux_models/<slot>_<fdid>.glb` (`cmd_export.cpp:766`).

Two consequences:

- A garment spanning two slots — a tunic whose hem is a leg-slot piece — has no
  representation at all. A consumer can only ever be handed "the chest part".
- Standalone-geometry items and texture-overlay items are two *parallel
  top-level lists* rather than two component kinds of one item, so nothing can
  describe an item that is both.

---

## 6. Untraceable naming (I6)

- Material display names resolve through a priority chain — customization
  choice name → texture type name → diagnostic chain
  (`export_materials.cpp:712-728`) — and `--slim-textures` filenames through a
  *different* chain ending in the community listfile
  (`gltf_mesh.cpp:191-200`). Both emit a **bare string**. Downstream, a
  listfile guess and a DB2 fact are indistinguishable.
- Bone names are contextual or synthesized (`applyContextualBoneNames`; the
  `"bone_" + index` fallback at `gltf_skeleton.cpp:136`) yet travel as plain
  strings that read as authoritative — including into `joint_names` and every
  `bone_name` field on physics, emitter, correction and gear entries.

---

## 7. Stage 3's own coverage gaps (canon:: not yet at legacy feature parity)

Sections 1-6 above are about the legacy pipeline's problems, the ones Stage 3
exists to fix. This section is different in kind: real, already-implemented
gaps in Stage 3 *itself* relative to what legacy already ships today. Found
2026-09-15 while building `husk export --compare-canon`'s runtime convergence
checker (`src/canon_diff.*`), and written down here because every one of them
was previously stated only as an isolated doc-comment in the file it lives
in — visible to a reader of that one file, invisible to `REFACTOR/README.md`'s
own Status summary, which currently reads as though Stage 3's `canon::Model`
composition root is a completed, like-for-like replacement. It isn't yet;
these are the concrete reasons why, not a vague caveat.

### 7.1 `canon::Material` never resolves real texture bytes (API shape + orchestrator wiring closed 2026-09-15)

**The design question is resolved**: canon:: is purely the representation of
already-decided facts. It never performs resolution itself; whatever
assembles a `canon::Model` resolves via `sources::Catalog` first and hands
canon:: the already-decided fact (or an explicit unresolved-reason) — a
pre-resolved input parameter, never a live `Catalog&` (matches §7.2's own
resolution for the same underlying question).

**The API shape is now implemented**: `assembleMaterial`/`assembleModel`
both take a new `TextureResolutions` parameter (`std::unordered_map<uint32_t,
TextureRef>`, keyed by the M2 texture array index — the same index
`MaterialLayer::identity` already exposes for a layer, no second key scheme
invented), defaulted to empty so every existing caller's behavior is
byte-for-byte unchanged. When a caller supplies an entry, canon:: records
that exact `TextureRef` (Resolved/KnownUnresolved/Ambiguous, whichever the
caller decided) verbatim — no resolution logic of canon:: own. Verified:
full suite green (966/966), `husk export --compare-canon` still clean
against `bloodelffemale.m2` (the new parameter is unused there, so this
confirms the default-empty path is a true no-op, not just untested).

**Orchestrator wiring is now done too**: `husk export --compare-canon`
(`src/cmd_export_canon.cpp`'s `buildTextureResolutions`) resolves every
distinct M2 texture-array index actually referenced by any real skin batch
through the SAME `sources::Catalog::texture()` call
`buildMaterialsAndPrimitives` (the legacy pipeline) already makes for that
exact `(modelPath, textureSlotIndex)` — one shared catalog answer, never a
second opinion (I2) — then converts each `Resolved<EncodedTexture>` into a
`canon::TextureRef` (`toCanonTextureRef`): a clean hit becomes `Resolved`
(fdid + `NameSource::Listfile`/`Db2`/`Synthesized` per tier), a genuine
ambiguity (non-empty `alternates`) becomes `Ambiguous` with the full
candidate set, a miss becomes `KnownUnresolved` with the catalog's own
`reason`. Deliberately does NOT replicate the legacy pipeline's separate
embedded-filename tier (pre-Cataclysm inline texture bytes) — a texture
unit with no real fdid and an embedded filename is left absent from the map,
falling back to `TextureRef`'s own default `KnownUnresolved`, since
`canon::TextureRef` has no shape for "these bytes came from inside the M2
itself" today. Verified: full suite still green (966/966); a real
`--compare-canon` run against `bloodelffemale.m2` stays clean (0 deviations)
with this wiring active. `canon_diff.cpp`'s own material-comparison note was
corrected to say what's actually still missing: `compareMaterialBlendModes`
itself was never extended to check resolved texture identity against
legacy's `gm.baseColorTextureFileDataId` — a gap in that one diagnostic, not
in canon:: or its resolution wiring.

**Texture-identity check added to `compareMaterialBlendModes`, found to be
currently unexercisable against a real fixture**: `compareMaterialBlendModes`
gained an optional `legacyMaterials` parameter (default empty, `{}` = skip,
same "predates this parameter" convention every other optional comparator
input already follows) — when both `canonModel.materials[i]`'s primary
layer is `TextureRef::State::Resolved` and `legacyMaterials` was supplied,
it checks the resolved `FileDataId` matches legacy's own
`baseColorTextureFileDataId`. 4 new unit tests cover match/mismatch/omitted/
`KnownUnresolved`-is-never-checked. **Running it for real against
`bloodelffemale.m2` surfaced a genuine, previously-undocumented structural
fact**: `buildMaterialsAndPrimitives` (legacy) dedupes materials by content
signature (`materialDedupKey`) — this real fixture's 70 surviving batches
collapse to 8 distinct `gltf::Material` entries — while
`canon::assembleModel` pushes exactly one `canon::Material` per surviving
batch with **no dedup at all** (`Model::materials`' own doc comment already
states the 1:1 batch correspondence as deliberate, but doesn't flag that
this diverges from legacy's own list shape). The new check's own
`legacyMaterials.size() != canonModel.materials.size()` guard caught this
safely (8 vs 70 → skipped with a note, not a false deviation), but it also
means the texture-identity check can currently never fire against ANY real
corpus file — only against hand-built unit fixtures where both lists happen
to already be the same length. Real follow-up, not done here: either give
`canon::Model` its own dedup pass (a real behavior change, not just a
diagnostic fix), or have the comparator walk `gltf::Mesh::primitives[i]
.materialIndex` on both sides instead of assuming positional 1:1
correspondence between the two material lists. Left open rather than
guessed at, since which of those two is "correct" is itself a real Stage 3
design question (does canon:: want legacy's dedup behavior at all, or is
"one material per batch" the deliberately simpler canon:: answer?) that
hasn't been decided yet.

**Still open**: no real fixture in this repo's `test_data/` ships alongside
a populated `--textures` directory, so this wiring has only been verified
structurally (clean rebuild, full suite, `--compare-canon` still clean with
the new parameter threaded through) — not yet against a real corpus file
where texture slots actually resolve to real bytes. A real end-to-end
visual check (do the resolved `TextureRef`s actually match what a real
textured render shows) is still open, same as the rest of this refactor's
"no gate is output-unchanged" policy.

### 7.2 canon animation: external `.anim` resolution (global sequences, alias resolution, external-`.anim` API shape + orchestrator wiring: all closed 2026-09-15)

**Global sequences and alias resolution are now implemented** (`canon_model.cpp`'s
three-pass `assembleModel`: pass 1 real-inline, pass 2 global-sequence, pass 3
alias-to-terminal reuse) and verified clean against `bloodelffemale.m2` via
`husk export --compare-canon` (0 deviations, `src/canon_diff.cpp`). Both were
pure M2-data operations with no filesystem/catalog involvement, as this
section originally predicted.

**A second, closely-related bug was found and fixed along the way, not
predicted by the original write-up**: the very first real-fixture
`--compare-canon` run after landing global-sequence/alias support still
showed ~25 deviations, all misdiagnosed at first glance as more alias cases.
They weren't — a direct probe of the real sequence flags
(`test_data/bloodelffemale.m2`) showed every one of them genuinely has the
inline bit set, no alias bit at all. The real cause: `buildAnimations`
(`export_animation.cpp:181-183`) only pushes a clip when at least one bone
produced real keyframe data for that sequence (`if (!anim.joints.empty())`);
`canon_model.cpp`'s pass 1 pushed a clip for every real-inline sequence
*unconditionally*, regardless of whether any bone had real data for it. Fixed
by adding the same `anyBoneAnimated` gate pass 2 (global sequences) already
had. Re-verified clean (0 deviations) after the fix.

**External `.anim` resolution is now closed too**, both the API shape and the
orchestrator wiring. `canon::assembleModel` gained an `ExternalAnimBlobs`
parameter (`canon_model.hpp`, `std::unordered_map<uint32_t,
std::vector<uint8_t>>` keyed by `model.sequences` index — the same
pre-resolved-input-parameter shape §7.1 settled on for textures), consumed
by pass 1's per-sequence loop exactly like a real-inline sequence's own
data. The file-search half (*deciding which FileDataID's `.anim` bytes to
fetch, and fetching them*) is reimplemented independently inside
`src/cmd_export_canon.cpp` itself (`resolveExternalAnimBlobForCanon`,
`findAnimFileId`, `findAnimFileByBasename`, `zeroPad` — all file-local),
mirroring `buildAnimations`'s own logic (`export_animation.cpp`:
FileDataID-named file first, same-basename fallback second, AFSB-over-AFM2
priority in a chunked file) rather than sharing code with it. Deliberate,
not an oversight: `--compare-canon`'s entire premise is comparing canon::
against an UNTOUCHED, ordinarily-shipping legacy pipeline — an earlier pass
of this work factored the shared logic directly out of `buildAnimations`
into a new `export_animation.hpp` function it then called too, verified
behavior-preserving (full suite green, no test changes needed), but Luna
correctly flagged that as wrong regardless of correctness: editing the
legacy file at all, even losslessly, means the "legacy" side of every
`--compare-canon` run from then on is no longer the real shipping pipeline,
undermining the comparison's own premise. Reverted, reimplemented
canon-side-only instead — the same "reimplement small private pieces rather
than share code with legacy" tradeoff `canon_model.cpp`/
`canon_animation_builder.cpp` already made for the sequence-flag bit tests,
applied here to a whole resolution routine. `git diff` against
`export_animation.cpp`/`.hpp` is empty; `husk export --compare-canon`'s own
orchestrator (`buildExternalAnimBlobs`) calls the canon-local resolver once
per non-inline, non-alias sequence in the re-parsed model, building the map
`assembleModel` now takes.

Verified against real data: exporting `bloodelffemale.m2` (inline bones)
with `--anim test_data/character/bloodelf/female --compare-canon` resolves
a real external `.anim` file under the same-basename fallback convention
(`bloodelffemale0060-00.anim`), legacy's own clip count rises 258 → 260, and
`--compare-canon` stays clean (0 deviations) against that same run — proof
canon's newly-wired external-anim path reproduces the exact clips legacy's
`buildAnimations` produces from the identical file, not just a structural
no-op. Full suite still green, 966/966.

**`.skel`-sourced coverage — closed 2026-09-15 (same-day follow-up, Stage 3's
next-biggest blocker)**: this section originally scoped itself to the
`bonesAreInline` case only — `m2input::buildCanonModel` read `model.bones`/
`model.sequences`/`model.blob` directly (the inline M2 source), never a
`.skel`-sourced bone/sequence pair, so a `.skel`-sourced export (`--skel`,
real AFSB-linked models — the common player-character case, including this
repo's own primary `bloodelffemale_hd.m2` fixture) either silently compared
against the model's own irrelevant, usually-empty inline data or (once the
material-dedup fix from §7.3 landed and mesh-skinning correctness started
mattering more) threw inside `runCanonCompareExport`'s own try/catch —
`--compare-canon` had, in effect, zero real coverage for the majority of
real character models.

**Fixed**: `m2input::buildCanonModel` gained an `ExternalSkeletonSource*`
parameter (`m2_canon_input.hpp`, default `nullptr` — every existing caller
unchanged): `{bones, sequences, blob}`, mirroring `skel::parseBones`/
`parseSequences`/`boneTrackBlob`'s own return shapes exactly. When supplied,
skeleton assembly, mesh-skinning's own bone-count bound, and all three
bone-animation passes (real-inline, global-sequence, alias) read from it
instead of `model`'s own inline arrays — mesh geometry (`model.vertices`)
and every material-related table stay `model`-sourced regardless, since
`.skel` only ever replaces bones/sequences (wowdev.wiki M2/.skel's own
scope). `runCanonCompareExport` (`cmd_export_canon.cpp`) now takes
`haveSkel`/`skelBytes` (already resolved once by `commands::resolveBones`
at the real call site, `cmd_export.cpp`) and, when `!bonesAreInline &&
haveSkel`, independently re-parses `skelBytes` itself — same "genuinely
separate re-derivation" policy this file's own top doc comment states for
the skin tier, not a reuse of legacy's own already-parsed result.
`buildExternalAnimBlobs`/`compareAnimations` both now take whichever
sequence array is actually in effect (`effectiveSequences`), not a
hard-coded `model.sequences` — a `.skel`-sourced clip's name (`anim_<id>_
<variationIndex>`) is reconstructed from the RIGHT array.

Verified against real data: `husk export bloodelffemale_hd.m2 --skin
bloodelffemale_hd00.skin --skel bloodelffemale_hd.skel --anim
character/bloodelf/female --compare-canon` (a real `.skel`-sourced, 245-bone,
338-animation character) now reports **"clean, no deviations found"** across
mesh/skeleton/animations/materials — not just "no crash," a genuine
structural match against legacy's own `.skel`-sourced output. Re-ran the
inline-bones case (`bloodelffemale.m2`) too, confirming the new parameter's
default (`nullptr`) is still a true no-op there. 2 new tests
(`tests/test_m2_canon_input.cpp`): one confirming the pre-existing
no-`ExternalSkeletonSource` call now throws against this real fixture (0
bones, real nonzero skin weights — expected, not silently wrong), one
confirming the real `.skel`-sourced skeleton/skinning/animation-clip output.
Full suite green, 977/977 (975 + 2 new).

Narrow, named, not independently verified: `assembleMaterial`'s own
`sequenceIndex` (chosen from whichever sequence source produced the first
resolved clip) is used as-is for a `.skel`-sourced model's material
tint/alpha-fade/UV-animation curves too, on the documented assumption
(`ExternalSkeletonSource`'s own doc comment, corroborated by `skel.hpp`'s
finding that SKB1 bone tracks share SKS1's sequence-array position 1:1)
that inline material M2Track arrays are sized to match whichever sequence
source is in effect — not confirmed against real data for the
material-track case specifically.

### 7.3 `canon::assembleModel` took `m2::`/`skin::` types directly — closed 2026-09-15 (sub-assemblers also moved, same day)

**Found while adding the material-dedup work below**: `canon::assembleModel`
(the whole-model composition root) took `m2::Model`/`skin::Batch`/
`skin::Submesh`/`std::vector<uint32_t>` directly and walked them itself —
silently making it a second M2-input-module in disguise, never caught
because every convergence test happened to feed it M2 data too.
`CANONICAL_MODEL.md`'s I1 never technically forbade this (I1 constrains
canon:: struct *fields*, said nothing about assembly *function signatures*)
— the gap was real, not a violated rule, and I1 has been extended with an
explicit clause naming it (`CANONICAL_MODEL.md`'s own updated section has
the full account, including Luna's own framing: *"the assemble model...
should not care if the source format is a potato or m2"*).

**Fixed**: `canon::assembleModel`'s old orchestration body (which sequence
produces a clip, which batch shares a material, the three-pass animation
walk) moved to a new file, `src/m2_canon_input.hpp`/`.cpp`
(`husk::m2input::buildCanonModel`) — the M2 input module, a sibling of
canon::, not part of it. `canon::assembleModel` itself shrank to real
composition only: `Model assembleModel(Skeleton, Mesh,
std::vector<Material>, std::vector<Identity> primitiveMaterials,
std::vector<AnimationClip>)` — no m2/skin type anywhere in it, validating
only the one structural invariant this composition step owns
(`primitiveMaterials.size() == mesh.primitives.size()`, and every
`RecordIndex` in it in range for `materials`).

**Landed together with the material-identity fix this section's own header
already named as the real motivating question**: `canon::Material` gained
a `Ref ref` identity field (`RecordIndex{i}` for every real local producer
today); `canon::Model::materials` is now genuinely DEDUPED (content-
distinct materials only) instead of one entry per surviving batch;
`canon::Model::primitiveMaterials` (one `Identity` per `mesh.primitives`
entry) replaces the old implicit "materials[i] describes primitives[i]"
positional convention. Dedup key: a batch's own
`(materialIndex, textureCount, textureComboIndex, textureCoordComboIndex,
colorIndex, textureWeightComboIndex, textureTransformComboIndex)` tuple —
this fully determines what `assembleMaterial` would build for it (given
the model-wide-constant sequenceIndex/textureResolutions), so two batches
sharing that tuple share one `canon::Material`, found by identity rather
than by hashing the built material's content after the fact the way
legacy's own `materialDedupKey` (`export_texture_resolution.cpp`) does —
cheaper (no redundant `assembleMaterial` calls for what turns out to be a
duplicate) and without that approach's float-stringification fragility
risk.

Every real consumer of the old 1:1 assumption was found and fixed, not
just the type declarations: `writers::writeLeanGlb`/`writers::writeBundle`
(both threw or silently mis-indexed on the old assumption; both now use a
new shared `canon::resolveMaterialIndex(model, primitiveIndex)` — the one
implementation of this lookup, I2, also reused by `canon_diff.cpp`'s
`compareMaterialBlendModes`, which itself gained the same indirection on
legacy's side via `gltf::Primitive::materialIndex` — legacy already had its
own equivalent index-into-deduped-list shape, `compareMaterialBlendModes`
just wasn't using it).

**Verified against real data, and the strongest kind of verification this
task got**: exporting `bloodelffemale.m2` through `--compare-canon`, the
canon bundle's own `materials` count came out to exactly **8**, matching
legacy's own real deduped count for the same file precisely — not just "no
crash," a real independent confirmation that the tuple-identity dedup key
lands on the same equivalence classes legacy's content-hash key does, on
real, non-trivial data (70 primitives → 8 distinct materials on both
sides). Full suite green, 975/975 (9 new/rewritten tests:
`tests/test_canon_model.cpp` — now covering `canon::assembleModel`'s own
much smaller pure-composition job — plus `tests/test_m2_canon_input.cpp`,
the renamed home of what used to be `test_canon_model.cpp`'s real-fixture
orchestration coverage, updated for the new dedup invariant).

**Follow-up, same day, now also closed**: `assembleMesh`/`assembleSkeleton`/
`assembleMaterial`/`assembleBoneAnimation(Global)` — the sub-assemblers
flagged above as still living in `namespace husk::canon` while taking
`m2::`/`skin::` types directly — moved into `namespace husk::m2input`:
`m2_mesh_input.hpp`/`.cpp`, `m2_skeleton_input.hpp`/`.cpp`,
`m2_material_input.hpp`/`.cpp`, `m2_animation_input.hpp`/`.cpp` (the old
`canon_mesh_builder.*`/`canon_skeleton_builder.*`/
`canon_material_builder.*`/`canon_animation_builder.*` moved to `trash/`).
The pure canon:: value types those files used to conflate with their
M2-consuming assembler in one file/namespace — `canon::Mesh`/
`PrimitiveGeoset`, `canon::BoneAnimationCurves` — were split out into new
struct-only files that stay in `namespace husk::canon` with zero
m2::/skin:: types (`canon_mesh.hpp`, `canon_animation.hpp`); canon::
Material/MaterialLayer already lived in their own `canon_material.hpp`
this way, so only its M2-consuming neighbor (`M2MaterialInputs`,
`TextureResolutions`, `assembleMaterial`) needed to move out, not the
whole file; `canon_skeleton_builder.hpp` had no struct of its own to
extract at all (Skeleton/Joint already lived in `canon_skeleton.hpp`), so
it was a straight namespace move. `canon_bone_naming.hpp`/`.cpp` was
checked and confirmed to take only `canon::Skeleton` — never part of this
gap, left in `husk::canon` unmoved. Every real consumer updated
(`m2_canon_input.cpp`, `cmd_export_canon.cpp`, `canon_diff.cpp`,
`writers/writer_common.hpp`'s doc comments, 5 convergence test files, plus
`tests/test_m2_canon_input.cpp`); full suite green, 975/975, both before
and after.

**Follow-up, same day, now also closed**: `canon::Mesh::positions`/
`normals`/`uv0`/`uv1`, `canon::Skeleton::Joint::globalPosition`, and
`canon::VecCurve`/`QuatCurve` (`canon_curve.hpp`) were declared as
`m2::Vec3`/`m2::Vec2`/`Curve<m2::Vec3>`/`Curve<m2::Quat>` — a namespaced M2
type used as a canon:: struct field, the same shape I1 forbids. New
`canon_primitives.hpp` gives canon:: its own `Vec2`/`Vec3`/`Quat`; every
real producer (`m2_mesh_input.cpp`, `m2_skeleton_input.cpp`,
`m2_material_input.cpp`, `m2_animation_input.cpp`) converts at its own
boundary instead. `canon_diff.cpp` and `writers/gltf_lean.cpp` each gained
(or already had) a small private `toGltf`/`toGltfScale`/`toGltfQuat`
wrapping the same shared `gltf_math.hpp` axis-conversion functions
`commands::toGltf` (export_transform.hpp) itself wraps, rather than adding
a canon::-accepting overload to that legacy file. `CANONICAL_MODEL.md`'s
own updated section has the full account. Full suite green, 975/975,
before and after. Nothing named as open in this section remains.

