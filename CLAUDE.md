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
  (single-tier or `all`), `husk export --bones-dir` (real `.bone` correction
  data attached as inert `bone_correction_sets` glTF skin `extras`, never
  applied to the render — see Resume), `husk dump-chunks` (JSON dump of chunks
  with no glTF equivalent, or `.bone` files directly). `blp/`'s `husk-blp` (BLP2 → PNG:
  palettized/DXT1/DXT5/BGRA). `husk export`'s CLI grammar is CLI11-based named
  flags (`--input`/`--output` positional-fallback, everything else named,
  `--skin`/`--textures`/`--skin-dir`/`--anim`/`--skel`/`--bones-dir`
  three-or-four-state — see `DESIGN.md`'s "CLI argument grammar for
  `export`"), with generated bash/zsh completions in `completions/`. See
  `README.md`'s format-support matrix and roadmap for the exact per-feature
  state — that table is the source of truth, not this file.
- **Target**: a real Blender import path for modern (Legion+ chunked) M2 — see
  `DESIGN.md`'s Goal section. All 8 roadmap stages are now done, including stage 7
  (output hardening: real exports now run through the Khronos glTF-Validator *and*
  headless Blender itself, `tests/test_conformance.cpp` — see Resume). Remaining
  work is either scope expansion (WMO/M3, not started, by design) or the structural
  gaps `TODO_correctness.md` already tracks (`AFSB`, `M2Particle`, plus `.bone`
  correction *selection* — the extras-export half is done, see Resume; picking
  which slot applies is blocked on client-side DB2 data husk doesn't have, not
  on more investigation) — nothing currently in flight.
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
- `--textures`/`--skin-dir`/`--anim` directories — user-populated,
  FileDataID-named, local filesystem only. **Never CASC** — husk has no
  CASC/listfile access and never will, by design (see `DESIGN.md`'s Non-goals).
- `.blp` texture files (separate `blp/` Python tool) — container hand-rolled, block
  decode delegated to Pillow via a synthetic DDS wrapper.
- No network access anywhere in this tool. No user input beyond CLI argv (parsed in
  `cmd_info.cpp`/`cmd_export.cpp`/`cmd_dump.cpp`, no interactive prompts).

Every boundary above is read via explicit bounds-checked parsing at named offsets,
throwing a descriptive `ParseError`/`std::runtime_error` on anything foreign data
claims that doesn't fit — never a silent misread (see git history for the specific
bugs found and fixed under this discipline), and `WIKI_FINDINGS.md` for every
real-file-driven spec correction found along the way.

## Resume

- **Last state**: `TODO_correctness.md` #6's extras-export half is now
  implemented — real `.bone` correction data attaches to `husk export`'s
  glTF output as inert `extras`, never applied to the render. New
  `husk export --bones-dir <dir>` flag (three-state, same shape as
  `--textures`/`--skin-dir`): resolves every FileDataID the model's/
  `.skel`'s `BFID` array declares to a real `<dir>/<FileDataID>.bone` file
  (silently skipping any that don't resolve, same policy `--textures`
  already uses for a missing PNG), parses each with the existing
  `husk::bone::parse`, and attaches every resolved slot as a
  `bone_correction_sets` key on the glTF **skin**'s own `extras` — one
  entry per `.bone` file, each a `(file_data_id, [{joint, matrix}, ...])`
  record. Deliberately *not* applied to the bind pose or any animation:
  which slot is "correct" for a given character is external,
  client-side customization-choice data husk still doesn't have (this
  session's own prior investigation, see Previous state below, and
  `WIKI_FINDINGS.md` §4/`TODO_correctness.md` #6) — same "tag it, don't
  guess at semantics" treatment as geoset selection/`textureTransform`.
  Went through a full plan-mode design pass before implementation, given
  the CLI-grammar/parsing-pipeline/glTF-schema surface touched; the
  approved plan is what got built, no deviations. All verified: clean
  rebuild, full 335-case `husk-tests` suite green via both
  `./build/husk-tests` and `ctest` (up from 324), plus a real
  `bloodelffemale_hd.m2`/`.skel` export re-checked by hand (20/20
  `.bone` slots attached, round-tripped through `gltf_validator` — the
  1.7M pre-existing `JOINTS_0` duplicate-value errors on that specific
  fixture are unrelated, confirmed identical with `--bones-dir none`,
  not a regression from this work).
  - **`src/skel.hpp`/`skel.cpp`**: new `findBoneFileDataIds` reads
    `.skel`'s own `BFID` chunk (previously explicitly out of scope, per
    this file's own doc comment) — same flat-`uint32`-array shape as an
    M2's own `BFID`, duplicated locally rather than shared from `m2.cpp`'s
    anonymous-namespace helper, matching this file's existing
    `findAnimFileIds` precedent (same "small parser helper, one per
    translation unit" pattern already established here, not a new one).
  - **`src/gltf.hpp`/`gltf.cpp`**: new `gltf::Skeleton::CorrectionSet`
    (`fileDataId` + `vector<{joint, matrix[16]}>`) as a field on
    `Skeleton` alongside `joints`; `writeGlbMulti` validates every
    correction's `joint` is in range (same `Error` shape as the existing
    parent-range check) and serializes non-empty `correctionSets` into
    `skin.extras["bone_correction_sets"]`, nested `tinygltf::Value`
    construction following the exact existing pattern
    `additional_textures`/`texture_transform` material extras already use.
  - **`src/commands.hpp`/`src/cmd_export.cpp`**: `ExportOptions::
    bonesDirArg` + `--bones-dir` registered in `addExportOptions` (so the
    completion generator picks it up automatically); resolution/
    attachment logic sits right after the existing skeleton-building
    block, keyed off the same `bonesAreInline`/`haveSkel` branch already
    used for choosing inline-vs-`.skel` bones/animation elsewhere in this
    function. Prints a summary note (`attached N/M '.bone' correction
    set(s)...`) only when `N > 0` — silent otherwise, matching
    `--textures`'s existing "quiet when nothing applies" behavior.
  - **`src/main.cpp`**: the hand-maintained bash/zsh completion-generator
    tables (`bashValueCompletion`/`zshValueAction`/`zshFlagLabel` — these
    are *not* derived from CLI11 introspection alone, a real gap this
    session had to discover by testing the regenerated completion
    function directly rather than assuming the flag-table change alone
    was sufficient) needed `--bones-dir` added explicitly, same
    `none`-plus-directory treatment as `--textures`/`--skin-dir`.
    `completions/husk.bash`/`.zsh` regenerated and functionally verified
    the same way prior sessions did (sourcing the script, driving
    `_husk_completions` with scripted `COMP_WORDS`/`COMP_CWORD` — confirmed
    `--bones-dir` offers `none` + real directories, not plain filenames).
  - **Tests**: `tests/test_skel.cpp` (`findBoneFileDataIds`: found/absent/
    malformed-length-throws, mirroring `findAnimFileIds`'s existing
    cases), `tests/test_gltf.cpp` (3 new cases: `correctionSets` round-trip
    as skin extras, no-`correctionSets`-means-no-key, out-of-range joint
    throws — same style as the existing billboard/geoset/textureTransform
    extras tests), `tests/test_cli.cpp` (4 new cases: explicit
    `--bones-dir` attaches + notes, `--bones-dir none` suppresses, unset
    defaults to the model's own directory, an out-of-range `.bone`
    correction fails the export naming the file/bone index — new local
    `buildBoneFile`/`boneCorrectionSkel` fixture helpers), `tests/
    test_data_paths.hpp`+`test_integration.cpp` (new `autoBonesDir`
    mirroring `autoSkinDir`: reads real `BFID` entries out of
    `bloodelffemale_hd.skel` and copies a few of this repo's own real
    `.bone` fixtures under those FileDataIDs — comment notes the NN→
    `BFID[NN]` positional assignment is arbitrary for test purposes, not a
    claimed real mapping; one new `doctest::skip`-gated real-data
    `TEST_CASE` checks the produced `.glb`'s skin extras directly via
    tinygltf). `test_main.cpp`'s startup banner gained a
    `HUSK_TEST_BONES_DIR` line.
  - **Environment note carried over, reconfirmed**: bare `python`/`python3`
    is still guarded off even under `direnv exec .`/`nix develop ./nix -c`
    — `direnv exec . uv run --no-project python3 <script>` is the
    sanctioned path for ad hoc Python in this repo (used again this
    session for the real-file `--bones-dir` smoke test), `nu` remains fine
    for direct byte-level work with no `uv` involved at all.
  - **Docs updated**: `README.md` (`.bone` corrections paragraph + flag
    table row + defaults/`none` lists + sidecar-resolution format-matrix
    row), `DESIGN.md` (CLI grammar table + three-state section + a new
    Key design decisions bullet matching the geoset/texture-transform
    precedent + a Non-goals clarifying sentence: an out-of-band
    CASC/DB2-scraping build tool is fine, husk itself talking to CASC at
    runtime never is), `TODO_correctness.md` #6 (extras-export marked
    done, remaining gap reframed as "external lookup, not more
    investigation"), `M2_COMPLETENESS.md` (`.bone` row + the sidecar
    FileDataID-resolution rows), `WIKI_FINDINGS.md` §4 (added the
    previously chat-only weapon-type/armor-type ruling-out finding — the
    corrected bones cluster on Head/Jaw, not hand/wrist — since
    `TODO_correctness.md` #6 now cites it as an established fact and it
    needs real receipts backing it, not just a claim).
