# Failure log

**Status: all 5 items below are fixed**, each with a regression test in
`tests/test_cli.cpp` (a new file — see item 5) that runs with no real game
files needed, so these can't silently regress. Left in place as a record of
what broke and why, not as an open todo list anymore.

Findings from a hands-on "break it on purpose" pass over the parts of husk
marked Done in the README (static mesh, skeleton + skinning, `.skel`
sidecar). Goal wasn't "does it crash" so much as "when it fails, does the
failure *say why*" — per the project's own foreign-data policy (the file on
disk is foreign data; validate at the boundary; on failure print expected
vs. actual). Each item below is a confirmed, reproduced defect, not a
guess — repro steps are given so they can be turned into regression tests.

Scope note: these are usability/robustness findings, not a security
review. husk is a local CLI reading files the user already extracted
themselves — there's no adversary here, no untrusted network input, no
privilege boundary. The realistic failure sources are the mundane ones a
tool that reads a format Blizzard changes under it (per this README's own
"Why test-first" section) actually runs into: a fat-fingered path or wrong
file handed to the wrong command, an incomplete/corrupted CASC extraction
or truncated download, a `.skin`/`.skel` sidecar paired with the wrong
model, and — the big one — a future or past M2 version this parser
doesn't know about yet, where a header field it doesn't expect shifts
every fixed offset after that point and turns a real count/index field
into garbage. Every fixture below was built to reproduce one of *those*
scenarios as directly as possible, not to model a hostile actor.

All fixtures referenced below were hand-built (not committed) to isolate
one specific malformed field at a time; the generator that built them is
described inline so the repro can be regenerated. `test_data/bloodelffemale.m2`
+ `test_data/bloodelffemale00.skin` (already in this repo's gitignored
`test_data/`) cover the real-file baseline; all 58 existing pure-logic +
integration tests pass against them before and after this investigation —
nothing here is a regression, these are all pre-existing gaps.

---

## 1. [Fixed] [Critical] `husk info` crashes the whole process (SIGABRT) instead of failing cleanly, on two independent classes of malformed/unreadable input

**What's expected:** every documented failure mode prints
`husk: couldn't read '<path>': <reason>` and exits 1, same as a bad-magic
or truncated-header file already does correctly.

**What actually happens:** `cmd_info.cpp`'s `info()` only catches
`catch (const m2::ParseError&)`. Two different exception types used
elsewhere in the same call chain are *not* `m2::ParseError` and escape
uncaught, hitting `std::terminate`:

- **`husk::ChunkError`** (`src/chunk.hpp`), thrown by `readChunks()` and
  used internally by `m2::resolveBlob()` whenever a file doesn't start
  with `MD20` and its "chunks" turn out malformed. Repro: any of —
  - a chunked file whose *second* chunk header is truncated (valid MD21
    wrapper, then 1 byte of a chunk tag with no size following)
  - a chunked file whose MD21 chunk lies about its own size (declares more
    payload than bytes remain in the file)
  - **plain garbage that isn't MD20 at all** — e.g. the 4 bytes `"XXXX"`
    followed by zeroes. `resolveBlob` doesn't recognize `"XXXX"` as MD20,
    so it falls through to "maybe this is chunked," starts reading
    zero-tag/zero-size "chunks" through the zero-filled buffer, and
    eventually runs out of bytes mid-header → `ChunkError`. This means
    **almost any non-M2 file handed to `husk info` crashes it**, not just
    deliberately crafted ones — try `husk info /bin/ls` or `husk info
    README.md`.
- **`std::ios_base::failure`**, thrown by `m2::loadFile`'s
  `std::ifstream` + `istreambuf_iterator` combo on a genuine OS-level read
  error (as opposed to "file doesn't exist," which is already handled).
  libstdc++'s `basic_filebuf` throws this unconditionally on a real I/O
  error, regardless of the stream's exception mask. Repro: `husk info
  <any directory>` — e.g. `husk info test_data`.

Observed output for the directory case:
```
$ husk info test_data
terminate called after throwing an instance of 'std::__ios_failure'
  what():  basic_filebuf::underflow error reading the file: Is a directory
