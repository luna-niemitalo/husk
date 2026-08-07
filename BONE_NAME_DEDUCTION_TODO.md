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

## Tier 2 approach

Match remaining unnamed bones against **Blender's Rigify human meta-rig**
by hierarchy/relative-position similarity, using the tier-0-named bones as
known correspondence anchors:

1. For each tier-0-named M2 bone, find its counterpart in the Rigify
   meta-rig by name (a small hand-maintained mapping table — WoW's key-bone
   names and Rigify's bone names don't share a vocabulary, e.g. `ForearmL`
   vs `forearm.L`). This gives a set of known-good (M2 bone, Rigify bone)
   anchor pairs.
2. For each remaining unnamed M2 bone, compute a position relative to its
   nearest anchor(s) — hierarchy distance (how many bones up/down the
   chain) and/or normalized spatial position between two anchors — and find
   the Rigify bone with the most similar relative position to *its* nearest
   anchors.
3. Only accept a match under a similarity threshold; below it, leave the
   bone unlabeled rather than guessing (same "no claim beyond what's
   supportable" discipline as tier 1). Define a policy for a tie between
   two similarly-close Rigify bones (report both as ambiguous, most likely
   — same shape as tier 1's branch-abandonment, not a coin flip).
4. Borrow the matched Rigify bone's real name for the label, but keep the
   numeric index visible in the output (`bone_<i>_<rigifyName>` or similar)
   so a tier-2 guess is never confused with tier-0 real M2 data, matching
   tier 0/1's own convention.

Rigify ships with Blender itself (GPL) and is already reachable from the
same headless-Blender environment `tests/blender_import_check.py` already
drives (`HUSK_BLENDER` in `CMakeLists.txt`) — no new asset needs vendoring,
but the *mapping table* (step 1) and the *similarity metric* (step 2) both
need real design work and a real fixture to validate against before this is
more than a sketch.

## Why this is a separate pass from tier 1

Tier 1 is deterministic chain-interpolation over data this model already
has. Tier 2 is a real correspondence-matching algorithm against an
*external* skeleton — fuzzy by nature, needs a similarity threshold and an
ambiguity policy, and touches an asset (Rigify) nothing in this codebase
has depended on before. Different order of complexity, different failure mode
if wrong (a plausible-looking but incorrect anatomical name, versus tier
1's worst case of just not labeling something) — worth its own design pass
rather than folding into the tier-1 change.
