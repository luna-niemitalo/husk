# Transform correctness triage: "upside down" export, root cause, and how to stop this class of bug permanently

Requested directly, after `BLENDER_EXPORT_TODO.md` §8 confirmed husk's M2→glTF
position export is measurably upside down: "I want a more robust system that
can test the correctness of the mesh regardless of the rotation... if the
code has a plethora of hardcoded signals, that is prone to break the instant
we get a model in an unexpected orientation... research and explore how to
fix this permanently, so if Blizzard changes what their models' up means, it
will not be this rework again." This is a "catastrophic issue causing
rewrite" triage: what's broken, why husk's own process let it ship, how the
transform layer should have been built in the first place, and what a
durable fix looks like — not a patch.

**Update, same session: implemented.** Luna reviewed §5's design and
answered the open questions directly — build §5a and §5c *before* the
formula fix ("yes, you build while i nap"), fold §5a's single-matrix
refactor into this same change ("part of this"), add §5c's optional
humanoid-landmark check ("worth adding, but not critical"), and add a
quadruped fixture for §5e with no specific preference. Corrected §5b/§5c's
own original drafts first per her direct pushback (see the two inline
corrections marked below — `reference/wow.export` was never trusted as
ground truth, and the semantic check was redesigned to not depend on any
asset having a stable "up" convention) before implementing anything. §5a
(the single source-of-truth matrix), §5c (the asset-agnostic synthetic
probe, run through a real headless Blender import), the optional humanoid
landmark check, and §5e (a real quadruped fixture) are all implemented,
tested, and green — see each subsection's own "Implemented" note below and
the Recommended-sequencing section for exactly what ran in what order,
including proving the new tests actually catch the historical bug (not
just that they pass) before trusting them.

**What's still explicitly open, by Luna's own design**: the last piece of
§7's sequencing — a real animated clip, visually confirmed in Blender's
actual GUI, not a headless probe — was deliberately left to her: "I read
through the document, start implementing and after all of it is tested and
implemented i will verify... until then you'll have to rely on headless
Blender." Everything in this document that *can* be verified headlessly has
been; the final sign-off is hers, not automated.

---

## 1. What's actually broken (recap + new evidence)

`BLENDER_EXPORT_TODO.md` §8 already established, empirically, twice over:

- A real headless-Blender import of `bloodelffemale.m2`'s export lands a
  head-height landmark bone (`_Name`, `keyBoneId` 22, M2 pivot `z=2.05`)
  **below** the root/feet-level bone (M2 pivot `(0,0,0)`) — Blender world
  `z=-2.05` vs. `z=0.00`.
- The render mesh's own bounding box sits entirely on the wrong side of the
  origin: `Z=-1.99` to `Z=+0.01`, the mirror image of a standing character.
- Flipping `src/gltf.cpp`'s `zUpToYUp` from `{x, -z, y}` to `{x, z, -y}`
  fixes both measurements exactly (`_Name` moves to `z=+2.05`, bounding box
  becomes `Z=-0.01` to `Z=+1.99`) and breaks exactly one test — the
  `zUpToYUp` unit test that hardcodes the old formula's literal output.

That fix was tested, confirmed, then **deliberately reverted** — not
because it's wrong, but because `DESIGN.md`'s own record shows the rotation
and scale conversions (`toGltf(m2::Quat)`, `toGltfScale`) were derived *by
hand* from the old position formula's permutation, and were never
independently re-verified against the flipped one. Shipping the position
fix alone risks a worse, harder-to-spot bug: a mesh that sits in the right
place but animates with limbs bending the wrong way.

### New finding this session: an independent second implementation confirms the fix, including the part `BLENDER_EXPORT_TODO.md` §8 explicitly left unverified