[1]    12345 IOT instruction (core dumped)  husk info test_data
```
Exit code 134, no `husk:` prefix, no indication to the user what they did
wrong — just a raw libstdc++ abort message.

**Why `export` doesn't have this bug:** `cmd_export.cpp` catches the much
broader `catch (const std::exception& e)`, so the identical malformed
inputs (same directory, same garbage file) fail cleanly there today —
confirmed by direct comparison. `info` is the outlier, not the norm.

**Fix:** either make `ChunkError` (and anywhere else a non-`ParseError`
exception can escape `m2::loadFile`) derive from a common base type
`cmd_info.cpp` catches, or simply widen `info()`'s catch to
`std::exception` like `cmd_export.cpp` already does. The latter is a
one-line, zero-risk fix.

**Applied:** `cmd_info.cpp`'s `info()` now catches `const std::exception&`
instead of `const m2::ParseError&`, matching `cmd_export.cpp`. Regression
tests: `tests/test_cli.cpp`'s `"husk info: directory as path..."`,
`"...truncated trailing chunk header..."`, and `"...generic non-M2
garbage..."` cases — all three previously reproduced the crash, all three
now assert a clean exit 1 with a `husk: couldn't read` message and
explicitly assert `"terminate called"` does *not* appear in the output.

---

## 2. [Fixed] [Medium] A corrupted or misread array count is `reserve()`'d *before* any bounds check, in three separate parsers — turns "bad count" into an uninformative `std::bad_alloc` instead of a real error message

**Where:** `m2::parseVertices` and `m2::parseBones` (`src/m2.cpp`), and
`skin::parseU16Array` (`src/skin.cpp`) — all three do
`result.reserve(array.count)` immediately, then only bounds-check each
element's byte offset *inside* the loop that follows. The reserve happens
before a single byte of the claimed range has been validated to exist.

**Repro (all three confirmed independently, memory-capped with `ulimit -v
2000000` as a safety net since the claimed sizes are hundreds of GB):**
- M2 with `header.vertices = {count: 0xFFFFFFF0, offset: 0}` on an
  otherwise-valid, minimal MD20 blob → `husk: export failed: std::bad_alloc`
- Same file shape but patching `header.bones` instead of `header.vertices`
  → identical `std::bad_alloc`
- `.skin` file with `header.indices.count = 0xFFFFFFF0` → identical
  `std::bad_alloc`

**Why it matters:** this is exactly the "format moved and we misread a
field" scenario the whole project is built to survive gracefully — a
version this parser doesn't know about yet, or a genuinely truncated/bad
extraction, is the realistic way a count field ends up garbage, not
deliberate tampering. And this project's own README and design notes make
a point of every other bounds violation printing exactly what was expected
vs. what was found (e.g. `"vertex 12 at offset 4096 needs 48 bytes but the
blob is only 2048 bytes"`). This is the one place that convention silently
breaks down — the user just sees `std::bad_alloc` with no field name, no
array, no file, nothing actionable, and no hint that "the format probably
changed" (which is the single most likely real explanation, per this
project's own README). It's also a real (if minor, since it fails instead
of hanging) resource-exhaustion risk: a 20-byte file with one corrupted
count field can make husk attempt a multi-hundred-GB allocation.

**Fix:** before reserving, check `array.count <= (blobSize - array.offset)
/ elementSize` (with the subtraction/division ordered to avoid unsigned
underflow when `offset > blobSize`) and throw the same descriptive
`ParseError` style already used for the in-loop checks — same fix,
applied at all three sites.

**Applied:** exactly that check, added at all three sites
(`m2::parseVertices`, `m2::parseBones` in `src/m2.cpp`,
`skin::parseU16Array` in `src/skin.cpp`), before their `reserve()` calls.
The now-redundant per-element bounds check inside each loop was removed
(the up-front check makes it unreachable — no more multiplexed validation
of the same thing twice). Regression tests: `tests/test_cli.cpp`'s three
"...corrupted huge {vertex,bone,indices} count..." cases, one per site,
each asserting the output does *not* contain `"bad_alloc"` and *does*
contain the new descriptive message.

---

## 3. [Fixed] [Medium] Cyclic bone-parent chains aren't detected anywhere — silently produces a "successful" `.glb` with a cyclic, spec-invalid scene graph

**What's checked today:** `cmd_export.cpp`'s `buildSkeleton()` verifies
each bone's `parentBone` is in range `[0, bones.size())`.
`gltf::writeGlb()` separately re-checks range *and* rejects a bone being
its own direct parent (`parent == i`).

**What's not checked:** a longer cycle — bone A's parent is B, B's parent
is A (or any longer loop). Every individual `parentBone` value in such a
cycle is in-range and non-self-referential, so it sails through both
checks.

