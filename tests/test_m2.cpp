// Spec source: https://wowdev.wiki/M2 "Header" and "Chunks" sections
// (fetched 2026-07-24). Offsets below are typed out fresh from that page,
// not copied from src/m2.cpp -- the point is that this file independently
// encodes the spec, so a mistake in m2.cpp's offset table shows up as a
// test failure instead of being rubber-stamped by a fixture built from the
// same wrong assumption.
//
// Header field offsets (expansion level >= 3, which covers every currently
// shipping model per the wiki's own version-range note):
//   0x000 magic (char[4])              0x004 version (uint32)
//   0x008 name (M2Array<char>)         0x010 global_flags (uint32)
//   0x014 global_loops (M2Array)       0x01C sequences (M2Array)
//   0x024 sequenceIdxHashById (M2Array) 0x02C bones (M2Array)
//   0x034 boneIndicesById (M2Array)    0x03C vertices (M2Array)
//   0x044 num_skin_profiles (uint32)   0x048 colors (M2Array)
//   0x050 textures (M2Array)           0x058 texture_weights (M2Array)
//   0x060 texture_transforms (M2Array) 0x068 textureIndicesById (M2Array)
//   0x070 materials (M2Array)          0x078 boneCombos (M2Array)
//   0x080 textureCombos (M2Array)      0x088 textureCoordCombos (M2Array)
//   0x090 textureWeightCombos (M2Array) 0x098 textureTransformCombos (M2Array)
//   0x0A0 bounding_box (CAaBox, 24B)   0x0B8 bounding_sphere_radius (float)
//   0x0BC collision_box (CAaBox, 24B)  0x0D4 collision_sphere_radius (float)
// M2Array<T> = { uint32_t count; uint32_t offset; } (8 bytes), offset
// relative to the start of this same blob.
// 0x0D8 is the end of the fixed portion this parser reads (216 bytes).
//
// M2Vertex (wowdev.wiki M2#Vertices), 48 bytes, no padding between fields:
//   0x00 pos (C3Vector)                0x0C bone_weights (uint8[4])
//   0x10 bone_indices (uint8[4])       0x14 normal (C3Vector)
//   0x20 tex_coords[2] (C2Vector[2])
// "Models ... use a Z-up coordinate system[]; to convert to Y-up, the X, Y,
// Z values become (X, -Z, Y)" -- this parser does NOT apply that conversion;
// it reads raw file values verbatim, same as every other field here.

#include <cstring>
#include <doctest/doctest.h>

#include "../src/chunk.hpp"
#include "../src/m2.hpp"

namespace {

constexpr size_t kFixedHeaderSize = 0x0D8;

void putU32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
    if (buf.size() < off + 4) buf.resize(off + 4, 0);
    std::memcpy(buf.data() + off, &v, 4);
}

void putF32(std::vector<uint8_t>& buf, size_t off, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    putU32(buf, off, bits);
}

void putArray(std::vector<uint8_t>& buf, size_t off, uint32_t count, uint32_t arrayOffset) {
    putU32(buf, off, count);
    putU32(buf, off + 4, arrayOffset);
}

// Builds a minimal-but-fully-populated MD20 blob: every array field gets a
// distinct (count, offset) pair so a field landing at the wrong byte offset
// shows up as reading the wrong sentinel value, not a coincidentally-correct
// zero. The model name "Sentinel" is appended right after the fixed header.
std::vector<uint8_t> buildMd20Blob() {
    std::vector<uint8_t> buf(kFixedHeaderSize, 0);

    std::memcpy(buf.data() + 0x000, "MD20", 4);
    putU32(buf, 0x004, 274);  // version

    std::string name = "Sentinel";
    size_t nameOffset = kFixedHeaderSize;
    buf.resize(nameOffset + name.size());
    std::memcpy(buf.data() + nameOffset, name.data(), name.size());
    putArray(buf, 0x008, static_cast<uint32_t>(name.size()), static_cast<uint32_t>(nameOffset));

    putU32(buf, 0x010, 0x1234);  // global_flags
    putArray(buf, 0x014, 1, 1000);   // global_loops
    putArray(buf, 0x01C, 2, 1001);   // sequences
    putArray(buf, 0x024, 3, 1002);   // sequenceIdxHashById
    putArray(buf, 0x02C, 4, 1003);   // bones
    putArray(buf, 0x034, 5, 1004);   // boneIndicesById
    putArray(buf, 0x03C, 6, 1005);   // vertices
    putU32(buf, 0x044, 7);           // num_skin_profiles
    putArray(buf, 0x048, 8, 1006);   // colors
    putArray(buf, 0x050, 9, 1007);   // textures
    putArray(buf, 0x058, 10, 1008);  // texture_weights
    putArray(buf, 0x060, 11, 1009);  // texture_transforms
    putArray(buf, 0x068, 12, 1010);  // textureIndicesById
    putArray(buf, 0x070, 13, 1011);  // materials
    putArray(buf, 0x078, 14, 1012);  // boneCombos
    putArray(buf, 0x080, 15, 1013);  // textureCombos
    putArray(buf, 0x088, 16, 1014);  // textureCoordCombos
    putArray(buf, 0x090, 17, 1015);  // textureWeightCombos
    putArray(buf, 0x098, 18, 1016);  // textureTransformCombos

    // bounding_box: min(-1,-2,-3) max(4,5,6)
    putF32(buf, 0x0A0, -1); putF32(buf, 0x0A4, -2); putF32(buf, 0x0A8, -3);
    putF32(buf, 0x0AC, 4);  putF32(buf, 0x0B0, 5);  putF32(buf, 0x0B4, 6);
    putF32(buf, 0x0B8, 7.5f);  // bounding_sphere_radius

    // collision_box: min(-10,-20,-30) max(40,50,60)
    putF32(buf, 0x0BC, -10); putF32(buf, 0x0C0, -20); putF32(buf, 0x0C4, -30);
    putF32(buf, 0x0C8, 40);  putF32(buf, 0x0CC, 50);  putF32(buf, 0x0D0, 60);
    putF32(buf, 0x0D4, 99.5f);  // collision_sphere_radius

    return buf;
}