- **Previous state**: `TODO_correctness.md` #6 (which `.bone` file — of a
  model's several, per its `BFID` array — applies to which LOD/context)
  got real forward progress this session, though not a full close. Pure
  investigation, no reverse-engineering: decoded and cross-compared all 20
  real `bloodelffemale_hd_00.bone`–`_19.bone` files (plus their 20
  `_sdr_00`–`_sdr_19` siblings) against `bloodelffemale_hd.skel`'s `BFID`
  chunk (manually chunk-parsed with `nu`/`uv run python3` scratch scripts,
  since husk itself doesn't parse `.skel`'s `BFID` — only `m2.cpp`'s BFID
  reading is wired up, and that model has 0 inline bones/external skel
  anyway). Verdict: **the LOD/render-distance hypothesis is now ruled out
  by real data**, not just left undetermined. Three independent pieces of
  evidence: (1) the count itself doesn't fit — 20 `.bone` slots vs. this
  model's `lod_count: 7`, no clean relationship; (2) the 20 files collapse
  into only 5 distinct bone-index sets, one of them (33 bones) repeated
  *verbatim* across 10 of the 20 files — a LOD ladder sheds bone
  involvement as detail drops, it doesn't reuse the identical set 10
  times; (3) where a bone is corrected in multiple files, the correction
  is a pure magnitude scale along one of exactly two fixed 3D directions
  (checked via bone 64's translation row across all 20 `_hd_*.bone`
  files — 14 share one direction at 9 distinct magnitudes, 6 share another
  at 3), the signature of a small number of shape variants reused across
  many selectable slots (e.g. some choices being texture/color-only and
  intentionally sharing bone data), not 20 independently-authored LOD
  tiers. What *does* select a slot most plausibly lives in client-side DB2
  data (something `ChrCustomizationBoneSet`-shaped, named from memory, not
  confirmed against a real DB2 dump) — squarely CASC/DBC-adjacent, which
  `DESIGN.md`'s non-goals already rule out for this project, same reason
  the geoset-default-selection question stays open. Documented in
  `WIKI_FINDINGS.md` §4's new follow-up (the receipts: exact magnitudes,
  bone-set overlaps, file-by-file breakdown), `TODO_correctness.md` #6
  (updated to state what kind of open question remains), `README.md`'s
  `.bone` section, and `M2_COMPLETENESS.md`'s sidecar table row. No code
  changed — `src/bone.hpp`/`.cpp` and `husk dump-chunks` are unaffected;
  this was strictly closing the "what do we even know" gap before any
  wiring-in could be attempted. Scratch analysis scripts used for this
  aren't checked in (ad hoc `nu`/Python in the session's scratchpad, not
  project code).
  - **Environment note for future sessions**: bare `python`/`python3` is
    guarded off (even under `direnv exec .`/`nix develop ./nix -c`) with a
    message pointing at "the flake" — this project's actual sanctioned
    Python entry point is `uv run --no-project python3 <script>` (or
    `cd blp/ && uv sync` for the real `husk-blp` project), matching
    `nix/flake.nix`'s comment that Python dependency management goes
    through `uv`, not a bare interpreter. `nu` (Nushell, Luna's globally
    preferred scripting language) is available system-wide and worked
    fine for direct byte-level chunk parsing without needing `uv` at all.
