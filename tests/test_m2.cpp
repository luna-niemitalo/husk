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
//
// M2CompBone (wowdev.wiki M2#Bones), >= Wrath shape (every version this
// parser targets), 88 bytes:
//   0x00 key_bone_id (int32)           0x04 flags (uint32)
//   0x08 parent_bone (int16)           0x0A submesh_id (uint16)
//   0x0C union{CompressData|boneNameCRC} (4 bytes, unread -- debug-only)
//   0x10 translation (M2Track<C3Vector>, 20 bytes)
//   0x24 rotation (M2Track<M2CompQuat>, 20 bytes)
//   0x38 scale (M2Track<C3Vector>, 20 bytes)
//   0x4C pivot (C3Vector, 12 bytes)
// -> 0x58 = 88 bytes total.
// M2Track<T> (>= Wrath) is always 20 bytes regardless of T -- both halves
// are (M2Array<M2Array<uint32_t>> timestamps, M2Array<M2Array<T>> values),
// and an M2Array is just a (count, offset) pair (8 bytes) no matter what
// it's an array of, so T's own size never affects M2Track<T>'s size:
//   M2TrackBase: 0x00 interpolation_type (u16) 0x02 global_sequence (u16)
//                0x04 timestamps (M2Array<M2Array<u32>>, 8 bytes) -> 0x0C
//   M2Track<T> adds: 0x0C values (M2Array<M2Array<T>>, 8 bytes) -> 0x14
// This parser doesn't resolve track contents (animation, stage 6) -- it
// only needs to skip over the right number of bytes to reach `pivot`.

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

// Writes one 88-byte M2CompBone record at `off`. The M2Track regions
// (translation/rotation/scale, 20 bytes each) are filled with `trackFiller`
// instead of left zeroed, so a test can prove the parser skips exactly the
// right number of bytes rather than coincidentally landing on zeros.
void putBone(std::vector<uint8_t>& buf, size_t off, int32_t keyBoneId, uint32_t flags,
             int16_t parentBone, const husk::m2::Vec3& pivot, uint8_t trackFiller = 0xEE) {
    if (buf.size() < off + 0x58) buf.resize(off + 0x58, 0);
    putU32(buf, off + 0x00, static_cast<uint32_t>(keyBoneId));
    putU32(buf, off + 0x04, flags);
    uint16_t parentBoneBits = static_cast<uint16_t>(parentBone);
    std::memcpy(buf.data() + off + 0x08, &parentBoneBits, 2);
    // 0x0A submesh_id, 0x0C union: left as zero, unread by the parser.
    for (size_t i = 0x10; i < 0x4C; ++i) buf[off + i] = trackFiller;
    putF32(buf, off + 0x4C, pivot.x);
    putF32(buf, off + 0x50, pivot.y);
    putF32(buf, off + 0x54, pivot.z);
}

// Builds an M2Track<T>'s `values` field (M2Array<M2Array<T>>, wowdev.wiki
// M2#Interpolation) at `trackOff+0x0C` from `subArrays`: subArrays[i] is
// the list of raw T value byte-blobs for animation sub-array i (an empty
// list means that sub-array's own M2Array<T> has count 0). Appends all the
// inner-array/value bytes at the current end of `buf` -- callers must
// ensure `buf` is already at least `trackOff + 0x14` bytes (the fixed
// M2Track header) before calling, same "reserve fixed slots before
// appending variable data" discipline as putTexture's filename handling
// above. `interpolation_type`/`global_sequence`/`timestamps`
// (trackOff+0x00..0x0C) are left zeroed -- husk never reads them.
void putTrackValues(std::vector<uint8_t>& buf, size_t trackOff,
                     const std::vector<std::vector<std::vector<uint8_t>>>& subArrays) {
    if (buf.size() < trackOff + 0x14) buf.resize(trackOff + 0x14, 0);

    size_t innerArraysOff = buf.size();
    buf.resize(innerArraysOff + subArrays.size() * 8, 0);

    for (size_t i = 0; i < subArrays.size(); ++i) {
        const auto& values = subArrays[i];
        if (values.empty()) {
            putArray(buf, innerArraysOff + i * 8, 0, 0);
            continue;
        }
        size_t firstValueOff = buf.size();
        for (const auto& v : values) {
            buf.insert(buf.end(), v.begin(), v.end());
        }
        putArray(buf, innerArraysOff + i * 8, static_cast<uint32_t>(values.size()),
                 static_cast<uint32_t>(firstValueOff));
    }

    putArray(buf, trackOff + 0x0C, static_cast<uint32_t>(subArrays.size()),
             static_cast<uint32_t>(innerArraysOff));
}