void appendChunk(std::vector<uint8_t>& buf, const char tag[4], const std::vector<uint8_t>& payload) {
    buf.insert(buf.end(), tag, tag + 4);
    uint32_t size = static_cast<uint32_t>(payload.size());
    uint8_t sizeBytes[4];
    std::memcpy(sizeBytes, &size, 4);
    buf.insert(buf.end(), sizeBytes, sizeBytes + 4);
    buf.insert(buf.end(), payload.begin(), payload.end());
}

// Writes one 48-byte M2Vertex record at `off`, per the offsets transcribed
// in the file-header comment above.
void putVertex(std::vector<uint8_t>& buf, size_t off, const husk::m2::Vertex& v) {
    if (buf.size() < off + 0x30) buf.resize(off + 0x30, 0);
    putF32(buf, off + 0x00, v.pos.x);
    putF32(buf, off + 0x04, v.pos.y);
    putF32(buf, off + 0x08, v.pos.z);
    for (int i = 0; i < 4; ++i) buf[off + 0x0C + i] = v.boneWeights[i];
    for (int i = 0; i < 4; ++i) buf[off + 0x10 + i] = v.boneIndices[i];
    putF32(buf, off + 0x14, v.normal.x);
    putF32(buf, off + 0x18, v.normal.y);
    putF32(buf, off + 0x1C, v.normal.z);
    putF32(buf, off + 0x20, v.texCoords[0].x);
    putF32(buf, off + 0x24, v.texCoords[0].y);
    putF32(buf, off + 0x28, v.texCoords[1].x);
    putF32(buf, off + 0x2C, v.texCoords[1].y);
}

void checkSentinelHeader(const husk::m2::Header& h) {
    CHECK(h.version == 274);
    CHECK(h.name == "Sentinel");
    CHECK(h.globalFlags == 0x1234);
    CHECK(h.globalLoops.count == 1);
    CHECK(h.globalLoops.offset == 1000);
    CHECK(h.sequences.count == 2);
    CHECK(h.sequences.offset == 1001);
    CHECK(h.sequenceLookup.count == 3);
    CHECK(h.sequenceLookup.offset == 1002);
    CHECK(h.bones.count == 4);
    CHECK(h.bones.offset == 1003);
    CHECK(h.boneLookup.count == 5);
    CHECK(h.boneLookup.offset == 1004);
    CHECK(h.vertices.count == 6);
    CHECK(h.vertices.offset == 1005);
    CHECK(h.numSkinProfiles == 7);
    CHECK(h.colors.count == 8);
    CHECK(h.colors.offset == 1006);
    CHECK(h.textures.count == 9);
    CHECK(h.textures.offset == 1007);
    CHECK(h.textureWeights.count == 10);
    CHECK(h.textureWeights.offset == 1008);
    CHECK(h.textureTransforms.count == 11);
    CHECK(h.textureTransforms.offset == 1009);
    CHECK(h.textureLookup.count == 12);
    CHECK(h.textureLookup.offset == 1010);
    CHECK(h.materials.count == 13);
    CHECK(h.materials.offset == 1011);
    CHECK(h.boneCombos.count == 14);
    CHECK(h.boneCombos.offset == 1012);
    CHECK(h.textureCombos.count == 15);
    CHECK(h.textureCombos.offset == 1013);
    CHECK(h.textureCoordCombos.count == 16);
    CHECK(h.textureCoordCombos.offset == 1014);
    CHECK(h.textureWeightCombos.count == 17);
    CHECK(h.textureWeightCombos.offset == 1015);
    CHECK(h.textureTransformCombos.count == 18);
    CHECK(h.textureTransformCombos.offset == 1016);

    CHECK(h.boundingBox.min.x == doctest::Approx(-1));
    CHECK(h.boundingBox.min.y == doctest::Approx(-2));
    CHECK(h.boundingBox.min.z == doctest::Approx(-3));
    CHECK(h.boundingBox.max.x == doctest::Approx(4));
    CHECK(h.boundingBox.max.y == doctest::Approx(5));
    CHECK(h.boundingBox.max.z == doctest::Approx(6));
    CHECK(h.boundingSphereRadius == doctest::Approx(7.5f));

    CHECK(h.collisionBox.min.x == doctest::Approx(-10));
    CHECK(h.collisionBox.max.z == doctest::Approx(60));
    CHECK(h.collisionSphereRadius == doctest::Approx(99.5f));
}

}  // namespace

