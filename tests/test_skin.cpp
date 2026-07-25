// Spec source: https://wowdev.wiki/M2/.skin "Header", "Vertices", "Indices",
// "Submeshes", "Texture units" sections (fetched 2026-07-24). Offsets below
// are typed out fresh from that page, not copied from src/skin.cpp -- same
// independent-transcription rationale as tests/test_m2.cpp (see the comment
// at the top of that file).
//
// M2SkinProfile header (>= Wrath, which is every field this parser reads):
//   0x00 magic (char[4], literal "SKIN", not reversed -- same M2-family
//        quirk as MD20/MD21, see src/chunk.hpp)
//   0x04 vertices (M2Array<uint16>) -- index into the M2's global vertex list
//   0x0C indices  (M2Array<uint16>) -- index into this skin's `vertices`
//        array above
//   0x14 bones (M2Array<ubyte4>) -- unread, see src/skin.hpp
//   0x1C submeshes (M2Array<M2SkinSection>)
//   0x24 batches (M2Array<M2Batch>)
// (boneCountMax/shadow_batches follow at 0x2C+; out of scope for this
// parser today, see src/skin.hpp)
//
// Indices form triangles in groups of 3: for skin-local vertex slot i,
// header.indices[i] selects a slot in header.vertices, and
// header.vertices[that] is the M2 global vertex index actually drawn.
//
// M2SkinSection (0x30 = 48 bytes), only the fields src/skin.hpp's Submesh
// surfaces:
//   0x00 skinSectionId (u16)   0x02 Level (u16)
//   0x04 vertexStart (u16)     0x06 vertexCount (u16)
//   0x08 indexStart (u16)      0x0A indexCount (u16)
//   0x0C boneCount (u16) ... 0x2C sortRadius (float) -- unread, see skin.hpp
//
// M2Batch (0x18 = 24 bytes), only the fields src/skin.hpp's Batch surfaces:
//   0x00 flags (u8)            0x01 priorityPlane (i8, unread)
//   0x02 shader_id (u16, unread) 0x04 skinSectionIndex (u16)
//   0x06 geosetIndex (u16, unread) 0x08 colorIndex (u16, unread)
//   0x0A materialIndex (u16)   0x0C materialLayer (u16, unread)
//   0x0E textureCount (u16)    0x10 textureComboIndex (u16)
//   0x12-0x17 texture{Coord,Weight,Transform}ComboIndex -- unread

#include <cstring>
#include <doctest/doctest.h>

#include "../src/skin.hpp"

namespace {

void putU8(std::vector<uint8_t>& buf, size_t off, uint8_t v) {
    if (buf.size() < off + 1) buf.resize(off + 1, 0);
    buf[off] = v;
}

void putU16(std::vector<uint8_t>& buf, size_t off, uint16_t v) {
    if (buf.size() < off + 2) buf.resize(off + 2, 0);
    std::memcpy(buf.data() + off, &v, 2);
}

void putU32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
    if (buf.size() < off + 4) buf.resize(off + 4, 0);
    std::memcpy(buf.data() + off, &v, 4);
}

void putArray(std::vector<uint8_t>& buf, size_t off, uint32_t count, uint32_t arrayOffset) {
    putU32(buf, off, count);
    putU32(buf, off + 4, arrayOffset);
}

constexpr size_t kFixedHeaderSize = 0x2C;  // through the end of `batches`

std::vector<uint8_t> buildSkinFile(uint32_t verticesCount, uint32_t verticesOffset,
                                    uint32_t indicesCount, uint32_t indicesOffset,
                                    uint32_t submeshesCount = 0, uint32_t submeshesOffset = 0,
                                    uint32_t batchesCount = 0, uint32_t batchesOffset = 0) {
    std::vector<uint8_t> buf(kFixedHeaderSize, 0);
    std::memcpy(buf.data() + 0x00, "SKIN", 4);
    putArray(buf, 0x04, verticesCount, verticesOffset);
    putArray(buf, 0x0C, indicesCount, indicesOffset);
    putArray(buf, 0x14, 0, 0);  // bones, unread
    putArray(buf, 0x1C, submeshesCount, submeshesOffset);
    putArray(buf, 0x24, batchesCount, batchesOffset);
    return buf;
}

