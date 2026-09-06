// Tests for husk::canon::Geoset (src/canon_geoset.hpp).

#include <doctest/doctest.h>

#include "canon_geoset.hpp"

using namespace husk::canon;

TEST_CASE("Geoset::fromRawId decomposes group/variant the same way gltf_mesh.cpp/gltf_skeleton.cpp do") {
    // 301 -> group 3, variant 1: a real hairstyle-family geoset shape
    // (hundreds digit = group, remainder = variant), same id/100,id%100
    // split as gltf_mesh.cpp's geoset_group/geoset_variant extras and
    // gltf_skeleton.cpp's tag-joint naming.
    Geoset g = Geoset::fromRawId(301);
    CHECK(g.group == 3);
    CHECK(g.variant == 1);
    CHECK(std::holds_alternative<RecordIndex>(g.ref.id));
    CHECK(std::get<RecordIndex>(g.ref.id).value == 301);
}

TEST_CASE("Geoset::fromRawId: variant 0 (bare group id, e.g. 0 or 100)") {
    Geoset zero = Geoset::fromRawId(0);
    CHECK(zero.group == 0);
    CHECK(zero.variant == 0);

    Geoset hundred = Geoset::fromRawId(100);
    CHECK(hundred.group == 1);
    CHECK(hundred.variant == 0);
}

TEST_CASE("Geoset::fromRawId: variant beyond two digits (e.g. skinSectionId 1301 -> group 13, variant 1)") {
    Geoset g = Geoset::fromRawId(1301);
    CHECK(g.group == 13);
    CHECK(g.variant == 1);
}

TEST_CASE("Geoset: name/source default to None/empty -- fromRawId has no real name to give") {
    Geoset g = Geoset::fromRawId(501);
    CHECK(g.ref.name.empty());
    CHECK(g.ref.source == NameSource::None);
}
