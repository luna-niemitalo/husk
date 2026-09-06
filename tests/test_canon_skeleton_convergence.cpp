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

    for (size_t i = 0; i < model.bones.size(); ++i) {
        INFO("joint index ", i);
        const auto& legacyJoint = legacy.joints[i];
        const auto& canonJoint = canonical.joints[i];

        CHECK(canonJoint.parent == legacyJoint.parent);

        // legacyJoint.globalPosition already went through toGltf's
        // zUpToYUp axis swap; canonJoint.globalPosition is raw M2 space
        // (canon_skeleton.hpp's own I1 doc comment) -- apply the same
        // conversion before comparing, or this compares apples to oranges.
        gltf::Vec3 canonAsGltf = gltf::zUpToYUp({canonJoint.globalPosition.x,
                                                  canonJoint.globalPosition.y,
                                                  canonJoint.globalPosition.z});
        CHECK(canonAsGltf.x == doctest::Approx(legacyJoint.globalPosition.x));
        CHECK(canonAsGltf.y == doctest::Approx(legacyJoint.globalPosition.y));
        CHECK(canonAsGltf.z == doctest::Approx(legacyJoint.globalPosition.z));

        CHECK(canonJoint.billboard == gltfBillboardToCanon(legacyJoint.billboardMode));

        // Tier-0 only: legacyJoint.name is empty exactly when
        // canonJoint.ref.source is Synthesized (both use the same
        // m2::keyBoneName lookup as the sole tier-0 name source).
        if (legacyJoint.name.empty()) {
            CHECK(canonJoint.ref.source == canon::NameSource::Synthesized);
            CHECK(canonJoint.ref.name == "bone_" + std::to_string(i));
        } else {
            CHECK(canonJoint.ref.source == canon::NameSource::M2Embedded);
            CHECK(canonJoint.ref.name == legacyJoint.name);
        }

        REQUIRE(std::holds_alternative<canon::RecordIndex>(canonJoint.ref.id));
        CHECK(std::get<canon::RecordIndex>(canonJoint.ref.id).value == i);
    }
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
