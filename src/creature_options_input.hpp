#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "canon_model.hpp"
#include "creature_display_db2.hpp"

// husk::creatureinput: a creature model's options and presets from the
// CreatureModelData -> CreatureDisplayInfo chain. Every texture a display
// puts in a slot becomes one of that slot's options, every particle colour
// row one particle colour option; each distinct combination a display uses
// is a preset listing every such display with its scale.
namespace husk::creatureinput {

struct OptionsReport {
    size_t displays = 0;  // displays of this model, humanoid ones included
    size_t humanoid = 0;  // dressed through the character path; not read here
};

// `model` must already carry its mesh and textureSlots; fills the slots'
// options, particleColorOptions and presets. `texture` resolves a
// FileDataID (each distinct one is asked once).
void addCreatureOptions(const creaturedisplay::Data& data, uint32_t modelFileDataId, canon::Model& model,
                        const std::function<canon::TextureRef(uint32_t)>& texture, OptionsReport& report);

}  // namespace husk::creatureinput
