# Garments: what a body wears, independent of source item slots

**Status: built for `husk export --appearance` and NPC equipment (schema 0.4.0, branch `format04-garments`).**
Replaces the model's `definition.items`, `selection.equipped` and `presets[].items`.

- **Built**: the `garments` document (`schema/garments.schema.json`), inline in the model's `garments`
  reference; the model's `definition.body` (regions with the layout's rectangles, parts, attachment points);
  presets' `garments` and bakes' `covers_garments`; stage 1 by slot (`src/canon_garment_builder.cpp`, rules
  `garment_split`, `item_geoset`, `garment_layer`); layer and order of every garment cut from a source item by
  its slot's draw priority (`source_slot_priority`); the body hem 1102 moved to the tunic of the same
  TransmogSet, or a `hem` garment, with its paint as an overlay masked to 1102's UV footprint
  (`hem_uv_footprint`); item-model group-13 hems as their own pieces, deduplicated by model file and geoset
  (`item_model_hem`); outfits per TransmogSet; validate-bundle checks preset garment ids and warns on
  conflicts and kind/layer/order deviations, `husk garments-check <bundle> [--preset ID]` lists them.
- **Built, NPC equipment** (`NpcGarments`): each display resolved through the same stage-1 split
  (models, model textures, overlays, geosets, files picked for the body model's race and sex), split once
  per slot it is worn in (NPCs wear one display in several slots, 3454 in seven, and the slot decides the
  split) and shared by every NPC wearing it there; item models are referenced by file, not exported as bundles. A
  display that draws nothing makes no garment (NPC slot 11's only display is one,
  `WIKI_FINDINGS/GARMENTS.md` "NPC item slot 11"). The hem rule, with no TransmogSet: see "NPC
  equipment: the hem by the NPC's own outfit" below.
- **Not built**: the external (shared, per-tree) garments list and its export command; `hides` pieces
  (HelmetGeosetData not read); `dress` vs `robe` by length; class-specific files; ItemSet as a set source
  (TransmogSet only); NPC outfits as `outfits` entries (a preset lists the NPC's garments); 1104/1105 stay
  plain `thigh` variants with no hem claim; item-model group-11 meshes stay with the legs garment; the
  Blender addon and the lean glb don't read garments.

## The problem

WoW packages clothing by equipment slot, a 20-year-old constraint:

- A **chest** item that is a tunic paints the torso, the arms and often the
  upper legs (`LegUpper` section), and may switch on a skirt mesh (geoset
  group 13). In WoW terms it "covers two slots"; to a person it is one
  tunic.
- A **legs** item is not always just leggings: its `LegUpper` texture often
  paints a tunic's hem onto the thighs, because older chest items couldn't
  reach that far. The hem belongs to the tunic, but WoW ships it as part of
  the trousers.
- A legs item can also carry its own skirt mesh (a kilt), which is a
  separate garment from the trousers it ships with.

Today's bundle copies this packaging: an item is a source row, its overlays
are keyed by WoW's `component_section`, its models by `ModelResourcesID`
position, and a preset wears one item per WoW slot. So a consumer can't wear
the tunic without the hem-painted trousers, or the trousers without the hem,
without changing the format. That breaks I7 (item identity is not slot
identity) and the §2 test (a non-WoW consumer can't use these facts as they
are).

## Principle

**A garment is the thing a person would name**: tunic, leggings, skirt,
boots, belt, cloak, glove pair, pauldron pair. Source items are packaging.

- **Many-to-many.** One source item can produce several garments: a legs
  item yields leggings plus a kilt. One garment can draw pieces from several
  source items: a tunic gets its body from the chest item and its hem from
  the legs item's texture.
- **Lossless.** Every piece of every source item lands in exactly one
  garment. Masks partition a source texture, so wearing all the garments
  from a source item's pieces reproduces that item exactly. That is what
  keeps WoW-exact "outfit loading" (I7) possible.
- **No slots in the canonical layer.** What a garment occupies is stated as
  body coverage plus a layer. WoW's slot goes under `source`. A consumer that
  wants slot-style UI derives it from coverage.

## Vocabulary

All of these are canonical names defined in the model's definition. The
source numbers for each live under its `source`.

| Concept | What it names | WoW source (under `source`) |
|---|---|---|
| **Body region** | An area of the body's composited skin texture: `arm_upper`, `arm_lower`, `hand`, `torso_upper`, `torso_lower`, `leg_upper`, `leg_lower`, `foot` (plus head/scalp regions). The definition gives each region's rectangle in the composite. | `CharComponentTextureSections` (SectionType + rect per layout) — **data** |
| **Body part** | A swappable piece of body mesh, with variants: `sleeve`, `glove_cuff`, `boot_shaft`, `boot_foot`, `knee`, `thigh`, `skirt`, `tabard`, `belt`, `chest`, … | geoset group number. The names come from our own visual read (`WIKI_FINDINGS/M2.md` table); a group not yet read keeps a neutral name `group_<n>` until it is. |
| **Attachment point** | Where a rigid model hangs: `head`, `shoulder_left`, `shoulder_right`, `back`, `hand_right`, `hand_left`, `hip_left`, … | `M2Attachment` id (**ref**, wiki table) |
| **Layer** | Stacking order: a named layer plus an integer `order` within it (default 0). Layers, bottom up: `skin` < `underwear` < `base` (shirt, leggings, socks) < `mid` (tunic, robe, dress, skirt, trousers, overalls) < `outer` (tabard, belt, bracers over sleeves, gloves, boots) < `over` (cloak, pauldrons, helm) | none: derived per garment kind (rule `garment_layer`), or for a garment cut from a source item per its slot (rule `source_slot_priority`, below); overridable by authoring |

Kinds are open strings. The schema carries a validated list of preset
kinds (`tunic`, `robe`, `leggings`, `skirt`, `boots`, …) and the default
layer of each. A kind outside that list, or a layer or order that differs
from the kind's default, is a warning:

- Declared on purpose (`"deviates": true` on the garment): reported as a
  known warning.
- Not declared: an unexpected warning. Something upstream is wrong (a
  misclassified item, a typo).

**Order within a layer.** Garments of one layer stack by `order`, lower
first. Overalls and trousers both sit in `mid` and both cover the thighs.
`mid -1` overalls under `mid 0` trousers, or the reverse, is a stated
choice rather than an accident of slot. A boot is `outer`, so it sits over
both whatever their order.

**Conflicts are reported, not guessed.** Among the garments a selection
wears, any group of two or more that share a layer *and* an order, and
overlap in coverage (a body region's pixels, or a body part's geometry), is
a conflict. Any number of garments can be in one conflict. The validator
(`validate-bundle`, and a `husk garments-check <selection>` report) lists
each conflict with its garments and the regions or parts they fight over.
A consumer resolves it by giving one of them another order; the format
never picks silently.

**Source slot priority.** Where the source states which of two garments
draws on top, that is not a conflict: WoW composites item textures and
picks shared body geosets by slot. So every garment husk cuts from a source
item takes its layer and order from the item's slot (`"layer_rule":
"source_slot_priority"`), not from its kind's default:

| Slot | Order | Layer (kind default when not named) |
|---|---|---|
| shirt | 1 | |
| legs (leggings, skirt, own hem) | 2 | |
| head | 3 | |
| feet | 4 | `mid` (kind default `outer`) |
| shoulder | 5 | |
| chest (tunic, robe, joined tunic) | 6 | |
| tabard | 7 | |
| waist | 8 | |
| wrist | 9 | |
| hands | 10 | |
| main hand, ranged | 11 | |
| off hand | 12 | |
| back | 13 | |

- The order is the slot's rank in the body composite's draw order, wow.export
  `src/js/wow/EquipmentSlots.js` `SLOT_LAYER` (shirt and legs 10, head and
  feet 11, shoulder and chest 13, tabard 17, waist 18, wrist 19, hands 20,
  main hand 21, off hand 22, back 23; the texture target is `SLOT_LAYER × 100
  + section`, drawn ascending, `CharMaterialRenderer.js` `update`). Ties are
  in slot-id order (shirt 4 before legs 7), as that stable sort leaves them:
  trousers draw over a shirt's thighs. **ref**: wow.export, which reads no
  table for it; the client's own order is not checked.
- Geometry agrees: wow.export `src/js/db/caches/DBItemGeosets.js`
  `GEOSET_PRIORITY` (from WMV `WoWItem.cpp`) gives sleeves to gloves over
  chest over shirt, the chest group to chest over shirt, and the skirt group
  to chest over legs. Every pair it ranks is ranked the same way by the
  texture order, so one order per garment carries both; no piece needs its
  own order.
- Boots move to `mid`: the feet slot draws its `LegLower` paint under a
  chest's (11 < 13), which `outer` would invert. Every other slot's kind
  layer already agrees with the draw order.
- A garment with this rule is not a layer or order deviation: the rule, not
  the kind, sets them. Its kind is still checked.
- Rank 1 and up keeps derived garments apart from authored ones, which
  default to order 0 and so sit under every derived garment of their layer.
  Two authored garments at one order still conflict, as do two derived
  garments the source does not rank (the same slot twice).

## Document shape (sketch)

A new document kind `garments`: one per corpus, with stable ids. A model
references it inline or externally, exactly like `appearances`.

```jsonc
{
  "$schema": "https://husk.invalid/schema/garments.schema.json",
  "schema_version": "0.4.0",
  "garments": [{
    "id": "<uuid>",
    "name": { "text": "Defias Tunic", "name_source": "source_item" },
    "kind": "tunic",                       // open string; validated preset list in the schema
    "layer": "mid",
    "order": 0,                            // within the layer; default 0
    // "deviates": true                    // kind/layer/order off the preset on purpose
    "covers": ["torso_upper", "torso_lower", "arm_upper", "leg_upper"],
    "pieces": [
      { "overlay":  { "region": "torso_upper", "texture": "<texture id>" },
        "source":   { "item": "<source item id>", "section": 3 } },
      { "overlay":  { "region": "leg_upper", "texture": "<texture id>",
                      "mask": "<texture id>" },          // the hem cut out of the legs texture
        "source":   { "item": "<legs source item id>", "section": 5 },
        "x-derived": "hem_uv_footprint" },
      { "geometry": { "part": "sleeve", "variant": 2 },
        "source":   { "item": "<source item id>", "geoset_group": 8, "value": 1 } },
      { "geometry": { "part": "skirt", "variant": 1 } , "source": { … } }
    ],
    "files": [ /* per-body picks of each piece's model/texture: the wearer's race/sex file */ ]
  }],
  "items": [{                              // the source items; the lint keeps "source" for raw objects

    "id": "<uuid>", "ref": { "table": "ItemModifiedAppearance", "row": 1234 },
    "garments": ["<tunic id>"],            // what wearing this item exactly means
    "source": { "slot": "chest", "inventory_type": 20 }
  }],
  "outfits": [{
    "id": "<uuid>", "name": { … }, "garments": ["…"],
    "source": { "table": "TransmogSet", "row": 77 }      // or an NPC's equipment, an ItemSet
  }]
}
```

Piece kinds:

- `overlay`: a texture painted onto a body region. Its optional `mask` says
  which pixels belong to this garment; no mask means all of them.
- `geometry`: shows variant N of a body part (WoW: an item geoset).
- `attached`: a rigid model at an attachment point (helm, pauldron, weapon).
- `skinned`: a model deformed by the body skeleton (WoW collection models),
  with its own geometry variants.
- `hides`: body parts the garment hides (a helm hides hair and ears; WoW's
  `HelmetGeosetData`).

Per-body files: an item model or texture depends on the wearer's race and
sex (`component_file_pick`). So each piece names its file per body, in
`files`, rather than once.

## Wearing: selections, conflicts, outfits

- A preset wears a list of garment ids, each at most once. There are no
  slots. `presets[].items` becomes `presets[].garments`. `selection.equipped`
  goes, see below.
- **Resolving what the worn garments show** is a pure function of layer and
  coverage, written in the schema as the consumer's rule:
  - Overlays composite bottom-up by layer.
  - When two garments set the same body part, the higher layer wins, then
    the higher order: robe skirt over trouser thighs, glove cuff over
    sleeve. This is WoW's priority table (gloves > chest > shirt for
    sleeves, chest > pants for trousers) expressed as layers and orders
    instead of slots ("Source slot priority").
  - A garment fully covered by a higher layer's geometry is hidden: leggings
    under a long robe.
- **Outfits** are named groups of garments: a transmog set, an NPC's
  equipment, a source item. Wearing an outfit is wearing its garments. A
  consumer can then swap one garment and keep the rest.
- **WoW-exact loading**: wear `items[].garments` for each equipped
  source item. Because the decomposition is lossless, this reproduces the
  client's look.

## How husk decomposes WoW items

### Stage 1: rules over DB2 data alone, ships in 0.4.0

| Source item content | Garments produced |
|---|---|
| Chest, no skirt geoset, overlays only above the waist | `shirt` or `tunic` (short): torso, arm and sleeve pieces |
| Chest with `LegUpper` overlay, no skirt geoset | `tunic`: the `LegUpper` overlay is its lower part |
| Chest with skirt geoset (group 13) | `robe` or `dress`: `LegLower` present means long. Skirt geometry and leg overlays are all one garment. |
| Legs, no skirt geoset | `leggings` (thigh, knee, leg overlays) |
| Legs with skirt geoset | `skirt` (the skirt mesh plus whichever leg overlay belongs to it, **see stage 2**) and `leggings` |
| Shirt | `shirt` (`base` layer, so a chest tunic draws over it) |
| Wrist | `bracers` (`ArmLower` overlay, cuff geoset) |
| Hands | `gloves` (`Hand` + `ArmLower` overlays, glove-cuff geoset) |
| Waist | `belt` (`TorsoLower` overlay, belt geoset, buckle model) |
| Feet | `boots` (`Foot` + `LegLower` overlays, boot geosets) |
| Shoulders | `pauldrons`: one garment with a left and a right `attached` piece. A selection may wear one side (12.x transmog splits shoulders). |
| Tabard, back, head, weapons | one garment each |

Which geoset group each slot's `GeosetGroup[n]` drives is **ref** (WMV
`WoWItem.cpp`, `CHARACTER_RENDERING.md` §6), so it has to be verified on
real models before the rules ship. Every stage-1 garment records its rule,
as `x-source {derived: garment_split}`.

### Stage 2: hems, from the meshes that show them

A hem comes from one of two sources (`WIKI_FINDINGS/GARMENTS.md`):

- **The body's hem mesh, geoset 1102** (group 11): a short flare under the
  belt, switched on by a legs item (`GeosetGroup[0]` = 1, 89 legs
  displays). Its paint is the band of the legs item's `LegUpper` texture
  that the mesh's UVs sample (rows 448–508 of the 2048×1024 layout on
  humanfemale_hd).
- **A hem submesh of the item's own model** (group 13, sometimes 11), on the
  item's own texture. A set's chest and legs often show the same mesh.

Either way the hem is separate geometry, so:

1. **The hem piece** is the hem mesh plus the pixels its UVs sample.
   - Body hem: the mask is the footprint of 1102's triangles in UV space,
     per body model, labelled `{derived: hem_uv_footprint}`. It cuts the
     hem's paint out of the legs texture exactly.
   - Item-model hem: the submesh and its own texture; no mask is needed.
2. **The rest stays with the leggings**, whole: the legs texture is not
   masked. The thigh mesh's waist band reads the same rows as the body hem,
   and it looks right both with the hem hidden and under a differently
   textured tunic (Luna, Blender, set 3540). So nothing is cut out of the
   leggings and nothing is painted in.
3. Both pieces keep the legs (or chest) item as their source, so the split
   loses nothing.
4. The hem piece joins the tunic from the same set (`TransmogSet` /
   `ItemSet`). If no tunic is identified, it becomes a `hem` garment of its
   own: still separable from the leggings, just not yet joined to a tunic.
   A mesh shown by both a set's chest and its legs is one hem piece,
   deduplicated by model and geoset.
5. Masks authored in the Blender addon (`{authored: …}`) remain possible,
   for a body model whose footprint turns out wrong.

**Worked example: transmog set 3540 (Luna, 2026-11-24).** A garment is the
body geometry it switches on plus the textures it samples; the hem mesh
moves from the legs item to the tunic.

| Garment | Geometry it switches on | Textures it samples |
|---|---|---|
| Tunic | sleeves 803, torso 2202, hem 1102 | the chest item's arm and torso sections (ArmUpper, ArmLower, TorsoUpper, TorsoLower); the legs item's `LegUpper` inside 1102's footprint |
| Leggings | thighs (trouser geometry other than the hem; 1102 is not switched on) | the legs item's `LegUpper`/`LegLower`, whole, as the source draws them |

The breeches' source item still produces both pieces (`items` lists
both garments for it), so wearing the WoW item reproduces the client look.
Wearing the leggings alone gives trousers with no hem, and wearing the tunic
over other leggings gives the hem in the tunic's own paint.

**NPC equipment: the hem by the NPC's own outfit.** An NPC's equipment
names displays, with no TransmogSet, and its outfit is fixed. So:

- A display's source item lists the display worn alone: a legs display's
  hem pieces are its own `hem` garment (`hem_own_garment`).
- An NPC whose own chest garment is a tunic wears, instead of that tunic,
  one joined tunic per chest and legs display pair: the tunic's pieces plus
  the legs' hem pieces (`hem_join_outfit`), minus an item-model hem mesh
  the chest already shows (same model file and geosets). Other NPCs with
  the same pair share it. It is not listed under either source item, whose
  lists stay the display alone; that is the one place a piece sits in two
  garments.
- Otherwise (a robe, or no chest) the NPC wears the legs' own `hem`
  garment. The chest's and legs' same item-model hem mesh is then drawn by
  both, as the client draws both items' models.

**Not hems:** the body's group-13 meshes 1302/1303 are robe skirts (a robe
hides the thighs), and 1301 is a thigh variant. 1104/1105 (knee length) are
not yet looked at.

## Bakes

An NPC's baked skin is valid only for one body (appearance id) plus one set
of garments: `covers_items` becomes `covers_garments`. Wearing anything else
means compositing from the pieces instead.

## What this removes or changes in 0.4.0

- `definition.items` → the `garments` document reference.
- `items[].textures[].texture_type`, `model_position` and
  `overlays[].component_section` → canonical region/part/attachment names,
  with the numbers under `source`.
- `presets[].items[]` with `slot` → `presets[].garments[]`. `slot` moves to
  `items[].source`.
- `selection.equipped` → removed. Nothing writes it: the schema slot, the
  `CharacterSelection::equippedItems` struct and
  `assembleCharacterSelection` exist, but no writer or command calls them,
  only tests. The gear an `--appearance` file names already goes to
  `presets[].items`. Under this design it becomes `presets[].garments`.

## Decisions (Luna, 2026-11-24)

1. **`kind`** is an open string. A validated preset list is the default,
   and anything off it warns. A deviation declared on purpose is a known
   warning; an undeclared one flags that something upstream is wrong.
2. **Layer** is named, with an integer `order` within it, and conflicts
   are reported across any number of garments (above).
3. **Hem masks** are in 0.4.0, made from the hem mesh's UV footprint
   (stage 2).
4. **Under a removed hem**: whatever is worn below. The hem is its own
   mesh, so no fill is needed.
5. **Pairs** are one garment with sided pieces, wearable per side.

## Evidence still needed before it ships

Findings: `WIKI_FINDINGS/GARMENTS.md` (12.1.5). Footprint code:
`src/garment_footprint.hpp`, debug command `husk uv-footprint`.

- **Settled**: the slot → geoset-group table, verified structurally on
  humanmale_hd, earthen and dracthyr bodies for shirt, chest/robe, waist,
  legs, feet, hands, back and tabard; head, shoulder and the -1 values are
  unverifiable on the body ("Slot → GeosetGroup table").
- **Settled**: region rectangles; all playable layouts share SectionType
  0-7 ("Region rectangles").
- **Settled, contradicts stage 2**: no playable body has a hem mesh of its
  own. Group 13 is 1301 (a thigh variant) and the full-length skirts 1302 /
  1303, which sample all of `LegUpper` below the belt rows plus `LegLower`,
  a superset of the thigh footprint, not disjoint from it. Variants are
  nested, not identical. Legacy bodies lack 1303 as some HD bodies do; the
  group-13 set does not change at Dragonflight ("Leg-region footprints").
- **Settled**: corpus counts by expansion: chest leg overlays are robes
  (1,490 of 1,520 skirted; 30 tunics), 246 legs displays with a skirt,
  200 of 200 sampled legs textures non-empty inside the footprint, so the
  footprint test selects every one ("Corpus counts").
- **Settled (Luna confirms)**: body group-13 skirts are robe geometry; a
  robe hides the thighs, so skirt and thigh are never visible together.
- **Settled, contradicts stage 2's mechanism**: a modern hem is a group-13
  (sometimes 11) submesh of the set's item model, on the item's own
  texture; no item mesh samples the body composite (0 of 1,687 geosets).
  94 chest displays show one without any body skirt, 291 legs displays
  show one (289 the same mesh as a chest of the set). Stage 2 needs a new
  mechanism; options in `WIKI_FINDINGS/GARMENTS.md` "What the evidence
  supports for the hem split" ("Item models").
- **Settled**: 1303 is selected by no DB2 row on a playable body; same
  extent as 1302, more triangles ("Which bodies have which group-13
  variants").
- **Open**: the item-model slot rule (show `group × 100 + 1 +
  AttachmentGeosetGroup[i]`) is wow.export's, unverified against the
  client.
