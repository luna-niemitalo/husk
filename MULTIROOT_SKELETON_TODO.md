# MULTIROOT_SKELETON_TODO — pre-mortem for representing M2's multi-root bone forests in glTF

**Framing, stated up front because it shapes everything below**: a
multi-root bone array is not a defect in the M2 data. WoW's engine has no
requirement that bones form a single tree -- a forest of independent
attachment points (particle-emitter anchors, accessory/utility bones) is
common, legitimate M2 shape, not corruption (see the real-data section
below: 35% of the corpus). The actual gap is that core glTF's skinning
model -- and tooling built assuming it, `gltf_validator` included --
generally expects a skin's joints to share one common root. **This file is
about closing a representation gap in husk's M2→glTF translation, not
"fixing" the source data.** That reframes the goal precisely: get as close
to a faithful 1:1 representation as glTF allows, adding the minimum
glTF-side scaffolding needed for tooling compatibility, without altering
or dropping any real M2 fact (bone position, parent relationship, or
index) to get there. Every design option below is judged against that bar,
not against "make the validator happy."

`gltf_validator` flags `SKIN_NO_COMMON_ROOT` ("Joints do not have a common
root") on some real exports whenever an M2's bone array contains more than
one bone with `parentBone == -1`. `buildSkeleton` (`src/cmd_export.cpp`)
parents every bone under its real M2 parent and stops there -- a bone
forest, not a tree, exactly mirroring the source. That fidelity is the
right default; the question this file is about is what (if anything)
needs to be added on the glTF side so downstream tooling doesn't misbehave
on a shape M2 allows but glTF's common tooling doesn't expect.

**Corpus-scale scope, measured directly (`tools/find_multiroot_skeletons.py`,
`multiroot_skeleton_files_for_exploration.txt`, repo root)**: **45,804 of
130,576 real `.m2` files (35%)** have more than one root bone by raw
`parentBone` parsing. That's the real denominator for this problem — far
bigger than the handful of fixtures found by accident in earlier sessions.

**But `gltf_validator` catches only a narrow slice of it, empirically —
raw root count is a poor predictor of whether a given file currently shows
the error.** Two findings that forced this correction, found while
generating the number above:

- `bloodelffemale.m2` — the project's own primary, most heavily-tested
  fixture — has **90 root bones out of 119** (23 of them actually
  referenced by a skinned vertex with nonzero weight), and exports with
  **zero** `gltf_validator` errors.
- `offhand_1h_revendreth_d_01.m2` has only **10 root bones** (2 vertex-
  referenced) and *does* trigger `SKIN_NO_COMMON_ROOT`.

More roots, more vertex-referenced roots, and still no error on one file;
fewer of both, and it errors on the other — neither "raw root count" nor
"root count among vertex-weighted joints" explains the difference. The
validator's specific check is catching something narrower than "this skin
is a genuine bone forest," and what exactly wasn't reverse-engineered this
session (out of scope for a pre-implementation survey; see Open questions
below).

**A real random sample confirms the gap is large.** 150 files drawn from
the 45,804-file list, each actually exported via `husk export` and checked
with the real `gltf_validator` (not the proxy script) — all 150 exported
successfully, and **11/150 (≈7.3%) showed `SKIN_NO_COMMON_ROOT`**.
Extrapolated (not a rigorous confidence interval, just a real sample rate),
that's on the order of ~3,300 files across the full corpus the validator
would flag *today* — a small fraction of the 45,804 files that are
actually structurally multi-root. Interesting, unexplained pattern in the
11 hits: 9 were the same base item across different race/gender variants
(`helm_mail_zuldazarraidmythic_d_01_*`), plus one other helm and one
creature — not a uniform random subset, suggesting some correlation with
model shape (bone count, hierarchy depth, something else) that wasn't
chased down.

**The conclusion this forces**: "passing `gltf_validator` with zero
errors" is not the same as "this model's joint hierarchy translates into
one glTF-tooling-friendly tree." The representation gap is real for the
*entire* 45,804-file/35%-of-corpus shape (every M2 with 2+ root bones,
faithfully represented as-is), not just the ~2-3% of files where
`gltf_validator` currently happens to notice. Scoping this to only
satisfy `gltf_validator` (e.g. testing exclusively against files known to
currently error) would under-scope it badly — whatever gets built needs
to trigger on `rootJointNodeIndices.size() > 1` regardless of whether that
specific file happens to be one the validator currently flags.

Not fixed yet anywhere in this repo. This file started as a pre-
implementation risk survey (given the fix touches `gltf.cpp`'s node-index
bookkeeping, which several other features already depend on being exactly
`meshCount + jointIndex`) and now also carries a decision — see **Decision**
below. Read `DESIGN.md`'s Key design decisions and `src/gltf.hpp`'s
`Skeleton`/`writeGlbMulti` doc comments before touching anything here;
they're the authoritative description of the invariants this file is
trying not to break.

---

## Decision: the glTF-native option (Option 1 below), chosen directly by Luna

**Chosen**: Option 1 -- a plain, non-joint glTF node synthesized as the
parent of every real root joint, never added to `skin.joints`, with
`skin.skeleton` pointed at it. Not Option 2 (fake extra joint) or Option 3
(filter out non-hierarchical bones, `wow.export`-style) -- see the Prior
art and Two open design questions sections below for the full comparison;
both are kept for the record, not deleted, since the reasoning still
matters even though the choice is made.

**Why**: this is the shape glTF's own tooling ecosystem was already
designed to handle -- a skeleton root's direct parent doesn't have to be a
joint itself, specifically to support multi-rooted skeletons (Prior art
section, the Khronos discussion). It's the option that best satisfies this
file's own stated fidelity goal (every real M2 bone preserved exactly,
nothing dropped, nothing turned into a fake bone) while still being a real,
spec-anticipated pattern rather than a husk-specific invention.

