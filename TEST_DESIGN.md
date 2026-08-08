# TEST_DESIGN.md

Cross-cutting facts about how `husk`'s test suite (`tests/`) is built and why —
coalesced out of comments that were scattered, near-verbatim, across several test
files (a former inline-comment audit found exactly which files/lines each section
below replaces; the audit itself is gone, see `README.md`'s pre-v1 cleanup note).
Read this before adding a new test
file or a new fixture-gated `TEST_CASE`; individual test files should point here
instead of re-explaining any of this inline.

## Four-tier architecture

Tests are organized into four tiers, each answering a different question:

1. **Unit** (`test_m2.cpp`, `test_gltf.cpp`, `test_skel.cpp`, `test_skin.cpp`,
   `test_phys.cpp`, `test_bone.cpp`, `test_chunk.cpp`) — does one function/parser
   produce the right bytes/values, in isolation, mostly against synthetic fixtures
   built in-test.
2. **CLI** (`test_cli.cpp`) — does the `husk` binary, invoked as a subprocess with a
   given argv, do the right thing end to end (exit code, stdout/stderr, files
   written), mostly against synthetic fixtures.
3. **Integration** (`test_integration.cpp`) — do real, personally-extracted M2/
   `.skin`/`.skel`/`.anim`/`.phys`/`.bone` fixtures (`test_data/`, gitignored, never
   committed) produce a plausible, internally-consistent `.glb` when read back with
   tinygltf. See "Shape-only vs. exact checks" below for what "plausible" means here.
4. **Conformance** (`test_conformance.cpp`) — does a real export pass validation by
   tools outside husk's own control: the Khronos `gltf_validator` and a real
   headless Blender import. This is the tier that catches "husk thinks the glTF is
   fine but nothing else agrees."

A change to core export logic should usually be covered at tier 1 (the unit fact),
optionally tier 2 (the CLI wiring), and — if it touches real-world fixtures or
output validity — tiers 3/4 too.

## Fixture-resolution model

Real fixtures (anything under `test_data/`) are **personal, gitignored WoW
extractions** — never committed, never assumed present. Every test that needs one:

- Resolves its path via a `HUSK_TEST_*` environment variable
  (`test_data_paths.hpp` is the single source of truth for the full list and their
  fallback defaults — e.g. `HUSK_TEST_M2`, `HUSK_TEST_SKEL_DIR`,
  `HUSK_TEST_BONES_DIR`, `HUSK_TEST_WEAPON_PHYS`), each defaulting to a real path
  under `test_data/` if the env var isn't set, so a fresh checkout with the
  extraction in place needs no environment configuration at all.
- Is wrapped in `doctest::skip()`, gated on that fixture's file actually existing on
  disk at test-collection time — so the suite still passes cleanly (skipping, not
  failing) on a machine without the personal extraction, e.g. CI or a fresh clone.
- `test_main.cpp`'s startup banner prints every resolved `HUSK_TEST_*` path once, so
  a run's test log states plainly which real-data tests actually executed vs. skipped
  — a skip should be visible, not silent.

Adding a new real-data test: add the env var + default to `test_data_paths.hpp`,
gate the `TEST_CASE` with `doctest::skip(!std::filesystem::exists(path))`, and add
the path to `test_main.cpp`'s banner. Don't invent a second resolution convention.

## Conformance gating (`HUSK_GLTF_VALIDATOR`/`HUSK_BLENDER`)

`test_conformance.cpp`'s cases are gated on two build-time `#ifdef`s
(`HUSK_GLTF_VALIDATOR`, `HUSK_BLENDER`), set by CMake only when the corresponding
external tool (Khronos `gltf_validator`, Blender itself) was actually found on
`PATH` at configure time. Neither tool is a hard dependency of this project — a
machine without them still builds and runs every other tier. When either macro is
undefined, the gated `TEST_CASE`s are compiled out entirely, not skipped at
runtime — this is a configure-time capability check, not a fixture-presence check
(contrast with the `doctest::skip` fixture pattern above, which is a runtime check).
If you add a new conformance case: gate it the same way, don't invent a runtime
check for tool presence.

## Mutation-tested regressions

A regression test that only proves it *passes* against current code proves nothing
about whether it would have caught the bug it's named after. Several tests in this
suite were built (and should continue to be built) by: writing the assertion,
temporarily reverting the fix it's supposed to guard (via `git stash`, a manual
one-line revert, or a hand-rolled broken variant), confirming the test actually
fails against the broken version with the *expected* failure shape (not a crash or
an unrelated error), then restoring the fix and confirming green. This is what
"proven to actually catch the bug, not just proven to pass" means whenever it's
invoked as a rationale for a test's shape — do this before trusting a new
regression test, and prefer noting *that this was done* over re-explaining the
mutation-testing concept itself inline.

## Independent-transcription convention

Wire-format byte offsets used in a unit test's synthetic-fixture builder (e.g. an
`M2Sequence`'s field offsets in `test_m2.cpp`, or a `.skin`/`.skel` record layout in
`test_skin.cpp`/`test_skel.cpp`) are typed out independently from the corresponding
`src/` parser's own offset table, not copy-pasted from it. The point: a test built
by copying the implementation's own offsets can't catch an off-by-one or
transposed-field bug in that implementation — both would agree on the same wrong
answer. Keeping the transcription independent means a mismatch between the test's
expected bytes and the parser's actual read is a real signal, not a foregone
conclusion. This convention applies wherever a test hand-builds raw struct bytes;
new struct-literal test fixtures should follow it rather than deriving offsets from
the header they're testing.

## Shape-only vs. exact checks

Two different real-data testing postures show up across the integration and
conformance tiers, and the difference matters when writing a new one:

- **Exact-value checks** are only realistic against fixtures whose full expected
  output was independently derived and is small/stable enough to hardcode (a
  synthetic fixture, or a tiny real one — e.g. an 8-position/12-triangle collision
  mesh). Use `CHECK(actual == expectedExactValue)`.
- **Shape-only checks** are for real, large, organically-varying fixtures (a real
  character model's hundreds of animation clips, thousands of vertices) where no
  one has hand-verified every value and hardcoding one would be brittle busywork,
  not a real test. These assert structural properties instead: counts within a
  plausible range or a conservative lower bound (not the exact number, so the test
  doesn't become a silent tripwire if the fixture set changes slightly), every
  value finite/in-range, no NaNs, round-trip consistency (e.g. Blender's own bone
  count matches the header's bone count exactly, even though individual bone
  transforms aren't hand-checked).

Don't force an exact-value check onto a real, large fixture just because it feels
more rigorous — a hardcoded "342 clips" assertion on a real character model is
usually asserting "whatever the code currently produces," not a verified fact, and
breaks on every unrelated fixture change. Use shape-only checks for that class of
fixture, and reserve exact-value checks for cases where the expected value was
actually derived by hand from an independent source.
