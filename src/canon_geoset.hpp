#pragma once

#include <cstdint>

#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// CANONICAL_MODEL.md's "Geosets are first-class" -- a pure value type.
namespace husk::canon {

// A geoset has no FileDataID and no DB2 row of its own -- it's
// M2SkinSection::skinSectionId, a plain array-position-shaped number
// (skin.hpp), so RecordIndex is the fitting Identity per I6. name/source
// stay at their None default until something layers a real choice name on
// top (ChrCustomizationOption/Choice) -- fromRawId doesn't have one to give.
struct Geoset {
    Ref ref;
    uint32_t group = 0;
    uint32_t variant = 0;

    // group*100+variant is M2SkinSection::skinSectionId's own
    // decomposition; stated once here so no call site re-derives it.
    static Geoset fromRawId(uint32_t rawId) {
        Geoset g;
        g.ref.id = RecordIndex{rawId};
        g.group = rawId / 100;
        g.variant = rawId % 100;
        return g;
    }
};

}  // namespace husk::canon
