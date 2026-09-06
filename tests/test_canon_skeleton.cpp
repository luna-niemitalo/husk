// Tests for husk::canon::Skeleton/Joint (src/canon_skeleton.hpp).

#include <doctest/doctest.h>

#include "canon_skeleton.hpp"

using namespace husk::canon;

TEST_CASE("Skeleton: root + 2 children hierarchy, parent indices into joints") {
    Skeleton skel;
    Joint root;
    root.parent = -1;
    root.globalPosition = {0.0f, 1.0f, 0.0f};
    root.ref = boneRef(0, "Root", NameSource::M2Embedded);
    skel.joints.push_back(root);

    Joint armL;
    armL.parent = 0;
    armL.globalPosition = {0.5f, 1.2f, 0.0f};
    armL.ref = boneRef(1, "ArmL", NameSource::M2Embedded);
    skel.joints.push_back(armL);

    Joint unnamed;
    unnamed.parent = 0;
    unnamed.globalPosition = {-0.5f, 1.2f, 0.0f};
    unnamed.ref = boneRef(2, "bone_2", NameSource::Synthesized);
    skel.joints.push_back(unnamed);

    REQUIRE(skel.joints.size() == 3);
    CHECK(skel.joints[0].parent == -1);
    CHECK(skel.joints[1].parent == 0);
    CHECK(skel.joints[2].parent == 0);
}

TEST_CASE("Joint: M2Embedded name comes from a real keyBoneName-table hit") {
    Joint j;
    j.ref = boneRef(1, "ArmL", NameSource::M2Embedded);
    CHECK(j.ref.name == "ArmL");
    CHECK(j.ref.source == NameSource::M2Embedded);
    REQUIRE(std::holds_alternative<RecordIndex>(j.ref.id));
    CHECK(std::get<RecordIndex>(j.ref.id).value == 1);
}

TEST_CASE("Joint: Synthesized name is the bone_<index> fallback, not a real table hit") {
    Joint j;
    j.ref = boneRef(42, "bone_42", NameSource::Synthesized);
    CHECK(j.ref.name == "bone_42");
    CHECK(j.ref.source == NameSource::Synthesized);
    CHECK(std::get<RecordIndex>(j.ref.id).value == 42);
}

TEST_CASE("Joint: billboard defaults to None, and carries one of the four real modes when set") {
    Joint plain;
    CHECK(plain.billboard == BillboardMode::None);

    Joint billboarded;
    billboarded.billboard = BillboardMode::Spherical;
    CHECK(billboarded.billboard == BillboardMode::Spherical);
}
