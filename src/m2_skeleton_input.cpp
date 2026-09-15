#include "m2_skeleton_input.hpp"

#include <stdexcept>
#include <string>

#include "canon_bone_naming.hpp"  // computeStructuralLabels
#include "m2_header.hpp"          // billboardModeName, keyBoneName

namespace husk::m2input {

namespace {

canon::BillboardMode toBillboardMode(uint32_t flags) {
    const char* name = m2::billboardModeName(flags);
    if (name == nullptr) return canon::BillboardMode::None;
    if (std::string(name) == "spherical") return canon::BillboardMode::Spherical;
    if (std::string(name) == "cylindrical_lock_x") return canon::BillboardMode::CylindricalLockX;
    if (std::string(name) == "cylindrical_lock_y") return canon::BillboardMode::CylindricalLockY;
    if (std::string(name) == "cylindrical_lock_z") return canon::BillboardMode::CylindricalLockZ;
    throw std::runtime_error("m2::billboardModeName returned an unrecognized mode: " +
                              std::string(name));
}

// Mirrors export_skeleton.cpp's checkNoBoneCycles exactly (memoized
// parent-chain walk, O(joints)) -- that function is private to its own
// translation unit, so this is a reimplementation, not a shared call.
void checkNoBoneCycles(const std::vector<canon::Joint>& joints) {
    enum class State { kUnvisited, kInProgress, kDone };
    std::vector<State> state(joints.size(), State::kUnvisited);

    for (size_t start = 0; start < joints.size(); ++start) {
        if (state[start] == State::kDone) continue;

        std::vector<size_t> path;
        size_t cur = start;
        while (true) {
            if (state[cur] == State::kDone) break;
            if (state[cur] == State::kInProgress) {
                throw std::runtime_error(
                    "bone " + std::to_string(cur) +
                    "'s parent chain loops back on itself -- not a valid bind-pose skeleton "
                    "(wrong .skel paired with this model?)");
            }
            state[cur] = State::kInProgress;
            path.push_back(cur);
            int parent = joints[cur].parent;
            if (parent == -1) break;
            cur = static_cast<size_t>(parent);
        }
        for (size_t idx : path) state[idx] = State::kDone;
    }
}

}  // namespace

canon::Skeleton assembleSkeleton(const std::vector<m2::Bone>& bones) {
    canon::Skeleton skeleton;
    skeleton.joints.reserve(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        const auto& b = bones[i];
        canon::Joint j;
        j.parent = b.parentBone;
        j.globalPosition = b.pivot;
        j.billboard = toBillboardMode(b.flags);
        if (const char* name = m2::keyBoneName(b.keyBoneId)) {
            j.ref = canon::boneRef(static_cast<uint32_t>(i), name, canon::NameSource::M2Embedded);
        } else {
            j.ref = canon::boneRef(static_cast<uint32_t>(i), "bone_" + std::to_string(i),
                                    canon::NameSource::Synthesized);
        }
        skeleton.joints.push_back(j);
    }

    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        int parent = skeleton.joints[i].parent;
        if (parent == -1) continue;
        if (parent < 0 || static_cast<size_t>(parent) >= skeleton.joints.size()) {
            throw std::runtime_error("bone " + std::to_string(i) + "'s parent (" +
                                      std::to_string(parent) + ") is out of range for " +
                                      std::to_string(skeleton.joints.size()) + " bones");
        }
    }
    checkNoBoneCycles(skeleton.joints);

    // Structural labeling needs a validated, cycle-free forest -- runs
    // only after checkNoBoneCycles confirms one.
    std::vector<std::string> labels = canon::computeStructuralLabels(skeleton);
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        skeleton.joints[i].structuralLabel = labels[i];
    }
    return skeleton;
}

}  // namespace husk::m2input
