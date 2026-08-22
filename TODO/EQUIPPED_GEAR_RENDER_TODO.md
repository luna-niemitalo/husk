# TODO: render resolved equipped-gear appearance (Blender-side)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Background

`CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 6 (done 2026-08-21) made
`husk appearance-string --db2-dir/--dbd-dir` resolve a `gear=SLOT:id`
entry's `ItemModifiedAppearanceID` to real DB2 data: an `ItemDisplayInfoID`,
the equipped item's own `.m2` FileDataID(s) (`src/modelfiledata_db2.hpp`,
via `ItemDisplayInfo.ModelResourcesID`), and texture FileDataID(s)
(`src/texturefiledata_db2.hpp`, via `ItemDisplayInfoModelMatRes`). Same
"husk resolves, never applies" policy as every other DB2 feature here —
printed to stdout, not wired into `husk export`'s own glTF output at all.

Turning that resolved data into an actual attached/rendered weapon, shield,
cape, helm, or piece of armor is real, scoped, **downstream Blender-side**
work — the same category as `CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`'s own
customization-choice texture switch, not `husk export`'s job (per
`DESIGN.md`'s Key design decisions: husk reads formats and writes glTF, it
doesn't render or composite).

## Two real, structurally different sub-problems

**Update (2026-08-21): the pre-8.0 body-section-enum hypothesis for
`TextureType` is now checked against real local data, and falsified.**
Exported the real local `ChrModelMaterial`/`CharComponentTextureSections`/
`ItemDisplayInfoModelMatRes` tables to sqlite (`husk db2-export`) and
compared distinct values directly:

- `CharComponentTextureSections.SectionType` (773 real rows): `{0..14}` —
  consistent with (a superset of) the old wiki's 0-8 body-section range.
- `ItemDisplayInfoModelMatRes.TextureType` (141,108 real rows): `{2, 3, 4,
  5, 24}` only — **`24` alone falsifies the "same 0-8 body-section enum"
  reading outright**; the old table only ever had 9 possible values.
  Concentrated almost entirely in `2`/`3`/`4` (99.3% of rows).
- `reference/wow.export`'s own real, working `DBItemDisplayInfoModelMatRes.js`
  (a live, shipping implementation, not a guess) **never reads `TextureType`
  at all** — it collects every texture FileDataID reachable via
  `MaterialResourcesID` for a given `ItemDisplayInfoID` unconditionally,
  with no per-type filtering or selection. This is independent confirmation
  that `TextureType` isn't load-bearing for "which body part does this
  texture belong to" in any implementation that's actually shipping.
- A real multi-row example (`ItemDisplayInfoID` 727070, 10 rows) shows
  `TextureType` 2/3/4 each appearing at both `ModelIndex` 0 and 1, and
  `TextureType` 3 alone carrying *two different* `MaterialResourcesID`
  values at the same `ModelIndex` — a shape that fits "texture-map kind"
  (e.g. diffuse/detail/specular-ish channels for the same surface) far
  better than "body region," where you'd expect one row per region, not
  two colliding ones under the same type+index.

**Corrected reading**: `TextureType` is most likely a small, fixed
*texture-role* enum (which map — diffuse, a secondary detail layer, a
specular/emissive-ish channel — not *where on the body*), unrelated to
`CharComponentTextureSections.SectionType`'s enum space. This doesn't
change Stage 6's own resolution work (it already ignores `TextureType`
beyond passing it through) but does mean **case 2 below cannot use
`TextureType` as its section key** the way this file originally assumed.

**Update (2026-08-22): the real section key found and wired in --
`ChrModelTextureTargetID` is NOT it, `ItemDisplayInfoMaterialRes.
ComponentSection` is.** Checked `ChrModelTextureTargetID` reachability
first, per this file's own prior next-step: confirmed absent from every
column of `ItemDisplayInfo`/`ItemDisplayInfoModelMatRes` in the current
local WoWDBDefs layout (`husk db2-export` + direct schema inspection),
and a real `MaterialResourcesID`-namespace join between
`ItemDisplayInfoModelMatRes` and `ChrCustomizationMaterial` (the only
table that carries `ChrModelTextureTargetID`) returns just 2 matches out
of 35,706 distinct local values -- noise, not a real shared key.
Falsified.

Found the real answer instead by reading `reference/wow.export`'s own
shipping equipped-item compositing code
(`src/js/modules/tab_characters.js`'s item-texture loop,
`src/js/db/caches/DBItemCharTextures.js`) rather than guessing further
from column names: the real client does NOT use
`ItemDisplayInfoModelMatRes` for case 2 at all -- that table (and its
`TextureType`) is case 1's own texture (a standalone item's own separate
geometry, e.g. a weapon's blade). Case 2 goes through a **different,
sibling real table**, `ItemDisplayInfoMaterialRes` (note "Material", not
"Model" -- a real, separate, 230k-row-local table, not an alias), whose
`ComponentSection` column *is* the section key: confirmed against real
local data that its distinct values are exactly `{0..8}`, matching
`reference/wow.export`'s own shipping `COMPONENT_SECTION` enum
(`ARM_UPPER`=0 .. `ACCESSORY`=8), a real subset of
`CharComponentTextureSections.SectionType`'s broader `{0..14}` range. A
real multi-section item was confirmed locally (`ItemDisplayInfoID` 233:
`ComponentSection` 6/LEG_LOWER -> `MaterialResourcesID` 34187,
`ComponentSection` 7/FOOT -> `MaterialResourcesID` 27801 -- a
boots-shaped item, plausible), traced end to end from a real
`ItemModifiedAppearanceID` (367 -> `ItemAppearanceID` 495 ->
`ItemDisplayInfoID` 233) via `husk db2-export`/sqlite before trusting it.
(`TextureFileData.db2`'s final `MaterialResourcesID -> FileDataID` hop
couldn't be independently re-verified this session -- the file is
currently a genuine 0-byte-or-missing gap in this machine's local
extraction, same pre-existing re-extraction-flakiness class documented
elsewhere in this project's history, unrelated to this finding.)

**Implemented**: `src/itemappearance_db2.hpp`/`.cpp` now also loads
`itemdisplayinfomaterialres.db2` (new `MaterialRes` struct,
`Data::materialRes`, `Resolution::sectionMaterials`) alongside the
existing `ModelMatRes`/`materials` (case 1, kept, now documented as
case-1-only). `husk appearance-string`'s `gear` output
(`src/cmd_appearance.cpp`) now prints `section(N)=<fdid>` per resolved
section overlay, alongside case 1's existing `texture(type=N)=<fdid>`.
Still "husk resolves, never applies" -- CLI stdout only, no glTF extras
(that stays step 2's own open design question below). 2 new CLI-tier
tests in `tests/test_cli_appearance.cpp` (multi-section resolution using
the real `ItemDisplayInfoID` 233 shape above, synthetic fixture). Full
suite green.

This means resolved gear splits into two real, differently-rendered
cases, not one:

1. **Standalone-geometry items** (weapons, shields, capes, some
   helms/headpieces) — a real, separate `.m2` file
   (`modelfiledata_db2.hpp`'s resolved FileDataID) that needs to be
   imported as its own object and positioned at the base character's real
   attachment point (an `M2Attachment`, already exported as a named glTF
   node/bone — see `BONE_NAME_DEDUCTION_TODO.md`'s "Attachment tier" for
   how attachment-derived bone names already work, e.g. `HandRight` for a
   mainhand weapon). This is an *additive* second object parented to the
   base character's skeleton, not a texture change.
2. **Object-skin texture overlay items** (most body armor: chest, legs,
   feet, hands, ...) — no separate geometry at all; the item instead
   recolors/re-textures specific real sections of the *base character's
   own mesh* via the resolved texture FileDataID(s), keyed by the same
   real body-section enum `chr_texture_layout`'s `texture_layers[]`
   already carries. This is the same rendering mechanism
   `CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`'s `apply_customization_texture_switch`
   already built for player customization choices — an equipped item is
   structurally just another texture source competing for the same real
   section rects, not a new mechanism to invent.

A single `ItemModifiedAppearanceID` could plausibly resolve to *both* (a
real `ModelResourcesID` for a shoulder piece's own small geometry pad
alongside real `ItemDisplayInfoModelMatRes` rows for its texture) — not
confirmed either way, worth checking against a real multi-part item before
assuming they're mutually exclusive.

## Concrete next steps

1. **Ground-truth the section-key reading against real local data — done,
   2026-08-21 (`TextureType` falsified) + 2026-08-22 (`ChrModelTextureTargetID`
   falsified, `ItemDisplayInfoMaterialRes.ComponentSection` confirmed and
   wired).** See the corrected reading above — this step is closed.
   `husk appearance-string --db2-dir/--dbd-dir` now prints real
   `section(N)=<fdid>` entries per resolved case-2 overlay.
2. **Case 1 (standalone-geometry attachment) — done, 2026-08-22 (this
   session, priority flipped to case-1-first per Luna's own direct
   instruction: "structurally simple... one correctly-set-up positional
   constraint/parent relationship," vs. case 2's genuinely hard
   compositing work, hard enough that Blizzard's own live client visibly
   gets it wrong sometimes).** `husk export --appearance` (new flag,
   `src/appearance_string.hpp`'s `AppearanceString` in, `cust` feeding the
   same resolution `--customization-choice-ids` already drove, `gear`
   feeding the new logic here — mutually exclusive with
   `--customization-choice-ids`) resolves each `gear=SLOT:id` entry's case
   1 data (`gltf::Skeleton::GearItem`, `gltf_skeleton.hpp`) into
   `gear_items` skin extras (root-joint node extras, same merge site every
   other feature here uses). Critically, husk itself — not the Blender
   script — resolves the item's own real FileDataID to a real local `.m2`
   (via the already-loaded `--listfile`/`--listfile-root`) and
   **recursively exports that item's own `.glb`**
   (`exportGearAuxItemModels`, `src/cmd_export.cpp`, reusing the exact
   same `exportOneModel` function `--from-list` batch mode already
   factored out — not a second export path) to a real, predictable
   `<output-dir>/aux_models/<slot>_<fdid>.glb` sibling file, with the
   RELATIVE path baked into `GearItem::auxGlbPath`. This was a real
   design correction mid-session (Luna's own direct call): an earlier
   draft had the Blender script itself resolve `--listfile` and shell out
   to `husk export` at import time — rejected as breaking this project's
   own "self-contained `.glb`, zero external file-path knowledge beyond
   what's baked into extras" discipline (the same reason
   `_root_joint_extras` replaced every earlier raw-file-path `read_*`
   function). Blender-side (`tools/husk_blender_geoset_mask.py`):
   `read_gear_items`/`apply_gear_items` join `aux_glb_path` against the
   main `.glb`'s own directory, import it, and parent the result to the
   real `attachment_<id>` object this slot maps to
   (`GEAR_SLOT_TO_ATTACHMENT_IDS`, a real slot-name -> `M2Attachment` id
   table sourced from `documentation/wowdev-wiki/md/M2.md`'s own
   Attachments table — `SLOT` is caller-defined per
   `appearance_string.hpp`'s own doc comment, so this mapping is this
   script's own convention, not something husk validates). Confirmed
   directly via a headless Blender round-trip that `attachment_<id>`
   objects already import as real Empties, `parent_type='BONE'`-parented
   to the correct bone with `M2Attachment::position`'s own offset already
   baked into `.location` — so parenting the imported item at local-space
   origin reproduces correct placement with **no second manual offset**,
   confirming the "mechanically simple" framing was right. Verified end
   to end against real local data: `ItemModifiedAppearanceID` 15 (real
   chain: `ItemAppearanceID` 154 → `ItemDisplayInfoID` 1542 →
   `ModelResourcesID` 160 → real `.m2` FileDataID 370361,
   `creature/pygmy/pygmyshaman.m2`, cross-checked directly via
   `husk db2-export`/sqlite before trusting it) exported against
   `bloodelffemale.m2` (`gear=MAINHAND:15`) produced a real
   `aux_models/mainhand_370361.glb`, and the Blender script (given only
   the main `.glb`'s own path, no `--listfile` anywhere) placed a real
   `gear_mainhand_att1` Empty parented to `attachment_1`
   (`Armature`/`Icosphere.001` children moved together, confirmed via
   headless inspection). New CLI-tier tests
   (`tests/test_cli_gear_export.cpp`: mutual-exclusivity, malformed
   string, no-db2-dir skip, case-1 extras shape, real aux-model export +
   `aux_glb_path`, case-2 extras shape, `cust` field wiring) — 7 new
   tests, full suite green.
3. **Case 2 (object-skin overlay) — best-effort, DB2 resolution + extras
   done, Blender-side rendering NOT done.** `attachGearAppearance`
   (`src/export_extras.cpp`) also resolves each entry's `sectionMaterials`
   (case 2) into `gear_section_overlays` skin extras
   (`gltf::Skeleton::GearSectionOverlay` — `slot`, `componentSection`,
   `materialResourcesId`, resolved texture `fileDataId`), same
   `--appearance` flag, same extras-merge site as case 1. What's still
   missing, explicitly out of this session's budget per the case-1-first
   priority call: the actual Blender-side node-graph compositing that
   would make an equipped item's texture visibly override/compete with
   whichever customization choice currently owns that
   `chr_texture_layout` section — `apply_customization_texture_switch`'s
   existing per-choice node groups would need a gear-override Mix node
   spliced in front of the Base Color input (see that function's own node
   graph, `tools/husk_blender_geoset_mask.py`), gated by a real rect test
   reusing `_build_section_overlay_group`'s existing UV/section-rect
   machinery against `ComponentSection` rather than a per-choice `Show
   Overlay` toggle. Not started. A further real refinement
   `reference/wow.export`'s own code applies before this that husk
   doesn't yet: `DBComponentTextureFileData.getTextureForRaceGender`
   picks the best of several real per-race/gender texture variants
   sharing one `MaterialResourcesID` — `componenttexturefiledata.db2`,
   present locally, not yet read by husk anywhere.
4. **Real interactive Blender GUI pass**, same standing discipline every
   other Blender-side feature here follows (`CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`'s
   own "Still open" section is the template): case 1 renders
   structurally and was verified headless this session (real placement
   confirmed by inspecting the actual object hierarchy/transforms in a
   headless Blender process) — but a real screenshot/GUI confirmation
   that the equipped item looks plausible against real in-game appearance
   is still Luna's own next action, not self-certified here. Case 2 has
   nothing to visually check yet (Blender-side rendering not built).
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
   resolution mechanism. **Important framing correction from Luna,
   mid-session**: WoW's "Midnight" minor patch made sheathing genuinely
   player-choosable for most weapon classes (one-handed swords/offhands
   can sheathe at hip OR back — real, valid alternatives, not one
   derivable correct answer) — so a real `Item.db2` `SheathType` lookup
   would NOT give "the correct" single answer even in principle for
   current content, not just "husk hasn't resolved it yet." A manual
   Blender dropdown/toggle among whichever real attachment nodes actually
   exist on a given character is therefore the structurally correct
   design here, not a fallback approximation of a better automatic
   answer — frame it that way if/when this gets built. Not attempted this
   session (case 1 + case 2's DB2 half filled the available budget).
   **Deliberately deferred until after the new Blender-side panel
   (separate script, cross-option-constraint UI for customization/geoset
   choices, a different session's own in-flight work) lands** — that
   panel is the natural home for a per-character display-state toggle
   like this one, and building this stretch goal first risked
   duplicating UI/state-management groundwork the panel is already
   laying down.

## Why this is its own file

Stage 6's own DB2 resolution work (`CHAR_TEXTURE_COMPOSITING_TODO.md`) is
done and fully verified — this is a structurally different, downstream
kind of task (Blender-side rendering/attachment, not DB2 chain-walking),
gated on real design decisions (extras schema, slot->attachment mapping)
this session didn't settle, same "one punch list per open problem"
convention `BONE_CORRECTION_APPLICATION_TODO.md`/`DPIV_TODO.md` already
follow.
