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

### 2.1 There is no `m2::Model`

**Correction (2026-09-03):** this section's own table originally listed three
commands. `cmd_info_json.cpp` is a real fourth hand-assembled view -- ~15
`m2::parse*` call sites of its own (`husk info --json`'s JSON twin of
`cmd_info.cpp`'s prose, `src/cmd_info_json.cpp`) -- that the table below had
simply omitted, not a case that didn't exist yet.

Four commands each hand-assemble a *different* partial view of the same file
from loose parse calls:

| Command | `m2::parse*` calls | What it omits |
|---|---|---|
| `cmd_export.cpp` | 11 kinds | attachments, events, lights, ribbons, particles |
| `cmd_info.cpp` | 12 kinds | vertices, colors, texture weights/transforms |
| `cmd_info_json.cpp` | 10 kinds, 15 call sites -- mirrors cmd_info.cpp's own fields, minus parseHeader/extractBlob (reuses the caller's already-parsed header/blob) | same as cmd_info.cpp: vertices, colors, texture weights/transforms |
| `cmd_dump.cpp` | header + blob only | everything else, re-parsed ad hoc downstream |

So "what does husk think is in this file" has four different answers depending
on which verb you typed.

**Update (2026-09-03): `cmd_info.cpp`/`cmd_info_json.cpp` migrated onto
`m2::Model`** -- both now call `m2::loadModel()` once and read fields off the
result instead of their own loose `parse*` calls (REFACTOR/README.md's
Migration order step 2; full narrative and the diff-gate numbers:
`REFACTOR_LOG.md`'s newest entry). `cmd_export.cpp`/`cmd_dump.cpp` are not
migrated yet -- this section stays open until they are too.

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

## 4. Blender-side coupling — every item violates I3

- **Four discovery mechanisms for one question** ("where is this texture"), in
  `tools/husk_blender_geoset_mask.py`: a `--textures` CLI argument, the
  `.glb`'s own directory, the current `.blend` file's directory, a preferred
  `textures/` subdirectory, and a `*_<fdid>.png` glob in the textures dir
  *and its parent*. **Reduced from six 2026-08-29**: the `husk` binary found
  on `PATH` or at `../build/husk`, and the `husk blp-export` subprocess it
  shelled out to, are both removed outright (`REFACTOR/AUDIT.md` §1.1's
  "closed 2026-08-29" entry has the measurement and rationale — the
  remaining filesystem glob fires for a real, narrow, now-documented class
  of texture husk's own export doesn't embed, a finding for `src/` to
  eventually close, not a reason to keep the subprocess). The four remaining
  mechanisms are all real "told where to look," not "went looking" —
  `--textures`/the `.glb`'s directory/the `.blend`'s directory/the
  `textures/` subdirectory are all ways of being handed a starting
  directory, and the same-basename glob inside it is the one piece still
  worth a future I3 pass if `src/`'s own embedding gap ever closes and makes
  it moot.
- **Duplicated helpers, now with a load-bearing reason, not just policy.**
  `_root_joint_extras` / `_deep_copy_id_property` are copied verbatim into
  `tools/husk_blender_options_panel.py:99-119`. **Investigated 2026-08-29**
  whether a shared sibling module could replace both copies: found a real
  blocker, not just a precaution — `husk_blender_options_panel.py`'s whole
  reason for existing as a separate file is running as a *registered
  embedded Text datablock* so a `.blend` self-installs with zero setup, and
  a headless round-trip confirmed `__file__` in that mode is a synthetic
  value (`<blend path>/<text name>`), not a real filesystem path, so a
  `__file__`-relative sibling import would resolve nothing there. Left as a
  documented copy; the comment at `:87-104` now states this concretely
  instead of citing sibling-session concurrency. The geoset vertex-group
  naming convention is still duplicated a third time in the same file, as a
  regex plus two prefix constants (`:73-84`) — same blocker applies, not
  revisited separately.
- **`render_glb.py` renders a pipeline that isn't the pipeline.** It imports
  `husk_blender_geoset_mask` for billboard alignment only (`:41-42`) and never runs
  the customization or geoset stages, so previews of any model carrying
  `chr_customization_options` show whichever candidate husk arbitrarily embedded
  as each slot's default. This is `TODO/RENDER_PIPELINE_DRIFT_TODO.md` item 1,
  found when a correct export produced a washed-out preview and the preview was
  believed over the export.

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

## 8. Duplicated / drifting constants in corpus tooling

**Done** for every real `ScanTask`-shaped module (`corpus_scan_framework.py`
now exposes `ROOT`/`LISTFILE`/`HUSK_BIN` as single shared, dynamically-read
values — see `CLI_AND_TOOLING.md` §4 and `REFACTOR_LOG.md`'s 2026-08-28
entry closing this item). `black_additive_task.py`, `casc_size_mismatch_task.py`,
`unfillable_texture_task.py`, `texture_dedup_collision_task.py`,
`m2_full_validation_task.py`, and `particle_only_task.py` no longer declare
their own copies. Deliberately **not** touched: `render_sample_driver.py`
(a driver script with its own argv, not a `ScanTask`, and part of the
render pipeline this project has repeatedly treated as human-gated —
see `CLAUDE.md`'s Hazards) still has its own `CORPUS_ROOT`/`HUSK_BIN`/
`LISTFILE` copies.

Two real bugs found and fixed along the way, neither hypothetical:

- `HUSK_BIN = "husk"` alone (the fix `CLI_AND_TOOLING.md` §4 originally
  proposed, trusting that doc's own claim the flake dev shell puts `husk`
  on `PATH`) **fails on the real environment** — verified live,
  `.direnv/bin` carries no `husk` symlink. Not a dev-shell bug: installing
  the flake as a package (`nix profile install`/`nix run`) does put `husk`
  on `PATH`, that just isn't the dev-shell environment this corpus tooling
  actually runs under. Fixed with the same `shutil.which("husk") or
  <build path>` fallback `corpus_checks.py` already used for
  `GLTF_VALIDATOR_BIN`, in one place (`corpus_scan_framework.HUSK_BIN`),
  read by every consumer.
- Every task module's own docstring documents running
  `corpus_scan_framework.py` directly as a script — which loads it as
  `__main__`, a *separate* module object from the `corpus_scan_framework`
  a task gets via its own `import corpus_scan_framework`. `_init_worker`
  was setting `ROOT`/`LISTFILE` on whichever identity actually ran while
  every task read them off the other, untouched, still-`None` one — caught
  live via a real smoke-test run (`AttributeError: 'NoneType' object has
  no attribute 'exists'`), not assumed. Fixed with one `sys.modules.
  setdefault("corpus_scan_framework", sys.modules[__name__])` so both
  names resolve to the same object regardless of which one loaded first.

The fix is subtraction, not relocation — see `CLI_AND_TOOLING.md` §4.

