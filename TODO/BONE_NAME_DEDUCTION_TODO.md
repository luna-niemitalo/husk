# TODO: bone-name deduction, tier 2 (reference-skeleton matching)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Background

`husk export`'s glTF joint names come from a priority chain
(`src/export_skeleton.cpp`, `buildSkeleton` + `applyContextualBoneNames`):

- **Tier 0** (implemented): `m2::Bone::keyBoneId` → a real Blizzard name via
  `m2::keyBoneName` (wowdev.wiki's 193-row Key Bone Lookup table). Real data,
  used as-is.
- **Attachment tier** (implemented): a bone with an `M2Attachment` pointing
  at it gets named via `m2::attachmentTypeName` (wowdev.wiki's 61-entry
  Attachment Lookup table — Shield, HandRight, Helm, Head, Chest, Breath,
  etc.) — same authority level as tier 0, just a different real per-file
  source. Verified against the real `bloodelffemale.m2` fixture: attachment
  IDs there map to bones 51-90, all previously bare `bone_<index>`.
- **Event tier** (implemented, weaker signal): a bone with an `M2Event`
  pointing at it and no attachment name gets named via `m2::eventName`
  (wowdev.wiki's "Possible Events" table, ~65 documented codes). Weaker
  than the attachment tier because the source table itself has real
  undocumented gaps (`$CHD`, `$CVS`, `$KVS`, `$WWG`, and the non-`$`-prefixed
  `DEST`/`POIN`/`WHEE`/`BOTT`/`TOP` oddities — the wiki's own "purpose
  unknown"), left unnamed rather than guessed at. Verified against the real
  `bloodelffemale.m2` fixture: event codes there map to bones 91-118.
- **Tier 1** (implemented, `deduceBoneNamesByTopology`): for a bone with no
  name from any tier above, borrow one only in the narrow, unambiguous case
  of a simple (non-branching) run of unnamed bones directly between one
  already-named ancestor and one already-named descendant — e.g. an unnamed
  bone between `ForearmL` and `HandL` becomes
  `bone_<i>_betweenForearmL_HandL`. Deliberately structural, not anatomical:
  never invents vocabulary (Wrist/Elbow/Knee/...) that isn't actually in the
  file. A branch or a dead end (no named descendant reachable) leaves every
  bone on that path unlabeled. Runs last so it can use attachment/event-
  derived names as landmarks too, not just tier-0 ones.
- **Tier 2** (this file, not started): a bone none of the above reached —
  no attachment, no event, and either a branch point or no named landmark
  nearby for tier 1 — falls through to a plain `bone_<index>` today. Real
  anatomical names (elbow, knee, spine segments, finger bones, ...) are
  only reachable at all by comparing this model's skeleton against a
  *reference* skeleton that has them.

**Real yield on `bloodelffemale.m2` (119 bones, 108 unnamed before any of
this)**: the attachment + event tiers alone took unnamed bones from 108
down to 48 — more than half. Tier 1 found zero on this specific fixture
(zero clean, unbranching, dually-landmarked gaps exist in it) but did find
2 on the larger `bloodelffemale_hd.m2`/`.skel` fixture (245 bones). The
originally-reported wrist bone (`bone_29`, a direct child of `ForearmR`)
still isn't named by anything above — no attachment or event targets it,
and it's a dead end (no children) so tier 1 has no descendant to interpolate
toward. **Tier 2 is the only remaining path to naming it** — this is the
real motivating case for actually building tier 2, not just a nice-to-have.

**Real yield on `bloodelffemale_hd.m2`/`.skel` (245 bones -- the actually-
current-content skeleton; the plain SD model is legacy data nobody's really
shipped against in a long time, comparable to pre-Cata) is proportionally
*worse*, not better: only ~39% named (95/245) versus ~60% on the SD model.
One genuine cross-tier win did show up here that didn't exist before the
attachment/event tiers landed: `bone_236_betweenSpellHandL_
PlaySoundKit_spellCastDirected` -- tier 1 chained an attachment-derived
landmark (`SpellHandL`) to an event-derived one (`$SCD`), confirming the
"run tier 1 last so it can use attachment/event names as landmarks too"
design decision produces real value, not just theoretical value.

The remaining 152 unnamed HD bones aren't evenly spread -- one contiguous
block, `bone_117`-`bone_173` (57 bones), dominates. Traced its ancestry: it
hangs directly off the real `Head` joint, almost certainly WoW's per-model
facial-animation bone expansion (lip/eye/brow control bones added for HD
character models). These were never attachment points or event targets to
begin with, so no signal husk has today, or that wowdev's own tables
document, can name them individually.

**Correction (checked directly, not assumed): Rigify's bundled human
meta-rig does have a real, extensive facial rig** -- instantiating
Blender 5.1.1's bundled Rigify (v0.6.10, already reachable via
`HUSK_BLENDER`) "Human (Meta-Rig)" template gives 159 bones, 84 of them
face-related: eyes, eyelids (upper/lower, both sides), brows (upper/lower,
both sides), lips, jaw, nose, chin, ears -- all real, individually named
(`lid.T.L.001`, `brow.B.R.002`, `lip.T.L`, ...). An earlier draft of this
file claimed the opposite; that was wrong, not verified before being
written down. So tier 2 scoped against Rigify plausibly *can* reach this
facial-bone block too, not just limb bones like `bone_29` -- the open
question is match quality (84 generic facial control bones vs. this
model's own 57, not a 1:1 correspondence), not "no reference exists at
all." Only one human sample template was found in this Rigify version (no
separate "with/without face bones" toggle at the metarig-template level);
that distinction may exist in a different/older addon or a different stage
of Rigify's own workflow, not confirmed either way.

**A same-species reference was also considered and ruled out at the naive
level**: comparing `bloodelffemale.m2` (SD) against `bloodelffemale_hd.m2`
(HD) bone-for-bone by raw index does *not* work -- checked directly, SD's
bone 29 (the unnamed wrist) and HD's bone 29 (`ArmL`, a completely
different joint) share nothing; the two skeletons are independently
authored bone arrays, not the same layout with HD appending bones at the
end. A *smarter* version of the same idea is still worth keeping on the
list, though: using a model's own higher-detail sibling (when one exists,
e.g. an SD/HD pair) as tier 2's reference skeleton, matched by the same
position/hierarchy algorithm below, instead of or alongside Rigify. Its
advantage over Rigify is real: both sides already share WoW's own bone
vocabulary, so step 1 below (the hand-maintained name-translation table,
the most fragile part of the Rigify approach) disappears entirely for any
model that actually has such a sibling. Its downside: most models don't
have one, so it can't be the *only* reference tier 2 supports.

