#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "canon_material.hpp"  // TextureRef, FramebufferBlend
#include "canon_primitives.hpp"
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// REFACTOR/EFFECTS.md: an effect is a behaviour defined once, in an effect
// document (effects/, written as is); a placement-set instance places it
// and supplies its inputs, its classic look and what isn't understood yet.
// Only the instance side lives here: effect documents are data files, not
// assembled values.
namespace husk::canon {

struct NamedInput;

// An input value. Which kind it is comes from the effect's declaration;
// these are the value shapes husk writes so far. A list holds a list
// input's elements, a record a record input's fields.
struct InputValue {
    std::variant<double, int64_t, bool, Vec3, TextureRef, std::vector<InputValue>, std::vector<NamedInput>> value;
};

struct NamedInput {
    std::string name;
    InputValue value;
};

struct LinearFog {
    float start = 0;  // view distance where fog begins
    float end = 0;    // view distance where fog is full
    Vec3 color;       // RGB 0..1 as stored
};

// The classic client's fog over view distance, by block; an absent block
// inherits from the enclosing fog.
struct DistanceFog {
    std::optional<LinearFog> linear;
};

// How the classic client drew one effect source.
struct StyleBlock {
    std::optional<DistanceFog> distanceFog;
    bool exitBlend = false;  // draws nothing; scales the enclosing fog down near the exits
    std::optional<DistanceFog> underwater;  // the look while the viewer is under water
    // The ambient colour the source overrides inside its region: the three
    // colours in source order, RGBA 0..1 as stored. Which one the client
    // uses is unverified (the wiki ties it to flag 0x1, contradictorily).
    std::optional<std::array<std::array<float, 4>, 3>> ambientColors;
    std::optional<uint32_t> maxParticles;  // the classic particle system's cap
    // How the classic client drew the source's particles.
    struct Draw {
        std::optional<FramebufferBlend> blend;  // nullopt when the source mode has no canon name
        std::array<float, 4> color{};            // RGBA multiplier as stored
    };
    std::optional<Draw> draw;
};

// One effect source's classic look: one block, or for a source repeated
// per list element (for_each), one per element.
struct SourceStyle {
    std::string source;
    std::vector<StyleBlock> blocks;
    bool perElement = false;
};

struct UnresolvedField {
    enum class Kind { Scalar, Scalars, Bits, Color, Count, Id, Angle, UnitVector, Words };
    std::string field;  // in the source's own terms
    Kind kind = Kind::Scalar;
    // uint32_t: Bits; vector<uint32_t>: Words.
    std::variant<double, std::vector<double>, uint32_t, std::vector<uint32_t>> value;
    std::optional<std::string> candidateRole;
};

// What makes a placement-set instance an effect instance. The instance's
// `asset` names the effect (its document's stem).
struct EffectPlacement {
    std::vector<NamedInput> inputs;
    std::vector<SourceStyle> style;
    std::vector<UnresolvedField> unresolved;
    SourceRecord origin;
};

// A reference to one instance of an owned placement set: stable across
// list order, so a part's fog list survives instances being added or left
// out.
struct InstanceRef {
    uint32_t set = 0;  // index into the owner's placement sets
    uint32_t id = 0;   // the instance's id
};

}  // namespace husk::canon
