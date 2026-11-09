# Character rendering: what it takes to match the game

Map of everything husk, MantleCore and the local reference tools (wow.export,
the client's own shaders) know about drawing a player character — old and HD
bodies, every race, customization, equipment — and what is still missing
before a character renders the same as in game with no guessed values.

Compiled 2026-11-09 from husk's code and docs, MantleCore's docs and
`src/bundle_view/`, `reference/wow.export` (= `~/dev/wow.export`), and the
local 12.1.5 / Forever 1.60.1 DB2s. Citations are `path:line` or a doc
section; facts move, so re-check a citation before building on it.

**Ownership rule.** MantleCore draws only what the bundle states. Every value
the bundle leaves out becomes a guess on MantleCore's side, so every gap
below is husk's gap, even where the fix is a renderer feature: husk must put
the data, the rule and its evidence status in the bundle.

## Evidence ladder

Every rule below is tagged with the strongest evidence behind it:

| Tag | Meaning |
|---|---|
| **client** | Read from the 12.1.0 client's shader bytecode (`WIKI_FINDINGS/M2/rendering.md`, `SHADER_FINDINGS/`) |
| **data** | Stated by a DB2 row or file field; join verified on real files |
| **ref** | wow.export / WMV behaviour: a working hypothesis, several known-wrong (§9) |
| **husk** | husk's own heuristic, labelled as such in code |
| **none** | Nobody knows; needs a source (§10) |

## 1. The pipeline, layer by layer

| # | Layer | Source of truth | husk reads | Bundle carries | MantleCore |
|---|---|---|---|---|---|
| 1 | Body: race/sex → ChrModel → M2 | `ChrRaceXChrModel`, `ChrModel.DisplayID` → `CreatureDisplayInfo` → `CreatureModelData` (**data**) | yes (`chrrace_db2.cpp`, `creature_display_db2.cpp:57-102`) | one catalog per ChrModel drawing the file | reads |
| 2 | Customization menu | `ChrCustomizationOption/Choice/Element` (**data**) | Option, Choice, Element: geoset, bone set, material | `definition.customization` | skin lab reads |
| 3 | Option gating | `ChrCustomizationReq`, `ReqChoice`, `VisReq` (**data**) | **no** | no | no |
| 4 | Geosets | `ChrCustomizationGeoset` (Type·100+ID) (**data**); base visibility rule (**ref**) | yes; only the last geoset element per choice; `Modifier` unread | `choices[].geoset_id`, no base rule | lowest variant per group, or the first preset |
| 5 | Skin composite | `ChrModelMaterial` (canvases), `CharComponentTextureSections` (rects), `ChrModelTextureLayer` (layers), `ChrCustomizationMaterial` → `TextureFileData` (**data**); compositor cases 0–13 (**client**) | yes; `BitMask2`, `OverlapSectionMask`, `TargetID[1]` unread | `materials/<n>.json` composites, `blend_mapping: unverified` | CPU compose, live upload (`skin_lab.cpp`) |
| 6 | Surface shading | M2 batch: shader ID, blend, flags, color/weight/UV tracks (**data**); combiner math (**client**) | yes | material + technique documents; with the gaps in §5 | 77% baked by technique; the rest use their first texture |
| 7 | Items: models | `ItemModifiedAppearance` → `ItemAppearance` → `ItemDisplayInfo.ModelResourcesID[0..1]` → `ModelFileData` → `ComponentModelFileData` race/sex/class/side (**data**) | `[0]` only; `.front()` pick, no ComponentModelFileData | bare `ItemDisplayInfo` refs | nothing |
| 8 | Items: body textures | `ItemDisplayInfoMaterialRes` (section 0–8) → `ComponentTextureFileData` race/sex/class (**data**) | MaterialRes yes; ComponentTextureFileData **no** | `SectionOverlayComponent` exists in `canon::Item`, never written | nothing |
| 9 | Items: geosets / helm hiding | `ItemDisplayInfo.GeosetGroup[6]`, `AttachmentGeosetGroup[6]`, `HelmetGeosetVis[2]` → `HelmetGeosetData` (**data**); slot→group table and priorities (**ref**) | **no** | no | no |
| 10 | Items: attachment | attachment ids per slot (**ref**, WMV table); points from M2 / `.skel` `SKA1` (**data**) | points yes | `resources.attachments` | points unused |
| 11 | Collection items (armor that skins to the body) | bone match by name CRC + pivot (**ref**) | no | no | no |
| 12 | Bone corrections | Choice → `ChrCustomizationBoneSet` → `.bone` (**data**); how to apply (**none**) | parsed; selection resolved in legacy extras | **no** (`canon_definition.hpp:42` excludes on purpose) | no |
| 13 | Skinned / conditional / display-info elements | `ChrCustomizationSkinnedModel`, `CondModel`, `DisplayInfo`, `ChrCustItemGeoModify`, `GeoComponentLink` (**data**) | **no** | no | no |
| 14 | Animation | sequences, `.anim`/`.skel` (**data**) | yes | clips as bare indices | CPU pose, GPU skin |
| 15 | Lighting / scene | lighting math (**client**); char-create / viewer light rig (**none**) | zone `light_params` only | no character scene | hand-set |

## 2. Body selection: old and HD models

- Live 12.1.5 ships both `<race><sex>.m2` (old body) and `_hd.m2` for the
  10 original races; allied races have one model plus `_sdr`.
- **No live ChrModel points at the old bodies**: ChrModel 1–18 use the HD
  DisplayIDs. Old bodies have no customization in live data; that is why
  `humanmale.m2` (119940) renders as a white body in MantleCore
  (`mantlecore/design/bundle-viewer.md:735-737`).
- **Forever 1.60.1 makes old bodies customizable**: ChrModel 257–278, layout
  203, own skeletons. `ChrModelAltVariant` maps ChrModel 1–18 → 257+;
  `…Option` (93 rows) and `…Choice` (1,136 rows) map each HD option/choice
  to its old-body counterpart (`WIKI_FINDINGS/FOREVER.md:141-142`). WoWDBDefs
  tags the same tables for retail 12.1.7.70424+, so live will likely gain
  them.
- **Hazard**: `chrrace_db2.cpp:107` strips `_hd` in the filename fallback, so
  an old body without a listfile FDID gets the HD ChrModel and its layout
  silently. With the AltVariant tables this must become an explicit mapping.
- husk reads WDC5 only, so pre-9.0 CharSections-era clients are out of reach;
  `CharSections` exists in no local build. `CharBaseSection`,
  `CharBaseInfo`, `CharHairGeosets` (with `HdCustomGeoFileDataID`) and
  `CharacterFacialHairStyles` remain in live and are unread.
- `SDReplacementModel` is empty in both builds: no HD↔SD mapping from there.

## 3. Defaults ("sane defaults for all races")

**No DB2 column states a default choice** (checked: Option, Choice, Req,
ReqChoice in WoWDBDefs). What exists:

| Source | Rule | Status |
|---|---|---|
| husk `defaultChoiceIdsForModel` (`chrcustomization_db2.hpp:218-229`) | lowest `OrderIndex` per option, ties to the lowest id; every option | **husk**; legacy path only, canon has no default preset |
| wow.export (`DBCharacterCustomization.js:161-162`, `tab_characters.js:871-876`) | only options **without `Option.Flags & 0x20`** get a default: the first choice in row order | **ref**; the 0x20 bit is a real data signal worth checking |
| MantleCore `DefaultSkinSelection` (`skin.cpp:166-174`) | choice 0 of each option of the first option's ChrModel | positional, inherits husk's sort |
| Blender script | `CURATED_DEFAULT_VARIANTS` (bloodelffemale_hd only), `{32: 2}` (Luna, 2 models), lowest variant | **husk**, not portable |

Missing pieces, in the order they matter:

1. **Requirement filtering.** `ChrCustomizationReq` (`ReqType`, `ClassMask`,
   `RaceMasks`, achievement/quest/item) is unread. The non-HD default picking
   a demon-hunter tattoo (`TEXTURE_POOL_RECALL_TODO.md` §1) fits this (inference). A
   default must be a choice a fresh, classless character may have.
2. **Option flags.** `Option.Flags` (0x20 above; the DBD's 1/2/3 comment),
   `OptionType`, `Choice.Flags`: decode against data before picking.
3. **Order.** `OrderIndex` ("sorts internal structures") vs `UiOrderIndex`
   ("returned in Lua"). Character-create presents the Lua order.
4. **Base geoset visibility** (geosets no choice names): only wow.export's
   string rule exists: show 0, `*01`, `32*`; hide `17*`, `35*`
   (`character-appearance.js:20-26`). husk has none; the bundle should state
   a per-model base set rather than leave it to the reader.

**Ground truth already on disk: humanoid NPCs.** 55,799 creature displays
use `CreatureDisplayInfoExtra` on 93 character models, each with the full
client-authored choice list (`CreatureDisplayInfoOption`), equipment
(`NPCModelItemSlotDisplayInfo`) and a **baked skin texture**
(`Bake`/`HDBakeMaterialResourcesID`). That bake is Blizzard's own composite
of those choices and gear. Composing the same inputs and diffing against the
bake checks layer order, blend mapping, texel scaling and gear interleaving
offline, per race (bakes may come from an older tool version; check
several).

**NPC appearances as presets: built (2026-10-11).** Every humanoid NPC
display is a `selection.presets` entry on its character model: `customization`
is `{option, choice}` indices into the model's own `definition.customization`,
plus the bake as a `skin` slot option and items as `definition.items` indices
(`creature_options_input.cpp:300-360`, BUNDLE_FORMAT.md "Options and
presets"). That is the large set of client-authored defaults a consumer can
pick from or scroll through. Measured on `humanmale_hd` (2026-11-09, live
12.1.5): 5,748 presets from 6,384 displays, 5,728 with customization, 30 with
unresolved picks, **4,617 distinct customization sets**. They take 4.7 MB of
the minified manifest, about 800 B each; the `rig` section is 48.7 MB.
Old bodies get none: `humanmale.m2` has no ChrModel (§2) and no humanoid
displays.

Open on presets:
- A preset ties appearance, gear and bake together. Scrolling appearances
  alone means deduplicating `customization` on the reader side (4,617 of
  5,748). A separate appearance list that presets index would state it once.
  This is a design call.
- Labels: built 2026-11-09. Each source lists its `users` (Creature rows,
  name and title). The client ships only 18,639 creatures, so on
  `humanmale_hd` 607 of 5,748 presets are named (955 names). Usage guide:
  BUNDLE_FORMAT.md "Using presets".
- MantleCore's default skin selection ignores presets (choice 0 per option,
  §3 table); taking preset 0, or a chosen one, removes that guess.

## 4. Skin composite (layer 5)

Client compositor, **client**: canvas `P`, layer `L`, unscaled texel copy;
cases 0 nothing, 1 copy, 2 alpha-tested copy, 3 add, 4 multiply, 5 mul2x,
6 overlay (keyed on canvas), 7 screen, 8 hard light, 9 alpha mix, 10 mul by
layer alpha, 11 mul by `L.r`, 12 greyscale, 13 gradient map
(`WIKI_FINDINGS/M2/rendering.md:194-224`).

Unknowns, all **none** unless noted:

| Question | husk C++ (canon) | Blender script | wow.export | MantleCore |
|---|---|---|---|---|
| `BlendMode` → case | same number if ≤ 13 | 0/1/9/15 mix, 4 mul, 6 overlay, 7 screen | per table, 2/3/5/8/10–14 unhandled | husk's ops; **mode 15 stand-in** |
| Mode 15 (7 of 10 layers on HD human composite 0) | unnamed | mix | blit | `alpha_blend`, marked approximate |
| Layer order | `Layer` column | `layer` | `ChrModelTextureTargetID` ascending | husk's |
| Rect for multi-bit masks | first section | bounding box | first section in row order | husk's |
| Texture smaller than rect | unscaled (as client) | scaled to fit | stretched | open: HD human textures are half their rects |
| Start canvas | empty | M2 base texture | opaque black | — |
| Gear layers among customization | none | none | target `SLOT_LAYER·100+section` | none |
| `ChrModelMaterial.TextureType` = M2 texture type | assumed (`creature_options_input.cpp:189`) | assumed | assumed | — |
| Canvas per target, if a target is on several canvases | first kept (`:184`) | — | — | — |

Where to settle them: the NPC bake diff (§3) for order, mode 15, scaling and
gear; the CPU-side `BlendMode` → case table lives in the client binary
(`TODO/MATERIALS_TODO.md` §1), and the bake diff can check its mapping
mode by mode.

Gear onto the atlas (layer 8): section types 0–8 are ARM_UPPER, ARM_LOWER,
HAND, TORSO_UPPER, TORSO_LOWER, LEG_UPPER, LEG_LOWER, FOOT, ACCESSORY
(**ref**, from wow.export). Texture choice per race/sex goes through
`ComponentTextureFileData`. wow.export's cascade is exact race+sex, then
race with sex 2 (any), then race 0, then the first candidate, ignoring
ClassID (**ref**). Gear blend into the atlas: wow.export uses the first
layer whose mask has the section bit (**ref**). Guild tabard: background,
emblem and border tables onto sections 3/4.

## 5. Surface shading (layer 6)

Settled (**client**, `WIKI_FINDINGS/M2/rendering.md`):
- All 36 combiners, alpha test (128/255 before the `mesh.a` multiply),
  emissive after lighting, edge fade, env selection, the dye/recolour
  palette, the guild-tint formula, the lighting model.

Character usage (`SHADER_FINDINGS/scan/shader_inventory.csv`):
- Body and hair are plain `Combiners_Opaque`; the look is the composite
  atlas plus lighting.
- Eyes (type 19) use `Mod_Add` alpha-key; eye glow uses `Mod_Mod2x`/`Mod_Mod`
  additive. Tabard is `Guild_Opaque`, six textures.
- `skin`/`avatar` effect shaders exist; which models use them is client-side
  (**none**). Whether `skin` is subsurface is contradicted between
  `SHADER_FINDINGS/README.md` §5 and `notes/model_effects.md`.

**Bundle bugs, husk-side, data we already have:**
1. Constant `M2Color` rgb/alpha and constant texture weight are dropped
   (`m2_material_input.cpp:214-240` return nothing unless animated). Static
   tints and half-alpha are lost; the 525 weight-zero batches would draw.
   The legacy glTF path keeps them (`export_materials.cpp:268-321`).
2. Env-mapped layers never flagged for 12.1 content (`textureCoordCombos` is
   empty in 132,860 of 132,863 files); armour shine falls to UV0
   (`MATERIALS_TODO.md` §8).
3. Material curves from one sequence only (§7).
4. Colour alpha and weight both animated: only colour alpha kept
   (`:343-368`).
5. `priorityPlane`/`materialLayer` unparsed (`skin.cpp:178`), so there is no
   draw order for coplanar transparent batches (eye glow over face).
6. Flags 0x8/0x10 (no depth test/write) only in raw `source.flags`.

**Unknown (client CPU side):** M2 blend 0–7 → internal blend class; per-draw
constants (`cb0[6].z`, `cb0[8]`, `cb0[22]`, guild colours `cb0[10..12]`, dye
palette index); which data drives dye (no ItemDisplayInfo colour chain is
known); unlit forced by Mod/Mod2x or by flag only.

## 6. Items (layers 7–11)

husk today: the ItemModifiedAppearance → ItemDisplayInfo chain; "case 1"
(own model + `ItemDisplayInfoModelMatRes`) and "case 2" (body overlay)
split; Hidden (id 0) handled; `canon::Item` types exist; nothing writes
them.

Needed, all with **data** sources present locally:

| Piece | Source | Rule status |
|---|---|---|
| Second model (right shoulder) | `ModelResourcesID[1]` | **data** |
| Per race/sex/class model | `ComponentModelFileData` (Race, Gender, Class, PositionIndex 0=L 1=R) | cascade order **ref** |
| Per race/sex texture | `ComponentTextureFileData` | cascade **ref** |
| Item model textures | `ModelMaterialResourcesID[0..1]` → M2 types 2–4/11–13 | **ref**; husk's "unrelated textures" finding predates the pallet fix, re-check (`cmd_db2_build.cpp:256-266`) |
| Character geosets | `GeosetGroup[n]` by slot table; value 1+n (feet: 0→2); priority gloves>chest>shirt for sleeves, chest>pants for trousers | table and priorities **ref** (WMV `WoWItem.cpp`) |
| Collection-model geosets | `AttachmentGeosetGroup[n]` | **ref** |
| Helmet hiding | `HelmetGeosetVis[sex]` → `HelmetGeosetData(race, HideGeosetGroup)`; `ChrModel.HelmVisFallbackChrModelID` | **data**; fallback unread |
| Attachment per slot | head 11, shoulder 6 L / 5 R, back 12, main 1, off 2, shield 0, bow 2 | **ref** (WMV); husk's Blender table differs (shoulder same model on both, no bow rule) |
| Collection items | skin to the body skeleton, bone match by name CRC + pivot | **ref** |
| Application order | base → customization → skinned-model hides → item geosets → helm hides | **ref** (`tab_characters.js:329-390`) |
| Appearance modifier | `ItemModifiedAppearance.ItemAppearanceModifierID` | unread; wow.export takes the last row |
| Item scaling, sheathe, visuals | `HelmetAnimScaling`, `SheatheTransformMatrixID`, `ItemVisual`, `ParticleColorID` | unread everywhere |

NPC equipment: `NPCModelItemSlotDisplayInfo` slots 0–10 named by wow.export
(**ref**); slot 11 (1,635 rows) unnamed; weapons from `Creature.AlwaysItem`.

## 7. Bone corrections, skinned models, animation

- `.bone`: BIDA/BOMT, near-identity scales clustered on head/jaw
  (`WIKI_FINDINGS/BONE.md`). Selection: Choice → BoneSet (**data**).
  Application (multiply order, space, inheritance) is **none**; needs a
  client screenshot of a large-correction choice and a probe of candidate
  compositions (`TODO/BONE_CORRECTION_APPLICATION_TODO.md`). The bundle
  carries none of it; it must carry the sets and the selection at least.
- Skinned-model elements (mechagnome limbs, etc.) hide base geosets and
  attach a model; nobody implements them.
- MantleCore is blocked on clip metadata: animation id/name, length, loop,
  blend time, variation/alias, move speed (`bundle-viewer.md:454-461`).
  Needed for any idle stance comparison; hand grip (HandsClosed frame 0,
  ref) depends on it.

## 8. Lighting and scene

The forward lighting model is decoded (`SHADER_FINDINGS/notes/
lighting_model.md`); its constants come from `LightData` by an **inferred**
mapping. Nothing is known about the character-create or model-viewer light
rig, and the bundle states no texture colour space. Matching "in game"
needs a chosen reference scene: a zone `light_params` at a stated time of
day is the one fully data-backed option today.

## 9. Known-wrong in the reference implementation

wow.export is the best map of joins, not of rules. Against the client
compositor it is wrong on modes 0, 2, 6, 10–13 (`rendering.md:226-231`).
It also: keeps one geoset per choice; ignores `Layer`, `BitMask2`,
`OverlapSectionMask`, `TargetID[1]`, ClassID, Req; flips Y on partial
`readPixels` (probable bug); attaches single-model shields to id 2, not 0;
disables specular; has no UV/colour/weight animation; and draws no
transparency order. Its legacy path is MPQ-era models with no character
customization.

## 10. Closing the unknowns: sources ranked

1. **NPC bake diff** (offline, data on disk): composite order, mode 15,
   texel scaling, gear interleave, start canvas. §3.
2. **DB2 data analysis** (offline): `Option.Flags` bits, Req filtering,
   `UiOrderIndex`, multi-geoset choices, `TextureFileData` UsageType ≠ 0
   meaning, ComponentModelFileData cascade (find rows where race-0 and
   exact-race both exist).
3. **Corpus statistics** (offline): which blend modes, flags and shader IDs
   occur together, e.g. the ready particle `blendingType` test
   (`SHADER_FINDINGS/notes/reconciliation.md:200-212`). This narrows the blend
   class mapping (§5) without the client.
4. **Client Lua** (character create, `C_CharacterCreation`): order and
   default behaviour as the UI sees them.
5. **Screenshots** for bone corrections and the final look. Human-gated.

No GPU frame capture of the live client (Luna, 2026-11-09). What only a
capture could read stays labelled unknown in the bundle until another source
turns up: per-draw constants (`cb0[6].z`, `cb0[8]`, `cb0[22]`, guild
colours), the dye data source, and the character-create light rig.

## 11. What husk must add to the bundle, in order

Each step is checkable against §10 sources before the next.

1. Fix the §5 material drops (constant color/weight, env flag, dual
   alpha, draw priority). Data in hand, no unknowns.
2. Req/flag-aware customization: read `ChrCustomizationReq`/`ReqChoice`/
   `VisReq`, `Option.Flags`, `OptionType`, `UiOrderIndex`; state per-option
   requirements. Defaults come from the NPC presets (§3); where a model has
   none, state a rule-derived preset with its evidence status.
3. Per-model base geoset set in the bundle (replaces every reader's rule).
4. NPC bake diff harness; then settle `blend_mapping` and flip it to
   verified per mode.
5. Item documents: models via ComponentModelFileData (L/R), textures via
   ComponentTextureFileData, geoset groups, helm hiding, attachment ids,
   collection skinning info. Presets reference them.
6. Bone sets and their choice selection carried (application rule marked
   unknown until §7 resolves).
7. Old bodies: `ChrModelAltVariant*` from Forever (later live), an explicit
   HD↔old mapping, removing the `_hd`-strip fallback's silent reuse.
8. Clip metadata for MantleCore.
9. Skinned/conditional model elements.
10. If Luna wants it, an appearance list that presets index (§3 "Open on
    presets").

## 12. Stale docs found while compiling this

Not fixed here, listed for a cleanup pass:
- `CLAUDE.md` "Creature options", `README.md:156`, `TODO/CREATURE_OPTIONS_TODO.md` §2:
  "no choice textures yet" — e587735 binds them (1,197 on humanmale_hd).
- `REFACTOR/BLENDER_ADDON.md:5-36`, CLAUDE.md Resume "Next step":
  `Definition` is now assembled and written; `Selection`/`Item` still aren't.
- `REFACTOR/MATERIALS.md:97-113`: wow.export blend names, "pseudo-verified"
  formulas, "proposal" header.
- `TODO/PIXEL_SHADER_FORMULAS_TODO.md`: lists formulas the bytecode settled.
- `REFACTOR/BUNDLE_FORMAT.md:166`: "four kinds", table lacks `material`.
- `reports/CHARACTER_PIPELINE_TEST_FINDINGS.md` Finding 4: HD models do have
  attachments (`.skel` `SKA1`); only the legacy path misses them.
- `CLAUDE_HISTORY.md` 2026-08-21 gear "verified end to end" predates the
  2026-08-23 pallet fix (706c8b6); its model was likely wrong.
- `modelfiledata_db2.hpp:16`, `canon_item.hpp:24`: multiple FDIDs are
  race/sex variants, not LODs.
- `WIKI_FINDINGS/BONE.md`, `bone.hpp`, `export_extras.cpp:89-93`: Option/
  Choice "0 bytes" / "data husk doesn't have" — both present and read.
- `development/REFERENCES.md`, CLAUDE.md corpus hazard: `live` is now
  12.1.5.70783 with the 2026-11-07 listfile.
- `SHADER_FINDINGS/README.md` §3.3 calls texture types 19/20 unknown;
  `m2_header.cpp:430-431` names them (source unstated).
- `export_texture_resolution.cpp` ~62-70, `appearance_string.hpp:16-20`:
  claim data husk now reads.