TEST_CASE("parseHeader: pre-Legion flat MD20 reads every fixed field correctly") {
    auto blob = buildMd20Blob();
    auto h = husk::m2::parseHeader(blob);
    CHECK(h.magic == 0x3032444D);  // "MD20" little-endian
    CHECK_FALSE(h.chunked);
    checkSentinelHeader(h);
}

TEST_CASE("parseHeader: Legion+ chunked file resolves MD21 regardless of chunk order") {
    auto md20 = buildMd20Blob();

    std::vector<uint8_t> file;
    appendChunk(file, "SFID", {1, 2, 3, 4});  // unrelated chunk before MD21
    appendChunk(file, "MD21", md20);
    appendChunk(file, "PFID", {5, 6, 7, 8});  // unrelated chunk after MD21

    auto h = husk::m2::parseHeader(file);
    CHECK(h.chunked);
    checkSentinelHeader(h);
}

TEST_CASE("parseHeader: chunked file with no MD21 chunk throws, names what it found") {
    std::vector<uint8_t> file;
    appendChunk(file, "SFID", {1});
    appendChunk(file, "PFID", {2});

    CHECK_THROWS_WITH_AS(husk::m2::parseHeader(file), doctest::Contains("SFID"),
                          husk::m2::ParseError);
}

TEST_CASE("parseHeader: too short to hold a magic value throws") {
    std::vector<uint8_t> file = {'M', 'D'};
    CHECK_THROWS_AS(husk::m2::parseHeader(file), husk::m2::ParseError);
}

TEST_CASE("parseHeader: MD20 blob shorter than the fixed header throws") {
    std::vector<uint8_t> file(0x50, 0);  // way short of 0xD8
    std::memcpy(file.data(), "MD20", 4);
    CHECK_THROWS_AS(husk::m2::parseHeader(file), husk::m2::ParseError);
}

TEST_CASE("parseHeader: wrong magic in a flat (non-chunked) file throws") {
    std::vector<uint8_t> file(kFixedHeaderSize, 0);
    std::memcpy(file.data(), "XXXX", 4);
    CHECK_THROWS_AS(husk::m2::parseHeader(file), husk::m2::ParseError);
}

TEST_CASE("parseHeader: name array pointing past the end of the blob throws") {
    auto blob = buildMd20Blob();
    // Corrupt the name array to claim far more bytes than the blob has.
    putArray(blob, 0x008, 0xFFFFFF, static_cast<uint32_t>(kFixedHeaderSize));
    CHECK_THROWS_AS(husk::m2::parseHeader(blob), husk::m2::ParseError);
}

TEST_CASE("parseHeader: empty name (count 0) is allowed, per the wiki's 9.2.0.41462+ note") {
    auto blob = buildMd20Blob();
    putArray(blob, 0x008, 0, 0);
    auto h = husk::m2::parseHeader(blob);
    CHECK(h.name.empty());
}

