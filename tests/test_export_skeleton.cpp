// buildSkeleton's bone-naming tiers: tier 0 (m2::keyBoneId -> a real
// wowdev.wiki name) is exercised elsewhere via real fixtures; this file
// covers everything applyContextualBoneNames adds on top -- the
// attachment-id tier, the event-identifier tier, and tier 1's local-
// topology deduction (BONE_NAME_DEDUCTION_TODO.md) -- with small, synthetic
// bone chains. Real M2 data isn't needed to prove this logic is correct.

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

husk::gltf::Skeleton named(std::vector<Bone> bones) {
    auto skeleton = husk::commands::buildSkeleton(bones);
    husk::commands::applyContextualBoneNames(skeleton);
    return skeleton;
}

}  // namespace

TEST_CASE("applyContextualBoneNames: tier 1 labels a single unnamed bone directly between two "
          "named ones") {
    // 0: root (ForearmL, keyBoneId 81) -> 1: unnamed -> 2: HandL (keyBoneId 65)
    auto skeleton = named({bone(-1, 81), bone(0), bone(1, 65)});

    REQUIRE(skeleton.joints.size() == 3);
    CHECK(skeleton.joints[0].name == "ForearmL");
    CHECK(skeleton.joints[1].name == "bone_1_betweenForearmL_HandL");
    CHECK(skeleton.joints[2].name == "HandL");
}

TEST_CASE("applyContextualBoneNames: tier 1 labels every bone in a longer simple chain") {
    // ForearmR(80) -> unnamed -> unnamed -> HandR(64)
    auto skeleton = named({bone(-1, 80), bone(0), bone(1), bone(2, 64)});

    CHECK(skeleton.joints[1].name == "bone_1_betweenForearmR_HandR");
    CHECK(skeleton.joints[2].name == "bone_2_betweenForearmR_HandR");
}

TEST_CASE("applyContextualBoneNames: tier 1 leaves a branch point unlabeled") {
    // 0: ForearmL(81) -> 1: unnamed, branches into 2 and 3 -> neither named.
    auto skeleton = named({bone(-1, 81), bone(0), bone(1), bone(1)});

    CHECK(skeleton.joints[1].name.empty());
    CHECK(skeleton.joints[2].name.empty());
    CHECK(skeleton.joints[3].name.empty());
}

TEST_CASE("applyContextualBoneNames: tier 1 leaves a dead-end chain (no named descendant) "
          "unlabeled") {
    auto skeleton = named({bone(-1, 81), bone(0), bone(1)});  // ForearmL -> unnamed -> unnamed

    CHECK(skeleton.joints[1].name.empty());
    CHECK(skeleton.joints[2].name.empty());
}

TEST_CASE("applyContextualBoneNames: tier 1 never overwrites a real keyBoneId name") {
    auto skeleton = named({bone(-1, 81), bone(0, 65)});  // ForearmL -> HandL, no gap

    CHECK(skeleton.joints[0].name == "ForearmL");
    CHECK(skeleton.joints[1].name == "HandL");
}

TEST_CASE("applyContextualBoneNames: a bone with no named ancestor at all stays unlabeled") {
    auto skeleton = named({bone(-1), bone(0), bone(1, 65)});  // unnamed root -> unnamed -> HandL

    CHECK(skeleton.joints[0].name.empty());
    CHECK(skeleton.joints[1].name.empty());  // no named *ancestor* -- HandL is a descendant only
    CHECK(skeleton.joints[2].name == "HandL");
}

TEST_CASE("applyContextualBoneNames: an attachment names an otherwise-unnamed bone") {
    auto skeleton = husk::commands::buildSkeleton({bone(-1)});
    skeleton.attachments.push_back({/*id=*/20, /*joint=*/0, {}});  // 20 = Head
    husk::commands::applyContextualBoneNames(skeleton);

    CHECK(skeleton.joints[0].name == "Head");
}

TEST_CASE("applyContextualBoneNames: an attachment never overwrites a real keyBoneId name") {
    auto skeleton = husk::commands::buildSkeleton({bone(-1, 81)});  // ForearmL
    skeleton.attachments.push_back({/*id=*/20, /*joint=*/0, {}});
    husk::commands::applyContextualBoneNames(skeleton);

    CHECK(skeleton.joints[0].name == "ForearmL");
}

TEST_CASE("applyContextualBoneNames: an undocumented attachment id leaves the bone unlabeled") {
    auto skeleton = husk::commands::buildSkeleton({bone(-1)});
    skeleton.attachments.push_back({/*id=*/58, /*joint=*/0, {}});  // real gap in the wiki table
    husk::commands::applyContextualBoneNames(skeleton);

    CHECK(skeleton.joints[0].name.empty());
}

TEST_CASE("applyContextualBoneNames: an event names an otherwise-unnamed bone, lower priority "
          "than attachment") {
    auto skeleton = husk::commands::buildSkeleton({bone(-1), bone(-1)});
    skeleton.events.push_back({"$BTH", /*joint=*/0, {}});
    skeleton.attachments.push_back({/*id=*/20, /*joint=*/1, {}});  // Head
    skeleton.events.push_back({"$BTH", /*joint=*/1, {}});          // should lose to the attachment
    husk::commands::applyContextualBoneNames(skeleton);

    CHECK(skeleton.joints[0].name == "Breath");
    CHECK(skeleton.joints[1].name == "Head");
}

TEST_CASE("applyContextualBoneNames: a bracket-ranged event identifier resolves via its prefix") {
    auto skeleton = husk::commands::buildSkeleton({bone(-1)});
    skeleton.events.push_back({"$FL2", /*joint=*/0, {}});  // "$FL[0-3]" in the wiki table
    husk::commands::applyContextualBoneNames(skeleton);

    CHECK(skeleton.joints[0].name == "FootstepHitLeft");
}

TEST_CASE("applyContextualBoneNames: an undocumented event identifier leaves the bone unlabeled") {
    auto skeleton = husk::commands::buildSkeleton({bone(-1)});
    skeleton.events.push_back({"$CHD", /*joint=*/0, {}});  // wiki: "probably does not exist?!"
    husk::commands::applyContextualBoneNames(skeleton);

    CHECK(skeleton.joints[0].name.empty());
}

TEST_CASE("applyContextualBoneNames: an attachment-derived name is a valid tier-1 landmark") {
    // 0: root, attachment id 20 (Head) -> 1: unnamed -> 2: HandL (keyBoneId 65)
    auto skeleton = husk::commands::buildSkeleton({bone(-1), bone(0), bone(1, 65)});
    skeleton.attachments.push_back({/*id=*/20, /*joint=*/0, {}});
    husk::commands::applyContextualBoneNames(skeleton);

    CHECK(skeleton.joints[0].name == "Head");
    CHECK(skeleton.joints[1].name == "bone_1_betweenHead_HandL");
}
