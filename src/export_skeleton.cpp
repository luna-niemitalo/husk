#include "export_skeleton.hpp"

#include <stdexcept>
#include <string>

#include "export_transform.hpp"

namespace husk::commands {

namespace {

// Detects a cycle in the bones' parent chains (bone A's parent is B, B's
// parent is A, or any longer loop). No single parentBone bounds check can
// catch this -- every individual index in a cycle is perfectly in-range --
// so this walks each joint's parent chain separately, memoizing finished
// (acyclic) nodes so the whole pass stays
// O(joints) instead of O(joints^2). A real (not just hand-crafted) way to
// hit this: a .skel file that doesn't actually belong to the M2 it's
// passed alongside -- the same mismatch category the model/.skin
// vertex-count cross-check exists to catch, just for bones instead of
// vertices. Assumes every joint's `parent` is already bounds-checked
// (either -1 or a valid index) -- callers must validate that first.
void checkNoBoneCycles(const std::vector<gltf::Skeleton::Joint>& joints) {
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

// Tier 1 of TODO/BONE_NAME_DEDUCTION_TODO.md's bone-naming scheme: for a bone
// with no real keyBoneId name (tier 0), borrow one *only* in the narrow,
// unambiguous case of a simple (non-branching) run of unnamed bones
// directly between one already-named ancestor and one already-named
// descendant -- e.g. an unnamed wrist bone directly between a named
// "ForearmL" and a named "HandL" becomes "bone_<i>_betweenForearmL_HandL".
// Deliberately structural, not anatomical: this never invents vocabulary
// (Wrist/Elbow/Knee/...) that isn't actually in the data, only says where
// the bone sits relative to two real key-bone names. A branch (more than
// one child) anywhere in the run, or no named descendant at all, leaves
// every bone on that path unlabeled -- they fall through to tier 2 (not yet
// implemented, see TODO/BONE_NAME_DEDUCTION_TODO.md) or the plain
// `bone_<index>` fallback (gltf_skeleton.cpp), never a guess.
//
// Reads a frozen snapshot of tier 0's own names (`tier0Name`/`hasTier0`),
// never a name this function itself just assigned -- a two-pass algorithm
// borrowing its own tier-1 output as a landmark for a different chain would
// let one deduced (not real) name propagate into another, compounding
// uncertainty instead of bounding it.
void deduceBoneNamesByTopology(std::vector<gltf::Skeleton::Joint>& joints) {
    std::vector<std::string> tier0Name(joints.size());
    std::vector<bool> hasTier0(joints.size(), false);
    for (size_t i = 0; i < joints.size(); ++i) {
        if (!joints[i].name.empty()) {
            hasTier0[i] = true;
            tier0Name[i] = joints[i].name;
        }
    }

    std::vector<std::vector<size_t>> children(joints.size());
    for (size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].parent != -1) children[static_cast<size_t>(joints[i].parent)].push_back(i);
    }

    for (size_t ancestor = 0; ancestor < joints.size(); ++ancestor) {
        if (!hasTier0[ancestor]) continue;
        for (size_t start : children[ancestor]) {
            std::vector<size_t> path;
            size_t cur = start;
            while (true) {
                if (hasTier0[cur]) {
                    for (size_t idx : path) {
                        joints[idx].name = "bone_" + std::to_string(idx) + "_between" +
                                            tier0Name[ancestor] + "_" + tier0Name[cur];
                    }
                    break;
                }
                if (children[cur].size() != 1) break;  // branch or dead end -- ambiguous, skip
                path.push_back(cur);
                cur = children[cur][0];
            }
        }
    }
}

}  // namespace

gltf::Skeleton buildSkeleton(const std::vector<m2::Bone>& bones) {
    gltf::Skeleton skeleton;
    skeleton.joints.reserve(bones.size());
    for (const auto& b : bones) {
        gltf::Skeleton::Joint j;
        j.parent = b.parentBone;
        j.globalPosition = toGltf(b.pivot);
        if (const char* mode = m2::billboardModeName(b.flags)) {
            j.billboardMode = mode;
        }
        if (const char* name = m2::keyBoneName(b.keyBoneId)) {
            j.name = name;
        }
        skeleton.joints.push_back(j);
    }

    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        auto& j = skeleton.joints[i];
        if (j.parent == -1) {
            j.localTranslation = j.globalPosition;
            continue;
        }
        if (j.parent < 0 || static_cast<size_t>(j.parent) >= skeleton.joints.size()) {
            throw std::runtime_error("bone " + std::to_string(i) + "'s parent (" +
                                      std::to_string(j.parent) + ") is out of range for " +
                                      std::to_string(skeleton.joints.size()) + " bones");
        }
        const gltf::Vec3& parentPos = skeleton.joints[static_cast<size_t>(j.parent)].globalPosition;
        j.localTranslation = {j.globalPosition.x - parentPos.x, j.globalPosition.y - parentPos.y,
                               j.globalPosition.z - parentPos.z};
    }
    checkNoBoneCycles(skeleton.joints);
    return skeleton;
}

void applyContextualBoneNames(gltf::Skeleton& skeleton) {
    for (const auto& a : skeleton.attachments) {
        if (a.joint < 0 || static_cast<size_t>(a.joint) >= skeleton.joints.size()) continue;
        auto& j = skeleton.joints[static_cast<size_t>(a.joint)];
        if (j.name.empty()) {
            if (const char* name = m2::attachmentTypeName(a.id)) j.name = name;
        }
    }
    for (const auto& e : skeleton.events) {
        if (e.joint < 0 || static_cast<size_t>(e.joint) >= skeleton.joints.size()) continue;
        auto& j = skeleton.joints[static_cast<size_t>(e.joint)];
        if (j.name.empty()) {
            if (const char* name = m2::eventName(e.identifier)) j.name = name;
        }
    }
    // Runs last, once every parent index is confirmed in-range and acyclic
    // (buildSkeleton's own checks, already run by the time this is called)
    // -- deduceBoneNamesByTopology indexes `children` by `parent` directly
    // and would need its own redundant bounds check otherwise. Also
    // deliberately last so it can use the attachment/event names just
    // assigned above as landmarks, not just real keyBoneId ones.
    deduceBoneNamesByTopology(skeleton.joints);
}

std::vector<gltf::JointWeights> buildSkinning(const std::vector<m2::Vertex>& vertices,
                                               size_t boneCount) {
    std::vector<gltf::JointWeights> skinning;
    skinning.reserve(vertices.size());
    for (size_t vi = 0; vi < vertices.size(); ++vi) {
        const auto& v = vertices[vi];
        gltf::JointWeights jw;
        for (int j = 0; j < 4; ++j) {
            if (v.boneIndices[j] >= boneCount) {
                throw std::runtime_error("vertex " + std::to_string(vi) + "'s bone_indices[" +
                                          std::to_string(j) + "] (" +
                                          std::to_string(v.boneIndices[j]) +
                                          ") is out of range for " + std::to_string(boneCount) +
                                          " bones");
            }
            jw.joints[j] = v.boneIndices[j];
            jw.weights[j] = static_cast<float>(v.boneWeights[j]) / 255.0f;
        }
        skinning.push_back(jw);
    }
    return skinning;
}

}  // namespace husk::commands
