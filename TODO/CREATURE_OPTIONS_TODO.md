# TODO: creature options — open items

Scope: dual-use (canon bundle). What exists: `REFACTOR/BUNDLE_FORMAT.md`
"Options and presets"; facts in `WIKI_FINDINGS/M2.md` "Creature
display".

1. **Creatures that alternate between models.** `Creature` +
   `CreatureXDisplayInfo` give a creature several displays with
   probabilities; 3,272 of 4,729 multi-display creatures span more than
   one model. An option can't swap the model, so this needs a home above
   the model document (design call, Luna).
2. **Humanoid NPC presets name, don't resolve**: their customization
   choices and items are refs only (`definition.customization_choices`,
   `definition.items`). Next: the character model's
   customization definition in the bundle (options, choices, their
   geosets and textures) so a preset's choices index it; then item
   documents (ItemDisplayInfo geometry, geosets, attachment) for
   `items`. The bake-slot rule (lowest texture type) and the slot names
   are wow.export's, unchecked against the client.
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
8. **Character animation size**: `humanmale_hd`'s `resources.rig` is
   94 MB of its 123 MB manifest (character models never reached the
   bundle before the SKID skeleton fix).
9. **Display fields not carried**: `ObjectEffectPackageID`,
   `StateSpellVisualKitID`, `AnimReplacementSetID`, `SizeClass`,
   `DissolveEffectID`, `MountPoofSpellVisualKitID`.
