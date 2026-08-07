// Tests for husk::m2's scene module (src/m2_scene.hpp/.cpp): parseAttachments/parseEvents/parseLights/parseRibbons/parseParticles.
// Split out of the former tests/test_m2.cpp -- see FILE_SPLIT_TODO.md Item 5.

#include "test_m2_fixtures.hpp"

// M2Attachment (wowdev.wiki M2#Attachments), 0x28 bytes: 0x00 id (u32),
// 0x04 bone (i16), 0x06 unknown (u16, unread), 0x08 position (C3Vector).
void putAttachment(std::vector<uint8_t>& buf, size_t off, uint32_t id, int16_t bone,
                    const husk::m2::Vec3& position) {
    if (buf.size() < off + 0x28) buf.resize(off + 0x28, 0);
    putU32(buf, off + 0x00, id);
    uint16_t boneBits = static_cast<uint16_t>(bone);
    std::memcpy(buf.data() + off + 0x04, &boneBits, 2);
    putF32(buf, off + 0x08, position.x);
    putF32(buf, off + 0x0C, position.y);
    putF32(buf, off + 0x10, position.z);
}

TEST_CASE("parseAttachments: reads id/bone/position for every entry") {
    std::vector<uint8_t> blob(200, 0);
    putAttachment(blob, 200, 3, 54, {-0.134592f, -0.281851f, 1.3306f});
    putAttachment(blob, 200 + 0x28, 4, 55, {1, 2, 3});

    husk::m2::Array array;
    array.count = 2;
    array.offset = 200;
    auto attachments = husk::m2::parseAttachments(blob, array);

    REQUIRE(attachments.size() == 2);
    CHECK(attachments[0].id == 3);
    CHECK(attachments[0].bone == 54);
    CHECK(attachments[0].position.x == doctest::Approx(-0.134592f));
    CHECK(attachments[0].position.z == doctest::Approx(1.3306f));
    CHECK(attachments[1].id == 4);
    CHECK(attachments[1].bone == 55);
}


TEST_CASE("parseAttachments: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 54321;
    CHECK(husk::m2::parseAttachments(blob, array).empty());
}


TEST_CASE("parseAttachments: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(20, 0);
    husk::m2::Array array;
    array.count = 1;   // 0x28 bytes needed
    array.offset = 0;  // but the blob is only 20 bytes
    CHECK_THROWS_AS(husk::m2::parseAttachments(blob, array), husk::m2::ParseError);
}

// M2Event (wowdev.wiki M2#Events), 0x24 bytes: 0x00 identifier (4 raw
// ASCII bytes), 0x04 data (u32), 0x08 bone (u32), 0x0C position (C3Vector).
void putEvent(std::vector<uint8_t>& buf, size_t off, const char identifier[4], uint32_t data,
              uint32_t bone, const husk::m2::Vec3& position) {
    if (buf.size() < off + 0x24) buf.resize(off + 0x24, 0);
    std::memcpy(buf.data() + off + 0x00, identifier, 4);
    putU32(buf, off + 0x04, data);
    putU32(buf, off + 0x08, bone);
    putF32(buf, off + 0x0C, position.x);
    putF32(buf, off + 0x10, position.y);
    putF32(buf, off + 0x14, position.z);
}


TEST_CASE("parseEvents: reads identifier/data/bone/position for every entry") {
    std::vector<uint8_t> blob(200, 0);
    putEvent(blob, 200, "$HIT", 0, 94, {-0.0276569f, 0.000218528f, 1.1743f});
    putEvent(blob, 200 + 0x24, "$DTH", 7, 10, {1, 2, 3});

    husk::m2::Array array;
    array.count = 2;
    array.offset = 200;
    auto events = husk::m2::parseEvents(blob, array);

    REQUIRE(events.size() == 2);
    CHECK(events[0].identifier == "$HIT");
    CHECK(events[0].data == 0);
    CHECK(events[0].bone == 94);
    CHECK(events[0].position.z == doctest::Approx(1.1743f));
    CHECK(events[1].identifier == "$DTH");
    CHECK(events[1].data == 7);
    CHECK(events[1].bone == 10);
}


