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

// A texture the model takes from outside: a layer reading the slot has its
// own texture as a default, `options` the textures the slot may hold. A
// slot no layer reads still lists its options. `sourceType`: the source's
// replaceable-texture type (M2 texture type), when it has one.
// `inferredFrom`: how husk tied the slot to the model, when the source
// doesn't say so directly; nullopt = stated by the source.
struct TextureSlot {
    std::string name;
    std::optional<uint32_t> sourceType;
    std::vector<TextureRef> options;
    std::optional<std::string> inferredFrom;
    // The material document whose composite builds the slot's texture from
    // customization choices (index into Model::materialDocuments).
    std::optional<uint32_t> composite;
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

// A named thing that uses a source row: an NPC (Creature row, its name in
// the ref) and its title, if any.
struct PresetUser {
    Ref ref;
    std::string title;  // empty = none
};

// A source row that uses a preset, and the scale it draws the model at:
// `scale` the row's own, `baseScale` the model data row's.
struct PresetSource {
    Ref ref;
    float scale = 1;
    float baseScale = 1;
    std::vector<PresetUser> users;
};

// A customization choice a preset makes: indices into Model::customization
// (canon_definition.hpp) -- the option, and the choice within it.
struct CustomizationPick {
    uint32_t option = 0;
    uint32_t choice = 0;
};

// A texture a customization choice binds to an image input of a material
// document (its composite), only while `onlyWith` is also chosen when set.
struct ChoiceTexture {
    CustomizationPick choice;
    uint32_t material = 0;  // index into Model::materialDocuments
    std::string input;
    TextureRef texture;
    std::optional<CustomizationPick> onlyWith;
};

// A customization choice's .bone correction set (ChrCustomizationElement
// -> ChrCustomizationBoneSet), when the set itself was loaded.
struct ChoiceCorrectionSet {
    CustomizationPick choice;
    uint32_t set = 0;  // index into Model::correctionSets
};

// A body a preset points at (REFACTOR/BUNDLE_FORMAT.md "Appearances"):
// the choices that make it, without what it wears. `key`: the source
// values that make it (customization choices by row id, then texture
// FileDataIDs by display slot, geosets, particle colour, alpha); with the
// model's identity it is
// the appearance's stable id, and equal keys are one appearance.
struct Appearance {
    std::string key;
    std::vector<SlotChoice> textures;
    // The mesh geoset ids shown; every other geoset is hidden. nullopt:
    // the body doesn't choose geosets.
    std::optional<std::vector<uint32_t>> shownGeosets;
    std::optional<uint32_t> particleColors;  // index into Model::particleColorOptions
    std::optional<float> opacity;            // 0..1; nullopt = opaque
    std::vector<CustomizationPick> customization;
    // Body values the source names but husk can't place in an option.
    std::vector<UnresolvedField> unresolved;
};

// A body plus what it wears, and the source rows that use the combination.
// `key`: the body's key plus the outfit's source values; the preset's
// stable id with the model's identity.
struct Preset {
    std::string key;
    std::optional<uint32_t> appearance;  // index into Model::appearances; nullopt = the body chooses nothing
    std::vector<SlotChoice> textures;    // the outfit's: a humanoid NPC's bake
    // Model::garments indices the bake in `textures` draws in, so a consumer
    // that swaps a garment knows the bake no longer matches. Empty: a bake
    // with no gear.
    std::vector<uint32_t> bakeCovers;
    std::vector<uint32_t> garments;  // indices into Model::garments.garments, each once
    std::vector<PresetSource> sources;
    // Outfit values the source names but husk can't place in an option.
    std::vector<UnresolvedField> unresolved;
};

}  // namespace husk::canon
