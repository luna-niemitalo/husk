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
| `src/export_texture_resolution.cpp` | 3 (literal FileDataID → listfile → fuzzy same-basename pool) | The real one. `export_materials.cpp:397-560` drives it. |
| `tools/corpus_scan_tasks/unfillable_texture_task.py:16-31` | 3, hand-mirrored | Its own docstring names the mirroring as deliberate. |
| `tools/husk_blender_geoset_mask.py:1550-1607` | 2 + parent-dir glob + `husk blp-export` subprocess | Different tier order *and* a different fallback set from both above. |

This shape has already caused one real incident: tier 2 was silently dropped
from the Python mirror during a rewrite, and a real 18,742-file CASC
re-extraction moved the scan's flagged count by exactly zero, because the tier
that would have noticed was never running. Nothing failed; the number was just
quietly wrong for weeks.

`CLAUDE.md`'s own Hazards section already warns readers which of these to trust
— a documentation workaround for a structural problem.

### 1.2 FileDataID → local path

**Reverse direction (path → FileDataID) done** — the former
`findFileDataIdForModelPath` (`src/export_extras.cpp`) moved to
`husk::sources::fileDataIdForPath` (§1.3, an earlier session).

**Forward direction (FileDataID → path), three of the four originally named,
done** (`REFACTOR_LOG.md`'s 2026-08-28 entries) — `husk::sources::pathForFileDataId`
(`src/sources/listfile_catalog.hpp`/`.cpp`), a verbatim-behavior move of
the identical `listfile.find(fdid)` + root-join step every call site
duplicated:

- The forward lookup inline in `export_materials.cpp`'s primary
  baseColorTexture texture-tier fallback — now calls `pathForFileDataId`
  (via `sources::resolveListfileTextureBytes`, §1.1's tier-2 wrapper), still
  does its own extension-stripping afterward.
- `exportGearAuxItemModels` (`src/cmd_export.cpp`) — now calls
  `pathForFileDataId`, still does its own existence check + error text.
- **A fifth site, not in this section's original four-site count**:
  `export_materials.cpp`'s `additionalTextureLayers` loop (multi-texture-
  layer batches, `textureCount > 1`) had its *own* independent
  `listfile.find(fdid)` + manual join, found while wiring the primary
  site's tier 2 through `Resolved<T>` — same duplication, just missed by
  the original audit pass. Now also routed through
  `resolveLiteralTextureBytes`/`resolveListfileTextureBytes`.

**Still separately duplicated, not covered by any of the above**: the two
name-only lookups — `export_materials.cpp:436`'s `gm.realContentName`
assignment and `export_extras.cpp:623`'s `cm.contentName` assignment — both
do `listfile.find(fdid)` then `.stem().string()` for display naming only
(no bytes, no path read). Identical to each other, not yet consolidated;
a real, small follow-up (`sources::contentNameForFileDataId(listfile, fdid)
-> optional<string>`), not attempted this session to keep this tick's scope
to the byte-resolution tiers.

**Deliberately still separate** (different data sources/languages, not
the same duplication):

- `resolveObjectSkinTextureFromKb` (`src/cmd_export.cpp`) — the same
  question answered out of the knowledge-base SQLite instead (a genuinely
  different backing store), then *injected back into the listfile map*
  (`cmd_export.cpp`'s `listfile.emplace(...)`) so the embed path picks it
  up — this injection itself is real duplication-adjacent surface, left
  for the real catalog object (§4 of `CLI_AND_TOOLING.md`'s
  `--knowledge-db` entry already flags it).
- `_load_listfile` (`tools/corpus_scan_tasks/unfillable_texture_task.py`) —
  a separate implementation, in Python, outside this C++ consolidation's
  reach.

---

## 2. Missing internal representation

### 2.1 There is no `m2::Model`

Three commands each hand-assemble a *different* partial view of the same file
from loose parse calls:

| Command | `m2::parse*` calls | What it omits |
|---|---|---|
| `cmd_export.cpp` | 11 kinds | attachments, events, lights, ribbons, particles |
| `cmd_info.cpp` | 12 kinds | vertices, colors, texture weights/transforms |
| `cmd_dump.cpp` | header + blob only | everything else, re-parsed ad hoc downstream |

So "what does husk think is in this file" has three different answers depending
on which verb you typed.

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
- **No schema version anywhere.** 13 root-joint extras keys
  (`bone_correction_sets`, `chr_customization_options`, `chr_enabled_materials`,
  `chr_texture_layout`, `creature_enabled_geosets`, `enabled_geosets`,
  `gear_items`, `gear_section_overlays`, `joint_names`, `particle_emitters`,
  `physics_bodies`, `physics_joints`, `ribbon_emitters`) plus 34 material and
  primitive extras keys, and no index document listing them. The only producer
  marker in the whole file is `model.asset.generator = "husk"`
  (`gltf.cpp:58`).

---

## 4. Blender-side coupling — every item violates I3

- **Six discovery mechanisms for one question** ("where is this texture"), in
  `tools/husk_blender_geoset_mask.py`: a `--textures` CLI argument
  (`:3309-3312`), the `.glb`'s own directory (`:3323`), the current `.blend`
  file's directory (`:3339`), a preferred `textures/` subdirectory (`:3350`),
  a `*_<fdid>.png` glob in the textures dir *and its parent* (`:1575-1596`),
  and a `husk` binary found on `PATH` or at `../build/husk` (`:1497-1502`) to
  shell out to `blp-export` (`:1538`).
- **Duplicated helpers with a manual resync policy.**
  `_root_joint_extras` / `_deep_copy_id_property` are copied verbatim into
  `tools/husk_blender_options_panel.py:99-119`, with a comment describing the
  copy as a deliberate "resync from there if they ever diverge" policy. The
  geoset vertex-group naming convention is duplicated a third time in the same
  file, as a regex plus two prefix constants (`:73-84`).
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

## 7. Interface inconsistency

- **26 `export` flags, zero `->group()` calls** (`cmd_export.cpp:491-673`) —
  `CLI.md` §1's flat-namespace failure verbatim.
- **Three grammars for one shape of question.**
  `--anim` is four-state (`auto` / `inline` / `none` / path);
  `--skin`, `--textures`, `--skin-dir`, `--skel`, `--bones-dir`, `--phys` are
  three-state; `--db2-dir`, `--dbd-dir`, `--listfile`, `--listfile-root` are
  two-state.
  The two-state case has a real justification — `auto` is only honest when the
  input describes where the thing is, and an external checkout has no canonical
  location — but that reason is written down nowhere. What *is* a genuine gap is
  the missing `none`: since config-file defaults landed, a configured
  `listfile`/`db2-dir` cannot be switched off per-invocation at all. The
  evidence is that `tests/run_husk.hpp` has to blank the entire config
  (`HUSK_CONFIG=/dev/null`) to get a clean run.
- **`husk info` emits human text only**, while `dump-chunks` emits JSON. Eight
  corpus tasks regex-scrape the former.
- **`--knowledge-db` is documented as known-wrong** (`CLAUDE.md` Hazards;
  `TODO/KNOWLEDGE_BASE_DESIGN.md`) and remains a live flag by deliberate
  decision (kept as diagnostic/future-work infrastructure, not retired).
  **Done**: the known-wrongness now surfaces at point of use (I4) — see
  `CLI_AND_TOOLING.md` §5. Still mutates the shared listfile map
  mid-export, a real instance of §1.2's duplication, left for the catalog.

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

