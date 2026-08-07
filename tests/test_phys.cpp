// .phys IS documented (documentation/wowdev-wiki/md/PHYS.md, wiki_revision
// 30458) -- these fixtures are built directly from that spec, not
// reverse-engineered. Unlike M2's own inline chunks, .phys chunk tags are
// byte-reversed on disk (WMO/ADT convention) -- appendChunk below takes
// the wiki's own, un-reversed tag spelling and reverses it before writing,
// so every test case reads naturally against the wiki text.
// TODO: Remove: verified against 103 real files, see `WIKI_FINDINGS/PHYS.md`.

#include <cstring>
#include <doctest/doctest.h>

#include "../src/phys.hpp"

namespace {

void putU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
}

void putI16(std::vector<uint8_t>& buf, int16_t v) { putU16(buf, static_cast<uint16_t>(v)); }

void putU32(std::vector<uint8_t>& buf, uint32_t v) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

void putI32(std::vector<uint8_t>& buf, int32_t v) { putU32(buf, static_cast<uint32_t>(v)); }

void putF32(std::vector<uint8_t>& buf, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    putU32(buf, bits);
}

void putVec3(std::vector<uint8_t>& buf, float x, float y, float z) {
    putF32(buf, x);
    putF32(buf, y);
    putF32(buf, z);
}

// A 12-float mat3x4, all zero -- field values inside frameA/frameB are
// never inspected by husk::phys (raw pass-through), so tests that need one
// just need the right byte count.
void putZeroMat3x4(std::vector<uint8_t>& buf) {
    for (int i = 0; i < 12; ++i) putF32(buf, 0.0f);
}

