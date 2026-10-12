#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// husk::canon: see canon_policy.hpp for the layer this belongs to.
namespace husk::canon {

// A source value with no field of its own yet, carried raw so nothing is
// lost (REFACTOR/EFFECTS.md "unresolved").
struct UnresolvedField {
    enum class Kind { Scalar, Scalars, Bits, Color, Count, Id, Angle, UnitVector, Words };
    std::string field;  // in the source's own terms
    Kind kind = Kind::Scalar;
    // uint32_t: Bits; vector<uint32_t>: Words.
    std::variant<double, std::vector<double>, uint32_t, std::vector<uint32_t>> value;
    std::optional<std::string> candidateRole;
};

}  // namespace husk::canon