TEST_CASE("expansionForVersion: matches the wiki's version table, overlaps included") {
    // Rows, transcribed from wowdev.wiki M2#Versions:
    //   256       Pre-Release
    //   256-257   Classic
    //   260-263   The Burning Crusade
    //   264       Wrath of the Lich King
    //   265-272   Cataclysm
    //   272       Mists of Pandaria / Warlords of Draenor
    //   272-274   Legion / Battle for Azeroth / Shadowlands
    // The wiki itself calls these "rough estimates" of overlapping ranges,
    // so a version matching multiple rows (e.g. 272) is the documented
    // behavior, not a bug.
    CHECK(husk::m2::expansionForVersion(256) == "Pre-Release or Classic");
    CHECK(husk::m2::expansionForVersion(257) == "Classic");
    CHECK(husk::m2::expansionForVersion(261) == "The Burning Crusade");
    CHECK(husk::m2::expansionForVersion(264) == "Wrath of the Lich King");
    CHECK(husk::m2::expansionForVersion(266) == "Cataclysm");
    CHECK(husk::m2::expansionForVersion(272) ==
          "Cataclysm or Mists of Pandaria / Warlords of Draenor or "
          "Legion / Battle for Azeroth / Shadowlands");
    CHECK(husk::m2::expansionForVersion(274) == "Legion / Battle for Azeroth / Shadowlands");
    CHECK(husk::m2::expansionForVersion(999) == "unknown");
}

TEST_CASE("extractBlob: flat MD20 file returns the file bytes verbatim") {
    auto blob = buildMd20Blob();
    auto extracted = husk::m2::extractBlob(blob);
    CHECK(extracted == blob);
}

TEST_CASE("extractBlob: Legion+ chunked file returns just the MD21 payload") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "SFID", {1, 2, 3, 4});
    appendChunk(file, "MD21", md20);
    auto extracted = husk::m2::extractBlob(file);
    CHECK(extracted == md20);
}

TEST_CASE("parseVertices: reads every field of every vertex at the right offset") {
    husk::m2::Vertex v0;
    v0.pos = {1, 2, 3};
    v0.boneWeights[0] = 10; v0.boneWeights[1] = 20; v0.boneWeights[2] = 30; v0.boneWeights[3] = 40;
    v0.boneIndices[0] = 1; v0.boneIndices[1] = 2; v0.boneIndices[2] = 3; v0.boneIndices[3] = 4;
    v0.normal = {0, 0, 1};
    v0.texCoords[0] = {0.25f, 0.5f};
    v0.texCoords[1] = {0.75f, 1.0f};

    husk::m2::Vertex v1;
    v1.pos = {-1, -2, -3};
    v1.boneWeights[0] = 255; v1.boneWeights[1] = 0; v1.boneWeights[2] = 0; v1.boneWeights[3] = 0;
    v1.boneIndices[0] = 9; v1.boneIndices[1] = 8; v1.boneIndices[2] = 7; v1.boneIndices[3] = 6;
    v1.normal = {1, 0, 0};
    v1.texCoords[0] = {0.1f, 0.2f};
    v1.texCoords[1] = {0.3f, 0.4f};

    size_t vertexOffset = 2000;
    std::vector<uint8_t> blob(vertexOffset, 0);
    putVertex(blob, vertexOffset, v0);
    putVertex(blob, vertexOffset + 0x30, v1);

    husk::m2::Array array;
    array.count = 2;
    array.offset = static_cast<uint32_t>(vertexOffset);
    auto vertices = husk::m2::parseVertices(blob, array);

    REQUIRE(vertices.size() == 2);

    CHECK(vertices[0].pos.x == doctest::Approx(1));
    CHECK(vertices[0].pos.y == doctest::Approx(2));
    CHECK(vertices[0].pos.z == doctest::Approx(3));
    CHECK(vertices[0].boneWeights[0] == 10);
    CHECK(vertices[0].boneWeights[3] == 40);
    CHECK(vertices[0].boneIndices[0] == 1);
    CHECK(vertices[0].boneIndices[3] == 4);
    CHECK(vertices[0].normal.z == doctest::Approx(1));
    CHECK(vertices[0].texCoords[0].x == doctest::Approx(0.25f));
    CHECK(vertices[0].texCoords[0].y == doctest::Approx(0.5f));
    CHECK(vertices[0].texCoords[1].x == doctest::Approx(0.75f));
    CHECK(vertices[0].texCoords[1].y == doctest::Approx(1.0f));

    CHECK(vertices[1].pos.x == doctest::Approx(-1));
    CHECK(vertices[1].boneWeights[0] == 255);
    CHECK(vertices[1].boneIndices[0] == 9);
    CHECK(vertices[1].normal.x == doctest::Approx(1));
    CHECK(vertices[1].texCoords[1].y == doctest::Approx(0.4f));
}

TEST_CASE("parseVertices: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;  // zero bytes -- offset would be out of range if ever read
    husk::m2::Array array;
    array.count = 0;
    array.offset = 12345;
    auto vertices = husk::m2::parseVertices(blob, array);
    CHECK(vertices.empty());
}

TEST_CASE("parseVertices: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(100, 0);
    husk::m2::Array array;
    array.count = 3;       // 3 * 48 = 144 bytes needed
    array.offset = 0;      // but the blob is only 100 bytes
    CHECK_THROWS_AS(husk::m2::parseVertices(blob, array), husk::m2::ParseError);
}