**What's still genuinely unverified, and deliberately not blocking the
decision**: Blender's *exact* import-time behavior with a non-joint parent
node above an armature (does `bone_count` grow by one anyway, does
`skin.skeleton` change anything visible) was never empirically checked
this session -- the original survey below treated that as a precondition
for deciding at all ("do not guess at this... before committing to one").
Luna's direct call overrides that: implement Option 1 now, verify Blender's
actual behavior as part of building the tests (Implementation plan step 4
below), and adjust the `bone_count`-style assertions to match reality
rather than to a prediction. If Blender's behavior turns out to make
Option 1 and Option 2 equivalent in practice, that's a real finding to
record here, not a reason the decision was wrong.

---

## Implementation plan (supersedes "Recommended first steps" below)

1. **`gltf.cpp`'s `writeGlbMulti`**: after the existing
   `rootJointNodeIndices` loop (the one already computing which joints have
   `parent == -1`), add: if `rootJointNodeIndices.size() > 1`, synthesize
   one `tinygltf::Node` with `.children = rootJointNodeIndices` (default/
   identity transform -- no translation/rotation/scale set, so it's
   transparent to every existing bind-pose/animation computation, see the
   invariant section below for why that matters), append it to
   `model.nodes` *after* every real joint node (index
   `meshCount + skeleton->joints.size()`), and use *that* node's index as
   the sole entry pushed into `scene.nodes` in place of the individual
   `rootJointNodeIndices` entries. Set `skin.skeleton` to that same index.
   When `rootJointNodeIndices.size() <= 1` (the overwhelming majority of
   real models), change nothing -- output must stay exactly what it is
   today.
2. **Do not touch `Skeleton::joints`, `cmd_export.cpp`, or
   `buildSkeleton`.** The synthetic node is never added to `skin.joints`,
   never gets an inverse bind matrix, and is invisible to every consumer
   that indexes `skeleton->joints` (vertex `JOINTS_0`, `EmitterAnchor`,
   `CorrectionSet`, `JointAnimation`) -- see the invariant section below,
   still the single most important thing this change must not violate.
3. **Update `src/gltf.hpp`'s doc comments** (`Skeleton`, `writeGlbMulti`)
   to describe the new synthesized-root behavior -- this project's own
   convention is doc comments as the authoritative contract, not just
   prose in a TODO file.
