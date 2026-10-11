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
// A model's variants: the ways one model is dressed (a creature's skins,
// geosets and particle colours), each a complete choice over the model's
// selectable features. "Variant" rather than "style": an effect
// instance's `style` is its classic look.
namespace husk::canon {

// A texture a material layer takes from outside the model: the model's
// own texture is a default the placer may replace. `sourceType` is the
// source's replaceable-texture type (M2 texture type, e.g. 11).
struct TextureSlot {
    std::string name;
    uint32_t sourceType = 0;
};

struct SlotTexture {
    uint32_t slot = 0;  // index into Model::textureSlots
    TextureRef texture;
};

// One recolouring ramp for particles, RGBA 0..1.
struct ParticleColorRamp {
    std::array<float, 4> start{};
    std::array<float, 4> mid{};
    std::array<float, 4> end{};
};

// A source row that dresses the model this way, and the scale it is
// drawn at: `scale` the row's own, `baseScale` the model data row's.
struct VariantSource {
    Ref ref;
    float scale = 1;
    float baseScale = 1;
};

struct Variant {
    std::vector<SlotTexture> textures;
    // The mesh geoset ids shown; every other geoset is hidden. nullopt:
    // the variant leaves geosets to the model's default.
    std::optional<std::vector<uint32_t>> shownGeosets;
    // Ramp i recolours emitters whose particle colour index is 11 + i.
    std::vector<ParticleColorRamp> particleColors;
    std::optional<float> opacity;  // 0..1; nullopt = opaque
    std::vector<VariantSource> sources;
    std::vector<UnresolvedField> unresolved;
};

}  // namespace husk::canon
