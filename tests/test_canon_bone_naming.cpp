// Tests for husk::canon chain/symmetry bone labeling (src/canon_bone_naming.hpp)
// per DESIGN.md's "canon:: bone naming" section. No existing pipeline to
// converge against (genuinely new behavior) -- checks are internal
// consistency (valid format, no collisions), determinism, and the real-data
// mirror-axis claim.

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <regex>
#include <set>

#include "canon_bone_naming.hpp"
#include "canon_skeleton_builder.hpp"
#include "m2.hpp"
#include "test_data_paths.hpp"

using namespace husk;
using namespace husk::canon;

namespace {

Joint makeJoint(int parent, float x, float y, float z) {
    Joint j;
    j.parent = parent;
    j.globalPosition = {x, y, z};
    return j;
}

// <stem>_<side>_<index>.<subindex> or <stem>_<index>.<subindex> (no side),
// where stem is exactly 3 alphabet chars (canon_bone_stem.hpp) -- doesn't
// match a "_sub_" nested label, which callers check for separately.
const std::regex kTopLevelLabel(R"(^[a-z0-9]{3}(_[LR])?_\d+\.\d+$)");

}  // namespace

TEST_CASE("computeStructuralLabels: mirrored limb pair shares a stem, unmirrored chain doesn't") {
    Skeleton skel;
    // Root on the sagittal plane (y=0), two 3-bone limbs mirrored across
    // y=0, one unmirrored spine/head chain also on the plane.
    skel.joints.push_back(makeJoint(-1, 0.0f, 0.0f, 1.0f));  // 0: root

    skel.joints.push_back(makeJoint(0, 0.1f, 0.5f, 1.0f));   // 1: left limb bone 0
    skel.joints.push_back(makeJoint(1, 0.2f, 0.5f, 0.9f));   // 2: left limb bone 1
    skel.joints.push_back(makeJoint(2, 0.3f, 0.5f, 0.8f));   // 3: left limb bone 2

    skel.joints.push_back(makeJoint(0, 0.1f, -0.5f, 1.0f));  // 4: right limb bone 0
    skel.joints.push_back(makeJoint(4, 0.2f, -0.5f, 0.9f));  // 5: right limb bone 1
    skel.joints.push_back(makeJoint(5, 0.3f, -0.5f, 0.8f));  // 6: right limb bone 2

    skel.joints.push_back(makeJoint(0, 0.0f, 0.0f, 1.5f));   // 7: spine
    skel.joints.push_back(makeJoint(7, 0.0f, 0.0f, 1.8f));   // 8: head

    auto labels = computeStructuralLabels(skel);
    REQUIRE(labels.size() == skel.joints.size());

    for (const auto& l : labels) {
        INFO("label: ", l);
        CHECK(std::regex_match(l, kTopLevelLabel));
    }

    std::set<std::string> unique(labels.begin(), labels.end());
    CHECK(unique.size() == labels.size());

    std::string leftStem = labels[1].substr(0, 3);
    std::string rightStem = labels[4].substr(0, 3);
    CHECK(leftStem == rightStem);
    CHECK(labels[1].find("_L_") != std::string::npos);
    CHECK(labels[4].find("_R_") != std::string::npos);
    CHECK(labels[2].find(leftStem + "_L_1.") == 0);
    CHECK(labels[5].find(rightStem + "_R_1.") == 0);

    // Root has no mirror partner (its own y sits on the plane), so it and
    // the spine/head bones hanging under it (its only non-mirrored
    // children, so they continue root's own chain rather than forking)
    // carry a stem distinct from the mirrored pair's shared one, and no
    // _L_/_R_ segment at all.
    std::string rootStem = labels[0].substr(0, 3);
    CHECK(rootStem != leftStem);
    CHECK(labels[0] == rootStem + "_0.0");  // "<stem>_<index>.<subindex>", no side token
    CHECK(labels[7] == rootStem + "_1.0");
    CHECK(labels[8] == rootStem + "_2.0");
}

TEST_CASE("computeStructuralLabels: a short (<=2 bone) branch gets a bumped subindex, "
          "not a new stem") {
    Skeleton skel;
    skel.joints.push_back(makeJoint(-1, 0.0f, 0.0f, 0.0f));  // 0: root (chain position 0)
    skel.joints.push_back(makeJoint(0, 0.0f, 0.0f, 1.0f));   // 1: chain position 1
    skel.joints.push_back(makeJoint(1, 0.0f, 0.0f, 2.0f));   // 2: chain position 2 (branch point)
    skel.joints.push_back(makeJoint(2, 0.0f, 0.0f, 3.0f));   // 3: main continuation (position 3)
    skel.joints.push_back(makeJoint(3, 0.0f, 0.0f, 4.0f));   // 4: main continuation (position 4)
    skel.joints.push_back(makeJoint(2, 0.1f, 0.0f, 2.1f));   // 5: 1-bone auxiliary branch off node 2

    auto labels = computeStructuralLabels(skel);
    std::set<std::string> unique(labels.begin(), labels.end());
    CHECK(unique.size() == labels.size());

    // Node 2 (branch point, position 2) keeps its own subindex 0; the main
    // chain continues past it to position 3/4 rather than stopping there.
    std::string stem = labels[0].substr(0, 3);
    CHECK(labels[2] == stem + "_2.0");
    CHECK(labels[3] == stem + "_3.0");
    CHECK(labels[4] == stem + "_4.0");
    // The short branch shares node 2's own position (index 2) and stem,
    // just a bumped subindex -- no "_sub_" of its own.
    CHECK(labels[5] == stem + "_2.1");
}