std::vector<uint8_t> fixed16Bytes(int16_t raw) {
    std::vector<uint8_t> b(2);
    std::memcpy(b.data(), &raw, 2);
    return b;
}

std::vector<uint8_t> vec3Bytes(const husk::m2::Vec3& v) {
    std::vector<uint8_t> b(12);
    std::memcpy(b.data() + 0, &v.x, 4);
    std::memcpy(b.data() + 4, &v.y, 4);
    std::memcpy(b.data() + 8, &v.z, 4);
    return b;
}

// M2Color (wowdev.wiki M2#Colors_and_transparency), 0x28 (40) bytes: color
// M2Track<C3Vector> at +0x00, alpha M2Track<fixed16> at +0x14.
void putColor(std::vector<uint8_t>& buf, size_t off,
              const std::vector<std::vector<std::vector<uint8_t>>>& colorSubArrays,
              const std::vector<std::vector<std::vector<uint8_t>>>& alphaSubArrays) {
    if (buf.size() < off + 0x28) buf.resize(off + 0x28, 0);
    putTrackValues(buf, off + 0x00, colorSubArrays);
    putTrackValues(buf, off + 0x14, alphaSubArrays);
}

// M2TextureWeight, 0x14 (20) bytes: weight M2Track<fixed16> at +0x00.
void putTextureWeight(std::vector<uint8_t>& buf, size_t off,
                       const std::vector<std::vector<std::vector<uint8_t>>>& weightSubArrays) {
    if (buf.size() < off + 0x14) buf.resize(off + 0x14, 0);
    putTrackValues(buf, off + 0x00, weightSubArrays);
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

TEST_CASE("parseHeader: SKID chunk, when present, is surfaced as skeletonFileId") {
    auto md20 = buildMd20Blob();

    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    uint32_t fileDataId = 0x00ABCDEFu;
    uint8_t skidPayload[4];
    std::memcpy(skidPayload, &fileDataId, 4);
    appendChunk(file, "SKID", std::vector<uint8_t>(skidPayload, skidPayload + 4));

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.skeletonFileId.has_value());
    CHECK(*h.skeletonFileId == 0x00ABCDEFu);
}

TEST_CASE("parseHeader: no SKID chunk leaves skeletonFileId empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);

    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.skeletonFileId.has_value());
}

