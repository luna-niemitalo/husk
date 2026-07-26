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

- **Last state**: every remaining actionable finding from `FINDINGS.md`
  (the external review below) got fixed in a second follow-up pass, same
  overall session — the punch list's items 2-6, on top of items 1/5
  the first pass already closed. All verified: clean rebuild, 310-case
  `husk-tests` suite (up from 267) green via both `./build/husk-tests`
  and `ctest`, plus a real `bloodelffemale.m2` export re-checked by hand.
  - **`buildMaterialsAndPrimitives` adversarial tests** (§4.2): 9 new
    `test_cli.cpp` cases, one per throw site in the batch→submesh→
    material→color/weight/texture/textureCoord chain, via a reusable
    `BatchFields`/`oneBatchSkin`/`materialsFixtureM2` fixture trio.
  - **`M2TextureTransform`** (§3.1): `m2::parseTextureTransforms` (new,
    mirrors `parseColors`'s constant-vs-animated split) + `.skin`'s
    `Batch.textureTransformComboIndex` (offset `0x16`, previously
    entirely unread) + `husk info` counts + `husk export` resolving a
    batch's reference into `gltf::Material::textureTransform` — real,
    inert `extras`, deliberately **not** a `KHR_texture_transform`
    applied to the render (the extension is itself static/non-animatable,
    and WoW's rotation pivots around the texture center vs. the
    extension's own origin — a correction this project's own real-file-
    verification discipline says shouldn't ship unchecked, and no real
    transform-carrying file was available; see `DESIGN.md`'s new entry).
    `bloodelffemale.m2` has 0 real `texture_transforms` (a character-
    model thing, not really used there) — confirmed safe on the
    zero-count path; the resolution logic itself is synthetic-tested.
  - **Global-sequence material-track asymmetry** (§3.2): `m2::Color`/
    `m2::TextureWeight` gained `colorAnimated`/`alphaAnimated`/
    `weightAnimated` flags (shared `trackHasAnimatedData` helper); `husk
    export` now prints a note instead of silently applying the static
    default. Verified against real data: exporting `bloodelffemale.m2`
    now reports "3 batch(es) whose color tint ... is animated" — a real,
    previously-silent case (plausibly blood elves' eye-glow), not
    hypothetical. Deliberately not extended to a full extras-based
    keyframe dump (see `DESIGN.md` for the scope line and why).
  - **Collision data** (§3.3): `cmd_info.cpp` now prints
    `collision_box`/`collision_sphere_radius` and counts
    `collision_indices`/`collision_face_normals`, not just
    `collision_positions`. Real data: 8/36/12 respectively, a real small
    hit-test mesh.
  - **`--version`/`-V`** (§2.3): `CMakeLists.txt` resolves `git describe
    --always --dirty` once at configure time into `HUSK_VERSION`.
  - **Remaining CLI argv edge cases** (§4.3): 8 more `test_cli.cpp` cases
    (zero-arg invocations for all three subcommands, `export`'s four
    "flag with no value" branches, `export`'s too-many-positionals case,
    `info`/`dump-chunks`'s extra-argument case).
  - **`cmd_dump.cpp` per-chunk coverage** (§4.4): 8 new `test_dump.cpp`
    round-trip tests (TXAC/EXPT/PADC/PSBC/PEDC/EDGF/DBOC/WFV3) — `WFV3`
    (~20 hand-transcribed fields, the highest-risk one) checked via exact
    `"key": value` substrings per field, not just "the number appears
    somewhere". `GPID`/`PGD1` deliberately left untested — they call the
    identical function pointer as `RPID`/`PABC`, so a second test would
    exercise the same code, not new coverage.
  `FINDINGS.md` itself was updated throughout: every fixed section marked
  `[Fixed]` with the original text kept as "originally found as follows"
  for the record, and the punch list resolved. Only §3.4 (five unused
  lookup-table arrays, awareness-only) and §3.5 (a self-flagged-in-code,
  needs-real-data caveat) remain genuinely open, plus §5's usability
  observations (not framed as defects).
- **Previous state**: an external read-only review (`FINDINGS.md`, new
  that session) audited the project against `~/docs/READABILITY.md`/
  `CLI.md`, M2 completeness, and test coverage — most findings were still
  open at that point, but the two highest-leverage ones were fixed
  immediately, both verified live:
  - **Silent test skips.** 12 of 260 `husk-tests` cases (in
    `test_integration.cpp`/`test_conformance.cpp`) used to `MESSAGE(...)`
    + early `return` when a `HUSK_TEST_*` env var or optional tool
    (`gltf_validator`/`blender`) was missing — doctest counted that as
    "passed" with 0 assertions, so a bare `./build/husk-tests` run always
    reported "0 skipped" even when roadmap-stage-7's entire conformance
    tier silently didn't run. Fixed two ways: every gated `TEST_CASE` is
    now `* doctest::skip(...)`, which doctest reports as a real, distinct
    "skipped" count; and a new `tests/test_data_paths.hpp` auto-detects
    real fixtures already sitting in this repo's own `test_data/` (env
    var still overrides) instead of requiring 8 hand-set env vars —
    `HUSK_TEST_SKIN_DIR` is even *constructed* on the fly from the SFID
    entry 0 read out of the resolved M2's own header. Net effect,
    verified: a bare `./build/husk-tests` run now actually exercises 259
    of 260 cases for real (368,515 assertions, up from 1,113) instead of
    248; only `HUSK_TEST_TEXTURES_DIR` (no committed `husk-blp`-converted
    PNGs) still needs to be set by hand, and now visibly skips instead of
    silently passing when it isn't. `tests/test_main.cpp` gained a startup
    banner printing every fixture's resolution up front.
  - **`--help`/`-h` bug.** Only `main.cpp` special-cased it, before a
    subcommand name was even read — `husk export --help` fell through to
    treating `--help` as a literal model path (`args[0]` is always the
    first positional in `exportGlb`), producing "couldn't open '--help'"
    instead of the good usage text `cmd_export.cpp` already had. Same bug
    in `info`/`dump-chunks`. Fixed via a shared `commands::isHelpFlag`
    (`src/commands.hpp`) checked before real argument parsing in all
    three, plus 8 new regression tests in `test_cli.cpp`. `main.cpp`'s own
    top-level usage text was also resynced (it undersold `export`'s real
    optional args/flags and `dump-chunks`' `.bone` support).
  260 → 267 total test cases (8 new `--help` regressions, minus the 1
  test_conformance.cpp case that split into a real-vs-`doctest::skip(true)`
  pair per missing tool — see test_conformance.cpp). `README.md`'s Testing
  section and `DESIGN.md`'s Testing architecture section were updated to
  match; the `ctest`-runs-from-`build/`-needs-absolute-paths hazard below
  is now moot for the *default* fixtures (baked-absolute
  `HUSK_TEST_DATA_DIR`) but still applies if you override one by hand with
  a relative path.
- **Earlier state**: roadmap stage 7 (output hardening) closed out that session —
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
- **Next step**: nothing in flight. `FINDINGS.md`'s punch list is now
  fully closed except two items that aren't really action items: §3.4
  (five lookup-table arrays parsed but never referenced — awareness-only,
  nothing downstream needs them yet) and §3.5 (multi-texture-layer index
  arithmetic self-flagged as unverified against a real multi-layer file —
  blocked on one showing up in `test_data/`, same "verify against real
  data before trusting it" bar every other claim in this project met).
  Otherwise the remaining known gaps are exactly the ones
  `TODO_correctness.md` already tracks (`AFSB` reverse-engineering,
  `M2Particle` dereferencing, `.bone` LOD-context integration) plus optional
  scope expansion (WMO/M3, or Blender-side tooling — a script/addon reading
  the geoset/multi-texture-layer `extras` `FAILURES2.md` #1/#6 added) — none
  of that is a husk-parsing task.
- **Hazards**: `HUSK_TEST_DATA_DIR` (new, `CMakeLists.txt`) is baked
  absolute at configure time, so the default `test_data/`-fallback
  fixtures are immune to the old `ctest`-runs-from-`build/` relative-path
  trap — but if you override any `HUSK_TEST_*` env var by hand for `ctest`
  specifically (not `./build/husk-tests` directly), it still needs to be
  absolute, or that one test fails on a bad relative path, not a real
  regression. No other known-stale doc content as of this session —
  checked README.md's roadmap/Testing text against the actual current
  source and they agree.
