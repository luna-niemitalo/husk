# Garments: what a body wears, independent of source item slots

**Status: proposal (2026-11-24), awaiting Luna's decisions at the end.
Nothing here exists yet.** Replaces the model's `definition.items`,
`selection.equipped` and `presets[].items` in schema 0.4.0.

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
| **Layer** | Stacking order, an ordered set: `skin` < `underwear` < `base` (shirt, leggings, socks) < `mid` (tunic, robe, dress, skirt) < `outer` (tabard, belt, bracers over sleeves, gloves) < `over` (cloak, pauldrons, helm) | none: derived per garment kind (rule `garment_layer`), overridable by authoring |

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
    "kind": "tunic",                       // open vocabulary, recommended list in the schema
    "layer": "mid",
    "covers": ["torso_upper", "torso_lower", "arm_upper", "leg_upper"],
    "pieces": [
      { "overlay":  { "region": "torso_upper", "texture": "<texture id>" },
        "source":   { "item": "<source item id>", "section": 3 } },
      { "overlay":  { "region": "leg_upper", "texture": "<texture id>",
                      "mask": "<texture id>" },          // the hem cut out of the legs texture
        "source":   { "item": "<legs source item id>", "section": 5 },
        "x-derived": "hem_split" },
      { "geometry": { "part": "sleeve", "variant": 2 },
        "source":   { "item": "<source item id>", "geoset_group": 8, "value": 1 } },
      { "geometry": { "part": "skirt", "variant": 1 } , "source": { … } }
    ],
    "files": [ /* per-body picks of each piece's model/texture: the wearer's race/sex file */ ]
  }],
  "source_items": [{
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
  - When two garments set the same body part, the higher layer wins: robe
    skirt over trouser thighs, glove cuff over sleeve. This is WoW's
    priority table (gloves > chest > shirt for sleeves, chest > pants for
    trousers) expressed as layers instead of slots.
  - A garment fully covered by a higher layer's geometry is hidden: leggings
    under a long robe.
- **Outfits** are named groups of garments: a transmog set, an NPC's
  equipment, a source item. Wearing an outfit is wearing its garments. A
  consumer can then swap one garment and keep the rest.
- **WoW-exact loading**: wear `source_items[].garments` for each equipped
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

### Stage 2: pixel-level splits (the hem in the legs texture)

DB2 data can't say which pixels of a legs item's `LegUpper` texture are a
tunic hem. The format is ready for it in 0.4.0: masks, plus pieces that
move between garments. How the masks get made:

1. **Authored first**: a mask drawn in the Blender addon or as a mask file,
   with provenance `{authored: …}`. This is exact, but it is manual.
2. **Detected later, as a research track**, with a heuristic and evidence:
   - The candidates are a legs item and a chest item of the same set
     (`TransmogSet` / `ItemSet` / shared ItemAppearance naming).
   - The hem is the band of the legs texture's `LegUpper` that continues
     the chest's `TorsoLower` across the waist seam (colour/pattern
     continuity) and differs from the legs' own `LegLower` material.
   - Before it is trusted: measured on a seeded sample against hand-made
     masks, with renders against in-game screenshots. It is labelled
     `{derived: hem_split}` with its confidence.
3. The hem piece moves to the tunic from the same set. If no tunic is
   identified, it becomes a `hem` garment of its own: still separable from
   the leggings, just not yet joined to a tunic.

**What shows under a removed hem.** The source never drew the leggings
under the hem. The default is honest: the masked area shows whatever is
below (body skin, underwear, another garment). An optional `underpaint`
piece can fill it in, authored or inpainted, flagged `synthesized: true` so
a consumer knows the source never drew it.

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
  `source_items[].source`.
- `selection.equipped` → removed. Nothing writes it: the schema slot, the
  `CharacterSelection::equippedItems` struct and
  `assembleCharacterSelection` exist, but no writer or command calls them,
  only tests. The gear an `--appearance` file names already goes to
  `presets[].items`. Under this design it becomes `presets[].garments`.

## Decisions for Luna

1. **Garment `kind`: open string or closed enum?** Recommend open, with a
   recommended list in the schema description. New kinds then need no
   schema bump.
2. **Layer: a named ordered set (above) or a number?** Recommend named.
   Names read better and an authoring tool can't produce ties.
3. **Hem split order: authored masks in 0.4.0 and detection as a later
   research track?** Recommend yes. The format carries masks from day one,
   and detection fills them in later, which is an additive change.
4. **Under a removed hem: show what's below by default, optional
   synthesized `underpaint`?** Recommend yes.
5. **Pairs (pauldrons, gloves, bracers): one garment with sided pieces,
   wearable per side?** Recommend yes.

## Evidence still needed before stage 1 ships

- The slot → geoset-group table on 2+ races (**ref** today).
- Region rectangles per texture layout from `CharComponentTextureSections`
  (**data**, not yet read).
- How many chest items carry `LegUpper` / `LegLower` overlays, and how many
  legs items carry a skirt geoset: a corpus count that sizes stage 2.