4. **Tests, in this order** (earlier ones inform later ones -- don't
   write the Blender assertion blind):
   - `test_gltf.cpp`: a new multi-root fixture (3+ independent joints, no
     shared parent -- extend the existing `buildChainSkeleton`-style
     helpers, don't reuse it as-is since it's single-root by construction).
     Assert: exactly one synthetic node exists, its `.children` matches
     `rootJointNodeIndices` exactly, `skin.joints.size()` stays exactly
     `skeleton->joints.size()` (no bogus extra entry -- this is the
     concrete, checkable difference from Option 2), `skin.skeleton` equals
     the synthetic node's index, and the synthetic node's own transform is
     untouched/default (translation/rotation/scale all absent -- guards
     against the "stray transform silently shifts every root's subtree"
     failure mode below).
   - `test_gltf.cpp` regression: every existing single-root skeleton test
     (`buildChainSkeleton`-based) must still produce identical node counts/
     indices/`scene.nodes` contents -- run the full suite and diff, don't
     just trust the `<= 1` gate by inspection.
   - `test_conformance.cpp`: a new case using `testWeaponParticleB()`
     and/or `testWeaponParticleStress()` (both real, already-wired-up
     multi-root fixtures) through the real `gltf_validator` -- assert
     `SKIN_NO_COMMON_ROOT` no longer appears in the output, every other
     check still passes. Then through the real headless-Blender probe
     (`tests/blender_import_check.py`) -- record what `bone_count` and
     `armature_count` actually report, and *only then* write the
     assertion to match (this is the empirical check the Decision section
     above explicitly deferred to here, not before).
   - One `--lod all` + multi-root combination case, and one `--bones-dir`
     + multi-root combination case (both real, untested combinations --
     see Concrete failure modes below).
5. **Docs once the above is green**: `DESIGN.md`'s Key design decisions
   (a new bullet, matching this session's other three from the same
   sweep), `M2_COMPLETENESS.md`'s "Skeleton / bone hierarchy" row (still
   `native — 100%`, but the note should mention multi-root synthesis now
   exists), `CLAUDE.md` Resume, and this file's own top framing (mark the
   Decision section's "still genuinely unverified" paragraph resolved,
   with whatever Blender actually reported).

---

## Open questions this session didn't chase down

- **What does `gltf_validator` actually check for `SKIN_NO_COMMON_ROOT`?**
  Not reverse-engineered this session -- checked two plausible hypotheses
  directly against real data (raw root count; root count restricted to
  joints referenced by a nonzero-weight vertex) and both were falsified by
  `bloodelffemale.m2` vs. `offhand_1h_revendreth_d_01.m2`. Worth reading
  the validator's actual source/spec text before implementing, both to
  understand what's being measured and because it might hint at what glTF
  tooling in general (not just this one validator) treats as "close
  enough" -- relevant to design question A below.
- **Why did the 150-file sample's 11 hits skew toward one item family?**
  Not investigated -- could be bone-count-dependent, hierarchy-shape-
  dependent, or coincidental at this sample size. A larger sample (the
  full 45,804, or a few thousand) would give a more reliable rate and
  might reveal the actual correlation.

---

## Prior art: how others have handled this (bounded research, not exhaustive)

Two real, concrete data points found via web search -- deliberately kept
short, not a deep investigation:

- **glTF's own spec/tooling explicitly anticipates this shape.** Per
  Khronos-side discussion of `skin.skeleton`: "the direct parent of the
  highest joint in the skin hierarchy must be used [if `skeleton` is
  unset] ... The direct parent does not have to be a joint, to allow for
  multi rooted skeletons." This is exactly design question A's "Option 1"
  below (a plain non-joint parent node) -- not a hack invented for this
  project, but the behavior glTF's own tooling ecosystem was already
  designed to handle. Strengthens Option 1 as the more spec-aligned
  default, though Blender's *specific* importer behavior with it still
  wasn't verified empirically this session (see design question A).
  [Source](https://github.com/KhronosGroup/glTF/issues/1270) (the
  discussion thread; the resolution/final spec wording wasn't confirmed
  directly, only the quoted community-summarized position).
