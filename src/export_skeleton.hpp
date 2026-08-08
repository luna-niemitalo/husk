#pragma once

#include <vector>

#include "gltf.hpp"
#include "m2.hpp"

// Bind-pose skeleton/skinning construction -- split out of cmd_export.cpp
// per FILE_SPLIT_TODO.md's Item 1.
namespace husk::commands {

// Builds a bind-pose Skeleton from M2's bones array: `bone.parentBone` is a
// direct index into the same bones array (-1 for a root), and each joint's
// local (parent-relative) translation is just the difference of the two
// bones' absolute pivots -- valid because M2's bind pose has no baked
// rotation/scale (see gltf::Skeleton's doc comment). Throws
// std::runtime_error if any bone's parentBone is out of range, or if the
// parent chain loops back on itself (see checkNoBoneCycles in the .cpp).
gltf::Skeleton buildSkeleton(const std::vector<m2::Bone>& bones);

// Second bone-naming pass, run once `skeleton.attachments`/`skeleton.events`
// are populated (attachPlacementNodes, cmd_export.cpp) -- separate from
// buildSkeleton itself because those two arrays aren't resolved yet at that
// point. For a joint with no real `keyBoneId` name: first tries the
// joint's own attachment id (m2::attachmentTypeName, a real 61-entry
// wowdev.wiki table, same authority as keyBoneId), then its own event
// identifier (m2::eventName, a weaker signal -- the source table itself has
// real undocumented gaps), then falls through to the existing tier-1
// structural chain-interpolation (deduceBoneNamesByTopology) -- run last so
// it can use attachment/event-derived names as landmarks too. See
// TODO/BONE_NAME_DEDUCTION_TODO.md for the full tier breakdown.
void applyContextualBoneNames(gltf::Skeleton& skeleton);

// Lifts M2Vertex's raw bone_weights[4]/bone_indices[4] into glTF's
// JOINTS_0/WEIGHTS_0 shape: weights normalized from 0-255 to 0.0-1.0,
// joint indices copied verbatim (M2Vertex.bone_indices are direct indices
// into the M2's `bones` array, confirmed against pywowlib's M2 writer --
// NOT indices into the .skin file's own, differently-indirected `bones`
// lookup table). Throws std::runtime_error if any index is out of range
// for `boneCount`.
std::vector<gltf::JointWeights> buildSkinning(const std::vector<m2::Vertex>& vertices, size_t boneCount);

}  // namespace husk::commands