- **Earlier state**: `export`'s CLI grammar migrated from a hand-rolled,
  position-dependent positional parser to named CLI11 flags, per
  `DESIGN_CHANGES.md`'s spec (that file is now deleted — folded back into
  `DESIGN.md`'s "CLI argument grammar for `export`" section and `README.md`'s
  `export` Usage subsection, per its own stated scratch-doc lifecycle).
  Breaking change to every existing `husk export` invocation's argument
  order, done deliberately in one pass rather than staged. All verified:
  clean rebuild, full 324-case `husk-tests` suite green via both
  `./build/husk-tests` and `ctest` (up from 310), plus real
  `bloodelffemale.m2` exports re-checked by hand for several flag states.
  - **CLI11 added as a new dependency** (`pkgs.cli11` in `nix/flake.nix`,
    `find_package(CLI11 CONFIG REQUIRED)` in `CMakeLists.txt`, linked
    `PUBLIC` on `husk-lib` so `main.cpp` gets it transitively too) —
    explicit sign-off obtained before landing, per this project's
    package-approval rule.
  - **`src/commands.hpp`/`src/cmd_export.cpp`**: new `ExportOptions` struct
    + `addExportOptions(CLI::App&, ExportOptions&)` is the one place
    export's flag surface is declared, shared by `exportGlb`'s real parse
    and the completion generator below (single source of truth, not two
    hand-maintained copies). `-i,--input`/`-o,--output` keep a positional
    fallback (the universal `tool in out` muscle memory); every other flag
    is named-only. `--skin`'s literal `none` is rejected at CLI11 parse
    time via an `Option::check` validator, with a message naming the real
    expected values. `resolveSkin` (new) folds what used to be two
    independent code paths (an omitted `.skin` positional vs. the literal
    word `auto`) into `--skin`'s single `auto` default: SFID-declared
    FileDataID match first (only committed to if that file actually
    exists on disk — a real filesystem peek, not just path construction),
    same-basename numbered scan as the fallback. `--anim` got a real fourth
    state (`none`, gating the entire per-sequence-plus-global-sequence
    animation-building call, not just external resolution) alongside
    `auto`/`inline`/an explicit directory. `--textures`/`--skin-dir`/
    `--skel` all got the three-state (`unset`→auto-default,
    explicit-value→override, explicit-`none`→deliberate skip) treatment
    `~/docs/CLI.md` §2.11 calls for. One real implementation gotcha found
    and fixed along the way, now documented in `DESIGN.md`: CLI11's
    `App::parse(std::vector<std::string>&)` consumes tokens from the
    *back* of the vector (mirrors how its `(argc, argv)` sibling reverses
    argv before the same internal call) — passing `args` in forward order
    silently bound every flag to the wrong neighboring token instead of
    erroring; `exportGlb` now reverses it first.
  - **Shell completion** (`src/main.cpp`, new): a hidden
    `--print-completion=<bash|zsh>` flag builds a throwaway `CLI::App` tree
    (via the same `addExportOptions` for `export`; `info`/`dump-chunks`
    hand-registered since neither is CLI11-based) and walks it with CLI11's
    own introspection API (never `.parse(...)`) to generate real, working
    bash/zsh completion scripts — captured into checked-in
    `completions/husk.bash`/`.zsh`. Verified functionally, not just
    syntax-checked: sourcing `completions/husk.bash` and driving its
    completion function directly with a scripted `COMP_WORDS`/`COMP_CWORD`
    confirms real flag names and per-flag value completion (`auto` for
    `--skin`; `auto inline none` for `--anim`; `none` + real directories
    for `--textures`/`--skin-dir`; `all` for `--lod`). `--print-completion`
    itself is absent from `husk --help`'s output (no human reader) but
    functions when invoked directly; unrecognized shell names fail cleanly
    naming what's actually supported.
  - **`tests/test_cli.cpp`**: every `export`-grammar test rewritten to the
    named-flag form (86 → 100 cases), plus 14 new cases proving the
    checklist behaviors directly rather than by inference — SFID-beats-
    same-basename resolution ordering, `--skin-dir none`'s fallback-only
    behavior, `--skin none`'s parse-time rejection, the `--lod`+
    `--skin-dir none` conflict, `--anim inline`'s external-suppression
    (same fixture as an `--anim <dir>` test, only the flag value differs,
    proving suppression rather than "happened to find nothing"), `--anim
    none`'s zero-clips-but-bones-still-present case, `--skel none`, and
    `-i`/`-o` positional/flag-form agreement including out-of-order flags.
  - **`tests/test_integration.cpp`/`tests/test_conformance.cpp`**: their
    `runHusk("export ...")` calls still used the old positional grammar
    (a real gap the test-rewrite pass surfaced, since those two files were
    out of that pass's scope) — converted to named flags in a follow-up
    fix, confirmed green.
  Real discrepancy worth flagging for future work: `resolveSkin`'s
  "couldn't resolve" error messages name only the *directory* searched,
  never the specific FileDataID/candidate filename the old positional
  code's `readFileBytes` "couldn't open '<path>'" message used to
  surface — a real (if minor) loss of specificity, noted in
  `tests/test_cli.cpp`'s comments rather than silently adjusted around.
- **Next step**: nothing in flight. `TODO_correctness.md` #6 is as closed
  as husk itself can close it — the remaining piece (a client-side DB2
  slot-selection lookup) is out of reach by design, not a to-do; a future
  session could revisit *applying* a specific slot (e.g. real, scrubable
  glTF animation clips per slot, mirroring `global_seq_<n>`) if that
  external mapping ever becomes reachable some other way, but that's new
  scope, not a continuation of this session's work. Remaining known gaps
  are exactly the ones `TODO_correctness.md` tracks (`AFSB`
  reverse-engineering, `M2Particle` dereferencing, two awareness-only/
  blocked-on-real-data footnotes), plus optional scope expansion (WMO/M3,
  or Blender-side tooling for the geoset/multi-texture-layer `extras`).
  One real, minor loose end from an earlier session worth picking up if
  `cmd_export.cpp` is touched again: `resolveSkin`'s failure messages could
  name the specific candidate path/FileDataID they tried, not just the
  directory — small, not urgent.
- **Hazards**: none new this session beyond the pre-existing
  `HUSK_TEST_DATA_DIR`/relative-path one below. `completions/husk.bash`/
  `.zsh` are generated, checked-in artifacts (`husk --print-completion=
  <bash|zsh>`) — if `addExportOptions`'s flag table changes, regenerate
  both rather than hand-editing; **the completion generator's per-flag
  value-taxonomy tables in `src/main.cpp` (`bashValueCompletion`/
  `zshValueAction`/`zshFlagLabel`) are hand-maintained, separate from
  `addExportOptions`, and don't pick up a new flag's `none`/directory
  semantics automatically** — a new flag falls through to plain-filename
  completion until it's added to those tables explicitly (found the hard
  way this session; verify by actually sourcing the regenerated script and
  driving `_husk_completions`/`_husk`, not just diffing that a new flag
  name appears). `HUSK_TEST_DATA_DIR` (`CMakeLists.txt`) is baked absolute
  at configure time, so the default `test_data/`-fallback fixtures are
  immune to the old `ctest`-runs-from-`build/` relative-path trap — but if
  you override any `HUSK_TEST_*` env var by hand for `ctest` specifically
  (not `./build/husk-tests` directly), it still needs to be absolute, or
  that one test fails on a bad relative path, not a real regression.
