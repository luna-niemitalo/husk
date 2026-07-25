// .bone files have no documented byte layout on wowdev.wiki at all (unlike
// M2/.skel's BFID chunk, which only documents the FileDataID *array*
// pointing at these files, not their content) -- the shape tested here
// (leading uint32 version, then a chunked BIDA/BOMT pair) was reverse
// engineered against real Legion+ character .bone files, not transcribed
// from a spec. See src/bone.hpp's doc comment for the reasoning: BOMT's
// per-entry 4x4 matrices were confirmed as row-major affine transforms (not
// guessed at) by observing every real file's last column read as exactly
// (0,0,0,1) and the 3x3 upper-left staying near-identity with small
// translation-sized deltas in row 3 -- the signature of a small corrective
// transform, not arbitrary data.

#include <cstring>
#include <doctest/doctest.h>

#include "../src/bone.hpp"

namespace {

void putU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
}

void putU32(std::vector<uint8_t>& buf, uint32_t v) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

void putF32(std::vector<uint8_t>& buf, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    putU32(buf, bits);
}

void appendChunk(std::vector<uint8_t>& buf, const char tag[4], const std::vector<uint8_t>& payload) {
    buf.insert(buf.end(), tag, tag + 4);
    putU32(buf, static_cast<uint32_t>(payload.size()));
    buf.insert(buf.end(), payload.begin(), payload.end());
}

// A near-identity 4x4 row-major matrix with a small translation in row 3 --
// the shape every real BOMT entry this parser was checked against has.
std::vector<uint8_t> identityWithTranslation(float tx, float ty, float tz) {
    std::vector<uint8_t> m;
    float rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, ty, tz, 1};
    for (float f : rows) putF32(m, f);
    return m;
}

std::vector<uint8_t> buildBoneFile(const std::vector<uint16_t>& boneIndices,
                                    const std::vector<std::vector<uint8_t>>& matrices,
                                    uint32_t version = 1) {
    std::vector<uint8_t> file;
    putU32(file, version);
    std::vector<uint8_t> bida;
    for (uint16_t idx : boneIndices) putU16(bida, idx);
    std::vector<uint8_t> bomt;
    for (const auto& m : matrices) bomt.insert(bomt.end(), m.begin(), m.end());
    appendChunk(file, "BIDA", bida);
    appendChunk(file, "BOMT", bomt);
    return file;
}

}  // namespace

TEST_CASE("bone::parse: reads bone indices and matrices in lockstep") {
    auto file = buildBoneFile({58, 59, 90}, {identityWithTranslation(0.001f, 0, 0),
                                              identityWithTranslation(0, -0.002f, 0),
                                              identityWithTranslation(0, 0, 0.003f)});

    auto corrections = husk::bone::parse(file);
    REQUIRE(corrections.size() == 3);

    CHECK(corrections[0].boneIndex == 58);
    CHECK(corrections[0].matrix[0] == doctest::Approx(1));   // [0][0]
    CHECK(corrections[0].matrix[12] == doctest::Approx(0.001f));  // row 3, x
    CHECK(corrections[0].matrix[15] == doctest::Approx(1));  // last column, row 3

    CHECK(corrections[1].boneIndex == 59);
    CHECK(corrections[1].matrix[13] == doctest::Approx(-0.002f));  // row 3, y

    CHECK(corrections[2].boneIndex == 90);
    CHECK(corrections[2].matrix[14] == doctest::Approx(0.003f));  // row 3, z
}

TEST_CASE("bone::parse: empty BIDA/BOMT (count 0) is not an error") {
    auto file = buildBoneFile({}, {});
    auto corrections = husk::bone::parse(file);
    CHECK(corrections.empty());
}

TEST_CASE("bone::parse: unrelated chunks around BIDA/BOMT don't confuse it") {
    std::vector<uint8_t> file;
    putU32(file, 1);
    appendChunk(file, "XXXX", {1, 2, 3, 4});
    std::vector<uint8_t> bida;
    putU16(bida, 42);
    appendChunk(file, "BIDA", bida);
    appendChunk(file, "BOMT", identityWithTranslation(1, 2, 3));

    auto corrections = husk::bone::parse(file);
    REQUIRE(corrections.size() == 1);
    CHECK(corrections[0].boneIndex == 42);
}

TEST_CASE("bone::parse: missing BOMT chunk throws, names what it found") {
    std::vector<uint8_t> file;
    putU32(file, 1);
    std::vector<uint8_t> bida;
    putU16(bida, 1);
    appendChunk(file, "BIDA", bida);

    CHECK_THROWS_WITH_AS(husk::bone::parse(file), doctest::Contains("BIDA"), husk::bone::ParseError);
}

TEST_CASE("bone::parse: BIDA/BOMT entry-count mismatch throws") {
    std::vector<uint8_t> file;
    putU32(file, 1);
    std::vector<uint8_t> bida;
    putU16(bida, 1);
    putU16(bida, 2);  // 2 indices...
    appendChunk(file, "BIDA", bida);
    appendChunk(file, "BOMT", identityWithTranslation(0, 0, 0));  // ...but only 1 matrix

    CHECK_THROWS_AS(husk::bone::parse(file), husk::bone::ParseError);
}

TEST_CASE("bone::parse: file shorter than the leading version field throws") {
    std::vector<uint8_t> file = {1, 2, 3};  // 3 bytes, needs 4
    CHECK_THROWS_AS(husk::bone::parse(file), husk::bone::ParseError);
}
