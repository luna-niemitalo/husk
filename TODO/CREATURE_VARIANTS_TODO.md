# TODO: creature variants — open items

Scope: dual-use (canon bundle). What exists: `REFACTOR/BUNDLE_FORMAT.md`
"Texture slots and variants"; facts in `WIKI_FINDINGS/M2.md` "Creature
display".

1. **Creatures that alternate between models.** `Creature` +
   `CreatureXDisplayInfo` give a creature several displays with
   probabilities; 3,272 of 4,729 multi-display creatures span more than
   one model. A variant can't swap the model, so this needs a home above
   the model document (design call, Luna).
2. **Humanoid NPC displays** (55,799, `ExtendedDisplayInfoID` != 0):
   `CreatureDisplayInfoExtra`'s baked skin, `CreatureDisplayInfoOption`'s
   customization choices, `NPCModelItemSlotDisplayInfo`'s gear. Skipped
   and counted today.
3. **Viewer-conditional displays**: `CreatureDisplayInfoCond` (134 rows,
   model and textures by the viewer's race/class/gender) and
   `ConditionalCreatureModelID` (1,021 displays). Not read.
4. **The fourth texture variation**: no texture type in the models that
   use it; carried as `unresolved`.
5. **Emitter textures in replaceable slots**: a particle/ribbon texture
   input doesn't name its slot, so a variant can't retexture it.
6. **No default variant**: a slot layer's own texture is the fuzzy-pool
   guess (often unresolved); nothing marks which variant a placed model
   wears.
7. **Geoset rule** is wow.export's, not checked against the client.
8. **Display fields not carried**: `ObjectEffectPackageID`,
   `StateSpellVisualKitID`, `AnimReplacementSetID`, `SizeClass`,
   `DissolveEffectID`, `MountPoofSpellVisualKitID`.
