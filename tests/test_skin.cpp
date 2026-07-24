// Spec source: https://wowdev.wiki/M2/.skin "Header", "Vertices", "Indices"
// sections (fetched 2026-07-24). Offsets below are typed out fresh from that
// page, not copied from src/skin.cpp -- same independent-transcription
// rationale as tests/test_m2.cpp (see the comment at the top of that file).
//
// M2SkinProfile header (>= Wrath, which is every field this parser reads):
//   0x00 magic (char[4], literal "SKIN", not reversed -- same M2-family
//        quirk as MD20/MD21, see src/chunk.hpp)
//   0x04 vertices (M2Array<uint16>) -- index into the M2's global vertex list
//   0x0C indices  (M2Array<uint16>) -- index into this skin's `vertices`
//        array above
// (bones/submeshes/batches/boneCountMax/shadow_batches follow at 0x14+;
// out of scope for this parser today, see src/skin.hpp)
//
// Indices form triangles in groups of 3: for skin-local vertex slot i,
// header.indices[i] selects a slot in header.vertices, and
// header.vertices[that] is the M2 global vertex index actually drawn.

#include <cstring>
#include <doctest/doctest.h>

#include "../src/skin.hpp"

namespace {

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

constexpr size_t kFixedHeaderSize = 0x14;  // through the end of `indices`

std::vector<uint8_t> buildSkinFile(uint32_t verticesCount, uint32_t verticesOffset,
                                    uint32_t indicesCount, uint32_t indicesOffset) {
    std::vector<uint8_t> buf(kFixedHeaderSize, 0);
    std::memcpy(buf.data() + 0x00, "SKIN", 4);
    putArray(buf, 0x04, verticesCount, verticesOffset);
    putArray(buf, 0x0C, indicesCount, indicesOffset);
    return buf;
}

}  // namespace

TEST_CASE("parseHeader: reads magic and both array fields at the right offsets") {
    auto file = buildSkinFile(6, 1000, 9, 2000);
    auto h = husk::skin::parseHeader(file);
    CHECK(h.magic == 0x4E494B53);  // "SKIN" little-endian
    CHECK(h.vertices.count == 6);
    CHECK(h.vertices.offset == 1000);
    CHECK(h.indices.count == 9);
    CHECK(h.indices.offset == 2000);
}

TEST_CASE("parseHeader: wrong magic throws") {
    auto file = buildSkinFile(0, 0, 0, 0);
    std::memcpy(file.data(), "XXXX", 4);
    CHECK_THROWS_AS(husk::skin::parseHeader(file), husk::skin::ParseError);
}

TEST_CASE("parseHeader: file shorter than the fixed header throws") {
    std::vector<uint8_t> file(0x10, 0);  // short of 0x14
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
