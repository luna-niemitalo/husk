#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "canon_model.hpp"
#include "creature_display_db2.hpp"

// husk::creatureinput: a model's creature variants from the
// CreatureModelData -> CreatureDisplayInfo chain. Displays that dress the
// model identically (textures, geosets, particle colours, opacity) are one
// variant listing every such display with its scale.
namespace husk::creatureinput {

struct VariantReport {
    size_t displays = 0;  // displays of this model, humanoid ones included
    size_t humanoid = 0;  // dressed through the character path; not variants
};

// `model` must already carry its mesh and textureSlots. `texture` resolves
// a FileDataID (each distinct one is asked once).
std::vector<canon::Variant> assembleVariants(const creaturedisplay::Data& data, uint32_t modelFileDataId,
                                             const canon::Model& model,
                                             const std::function<canon::TextureRef(uint32_t)>& texture,
                                             VariantReport& report);

}  // namespace husk::creatureinput
