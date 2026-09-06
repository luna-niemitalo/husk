// Tests for husk::canon::Physics/boneRef (src/canon_physics.hpp).

#include <doctest/doctest.h>

#include "canon_physics.hpp"

using namespace husk::canon;

TEST_CASE("canon::Physics is phys::File itself -- fields are directly accessible, no re-derivation") {
    Physics p;
    p.version = 3;
    husk::phys::Body body;
    body.boneIndex = 7;
    p.bodies.push_back(body);

    CHECK(p.version == 3);
    CHECK(p.bodies.size() == 1);
    CHECK(p.bodies[0].boneIndex == 7);
}

TEST_CASE("boneRef wraps a bone index as a RecordIndex identity, name/source left at None") {
    Ref ref = boneRef(42);
    REQUIRE(std::holds_alternative<RecordIndex>(ref.id));
    CHECK(std::get<RecordIndex>(ref.id).value == 42);
    CHECK(ref.name.empty());
    CHECK(ref.source == NameSource::None);
}

TEST_CASE("boneRef: bone index 0 is a real bone, not an absent one -- distinct from Identity::None") {
    Ref ref = boneRef(0);
    REQUIRE(std::holds_alternative<RecordIndex>(ref.id));
    CHECK(std::get<RecordIndex>(ref.id).value == 0);
    CHECK_FALSE(std::holds_alternative<None>(ref.id));
}
