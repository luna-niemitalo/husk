#pragma once

#include <string>
#include <vector>

#include "canon_skeleton.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// Chain/symmetry detection + label assembly for DESIGN.md's "canon:: bone
// naming" scheme -- consumes canon_bone_stem.hpp's stem sequence, produces
// the "consistent" tier (DESIGN.md tier 2) canon::Joint::structuralLabel
// wires onto. No existing pipeline converges against this (genuinely new
// behavior) -- correctness bar is internal consistency (valid format, no
// collisions), determinism, and a mirror axis verified against real data.
namespace husk::canon {

// One label per skeleton.joints entry, same order/size, every entry
// non-empty -- computed unconditionally, named or not (DESIGN.md's "apply
// more broadly" scope note). Assumes skeleton.joints is already a valid,
// cycle-free forest (assembleSkeleton guarantees this before calling
// here) -- no cycle/range re-validation here (I: the interior trusts its
// inputs).
std::vector<std::string> computeStructuralLabels(const Skeleton& skeleton);

}  // namespace husk::canon