TEST_CASE("parseHeader: flat (non-chunked) MD20 file never has skeletonFileId") {
    auto blob = buildMd20Blob();
    auto h = husk::m2::parseHeader(blob);
    CHECK_FALSE(h.skeletonFileId.has_value());
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

TEST_CASE("parseBones: reads key_bone_id/flags/parent_bone/pivot, skipping the M2Track regions") {
    size_t boneOffset = 3000;
    std::vector<uint8_t> blob(boneOffset, 0);
    putBone(blob, boneOffset, /*keyBoneId=*/5, /*flags=*/0x1234, /*parentBone=*/-1,
            husk::m2::Vec3{1, 2, 3}, /*trackFiller=*/0xEE);
    putBone(blob, boneOffset + 0x58, /*keyBoneId=*/-1, /*flags=*/0, /*parentBone=*/0,
            husk::m2::Vec3{4, 5, 6}, /*trackFiller=*/0xAA);

    husk::m2::Array array;
    array.count = 2;
    array.offset = static_cast<uint32_t>(boneOffset);
    auto bones = husk::m2::parseBones(blob, array);

    REQUIRE(bones.size() == 2);
    CHECK(bones[0].keyBoneId == 5);
    CHECK(bones[0].flags == 0x1234);
    CHECK(bones[0].parentBone == -1);
    CHECK(bones[0].pivot.x == doctest::Approx(1));
    CHECK(bones[0].pivot.y == doctest::Approx(2));
    CHECK(bones[0].pivot.z == doctest::Approx(3));

    CHECK(bones[1].keyBoneId == -1);
    CHECK(bones[1].parentBone == 0);
    CHECK(bones[1].pivot.x == doctest::Approx(4));
    CHECK(bones[1].pivot.z == doctest::Approx(6));
}

TEST_CASE("parseBones: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 54321;
    CHECK(husk::m2::parseBones(blob, array).empty());
}

TEST_CASE("parseBones: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(50, 0);
    husk::m2::Array array;
    array.count = 1;   // 88 bytes needed
    array.offset = 0;  // but the blob is only 50 bytes
    CHECK_THROWS_AS(husk::m2::parseBones(blob, array), husk::m2::ParseError);
}

// M2Texture (wowdev.wiki M2#Textures), 16 bytes: 0x00 type (u32), 0x04
// flags (u32), 0x08 filename (M2Array<char>).
void putTexture(std::vector<uint8_t>& buf, size_t off, uint32_t type, uint32_t flags,
                 const std::string& filename) {
    if (buf.size() < off + 0x10) buf.resize(off + 0x10, 0);
    putU32(buf, off + 0x00, type);
    putU32(buf, off + 0x04, flags);
    if (filename.empty()) {
        putArray(buf, off + 0x08, 0, 0);
        return;
    }
    size_t nameOff = buf.size();
    buf.resize(nameOff + filename.size());
    std::memcpy(buf.data() + nameOff, filename.data(), filename.size());
    putArray(buf, off + 0x08, static_cast<uint32_t>(filename.size()),
             static_cast<uint32_t>(nameOff));
}

// M2Material (wowdev.wiki M2#Render_flags_and_blending_modes), 4 bytes:
// 0x00 flags (u16), 0x02 blending_mode (u16).
void putMaterial(std::vector<uint8_t>& buf, size_t off, uint16_t flags, uint16_t blendMode) {
    if (buf.size() < off + 0x04) buf.resize(off + 0x04, 0);
    uint16_t f = flags, b = blendMode;
    std::memcpy(buf.data() + off + 0x00, &f, 2);
    std::memcpy(buf.data() + off + 0x02, &b, 2);
}

TEST_CASE("parseTextures: reads type/flags/filename for every entry") {
    size_t off = 500;
    // Both fixed-size records reserved up front -- putTexture appends
    // variable-length filename data at the *current* end of the buffer, so
    // writing record 0's name before record 1's fixed bytes exist would
    // otherwise let record 1 clobber it.
    std::vector<uint8_t> blob(off + 2 * 0x10, 0);
    putTexture(blob, off, /*type=*/0, /*flags=*/0x1, "Textures\\foo.blp");
    putTexture(blob, off + 0x10, /*type=*/6, /*flags=*/0, "");  // char hair, no embedded name

    husk::m2::Array array;
    array.count = 2;
    array.offset = static_cast<uint32_t>(off);
    auto textures = husk::m2::parseTextures(blob, array);

    REQUIRE(textures.size() == 2);
    CHECK(textures[0].type == 0);
    CHECK(textures[0].flags == 0x1);
    CHECK(textures[0].filename == "Textures\\foo.blp");
    CHECK(textures[1].type == 6);
    CHECK(textures[1].flags == 0);
    CHECK(textures[1].filename.empty());
}

TEST_CASE("parseTextures: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 999;
    CHECK(husk::m2::parseTextures(blob, array).empty());
}

TEST_CASE("parseTextures: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(10, 0);  // 0x10 bytes needed for one entry
    husk::m2::Array array;
    array.count = 1;
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseTextures(blob, array), husk::m2::ParseError);
}

TEST_CASE("parseMaterials: reads flags/blendMode for every entry") {
    size_t off = 300;
    std::vector<uint8_t> blob(off, 0);
    putMaterial(blob, off, /*flags=*/0x04, /*blendMode=*/0);
    putMaterial(blob, off + 0x04, /*flags=*/0x11, /*blendMode=*/4);

    husk::m2::Array array;
    array.count = 2;
    array.offset = static_cast<uint32_t>(off);
    auto materials = husk::m2::parseMaterials(blob, array);

    REQUIRE(materials.size() == 2);
    CHECK(materials[0].flags == 0x04);
    CHECK(materials[0].blendMode == 0);
    CHECK(materials[1].flags == 0x11);
    CHECK(materials[1].blendMode == 4);
}

