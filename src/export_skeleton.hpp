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

// Lifts M2Vertex's raw bone_weights[4]/bone_indices[4] into glTF's
// JOINTS_0/WEIGHTS_0 shape: weights normalized from 0-255 to 0.0-1.0,
// joint indices copied verbatim (M2Vertex.bone_indices are direct indices
// into the M2's `bones` array, confirmed against pywowlib's M2 writer --
// NOT indices into the .skin file's own, differently-indirected `bones`
// lookup table). Throws std::runtime_error if any index is out of range
// for `boneCount`.
std::vector<gltf::JointWeights> buildSkinning(const std::vector<m2::Vertex>& vertices, size_t boneCount);

}  // namespace husk::commands
