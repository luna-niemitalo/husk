#pragma once

#include <vector>

#include "canon_skeleton.hpp"
#include "m2_skeleton.hpp"

// husk::canon: assembly of canon::Skeleton from raw m2::Bone data -- the
// adjacent twin of commands::buildSkeleton (export_skeleton.hpp), built to
// prove structural convergence (REFACTOR/README.md stage 3's gate) without
// touching the existing pipeline. Tier-0 naming only (m2::keyBoneName) --
// applyContextualBoneNames' attachment/event/topology tiers run later in the
// real pipeline and are deliberately out of scope here, same scope
// canon_skeleton.hpp's own Joint::ref doc comment already commits to.
namespace husk::canon {

// Throws std::runtime_error on an out-of-range parent index or a cyclic
// parent chain -- the same two malformed-input cases
// commands::buildSkeleton guards against (checkNoBoneCycles,
// export_skeleton.cpp), reimplemented here since that function is private
// to its own translation unit.
Skeleton assembleSkeleton(const std::vector<m2::Bone>& bones);

}  // namespace husk::canon
