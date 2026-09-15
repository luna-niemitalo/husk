// Tests for husk::canon::Curve/ScalarCurve/VecCurve/QuatCurve (src/canon_curve.hpp).

#include <doctest/doctest.h>

#include "canon_curve.hpp"

using namespace husk::canon;

TEST_CASE("ScalarCurve: sequence-scoped, linear, one keyframe pair") {
    ScalarCurve c;
    c.sequence = SequenceRef::sequence(3);
    c.interpolation = Interpolation::Linear;
    c.keyframes.emplace_back(0.5f, 0.25f);

    CHECK(c.sequence.kind == SequenceRef::Kind::Sequence);
    CHECK(c.sequence.index == 3);
    CHECK(c.interpolation == Interpolation::Linear);
    REQUIRE(c.keyframes.size() == 1);
    CHECK(c.keyframes[0].first == doctest::Approx(0.5f));
    CHECK(c.keyframes[0].second == doctest::Approx(0.25f));
}

TEST_CASE("VecCurve: global-sequence-scoped, step interpolation") {
    VecCurve c;
    c.sequence = SequenceRef::globalSequence(2);
    c.interpolation = Interpolation::Step;
    c.keyframes.emplace_back(0.0f, Vec3{1.0f, 2.0f, 3.0f});

    CHECK(c.sequence.kind == SequenceRef::Kind::GlobalSequence);
    CHECK(c.sequence.index == 2);
    CHECK(c.interpolation == Interpolation::Step);
    REQUIRE(c.keyframes.size() == 1);
    CHECK(c.keyframes[0].second.x == doctest::Approx(1.0f));
    CHECK(c.keyframes[0].second.y == doctest::Approx(2.0f));
    CHECK(c.keyframes[0].second.z == doctest::Approx(3.0f));
}

TEST_CASE("QuatCurve: default is sequence-scoped index 0, linear") {
    QuatCurve c;

    CHECK(c.sequence.kind == SequenceRef::Kind::Sequence);
    CHECK(c.sequence.index == 0);
    CHECK(c.interpolation == Interpolation::Linear);
    CHECK(c.keyframes.empty());
}

TEST_CASE("QuatCurve: multi-keyframe rotation curve, file order preserved") {
    QuatCurve c;
    c.sequence = SequenceRef::sequence(7);
    c.keyframes.emplace_back(0.0f, Quat{0.0f, 0.0f, 0.0f, 1.0f});
    c.keyframes.emplace_back(1.5f, Quat{0.0f, 0.7071f, 0.0f, 0.7071f});

    REQUIRE(c.keyframes.size() == 2);
    CHECK(c.keyframes[0].first == doctest::Approx(0.0f));
    CHECK(c.keyframes[1].first == doctest::Approx(1.5f));
    CHECK(c.keyframes[1].second.w == doctest::Approx(0.7071f));
}