TEST_CASE("parseMaterials: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 777;
    CHECK(husk::m2::parseMaterials(blob, array).empty());
}

TEST_CASE("parseMaterials: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(2, 0);  // 4 bytes needed for one entry
    husk::m2::Array array;
    array.count = 1;
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseMaterials(blob, array), husk::m2::ParseError);
}

TEST_CASE("parseUint16Array: reads count values at offset in order") {
    size_t off = 40;
    std::vector<uint8_t> blob(off, 0);
    putU32(blob, off + 0, 0x00090001);  // two u16s packed: 1, 9 (little-endian)
    putU32(blob, off + 4, 0x00080002);  // 2, 8

    husk::m2::Array array;
    array.count = 4;
    array.offset = static_cast<uint32_t>(off);
    auto values = husk::m2::parseUint16Array(blob, array);
    REQUIRE(values.size() == 4);
    CHECK(values[0] == 1);
    CHECK(values[1] == 9);
    CHECK(values[2] == 2);
    CHECK(values[3] == 8);
}

TEST_CASE("parseUint16Array: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 555;
    CHECK(husk::m2::parseUint16Array(blob, array).empty());
}

TEST_CASE("parseUint16Array: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(2, 0);  // 4 bytes needed for two u16 entries
    husk::m2::Array array;
    array.count = 2;
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseUint16Array(blob, array), husk::m2::ParseError);
}

TEST_CASE("parseHeader: TXID chunk, when present, is surfaced as textureFileDataIds") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    // Three FileDataIDs: two real, one 0 (a non-file-based texture type,
    // wowdev.wiki M2#TXID).
    std::vector<uint8_t> txidPayload;
    for (uint32_t id : {1034713u, 0u, 220043u}) {
        uint8_t bytes[4];
        std::memcpy(bytes, &id, 4);
        txidPayload.insert(txidPayload.end(), bytes, bytes + 4);
    }
    appendChunk(file, "TXID", txidPayload);

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.textureFileDataIds.has_value());
    REQUIRE(h.textureFileDataIds->size() == 3);
    CHECK((*h.textureFileDataIds)[0] == 1034713u);
    CHECK((*h.textureFileDataIds)[1] == 0u);
    CHECK((*h.textureFileDataIds)[2] == 220043u);
}

TEST_CASE("parseHeader: no TXID chunk leaves textureFileDataIds empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.textureFileDataIds.has_value());
}

TEST_CASE("parseHeader: flat (non-chunked) MD20 file never has textureFileDataIds") {
    auto blob = buildMd20Blob();
    auto h = husk::m2::parseHeader(blob);
    CHECK_FALSE(h.textureFileDataIds.has_value());
}

// Forward-compatibility: husk::readChunks (src/chunk.cpp) is tag-agnostic
// by construction -- it doesn't validate chunk tags against any list, so a
// brand-new chunk a future client build ships (this format adds them
// often, see README.md's Design notes) doesn't break parsing on its own.
// `Header::chunkTags` is what turns that "silently fine" default into an
// actual diagnostic opportunity for callers (see cmd_info.cpp's
// documentedM2ChunkTags/isUndocumentedChunkTag) -- these tests lock in
// both halves: chunkTags is populated correctly, and a chunk tag this
// parser has genuinely never heard of doesn't throw.
TEST_CASE("parseHeader: chunkTags lists every top-level chunk tag, in file order") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "SFID", {1, 2, 3, 4});
    appendChunk(file, "MD21", md20);
    appendChunk(file, "TXID", {5, 6, 7, 8});

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.chunkTags.size() == 3);
    CHECK(h.chunkTags[0] == "SFID");
    CHECK(h.chunkTags[1] == "MD21");
    CHECK(h.chunkTags[2] == "TXID");
}

TEST_CASE("parseHeader: flat (non-chunked) MD20 file has an empty chunkTags") {
    auto blob = buildMd20Blob();
    auto h = husk::m2::parseHeader(blob);
    CHECK(h.chunkTags.empty());
}

