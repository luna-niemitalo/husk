// Independently-derived correctness tests for husk::writers::
// localBindTranslation/composeJointCurves (src/writers/writer_common.hpp) --
// the shared parent-relative-bind-translation / final-local-transform math
// both planned leaf writers (native bundle, lean glTF) need. Every expected
// value here is computed by hand in the test itself, never by calling the
// function under test for its own expected output.

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>

#include "canon_model.hpp"
#include "m2.hpp"
#include "m2_canon_input.hpp"
#include "skin.hpp"
#include "test_data_paths.hpp"
#include "writers/writer_common.hpp"

using namespace husk;

namespace {

canon::Joint makeJoint(int parent, m2::Vec3 pos) {
    canon::Joint j;
    j.parent = parent;
    j.globalPosition = pos;
    j.ref.name = "j";
    j.ref.source = canon::NameSource::Synthesized;
    return j;
}

}  // namespace

TEST_CASE("writers::localBindTranslation: root joint returns its own absolute position unchanged") {
    canon::Skeleton skeleton;
    skeleton.joints.push_back(makeJoint(-1, {1.0f, 2.0f, 3.0f}));

    m2::Vec3 result = writers::localBindTranslation(skeleton, 0);
    CHECK(result.x == doctest::Approx(1.0f));
    CHECK(result.y == doctest::Approx(2.0f));
    CHECK(result.z == doctest::Approx(3.0f));
}

TEST_CASE("writers::localBindTranslation: 3-joint chain, parent-relative offsets computed by hand") {
    // root at (0,0,0); child at (10,0,0) -- absolute; grandchild at (10,5,-2).
    canon::Skeleton skeleton;
    skeleton.joints.push_back(makeJoint(-1, {0.0f, 0.0f, 0.0f}));
    skeleton.joints.push_back(makeJoint(0, {10.0f, 0.0f, 0.0f}));
    skeleton.joints.push_back(makeJoint(1, {10.0f, 5.0f, -2.0f}));

    m2::Vec3 root = writers::localBindTranslation(skeleton, 0);
    CHECK(root.x == doctest::Approx(0.0f));
    CHECK(root.y == doctest::Approx(0.0f));
    CHECK(root.z == doctest::Approx(0.0f));

    // child relative to root: (10,0,0) - (0,0,0) = (10,0,0)
    m2::Vec3 child = writers::localBindTranslation(skeleton, 1);
    CHECK(child.x == doctest::Approx(10.0f));
    CHECK(child.y == doctest::Approx(0.0f));
    CHECK(child.z == doctest::Approx(0.0f));

    // grandchild relative to child: (10,5,-2) - (10,0,0) = (0,5,-2)
    m2::Vec3 grandchild = writers::localBindTranslation(skeleton, 2);
    CHECK(grandchild.x == doctest::Approx(0.0f));
    CHECK(grandchild.y == doctest::Approx(5.0f));
    CHECK(grandchild.z == doctest::Approx(-2.0f));
}

TEST_CASE("writers::localBindTranslation: throws on out-of-range jointIndex") {
    canon::Skeleton skeleton;
    skeleton.joints.push_back(makeJoint(-1, {0.0f, 0.0f, 0.0f}));

    CHECK_THROWS_AS(writers::localBindTranslation(skeleton, 1), std::runtime_error);
}

TEST_CASE("writers::localBindTranslation: throws on out-of-range parent") {
    canon::Skeleton skeleton;
    skeleton.joints.push_back(makeJoint(5, {0.0f, 0.0f, 0.0f}));  // parent 5 doesn't exist

    CHECK_THROWS_AS(writers::localBindTranslation(skeleton, 0), std::runtime_error);
}

TEST_CASE("writers::composeJointCurves: nullopt passthrough when boneCurves[jointIndex] is nullopt") {
    canon::Skeleton skeleton;
    skeleton.joints.push_back(makeJoint(-1, {0.0f, 0.0f, 0.0f}));

    canon::AnimationClip clip;
    clip.sequence = canon::SequenceRef::sequence(0);
    clip.boneCurves.push_back(std::nullopt);

    CHECK(!writers::composeJointCurves(skeleton, clip, 0).has_value());
}

TEST_CASE("writers::composeJointCurves: throws on out-of-range jointIndex against boneCurves") {
    canon::Skeleton skeleton;
    skeleton.joints.push_back(makeJoint(-1, {0.0f, 0.0f, 0.0f}));

    canon::AnimationClip clip;
    clip.sequence = canon::SequenceRef::sequence(0);
    clip.boneCurves.push_back(std::nullopt);

    CHECK_THROWS_AS(writers::composeJointCurves(skeleton, clip, 1), std::runtime_error);
}

