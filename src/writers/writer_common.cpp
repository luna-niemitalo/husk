#include "writer_common.hpp"

#include <stdexcept>
#include <string>

namespace husk::writers {

m2::Vec3 localBindTranslation(const canon::Skeleton& skeleton, size_t jointIndex) {
    if (jointIndex >= skeleton.joints.size()) {
        throw std::runtime_error("joint index " + std::to_string(jointIndex) +
                                  " is out of range for " + std::to_string(skeleton.joints.size()) +
                                  " joints");
    }
    const canon::Joint& joint = skeleton.joints[jointIndex];
    if (joint.parent == -1) {
        return joint.globalPosition;
    }
    if (joint.parent < 0 || static_cast<size_t>(joint.parent) >= skeleton.joints.size()) {
        throw std::runtime_error("joint " + std::to_string(jointIndex) + "'s parent (" +
                                  std::to_string(joint.parent) + ") is out of range for " +
                                  std::to_string(skeleton.joints.size()) + " joints");
    }
    const m2::Vec3& parentPos = skeleton.joints[static_cast<size_t>(joint.parent)].globalPosition;
    const m2::Vec3& childPos = joint.globalPosition;
    return {childPos.x - parentPos.x, childPos.y - parentPos.y, childPos.z - parentPos.z};
}

std::optional<ComposedJointCurves> composeJointCurves(const canon::Skeleton& skeleton,
                                                       const canon::AnimationClip& clip, size_t jointIndex) {
    if (jointIndex >= clip.boneCurves.size()) {
        throw std::runtime_error("joint index " + std::to_string(jointIndex) +
                                  " is out of range for " + std::to_string(clip.boneCurves.size()) +
                                  " boneCurves entries");
    }
    if (!clip.boneCurves[jointIndex]) {
        return std::nullopt;
    }
    const canon::BoneAnimationCurves& raw = *clip.boneCurves[jointIndex];
    m2::Vec3 bindTranslation = localBindTranslation(skeleton, jointIndex);

    ComposedJointCurves composed;
    composed.translation.sequence = raw.translation.sequence;
    composed.translation.interpolation = raw.translation.interpolation;
    composed.translation.keyframes.reserve(raw.translation.keyframes.size());
    for (const auto& [t, delta] : raw.translation.keyframes) {
        composed.translation.keyframes.emplace_back(
            t, m2::Vec3{bindTranslation.x + delta.x, bindTranslation.y + delta.y, bindTranslation.z + delta.z});
    }
    composed.rotation = raw.rotation;
    composed.scale = raw.scale;
    return composed;
}

}  // namespace husk::writers