TEST_CASE("parseEvents: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 54321;
    CHECK(husk::m2::parseEvents(blob, array).empty());
}


TEST_CASE("parseEvents: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(20, 0);
    husk::m2::Array array;
    array.count = 1;   // 0x24 bytes needed
    array.offset = 0;  // but the blob is only 20 bytes
    CHECK_THROWS_AS(husk::m2::parseEvents(blob, array), husk::m2::ParseError);
}

// M2Light (wowdev.wiki M2#Lights), 0x9C bytes: only the 3 static fields
// (0x00 type u16, 0x02 bone i16, 0x04 position C3Vector) are written here
// -- the rest of the record is M2Track-animated data this parser skips.
void putLight(std::vector<uint8_t>& buf, size_t off, uint16_t type, int16_t bone,
              const husk::m2::Vec3& position) {
    if (buf.size() < off + 0x9C) buf.resize(off + 0x9C, 0);
    putU16(buf, off + 0x00, type);
    uint16_t boneBits = static_cast<uint16_t>(bone);
    std::memcpy(buf.data() + off + 0x02, &boneBits, 2);
    putF32(buf, off + 0x04, position.x);
    putF32(buf, off + 0x08, position.y);
    putF32(buf, off + 0x0C, position.z);
}


TEST_CASE("parseLights: reads type/bone/position for every entry, skipping the M2Track region") {
    std::vector<uint8_t> blob(300, 0);
    putLight(blob, 300, 1, -1, {1, 2, 3});
    putLight(blob, 300 + 0x9C, 0, 5, {4, 5, 6});

    husk::m2::Array array;
    array.count = 2;
    array.offset = 300;
    auto lights = husk::m2::parseLights(blob, array);

    REQUIRE(lights.size() == 2);
    CHECK(lights[0].type == 1);
    CHECK(lights[0].bone == -1);
    CHECK(lights[0].position.x == doctest::Approx(1));
    CHECK(lights[1].type == 0);
    CHECK(lights[1].bone == 5);
    CHECK(lights[1].position.z == doctest::Approx(6));
}


TEST_CASE("parseLights: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 54321;
    CHECK(husk::m2::parseLights(blob, array).empty());
}


TEST_CASE("parseLights: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(20, 0);
    husk::m2::Array array;
    array.count = 1;   // 0x9C bytes needed
    array.offset = 0;  // but the blob is only 20 bytes
    CHECK_THROWS_AS(husk::m2::parseLights(blob, array), husk::m2::ParseError);
}

// M2Ribbon (wowdev.wiki M2#Ribbon_emitters), 0xB0 bytes: only the static
// fields (see husk::m2::Ribbon's doc comment) are written here -- the
// M2Track/M2Array-indirected fields in between are left zeroed, same
// skipped-region convention putLight above uses.
void putRibbon(std::vector<uint8_t>& buf, size_t off, uint32_t ribbonId, uint32_t boneIndex,
               const husk::m2::Vec3& position, float edgesPerSecond, float edgeLifetime,
               float gravity, uint16_t textureRows, uint16_t textureCols) {
    if (buf.size() < off + 0xB0) buf.resize(off + 0xB0, 0);
    putU32(buf, off + 0x00, ribbonId);
    putU32(buf, off + 0x04, boneIndex);
    putF32(buf, off + 0x08, position.x);
    putF32(buf, off + 0x0C, position.y);
    putF32(buf, off + 0x10, position.z);
    putF32(buf, off + 0x74, edgesPerSecond);
    putF32(buf, off + 0x78, edgeLifetime);
    putF32(buf, off + 0x7C, gravity);
    putU16(buf, off + 0x80, textureRows);
    putU16(buf, off + 0x82, textureCols);
}