TEST_CASE("computeStructuralLabels: a long (>2 bone) branch is promoted to its own "
          "stem, nested under the branch point") {
    Skeleton skel;
    // Root is the branch point itself: a clearly-larger 4-bone main chain
    // (1,2,3,4) picks it as "main" over the 3-bone branch (5,6,7)
    // unambiguously (no size tie -- DESIGN.md names no tiebreak for that
    // case, so this test avoids needing one).
    skel.joints.push_back(makeJoint(-1, 0.0f, 0.0f, 0.0f));  // 0: root (branch point)
    skel.joints.push_back(makeJoint(0, 0.0f, 0.0f, 1.0f));   // 1: main chain
    skel.joints.push_back(makeJoint(1, 0.0f, 0.0f, 2.0f));   // 2: main chain
    skel.joints.push_back(makeJoint(2, 0.0f, 0.0f, 3.0f));   // 3: main chain
    skel.joints.push_back(makeJoint(3, 0.0f, 0.0f, 4.0f));   // 4: main chain
    // A 3-bone branch off root -- longer than the 2-bone fold threshold.
    skel.joints.push_back(makeJoint(0, 0.1f, 0.0f, 0.1f));   // 5: branch bone 0
    skel.joints.push_back(makeJoint(5, 0.1f, 0.0f, 0.2f));   // 6: branch bone 1
    skel.joints.push_back(makeJoint(6, 0.1f, 0.0f, 0.3f));   // 7: branch bone 2

    auto labels = computeStructuralLabels(skel);
    std::set<std::string> unique(labels.begin(), labels.end());
    CHECK(unique.size() == labels.size());

    std::string rootStem = labels[0].substr(0, 3);
    CHECK(labels[0] == rootStem + "_0.0");
    CHECK(labels[1] == rootStem + "_1.0");
    CHECK(labels[2] == rootStem + "_2.0");
    CHECK(labels[3] == rootStem + "_3.0");
    CHECK(labels[4] == rootStem + "_4.0");

    std::string branchPrefix = labels[0] + "_sub_";
    REQUIRE(labels[5].rfind(branchPrefix, 0) == 0);
    std::string subStem = labels[5].substr(branchPrefix.size(), 3);
    CHECK(labels[5] == branchPrefix + subStem + "_0.0");
    CHECK(labels[6] == branchPrefix + subStem + "_1.0");
    CHECK(labels[7] == branchPrefix + subStem + "_2.0");
    // The promoted sub-chain's own stem is distinct from the root chain's.
    CHECK(subStem != rootStem);
}

TEST_CASE("computeStructuralLabels: deterministic across repeated runs on the same input") {
    Skeleton skel;
    skel.joints.push_back(makeJoint(-1, 0.0f, 0.0f, 0.0f));
    skel.joints.push_back(makeJoint(0, 0.1f, 0.3f, 0.1f));
    skel.joints.push_back(makeJoint(1, 0.2f, 0.3f, 0.2f));
    skel.joints.push_back(makeJoint(0, 0.1f, -0.3f, 0.1f));
    skel.joints.push_back(makeJoint(3, 0.2f, -0.3f, 0.2f));

    auto first = computeStructuralLabels(skel);
    auto second = computeStructuralLabels(skel);
    CHECK(first == second);
}

TEST_CASE("canon::assembleSkeleton: every real bloodelffemale.m2 joint gets a unique "
          "structural label, and its real mirrored key-bone pair shares a stem" *
          doctest::skip(test::testM2().empty())) {
    std::ifstream mf(test::testM2(), std::ios::binary);
    REQUIRE(mf.good());
    std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(mf)),
                                    std::istreambuf_iterator<char>());
    auto model = m2::loadModel(fileBytes);
    REQUIRE(!model.bones.empty());

    Skeleton skeleton = assembleSkeleton(model.bones);
    REQUIRE(skeleton.joints.size() == model.bones.size());

    std::set<std::string> unique;
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        INFO("joint index ", i);
        const std::string& label = skeleton.joints[i].structuralLabel;
        CHECK(!label.empty());
        unique.insert(label);
    }
    CHECK(unique.size() == skeleton.joints.size());

    // bloodelffemale.m2 has no keyBoneId 0/1 (ArmL/ArmR) hit at all -- its
    // real mirrored key-bone pair is ForearmL/ForearmR (keyBoneId 81/80),
    // confirmed via `husk info` before writing this test. Use that real
    // pair instead of assuming 0/1 exists in every fixture.
    int forearmLIdx = -1, forearmRIdx = -1;
    for (size_t i = 0; i < model.bones.size(); ++i) {
        if (model.bones[i].keyBoneId == 81) forearmLIdx = static_cast<int>(i);
        if (model.bones[i].keyBoneId == 80) forearmRIdx = static_cast<int>(i);
    }
    REQUIRE(forearmLIdx != -1);
    REQUIRE(forearmRIdx != -1);

    const std::string& lLabel = skeleton.joints[static_cast<size_t>(forearmLIdx)].structuralLabel;
    const std::string& rLabel = skeleton.joints[static_cast<size_t>(forearmRIdx)].structuralLabel;
    CHECK(lLabel.substr(0, 3) == rLabel.substr(0, 3));
    CHECK(lLabel.find("_L_") != std::string::npos);
    CHECK(rLabel.find("_R_") != std::string::npos);
}
