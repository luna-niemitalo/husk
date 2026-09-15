#pragma once

#include "canon_curve.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// CANONICAL_MODEL.md's Resources layer -- one bone's worth of assembled
// TRS curves for one sequence, a pure value type. Assembling one of these
// from a real M2's own track data is `husk::m2input`'s job
// (m2_animation_input.hpp), not canon::'s -- see CANONICAL_MODEL.md's
// extended I1 section for why the struct and the M2-consuming assembler
// that builds it live in different files/namespaces now.
namespace husk::canon {

// One bone's worth of assembled curves for one M2Sequence (or global
// sequence). Any of the three may hold zero keyframes (that property
// simply has no data for this sequence) -- m2input::assembleBoneAnimation
// only returns nullopt when all three are empty, mirroring
// buildJointAnimation's own "nothing to animate" early return.
struct BoneAnimationCurves {
    VecCurve translation;
    QuatCurve rotation;
    VecCurve scale;
};

}  // namespace husk::canon
