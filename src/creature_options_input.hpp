#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "canon_garment_builder.hpp"
#include "canon_item.hpp"
#include "canon_model.hpp"
#include "creature_display_db2.hpp"

// husk::creatureinput: a creature model's options and presets from the
// CreatureModelData -> CreatureDisplayInfo chain. Every texture a display
// puts in a slot becomes one of that slot's options (a slot the model has
// no layer for is added, as is an inferred one for the fourth entry, which
// has no texture type), every particle colour row one particle colour
// option. A character model gets its customization definition; a
// humanoid NPC's baked skin is an option of the lowest-type slot, its
// customization choices index that definition, its equipment becomes
// garments (canon_garment_builder.hpp NpcGarments). Each distinct
// combination a display uses is a preset listing every such display with
// its scale.
namespace husk::creatureinput {

struct OptionsReport {
    size_t displays = 0;  // displays of this model, humanoid ones included
    size_t humanoid = 0;  // humanoid NPCs (baked skin, customization, items)
    std::string notes;    // what building the customization definition reported
    // Choice textures whose target no layout layer draws, whose material
    // names no texture, or whose related choice isn't the model's.
    size_t unplacedChoiceTextures = 0;
};

// How a humanoid NPC's equipment becomes garments with pieces. `resolve`
// gives a display as an item (canon::assembleDisplayItem, files picked for
// the model's wearer), asked once per display; `slot` is empty for a slot
// with no name. nullopt: not resolvable, the garment carries identity only.
struct NpcEquipmentInputs {
    std::function<std::optional<canon::Item>(uint32_t itemDisplayInfo, const std::string& slot)> resolve;
    canon::garments::Inputs garments;
};

// `model` must already carry its mesh and textureSlots; fills the slots'
// options, particleColorOptions and presets. `texture` resolves a
// FileDataID (each distinct one is asked once). `equipment` nullptr: NPC
// equipment by identity only.
void addCreatureOptions(const creaturedisplay::Data& data, uint32_t modelFileDataId, canon::Model& model,
                        const std::function<canon::TextureRef(uint32_t)>& texture, OptionsReport& report,
                        const NpcEquipmentInputs* equipment = nullptr);

}  // namespace husk::creatureinput