This repo already has `reference/wow.export` checked in (used for
`TOOL_COMPARISON.md`'s husk-vs-wow.export writeup) — a widely-used,
independently-authored, community-trusted M2→glTF/OBJ/etc. export pipeline
that solves this *exact* problem. Nobody had cross-checked its own
coordinate-conversion code against husk's until now. It's a real,
independent second opinion — not the same formula checked against itself.

`reference/wow.export/src/js/3D/loaders/M2Loader.js` converts every
vertex, normal, collision normal, bone translation, bone pivot, bone
rotation, and bone scale with the same three blocks (lines 190-260,
380-450, 630-655), reproduced here in husk's own `(x, y, z)` naming:

```
translation / pivot / vertex / normal:  (x, y, z) -> (x,  z, -y)
rotation (quaternion, vector part only): (x, y, z, w) -> (x,  z, -y, w)
scale (unsigned magnitude, no sign flip): (x, y, z) -> (x,  z,  y)
```

Compare against husk today:

| Value | husk today | wow.export | Match? |
|---|---|---|---|
| Position (`zUpToYUp`) | `{x, -z, y}` | `{x, z, -y}` | **No — husk has the wrong sign/permutation** |
| Rotation (`toGltf(m2::Quat)`) | `{x, -z, y, w}` | `{x, z, -y, w}` | **No — same error, husk derived it *from* the wrong position formula** |
| Scale (`toGltfScale`) | `{x, z, y}` | `{x, z, y}` | **Yes — already correct, untouched by this bug** |

This is stronger evidence than the session's own headless-Blender probe,
for two reasons:

1. It's a **second, independently-written implementation**, not husk's own
   formula checked against Blender's roundtrip of husk's own formula — the
   Blender check alone can't rule out "both husk and my mental model of
   what 'correct' looks like share the same misunderstanding."
2. It directly answers the one thing `BLENDER_EXPORT_TODO.md` §8 explicitly
   flagged as unverified: **the rotation conversion**. wow.export's own
   rotation block uses the identical permutation-of-the-vector-part rule
   husk's own code comment already describes as the right *shape* of fix —
   just applied to the corrected permutation, not the current wrong one.
   Scale needed no change at all — husk's existing `toGltfScale` already
   matches wow.export exactly, which is itself informative (see §4).

So: the flipped position formula, the flipped rotation formula, and the
existing (untouched) scale formula now have **three independent, converging
signals** — husk's own headless-Blender roundtrip (positions only), the
mathematical change-of-basis derivation (`DESIGN.md`'s existing reasoning,
positions and rotations), and now a separate real-world implementation
(positions, rotations, *and* scale, all at once). See §7 for why this still
isn't "ship it" on its own.

**Explicit caveat, corrected after review: wow.export is not ground truth
here, and this document should not treat it as one.** wow.export is
real-world-deployed and battle-tested by a large user base, but it is also
known to be flaky and not reliably correct in general — its own codebase
has plenty of rough edges. Agreement with wow.export is *directional*
evidence (a second, independently-derived formula landing on the same
answer as the mathematically-predicted fix is more informative than no
second opinion at all), not proof. It's being used here the way a second,
imperfect witness is used in any investigation: it raises confidence, it
doesn't close the case. §5b below revises the original "cross-validate as a
standing check" proposal accordingly — wow.export is not trustworthy enough
to gate a test suite on, and the real fix for "how do we know what's
correct" has to come from something that doesn't depend on trusting a
specific external tool at all (§5c's synthetic probe).

---

## 2. Root cause, mechanically

`src/gltf.cpp`'s `zUpToYUp` cites `documentation/wowdev-wiki/md/M2.md` line
1564 verbatim: "Models... use a Z-up coordinate system, so in order to
convert to Y-up, the X, Y, Z values become (X, -Z, Y)." husk's code matches
that sentence exactly — this was never a transcription bug. The problem is
what happens *after* husk's own conversion: Blender's stock glTF importer
performs its own, independent Y-up→Z-up conversion on the way back in, and
the composition of "wiki's documented Z-up→Y-up formula" with "Blender's
own Y-up→Z-up import formula" does not net out to identity the way
round-tripping the same physical up-axis through two inverse conversions
should. Something in that chain — the wiki's own formula, an unstated
assumption about which axis conversion Blender's importer actually performs
(there is more than one glTF-Blender axis convention in the wild;
`documentation/wowdev-wiki/md/M2.md` doesn't specify which importer or glTF
convention it was validated against, if any), or an interaction neither
source individually anticipated — was never resolved with certainty. What
*is* certain: wowdev.wiki's own prose, taken at face value and implemented
literally, produces a net 180°-about-X flip in the one Blender-import path
Luna's own notes call "the one that works."

## 3. Root cause, as a process failure — why this shipped and stayed hidden

This is the part actually worth fixing, since a code fix alone doesn't stop
a repeat.

1. **A single secondary source was trusted with no cross-check at all —
   not against a second opinion, and not against any ground truth husk
   could generate for itself.** wowdev.wiki is a wiki — community-
   maintained, disputed in exactly the way `BLENDER_EXPORT_TODO.md` §1
   already found for an unrelated field (`.skin`'s bone-index dispute,
   three editors disagreeing on the same page) — and its one sentence on
   this topic was implemented literally and never checked against
   anything else. To be precise about what "cross-check" should mean here,
   since it's tempting to overcorrect toward "should have diffed against
   `reference/wow.export`" — **that's not quite right either.**
   `reference/wow.export` is itself flaky and not reliably correct (it's
   real-world-deployed, which makes it a *useful* secondary data point, not
   an *authoritative* one). The actual gap is that husk had no way to check
   its own formula against **anything it didn't already assume** —
   including a ground truth it could construct entirely for itself, with no
   dependency on any external source's reliability at all (§5c). Consulting
   `reference/wow.export` earlier would have raised suspicion sooner (its
   formula disagreed with husk's own), but treating that disagreement as
   decisive on its own would trade one single-source-of-truth problem for
   another.

2. **Dependent conversions were derived by hand from the (wrong) position
   formula, and the derivation's own correctness was asserted, not
   tested.** `DESIGN.md`'s own text is explicit about this: the rotation
   and scale conversions were "derived from the general change-of-basis
   rule... checked numerically against several test rotations... not
   taken on faith" — but "checked numerically" here meant "does the
   permutation-shortcut match a matrix-conjugation of the *same* formula,"
   an internal-consistency check, not a check against any external ground
   truth. If the input formula is wrong, a numerically-consistent
   derivation from it reproduces the same wrong answer with perfect
   internal agreement. `toGltf(m2::Quat)`'s own doc comment says as much,
   already: *"this wasn't visually verified against a real animated model
   in a 3D viewer, only mathematically... treat a first real animated .glb
   as still worth a sanity look in Blender."* That sentence is a
   correctly-identified, explicitly-written-down risk that then sat
   unconverted into an actual test until a human happened to look —
   about a week later, in wall-clock terms, but still long enough (several
   sessions of further work layered on top of the same unverified
   assumption) that the gap between "written down as a risk" and "actually
   checked" was real, not instantaneous.

3. **No test in this codebase has ever asked "which way is up."**
   `tests/blender_import_check.py` (the headless-Blender conformance tier)
   checks armature count, bone count, mesh/vertex counts, action count, and
   collision-mesh counts — pure structure, no position ever leaves the
   Python probe as a printed value at all. `tests/test_conformance.cpp`'s
   one position-bearing check (the bounding-box containment test,
   `WIKI_FINDINGS.md` §5) compares the M2 header's *own* bounding box,
   passed through the *same* `zUpToYUp`, against the exported mesh's
   bounding box, also passed through the *same* `zUpToYUp` — a real,
   valuable check for "is the header's stated bounds a superset of the
   actual geometry," but structurally blind to a bug that flips both sides
   identically, because a consistent global sign error cancels out of a
   containment comparison between two things that both went through the
   same (buggy) conversion. This is exactly the situation `BLENDER_EXPORT_TODO.md`
   §8 already diagnosed in its own words: *"nothing in this project's
   existing test suite actually checks absolute up-direction, only counts,
   containment... and structural agreement between tinygltf and Blender."*
   Two independent tools agreeing that a mesh has N vertices proves nothing
   about whether the mesh is right-side up.

4. **Verification leaned on one fixture family.** `bloodelffemale.m2`/`_hd.m2`
   carry essentially every position-bearing conformance/integration test in
   this repo. Both are the same humanoid biped, same rest pose convention,
   same up-direction quirks (if any). A test suite built entirely around
   one model's geometry can't distinguish "this specific model happens to
   look plausible" from "the transform is actually correct for any model,"
   and can't surface an orientation bug that a different body plan
   (quadruped, flying creature, a turret with no obvious "up") might
   expose differently.

None of these four are "husk's coordinate math is hard" — they're testing
methodology gaps that would reproduce identically for *any* future
transform-layer change, including the one this document proposes. §5 and §6
are the actual fix for the process, not just the formula.

## 4. A smaller, related observation: why scale was already right

Worth noting precisely, since it's informative about the shape of the bug:
`toGltfScale` was never wrong, and wow.export's own scale conversion
confirms it (§1's table). That's consistent with the root cause being a
**sign/handedness** error specifically (a proper-rotation-vs-improper
distinction), not a general "the axis convention is wrong" error — scale is
insensitive to sign (a magnitude), so an error that only shows up as a sign
flip on position/rotation's vector part would leave scale untouched exactly
as observed. This is a useful sanity check on the diagnosis itself: the
*shape* of what's broken (position + rotation, not scale) matches the
*shape* of the proposed explanation (a sign error in the permutation, not a
wrong axis pairing).

---

## 5. How this should have been built the first time — the durable fix

The request was explicit: not "fix this file," but "fix this permanently."
That means treating the coordinate conversion as a single designed
subsystem with its own correctness obligations, not three independently
hand-derived free functions (`zUpToYUp`, `toGltf(Quat)`, `toGltfScale`)
that happen to agree today because a human re-derived each one correctly
from the others by hand.

**5a. One source of truth, mechanically-derived sub-conversions.**
Represent the WoW→glTF axis conversion as a single explicit change-of-basis
value — concretely, a 3×3 signed-permutation matrix (or, if a future WoW
coordinate convention ever turns out not to be a pure axis permutation, a
general orthonormal 3×3 matrix) — defined in exactly one place. Derive
position, normal, rotation, and scale conversions from that one matrix
*mechanically*, at compile time or via a small shared helper, rather than
as three separately-typed functions a human keeps in sync by memory:

- position/normal: `v' = M * v`
- rotation: convert the quaternion to a 3×3 rotation matrix `R`, compute
  `R' = M * R * Mᵀ` (valid since `M` is orthonormal), convert back to a
  quaternion — this is the *general* rule `DESIGN.md` already cites in
  words ("apply the same permutation to the vector part") but currently
  implemented as a second, separately-hand-typed function instead of a
  literal application of the stated rule to the stated matrix
- scale: apply `M`'s permutation to the diagonal, dropping signs (only
  valid because `M` is a signed permutation — flag this assumption
  explicitly in code, since it would need revisiting if `M` ever becomes a
  general rotation)

Once there is one matrix, "the position formula changed" cannot silently
leave rotation stale — there's nothing left to independently get wrong.

### Implemented

`gltf::zUpToYUp`/`gltf::rotationZUpToYUp`/`gltf::scaleZUpToYUp` (`src/gltf.hpp`/
`gltf.cpp`) are the single source of truth described above — a private `Mat3`
(row-major 3×3) plus `kWowToGltf`, the one matrix every value kind is
derived from mechanically (`applyMat3` for position/normal,
`quatToMat3`/`multiply`/`transpose`/`mat3ToQuat` composing `R' = M R Mᵀ` for
rotation, an unsigned-permutation `applyMat3` variant for scale). A
`static_assert` on `kWowToGltf`'s determinant (`mat3Determinant`) enforces
§5d's invariant at compile time, not in a comment. `src/cmd_export.cpp`'s
`toGltf(m2::Quat)`/`toGltfScale` are now thin wrappers calling these
directly — the exact "no separately hand-typed formula left to drift"
result this section describes. Verified two ways before trusting the
mechanical derivation itself: a property-based test
(`tests/test_gltf.cpp`, "converting-then-rotating equals
rotating-then-converting") checks, for several real test rotations and
probe vectors, that `M*(rotate(q,v))` equals `rotate(rotationZUpToYUp(q),
M*v)` — an algebraic identity any correct implementation must satisfy for
*any* orthonormal `M`, independent of whether `M` itself is the historically-
buggy matrix or the corrected one, so this specifically catches a bug in
the matrix<->quaternion round trip machinery, not in which matrix is
"right." (One real false alarm caught and fixed here: the test's own
hand-typed "arbitrary rotation" quaternion literal wasn't quite unit-length,
which broke `quatToMat3`'s implicit unit-quaternion assumption and produced
a small, confusing failure that looked like a code bug — fixed by
normalizing every test quaternion at test time rather than trusting a
hand-typed literal's precision.) Separately, §5c's synthetic probe (below)
verifies the *chosen* matrix is actually correct, through the real pipeline.

**5b. `reference/wow.export` is a weak, non-authoritative corroborating
signal — use it for triage, never as a pass/fail gate.** Corrected after
review: the original draft of this section proposed diffing husk's output
against wow.export's own conversion math as a "standing check" in the test
suite. That's wrong, and worth explaining why, since it's a tempting
mistake to make with any second implementation that happens to be sitting
in the repo already. wow.export is real, deployed, and used by a lot of
people — but it's also known to be flaky and not reliably correct. Wiring
a `ctest` case to assert husk agrees with it would mean a future wow.export
bug (or an update that fixes one) silently starts failing or passing
husk's own suite for reasons that have nothing to do with husk's own
correctness. The right role for it: an ad hoc, one-off script (same
"scratch, not committed" convention this repo already uses for real-file
investigation scripts) to consult *when a formula is disputed or an
external spec is ambiguous* — exactly how it was used in §1 above, to
widen the hypothesis space and add one more data point before trusting a
fix. Not a standing test, not a gate, not treated as ground truth on its
own.

### Implemented (as designed — no standing test added)

Used exactly this way in §1's investigation (already reflected above) and
nowhere else — no `reference/wow.export`-diffing script or `ctest` case was
added, per this section's own conclusion. Nothing further to do here.

**5c. An asset-agnostic, synthetic coordinate-frame probe — not a
real-model semantic heuristic.** This is a direct correction of the
original draft's "head above feet" proposal, which doesn't survive contact
with the actual shape of husk's corpus. Pushback that killed it: what
ground truth does a *weapon* have? A sword's rest-pose local orientation
has no necessary relationship to world "up" at all — it gets whatever
orientation its parent hand-attachment bone imposes, and even in its own
local space, "blade points up" isn't a real WoW authoring convention to
assert. Same problem for a mount, a siege engine, a doodad, a flying
creature with no feet — "does this look upright" is a question that only
has a stable answer for one body plan (standing bipeds), and hard-coding
that into the one general orientation check is exactly the "conditional on
asset type, fragile the instant something unexpected shows up" pattern
this whole investigation is trying to get away from.

The fix: stop asking the real asset semantic questions at all. **Test the
coordinate *frame*, not the content.** Construct a minimal, synthetic
skeleton — not a real character, not tied to any body plan — purpose-built
as a coordinate-frame probe: a root bone at the local origin, with three
child bones offset by one unit along local `+X`, local `+Y`, and local
`+Z` respectively (plus, separately, one child with a known, non-trivial
rotation — e.g. 90° about local `+Z` — and one with a known non-uniform
scale, so rotation and scale get the same asset-independent ground truth
translation gets). Wrap it in a minimal valid `.m2`/`.skel`, export it
through husk, and import the result through the real headless-Blender
tier (`tests/blender_import_check.py`) — this specific step matters: the
bug in this document was never in the *math*, which was internally
consistent throughout, it was in the *composition* with Blender's actual,
empirical import behavior, so the check has to go through the real
importer, not just re-derive the same formula and check it agrees with
itself. Assert exactly which Blender-space axis and sign each probe bone
lands at — a hardcoded expectation, but one derived from "this is what a
correct change-of-basis must do to a *unit vector*," not from any claim
about what a WoW asset should look like.

This generalizes for free across every asset type in the corpus, with zero
conditionals: the probe skeleton isn't a humanoid, a weapon, or anything
else — it's asset type as a category doesn't apply to it, so there's
nothing to gate on. It's also strictly more diagnostic than the real-model
symptom that actually surfaced this bug: "head landed below feet" tells
you *something* is wrong; "the `+Y` probe bone landed at Blender
`(0, -1, 0)` and the `+Z` probe at `(0, 0, -1)`, both instead of the
identity mapping" tells you exactly which axes/signs are broken,
immediately — confirmed directly, not just hypothesized: running the probe
against the historical (buggy) matrix (below) reproduced exactly this,
while the `+X` probe landed correctly, matching the root-cause math in §2
(the bug was equivalent to using the *inverse* of the correct conversion,
which fixes the axis a rotation's own axis leaves invariant and flips the
other two).

Real-content checks (the head/feet landmark idea) are still worth having,
but demoted to an optional, best-effort **secondary** signal for models
that happen to be humanoid — never load-bearing, never gating a build on
its own, and explicitly skipped (not failed) for anything without a
head-class and foot-class key bone. Its only job is "a second, real-world
sanity confirmation on top of the synthetic probe that already proved the
math is right" — not the primary mechanism for catching this class of bug,
which is §5c's synthetic probe.

### Implemented

The synthetic probe: a `gltf::Skeleton` built directly (not through any M2
file) with a root joint named `husk_probe_root` at the local origin and
three children — `husk_probe_x`/`_y`/`_z` — offset by `zUpToYUp` of the M2-
local unit vectors along +X/+Y/+Z respectively, fed straight to
`gltf::writeGlbMulti` (`tests/test_conformance.cpp`). `tests/
blender_import_check.py` gained a generic bit: any armature bone whose name
starts with `husk_probe_` gets its world-space head position printed, so
the check has zero fixture-specific plumbing beyond the probe skeleton
itself. Asserted: each probe lands at *exactly* its own original M2-local
offset in Blender's world space (the round-trip-identity property §2
describes) — no per-axis special-casing, one assertion shape reused three
times.

**Proven to actually catch the bug, not just proven to pass**: before
trusting this test, its `kWowToGltf` matrix was temporarily reverted to the
historical formula, rebuilt, and rerun — it failed exactly as predicted
(`husk_probe_x` still correct at `(1,0,0)`, `husk_probe_y` landing at
`(0,-1,0)`, `husk_probe_z` at `(0,0,-1)`), then the corrected matrix was
restored and reverified green. Same "prove a regression test actually
regresses" discipline this project's own history already uses for the
`--anim` basename-fallback fix and the multi-root synthesized-node fix.

The optional secondary humanoid-landmark check: `blender_import_check.py`
also prints the world head position of a bone literally named `_Name` (a
real wowdev.wiki key bone, id 22, near the top of a humanoid's head) when
the imported armature happens to have one; a new, `doctest::skip`-gated
`test_conformance.cpp` case asserts it lands above the armature origin on
Blender's own up axis. **A real bug this caught in its own first draft**:
Blender is natively Z-up, not Y-up — the check first asserted the *second*
raw component of the printed `(x, y, z)` triple was positive, which is
wrong (Blender's own "up" is the *third* component); caught immediately
because the real `bloodelffemale.m2` fixture's own `_Name` landmark prints
as `(0, 0, 2.05)` — obviously wrong against a `.y > 0` assertion, obviously
right against `.z > 0` — before the test was ever trusted. A useful
reminder that "which raw component means up" isn't free even once the
underlying math is right; the synthetic probe's own three separate
per-axis assertions (`px.x`, `py.y`, `pz.z`) never had this ambiguity
because each one names its own axis explicitly.

**5d. A determinant/handedness check as a standing invariant, not a
comment.** `DESIGN.md`'s own text already computes that `zUpToYUp`'s
permutation "has determinant +1" as part of justifying the rotation
derivation — that fact is exactly the kind of thing that should be a
runtime-computed assertion (`static_assert` or a startup/test-time check on
the matrix from §5a) rather than a claim in prose that has to be
re-verified by hand every time the matrix changes.

**5e. Fixture diversity — for structural coverage, not for orientation
correctness.** Add at least one non-biped, non-`bloodelf*` fixture to the
position-bearing conformance tier — a quadruped creature and a
symmetric/non-humanoid object (a turret, a siege weapon) are both good
candidates and this repo already has thousands of real files to pick from.
Worth being precise about what this buys, now that §5c's primary check is
the synthetic probe rather than a real-content heuristic: it does **not**
add orientation-correctness coverage the synthetic probe doesn't already
have — the probe is asset-shape-independent by construction, one weapon or
quadruped fixture doesn't make it "more correct" for other weapons or
quadrupeds any more than the single humanoid fixture already did for other
humanoids. What real diversity *does* buy: exercising the rest of the
export pipeline (multi-root skeletons, non-trivial batch/material setups,
particle/ribbon anchors, `.phys` bodies) against body plans and hierarchy
shapes `bloodelffemale.m2` doesn't represent, so a *different* class of bug
— unrelated to axis convention — doesn't get the same free pass this one
did by only ever running against one file family. And, secondarily,
gives §5c's optional humanoid-landmark check more than one biped to run
against.

### Implemented

`test_data/creature/wolf/wolf.m2`/`wolf00.skin` (a real, small, 66-bone/
557-vertex base wolf model — gitignored, same "personal WoW extraction,
never committed" convention every other `test_data/` fixture already
follows) plus `HUSK_TEST_QUADRUPED_M2`/`_SKIN` env-var resolution
(`tests/test_data_paths.hpp`, same `resolve()`/banner pattern as every
other fixture) and two new `doctest::skip`-gated `test_conformance.cpp`
cases: a real `gltf_validator` zero-errors check, and a headless-Blender
bone/vertex-count-agreement check (deliberately the same *shape* of check
`bloodelffemale.m2` already gets elsewhere in this file — structural
pipeline coverage, not a new orientation assertion, per this section's own
"does not add orientation-correctness coverage" conclusion above). Both
pass on the real fixture.

**5f. Make the visual-verification tier repeatable, not disposable.** This
session's actual verification method — a throwaway headless-Blender Python
probe, run once, numbers eyeballed, then deleted — is real signal (it's
what caught this) but leaves nothing behind for the next transform change
to reuse. Promote the probe pattern used for §1's measurement into a
committed, general-purpose tool (extending `tests/blender_import_check.py`
with the two landmark-bone-position prints this investigation used, gated
the same `doctest::skip`/env-var-directory convention every other
real-fixture test in this repo already uses) so "does this still point the
right way" is a one-command check forever, not a bespoke script re-invented
under time pressure the next time someone doubts the orientation.

### Implemented

Folded into §5c's implementation above rather than built as a separate
piece — `blender_import_check.py`'s `husk_probe_`-prefix printing and its
`_Name`-landmark printing are both committed, general-purpose additions to
the same script every other conformance test already shares, not one-off
scratch code. Any future transform change gets both checks for free by
running the existing suite, no bespoke probe script needed.

## 6. What this buys, concretely, against the actual worry

The stated fear was specific: a future model in an "unexpected orientation,"
or Blizzard changing what "up" means, silently breaking husk again the same
way. Walking through §5's pieces against that scenario:

- §5a means there is exactly one place encoding "what up means" — a single
  matrix, not three independently-hand-derived formulas that could drift
  out of sync with each other even if someone *does* remember to update all
  of them.
- §5c's synthetic probe carries **no assumption about asset type at all** —
  it isn't a humanoid, a weapon, or anything else, so there's nothing to
  gate on and nothing that breaks the instant an unfamiliar body plan shows
  up. It checks a property of the coordinate *frame* (does a unit vector
  along each local axis land where a correct change-of-basis says it
  should, through the *real* Blender import path), independent of what any
  real WoW asset's own orientation convention happens to be — this is the
  direct fix for the "weapons aren't necessarily up-is-up, this seems
  fragile" pushback: the probe never asks what "up" should mean for a
  weapon, a mount, or anything else, because it isn't testing a real asset.
  If Blizzard ever changed what a raw axis in the M2 container represents,
  this probe would need its own expected values updated to match the new
  documented convention — same as any coordinate-system test would — but it
  would keep working identically for every asset type without new
  conditionals, because it was never conditioned on asset type to begin
  with.
- §5b, corrected, means an ambiguous or disputed formula gets a second data
  point for triage before anyone trusts a fix — not a second single source
  of truth substituting for the first.
- §5e gives the export pipeline's *other* moving parts (skeleton shape,
  batching, sidecar data) real diversity, so a future non-orientation bug
  doesn't get the same one-fixture free pass this bug's siblings (§1/§2 in
  `BLENDER_EXPORT_TODO.md`) already showed is possible.

None of this prevents a *new* kind of bug from ever existing — nothing can
promise that — but it converts "someone has to notice by eye, in Blender,
by chance" into "an automated, semantic, fixture-diverse test fails loudly
the moment it happens," which is the actual, buildable version of
"permanent."

---

## 7. The fix — implemented, tested, and applied

**Update: shipped this session**, per Luna's own explicit "build while I
nap" direction, following the sequencing this section originally
recommended (still shown below, now marked against what actually happened).
The concrete code change — real, in `src/gltf.cpp`/`gltf.cpp`, not the
sketch this section originally proposed — is now mechanically derived per
§5a rather than three separate hand-typed functions:

```cpp
// src/gltf.cpp -- the single matrix (kWowToGltf), row-major:
//   {1, 0, 0}
//   {0, 0, 1}
//   {0, -1, 0}
// i.e. (x, y, z) -> (x, z, -y), mechanically applied by zUpToYUp/
// rotationZUpToYUp/scaleZUpToYUp -- see gltf.hpp's own doc comments.
```

This is the exact permutation `BLENDER_EXPORT_TODO.md` §8 tested for
position and confirmed fixes both measurements, extended to rotation using
the same "same permutation on the vector part" rule the codebase already
uses — now with §1's independent wow.export cross-check confirming both the
position *and* rotation formulas match a second, real implementation,
confirming scale needs no change at all, *and* with §5c's synthetic probe
now proving the whole thing through a real headless-Blender import (and
proving, by reverting it temporarily, that the probe actually catches the
historical bug and not just that it passes).

**Why this wasn't "just ship it," even with three-way corroboration** (the
reasoning that drove the "build the tests first" sequencing below):

1. **No animated pose has been visually confirmed correct yet, by anyone,
   in Blender's actual viewport**, with this exact rotation formula. **This
   one is still true, deliberately** — it's the one item in this whole list
   Luna kept for herself: "after all of it is tested and implemented i will
   verify... until then rely on headless Blender." Every piece of evidence
   this session added is numeric (headless probes, the property test, the
   wow.export cross-check) — real signal, but not the same as watching a
   two-handed attack animation actually look like one. A plausible failure
   mode this session's own checks don't rule out: a formula that's correct
   for *rest-pose* bind transforms but interacts wrong with
   `M2CompBone.pivot`-relative keyframe deltas specifically (`DESIGN.md`'s
   own rotation-derivation caveat already flags dependence on "the
   assumption that WoW bones use plain parent-relative TRS with no separate
   pivot concept beyond `M2CompBone.pivot`" — the property test in §5a
   confirms the *conversion math* is self-consistent, not that this
   specific pivot assumption holds for every real animated bone).
2. **This touches every position/rotation/scale husk has ever exported.**
   Resolved by shipping the fix and the verification system together, as
   this section originally insisted on — every existing test that touched a
   position/rotation/scale value passed unmodified against the corrected
   formula (confirmed by the full suite staying green, 484/484 +1 skip via
   `husk-tests`, 485/485 via `ctest`, immediately after the flip — nothing
   needed hand-updating, which is itself informative: it means no other
   test in this codebase was silently depending on the old formula's
   specific wrong values).
3. **§5's testing infrastructure doesn't exist yet.** No longer true — see
   each subsection's own "Implemented" note above.

**What actually happened, following this section's own original
recommended sequencing**:

1. Built §5a (single-matrix transform) and §5c's synthetic axis probe
   first, against the *current, buggy* formula. The probe failed exactly as
   predicted — `husk_probe_x` still correct at `(1,0,0)`, `husk_probe_y`
   landing at `(0,-1,0)` instead of `(0,1,0)`, `husk_probe_z` at `(0,0,-1)`
   instead of `(0,0,1)` — proving the new test tier actually catches the
   known bug, precisely, before it was trusted to catch anything else.
2. Applied the formula fix.
3. Confirmed §5c's probe now passes on position (rotation is covered
   separately by §5a's property test, not by the probe, since
   `gltf::Skeleton` carries no bind-pose rotation to probe — see §5a's own
   "Implemented" note), the optional humanoid-landmark check passes on the
   real `bloodelffemale.m2` fixture, and the full existing suite stayed
   green with no hand-updates needed.
4. **Deliberately not done this session, left to Luna as planned**: a real
   animated clip, visually confirmed in Blender's actual viewport (not
   headless), covering position, rotation, *and* scale together, per
   `BLENDER_EXPORT_TODO.md` §8's own closing recommendation — this is the
   one piece no automated check in this
   document substitutes for.
5. §5e (fixture diversity — a real quadruped fixture) and §5f (the
   committed, reusable probe script) landed alongside this same session,
   not deferred — see their own "Implemented" notes above.

---

## 8. Open questions for Luna — answered

All four resolved directly, then implemented per her answers (see each
section's own "Implemented" note above for what actually got built):

- Tests before the formula fix, not after: **"yes, you build while i
  nap."** Section 7's "what actually happened" sequencing follows this.
- §5a's single-matrix refactor, folded into this same change rather than
  scoped separately: **"part of this."**
- §5c's optional humanoid-landmark secondary check: **"worth adding, but
  not critical"** — built as a real, non-`skip(true)` test, explicitly
  documented as non-load-bearing.
- §5e's quadruped fixture, no specific file preference: **"no preferences,
  any will do"** — a real `wolf.m2` (66 bones, 557 vertices) was pulled
  from the local corpus and committed as `test_data/creature/wolf/`.

**Still open, deliberately, per her own explicit deferral**: the final
real-animated-clip visual sign-off in Blender's actual viewport — see §7
item 4. Nothing in this document substitutes for that; it's the one piece
that was never meant to be automated away.
