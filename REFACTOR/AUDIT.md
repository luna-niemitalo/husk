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

### 1.1 "Which real bytes does a texture slot resolve to" — four implementations

| Where | Tiers implemented | Notes |
|---|---|---|
| `src/export_texture_resolution.cpp` | 3 (literal FileDataID → listfile → fuzzy same-basename pool) | The real one. `export_materials.cpp:397-560` drives it. |
| `tools/corpus_scan_tasks/unfillable_texture_task.py:16-31` | 3, hand-mirrored | Its own docstring names the mirroring as deliberate. |
| `tools/corpus_scan_tasks/missing_texture_task.py` | 1 (literal only) | Its own module doc admits it over-flags anything the other tiers would resolve. |
| `tools/husk_blender_geoset_mask.py:1550-1607` | 2 + parent-dir glob + `husk blp-export` subprocess | Different tier order *and* a different fallback set from all three above. |

This shape has already caused one real incident: tier 2 was silently dropped
from the Python mirror during a rewrite, and a real 18,742-file CASC
re-extraction moved the scan's flagged count by exactly zero, because the tier
that would have noticed was never running. Nothing failed; the number was just
quietly wrong for weeks.

`CLAUDE.md`'s own Hazards section already warns readers which of these to trust
— a documentation workaround for a structural problem.

### 1.2 FileDataID → local path — four more

- `findFileDataIdForModelPath` (`src/export_extras.cpp:312`) — a *linear reverse
  scan* over the loaded listfile map, path → FileDataID.
- The forward lookup inline in `export_materials.cpp:471-489`.
- `exportGearAuxItemModels` (`src/cmd_export.cpp:740`) — its own
  `listfile.find(fdid)` + existence check + error text.
- `resolveObjectSkinTextureFromKb` (`src/cmd_export.cpp:238`) — the same
  question answered out of the knowledge-base SQLite instead, then *injected
  back into the listfile map* (`cmd_export.cpp:973-975`) so the embed path picks
  it up.
- `_load_listfile` (`tools/corpus_scan_tasks/unfillable_texture_task.py:95`) —
  a fifth, in Python.

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
- **`--knowledge-db` is documented as known-wrong** and unusable for real output
  (`CLAUDE.md` Hazards; `TODO/KNOWLEDGE_BASE_DESIGN.md`), yet remains a live
  flag that mutates the shared listfile map mid-export
  (`cmd_export.cpp:965-976`).

---

## 8. Duplicated / drifting constants in corpus tooling

`corpus_scan_framework.py:572` already takes `--root` as a real argument. Yet:

- `CORPUS_ROOT` is re-declared in **6 task modules** (`black_additive_task.py:47`,
  `casc_size_mismatch_task.py:53`, `unfillable_texture_task.py:59`,
  `texture_dedup_collision_task.py:56`, `render_sample_driver.py:54`,
  `m2_full_validation_task.py:29`) — seven copies of one value counting the
  framework's own hardcoded default. A task can silently disagree with the root
  it is actually being run against.
- `HUSK_BIN = .../build/husk` in **7 modules**, while the flake dev shell
  already puts `husk` on `PATH` — the same disease as the Blender script's
  `_find_husk_binary`, and it silently pins scans to a stale local build.
- `LISTFILE` in **5 modules**.
- `m2_full_validation_task.py:34-37` reconciles the copies by reaching into
  *another module's* globals: `cc.CORPUS_ROOT = ...`, `cc.HUSK_BIN = ...`,
  `cc.TIMEOUT = ...`.

The fix is subtraction, not relocation — see `CLI_AND_TOOLING.md` §4.

---

## 9. Doc/reality drift

- `DESIGN.md:142-143` states as a non-goal: *"No Blender addon. A `.glb` file
  Blender's stock importer can open is the entire deliverable."* There are
  ~4,150 lines of Blender Python in `tools/` implementing an addon in all but
  packaging.
- `src/cmd_export.cpp:919-923` still comments that husk "has no way to derive a
  layout ID on its own." Auto-derivation has existed since 2026-08-21.

---

## 10. File-size ceiling

`FILE_SIZE.md` sets a 1000-line ceiling, and §4 requires any file past it to
carry a one-line comment naming which exception applies. None of these do:

| File | Lines |
|---|---|
| `tools/husk_blender_geoset_mask.py` | 3505 |
| `src/cmd_export.cpp` | 1310 |
| `tools/corpus_scan_tasks/render_glb.py` | 1144 |
| `src/export_extras.cpp` | 1040 |

The stage split in `REFACTOR/README.md` resolves three of the four structurally
rather than by cutting them at arbitrary lines; `render_glb.py` is addressed in
`BLENDER_ADDON.md`.

---

## 11. Stale claims inside the corpus tooling itself

- `tools/corpus_scan_tasks/shader_id_task.py`'s own docstring states *"husk
  currently parses no field of M2Batch's on-disk shader_id"* — and builds its own
  raw parser on that basis. husk has parsed it since
  `src/skin.cpp:195` (`Batch::shaderId`, `skin.hpp:70`), with real name
  resolution in `src/m2_shader_names.hpp`. The task is duplicating a parser that
  now exists, on the strength of a comment that is no longer true.
- `tools/corpus_scan_tasks/missing_texture_task.py` is superseded outright by
  `unfillable_texture_task.py` — its own module doc says it over-flags — but it
  is still present and still runnable, so a future session can pick the wrong one
  the same way `CLAUDE.md`'s Hazards section has to warn against.
