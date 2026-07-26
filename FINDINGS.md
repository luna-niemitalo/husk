# FINDINGS.md — external evaluation

An outside-in review of husk against Luna's own general-purpose standards
(`~/docs/READABILITY.md`, `~/docs/CLI.md`), plus a fresh completeness/
correctness pass on the M2 read pipeline and a test-coverage audit. Written
read-only, nothing in the repo was changed to produce this. Where a finding
overlaps something already tracked in `TODO_correctness.md`/`DESIGN.md`,
it's noted as such rather than repeated at length — the point of this
document is what those don't already say.

Scope note: this is a project self-review against project-adjacent
standards, not a security review and not a wowdev.wiki spec audit (that
discipline already exists in `WIKI_FINDINGS.md`/`FAILURES.md`/`FAILURES2.md`
and is, on the evidence below, unusually rigorous).

**Post-review update**: every finding in this document except §3.4/§3.5
(a footnote-level "worth knowing" item and a self-flagged-in-code caveat,
neither an action item) and §5's usability observations (not framed as
defects) was fixed across two follow-up passes in the same overall
session — see each section's own `[Fixed]` marker for what changed,
`CLAUDE.md`'s Resume for the full list, and §6's punch list for the
final status of every item. Left in place, marked `[Fixed]`, rather than
removed — the original finding text is kept as "originally found as
follows" for the record, since this document is as much a record of what
was checked as of what's currently true.

---

## 1. Against `READABILITY.md`

