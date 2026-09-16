# TODO: render resolved equipped-gear appearance (Blender-side)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file. Full narrative for the now-closed steps 1/2:
`CLAUDE_HISTORY.md`'s 2026-08-21/2026-08-22 archival entry.

## Background

`husk appearance-string --db2-dir/--dbd-dir` resolves a `gear=SLOT:id`
entry's `ItemModifiedAppearanceID` to real DB2 data: an `ItemDisplayInfoID`,
the equipped item's own `.m2` FileDataID(s) (`src/modelfiledata_db2.hpp`,
via `ItemDisplayInfo.ModelResourcesID`), and texture FileDataID(s)
(`src/texturefiledata_db2.hpp`, via `ItemDisplayInfoModelMatRes`). Same
"husk resolves, never applies" policy as every other DB2 feature here —
printed to stdout, and (via `husk export --appearance`) attached as inert
glTF skin extras. Turning that into actually rendered gear is real,
scoped, **downstream Blender-side** work — the same category as
`CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`'s own customization-choice texture
switch, not `husk export`'s job (per `DESIGN.md`'s Key design decisions:
husk reads formats and writes glTF, it doesn't render or composite).

Resolved gear splits into two real, differently-rendered cases:

1. **Standalone-geometry items** (weapons, shields, capes, some
   helms/headpieces) — a real, separate `.m2` file that needs to be
   imported as its own object and positioned at the base character's real
   attachment point (an `M2Attachment`, exported as a named glTF
   node/bone — see `BONE_NAME_DEDUCTION_TODO.md`'s "Attachment tier").
   This is an *additive* second object parented to the base character's
   skeleton, not a texture change. **Done** — `husk export --appearance`
   resolves this into `gear_items` skin extras and recursively exports
   each item's own `aux_models/<slot>_<fdid>.glb`;
   `tools/husk_blender_geoset_mask.py`'s `read_gear_items`/
   `apply_gear_items` import and parent it to the right
   `attachment_<id>` object. See `EXTRAS_SCHEMA.md` for the exact
   `gear_items` shape.
2. **Object-skin texture overlay items** (most body armor: chest, legs,
   feet, hands, ...) — no separate geometry at all; the item instead
   recolors/re-textures specific real sections of the *base character's
   own mesh*, keyed by `ItemDisplayInfoMaterialRes.ComponentSection`
   (real enum `{0..8}`, `ARM_UPPER`=0..`ACCESSORY`=8 — confirmed against
   real local data, see the history entry for the falsified hypotheses
   that preceded this). This is the same rendering mechanism
   `CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`'s
   `apply_customization_texture_switch` already built for player
   customization choices — an equipped item is structurally just another
   texture source competing for the same real section rects. **DB2
   resolution done** (`gear_section_overlays` skin extras — see
   `EXTRAS_SCHEMA.md`); **Blender-side rendering not done**, see step 3.

A single `ItemModifiedAppearanceID` could plausibly resolve to *both* (a
real `ModelResourcesID` for a shoulder piece's own small geometry pad
alongside case-2 texture rows) — not confirmed either way, worth checking
against a real multi-part item before assuming they're mutually
exclusive.

## Concrete next steps

3. **Case 2 (object-skin overlay) — Blender-side rendering, not started.**
   `attachGearAppearance` (`src/export_extras.cpp`) already resolves each
   entry's `sectionMaterials` into `gear_section_overlays` skin extras
   (`gltf::Skeleton::GearSectionOverlay` — `slot`, `componentSection`,
   `materialResourcesId`, resolved texture `fileDataId`). What's missing
   is the actual Blender-side node-graph compositing that makes an
   equipped item's texture visibly override/compete with whichever
   customization choice currently owns that `chr_texture_layout` section:
   `apply_customization_texture_switch`'s existing per-choice node groups
   need a gear-override Mix node spliced in front of the Base Color input
   (see that function's own node graph, `tools/husk_blender_geoset_mask.py`),
   gated by a real rect test reusing `_build_section_overlay_group`'s
   existing UV/section-rect machinery against `ComponentSection` rather
   than a per-choice `Show Overlay` toggle. A further real refinement
   `reference/wow.export`'s own code applies before this that husk
   doesn't yet: `DBComponentTextureFileData.getTextureForRaceGender` picks
   the best of several real per-race/gender texture variants sharing one
   `MaterialResourcesID` — `componenttexturefiledata.db2`, present
   locally, not yet read by husk anywhere.
4. **Real interactive Blender GUI pass**, same standing discipline every
   other Blender-side feature here follows (`CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`'s
   own "Still open" section is the template): case 1 renders
   structurally and was verified headless (real placement confirmed by
   inspecting the actual object hierarchy/transforms in a headless
   Blender process) — but a real screenshot/GUI confirmation that the
   equipped item looks plausible against real in-game appearance is
   still Luna's own next action, not self-certified here. Case 2 has
   nothing to visually check yet (Blender-side rendering not built, see
   step 3).
5. **Stretch goal, not started: a Blender-side weapon display-state
   toggle (in-hand / sheathed-on-back / sheathed-on-hip)** — Luna's own
   idea, flagged explicitly as "fun to have," not required. Real
   grounding: `M2Attachment` ids 1/2 (HandRight/HandLeft, in-hand), 26/27/28
   (SheathMainHand/SheathOffHand/SheathShield, hip-ish sheath points),
   30/31 (LargeWeaponLeft/LargeWeaponRight, back-mounted large weapons),
   and a `HipWeaponLeft`/`HipWeaponRight` pair nearby in the same table
   (`documentation/wowdev-wiki/md/M2.md`'s Attachments section) — every
   one of these already exports as a real named `attachment_<id>` child
   node unconditionally, regardless of gear, so this is a re-parenting
   toggle (or a Child-Of/Copy-Transforms constraint re-target), not a new
   resolution mechanism. **Important framing correction from Luna**:
   WoW's "Midnight" minor patch made sheathing genuinely player-choosable
   for most weapon classes (one-handed swords/offhands can sheathe at hip
   OR back — real, valid alternatives, not one derivable correct answer)
   — so a real `Item.db2` `SheathType` lookup would NOT give "the
   correct" single answer even in principle. A manual Blender
   dropdown/toggle among whichever real attachment nodes actually exist
   on a given character is therefore the structurally correct design
   here, not a fallback approximation of a better automatic answer.
   **Deliberately deferred until after the new Blender-side panel**
   (separate script, cross-option-constraint UI for customization/geoset
   choices, a different session's own in-flight work) lands — that panel
   is the natural home for a per-character display-state toggle like this
   one, and building this stretch goal first risks duplicating UI/
   state-management groundwork the panel is already laying down.

## Why this is its own file

The DB2 resolution work above is done and fully verified — this is a
structurally different, downstream kind of task (Blender-side
rendering/attachment, not DB2 chain-walking), gated on real design
decisions (extras schema, slot->attachment mapping) not yet settled for
case 2, same "one punch list per open problem" convention
`BONE_CORRECTION_APPLICATION_TODO.md`/`DPIV_TODO.md` already follow.