**Repro:** a `.skel` file (`SKB1` chunk) with exactly 2 bones, bone 0's
`parentBone = 1`, bone 1's `parentBone = 0`, paired with a trivial 1-vertex
M2 + matching `.skin`:
```
$ husk export tiny_valid_m2.m2 tiny_matching_skin.skin out.glb skel_cycle.skel
out.glb: 1 vertices, 1 triangles, 2 bones (bind pose only, no animation)
$ echo $?
0
```
Exits **0** and writes a real 856-byte `.glb`. Compare to the self-parent
case (`parentBone = 0` on bone 0 itself), which *is* caught:
`husk: export failed: writeGlb: joint 0 is its own parent`.

**What actually ends up in the file:** `writeGlb`'s joint-node-building
loop does `jointNodes[parent].children.push_back(nodeIdx)` for every
joint — for this 2-cycle that produces node 1 having node 2 as a child
*and* node 2 having node 1 as a child, i.e. two glTF nodes that are each
other's descendant. glTF's own spec forbids cycles in the node graph, and
neither node has `parent == -1`, so neither is even reachable from
`scene.nodes` in the first place — they exist only in the cyclic pair and
in the skin's `joints` list. This is likely to hang or error out any real
consumer (Blender's glTF importer, three.js, the Khronos validator)
instead of husk itself catching a structurally impossible skeleton at the
one point it still has enough information to say which two bones are
involved.

**Realistic trigger, not just a hand-built fixture:** the README already
documents one real way to get a `.skel` that doesn't actually belong to
the M2 it's passed alongside — pairing the wrong LOD/model's `.skin` file
is called out explicitly as a mistake the vertex-count cross-check exists
to catch loudly. A `.skel` from a *different model* than the one it's
passed with is the exact same mistake, just for bones instead of
vertices, and there's no equivalent guard for it — a mismatched skeleton
could plausibly produce nonsensical (including cyclic) parent indices,
the same way a mismatched `.skin` produces nonsensical vertex indices.

**Why it matters more than it might look:** this is exactly the "fail
loudly instead of silently misreading" property the model/`.skin`
vertex-count cross-check (`cmd_export.cpp`) was specifically built to
guarantee for a different failure mode — bone-cycle detection has no
equivalent, so `export`'s "loud failure" guarantee has a real hole in it.

**Fix:** walk each joint's parent chain to a root (or -1) with a
visited-set, in `buildSkeleton()` (where the bone index is still easy to
name), and throw `std::runtime_error` naming the cycle, before it ever
reaches `writeGlb`.

**Applied:** `cmd_export.cpp` now has `checkNoBoneCycles()`, called at the
end of `buildSkeleton()` — an O(joints) three-color (unvisited/in-progress/
done) walk of each joint's parent chain, memoizing finished nodes so
repeated starts stay cheap. Throws naming the specific bone the cycle was
found at. Side effect worth noting: this also now catches the self-parent
(1-node cycle) case *before* `gltf::writeGlb`'s own dedicated check ever
runs for any caller that goes through `buildSkeleton()` — `writeGlb`'s
check is untouched and still fires for any caller that doesn't (it's a
public library function with its own contract, not just internal
plumbing; see `tests/test_gltf.cpp`'s `"writeGlb: joint that is its own
parent throws"`, still passing, still exercising `writeGlb` directly).
Regression tests: `tests/test_cli.cpp`'s `"...2-cycle in the bones'
parent chain..."` and `"...bone that is its own parent (a 1-node
cycle)..."` cases.

---

## 4. [Fixed] [Low] Non-finite (`NaN` / `Infinity`) vertex floats pass straight through into the exported glTF, unvalidated

**Repro:** a 1-vertex M2 with `position.x` set to a quiet-NaN bit pattern
(`0x7FC00000`) and `position.y` set to `+Infinity` (`0x7F800000`),
otherwise valid:
```
$ husk export nan_vertex_m2.m2 tiny_matching_skin.skin out.glb
out.glb: 1 vertices, 1 triangles
$ echo $?
0
```
Succeeds. `m2::parseVertices` reads the raw IEEE-754 bits with no
finiteness check, and `gltf::writeGlb`'s position-accessor `min`/`max`
computation (`std::min`/`std::max` over the raw values) has no guard
either — both the buffer data and the accessor bounds can end up carrying
`NaN`/`Inf` into the output file.

**Why it matters:** the glTF 2.0 spec requires finite values for
`POSITION`/`NORMAL` accessor data and their `min`/`max` bounds. A
corrupted or truncated-read M2 (bad extraction, interrupted download, disk
corruption — any bit-flip in a position field lands here) that produces
non-finite geometry will export "successfully" by husk's own accounting,
then fail in a much more confusing way and location — inside Blender's
importer, or the Khronos validator that roadmap stage 7 already plans to
run against output — with no link back to which vertex in which source
file was actually bad.

**Fix:** cheap `std::isfinite()` check on each component while building
`gltf::Mesh` in `cmd_export.cpp` (where the M2 vertex index is still
known), or as part of the stage-7 output-hardening pass. Either is fine;
just needs to happen *somewhere* before the bad value reaches tinygltf.

**Applied:** exactly that — `cmd_export.cpp`'s mesh-building loop now
checks `isFinite(v.pos) && isFinite(v.normal)` per vertex before pushing
it into `gltf::Mesh`, throwing `std::runtime_error` naming the vertex
index. This is a stopgap at the point that was easiest to fix now, not a
substitute for the real stage-7 glTF-Validator pass the README already
plans — that pass may well catch other spec violations this doesn't.
Regression test: `tests/test_cli.cpp`'s `"...non-finite (NaN/Inf) vertex
position..."` case.

---

## 5. [Fixed] [Test coverage gap] The CLI command layer (`cmd_info.cpp`, `cmd_export.cpp`) — where every bug above actually lives — had zero committed, always-run test coverage

Checked which test file actually calls into `husk::commands::info` /
`husk::commands::exportGlb` (directly or by spawning the built binary):
only `tests/test_integration.cpp` touches the command layer at all, and
**every test in it is skipped unless real, gitignored, game-extracted
`.m2`/`.skin`/`.skel` files are pointed to via environment variables**
(`HUSK_TEST_M2`, `HUSK_TEST_SKIN`, etc. — see `README.md`'s Testing
section). Nobody without a personal WoW install and those specific env
vars set ever runs a single test against the command layer — including CI,
presumably, unless it's specifically configured with those secrets/files.

Meanwhile, every *other* module (`m2`, `skin`, `skel`, `gltf`, `chunk`) has
thorough synthetic-fixture pure-logic tests that need no real files and
always run. The command layer is the sole exception, and it's exactly
where findings #1 and (partially) #3 above were hiding — a malformed-input
test against `cmd_info.cpp`/`cmd_export.cpp` using nothing but synthetic
fixtures (the same style already used everywhere else in this test suite)
would have caught the `ChunkError`-escapes-`info` bug immediately.

**Fix:** add a small set of pure-logic tests for `cmd_info.cpp`/
`cmd_export.cpp` using synthetic fixtures (no real game files needed) —
at minimum: bad magic, truncated/oversized chunk, and a directory-as-path
case, asserting a clean non-zero exit and a `husk:`-prefixed message
rather than a crash. This is the same technique `test_integration.cpp`
already uses to spawn the binary as a subprocess; it just needs fixtures
that don't require `HUSK_TEST_*` env vars to exist at all.

**Applied:** new `tests/test_cli.cpp`, registered in `CMakeLists.txt`,
covering findings #1-#4 above (9 test cases total) with hand-built
synthetic fixtures written to temp files at test time — no `HUSK_TEST_*`
env vars needed, confirmed by running `./build/husk-tests
--test-case="husk info:*,husk export:*"` with none set: all 14 matching
cases pass (the pre-existing real-file-only ones correctly report
SKIPPED, not failed or absent). The subprocess-spawning helper
(`runHusk`/`RunResult`/`envOrEmpty`) was factored out of
`test_integration.cpp` into a shared `tests/run_husk.hpp` so both files
use the identical mechanism rather than a second copy of it.

---

## What was checked and is *not* broken (for context, not action items)

So this list doesn't read as "everything is on fire" — these all worked
exactly as documented and did fail loudly with a specific, correct
message: empty file, bad M2/`.skin` magic, name-array claiming more bytes
than the file has, model/`.skin` vertex-count mismatch (the real
`bloodelffemale.m2` + `bloodelffemale_hd00.skin` pairing from the README),
missing/nonexistent `.skel` path, a model with inline bones plus a
redundant `--skel` argument (correctly noted-and-ignored on stderr, not an
error), output path pointing at a directory, `.skin` index count not a
multiple of 3, self-parenting bone, and writing the export output over top
of its own input file (reads the whole input into memory first, so no
read/write race). `cmd_export.cpp`'s broad exception handling in
particular means most of the "crashes in `info`" cases above are already
fine when the identical bad input goes through `export` instead.
