# TODO: verify and apply `.bone` correction-matrix semantics (Blender-side)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Background

A now-closed, deleted TODO (the geoset-selection work, see git history and
`TODO_correctness.md` #2's own detail) closed the *selection* half of
`.bone` corrections: `husk export --db2-dir/--dbd-dir/
--customization-choice-ids` (`src/chrcustomization_db2.hpp`) now resolves
a real `ChrCustomizationChoiceID` all the way to a real `.bone`
`BoneFileDataID` and marks the matching `--bones-dir`-resolved
`CorrectionSet` with `selected_by_choice_ids` extras. Given a real
character's customization choices, a consumer can now know *which* of a
model's several `.bone` files is the correct one with no external data
missing anymore.

**What's still missing is not architectural.** An earlier draft of that
now-deleted TODO wrongly framed this as blocked by "husk never applies
`.bone` corrections to the render" — that boundary is real for the
`husk export` C++ binary (`DESIGN.md`'s Key design decisions: husk reads
formats and writes glTF, it doesn't render), but `tools/
husk_blender_geoset_mask.py` is not bound by it — it already applies
husk-exported extras to real Blender rendering for billboard alignment,
texture-transform animation, tint/fade curves, and geoset switching. It is
exactly "a downstream renderer or Blender script that does have the
slot-selection mapping to apply on top," per `DESIGN.md`'s own original
phrasing for what `bone_correction_sets` was waiting for. There is no
remaining reason not to build a consumer, once the actual application
math is trustworthy.

## The real, narrower blocker: application semantics were never verified

`src/bone.hpp`'s own doc comment (reverse-engineered, no wowdev.wiki spec
exists for `.bone` at all) says only that `BOMT`'s matrices are "a small
corrective delta transform... not a full replacement pose," inferred from
byte shape (near-identity 3x3, `(0,0,0,1)` last column) — never confirmed
against real client rendering. Open, unconfirmed questions any real
application needs answered first:

- **Composition order**: is the correction pre-multiplied or
  post-multiplied against the bone's existing bind-pose local transform?
- **Space**: is the delta expressed in the bone's own local space, its
  parent's space, or full model space?
- **Parent-chain interaction**: does the correction apply before or after
  the bone's position in its parent chain is resolved — i.e. does
  correcting bone N visibly shift bone N's children too (expected for a
  "wrist correction" that should carry the hand with it), or is it meant
  to be purely local?

Applying an unverified composition risks a *confidently wrong* result —
the same failure shape this project has already hit twice: the additive-
blend `unlit`-co-occurrence bug, and the billboard-alignment math that
shipped explicitly flagged "never ground-truthed against real client
behavior" until it was. Both were caught by real comparison, not by
re-reading the code more carefully.

## Concrete next steps

1. **Pick a real, visually-legible repro.** `WIKI_FINDINGS/BONE.md`'s
   existing investigation already narrowed real corrected bones to a
   small set clustered on Head/Jaw, with corrections that are pure
   magnitude scales along one of two fixed directions — a good candidate
   is whichever real `bloodelffemale_hd` `.bone` slot produces the
   *largest* such scale (more visually obvious than a subtle one), cross-
   referenced against a real `ChrCustomizationChoiceID` that selects it
   (now directly checkable via this session's own `--customization-choice-ids`
   work — resolve a choice, confirm it selects a `.bone` file with a
   large correction, before building anything).
2. **Get a real ground-truth reference.** Same method the billboard-
   alignment work eventually needed: a real client screenshot (or WMV/
   `reference/wow.export` render) of that exact character/customization
   combination, showing the corrected region clearly (Luna's own
   interactive step — this isn't something to guess at or automate away,
   per this project's own standing precedent for this class of question).
3. **Build a small, disposable headless-Blender probe** (scratchpad only,
   not committed, same tier as this project's other one-off verification
   scripts) that tries the plausible composition orders/spaces from the
   open questions above against the real correction matrix, and compares
   each candidate's resulting joint position/orientation against the
   reference from step 2 — not "does it look plausible," a real numeric or
   visual match against known-correct output.
4. **Once one composition is confirmed correct**, wire it into
   `tools/husk_blender_geoset_mask.py` as a real, permanent consumer of
   `selected_by_choice_ids`: for each marked `CorrectionSet`, apply its
   `(bone_index, matrix)` pairs to the matching bone using the now-verified
   composition, gated behind the same kind of stage wrapper
   (`_run_stage`) every other stage in this script's `main()` already
   uses.
5. **Verify the wired version headlessly** against the same repro from
   step 1, confirming it reproduces step 2's reference — not just that it
   runs without crashing.

## Why this is its own file

The DB2-selection work this grew out of (resolving *which* `.bone` slot
applies) is fully done and its own TODO deleted per this project's own
convention. This is a structurally different, still-open kind of task —
an unverified-math investigation gated on a real human comparison step,
not a data-plumbing task — so it gets its own file rather than being
folded into a done-and-removed one, same "one punch list per open
problem" convention `DPIV_TODO.md`/`PIXEL_SHADER_FORMULAS_TODO.md` already
follow (a real, scoped, but human-verification-gated follow-up split out
from whatever investigation found it).
