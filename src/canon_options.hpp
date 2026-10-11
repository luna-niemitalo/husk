#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "canon_effects.hpp"  // UnresolvedField
#include "canon_material.hpp"
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// A model's options (CANONICAL_MODEL.md's Definition layer): what a
// renderer may choose, each choice valid on its own whether or not the
// source ever combined it with the others. Geoset options are the mesh's
// own geosets. Presets (the Selection layer) are the combinations a
// source is known to use, and where.
namespace husk::canon {

// A texture a material layer takes from outside the model: the layer's own
// texture is a default, `options` the textures the slot may hold.
// `sourceType` is the source's replaceable-texture type (M2 texture type).
struct TextureSlot {
    std::string name;
    uint32_t sourceType = 0;
    std::vector<TextureRef> options;
};

// One recolouring ramp for particles, RGBA 0..1.
struct ParticleColorRamp {
    std::array<float, 4> start{};
    std::array<float, 4> mid{};
    std::array<float, 4> end{};
};

// One way to recolour the model's particles: ramp i recolours emitters
// whose particle colour index is 11 + i.
struct ParticleColorOption {
    Ref ref;
    std::vector<ParticleColorRamp> ramps;
};

struct SlotChoice {
    uint32_t slot = 0;    // index into Model::textureSlots
    uint32_t option = 0;  // index into that slot's options
};

// A source row that uses a preset, and the scale it draws the model at:
// `scale` the row's own, `baseScale` the model data row's.
struct PresetSource {
    Ref ref;
    float scale = 1;
    float baseScale = 1;
};

struct Preset {
    std::vector<SlotChoice> textures;
    // The mesh geoset ids shown; every other geoset is hidden. nullopt:
    // the preset doesn't choose geosets.
    std::optional<std::vector<uint32_t>> shownGeosets;
    std::optional<uint32_t> particleColors;  // index into Model::particleColorOptions
    std::optional<float> opacity;            // 0..1; nullopt = opaque
    std::vector<PresetSource> sources;
    // Source values the preset names but husk can't place in an option.
    std::vector<UnresolvedField> unresolved;
};

}  // namespace husk::canon