## Tier 2 approach

Match remaining unnamed bones against a **reference skeleton** by
hierarchy/relative-position similarity, using the tier-0/attachment/event-
named bones as known correspondence anchors. Two candidate references,
not mutually exclusive:

- **Blender's bundled Rigify human meta-rig** — generic, but real and
  detailed (159 bones including a full 84-bone facial rig, confirmed by
  direct instantiation, see above). Works for any model. Needs step 1
  below (a hand-maintained WoW-name ↔ Rigify-name translation table) since
  the two skeletons don't share vocabulary.
- **A model's own higher-detail sibling** (e.g. an SD/HD pair like
  `bloodelffemale.m2`/`bloodelffemale_hd.m2`), when one exists — both
  sides already share WoW's own bone vocabulary (`ForearmL` means the same
  thing on both), so step 1's translation table isn't needed at all for
  models that have one. Only covers models with a known sibling, so can't
  be the sole reference tier 2 supports.

Algorithm (same shape regardless of which reference is used):

1. For each already-named M2 bone (any tier above), find its counterpart
   in the reference skeleton — by a hand-maintained name-translation table
   (Rigify case) or directly by matching name (sibling-skeleton case).
   This gives a set of known-good (M2 bone, reference bone) anchor pairs.
2. For each remaining unnamed M2 bone, compute a position relative to its
   nearest anchor(s) — hierarchy distance (how many bones up/down the
   chain) and/or normalized spatial position between two anchors — and find
   the reference bone with the most similar relative position to *its*
   nearest anchors.
3. Only accept a match under a similarity threshold; below it, leave the
   bone unlabeled rather than guessing (same "no claim beyond what's
   supportable" discipline as tier 1). Define a policy for a tie between
   two similarly-close reference bones (report both as ambiguous, most
   likely — same shape as tier 1's branch-abandonment, not a coin flip).
4. Borrow the matched reference bone's real name for the label, but keep
   the numeric index visible in the output (`bone_<i>_<referenceName>` or
   similar) so a tier-2 guess is never confused with tier-0 real M2 data,
   matching every earlier tier's own convention.

Rigify ships with Blender itself (GPL) and is already reachable from the
same headless-Blender environment `tests/blender_import_check.py` already
drives (`HUSK_BLENDER` in `CMakeLists.txt`) — no new asset needs vendoring
for that path. Either way, the *similarity metric* (step 2) needs real
design work and a real fixture to validate against before this is more
than a sketch; the Rigify path additionally needs the translation table
(step 1).

## Why this is a separate pass from tier 1

Tier 1 is deterministic chain-interpolation over data this model already
has. Tier 2 is a real correspondence-matching algorithm against an
*external* skeleton — fuzzy by nature, needs a similarity threshold and an
ambiguity policy, and touches an asset (Rigify) nothing in this codebase
has depended on before. Different order of complexity, different failure mode
if wrong (a plausible-looking but incorrect anatomical name, versus tier
1's worst case of just not labeling something) — worth its own design pass
rather than folding into the tier-1 change.
