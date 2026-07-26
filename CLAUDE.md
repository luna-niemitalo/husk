# CLAUDE.md — husk

Global rules apply (`~/.claude/CLAUDE.md`). Nix conventions: `.claude/rules/nix.md`.
Read `DESIGN.md` before any structural change.

## Purpose

CLI that reads WoW M2 model files (+ `.skin`/`.skel`/`.bone`/`.anim` sidecars) and
exports them to glTF 2.0 (`.glb`) for Blender import; `husk-blp` (separate Python
tool, `blp/`) converts BLP2 textures to PNG.

## Status

- **Current**: `husk info` (header/record-count/chunk-tag summary, incl. per-texture/
  material detail and sidecar FileDataIDs), `husk export` (static mesh → skeleton +
  skinning, inline or external `.skel` → materials with real embedded textures →
  animation, inline/external-`.anim`/`.skel`-sourced, verified against real
  `bloodelffemale.m2`/`bloodelffemale_hd.m2` data), `husk export --lod`
  (single-tier or `all`), `husk dump-chunks` (JSON dump of chunks with no glTF
  equivalent, or `.bone` files directly). `blp/`'s `husk-blp` (BLP2 → PNG:
  palettized/DXT1/DXT5/BGRA). See `README.md`'s format-support matrix and roadmap for
  the exact per-feature state — that table is the source of truth, not this file.
- **Target**: a real Blender import path for modern (Legion+ chunked) M2 — see
  `DESIGN.md`'s Goal section. Roadmap stage 7 (Khronos glTF-Validator pass) is the
  next real milestone; WMO and M3 are tracked, not started, by design.