TEST_CASE("parseHeader: a chunk tag this parser has never heard of is tolerated, not an error") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    // "ZZZZ" is deliberately not a real wowdev.wiki-documented M2 chunk tag
    // (see cmd_info.cpp's documentedM2ChunkTags) -- stands in for whatever
    // the next client build actually adds.
    appendChunk(file, "ZZZZ", {0xDE, 0xAD, 0xBE, 0xEF});

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.chunkTags.size() == 2);
    CHECK(h.chunkTags[1] == "ZZZZ");
}

TEST_CASE("parseColors: an unambiguously constant track (one sub-array, one keyframe) resolves") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putColor(blob, off,
              /*colorSubArrays=*/{{vec3Bytes({0.5f, 0.25f, 0.75f})}},
              /*alphaSubArrays=*/{{fixed16Bytes(16384)}});  // ~0.5

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto colors = husk::m2::parseColors(blob, array);

    REQUIRE(colors.size() == 1);
    REQUIRE(colors[0].color.has_value());
    CHECK(colors[0].color->x == doctest::Approx(0.5f));
    CHECK(colors[0].color->y == doctest::Approx(0.25f));
    CHECK(colors[0].color->z == doctest::Approx(0.75f));
    REQUIRE(colors[0].alpha.has_value());
    CHECK(*colors[0].alpha == doctest::Approx(16384.0f / 32767.0f));
}

TEST_CASE(
    "parseColors: more than one animation sub-array means real per-sequence animation, not a "
    "static value -- regression test for a real bug found against bloodelffemale.m2, where "
    "naively reading element [0][0] picked up sequence 0's alpha keyframe (0, fully "
    "transparent) as if it were a sensible default and would have rendered the model invisible") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putColor(blob, off,
              /*colorSubArrays=*/{{vec3Bytes({1, 1, 1})}, {vec3Bytes({1, 1, 1})}},
              /*alphaSubArrays=*/{{fixed16Bytes(0)}, {fixed16Bytes(32767)}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto colors = husk::m2::parseColors(blob, array);

    REQUIRE(colors.size() == 1);
    CHECK_FALSE(colors[0].alpha.has_value());
}

TEST_CASE("parseColors: more than one keyframe in an otherwise-single sub-array is also treated "
          "as animated, not a static value") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putColor(blob, off,
              /*colorSubArrays=*/{{vec3Bytes({1, 1, 1})}},
              /*alphaSubArrays=*/{{fixed16Bytes(0), fixed16Bytes(32767)}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto colors = husk::m2::parseColors(blob, array);

    REQUIRE(colors.size() == 1);
    CHECK_FALSE(colors[0].alpha.has_value());
}

TEST_CASE("parseColors: a track with no sub-arrays at all has no value") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putColor(blob, off, /*colorSubArrays=*/{}, /*alphaSubArrays=*/{});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto colors = husk::m2::parseColors(blob, array);

    REQUIRE(colors.size() == 1);
    CHECK_FALSE(colors[0].color.has_value());
    CHECK_FALSE(colors[0].alpha.has_value());
}

TEST_CASE("parseColors: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 12345;
    CHECK(husk::m2::parseColors(blob, array).empty());
}

TEST_CASE("parseColors: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(10, 0);  // 0x28 bytes needed for one entry
    husk::m2::Array array;
    array.count = 1;
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseColors(blob, array), husk::m2::ParseError);
}