TEST_CASE("writers::composeJointCurves: translation = bind + delta, rotation/scale pass through raw") {
    // 2-joint chain: root at (0,0,0), child at (4,0,0) -- child's bind
    // translation relative to root is (4,0,0), computed by hand.
    canon::Skeleton skeleton;
    skeleton.joints.push_back(makeJoint(-1, {0.0f, 0.0f, 0.0f}));
    skeleton.joints.push_back(makeJoint(0, {4.0f, 0.0f, 0.0f}));

    canon::BoneAnimationCurves raw;
    raw.translation.sequence = canon::SequenceRef::sequence(3);
    raw.translation.interpolation = canon::Interpolation::Linear;
    raw.translation.keyframes = {{0.0f, m2::Vec3{1.0f, 2.0f, 3.0f}}, {1.0f, m2::Vec3{-1.0f, 0.5f, 0.0f}}};

    raw.rotation.sequence = canon::SequenceRef::sequence(3);
    raw.rotation.interpolation = canon::Interpolation::Step;
    raw.rotation.keyframes = {{0.0f, m2::Quat{0.0f, 0.0f, 0.0f, 1.0f}},
                               {1.0f, m2::Quat{0.7071f, 0.0f, 0.0f, 0.7071f}}};

    raw.scale.sequence = canon::SequenceRef::sequence(3);
    raw.scale.interpolation = canon::Interpolation::Linear;
    raw.scale.keyframes = {{0.0f, m2::Vec3{1.0f, 1.0f, 1.0f}}, {1.0f, m2::Vec3{2.0f, 2.0f, 2.0f}}};

    canon::AnimationClip clip;
    clip.sequence = canon::SequenceRef::sequence(3);
    clip.boneCurves.push_back(std::nullopt);  // joint 0, root, no curve data
    clip.boneCurves.push_back(raw);           // joint 1, child

    auto composed = writers::composeJointCurves(skeleton, clip, 1);
    REQUIRE(composed.has_value());

    // Independently computed expected translation: bind (4,0,0) + each raw delta.
    REQUIRE(composed->translation.keyframes.size() == 2);
    CHECK(composed->translation.keyframes[0].second.x == doctest::Approx(5.0f));  // 4 + 1
    CHECK(composed->translation.keyframes[0].second.y == doctest::Approx(2.0f));  // 0 + 2
    CHECK(composed->translation.keyframes[0].second.z == doctest::Approx(3.0f));  // 0 + 3
    CHECK(composed->translation.keyframes[1].second.x == doctest::Approx(3.0f));  // 4 + -1
    CHECK(composed->translation.keyframes[1].second.y == doctest::Approx(0.5f));  // 0 + 0.5
    CHECK(composed->translation.keyframes[1].second.z == doctest::Approx(0.0f));  // 0 + 0

    // Rotation/scale pass through unchanged -- direct comparison against the raw deltas.
    REQUIRE(composed->rotation.keyframes.size() == raw.rotation.keyframes.size());
    for (size_t i = 0; i < raw.rotation.keyframes.size(); ++i) {
        CHECK(composed->rotation.keyframes[i].second.x == doctest::Approx(raw.rotation.keyframes[i].second.x));
        CHECK(composed->rotation.keyframes[i].second.y == doctest::Approx(raw.rotation.keyframes[i].second.y));
        CHECK(composed->rotation.keyframes[i].second.z == doctest::Approx(raw.rotation.keyframes[i].second.z));
        CHECK(composed->rotation.keyframes[i].second.w == doctest::Approx(raw.rotation.keyframes[i].second.w));
    }
    CHECK(composed->rotation.interpolation == raw.rotation.interpolation);

    REQUIRE(composed->scale.keyframes.size() == raw.scale.keyframes.size());
    for (size_t i = 0; i < raw.scale.keyframes.size(); ++i) {
        CHECK(composed->scale.keyframes[i].second.x == doctest::Approx(raw.scale.keyframes[i].second.x));
        CHECK(composed->scale.keyframes[i].second.y == doctest::Approx(raw.scale.keyframes[i].second.y));
        CHECK(composed->scale.keyframes[i].second.z == doctest::Approx(raw.scale.keyframes[i].second.z));
    }
}

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("writers::composeJointCurves converges against independently-recomputed bind+delta on a real "
          "fixture" *
          doctest::skip(test::testM2().empty() || test::testSkin().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testM2()));
    std::vector<uint8_t> skinFile = readFile(test::testSkin());
    skin::Header header = skin::parseHeader(skinFile);
    std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(skinFile, header.submeshes);
    std::vector<skin::Batch> batches = skin::parseBatches(skinFile, header.batches);
    std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(skinFile, header);

    canon::Model canonModel = m2input::buildCanonModel(model, batches, submeshes, triangleIndices);
    REQUIRE(!canonModel.animations.empty());

    // Find some joint with real translation curve data in the first clip --
    // scanned directly, not assumed at a fixed index.
    const canon::AnimationClip& clip = canonModel.animations.front();
    std::optional<size_t> pickedJoint;
    for (size_t bi = 0; bi < clip.boneCurves.size(); ++bi) {
        if (clip.boneCurves[bi] && !clip.boneCurves[bi]->translation.keyframes.empty()) {
            pickedJoint = bi;
            break;
        }
    }
    REQUIRE(pickedJoint.has_value());
    size_t bi = *pickedJoint;

    m2::Vec3 expectedBind = writers::localBindTranslation(canonModel.skeleton, bi);
    const auto& rawKeyframes = clip.boneCurves[bi]->translation.keyframes;
    REQUIRE(!rawKeyframes.empty());
    const auto& [t0, delta0] = rawKeyframes[0];

    auto composed = writers::composeJointCurves(canonModel.skeleton, clip, bi);
    REQUIRE(composed.has_value());
    REQUIRE(!composed->translation.keyframes.empty());
    CHECK(composed->translation.keyframes[0].first == doctest::Approx(t0));
    CHECK(composed->translation.keyframes[0].second.x == doctest::Approx(expectedBind.x + delta0.x));
    CHECK(composed->translation.keyframes[0].second.y == doctest::Approx(expectedBind.y + delta0.y));
    CHECK(composed->translation.keyframes[0].second.z == doctest::Approx(expectedBind.z + delta0.z));
}