// Writes one 0x30-byte M2SkinSection record at `off`.
void putSubmesh(std::vector<uint8_t>& buf, size_t off, uint16_t vertexStart, uint16_t vertexCount,
                 uint16_t indexStart, uint16_t indexCount) {
    if (buf.size() < off + 0x30) buf.resize(off + 0x30, 0);
    putU16(buf, off + 0x04, vertexStart);
    putU16(buf, off + 0x06, vertexCount);
    putU16(buf, off + 0x08, indexStart);
    putU16(buf, off + 0x0A, indexCount);
}

// Writes one 0x18-byte M2Batch record at `off`. colorIndex defaults to
// 0xFFFF ("none", per wowdev.wiki) and textureCoordComboIndex/
// textureWeightComboIndex default to 0, matching skin::Batch's own default
// member initializers -- existing call sites that don't care about these
// three newer fields don't need to change.
void putBatch(std::vector<uint8_t>& buf, size_t off, uint8_t flags, uint16_t skinSectionIndex,
              uint16_t materialIndex, uint16_t textureCount, uint16_t textureComboIndex,
              uint16_t colorIndex = 0xFFFF, uint16_t textureCoordComboIndex = 0,
              uint16_t textureWeightComboIndex = 0) {
    if (buf.size() < off + 0x18) buf.resize(off + 0x18, 0);
    putU8(buf, off + 0x00, flags);
    putU16(buf, off + 0x04, skinSectionIndex);
    putU16(buf, off + 0x08, colorIndex);
    putU16(buf, off + 0x0A, materialIndex);
    putU16(buf, off + 0x0E, textureCount);
    putU16(buf, off + 0x10, textureComboIndex);
    putU16(buf, off + 0x12, textureCoordComboIndex);
    putU16(buf, off + 0x14, textureWeightComboIndex);
}

}  // namespace

TEST_CASE("parseHeader: reads magic and every array field at the right offsets") {
    auto file = buildSkinFile(6, 1000, 9, 2000, 3, 3000, 5, 4000);
    auto h = husk::skin::parseHeader(file);
    CHECK(h.magic == 0x4E494B53);  // "SKIN" little-endian
    CHECK(h.vertices.count == 6);
    CHECK(h.vertices.offset == 1000);
    CHECK(h.indices.count == 9);
    CHECK(h.indices.offset == 2000);
    CHECK(h.submeshes.count == 3);
    CHECK(h.submeshes.offset == 3000);
    CHECK(h.batches.count == 5);
    CHECK(h.batches.offset == 4000);
}

TEST_CASE("parseHeader: wrong magic throws") {
    auto file = buildSkinFile(0, 0, 0, 0);
    std::memcpy(file.data(), "XXXX", 4);
    CHECK_THROWS_AS(husk::skin::parseHeader(file), husk::skin::ParseError);
}

TEST_CASE("parseHeader: file shorter than the fixed header (through batches) throws") {
    std::vector<uint8_t> file(0x20, 0);  // short of 0x2C
    std::memcpy(file.data(), "SKIN", 4);
    CHECK_THROWS_AS(husk::skin::parseHeader(file), husk::skin::ParseError);
}

TEST_CASE("parseU16Array: reads count values at offset in order") {
    std::vector<uint8_t> file(100, 0);
    size_t arrOff = 40;
    putU16(file, arrOff + 0, 111);
    putU16(file, arrOff + 2, 222);
    putU16(file, arrOff + 4, 333);

    husk::m2::Array a;
    a.count = 3;
    a.offset = static_cast<uint32_t>(arrOff);
    auto values = husk::skin::parseU16Array(file, a);
    REQUIRE(values.size() == 3);
    CHECK(values[0] == 111);
    CHECK(values[1] == 222);
    CHECK(values[2] == 333);
}