TEST_CASE("parseRibbons: reads ribbonId/boneIndex/position/edgesPerSecond/edgeLifetime/gravity/"
          "textureRows/textureCols for every entry, skipping the M2Track/M2Array regions") {
    std::vector<uint8_t> blob(300, 0);
    putRibbon(blob, 300, 0xFFFFFFFFu, 3, {1, 2, 3}, 10.0f, 2.5f, -0.5f, 4, 2);
    putRibbon(blob, 300 + 0xB0, 0xFFFFFFFFu, 7, {4, 5, 6}, 20.0f, 1.0f, 0.5f, 1, 1);

    husk::m2::Array array;
    array.count = 2;
    array.offset = 300;
    auto ribbons = husk::m2::parseRibbons(blob, array);

    REQUIRE(ribbons.size() == 2);
    CHECK(ribbons[0].ribbonId == 0xFFFFFFFFu);
    CHECK(ribbons[0].boneIndex == 3);
    CHECK(ribbons[0].position.x == doctest::Approx(1));
    CHECK(ribbons[0].position.z == doctest::Approx(3));
    CHECK(ribbons[0].edgesPerSecond == doctest::Approx(10.0f));
    CHECK(ribbons[0].edgeLifetime == doctest::Approx(2.5f));
    CHECK(ribbons[0].gravity == doctest::Approx(-0.5f));
    CHECK(ribbons[0].textureRows == 4);
    CHECK(ribbons[0].textureCols == 2);

    CHECK(ribbons[1].boneIndex == 7);
    CHECK(ribbons[1].position.y == doctest::Approx(5));
    CHECK(ribbons[1].edgesPerSecond == doctest::Approx(20.0f));
    CHECK(ribbons[1].textureRows == 1);
    CHECK(ribbons[1].textureCols == 1);
}


TEST_CASE("parseRibbons: reads textureIndices/materialIndices lookup arrays, the 6 M2Track offsets, "
          "and the trailing priorityPlane/ribbonColorIndex/textureTransformLookupIndex fields") {
    std::vector<uint8_t> blob(300, 0);
    putRibbon(blob, 300, 0xFFFFFFFFu, 3, {1, 2, 3}, 10.0f, 2.5f, -0.5f, 4, 2);

    // textureIndices (0x14) -> [5, 6]; materialIndices (0x1C) -> [9].
    size_t texIdxOff = blob.size();
    putU16(blob, texIdxOff, 5);
    putU16(blob, texIdxOff + 2, 6);
    putArray(blob, 300 + 0x14, 2, static_cast<uint32_t>(texIdxOff));
    size_t matIdxOff = blob.size();
    putU16(blob, matIdxOff, 9);
    putArray(blob, 300 + 0x1C, 1, static_cast<uint32_t>(matIdxOff));

    putU16(blob, 300 + 0xAC, static_cast<uint16_t>(-7));  // priorityPlane
    blob[300 + 0xAE] = static_cast<uint8_t>(static_cast<int8_t>(-2));    // ribbonColorIndex
    blob[300 + 0xAF] = static_cast<uint8_t>(static_cast<int8_t>(3));     // textureTransformLookupIndex

    husk::m2::Array array;
    array.count = 1;
    array.offset = 300;
    auto ribbons = husk::m2::parseRibbons(blob, array);

    REQUIRE(ribbons.size() == 1);
    const auto& r = ribbons[0];
    REQUIRE(r.textureIndices.size() == 2);
    CHECK(r.textureIndices[0] == 5);
    CHECK(r.textureIndices[1] == 6);
    REQUIRE(r.materialIndices.size() == 1);
    CHECK(r.materialIndices[0] == 9);
    CHECK(r.colorTrackOffset == 300 + 0x24);
    CHECK(r.alphaTrackOffset == 300 + 0x38);
    CHECK(r.heightAboveTrackOffset == 300 + 0x4C);
    CHECK(r.heightBelowTrackOffset == 300 + 0x60);
    CHECK(r.texSlotTrackOffset == 300 + 0x84);
    CHECK(r.visibilityTrackOffset == 300 + 0x98);
    CHECK(r.priorityPlane == -7);
    CHECK(r.ribbonColorIndex == -2);
    CHECK(r.textureTransformLookupIndex == 3);
}