TEST_CASE("parseTextureWeights: an unambiguously constant track resolves") {
    size_t off = 500;
    std::vector<uint8_t> blob(off, 0);
    putTextureWeight(blob, off, {{fixed16Bytes(6553)}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto weights = husk::m2::parseTextureWeights(blob, array);

    REQUIRE(weights.size() == 1);
    REQUIRE(weights[0].weight.has_value());
    CHECK(*weights[0].weight == doctest::Approx(6553.0f / 32767.0f));
}

TEST_CASE("parseTextureWeights: an animated (multi-sub-array) track has no value") {
    size_t off = 500;
    std::vector<uint8_t> blob(off, 0);
    putTextureWeight(blob, off, {{fixed16Bytes(32767)}, {fixed16Bytes(0)}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto weights = husk::m2::parseTextureWeights(blob, array);

    REQUIRE(weights.size() == 1);
    CHECK_FALSE(weights[0].weight.has_value());
}

TEST_CASE("parseTextureWeights: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 999;
    CHECK(husk::m2::parseTextureWeights(blob, array).empty());
}

TEST_CASE("parseTextureWeights: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(5, 0);  // 0x14 bytes needed for one entry
    husk::m2::Array array;
    array.count = 1;
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseTextureWeights(blob, array), husk::m2::ParseError);
}

TEST_CASE("parseHeader: SFID chunk, when present, is surfaced as skinFileDataIds") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    std::vector<uint8_t> sfidPayload;
    for (uint32_t id : {469824u, 469830u}) {
        uint8_t bytes[4];
        std::memcpy(bytes, &id, 4);
        sfidPayload.insert(sfidPayload.end(), bytes, bytes + 4);
    }
    appendChunk(file, "SFID", sfidPayload);

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.skinFileDataIds.has_value());
    REQUIRE(h.skinFileDataIds->size() == 2);
    CHECK((*h.skinFileDataIds)[0] == 469824u);
    CHECK((*h.skinFileDataIds)[1] == 469830u);
}

TEST_CASE("parseHeader: no SFID chunk leaves skinFileDataIds empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.skinFileDataIds.has_value());
}

TEST_CASE("parseHeader: LDV1 chunk, when present, is surfaced as lodCount") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    // struct LodData { uint16 unk0; uint16 lodCount; ... } -- only the
    // first 4 bytes matter to husk.
    std::vector<uint8_t> ldv1Payload = {0x08, 0x00, 0x03, 0x00};  // unk0=8, lodCount=3
    appendChunk(file, "LDV1", ldv1Payload);

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.lodCount.has_value());
    CHECK(*h.lodCount == 3);
}

TEST_CASE("parseHeader: no LDV1 chunk leaves lodCount empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.lodCount.has_value());
}

TEST_CASE("parseHeader: BFID chunk, when present, is surfaced as boneFileDataIds") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    std::vector<uint8_t> bfidPayload;
    for (uint32_t id : {100001u, 100002u, 100003u}) {
        uint8_t bytes[4];
        std::memcpy(bytes, &id, 4);
        bfidPayload.insert(bfidPayload.end(), bytes, bytes + 4);
    }
    appendChunk(file, "BFID", bfidPayload);

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.boneFileDataIds.has_value());
    REQUIRE(h.boneFileDataIds->size() == 3);
    CHECK((*h.boneFileDataIds)[1] == 100002u);
}

TEST_CASE("parseHeader: no BFID chunk leaves boneFileDataIds empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.boneFileDataIds.has_value());
}

TEST_CASE("parseHeader: AFID chunk, when present, is surfaced as animFileIds") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    // { uint16 anim_id; uint16 sub_anim_id; uint32 file_id; }[]
    std::vector<uint8_t> afidPayload;
    auto putEntry = [&](uint16_t animId, uint16_t subAnimId, uint32_t fileId) {
        uint8_t a[2], s[2], f[4];
        std::memcpy(a, &animId, 2);
        std::memcpy(s, &subAnimId, 2);
        std::memcpy(f, &fileId, 4);
        afidPayload.insert(afidPayload.end(), a, a + 2);
        afidPayload.insert(afidPayload.end(), s, s + 2);
        afidPayload.insert(afidPayload.end(), f, f + 4);
    };
    putEntry(120, 0, 469839);
    putEntry(119, 1, 469836);
    appendChunk(file, "AFID", afidPayload);

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.animFileIds.has_value());
    REQUIRE(h.animFileIds->size() == 2);
    CHECK((*h.animFileIds)[0].animId == 120);
    CHECK((*h.animFileIds)[0].subAnimId == 0);
    CHECK((*h.animFileIds)[0].fileId == 469839u);
    CHECK((*h.animFileIds)[1].animId == 119);
    CHECK((*h.animFileIds)[1].subAnimId == 1);
    CHECK((*h.animFileIds)[1].fileId == 469836u);
}

TEST_CASE("parseHeader: no AFID chunk leaves animFileIds empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.animFileIds.has_value());
}
