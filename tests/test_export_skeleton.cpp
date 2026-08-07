// buildSkeleton's bone-naming tiers: tier 0 (m2::keyBoneId -> a real
// wowdev.wiki name) is exercised elsewhere via real fixtures; this file
// covers tier 1 (BONE_NAME_DEDUCTION_TODO.md's local-topology deduction)
// with small, synthetic bone chains -- real M2 data isn't needed to prove
// the chain-interpolation/branch-detection logic itself is correct.

#include <doctest/doctest.h>

#include "../src/export_skeleton.hpp"

namespace {

using husk::m2::Bone;

Bone bone(int16_t parent, int32_t keyBoneId = -1) {
    Bone b;
    b.parentBone = parent;
    b.keyBoneId = keyBoneId;
    return b;
}

}  // namespace

TEST_CASE("buildSkeleton: tier 1 labels a single unnamed bone directly between two named ones") {
    // 0: root (ForearmL, keyBoneId 81) -> 1: unnamed -> 2: HandL (keyBoneId 65)
    std::vector<Bone> bones = {bone(-1, 81), bone(0), bone(1, 65)};
    auto skeleton = husk::commands::buildSkeleton(bones);

    REQUIRE(skeleton.joints.size() == 3);
    CHECK(skeleton.joints[0].name == "ForearmL");
    CHECK(skeleton.joints[1].name == "bone_1_betweenForearmL_HandL");
    CHECK(skeleton.joints[2].name == "HandL");
}

TEST_CASE("buildSkeleton: tier 1 labels every bone in a longer simple chain") {
    // ForearmR(81->80 is ForearmR) -> unnamed -> unnamed -> HandR(64)
    std::vector<Bone> bones = {bone(-1, 80), bone(0), bone(1), bone(2, 64)};
    auto skeleton = husk::commands::buildSkeleton(bones);

    CHECK(skeleton.joints[1].name == "bone_1_betweenForearmR_HandR");
    CHECK(skeleton.joints[2].name == "bone_2_betweenForearmR_HandR");
}

TEST_CASE("buildSkeleton: tier 1 leaves a branch point unlabeled") {
    // 0: ForearmL(81) -> 1: unnamed, branches into 2 and 3 -> neither named.
    std::vector<Bone> bones = {bone(-1, 81), bone(0), bone(1), bone(1)};
    auto skeleton = husk::commands::buildSkeleton(bones);

    CHECK(skeleton.joints[1].name.empty());
    CHECK(skeleton.joints[2].name.empty());
    CHECK(skeleton.joints[3].name.empty());
}

TEST_CASE("buildSkeleton: tier 1 leaves a dead-end chain (no named descendant) unlabeled") {
    std::vector<Bone> bones = {bone(-1, 81), bone(0), bone(1)};  // ForearmL -> unnamed -> unnamed
    auto skeleton = husk::commands::buildSkeleton(bones);

    CHECK(skeleton.joints[1].name.empty());
    CHECK(skeleton.joints[2].name.empty());
}

TEST_CASE("buildSkeleton: tier 1 never overwrites a real keyBoneId name") {
    std::vector<Bone> bones = {bone(-1, 81), bone(0, 65)};  // ForearmL -> HandL, no gap
    auto skeleton = husk::commands::buildSkeleton(bones);

    CHECK(skeleton.joints[0].name == "ForearmL");
    CHECK(skeleton.joints[1].name == "HandL");
}

TEST_CASE("buildSkeleton: a bone with no named ancestor at all stays unlabeled") {
    std::vector<Bone> bones = {bone(-1), bone(0), bone(1, 65)};  // unnamed root -> unnamed -> HandL
    auto skeleton = husk::commands::buildSkeleton(bones);

    CHECK(skeleton.joints[0].name.empty());
    CHECK(skeleton.joints[1].name.empty());  // no named *ancestor* -- HandL is a descendant only
    CHECK(skeleton.joints[2].name == "HandL");
}