TEST_CASE("parseRibbons: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 54321;
    CHECK(husk::m2::parseRibbons(blob, array).empty());
}


TEST_CASE("parseRibbons: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(20, 0);
    husk::m2::Array array;
    array.count = 1;   // 0xB0 bytes needed
    array.offset = 0;  // but the blob is only 20 bytes
    CHECK_THROWS_AS(husk::m2::parseRibbons(blob, array), husk::m2::ParseError);
}

// M2Particle, Cata+ shape (wowdev.wiki M2#Particle_emitters -- see
// husk::m2::ParticleEmitter's doc comment for the full 0x1EC-byte offset
// derivation and its real-file cross-check). Writes the fields this test
// suite actually checks; every M2Track/FBlock region in between is left
// zeroed (interpolation_type 0/global_sequence 0 reads as a valid, if
// empty, constant track -- same convention putRibbon above uses for its
// own skipped track regions).
void putParticle(std::vector<uint8_t>& buf, size_t off, uint32_t particleId, uint32_t flags,
                  const husk::m2::Vec3& position, uint16_t boneId, uint16_t textureId,
                  const std::string& particleModelFilename, uint8_t blendingType,
                  uint8_t emitterType, uint16_t particleColorIndex, uint16_t rows, uint16_t columns) {
    if (buf.size() < off + 0x1EC) buf.resize(off + 0x1EC, 0);
    putU32(buf, off + 0x00, particleId);
    putU32(buf, off + 0x04, flags);
    putF32(buf, off + 0x08, position.x);
    putF32(buf, off + 0x0C, position.y);
    putF32(buf, off + 0x10, position.z);
    putU16(buf, off + 0x14, boneId);
    putU16(buf, off + 0x16, textureId);
    if (particleModelFilename.empty()) {
        putArray(buf, off + 0x18, 0, 0);
    } else {
        size_t nameOff = buf.size();
        buf.resize(nameOff + particleModelFilename.size());
        std::memcpy(buf.data() + nameOff, particleModelFilename.data(), particleModelFilename.size());
        putArray(buf, off + 0x18, static_cast<uint32_t>(particleModelFilename.size()),
                 static_cast<uint32_t>(nameOff));
    }
    putArray(buf, off + 0x20, 0, 0);  // childEmittersModelFilename, empty
    buf[off + 0x28] = blendingType;
    buf[off + 0x29] = emitterType;
    putU16(buf, off + 0x2A, particleColorIndex);
    putU16(buf, off + 0x30, rows);
    putU16(buf, off + 0x32, columns);
}


