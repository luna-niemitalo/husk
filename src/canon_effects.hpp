#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "canon_primitives.hpp"
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// REFACTOR/EFFECTS.md: an effect is a behaviour defined once, in an effect
// document (effects/, written as is); a placement-set instance places it
// and supplies its inputs, its classic look and what isn't understood yet.
// Only the instance side lives here: effect documents are data files, not
// assembled values.
namespace husk::canon {

// An input value. Which kind it is comes from the effect's declaration;
// these are the value shapes husk writes so far.
using InputValue = std::variant<double, int64_t, bool, Vec3>;

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
};

struct UnresolvedField {
    enum class Kind { Scalar, Scalars, Bits, Color, Count, Id, Angle, UnitVector };
    std::string field;  // in the source's own terms
    Kind kind = Kind::Scalar;
    std::variant<double, std::vector<double>, uint32_t> value;  // uint32_t: Bits
    std::optional<std::string> candidateRole;
};

// What makes a placement-set instance an effect instance. The instance's
// `asset` names the effect (its document's stem).
struct EffectPlacement {
    std::vector<std::pair<std::string, InputValue>> inputs;
    std::vector<std::pair<std::string, StyleBlock>> style;  // by the effect's source id
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
