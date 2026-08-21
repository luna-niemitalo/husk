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

`ItemDisplayInfoModelMatRes`'s own `TextureType` field is **not** a
"weapon vs. armor" marker — wowdev.wiki's `ItemDisplayInfoMaterialRes`
page (the pre-8.0 ancestor of this table) documents it as a real body
**texture slot** enum (`UpperArm=0, LowerArm=1, Hands=2, UpperTorso=3,
LowerTorso=4, UpperLeg=5, LowerLeg=6, Foot=7, Accessory=8`) — the same
kind of section identifier `CharComponentTextureSections`/
`texture_section_type_bit_mask` already use elsewhere in this project
(`chrmodel_db2.hpp`, consumed by `CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`'s
own node-graph switch). **Not yet cross-checked against a real local
`ItemDisplayInfoModelMatRes` row + a real body-armor item** to confirm the
enum still applies unchanged at the current live layout — worth doing
before trusting this reading for real work, see "Concrete next steps"
below.

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

1. **Ground-truth the `TextureType`/section-enum reading against real
   local data.** Pick a real body-armor `ItemModifiedAppearanceID` (a
   chest or leg piece is the safest bet — least likely to also carry
   standalone geometry), resolve it via `husk appearance-string
   --db2-dir/--dbd-dir`, and cross-check the resolved `TextureType`
   value(s) against `CharComponentTextureSections.db2`'s own real section
   IDs (same table `chrmodel_db2.hpp` already reads) to confirm they're
   the same enum space, not just structurally similar. Do this before
   writing any rendering code — get the mapping wrong and case 2 below
   silently textures the wrong body part.
2. **Case 2 first (object-skin overlay)** — very likely far more common
   than case 1 given the real corpus's own `item/objectcomponents/`
   directory shape (`KNOWLEDGE_BASE_DESIGN.md`'s own prior investigation
   already found this class dominant), and reuses
   `apply_customization_texture_switch`'s existing node-graph machinery
   almost directly: a new Blender-side reader
   (`read_gear_appearance` or similar, same "parse raw extras/CLI JSON,
   build a real node graph" pattern) needs the resolved `(TextureType,
   texture FileDataID)` pairs — currently only printed to stdout by
   `husk appearance-string`, not yet exposed as glTF `extras` the way
   `chr_enabled_materials`/`chr_customization_options` are. Real design
   question, not yet settled: does this data belong on `husk export`'s
   own skin `extras` (would need `husk export` to accept a `gear=...`
   argument, same shape `--customization-choice-ids` already has) or
   stay a separate `husk appearance-string` output a caller feeds into
   the Blender script alongside the `.glb` path? The former matches this
   project's existing "husk attaches, Blender consumes extras" pattern
   more closely; the latter avoids growing `husk export`'s own flag
   surface for a feature that's arguably orthogonal to a single-model
   export. Worth a real design pass before committing either way.
3. **Case 1 (standalone geometry attachment)** — needs a second glTF
   import + a real transform placing it at the base character's resolved
   attachment-point bone. `tools/husk_blender_geoset_mask.py` already
   knows how to read a model's own attachment-derived bone names; the new
   piece is importing a *second* `.glb` (the equipped item, exported
   separately via a normal `husk export <item.m2>` call once its
   FileDataID resolves to a real local file — same `--listfile`
   resolution path every other husk feature already uses) and parenting
   it to the right bone with the real M2 attachment offset applied (not
   just bone-parented at the bone's own origin — `M2Attachment`'s own
   `position` field, already exported per-node, is exactly this offset).
   Multiple real attachment slots exist per item class (`Shield`,
   `HandRight`, `HandLeft`, `Helm`, ...) — resolving *which* attachment
   point a given equipped slot (`gear=MAINHAND:...` etc.) maps to is
   itself real work: `husk-appearance/1`'s `SLOT` token is caller-defined
   (`src/appearance_string.hpp`'s own doc comment: "an opaque uppercase
   token... husk's grammar does not hardcode Blizzard's equipment-slot
   name enum"), so a slot->attachment-point mapping table needs to live
   in the Blender-side consumer, not husk itself.
4. **Real interactive Blender GUI pass**, same standing discipline every
   other Blender-side feature here follows (`CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`'s
   own "Still open" section is the template): once either case renders
   structurally, get a real screenshot/GUI confirmation that the result
   looks plausible against real in-game appearance — not something to
   self-certify.

## Why this is its own file

Stage 6's own DB2 resolution work (`CHAR_TEXTURE_COMPOSITING_TODO.md`) is
done and fully verified — this is a structurally different, downstream
kind of task (Blender-side rendering/attachment, not DB2 chain-walking),
gated on real design decisions (extras schema, slot->attachment mapping)
this session didn't settle, same "one punch list per open problem"
convention `BONE_CORRECTION_APPLICATION_TODO.md`/`DPIV_TODO.md` already
follow.