- **`wow.export`** (Kruithne/wow.export, the most established general-
  purpose WoW model export toolkit, M2→glTF-with-armature support since
  v0.1.57) **added a user-facing toggle for exactly this shape**: v0.2.0's
  changelog reads "Added option that controls the inclusion of prefix
  bones in glTF exports." "Prefix bones" reads as the same shape this
  session found in real data (`bloodelffemale.m2`'s 89 non-hierarchical,
  `keyBoneId == -1` single-node roots alongside the one real 15-bone
  hierarchy) -- i.e. their answer isn't "synthesize a common root," it's
  "let the user choose whether to include the non-hierarchical utility/
  attachment bones in the exported armature at all." Not independently
  confirmed against `wow.export`'s actual source (out of scope for a
  bounded search) -- inferred from the changelog wording alone.
  [Source](https://github.com/Kruithne/wow.export) /
  [CHANGELOG](https://github.com/Kruithne/wow.export/blob/main/CHANGELOG.md).

**A third design option this suggests, worth weighing against the two
below**: **Option 3 -- an opt-in/opt-out filter that excludes non-
hierarchical "prefix"/attachment-only bones from the exported skeleton
entirely**, `wow.export`-style, rather than synthesizing a wrapper node
that keeps every bone. Directly in tension with this file's own stated
1:1-fidelity goal (above) -- filtering *drops* real M2 bones from the
glTF output, where wrapping (Options 1/2) keeps every one and only adds a
harmless connective node. `wow.export` making this a *toggle* (not an
unconditional default either way) suggests both audiences are real --
worth considering a similar toggle in husk (matching the existing
`--bones-dir none`/`--skin-dir none` "explicit opt-out, on by default"
CLI convention) rather than picking one behavior unconditionally, but this
needs its own design pass, not a decision baked into this survey.

---

## The one invariant that must never break

**`Skeleton::joints`' ordering and indices are raw M2 bone-array indices,
copied verbatim, at every single consumer — never remapped, never
validated against anything but bounds.** Concretely:

- `buildSkinning` (`src/cmd_export.cpp`) copies `M2Vertex.bone_indices[4]`
  straight into `gltf::JointWeights.joints[4]` — these become glTF's
  `JOINTS_0` attribute, which indexes into `skin.joints[]` by position.
- `Skeleton::EmitterAnchor::joint` (ribbon/particle placement) and
  `Skeleton::CorrectionSet::Correction::joint` (`.bone` corrections) are
  both raw M2 bone-array indices too, bounds-checked against
  `skeleton->joints.size()` in `writeGlbMulti` but never remapped.
- `JointAnimation::joint` (animation clips) is the same.
- `writeGlbMulti` itself builds `skin.joints[i] = meshCount + i` and every
  inverse bind matrix by iterating `skeleton->joints` **in order, once**,
  with no reordering anywhere.

**If a synthetic node is ever inserted into `Skeleton::joints` itself**
(e.g. prepended at index 0 so it can be "the first joint"), every one of
the four consumers above goes silently, invisibly wrong: still in-bounds
(so no exception fires), but pointing at the *wrong bone* — a vertex
deforms with a neighboring bone's transform instead of its own, an emitter
anchors to the wrong point, a `.bone` correction applies to the wrong
joint. This would not crash, not fail any bounds check, and not show up in
`gltf_validator` (which only checks structural validity, not *which* joint
a vertex is skinned to) — it would just look subtly wrong in Blender, or
not even that if the affected bone happens to be visually similar to its
neighbor. **This is the single most important thing to get right**, and
the reason the fix must live entirely inside `gltf.cpp`'s `writeGlbMulti`,
touching only glTF-side node/skin construction — `Skeleton::joints` itself,
and everything upstream of it in `cmd_export.cpp`, must stay untouched, in
the exact order the M2's own `bones` array already has.

Any synthetic node this fix adds must be appended **past the end** of the
existing joint-node index range (`meshCount + skeleton->joints.size()`,
the first free index), never inserted before or between real joints.

---

## Real data: multi-root is common, intentional M2 data, not corruption -- and can mean *every* bone is a root

Checked two real fixtures directly (raw bone-array bytes, `parentBone`
field, not through husk's own code -- an independent read, same
discipline `WIKI_FINDINGS.md`'s other findings use), already wired up as
real test fixtures (`tests/test_data_paths.hpp`'s
`kWeaponParticleB`/`testWeaponParticleB()` and
`kWeaponParticleStress`/`testWeaponParticleStress()`):

- **`offhand_1h_revendreth_d_01.m2`** (`HUSK_TEST_WEAPON_PARTICLE_B`): 15
  bones, **10 roots**. Three real small hierarchies (subtree sizes 2, 3,
  3) plus seven single-bone roots with no children at all. A genuinely
  mixed shape -- one or two "real" sub-skeletons plus several independent
  attachment points.
- **`mace_2h_bolvar_d_01.m2`** (`HUSK_TEST_WEAPON_PARTICLE_STRESS`, the
  64-particle-emitter stress fixture): 78 bones, **78 roots** -- every
  single bone is its own tree, subtree size 1 each. There is no hierarchy
  at all in this file; it's a flat list of independent attachment points
  (consistent with "one bone per particle emitter, no relationship between
  them needed"). 66 of the 78 have `M2CompBone.flags & 0x200` set
  (wowdev.wiki: `transformed`, meaning not further investigated this
  session -- not confirmed what distinguishes the 66 from the other 12
  bare-flag-0x0 roots).

**Conclusions worth designing around:**

1. Multi-root is real, common, intentional M2 data -- not a parsing bug
   and not something to "fix" on the M2 side. The fix belongs entirely at
   the glTF-export boundary, purely for glTF-tooling compatibility.
2. Root count can be *large* -- not "usually 2, sometimes a few." A
   synthetic wrapper node needs to comfortably parent dozens of children,
   not just 2-3.
3. The shape varies file to file (mixed hierarchy+isolated-bones vs.
   fully-flat) -- any fix needs to handle both without special-casing one
   shape over the other.
4. Not yet characterized: whether the 10 geometry-less-VFX files found
   this session share either of these two shapes, a third shape, or vary
   among themselves. Worth doing before implementing -- see Recommended
   first steps below.

---

## Two design questions -- now decided (Decision section above), kept here for the reasoning trail

### A. Does the synthetic node join `skin.joints`, or stay a plain parent node outside the skin? — **Decided: Option 1**

Three real options now, weighed against this file's own 1:1-fidelity goal
(stated at the top) -- Options 1/2 keep every real M2 bone; Option 3
doesn't:

- **Option 1 -- plain node, not a joint.** Append one `tinygltf::Node`
  after all real joint nodes (index `meshCount + jointCount`), give it
  `.children = ` every node index that used to go directly into
  `scene.nodes` (the real roots), and put *that* node's index into
  `scene.nodes` instead. Never added to `skin.joints`, never gets an
  inverse bind matrix. Minimal footprint -- `skin.joints.size()` stays
  exactly `header.bones.count`, matching every existing invariant/test
  that currently assumes that. **Has real glTF-ecosystem precedent** --
  see Prior art above: glTF's own tooling explicitly anticipates a
  skeleton root's direct parent not being a joint itself, precisely for
  multi-rooted skeletons.
- **Option 2 -- append it as one more real joint.** Same node, but also
  appended to `skin.joints` (at the end, index `jointCount`, past every
  real M2 bone index -- safe per the invariant above, since no vertex/
  anchor/correction/animation index ever reaches that value) with an
  identity inverse bind matrix (it has no real bind-pose offset -- sits at
  the world origin). Matches the "root/Armature bone" pattern several
  other real-world exporters use.
- **Option 3 -- filter out non-hierarchical bones instead of wrapping
  them** (`wow.export`'s apparent approach, see Prior art above -- an
  opt-in/opt-out toggle, not synthesis at all). Diverges from this file's
  own fidelity goal (drops real bones from the glTF skeleton) but has
  real community precedent as a *user-controllable* choice, not an
  unconditional default. Worth a real design pass of its own if pursued,
  not folded into A/B here.

**Originally why this wasn't decided here** (kept for the record --
overridden by Luna's direct call, see Decision above): which one Blender's
glTF importer treats as "a bone" seemed like the deciding factor, and
that's an empirical question, not a spec-reading one --
`tests/blender_import_check.py`'s `bone_count` probe
(`len(armature.data.bones)`) is a real, already-wired-up way to check it
directly, but doing so needs a real synthesized fixture to run through it,
which doesn't exist without writing the code first. The original
recommendation was to build a throwaway spike and check Blender's actual
behavior before picking -- Luna chose to implement Option 1 directly
instead (it's the spec-anticipated, fidelity-preserving choice regardless
of Blender's specific bone-counting quirks) and verify Blender's real
behavior as part of building the real tests (Implementation plan step 4).
If Option 1 still produces an extra Blender bone anyway (i.e. Blender's
importer builds bones for the whole node hierarchy under an armature
regardless of `skin.joints` membership -- plausible, not confirmed), that's
a real finding to record here once known, not a reason the choice was
wrong.

### B. Should `skin.skeleton` be set to the synthetic node? — **Decided: yes**

`writeGlbMulti` currently never sets `tinygltf::Skin::skeleton` at all
(left at its default/unset). **Now set to the synthetic node's index
whenever one is synthesized** (single-root models: still unset, unchanged
behavior) -- this is literally the scenario the Khronos discussion (Prior
art) describes: an explicit skeleton-root hint whose direct parent isn't
itself a deforming joint. Caveat carried over honestly: this session never
verified the spec text closely enough to be fully confident in that
reading, nor checked whether `gltf_validator`/Blender's importer actually
behave differently with `skin.skeleton` set vs. unset -- setting it is the
better-justified default given the Prior art finding, but Implementation
plan step 4's real `gltf_validator`/Blender runs are also the place this
gets a real empirical check, not just a spec-reading one.

---

## Concrete places this could fail invisibly (no crash, no validator error, just wrong)

- **The joints-array-corruption failure mode described above**, if the
  synthetic node ever ends up inside `Skeleton::joints` (the M2-domain
  struct) instead of purely in `gltf.cpp`'s glTF-side node construction.
  The highest-severity, most-invisible failure this rework could
  introduce -- re-read the invariant section above before writing any
  code that touches `Skeleton::joints`'s length or order.
- **A single-root model changing output at all.** The fix must be
  strictly conditional on `rootJointNodeIndices.size() > 1` -- the
  overwhelming majority of real models have exactly one root, and every
  existing `test_gltf.cpp` skeleton test (`buildChainSkeleton`-based, one
  root) hard-codes exact node counts/indices assuming no synthetic node
  exists. Applying the synthesis unconditionally, or getting the gating
  condition off by one, silently changes every one of those models' node
  layout and would need every existing skeleton-shape test re-verified,
  not just the new multi-root ones.
- **`tests/test_conformance.cpp`'s exact-match assertions**
  (`model.skins[0].joints.size() == header.bones.count`,
  `bone_count == joints.size()`) only run against the single-root
  bloodelf character fixture today -- a regression on a multi-root model
  wouldn't be caught by *any* currently-passing test, since none of them
  exercise a multi-root real file through the Blender readback path at
  all. Needs a new test using `testWeaponParticleB()`/
  `testWeaponParticleStress()` specifically, not just an extension of the
  existing single-root one.
- **Bind-pose math silently drifting if the synthetic node ever gets a
  non-identity transform** by accident (e.g. a copy-paste that reuses a
  real joint's translation). Each existing root joint's own
  `localTranslation` already equals its *absolute* bind-pose position
  (`buildSkeleton`'s `parent == -1` branch) -- correct only as long as
  whatever parents it now has a true identity transform. A stray
  translation/rotation/scale on the synthetic node would shift or rotate
  every formerly-root joint's subtree by that amount, silently, with no
  validator or bounds check catching it (it'd just look wrong, or subtly
  wrong, in Blender).
- **`--lod all` (shared skeleton across multiple mesh nodes) +
  multi-root, untested combination.** The skeleton-building block runs
  once, shared by every `NamedMesh` entry, so the fix naturally applies
  uniformly -- but this specific combination (multiple LOD tiers *and* a
  synthesized root) has no real fixture or test today and should get one,
  not be assumed to "just work" because each half works separately.
- **`--bones-dir` (`.bone` correction data) + multi-root, untested
  combination.** `CorrectionSet::Correction::joint` indices are raw M2
  bone indices (same invariant as above) so should be unaffected in
  principle -- but "should be unaffected in principle" is exactly the
  kind of claim this whole file exists to *not* leave unverified. Needs a
  real combined fixture.
- **Downstream tooling (Blender scripts, a custom renderer) that
  currently assumes `skin.joints[0]` or the first `scene.nodes` armature
  entry is meaningful** (e.g. "the root bone") -- not a husk-internal
  risk, but worth a README/DESIGN.md callout once implemented: with
  Option 1 chosen, `skin.joints` itself never grows an extra entry (a
  smaller behavior change for existing consumers than Option 2 would have
  been), but `scene.nodes`'s top-level armature entry *does* change, for
  multi-root models, from "one of several real joints" to "a new synthetic
  node" -- worth flagging in the same doc pass as step 5 of the
  Implementation plan.
- **A silent semantic misread of `M2CompBone.flags & 0x200`
  ("transformed")** -- not confirmed what distinguishes the 66 flagged
  vs. 12 unflagged roots in the stress fixture; if a future design ends up
  wanting to treat root bones differently based on this flag (e.g.
  "billboard-like utility bones vs. real attachment points"), that
  distinction needs its own real-data investigation first, per this
  project's own "don't guess at semantics" rule -- not assumed from the
  flag's wiki name alone.

---

## Recommended first steps, in order (superseded by Implementation plan above -- kept for the historical reasoning trail)

1. ~~Characterize the shape more broadly before designing anything.~~
   **Done, corpus-wide** -- `tools/find_multiroot_skeletons.py` +
   `multiroot_skeleton_files_for_exploration.txt` (45,804 files, real
   `gltf_validator` sample rate ≈7.3%, see above). What's *not* done:
   subtree-size-distribution/model-type correlation across that full list
   (only 2 files got that level of detail) -- worth revisiting if the
   design ends up needing to special-case shapes (e.g. "fully flat, every
   bone a root" vs. "one real hierarchy plus a few strays").
2. **Answer design question A empirically** (see above) with a throwaway
   local spike against a real fixture and the real Blender probe, before
   writing the real implementation.
3. **Check the glTF spec text and `gltf_validator`'s own source/docs for
   `skin.skeleton`'s exact semantics** (design question B) -- this wasn't
   done this session and shouldn't be guessed at.
4. Only then: implement in `gltf.cpp`'s `writeGlbMulti`, strictly gated on
   `rootJointNodeIndices.size() > 1`, touching nothing in
   `cmd_export.cpp`/`Skeleton::joints` itself.
5. New tests, at minimum: `writeGlbMulti` unit tests in `test_gltf.cpp`
   (synthetic fixture with 3+ independent roots, asserting the synthetic
   node's `.children`, that `skin.joints`/IBM count matches whichever
   option was chosen, and that a single-root fixture's output is
   byte-identical to before), a `test_conformance.cpp` case using
   `testWeaponParticleB()`/`testWeaponParticleStress()` through the real
   `gltf_validator` (asserting `SKIN_NO_COMMON_ROOT` is gone) *and* the
   real Blender probe (asserting `bone_count` matches whatever the chosen
   design predicts, not just "no crash"), and one `--lod all` +
   multi-root combination case.
6. Docs once implemented: `DESIGN.md`'s Hazards note (currently describes
   this as unfixed) and Key design decisions (the two questions above,
   answered), `M2_COMPLETENESS.md`'s "Skeleton / bone hierarchy" row
   (currently `native — 100%` -- true for single-root models, was quietly
   not true for multi-root ones; worth a note either way this resolves),
   `CLAUDE.md` Resume.
