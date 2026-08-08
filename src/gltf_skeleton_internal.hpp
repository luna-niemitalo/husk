#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

#include <tiny_gltf.h>

#include "gltf_skeleton.hpp"

// writeGlbMulti's (gltf.cpp) skeleton-side phase-functions: input
// validation, joint/skin/extras emission, and animation-clip emission --
// not part of the public husk::gltf API (see gltf_skeleton.hpp for that),
// promoted out of gltf.cpp's own former anonymous namespace so gltf.cpp
// (the orchestrator) can call into gltf_skeleton.cpp, same "cross-file
// helper gets promoted" pattern as gltf_mesh_internal.hpp /
// gltf_buffer_utils.hpp (FILE_SPLIT_TODO.md Item 2).
namespace husk::gltf {

// Throws Error (gltf.hpp) if any joint's `parent` is out of range or
// self-referential. No-op if `skeleton` is null or `!hasSkeleton` (an empty
// joints list).
void validateSkeletonTopology(const Skeleton* skeleton, bool hasSkeleton);

// Throws Error if any CorrectionSet/EmitterAnchor/Attachment/Event/Light/
// PhysicsBody entry's `joint` is out of range for `skeleton->joints`. No-op
// if `skeleton` is null (note: unlike validateSkeletonTopology, this runs
// even when `skeleton->joints` is empty -- mirrors writeGlbMulti's former
// `if (skeleton)`-not-`if (hasSkeleton)` guard exactly, not a behavior
// change).
void validateSkeletonAnchors(const Skeleton* skeleton);

// Throws Error if `animations` is non-empty without a skeleton, if any
// JointAnimation::joint is out of range for `skeleton->joints`, or if any
// joint entry's three time/value pairs have mismatched lengths.
void validateAnimations(const std::vector<Animation>& animations, const Skeleton* skeleton,
                         bool hasSkeleton);

// The skeleton/skin half of writeGlbMulti's former single body: joint
// nodes (with inverse bind matrices), the shared glTF skin (with
// bone_correction_sets/ribbon_emitters/particle_emitters/physics_bodies
// extras), geoset tag joint nodes (Skeleton::GeosetTag,
// GEOSET_MASK_TODO.md), the synthesized multi-root parent node (if
// `skeleton` has more than one root joint), and the Attachment/Event/Light
// placement nodes -- see gltf.hpp's writeGlbMulti doc comment for the
// exact shape/node-index layout this reproduces. Appends inverse-bind-
// matrix data to `buffer`/`views`/`accessors`. Returns a default-
// constructed (empty) result if `!hasSkeleton`.
struct SkeletonEmission {
    int skinIndex = -1;  // -1 if no skeleton; else always 0 (writeGlbMulti has exactly one skin)
    std::optional<tinygltf::Skin> skin;
    std::vector<tinygltf::Node> jointNodes;
    std::vector<int> rootJointNodeIndices;
    // One node per Skeleton::geosetTags entry, appended right after
    // jointNodes (real joint indices 0..jointNodes.size()-1 are never
    // renumbered on account of these) and parented under the single real
    // root joint (single-root skeletons) or the synthesized multi-root
    // parent node (multi-root skeletons) -- either way a real descendant
    // of whatever `skin.skeleton` already is/would be, so the "closest
    // common root" property glTF expects of a skin's joint hierarchy still
    // holds with these added. Also appended to `skin.joints` itself
    // (unlike anchorNodes below) -- see Skeleton::GeosetTag's doc comment
    // for why.
    std::vector<tinygltf::Node> geosetTagNodes;
    // geosetId -> this tag's *skin-relative* joint index (position within
    // `skin.joints`, the same convention JOINTS_0/JOINTS_1 values use --
    // NOT a glTF node index). emitMeshNode (gltf_mesh.cpp) looks up a
    // primitive's skinSectionId here to build JOINTS_1/WEIGHTS_1. Empty
    // when `skeleton->geosetTags` is empty.
    std::unordered_map<int, uint32_t> geosetTagJointIndex;
    bool hasSyntheticRoot = false;
    int syntheticRootNodeIndex = -1;
    tinygltf::Node syntheticRootNode;
    // Attachment/Event/Light nodes, in that order -- appended past the
    // joint-node range, past geosetTagNodes, and past the synthesized
    // multi-root node, if any.
    std::vector<tinygltf::Node> anchorNodes;
};
SkeletonEmission emitSkeletonAndSkin(const Skeleton* skeleton, bool hasSkeleton, size_t meshCount,
                                      tinygltf::Buffer& buffer, std::vector<tinygltf::BufferView>& views,
                                      std::vector<tinygltf::Accessor>& accessors);

// One glTF animation per husk::gltf::Animation -- see Animation's doc
// comment for the sequence_metadata extras this attaches. Appends sampler
// input/output data to `buffer`/`views`/`accessors`. Joint i's channels
// target node (meshCount + i), the same joint-node offset
// emitSkeletonAndSkin's own output uses.
std::vector<tinygltf::Animation> buildAnimationClips(const std::vector<Animation>& animations,
                                                       size_t meshCount, tinygltf::Buffer& buffer,
                                                       std::vector<tinygltf::BufferView>& views,
                                                       std::vector<tinygltf::Accessor>& accessors);

}  // namespace husk::gltf
