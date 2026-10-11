# TODO: creature options — open items

Scope: dual-use (canon bundle). What exists: `REFACTOR/BUNDLE_FORMAT.md`
"Options and presets"; facts in `WIKI_FINDINGS/M2.md` "Creature
display".

1. **Creatures that alternate between models.** `Creature` +
   `CreatureXDisplayInfo` give a creature several displays with
   probabilities; 3,272 of 4,729 multi-display creatures span more than
   one model. An option can't swap the model, so this needs a home above
   the model document (design call, Luna).
2. **Humanoid NPC displays** (55,799, `ExtendedDisplayInfoID` != 0):
   `CreatureDisplayInfoExtra`'s baked skin, `CreatureDisplayInfoOption`'s
   customization choices, `NPCModelItemSlotDisplayInfo`'s gear. Skipped
   and counted today.
3. **Viewer-conditional displays**: `CreatureDisplayInfoCond` (134 rows,
   model and textures by the viewer's race/class/gender) and
   `ConditionalCreatureModelID` (1,021 displays). Not read. These condition
   on the viewer, not on other options; if an option-on-option
   constraint ever turns up, it needs a constraint form in the format.
4. **The fourth texture variation**: no texture type in the models that
   use it; now a `variation_4` slot whose options carry `inferred_from` (the model is known from the display; which part it textures is not).
5. **Emitter textures in replaceable slots**: a particle/ribbon texture
   input doesn't name its slot, so a slot option can't retexture it.
6. **Default texture**: a slot layer's own texture is the fuzzy-pool
   guess (often unresolved) rather than one of the slot's options.
7. **Geoset rule** is wow.export's, not checked against the client.
8. **Display fields not carried**: `ObjectEffectPackageID`,
   `StateSpellVisualKitID`, `AnimReplacementSetID`, `SizeClass`,
   `DissolveEffectID`, `MountPoofSpellVisualKitID`.
