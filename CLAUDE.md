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
  `DESIGN.md`'s Goal section. All 8 roadmap stages are now done, including stage 7
  (output hardening: real exports now run through the Khronos glTF-Validator *and*
  headless Blender itself, `tests/test_conformance.cpp` — see Resume). Remaining
  work is either scope expansion (WMO/M3, not started, by design) or the structural
  gaps `TODO_correctness.md` already tracks (`AFSB`, `M2Particle`, `.bone`
  LOD-context integration) — nothing currently in flight.
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

- **Last state**: roadmap stage 7 (output hardening) closed out this session —
  every real export now runs through two independent real downstream consumers
  in `tests/test_conformance.cpp` (2 new tests, 258 → 260 total), not just
  tinygltf's own permissive reader:
  - The Khronos glTF-Validator CLI (`gltf_validator`), asserting zero
    validator-reported errors. Getting this running surfaced a real nix
    packaging bug in `nix/flake.nix`'s `gltf-validator` derivation (added
    just before this session, from the official precompiled Linux release):
    it's a Dart-AOT binary whose app logic lives in a VM isolate snapshot
    appended to the ELF, and *both* `autoPatchelfHook`'s interpreter rewrite
    and stdenv's default strip step independently corrupt that snapshot —
    confirmed by hand-testing each in isolation. Fixed with `dontPatchELF`/
    `dontStrip` plus running the untouched binary through `steam-run-free`
    (same FHS-sandbox mechanism as `steam-run`, MIT-licensed, doesn't pull in
    Steam's unfree bits the way plain `steam-run` does).
  - Blender itself, run headlessly (`--background --factory-startup`, so it's
    hermetic against personal addon config) via new `tests/
    blender_import_check.py`, which imports the glb and prints bone/mesh/
    animation counts. The C++ test cross-checks those against tinygltf's own
    reading of the *same* file rather than hardcoded fixture numbers, so
    agreement between two independent glTF implementations is the actual
    signal, not a tautology.
  Verified against real data: `bloodelffemale.m2` passes the validator with 0
  errors/0 warnings; Blender's importer agrees with tinygltf on 119 bones and
  258 animation clips. `tests/run_husk.hpp`'s subprocess-runner was generalized
  (`runCommand`, with `runHusk` now a thin wrapper) to spawn `gltf_validator`/
  `blender`, not just the `husk` binary. README.md's roadmap stage 7 and
  Testing section were updated to match (was previously the one open roadmap
  stage; all 8 are done now).
- **Next step**: nothing in flight. The remaining known gaps are exactly the
  ones `TODO_correctness.md` already tracks (`AFSB` reverse-engineering,
  `M2Particle` dereferencing, `.bone` LOD-context integration) plus optional
  scope expansion (WMO/M3, or Blender-side tooling — a script/addon reading
  the geoset/multi-texture-layer `extras` `FAILURES2.md` #1/#6 added) — none
  of that is a husk-parsing task.
- **Hazards**: `ctest` (unlike running `./build/husk-tests` directly) executes
  from `build/`, not repo root — the `HUSK_TEST_M2`/`HUSK_TEST_SKIN` env vars
  need absolute paths when driving the real-data tests through `ctest`
  specifically, or every one of them fails on a bad relative path, not a real
  regression. No other known-stale doc content as of this session — checked
  README.md's roadmap/Testing text against the actual current source and
  they agree.