TEST_CASE("parseU16Array: empty array returns an empty vector without touching the file") {
    std::vector<uint8_t> file;  // zero bytes
    husk::m2::Array a;
    a.count = 0;
    a.offset = 99999;
    CHECK(husk::skin::parseU16Array(file, a).empty());
}

TEST_CASE("parseU16Array: array running past the end of the file throws") {
    std::vector<uint8_t> file(10, 0);
    husk::m2::Array a;
    a.count = 10;  // 20 bytes needed
    a.offset = 0;
    CHECK_THROWS_AS(husk::skin::parseU16Array(file, a), husk::skin::ParseError);
}

TEST_CASE("resolveTriangleIndices: two-level lookup collapses to global vertex indices") {
    // Local `vertices` lookup table (skin-local slot -> M2 global vertex
    // index): slots 0..3 point at global vertices 50, 51, 52, 53.
    std::vector<uint8_t> file(200, 0);
    size_t verticesOff = 100;
    putU16(file, verticesOff + 0, 50);
    putU16(file, verticesOff + 2, 51);
    putU16(file, verticesOff + 4, 52);
    putU16(file, verticesOff + 6, 53);

    // `indices` (one triangle): slots 2, 0, 3 -> should resolve to
    // global vertices 52, 50, 53.
    size_t indicesOff = 150;
    putU16(file, indicesOff + 0, 2);
    putU16(file, indicesOff + 2, 0);
    putU16(file, indicesOff + 4, 3);

    husk::skin::Header h;
    h.vertices.count = 4;
    h.vertices.offset = static_cast<uint32_t>(verticesOff);
    h.indices.count = 3;
    h.indices.offset = static_cast<uint32_t>(indicesOff);

    auto triangle = husk::skin::resolveTriangleIndices(file, h);
    REQUIRE(triangle.size() == 3);
    CHECK(triangle[0] == 52);
    CHECK(triangle[1] == 50);
    CHECK(triangle[2] == 53);
}

TEST_CASE("resolveTriangleIndices: an out-of-range local index throws") {
    std::vector<uint8_t> file(200, 0);
    size_t verticesOff = 100;
    putU16(file, verticesOff + 0, 50);  // only 1 entry, slot 0

    size_t indicesOff = 150;
    putU16(file, indicesOff + 0, 5);  // slot 5 doesn't exist

    husk::skin::Header h;
    h.vertices.count = 1;
    h.vertices.offset = static_cast<uint32_t>(verticesOff);
    h.indices.count = 1;
    h.indices.offset = static_cast<uint32_t>(indicesOff);

    CHECK_THROWS_AS(husk::skin::resolveTriangleIndices(file, h), husk::skin::ParseError);
}

TEST_CASE("parseSubmeshes: reads vertexStart/vertexCount/indexStart/indexCount for every entry") {
    std::vector<uint8_t> file(500, 0);
    size_t off = 200;
    putSubmesh(file, off, /*vertexStart=*/0, /*vertexCount=*/10, /*indexStart=*/0,
               /*indexCount=*/30);
    putSubmesh(file, off + 0x30, /*vertexStart=*/10, /*vertexCount=*/5, /*indexStart=*/30,
               /*indexCount=*/9);

    husk::m2::Array a;
    a.count = 2;
    a.offset = static_cast<uint32_t>(off);
    auto submeshes = husk::skin::parseSubmeshes(file, a);

    REQUIRE(submeshes.size() == 2);
    CHECK(submeshes[0].vertexStart == 0);
    CHECK(submeshes[0].vertexCount == 10);
    CHECK(submeshes[0].indexStart == 0);
    CHECK(submeshes[0].indexCount == 30);
    CHECK(submeshes[1].vertexStart == 10);
    CHECK(submeshes[1].vertexCount == 5);
    CHECK(submeshes[1].indexStart == 30);
    CHECK(submeshes[1].indexCount == 9);
}