TEST_CASE("parseParticles: reads particleId/flags/position/boneId/textureId/"
          "particleModelFilename/blendingType/emitterType/particleColorIndex/rows/columns for "
          "every entry") {
    // Both fixed-size records reserved up front, same reason
    // parseTextures's own test pre-reserves 2*0x10 -- putParticle appends
    // record 0's variable-length filename data at the *current* end of the
    // buffer, which would otherwise land exactly where record 1's fixed
    // bytes need to go.
    std::vector<uint8_t> blob(500 + 2 * 0x1EC, 0);
    putParticle(blob, 500, 0xFFFFFFFFu, 0x10000000u, {0.9f, -0.1f, 0.05f}, 12, 0x4,
                "particle\\fire.m2", 4, 1, 0, 1, 1);
    putParticle(blob, 500 + 0x1EC, 0xFFFFFFFFu, 2, {0, 0, 0}, 13, 0, "", 2, 2, 11, 2, 4);

    husk::m2::Array array;
    array.count = 2;
    array.offset = 500;
    auto particles = husk::m2::parseParticles(blob, array);

    REQUIRE(particles.size() == 2);
    const auto& p0 = particles[0];
    CHECK(p0.particleId == 0xFFFFFFFFu);
    CHECK(p0.flags == 0x10000000u);
    CHECK(p0.position.x == doctest::Approx(0.9f));
    CHECK(p0.position.z == doctest::Approx(0.05f));
    CHECK(p0.boneId == 12);
    CHECK(p0.textureId == 0x4);
    CHECK(p0.particleModelFilename == "particle\\fire.m2");
    CHECK(p0.blendingType == 4);
    CHECK(p0.emitterType == 1);
    CHECK(p0.rows == 1);
    CHECK(p0.columns == 1);

    const auto& p1 = particles[1];
    CHECK(p1.boneId == 13);
    CHECK(p1.particleModelFilename.empty());
    CHECK(p1.blendingType == 2);
    CHECK(p1.emitterType == 2);
    CHECK(p1.particleColorIndex == 11);
    CHECK(p1.rows == 2);
    CHECK(p1.columns == 4);
}


TEST_CASE("parseParticles: reads multiTexScale/priorityPlane/lifespanVariation/splinePoints/"
          "multiTexScrollMid/multiTexScrollRange") {
    std::vector<uint8_t> blob(500, 0);
    putParticle(blob, 500, 0xFFFFFFFFu, 0, {0, 0, 0}, 0, 0, "", 0, 1, 0, 1, 1);

    // multiTexScale: fixed_point<int8_t,2,5>[2] -- raw 16 -> 16/32 = 0.5.
    blob[500 + 0x2C] = 16;
    blob[500 + 0x2D] = static_cast<uint8_t>(static_cast<int8_t>(-16));  // -16/32 = -0.5
    putU16(blob, 500 + 0x2E, static_cast<uint16_t>(-3));                // priorityPlane
    putF32(blob, 500 + 0xAC, 0.25f);                                    // lifespanVariation

    // splinePoints (M2Array<C3Vector>) -> one point (1, 2, 3).
    size_t splineOff = blob.size();
    putF32(blob, splineOff, 1.0f);
    putF32(blob, splineOff + 4, 2.0f);
    putF32(blob, splineOff + 8, 3.0f);
    putArray(blob, 500 + 0x1C0, 1, static_cast<uint32_t>(splineOff));

    // multiTexScrollMid/Range: fixed_point<uint16_t,6,9>, raw 512 -> 1.0.
    putU16(blob, 500 + 0x1DC, 512);
    putU16(blob, 500 + 0x1E4, 1024);  // 1024/512 = 2.0

    husk::m2::Array array;
    array.count = 1;
    array.offset = 500;
    auto particles = husk::m2::parseParticles(blob, array);

    REQUIRE(particles.size() == 1);
    const auto& p = particles[0];
    CHECK(p.multiTexScale[0] == doctest::Approx(0.5f));
    CHECK(p.multiTexScale[1] == doctest::Approx(-0.5f));
    CHECK(p.priorityPlane == -3);
    CHECK(p.lifespanVariation == doctest::Approx(0.25f));
    REQUIRE(p.splinePoints.size() == 1);
    CHECK(p.splinePoints[0].y == doctest::Approx(2.0f));
    CHECK(p.multiTexScrollMid[0] == doctest::Approx(1.0f));
    CHECK(p.multiTexScrollRange[0] == doctest::Approx(2.0f));
}


TEST_CASE("parseParticles: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 12345;
    CHECK(husk::m2::parseParticles(blob, array).empty());
}


TEST_CASE("parseParticles: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(20, 0);
    husk::m2::Array array;
    array.count = 1;   // 0x1EC bytes needed
    array.offset = 0;  // but the blob is only 20 bytes
    CHECK_THROWS_AS(husk::m2::parseParticles(blob, array), husk::m2::ParseError);
}