This project already **is** the reference example the doc's own preamble
gestures at ("derived from observed conventions... these are not
aspirations"). Concretely, spot-checked against the axes in §3:

- **Current vs target (3.10)**: exemplary. `README.md`'s legend
  (✅/📖/🚧/⬜/⬛/❔) and format matrix, `DESIGN.md`'s "Open work", and
  `CLAUDE.md`'s `## Status`/`## Resume` sections all separately and
  consistently mark what exists vs what doesn't — checked several claims
  (global-sequence clips, `KHR_materials_unlit`, `.bone` parsing) against
  the actual code and they all matched.
- **Documentation layer system (§5)**: followed to the letter — `DESIGN.md`
  (why), `README.md` (what/how + status), `WIKI_FINDINGS.md` (spec
  corrections), `TODO_correctness.md` (open punch list), `FAILURES.md`/
  `FAILURES2.md` (fixed-bug history with severity tags), why-comments inline
  pointing back at these. No fact was found duplicated across layers in the
  files sampled.
- **Boundary hardness / error philosophy (3.4/3.5)**: `cmd_export.cpp`'s
  `buildMaterialsAndPrimitives` (line 652 on) is a clean worked example —
  six distinct out-of-range checks, each throwing with the *actual* index,
  the *actual* bound, and which array — expected-and-actual done right at
  every single one. Interior code (e.g. `gltf.cpp`'s builders) stays clean
  and doesn't re-check what the boundary already validated.
- **Idiom stability / hub-and-spoke (3.2/3.12)**: `.skel` sidecars reusing
  `m2::parseBones`/`parseSequences` outright rather than a parallel
  implementation (documented as a hard constraint in `DESIGN.md`) is exactly
  the pattern the doc asks for, and it's still true in the code.
- **One soft nit**: `cmd_export.cpp` is 1393 lines and `m2.cpp` is 1204.
  Both still pass the "nameable purpose" test (3.11) — one file per pipeline
  stage, no confetti — and internally they're well-decomposed into ~20
  focused free functions each, so this reads as "the domain is just this
  big" rather than a missing seam. Not a finding, just noted since it's the
  one place the doc's size guidance could theoretically bite later.

No readability regressions found. This section is short because there
wasn't much to say — the codebase already conforms.

---

## 2. Against `CLI.md`

### 2.1 [Fixed] Concrete bug: subcommand `--help`/`-h` doesn't work

**Fixed**: a shared `husk::commands::isHelpFlag` (`src/commands.hpp`) is
now checked in `info`/`exportGlb`/`dumpChunks` before any real argument
parsing, printing the existing `printUsage()` text to stdout and exiting
0 — same fix shape as `main.cpp`'s own top-level check, just applied one
level down too. 8 new regression tests in `tests/test_cli.cpp` cover all
three subcommands, both `--help` and `-h`, and `--help` appearing after a
real positional. `main.cpp`'s top-level usage text (§2.2 below) was
resynced in the same pass. Originally reproduced against the built binary
as follows (kept for the record):

```
$ ./build/husk export --help
husk: note: no output path given -- writing to '--help.glb'
husk: export failed: couldn't open '--help' for reading

$ ./build/husk info --help
husk: couldn't read '--help': couldn't open '--help' for reading

$ ./build/husk dump-chunks --help
husk: dump-chunks failed: couldn't open '--help' for reading
```

Every subcommand has a genuinely excellent, detailed `printUsage()` (see
`husk export` with zero args — several paragraphs, covers every flag and
default) — but it's only reached via an *error* path (missing/invalid
positional), never via an explicit `--help`/`-h` check. Only `main.cpp`
(the top-level dispatcher) special-cases `--help`/`-h`, and only before a
subcommand name is even read. This directly hits CLI.md's failure-taxonomy
§1 ("no shared grammar" — `--help` means "show usage" at the top level and
"open a file called --help" one level down) and breaks rung 5 of the
null-then-fallback chain (§3): a user who's just learned `husk export`
exists and types `husk export --help` — an extremely natural next move —
gets a misleading file-not-found error instead of the usage text that
already exists and is good. **Fix is cheap**: each of `exportGlb`/`info`/
`dumpChunks` needs the same one-line check `main.cpp` already does, before
treating `args[0]` as a positional.

### 2.2 [Fixed] Top-level usage text is stale relative to subcommand reality

**Fixed** alongside §2.1: `main.cpp`'s summary now says `export <file.m2>
[args...]` / `dump-chunks <file.m2|.bone>` and points at each
subcommand's own `--help` (now real) for the full picture, rather than
repeating detail that drifts. Originally found as follows: `main.cpp`'s
`usage` string (lines 7-13) showed:

```
export <file.m2> <file.skin> <out.glb>     export a mesh (+ skin/animation) to glTF
```

as if all three are required positionals. In reality (per `cmd_export.cpp`'s
own `printUsage`) only `<file.m2>` is required; `.skin`/output/`.skel` are
all trailing-optional, `auto` + `--skin-dir` is a whole separate resolution
mode, and `--textures`/`--anim-dir`/`--lod` exist and aren't mentioned at
all. `dump-chunks <file.m2>` similarly doesn't mention it also accepts a
`.bone` file directly. This is the exact "aspiration/reality drift" pattern
`READABILITY.md` §3.10 warns about, just inverted — here the *simpler*,
now-stale text undersells what the tool grew into, rather than overselling
unbuilt work. Minor (the real usage text is one `--help` fall-through away
once §2.1 above is fixed), but worth a one-line sync.

### 2.3 [Fixed] No `--version` / `-V`

**Fixed**: `CMakeLists.txt` resolves `git describe --always --dirty` once
at configure time (never re-run live, matching `CLI.md` §2.9's "no inline
computation" rule) into `HUSK_VERSION`; `husk --version`/`-V` prints it.
Originally: not present at any level. Low priority for a locally-built dev
tool, but worth a line given the tool consumes wowdev.wiki spec state that
moves ("this format keeps growing new chunks," per `DESIGN.md`) — knowing
which build produced a given `.glb` matters for bug reports against a
moving target.

### 2.4 What's already excellent (checked, not just assumed)

- **§2.11 (input-derived defaults)** is close to a worked example of the
  doc's own ideal: `auto`/`none`/explicit-value as three real states,
  auto-resolution *announces* what it resolved to (`husk: note: resolved
  'auto' -> '...'`), and a failed auto-resolve degrades per-item rather than
  hard-failing the whole run — all verified directly against
  `cmd_export.cpp`'s `exportGlb`/`resolveAutoSkinPaths`.
- **§2.5/§2.9 (durable-fact defaults, no inline computation)**:
  `--textures`/`--skin-dir`/`--anim-dir` defaulting to the model's own
  directory (a real, confirmed-fixed usability gap per
  `TODO_correctness.md`'s CLI section) is exactly right per §2.5's test —
  "does this value ever change between calls" — for the common
  drop-everything-in-one-directory workflow the doc describes.
  `--lod`/`--skin-dir` only accepted alongside `auto` is enforced as a real
  usage error, not silently ignored.
- **§2.7 (errors state expected/actual)**: true everywhere sampled, not
  just at the M2-parsing boundary — CLI-level errors ("`--skin-dir` only
  does anything when the .skin path is 'auto'") are just as precise as the
  binary-format ones.

---

## 3. M2 implementation: completeness and correctness

`TODO_correctness.md`/`DESIGN.md`/the README's format matrix already track
a specific, honest list of known gaps (`AFSB` reverse-engineering,
`M2Particle`, `M2Camera` deprioritized on purpose, ribbon `M2Track`s
unread, `.phys` content unparsed). Those are not repeated here except where
new context changes their severity. What follows is genuinely new,
found by cross-referencing every `Header` array against what actually
consumes it downstream.

### 3.1 [Fixed] `M2TextureTransform` — parsed as a descriptor, never dereferenced, not even counted (highest-impact new finding)

**Fixed**, with a deliberate scope decision: `m2::parseTextureTransforms`
resolves each of the 3 tracks (translation/rotation/scaling) the same
constant-vs-animated way `parseColors` already does; `.skin`'s
`Batch.textureTransformComboIndex` (offset `0x16`, previously entirely
unread) is now parsed too; `husk info` counts `texture_transforms`; `husk
export` resolves a batch's reference and surfaces it as
`gltf::Material::textureTransform` — real, inert `extras`, **not** a real
`KHR_texture_transform` applied to the render. That last part was a
deliberate call, not a shortcut: `KHR_texture_transform` is itself static
(no animation-channel target), so it couldn't represent the animated case
either — and WoW's own rotation pivot (texture center, 0.5/0.5) differs
from the extension's (0,0), a pivot-correction detail this project's own
methodology says shouldn't ship without a real animated file to verify
against, which wasn't available. See `DESIGN.md`'s new "A batch's
`M2TextureTransform`..." entry for the full reasoning, and `m2::
TextureTransform`'s doc comment in `src/m2.hpp`. Verified against real
data: `bloodelffemale.m2` has 0 `texture_transforms` (character models
mostly don't use this — it's an environment/world-object feature), so
this was confirmed safe on the zero-count path; the resolution/extras
code itself is covered by synthetic tests (`test_m2.cpp`, `test_skin.cpp`,
`test_gltf.cpp`, `test_cli.cpp`) since no real transform-carrying file
was available to verify against directly.

Originally found as follows (kept for the record): `Header::textureTransforms` and `Header::textureTransformCombos`
(`src/m2.hpp:110,117`, offsets `0x060`/`0x098` in `src/m2.cpp:33,40`) are
read into `Array` descriptors and then never touched again anywhere in
`cmd_export.cpp`, `cmd_dump.cpp`, or `cmd_info.cpp` — confirmed by grep:
zero other matches for `textureTransform` in any `cmd_*.cpp`. This is the
mechanism behind WoW's scrolling/rotating/scaling UV animation — lava,
waterfalls, force-fields, portals, some cloak/aura effects. Unlike the
already-tracked gaps, this one isn't in the README's format matrix,
`DESIGN.md`, or `TODO_correctness.md` at all, and unlike (say) `M2Camera`
it has real, visible, base-layer impact: any model that leans on this for
its normal look exports with a texture that's *silently static* where the
real model animates continuously — closer in kind to the
already-fixed-and-documented `FAILURES2.md` bugs (real visual wrongness,
not just missing nice-to-have data) than to the deliberately-deprioritized
gaps. At minimum, `husk info` should surface the array counts the way every
other header array already is (one line, matching the existing pattern);
actually resolving the transform into glTF's `KHR_texture_transform`
extension (which maps onto this reasonably well — translation/rotation/
scale) is the real fix and is a comparable scope to the already-completed
`KHR_materials_unlit` translation.

### 3.2 [Fixed] Global-sequence tracks: fixed for bones, not for materials — a real asymmetry

**Fixed**, matching the multi-texture-layer/geoset precedent: `m2::Color`/
`m2::TextureWeight` gained `colorAnimated`/`alphaAnimated`/
`weightAnimated` flags (via a shared `trackHasAnimatedData` helper,
distinguishing "nullopt because empty" from "nullopt because animated and
dropped"), and `cmd_export.cpp` now prints a note
(`animatedTintOrFadeBatchCount`) whenever a batch hits this case, instead
of silently falling back to the static default with no indication.
Deliberately **not** extended to a full extras-based keyframe dump the way
the texture-transform fix (§3.1) got — that would need the same per-
sequence/global-sequence resolution `buildAnimations` already does for
bones, applied to a material property instead, real future work tracked
in `DESIGN.md` rather than attempted this pass. Verified against real
data: exporting `bloodelffemale.m2` now prints "3 batch(es) whose color
tint (M2Color) or transparency fade (M2TextureWeight) is animated" — a
real, previously-silent case (plausibly the eye-glow effect blood elves
are known for), not a hypothetical one; a synthetic regression test
(`test_cli.cpp`) also confirms a genuinely constant track still gets no
spurious note.

Originally found as follows (kept for the record): `FAILURES2.md` #7 fixed global-sequence-driven **bone** tracks
(`resolveVec3GlobalSequenceTrack`/`resolveQuatGlobalSequenceTrack`, wired
into `buildGlobalSequenceAnimations`, `cmd_export.cpp:369-399`). The
*identical* track shape on the **material** side —
`M2Color`/`M2TextureWeight`, i.e. `parseColors`/`parseTextureWeights`
(`src/m2.cpp:688,719`) — still only goes through
`constantTrackValueOffset`'s constant-only path, which returns `nullopt`
for any non-constant track, including a global-sequence one, with no
diagnostic. Concretely: a material-level glow/tint pulse (eye glow, enchant
glow, some armor/tabard effects — the material-track sibling of the
eye-glow/torch-flicker case `DESIGN.md` uses to motivate the bone-track
fix) is not even attempted and silently falls back to a static default with
no note printed (contrast with the multi-texture-layer and
multiple-geoset cases, which *do* print a note when they hit their own
version of "can't fully translate this"). This is worth fixing for
consistency with the bone-track precedent that already exists in the same
file, and at minimum deserves the same stderr note the other
can't-fully-translate cases already get.

### 3.3 [Fixed] Collision geometry: inconsistently surfaced relative to the README's own claim

**Fixed**: `cmd_info.cpp` now prints `collision_box`/`collision_sphere_radius`
(same scalar-print pattern as `bounding_box`/`bounding_sphere_radius`) and
counts `collision_indices`/`collision_face_normals` (same `printArray`
pattern as everything else), not just `collision_positions`. Verified
against real data: `bloodelffemale.m2` reports 8 `collision_positions`, 36
`collision_indices` (12 triangles), 12 `collision_face_normals` — a real,
small hit-test mesh, not zeros. Still just counts/scalars, not the mesh's
actual content (no `dump-chunks`-style dump of it) — that remains open,
same depth as every other still-🚧 row in the format matrix, now noted
there explicitly rather than left implicit.

Originally found as follows (kept for the record): `Header::collisionBox`/`collisionSphereRadius`/`collisionIndices`/
`collisionFaceNormals` are all parsed (`src/m2.cpp:179-183`), but
`cmd_info.cpp` only prints `collision_positions` (`cmd_info.cpp:245`) — not
`collision_box`, `collision_sphere_radius`, `collision_indices`, or
`collision_face_normals`, even as counts. The README's format matrix row
("🚧 `bounding_box`/`collision_box` fields read") reads as if collision
data gets the same treatment as bounding-box data; it doesn't — the box/
radius scalars aren't printed at all, and two of the three collision arrays
aren't even counted. Low visual-impact (collision mesh isn't rendered by
export, by design — glTF has no native collision-mesh slot either), but a
real "parsed then dropped" case and a one-line `cmd_info.cpp` fix (same
`printArray`/scalar-print pattern already used for everything else in that
function) to make the tool's own introspection honest about what it holds
in memory. Good `dump-chunks`-style candidate if anyone ever wants the
actual hit-test mesh.

### 3.4 Five lookup-table arrays parsed, never referenced: `boneLookup`, `attachmentLookup`, `cameraLookup`, `textureLookup`, `sequenceLookup`

All five (`src/m2.hpp:102-104,111,135,139`) are read into descriptors and
never dereferenced or counted anywhere downstream. Lowest priority of the
findings here — these are indirection/name-lookup tables (key-bone role
lookup, replaceable-texture lookup, name-lookup for cameras/attachments),
not required for the mesh/skin/material/animation pipeline that's already
implemented, and husk's own documented design choice (full per-vertex
global joint indices instead of hardware bone-limit batching) already makes
the closely-related `boneCombos` moot by intent, not oversight. Flagging
only because it's a real instance of the same "parsed, dropped" pattern as
3.3, in case any of them become relevant to future work (e.g.
`attachmentLookup` would matter if attachment-point *naming* — not just
raw id/bone/position, which `husk info` already prints — ever gets added).

### 3.5 Self-flagged, lower-priority: multi-texture-layer index arithmetic unverified against a real file

`cmd_export.cpp:806-819`'s `textureComboIndex + layer` /
`textureCoordComboIndex + layer` arithmetic for a batch's 2nd+ texture
layer is implemented straight from wiki prose, with an in-code note that it
hasn't been cross-checked against a real multi-layer file the way nearly
everything else in this codebase has been (the project's own stated bar,
per `DESIGN.md`'s recurring "decode real records... don't guess from text
alone" principle). Already visible in the code as a caveat, not a hidden
gap — listed here only so it's tracked alongside the others rather than
lost. Worth a real-file check whenever a multi-texture-layer test model is
available (`WIKI_FINDINGS.md`'s methodology is the template).

### 3.6 Checked and found accurate, no gap

- `--lod`/`auto` LOD selection uses the `.skin`-external SFID-index
  convention correctly — `.skin` itself carries no internal LOD marker to
  cross-check against (`src/skin.hpp:11-12`), so there's no missed
  internal field being ignored.
- `DESIGN.md`'s claim that `M2Color`/`M2TextureWeight` resolution is
  constant-value-only is accurate for the non-global-sequence case (see 3.2
  for the one real gap in that story).

---

## 4. Test coverage

The suite is unusually disciplined for its size (260 test cases / 1113
assertions, all passing) and the three-tier architecture `DESIGN.md`
describes is real, not aspirational. Concrete gaps found by mapping every
`throw`/bounds-check site in `src/` against `tests/`:

### 4.1 [Fixed] ~5% of the suite silently no-ops without env vars/tools on `PATH` — verified live

**Fixed** two ways, both verified live. First, visibility: every gated
`TEST_CASE` now uses `* doctest::skip(...)` instead of a runtime
`MESSAGE` + early `return`, so doctest's own summary reports a real,
distinct "skipped" count rather than folding a 0-assertion test into
"passed." Second, and better per the request that prompted this fix:
`tests/test_data_paths.hpp` auto-detects real fixtures already sitting in
this repo's own (gitignored) `test_data/` directory — an explicit
`HUSK_TEST_*` env var still overrides, but the default case no longer
needs one hand-set at all, and `HUSK_TEST_SKIN_DIR` specifically is
*constructed* on the fly (real SFID entry 0 read out of the resolved M2's
own header, `.skin` copied there under that FileDataID) rather than
requiring a pre-built directory. Verified: a bare `./build/husk-tests`
run now reports **259 of 260 cases genuinely exercised** (368,515
assertions, up from 1,113) and exactly 1 honestly skipped
(`HUSK_TEST_TEXTURES_DIR` — no `husk-blp`-converted PNGs are committed,
by design, since that's a separate Python toolchain). `tests/test_main.cpp`
gained a startup banner printing every fixture's resolution up front.
Originally found as follows (kept for the record): ran
`./build/husk-tests` with no `HUSK_TEST_*` env vars set: **all 260 cases
reported passed, 0 failed, 0 skipped** at the doctest summary level, but
12 of those (11 in `test_integration.cpp`, both in `test_conformance.cpp`)
printed a `SKIPPED` message and returned immediately — they ran zero
assertions, not "zero relevant assertions." Every "verified against real
data" and "passes the Khronos validator / Blender agrees with tinygltf"
claim in `CLAUDE.md`/`README.md` — i.e. the entirety of roadmap stage 7 —
depended on these 12 actually running, which required explicit env-var
wiring that wasn't visible anywhere in the repo (no CI config found).

### 4.2 [Fixed] `buildMaterialsAndPrimitives`'s six bounds checks have zero adversarial tests

**Fixed**: 9 new `test_cli.cpp` cases (one per throw site — the "resolved
via combo" checks for texture weight/texture each split into an outer-
index test and an inner-resolved-value test) isolate each check in
`buildMaterialsAndPrimitives`'s chain, verified via a reusable
`BatchFields`/`oneBatchSkin`/`materialsFixtureM2` fixture trio that lets a
test break exactly one field while keeping every earlier check in the
chain valid. All 9 assert both the nonzero exit code and the exact
expected-vs-actual message text.

Originally found as follows (kept for the record): `cmd_export.cpp:652-820` is the most complex cross-referencing logic in the
codebase — batch → submesh → material → color/weight/texture combo, four
separate arrays chained together — and has six distinct, well-written
`throw` sites with real expected-vs-actual messages (verified directly,
see §1 above). Every `test_cli.cpp` fixture sets all of
`skinSectionIndex`/`materialIndex`/`colorIndex`/`textureComboIndex`/
`textureCoordComboIndex`/`textureWeightComboIndex` to valid values; none
drives an out-of-range case. This is exactly the situation a real
mismatched-`.skin`-and-`.m2` extraction (a scenario the integration tests
*do* cover one instance of, at the file level — `HUSK_TEST_MISMATCHED_SKIN`)
would hit, and it's currently untested at the unit level where it'd be
cheap and fast to check. Highest-value gap to close of everything found in
this audit.

### 4.3 [Fixed] `main.cpp` and several CLI argv edge cases have no coverage at all

**Fixed**: 8 new `test_cli.cpp` cases now cover `husk export`/`info`/
`dump-chunks` with zero arguments, `export`'s four "flag given with no
value" branches (`--textures`/`--skin-dir`/`--anim-dir`/`--lod`),
`export`'s too-many-positionals case, and `info`/`dump-chunks`'s
extra-argument case — on top of the `--help`/`-h`/unknown-command/
no-command tests §2.1's fix already added (which, as originally noted
here, would have caught that bug directly).

Originally found as follows (kept for the record): No test invokes the binary with zero arguments, an unknown command name, or
top-level `--help`/`-h` (ironic given §2.1's finding — a test here would
have caught that bug directly). Also untested: `cmd_info.cpp`'s
`argc != 1` guard, `cmd_export.cpp`'s `argc < 1` guard, `cmd_dump.cpp`'s
extra-args case, and all four of `--textures`/`--skin-dir`/`--anim-dir`/
`--lod`'s "flag given with no value" branches (`cmd_export.cpp:1010-1032`).
Cheap to close via `test_cli.cpp`'s existing subprocess-spawn pattern.

### 4.4 [Fixed] `cmd_dump.cpp`: 9 of ~14 per-chunk JSON dumpers are asserted-covered, not actually tested

**Fixed**: 8 new `test_dump.cpp` round-trip tests cover TXAC/EXPT/PADC/
PSBC/PEDC/EDGF/DBOC and, highest-priority, `dumpWfv3` — every field at its
own offset checked via an exact `"key": value` substring per field (not
just "the number appears somewhere"), the precision the transcription-bug
risk actually called for. GPID/PGD1 stay untested on purpose: they call
the *identical* function pointer as RPID/PABC (`dumpFileDataIdArrayChunk`/
`dumpU16ArrayChunk`), so a second test would exercise the same code, not
new coverage — documented explicitly in `test_dump.cpp`'s header comment
now, instead of lumped in with the genuinely-shared-shape claim that
turned out to be true for some tags and not fully checked for others.

Originally found as follows (kept for the record): `test_dump.cpp`'s own comments claim TXAC/EXPT/PSBC/PEDC/GPID/PGD1/EDGF/
DBOC/WFV3 "share a shape already covered" by the four chunk types that
*are* tested (NERF/PABC/RPID/TEXL) — but that's a claim, not something a
test verifies. `dumpWfv3` in particular (~20 sequential hand-transcribed
float/int fields, `cmd_dump.cpp:293-340`) has no round-trip test — exactly
the class of silent transcription bug this project's own history
(`WIKI_FINDINGS.md`'s `M2Sequence`-is-64-not-36-bytes investigation) shows
is easy to introduce and hard to notice without one. The `kFallback`
tag→note table (WFV2/DPIV/AFRA/DETL/PFDC/PCOL/EXP2) is only exercised via
WFV1.

### 4.5 What's already excellent (no action needed)

- `m2.cpp`/`m2.hpp` (99 tests): essentially every `ParseError` site has
  both a positive and a truncated/oversized-count adversarial test — best
  covered file in the codebase by a wide margin.
- `skin.cpp`/`skel.cpp`/`chunk.cpp`/`bone.cpp`: every documented throw site
  has a negative test.
- `gltf.cpp`: 45 dedicated unit tests exercise every output shape
  (unskinned/skinned mesh, multi-LOD `writeGlbMulti`,
  `KHR_materials_unlit`, embedded textures, geoset/multi-texture-layer/
  billboard `extras`, `TEXCOORD_1`, `STEP` vs `LINEAR`) directly at the
  `writeGlb` API level, independent of the gated integration tests.

---

## 5. Usability, beyond the CLI-specific findings in §2

- **The `blp/` texture-conversion split is a real, documented two-step
  workflow** (`husk-blp` → PNG, then `husk export --textures <dir>`), which
  is the right architectural call per `DESIGN.md` (Pillow's DXT/BC
  maturity vs. reimplementing block decode) — but it means the *first*
  thing a new user does with a textured model requires knowing to `cd blp
  && uv sync` before `husk export` will produce anything but flat-gray
  materials. This is already about as well-mitigated as it can be without
  merging the tools (Building/Usage sections both explain it, `export`'s
  own `--help` text explains what `--textures` needs), so this is a
  observation, not a fix-it — the friction is inherent to the deliberate
  process-boundary decision, not a gap in how it's communicated.
- **No `husk` shell-completion script** (bash/zsh/fish). Given CLI.md
  §2.3 treats autocomplete as "the interface, not an add-on," and husk's
  own subcommand/flag surface is small and stable, a completion script
  would be cheap and would directly serve the doc's discovery-by-doing
  ideal (flag names, and — where feasible — `--lod`'s `all` literal and the
  `auto` keyword). Not urgent for a single-developer local tool, but worth
  a mention since it's rung 2 of CLI.md's own fallback chain and currently
  entirely absent at every subcommand level.

---

## 6. Summary punch list, roughly by leverage

1. ~~Fix `--help`/`-h` on all three subcommands (§2.1)~~ **Done.**
2. ~~Add adversarial/out-of-range tests for `buildMaterialsAndPrimitives`
   (§4.2)~~ **Done.**
3. ~~Decide the fate of `M2TextureTransform` (§3.1)~~ **Done** — real
   resolution + `husk info` counts + inert `extras`, deliberately not a
   `KHR_texture_transform` translation (see `DESIGN.md` for why).
4. ~~Fix the global-sequence-track asymmetry between bones and materials
   (§3.2)~~ **Done** — diagnostic note, not a full extras-based keyframe
   dump (see `DESIGN.md` for the scope line drawn and why).
5. ~~Wire CI (or at least document how to) so the 12 gated tests (§4.1)
   actually run somewhere~~ **Done** — auto-detected from `test_data/`
   instead, so it no longer needs separate CI wiring to exercise for real
   locally (a real CI pipeline, if one gets added later, would still need
   `test_data/` populated or `HUSK_TEST_*` set, same as any other
   environment).
6. ~~Small mop-up: `cmd_info.cpp` collision-data printing (§3.3),
   `--version` flag (§2.3), `cmd_dump.cpp` per-chunk test coverage (§4.4),
   remaining `main.cpp`/argv edge-case tests beyond `--help` (§4.3)~~
   **Done**, all four.

What's left, genuinely open: §3.4 (five lookup-table arrays parsed but
never referenced — flagged for awareness, not an action item, since
nothing downstream needs them yet) and §3.5 (multi-texture-layer index
arithmetic self-flagged as unverified against a real multi-layer file —
needs one to show up in `test_data/` before it can be checked the way
this project checks everything else). §5's usability notes were
observations, not defects, and weren't actioned.

Nothing found in this review rose to "the project is wrong about its own
status" — every current-vs-target claim spot-checked against code held
up. Every finding that *was* actionable got fixed and verified (build +
310-case test suite + real-data export, all green) rather than just
written down.