TEST_CASE("parseSubmeshes: empty array returns an empty vector without touching the file") {
    std::vector<uint8_t> file;
    husk::m2::Array a;
    a.count = 0;
    a.offset = 12345;
    CHECK(husk::skin::parseSubmeshes(file, a).empty());
}

TEST_CASE("parseSubmeshes: array running past the end of the file throws") {
    std::vector<uint8_t> file(20, 0);  // 0x30 bytes needed for even one entry
    husk::m2::Array a;
    a.count = 1;
    a.offset = 0;
    CHECK_THROWS_AS(husk::skin::parseSubmeshes(file, a), husk::skin::ParseError);
}

TEST_CASE("parseBatches: reads flags/skinSectionIndex/materialIndex/textureCount/"
          "textureComboIndex for every entry") {
    std::vector<uint8_t> file(500, 0);
    size_t off = 200;
    putBatch(file, off, /*flags=*/16, /*skinSectionIndex=*/0, /*materialIndex=*/3,
             /*textureCount=*/1, /*textureComboIndex=*/0);
    putBatch(file, off + 0x18, /*flags=*/144, /*skinSectionIndex=*/63, /*materialIndex=*/0,
             /*textureCount=*/6, /*textureComboIndex=*/2);

    husk::m2::Array a;
    a.count = 2;
    a.offset = static_cast<uint32_t>(off);
    auto batches = husk::skin::parseBatches(file, a);

    REQUIRE(batches.size() == 2);
    CHECK(batches[0].flags == 16);
    CHECK(batches[0].skinSectionIndex == 0);
    CHECK(batches[0].materialIndex == 3);
    CHECK(batches[0].textureCount == 1);
    CHECK(batches[0].textureComboIndex == 0);
    // Neither call above specified colorIndex/textureCoordComboIndex/
    // textureWeightComboIndex -- putBatch's own defaults (0xFFFF/0/0)
    // should come through unchanged.
    CHECK(batches[0].colorIndex == 0xFFFF);
    CHECK(batches[0].textureCoordComboIndex == 0);
    CHECK(batches[0].textureWeightComboIndex == 0);
    CHECK(batches[1].flags == 144);
    CHECK(batches[1].skinSectionIndex == 63);
    CHECK(batches[1].materialIndex == 0);
    CHECK(batches[1].textureCount == 6);
    CHECK(batches[1].textureComboIndex == 2);
}

TEST_CASE("parseBatches: reads colorIndex/textureCoordComboIndex/textureWeightComboIndex") {
    std::vector<uint8_t> file(100, 0);
    size_t off = 20;
    putBatch(file, off, /*flags=*/0, /*skinSectionIndex=*/5, /*materialIndex=*/2,
             /*textureCount=*/1, /*textureComboIndex=*/1, /*colorIndex=*/7,
             /*textureCoordComboIndex=*/1, /*textureWeightComboIndex=*/3);

    husk::m2::Array a;
    a.count = 1;
    a.offset = static_cast<uint32_t>(off);
    auto batches = husk::skin::parseBatches(file, a);

    REQUIRE(batches.size() == 1);
    CHECK(batches[0].colorIndex == 7);
    CHECK(batches[0].textureCoordComboIndex == 1);
    CHECK(batches[0].textureWeightComboIndex == 3);
}

TEST_CASE("parseBatches: empty array returns an empty vector without touching the file") {
    std::vector<uint8_t> file;
    husk::m2::Array a;
    a.count = 0;
    a.offset = 54321;
    CHECK(husk::skin::parseBatches(file, a).empty());
}

TEST_CASE("parseBatches: array running past the end of the file throws") {
    std::vector<uint8_t> file(10, 0);  // 0x18 bytes needed for even one entry
    husk::m2::Array a;
    a.count = 1;
    a.offset = 0;
    CHECK_THROWS_AS(husk::skin::parseBatches(file, a), husk::skin::ParseError);
}
