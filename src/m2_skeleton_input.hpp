#pragma once

#include <vector>

#include "canon_skeleton.hpp"
#include "m2_skeleton.hpp"

// husk::m2input: the M2 input module -- see m2_canon_input.hpp's own doc
// comment for the split this belongs to. Turns raw m2::Bone data into a
// canon::Skeleton. Tier-0 naming only (m2::keyBoneName, else a synthesized
// "bone_<index>"): no attachment/event/topology naming tier exists, the
// scope canon_skeleton.hpp's own Joint::ref doc comment commits to.
namespace husk::m2input {

// Throws std::runtime_error on an out-of-range parent index or a cyclic
// parent chain.
canon::Skeleton assembleSkeleton(const std::vector<m2::Bone>& bones);

}  // namespace husk::m2input
