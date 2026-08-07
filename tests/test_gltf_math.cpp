// Tests for husk::gltf's math module (src/gltf_math.hpp/.cpp): Vec3/Vec2/
// Quat, zUpToYUp/rotationZUpToYUp/scaleZUpToYUp.
// Split out of the former tests/test_gltf.cpp -- see FILE_SPLIT_TODO.md
// Item 5.

#include <cmath>
#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "../src/gltf.hpp"

// (X, Z, -Y) is the corrected WoW Z-up -> glTF Y-up formula: independently
// confirmed against reference/wow.export's own (differently-derived)
// conversion code, and empirically confirmed to produce a right-side-up
// import via a real headless-Blender check (see the synthetic
// coordinate-frame probe in tests/test_conformance.cpp, which exercises
// this through the real Blender import path, not just this function in
// isolation).
// TODO: Remove: TRANSFORM_TRIAGE.md -- the formula this test used to
// assert before the fix ((X, -Z, Y), per wowdev.wiki's literal text)
// composed with Blender's own import conversion into a net 180-degree
// flip, not the identity a correct round trip requires (confirmed via a
// real headless-Blender import landing a head-height landmark bone below a feet-height one).
TEST_CASE("zUpToYUp: (X, Y, Z) becomes (X, Z, -Y) -- the corrected formula") {
    husk::gltf::Vec3 in{1, 2, 3};
    auto out = husk::gltf::zUpToYUp(in);
    CHECK(out.x == doctest::Approx(1));
    CHECK(out.y == doctest::Approx(3));
    CHECK(out.z == doctest::Approx(-2));
}

TEST_CASE("scaleZUpToYUp: (X, Y, Z) becomes (X, Z, Y) -- unsigned, unaffected by the "
          "position-formula's sign convention") {
    husk::gltf::Vec3 in{1, 2, 3};
    auto out = husk::gltf::scaleZUpToYUp(in);
    CHECK(out.x == doctest::Approx(1));
    CHECK(out.y == doctest::Approx(3));
    CHECK(out.z == doctest::Approx(2));
}

TEST_CASE("rotationZUpToYUp: identity rotation maps to identity rotation") {
    husk::gltf::Quat identity{0, 0, 0, 1};
    auto out = husk::gltf::rotationZUpToYUp(identity);
    CHECK(out.x == doctest::Approx(0));
    CHECK(out.y == doctest::Approx(0));
    CHECK(out.z == doctest::Approx(0));
    CHECK(std::abs(out.w) == doctest::Approx(1));
}

namespace {

// Rotates `v` by quaternion `q` (standard active-rotation formula,
// independent of husk's own gltf.cpp internals -- this is the test's own
// ground truth, not a call into the code under test).
husk::gltf::Vec3 rotateVec(const husk::gltf::Quat& q, const husk::gltf::Vec3& v) {
    husk::gltf::Vec3 qv{q.x, q.y, q.z};
    auto cross = [](const husk::gltf::Vec3& a, const husk::gltf::Vec3& b) {
        return husk::gltf::Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    auto add = [](const husk::gltf::Vec3& a, const husk::gltf::Vec3& b) {
        return husk::gltf::Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
    };
    auto scale = [](const husk::gltf::Vec3& a, float s) { return husk::gltf::Vec3{a.x * s, a.y * s, a.z * s}; };
    husk::gltf::Vec3 t = scale(cross(qv, v), 2.0f);
    return add(add(v, scale(t, q.w)), cross(qv, t));
}

bool approxEqual(const husk::gltf::Vec3& a, const husk::gltf::Vec3& b, float eps = 1e-4f) {
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps && std::abs(a.z - b.z) < eps;
}

}  // namespace

// This test exists to guarantee a property, not a specific value:
// "mechanically derive rotation from the same matrix as position, don't
// hand-derive it separately" -- rotating a vector by q, then converting to
// glTF space, must equal converting the vector to glTF space first and
// then rotating by the converted quaternion. This isn't a claim about
// which concrete matrix is "correct" for WoW (that's zUpToYUp's own
// literal-value test, cross-checked against wow.export and a real Blender
// import elsewhere) -- it's a check that rotationZUpToYUp's own
// implementation (quaternion -> matrix -> conjugate -> quaternion) is
// internally consistent with zUpToYUp for *any* rotation, catching an
// implementation bug in the conversion machinery itself (a sign error in
// the matrix<->quat round trip, say) independently of which underlying
// WoW->glTF matrix is in use.
// TODO: Remove: TRANSFORM_TRIAGE.md §5a citation for this design's origin.
TEST_CASE("rotationZUpToYUp: converting-then-rotating equals rotating-then-converting, for "
          "several real test rotations") {
    struct Case {
        const char* name;
        husk::gltf::Quat q;
    };
    // sqrt(2)/2, for 90-degree rotations about a single axis.
    constexpr float kHalfSqrt2 = 0.70710678f;
    std::vector<Case> cases = {
        {"90 deg about X", {kHalfSqrt2, 0, 0, kHalfSqrt2}},
        {"90 deg about Y", {0, kHalfSqrt2, 0, kHalfSqrt2}},
        {"90 deg about Z", {0, 0, kHalfSqrt2, kHalfSqrt2}},
        {"180 deg about X", {1, 0, 0, 0}},
        {"180 deg about (1,1,1)/sqrt(3)", {0.5773503f, 0.5773503f, 0.5773503f, 0}},
        // An arbitrary, non-axis-aligned rotation -- axis (1,2,-1)
        // normalized, angle ~73.4 degrees -- so this isn't only exercising
        // the tidy 90/180-degree cases above.
        {"arbitrary", {0.2837400f, 0.5674800f, -0.2837400f, 0.7325400f}},
    };
    std::vector<husk::gltf::Vec3> probeVectors = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 2, 3}, {-2, 1, 0.5f}};

    for (const auto& c : cases) {
        std::string name = c.name;
        CAPTURE(name);
        // Both quatToMat3 (the conversion under test) and rotateVec (this
        // test's own independent ground truth) assume a unit quaternion --
        // a real invariant every M2 bone-rotation keyframe already
        // satisfies (see m2::readCompQuat), but not something a hand-typed
        // literal in a test case is guaranteed to hit exactly. Normalize
        // here rather than trust each literal's own precision, so a
        // slightly-off "arbitrary" test case can't manufacture a spurious
        // failure that looks like a real bug in the conversion.
        husk::gltf::Quat q = c.q;
        float mag = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        q.x /= mag;
        q.y /= mag;
        q.z /= mag;
        q.w /= mag;
        auto qConverted = husk::gltf::rotationZUpToYUp(q);
        int vecIndex = 0;
        for (const auto& v : probeVectors) {
            CAPTURE(vecIndex);
            husk::gltf::Vec3 rotateThenConvert = husk::gltf::zUpToYUp(rotateVec(q, v));
            husk::gltf::Vec3 convertThenRotate = rotateVec(qConverted, husk::gltf::zUpToYUp(v));
            INFO("rotateThenConvert = (", rotateThenConvert.x, ",", rotateThenConvert.y, ",", rotateThenConvert.z,
                 "), convertThenRotate = (", convertThenRotate.x, ",", convertThenRotate.y, ",",
                 convertThenRotate.z, ")");
            CHECK(approxEqual(rotateThenConvert, convertThenRotate));
            ++vecIndex;
        }
    }
}
