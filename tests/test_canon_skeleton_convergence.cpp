// Structural convergence proof for canon::assembleSkeleton
// (canon_skeleton_builder.hpp) against commands::buildSkeleton
// (export_skeleton.hpp), the real production pipeline -- REFACTOR/README.md
// stage 3's gate: same joint inventory, every difference attributed, not
// byte-identity. Both run over the SAME parsed m2::Bone vector from a real
// fixture; per-joint parent/position/billboard/name are compared for every
// bone, not a sample.

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>

#include "canon_skeleton_builder.hpp"
#include "export_skeleton.hpp"
#include "gltf_math.hpp"
#include "m2.hpp"
#include "test_data_paths.hpp"

using namespace husk;

namespace {

canon::BillboardMode gltfBillboardToCanon(const std::string& mode) {
    if (mode.empty()) return canon::BillboardMode::None;
    if (mode == "spherical") return canon::BillboardMode::Spherical;
    if (mode == "cylindrical_lock_x") return canon::BillboardMode::CylindricalLockX;
    if (mode == "cylindrical_lock_y") return canon::BillboardMode::CylindricalLockY;
    if (mode == "cylindrical_lock_z") return canon::BillboardMode::CylindricalLockZ;
    FAIL("unrecognized gltf billboard mode string: ", mode);
    return canon::BillboardMode::None;
}

}  // namespace

TEST_CASE("canon::assembleSkeleton converges with commands::buildSkeleton on a real fixture, "
          "joint by joint" *
          doctest::skip(test::testM2().empty())) {
    std::ifstream mf(test::testM2(), std::ios::binary);
    REQUIRE(mf.good());
    std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(mf)),
                                    std::istreambuf_iterator<char>());
    auto model = m2::loadModel(fileBytes);
    REQUIRE(!model.bones.empty());

    gltf::Skeleton legacy = commands::buildSkeleton(model.bones);
    canon::Skeleton canonical = canon::assembleSkeleton(model.bones);

    REQUIRE(legacy.joints.size() == model.bones.size());
    REQUIRE(canonical.joints.size() == model.bones.size());

    // One CHECK per kind of fact verified, not one per joint -- a real
    // skeleton runs to hundreds of joints, and asserting per-joint-per-field
    // multiplies this test's assertion count by that corpus size for no
    // added coverage (every joint is still checked; only the assertion
    // *count* changes). On a mismatch, INFO below names the first offending
    // joint and both its legacy/canon values, same aggregate-then-diagnose
    // discipline test_canon_animation_convergence.cpp's own findBestInlinePick
    // and test_writers_bundle.cpp's BufferSlice check already establish.
    std::optional<size_t> parentMismatch, positionMismatch, billboardMismatch, nameMismatch, idMismatch;
    for (size_t i = 0; i < model.bones.size(); ++i) {
        const auto& legacyJoint = legacy.joints[i];
        const auto& canonJoint = canonical.joints[i];

        if (canonJoint.parent != legacyJoint.parent && !parentMismatch) parentMismatch = i;

        // legacyJoint.globalPosition already went through toGltf's
        // zUpToYUp axis swap; canonJoint.globalPosition is raw M2 space
        // (canon_skeleton.hpp's own I1 doc comment) -- apply the same
        // conversion before comparing, or this compares apples to oranges.
        gltf::Vec3 canonAsGltf = gltf::zUpToYUp({canonJoint.globalPosition.x,
                                                  canonJoint.globalPosition.y,
                                                  canonJoint.globalPosition.z});
        bool posMatches = canonAsGltf.x == doctest::Approx(legacyJoint.globalPosition.x) &&
                           canonAsGltf.y == doctest::Approx(legacyJoint.globalPosition.y) &&
                           canonAsGltf.z == doctest::Approx(legacyJoint.globalPosition.z);
        if (!posMatches && !positionMismatch) positionMismatch = i;

        if (canonJoint.billboard != gltfBillboardToCanon(legacyJoint.billboardMode) && !billboardMismatch) {
            billboardMismatch = i;
        }

        // Tier-0 only: legacyJoint.name is empty exactly when
        // canonJoint.ref.source is Synthesized (both use the same
        // m2::keyBoneName lookup as the sole tier-0 name source).
        bool nameMatches = legacyJoint.name.empty()
                                ? (canonJoint.ref.source == canon::NameSource::Synthesized &&
                                   canonJoint.ref.name == "bone_" + std::to_string(i))
                                : (canonJoint.ref.source == canon::NameSource::M2Embedded &&
                                   canonJoint.ref.name == legacyJoint.name);
        if (!nameMatches && !nameMismatch) nameMismatch = i;

        bool idMatches = std::holds_alternative<canon::RecordIndex>(canonJoint.ref.id) &&
                          std::get<canon::RecordIndex>(canonJoint.ref.id).value == i;
        if (!idMatches && !idMismatch) idMismatch = i;
    }
    INFO("first joint with a mismatched parent index (if any): ", parentMismatch.value_or(-1));
    CHECK(!parentMismatch.has_value());
    INFO("first joint with a mismatched bind position (if any): ", positionMismatch.value_or(-1));
    CHECK(!positionMismatch.has_value());
    INFO("first joint with a mismatched billboard mode (if any): ", billboardMismatch.value_or(-1));
    CHECK(!billboardMismatch.has_value());
    INFO("first joint with a mismatched name/source (if any): ", nameMismatch.value_or(-1));
    CHECK(!nameMismatch.has_value());
    INFO("first joint with a mismatched ref.id (if any): ", idMismatch.value_or(-1));
    CHECK(!idMismatch.has_value());
}

TEST_CASE("canon::assembleSkeleton throws on an out-of-range parent index, same as "
          "commands::buildSkeleton") {
    std::vector<m2::Bone> bones(2);
    bones[0].parentBone = -1;
    bones[1].parentBone = 5;  // out of range for a 2-bone array

    CHECK_THROWS_AS(canon::assembleSkeleton(bones), std::runtime_error);
    CHECK_THROWS_AS(commands::buildSkeleton(bones), std::runtime_error);
}

TEST_CASE("canon::assembleSkeleton throws on a cyclic parent chain, same as "
          "commands::buildSkeleton") {
    std::vector<m2::Bone> bones(3);
    bones[0].parentBone = 1;
    bones[1].parentBone = 2;
    bones[2].parentBone = 0;  // cycle: 0 -> 1 -> 2 -> 0

    CHECK_THROWS_AS(canon::assembleSkeleton(bones), std::runtime_error);
    CHECK_THROWS_AS(commands::buildSkeleton(bones), std::runtime_error);
}
