#include "canon_skeleton_builder.hpp"

#include <stdexcept>
#include <string>

#include "m2_header.hpp"  // billboardModeName, keyBoneName

namespace husk::canon {

namespace {

BillboardMode toBillboardMode(uint32_t flags) {
    const char* name = m2::billboardModeName(flags);
    if (name == nullptr) return BillboardMode::None;
    if (std::string(name) == "spherical") return BillboardMode::Spherical;
    if (std::string(name) == "cylindrical_lock_x") return BillboardMode::CylindricalLockX;
    if (std::string(name) == "cylindrical_lock_y") return BillboardMode::CylindricalLockY;
    if (std::string(name) == "cylindrical_lock_z") return BillboardMode::CylindricalLockZ;
    throw std::runtime_error("m2::billboardModeName returned an unrecognized mode: " +
                              std::string(name));
}

// Mirrors export_skeleton.cpp's checkNoBoneCycles exactly (memoized
// parent-chain walk, O(joints)) -- that function is private to its own
// translation unit, so this is a reimplementation, not a shared call.
void checkNoBoneCycles(const std::vector<Joint>& joints) {
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

Skeleton assembleSkeleton(const std::vector<m2::Bone>& bones) {
    Skeleton skeleton;
    skeleton.joints.reserve(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        const auto& b = bones[i];
        Joint j;
        j.parent = b.parentBone;
        j.globalPosition = b.pivot;
        j.billboard = toBillboardMode(b.flags);
        if (const char* name = m2::keyBoneName(b.keyBoneId)) {
            j.ref = boneRef(static_cast<uint32_t>(i), name, NameSource::M2Embedded);
        } else {
            j.ref = boneRef(static_cast<uint32_t>(i), "bone_" + std::to_string(i),
                             NameSource::Synthesized);
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
    return skeleton;
}

}  // namespace husk::canon