// `tag` is given in wowdev.wiki's own, un-reversed spelling (e.g. "PHYS")
// -- reversed here before writing, matching .phys's real on-disk WMO/ADT
// convention (see src/phys.hpp's doc comment).
void appendChunk(std::vector<uint8_t>& buf, const char tag[4], const std::vector<uint8_t>& payload) {
    buf.push_back(tag[3]);
    buf.push_back(tag[2]);
    buf.push_back(tag[1]);
    buf.push_back(tag[0]);
    putU32(buf, static_cast<uint32_t>(payload.size()));
    buf.insert(buf.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> physChunk(int16_t version) {
    std::vector<uint8_t> p;
    putI16(p, version);
    return p;
}

}  // namespace

TEST_CASE("phys::parse: a minimal real-shaped file (BDY4/SHP2/CAPS/JOIN/WLJ2) round-trips every "
          "field") {
    std::vector<uint8_t> file;
    appendChunk(file, "PHYS", physChunk(5));
    std::vector<uint8_t> phyt;
    putU32(phyt, 4);
    appendChunk(file, "PHYT", phyt);

    std::vector<uint8_t> bdy4;
    putU16(bdy4, 1);              // type
    putU16(bdy4, 7);               // boneIndex
    putVec3(bdy4, 1, 2, 3);         // position
    putU16(bdy4, 0);               // shapeIndex (-> shapeBase)
    putU16(bdy4, 0);               // padding
    putI32(bdy4, 1);                // shapesCount
    putF32(bdy4, 0.5f);             // unk0
    putF32(bdy4, 1.0f);             // x1c
    putF32(bdy4, 0.1f);             // drag
    putF32(bdy4, 0.7f);             // unk1
    putF32(bdy4, 0.9f);             // x28
    putU32(bdy4, 0);                // BDY4's trailing 4 unused bytes
    appendChunk(file, "BDY4", bdy4);

    std::vector<uint8_t> shp2;
    putI16(shp2, 1);   // type: capsule
    putI16(shp2, 0);   // index
    putU32(shp2, 0);   // unk[4]
    putF32(shp2, 0.3f);  // friction
    putF32(shp2, 0.4f);  // restitution
    putF32(shp2, 0.5f);  // density
    putU32(shp2, 0);    // SHP2's _x14
    putF32(shp2, 1.0f);  // SHP2's _x18
    putU16(shp2, 0);     // SHP2's _x1c
    putU16(shp2, 0);     // SHP2's _x1e
    appendChunk(file, "SHP2", shp2);

    std::vector<uint8_t> caps;
    putVec3(caps, 0, 0, -1);  // localPosition1
    putVec3(caps, 0, 0, 1);   // localPosition2
    putF32(caps, 0.25f);      // radius
    appendChunk(file, "CAPS", caps);

    std::vector<uint8_t> join;
    putU32(join, 0);   // bodyA
    putU32(join, 0);   // bodyB
    putU32(join, 0);   // unk[4]
    putI16(join, 2);   // type: weld
    putI16(join, 0);   // index
    appendChunk(file, "JOIN", join);

    std::vector<uint8_t> wlj2;
    putZeroMat3x4(wlj2);  // frameA
    putZeroMat3x4(wlj2);  // frameB
    putF32(wlj2, 10.0f);  // angularFrequencyHz
    putF32(wlj2, 0.8f);   // angularDampingRatio
    putF32(wlj2, 5.0f);   // linearFrequencyHz
    putF32(wlj2, 0.6f);   // linearDampingRatio
    appendChunk(file, "WLJ2", wlj2);

    auto f = husk::phys::parse(file);
    CHECK(f.version == 5);
    CHECK(f.phyt == 4);

    REQUIRE(f.bodies.size() == 1);
    CHECK(f.bodies[0].type == 1);
    CHECK(f.bodies[0].boneIndex == 7);
    CHECK(f.bodies[0].position.x == doctest::Approx(1));
    CHECK(f.bodies[0].position.y == doctest::Approx(2));
    CHECK(f.bodies[0].position.z == doctest::Approx(3));
    CHECK(f.bodies[0].shapeBase == 0);
    CHECK(f.bodies[0].shapeCount == 1);
    CHECK(f.bodies[0].unk1 == doctest::Approx(0.7f));

    REQUIRE(f.shapes.size() == 1);
    CHECK(f.shapes[0].type == 1);
    CHECK(f.shapes[0].index == 0);
    CHECK(f.shapes[0].friction == doctest::Approx(0.3f));
    CHECK(f.shapes[0].density == doctest::Approx(0.5f));

    REQUIRE(f.capsules.size() == 1);
    CHECK(f.capsules[0].radius == doctest::Approx(0.25f));
    CHECK(f.capsules[0].localPosition2.z == doctest::Approx(1));

    REQUIRE(f.joints.size() == 1);
    CHECK(f.joints[0].type == 2);
    CHECK(f.joints[0].bodyA == 0);
    CHECK(f.joints[0].bodyB == 0);

    REQUIRE(f.weldJoints.size() == 1);
    CHECK(f.weldJoints[0].angularFrequencyHz == doctest::Approx(10.0f));
    CHECK(f.weldJoints[0].linearDampingRatio == doctest::Approx(0.6f));
}

TEST_CASE("phys::parse: BODY (v0/1, no version-2 field) parses with a real body, no shapes") {
    std::vector<uint8_t> file;
    appendChunk(file, "PHYS", physChunk(1));

    std::vector<uint8_t> body;
    putU16(body, 0);          // type
    putU16(body, 0);          // padding
    putVec3(body, 4, 5, 6);   // position
    putU16(body, 3);          // modelBoneIndex
    putU16(body, 0);          // padding
    putI32(body, 0);          // shapes_base
    putI32(body, 0);          // shapes_count -- 0, so no shapes chunk needed
    appendChunk(file, "BODY", body);

    auto f = husk::phys::parse(file);
    REQUIRE(f.bodies.size() == 1);
    CHECK(f.bodies[0].boneIndex == 3);
    CHECK(f.bodies[0].position.x == doctest::Approx(4));
    CHECK(f.bodies[0].shapeCount == 0);
    CHECK(f.phyt == 0);  // no PHYT chunk -- default
}

TEST_CASE("phys::parse: prefers BDY4 over BODY when both are present (selection order, not real "
          "data -- never observed together in the real corpus)") {
    std::vector<uint8_t> file;
    appendChunk(file, "PHYS", physChunk(5));

    std::vector<uint8_t> body;
    putU16(body, 0);
    putU16(body, 0);
    putVec3(body, 0, 0, 0);
    putU16(body, 111);  // distinguishing boneIndex -- should NOT win
    putU16(body, 0);
    putI32(body, 0);
    putI32(body, 0);
    appendChunk(file, "BODY", body);

    std::vector<uint8_t> bdy4;
    putU16(bdy4, 0);
    putU16(bdy4, 222);  // distinguishing boneIndex -- should win
    putVec3(bdy4, 0, 0, 0);
    putU16(bdy4, 0);
    putU16(bdy4, 0);
    putI32(bdy4, 0);
    putF32(bdy4, 0);
    putF32(bdy4, 1);
    putF32(bdy4, 0);
    putF32(bdy4, 0);
    putF32(bdy4, 0.9f);
    putU32(bdy4, 0);
    appendChunk(file, "BDY4", bdy4);

    auto f = husk::phys::parse(file);
    REQUIRE(f.bodies.size() == 1);
    CHECK(f.bodies[0].boneIndex == 222);
}

TEST_CASE("phys::parse: SHOJ chunk sized as a multiple of only 0x6c (pre-motor variant) parses "
          "without motor fields") {
    std::vector<uint8_t> file;
    appendChunk(file, "PHYS", physChunk(1));
    std::vector<uint8_t> shoj;
    putZeroMat3x4(shoj);
    putZeroMat3x4(shoj);
    putF32(shoj, 1.0f);  // lowerTwistAngle
    putF32(shoj, 2.0f);  // upperTwistAngle
    putF32(shoj, 3.0f);  // coneAngle
    REQUIRE(shoj.size() == 0x6c);
    appendChunk(file, "SHOJ", shoj);

    auto f = husk::phys::parse(file);
    REQUIRE(f.shoulderJoints.size() == 1);
    CHECK(f.shoulderJoints[0].coneAngle == doctest::Approx(3.0f));
    CHECK(f.shoulderJoints[0].maxMotorTorque == doctest::Approx(0.0f));
}

TEST_CASE("phys::parse: SHOJ chunk sized as a multiple of only 0x74 (post-motor variant) parses "
          "the motor fields") {
    std::vector<uint8_t> file;
    appendChunk(file, "PHYS", physChunk(2));
    std::vector<uint8_t> shoj;
    putZeroMat3x4(shoj);
    putZeroMat3x4(shoj);
    putF32(shoj, 1.0f);
    putF32(shoj, 2.0f);
    putF32(shoj, 3.0f);
    putF32(shoj, 42.0f);  // maxMotorTorque
    putU32(shoj, 7);      // motorMode
    REQUIRE(shoj.size() == 0x74);
    appendChunk(file, "SHOJ", shoj);

    auto f = husk::phys::parse(file);
    REQUIRE(f.shoulderJoints.size() == 1);
    CHECK(f.shoulderJoints[0].maxMotorTorque == doctest::Approx(42.0f));
    CHECK(f.shoulderJoints[0].motorMode == 7);
}

TEST_CASE("phys::parse: a SHOJ chunk dividing evenly by both known strides throws, rather than "
          "silently picking one (never observed in any real file checked)") {
    std::vector<uint8_t> file;
    appendChunk(file, "PHYS", physChunk(2));
    // LCM(0x6c=108, 0x74=116) = 3132 -- the smallest size that's an exact
    // multiple of both.
    std::vector<uint8_t> shoj(3132, 0);
    appendChunk(file, "SHOJ", shoj);

    CHECK_THROWS_WITH_AS(husk::phys::parse(file), doctest::Contains("both known strides"),
                         husk::phys::ParseError);
}

TEST_CASE("phys::parse: a body referencing an out-of-range shape throws, naming the range") {
    std::vector<uint8_t> file;
    appendChunk(file, "PHYS", physChunk(1));
    std::vector<uint8_t> body;
    putU16(body, 0);
    putU16(body, 0);
    putVec3(body, 0, 0, 0);
    putU16(body, 0);
    putU16(body, 0);
    putI32(body, 0);
    putI32(body, 1);  // claims 1 shape, but no shape chunk exists at all
    appendChunk(file, "BODY", body);

    CHECK_THROWS_WITH_AS(husk::phys::parse(file), doctest::Contains("shape"), husk::phys::ParseError);
}

TEST_CASE("phys::parse: a joint referencing an out-of-range body throws") {
    std::vector<uint8_t> file;
    appendChunk(file, "PHYS", physChunk(1));
    std::vector<uint8_t> join;
    putU32(join, 0);  // bodyA -- but there are 0 bodies in this file
    putU32(join, 0);
    putU32(join, 0);
    putI16(join, 2);  // weld
    putI16(join, 0);
    appendChunk(file, "JOIN", join);

    CHECK_THROWS_WITH_AS(husk::phys::parse(file), doctest::Contains("body/bodies"),
                         husk::phys::ParseError);
}

TEST_CASE("phys::parse: a joint referencing an out-of-range type-specific index throws") {
    std::vector<uint8_t> file;
    appendChunk(file, "PHYS", physChunk(1));
    std::vector<uint8_t> body;
    putU16(body, 0);
    putU16(body, 0);
    putVec3(body, 0, 0, 0);
    putU16(body, 0);
    putU16(body, 0);
    putI32(body, 0);
    putI32(body, 0);
    appendChunk(file, "BODY", body);
    std::vector<uint8_t> join;
    putU32(join, 0);
    putU32(join, 0);
    putU32(join, 0);
    putI16(join, 2);  // weld -- but no weld-joint chunk exists at all
    putI16(join, 0);
    appendChunk(file, "JOIN", join);

    CHECK_THROWS_WITH_AS(husk::phys::parse(file), doctest::Contains("weld"), husk::phys::ParseError);
}

TEST_CASE("phys::parse: no PHYS chunk throws, naming what it found instead") {
    std::vector<uint8_t> file;
    appendChunk(file, "ZZZZ", {1, 2, 3, 4});

    CHECK_THROWS_WITH_AS(husk::phys::parse(file), doctest::Contains("no PHYS chunk"),
                         husk::phys::ParseError);
}

TEST_CASE("phys::parse: a record chunk whose size isn't an exact multiple of its stride throws") {
    std::vector<uint8_t> file;
    appendChunk(file, "PHYS", physChunk(1));
    appendChunk(file, "BODY", std::vector<uint8_t>(0x1c - 1, 0));  // 1 byte short of one BODY record

    CHECK_THROWS_WITH_AS(husk::phys::parse(file), doctest::Contains("exact multiple"),
                         husk::phys::ParseError);
}

TEST_CASE("phys::parse: PLYT's self-describing header+data region round-trips a single small "
          "polytope") {
    std::vector<uint8_t> file;
    appendChunk(file, "PHYS", physChunk(3));

    std::vector<uint8_t> plyt;
    putU32(plyt, 1);  // count: 1 polytope

    // header[0], 0x50 bytes:
    putU32(plyt, 2);          // vertexCount
    putU32(plyt, 0);          // unk_04
    putU32(plyt, 0);          // RUNTIME_08 (lo)
    putU32(plyt, 0);          // RUNTIME_08 (hi)
    putU32(plyt, 1);          // count_10
    putU32(plyt, 0);          // unk_14
    putU32(plyt, 0);          // RUNTIME_18 (lo)
    putU32(plyt, 0);          // RUNTIME_18 (hi)
    putU32(plyt, 0);          // RUNTIME_20 (lo)
    putU32(plyt, 0);          // RUNTIME_20 (hi)
    putU32(plyt, 3);          // nodeCount
    putU32(plyt, 0);          // unk_2C
    putU32(plyt, 0);          // RUNTIME_30 (lo)
    putU32(plyt, 0);          // RUNTIME_30 (hi)
    for (int i = 0; i < 6; ++i) putF32(plyt, 0.0f);  // unk_38[6]
    REQUIRE(plyt.size() == 4 + 0x50);

    // data[0]: 2 vertices, 1 unk_1 (16 bytes) + 1 unk_2 (1 byte), 3 nodes (4 bytes each)
    putVec3(plyt, 1, 0, 0);
    putVec3(plyt, 0, 1, 0);
    for (int i = 0; i < 16; ++i) plyt.push_back(static_cast<uint8_t>(i));  // unk_1[0]
    plyt.push_back(0xAB);                                                 // unk_2[0]
    plyt.push_back(1);
    plyt.push_back(0);
    plyt.push_back(1);
    plyt.push_back(2);  // nodes[0]
    plyt.push_back(1);
    plyt.push_back(1);
    plyt.push_back(2);
    plyt.push_back(0);  // nodes[1]
    plyt.push_back(1);
    plyt.push_back(0);
    plyt.push_back(0);
    plyt.push_back(1);  // nodes[2]
    appendChunk(file, "PLYT", plyt);

    // A shape/body pair pointing at this polytope, so it's reachable
    // end-to-end the same way a real file's would be.
    std::vector<uint8_t> shap;
    putI16(shap, 3);  // polytope
    putI16(shap, 0);
    putU32(shap, 0);
    putF32(shap, 0);
    putF32(shap, 0);
    putF32(shap, 0);
    appendChunk(file, "SHAP", shap);
    std::vector<uint8_t> body;
    putU16(body, 0);
    putU16(body, 0);
    putVec3(body, 0, 0, 0);
    putU16(body, 0);
    putU16(body, 0);
    putI32(body, 0);
    putI32(body, 1);
    appendChunk(file, "BODY", body);

    auto f = husk::phys::parse(file);
    REQUIRE(f.polytopes.size() == 1);
    const auto& poly = f.polytopes[0];
    CHECK(poly.vertexCount == 2);
    CHECK(poly.count10 == 1);
    CHECK(poly.nodeCount == 3);
    REQUIRE(poly.vertices.size() == 2);
    CHECK(poly.vertices[1].y == doctest::Approx(1));
    REQUIRE(poly.unk2.size() == 1);
    CHECK(poly.unk2[0] == 0xAB);
    REQUIRE(poly.nodes.size() == 3);
    CHECK(poly.nodes[1][2] == 2);
}

TEST_CASE("phys::parse: PLYT whose header+data doesn't consume the whole chunk throws") {
    std::vector<uint8_t> file;
    appendChunk(file, "PHYS", physChunk(3));
    std::vector<uint8_t> plyt;
    putU32(plyt, 1);  // count: 1 polytope
    putU32(plyt, 1);  // header[0].vertexCount=1 -- needs 12 more bytes of vertex data below
    // header[0]'s remaining 19 4-byte fields (unk_04/RUNTIME_08 x2/count_10/
    // unk_14/RUNTIME_18 x2/RUNTIME_20 x2/nodeCount/unk_2C/RUNTIME_30 x2/
    // unk_38[6]) -- all zero, so count_10/nodeCount both resolve to 0 and
    // contribute no further data requirement.
    for (int i = 0; i < 19; ++i) putU32(plyt, 0);
    REQUIRE(plyt.size() == 4 + 0x50);
    // deliberately omit the 1 vertex's 12 bytes of data the header above claims
    appendChunk(file, "PLYT", plyt);

    CHECK_THROWS_AS(husk::phys::parse(file), husk::phys::ParseError);
}