- Anything not listed under Current does not exist yet. In particular: `M2Particle`/
  `M2Camera` are still count-only (not dereferenced); `.anim`'s `AFSB` chunk shape is
  undocumented and detected-but-skipped, not parsed. Three FAILURES2.md gaps
  (geoset selection #1, multi-texture-layer rendering #6, global-sequence animation
  #7) all went further than a diagnostic this session: geoset `skinSectionId` and
  additional (`textureCount > 1`) texture layers are now real glTF `extras`
  metadata on every primitive/material (husk still doesn't *filter*/*render* either
  one — no DBC data to ground a default geoset choice in, no core-glTF slot for a
  second texture layer — but a custom renderer or Blender script now has everything
  it needs to implement its own selection/blend on top), and global-sequence
  tracks resolve to real, separate glTF animation clips
  (`global_seq_<n>`). Verified against real data: `bloodelffemale.m2` goes from 256
  to 258 animation clips, and its 66-geoset/1-multi-texture-batch `.skin` exports
  cleanly with the new extras attached.

## Boundaries

- Model file bytes (`.m2`) — chunk container + fixed-offset header/arrays
  (`src/chunk.cpp`, `src/m2.cpp`).
- `.skin` sidecar — triangle-index lookup, submesh/batch structure (`src/skin.cpp`).
- `.skel` sidecar — external bones + sequences (`src/skel.cpp`).
- `.bone` sidecar — per-bone correction matrices, reverse-engineered (`src/bone.cpp`).
- `.anim` sidecar — external per-sequence keyframe blob; `AFM2` (flat) parsed, `AFSB`
  detected and rejected (`m2::extractAnimBlob`).
- `--textures`/`--skin-dir`/`--anim-dir` directories — user-populated,
  FileDataID-named, local filesystem only. **Never CASC** — husk has no
  CASC/listfile access and never will, by design (see `DESIGN.md`'s Non-goals).
- `.blp` texture files (separate `blp/` Python tool) — container hand-rolled, block
  decode delegated to Pillow via a synthetic DDS wrapper.
- No network access anywhere in this tool. No user input beyond CLI argv (parsed in
  `cmd_info.cpp`/`cmd_export.cpp`/`cmd_dump.cpp`, no interactive prompts).

Every boundary above is read via explicit bounds-checked parsing at named offsets,
throwing a descriptive `ParseError`/`std::runtime_error` on anything foreign data
claims that doesn't fit — never a silent misread. See `FAILURES.md`/`FAILURES2.md`
for the specific bugs found and fixed under this discipline, and `WIKI_FINDINGS.md`
for every real-file-driven spec correction found along the way.

## Resume

- **Last state**: `FAILURES2.md` (a second read-only inspection pass, follow-on to
  `FAILURES.md`) done — 9 real findings fixed in `src/`/`tests/` (each with a
  regression test), one (`M2.md` documentation-mirror finding) retracted as
  mistaken. Three of those 9 were then pushed further than the initial
  diagnostic-only fix, after Luna asked whether they could actually be solved:
  - **#7 global-sequence animation** — fully resolved, real glTF clips. New
    `m2::resolveVec3/QuatGlobalSequenceTrack` + `m2::parseGlobalLoops`
    (`src/m2.cpp`/`.hpp`) resolve a global-sequence track's single outer
    sub-array; `cmd_export.cpp`'s `buildGlobalSequenceAnimations` turns every
    distinct global-sequence index a model's bones use into a real clip
    (`global_seq_<n>`). `bloodelffemale.m2`: 256 → 258 clips.
  - **#1 geoset selection + #6 multi-texture layers** — not filtered/rendered
    (still can't be, without DBC data / a core-glTF multi-layer slot — see
    below), but no longer diagnostic-only either: every submesh's real
    `skinSectionId` (+derived `geoset_group`/`geoset_variant`) and every
    additional (`textureCount > 1`) texture layer's FileDataID/UV-set (+a real
    embedded-but-unused image when `--textures` has a match) are now inert
    glTF `extras` on each primitive/material — `gltf::Primitive::skinSectionId`,
    `gltf::Material::additionalTextureLayers` (`src/gltf.hpp`/`.cpp`), wired
    from `cmd_export.cpp`'s `buildMaterialsAndPrimitives`. Same "tag it, don't
    guess at semantics" precedent `billboardMode` already set. Luna's own
    framing settled the design question: Blender (mesh masks, geometry nodes,
    driven materials) can implement selection/toggling itself once the data's
    there with distinguishing metadata — husk doesn't need to split submeshes
    into separate glTF nodes for that to work, tagging-in-place is enough.
  258/258 tests passing (synthetic + real-data integration against
  `bloodelffemale.m2`, including its 66 real geosets and 1 real multi-texture
  batch). Luna independently split the old single `README.md` into `README.md`
  (usage/status/roadmap) + `DESIGN.md` (architecture rationale) during this same
  session; that split is reflected in this file and is otherwise Luna's edit, not
  touched further.
- **Next step**: nothing in flight. If picking this back up: `FAILURES2.md` #1/#6
  are about as far as they reasonably go without either DBC/DB2 access (out of
  scope, hard project non-goal) or a documented WoW combiner-math translation to
  core glTF (doesn't exist) — further work there would be Blender-side (a script/
  addon reading the `extras` this session added), not more husk parsing. `.bone`
  LOD-context integration, `M2Particle`/`M2Camera` dereferencing, and `AFSB`
  reverse-engineering remain the open structural gaps (`TODO_correctness.md`).
- **Hazards**: `README.md`'s Usage/roadmap prose still says things like "husk info
  resolves and prints attachments/events/lights records" without yet mentioning the
  newer per-texture/material printing, the now-258-not-256 animation-clip count for
  `bloodelffemale.m2`, or the geoset/multi-texture-layer `extras` metadata added
  this session — the format matrix and roadmap text haven't been synced to
  `FAILURES2.md`'s fixes yet (deliberately deferred while Luna was mid-edit on
  `README.md`/`DESIGN.md` concurrently — don't touch those two files without
  rereading them fresh first, they may have moved again).
