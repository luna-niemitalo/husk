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
//   0x0D8 collisionIndices (M2Array)   0x0E0 collisionPositions (M2Array)
//   0x0E8 collisionFaceNormals (M2Array) 0x0F0 attachments (M2Array)
//   0x0F8 attachmentLookup (M2Array)   0x100 events (M2Array)
//   0x108 lights (M2Array)             0x110 cameras (M2Array)
//   0x118 cameraLookup (M2Array)       0x120 ribbonEmitters (M2Array)
//   0x128 particleEmitters (M2Array)
// M2Array<T> = { uint32_t count; uint32_t offset; } (8 bytes), offset
// relative to the start of this same blob.
// 0x130 is the end of the fixed portion this parser reads (304 bytes).
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

constexpr size_t kFixedHeaderSize = 0x130;

void putU32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
    if (buf.size() < off + 4) buf.resize(off + 4, 0);
    std::memcpy(buf.data() + off, &v, 4);
}

void putU16(std::vector<uint8_t>& buf, size_t off, uint16_t v) {
    if (buf.size() < off + 2) buf.resize(off + 2, 0);
    std::memcpy(buf.data() + off, &v, 2);
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

    putArray(buf, 0x0D8, 19, 1017);  // collisionIndices
    putArray(buf, 0x0E0, 20, 1018);  // collisionPositions
    putArray(buf, 0x0E8, 21, 1019);  // collisionFaceNormals
    putArray(buf, 0x0F0, 22, 1020);  // attachments
    putArray(buf, 0x0F8, 23, 1021);  // attachmentLookup
    putArray(buf, 0x100, 24, 1022);  // events
    putArray(buf, 0x108, 25, 1023);  // lights
    putArray(buf, 0x110, 26, 1024);  // cameras
    putArray(buf, 0x118, 27, 1025);  // cameraLookup
    putArray(buf, 0x120, 28, 1026);  // ribbonEmitters
    putArray(buf, 0x128, 29, 1027);  // particleEmitters

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

std::vector<uint8_t> vec2Bytes(const husk::m2::Vec2& v) {
    std::vector<uint8_t> b(8);
    std::memcpy(b.data() + 0, &v.x, 4);
    std::memcpy(b.data() + 4, &v.y, 4);
    return b;
}

std::vector<uint8_t> floatBytes(float v) {
    std::vector<uint8_t> b(4);
    std::memcpy(b.data(), &v, 4);
    return b;
}

// `size` is 1 (uint8_t) or 2 (uint16_t) -- matches resolveRawIntTrackSequence/
// resolveRawIntGlobalSequenceTrack's own `elementSize` parameter.
std::vector<uint8_t> rawIntBytes(uint32_t v, size_t size) {
    std::vector<uint8_t> b(size);
    for (size_t i = 0; i < size; ++i) b[i] = static_cast<uint8_t>(v >> (8 * i));
    return b;
}

// Raw wire bytes for one M2CompQuat: 4x int16, x/y/z/w order (see
// husk::m2::Quat's doc comment for why w comes last, and
// src/m2.cpp's readCompQuat for the decompression formula this is meant to
// exercise).
std::vector<uint8_t> quatWireBytes(int16_t x, int16_t y, int16_t z, int16_t w) {
    std::vector<uint8_t> b(8);
    std::memcpy(b.data() + 0, &x, 2);
    std::memcpy(b.data() + 2, &y, 2);
    std::memcpy(b.data() + 4, &z, 2);
    std::memcpy(b.data() + 6, &w, 2);
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

// Raw wire bytes for one C4Quaternion: 4 raw floats, x/y/z/w order (NOT
// the compressed M2CompQuat quatWireBytes above builds -- see
// husk::m2::TextureTransform's doc comment for why rotation here is this
// different, uncompressed type).
std::vector<uint8_t> quatFloatBytes(float x, float y, float z, float w) {
    std::vector<uint8_t> b(16);
    std::memcpy(b.data() + 0, &x, 4);
    std::memcpy(b.data() + 4, &y, 4);
    std::memcpy(b.data() + 8, &z, 4);
    std::memcpy(b.data() + 12, &w, 4);
    return b;
}

// M2TextureTransform (wowdev.wiki M2#Texture_Transforms), 0x3C (60) bytes:
// translation M2Track<C3Vector> at +0x00, rotation M2Track<C4Quaternion>
// at +0x14, scaling M2Track<C3Vector> at +0x28.
void putTextureTransform(std::vector<uint8_t>& buf, size_t off,
                          const std::vector<std::vector<std::vector<uint8_t>>>& translationSubArrays,
                          const std::vector<std::vector<std::vector<uint8_t>>>& rotationSubArrays,
                          const std::vector<std::vector<std::vector<uint8_t>>>& scalingSubArrays) {
    if (buf.size() < off + 0x3C) buf.resize(off + 0x3C, 0);
    putTrackValues(buf, off + 0x00, translationSubArrays);
    putTrackValues(buf, off + 0x14, rotationSubArrays);
    putTrackValues(buf, off + 0x28, scalingSubArrays);
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

    CHECK(h.collisionIndices.count == 19);
    CHECK(h.collisionIndices.offset == 1017);
    CHECK(h.collisionPositions.count == 20);
    CHECK(h.collisionPositions.offset == 1018);
    CHECK(h.collisionFaceNormals.count == 21);
    CHECK(h.collisionFaceNormals.offset == 1019);
    CHECK(h.attachments.count == 22);
    CHECK(h.attachments.offset == 1020);
    CHECK(h.attachmentLookup.count == 23);
    CHECK(h.attachmentLookup.offset == 1021);
    CHECK(h.events.count == 24);
    CHECK(h.events.offset == 1022);
    CHECK(h.lights.count == 25);
    CHECK(h.lights.offset == 1023);
    CHECK(h.cameras.count == 26);
    CHECK(h.cameras.offset == 1024);
    CHECK(h.cameraLookup.count == 27);
    CHECK(h.cameraLookup.offset == 1025);
    CHECK(h.ribbonEmitters.count == 28);
    CHECK(h.ribbonEmitters.offset == 1026);
    CHECK(h.particleEmitters.count == 29);
    CHECK(h.particleEmitters.offset == 1027);
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

TEST_CASE("parseHeader: PFID chunk, when present, is surfaced as physFileId") {
    auto md20 = buildMd20Blob();

    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    uint32_t fileDataId = 0x00123456u;
    uint8_t pfidPayload[4];
    std::memcpy(pfidPayload, &fileDataId, 4);
    appendChunk(file, "PFID", std::vector<uint8_t>(pfidPayload, pfidPayload + 4));

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.physFileId.has_value());
    CHECK(*h.physFileId == 0x00123456u);
}

TEST_CASE("parseHeader: no PFID chunk leaves physFileId empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);

    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.physFileId.has_value());
}

TEST_CASE("parseHeader: flat (non-chunked) MD20 file never has physFileId") {
    auto blob = buildMd20Blob();
    auto h = husk::m2::parseHeader(blob);
    CHECK_FALSE(h.physFileId.has_value());
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
    CHECK(bones[0].translationTrackOffset == boneOffset + 0x10);
    CHECK(bones[0].rotationTrackOffset == boneOffset + 0x24);
    CHECK(bones[0].scaleTrackOffset == boneOffset + 0x38);

    CHECK(bones[1].keyBoneId == -1);
    CHECK(bones[1].parentBone == 0);
    CHECK(bones[1].pivot.x == doctest::Approx(4));
    CHECK(bones[1].pivot.z == doctest::Approx(6));
    CHECK(bones[1].translationTrackOffset == boneOffset + 0x58 + 0x10);
    CHECK(bones[1].rotationTrackOffset == boneOffset + 0x58 + 0x24);
    CHECK(bones[1].scaleTrackOffset == boneOffset + 0x58 + 0x38);
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

// Bit values transcribed independently from wowdev.wiki M2#Bones's
// M2CompBone::flags enum, not copied from m2.hpp's BoneFlag namespace --
// same cross-check discipline as every other offset table in this file.
TEST_CASE("billboardModeName: names each of the four billboard bits, per wowdev.wiki M2#Bones") {
    CHECK(std::string(husk::m2::billboardModeName(0x8)) == "spherical");
    CHECK(std::string(husk::m2::billboardModeName(0x10)) == "cylindrical_lock_x");
    CHECK(std::string(husk::m2::billboardModeName(0x20)) == "cylindrical_lock_y");
    CHECK(std::string(husk::m2::billboardModeName(0x40)) == "cylindrical_lock_z");
}

TEST_CASE("billboardModeName: no billboard bit set returns nullptr, even with unrelated bits set") {
    CHECK(husk::m2::billboardModeName(0x0) == nullptr);
    // ignoreParentTranslate|ignoreParentScale|ignoreParentRotation (0x1|0x2|0x4) --
    // real, unrelated bone flags, not billboard bits.
    CHECK(husk::m2::billboardModeName(0x7) == nullptr);
}

TEST_CASE("billboardModeName: a bone with a billboard bit set alongside unrelated bits is still "
          "recognized") {
    CHECK(std::string(husk::m2::billboardModeName(0x1 | 0x8)) == "spherical");
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

TEST_CASE("parseVec3Array: reads count C3Vectors at offset in order") {
    size_t off = 40;
    std::vector<uint8_t> blob(off, 0);
    blob.resize(off + 24, 0);
    putF32(blob, off + 0, 1.0f);
    putF32(blob, off + 4, 2.0f);
    putF32(blob, off + 8, 3.0f);
    putF32(blob, off + 12, -1.0f);
    putF32(blob, off + 16, -2.0f);
    putF32(blob, off + 20, -3.0f);

    husk::m2::Array array;
    array.count = 2;
    array.offset = static_cast<uint32_t>(off);
    auto values = husk::m2::parseVec3Array(blob, array);
    REQUIRE(values.size() == 2);
    CHECK(values[0].x == doctest::Approx(1.0f));
    CHECK(values[0].y == doctest::Approx(2.0f));
    CHECK(values[0].z == doctest::Approx(3.0f));
    CHECK(values[1].x == doctest::Approx(-1.0f));
    CHECK(values[1].y == doctest::Approx(-2.0f));
    CHECK(values[1].z == doctest::Approx(-3.0f));
}

TEST_CASE("parseVec3Array: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 555;
    CHECK(husk::m2::parseVec3Array(blob, array).empty());
}

TEST_CASE("parseVec3Array: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(8, 0);  // 12 bytes needed for one C3Vector
    husk::m2::Array array;
    array.count = 1;
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseVec3Array(blob, array), husk::m2::ParseError);
}

TEST_CASE("parseCollisionMesh: dereferences positions/indices/faceNormals into one CollisionMesh") {
    std::vector<uint8_t> blob(100, 0);
    // 2 positions (C3Vector) at offset 0
    putF32(blob, 0, 0.0f);
    putF32(blob, 4, 0.0f);
    putF32(blob, 8, 0.0f);
    putF32(blob, 12, 1.0f);
    putF32(blob, 16, 0.0f);
    putF32(blob, 20, 0.0f);
    // 3 indices (uint16) at offset 24, forming one triangle
    putU32(blob, 24, 0x00020001);  // 1, 2 (little-endian u16 pair)
    blob[28] = 0;
    blob[29] = 0;  // 0
    // 1 face normal (C3Vector) at offset 30
    putF32(blob, 30, 0.0f);
    putF32(blob, 34, 0.0f);
    putF32(blob, 38, 1.0f);

    husk::m2::Array positions;
    positions.count = 2;
    positions.offset = 0;
    husk::m2::Array indices;
    indices.count = 3;
    indices.offset = 24;
    husk::m2::Array faceNormals;
    faceNormals.count = 1;
    faceNormals.offset = 30;

    auto mesh = husk::m2::parseCollisionMesh(blob, positions, indices, faceNormals);
    REQUIRE(mesh.positions.size() == 2);
    REQUIRE(mesh.indices.size() == 3);
    REQUIRE(mesh.faceNormals.size() == 1);
    CHECK(mesh.indices[0] == 1);
    CHECK(mesh.indices[1] == 2);
    CHECK(mesh.indices[2] == 0);
    CHECK(mesh.faceNormals[0].z == doctest::Approx(1.0f));
}

TEST_CASE("parseCollisionMesh: every array empty returns an all-empty CollisionMesh") {
    std::vector<uint8_t> blob;
    husk::m2::Array empty;
    auto mesh = husk::m2::parseCollisionMesh(blob, empty, empty, empty);
    CHECK(mesh.positions.empty());
    CHECK(mesh.indices.empty());
    CHECK(mesh.faceNormals.empty());
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

// Regression test for FAILURES2.md #8: a TXID chunk whose byte length isn't
// a multiple of 4 (one uint32 FileDataID per entry) used to silently
// truncate -- the last partial entry just vanished, with no error and no
// count mismatch a caller could notice -- rather than failing loudly like
// every other fixed-record-array parser in this codebase does for the same
// class of foreign-data mismatch (FAILURES.md #2).
TEST_CASE("parseHeader: TXID chunk with a byte length not a multiple of 4 throws, rather than "
          "silently dropping the trailing partial entry") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    appendChunk(file, "TXID", {1, 2, 3, 4, 5, 6});  // 6 bytes: 1 whole entry + 2 stray bytes
    CHECK_THROWS_WITH_AS(husk::m2::parseHeader(file), doctest::Contains("TXID"),
                          husk::m2::ParseError);
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
    // colorTrackOffset/alphaTrackOffset are always set,
    // even for an unambiguously constant track -- a caller only consults
    // them when *Animated is true, but they're set unconditionally so
    // there's no separate "did this get populated" state to track.
    CHECK(colors[0].colorTrackOffset == off + 0x00);
    CHECK(colors[0].alphaTrackOffset == off + 0x14);
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
    // weightTrackOffset, always set -- see parseColors'
    // matching colorTrackOffset/alphaTrackOffset test above.
    CHECK(weights[0].weightTrackOffset == off + 0x00);
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

TEST_CASE("parseTextureTransforms: an unambiguously constant transform (translation/rotation/"
          "scaling all one sub-array, one keyframe) resolves all three") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putTextureTransform(blob, off,
                         /*translationSubArrays=*/{{vec3Bytes({0.1f, 0.2f, 0.0f})}},
                         /*rotationSubArrays=*/{{quatFloatBytes(0, 0, 0.7071f, 0.7071f)}},
                         /*scalingSubArrays=*/{{vec3Bytes({2, 2, 1})}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto transforms = husk::m2::parseTextureTransforms(blob, array);

    REQUIRE(transforms.size() == 1);
    REQUIRE(transforms[0].translation.has_value());
    CHECK(transforms[0].translation->x == doctest::Approx(0.1f));
    CHECK(transforms[0].translation->y == doctest::Approx(0.2f));
    REQUIRE(transforms[0].rotation.has_value());
    CHECK(transforms[0].rotation->z == doctest::Approx(0.7071f));
    CHECK(transforms[0].rotation->w == doctest::Approx(0.7071f));
    REQUIRE(transforms[0].scaling.has_value());
    CHECK(transforms[0].scaling->x == doctest::Approx(2.0f));
    CHECK_FALSE(transforms[0].translationAnimated);
    CHECK_FALSE(transforms[0].rotationAnimated);
    CHECK_FALSE(transforms[0].scalingAnimated);
}

TEST_CASE("parseTextureTransforms: a genuinely animated (multi-sub-array) rotation track has no "
          "value and is flagged animated, independent of the other two (still-constant) tracks") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putTextureTransform(blob, off,
                         /*translationSubArrays=*/{{vec3Bytes({0, 0, 0})}},
                         /*rotationSubArrays=*/
                         {{quatFloatBytes(0, 0, 0, 1)}, {quatFloatBytes(0, 0, 1, 0)}},
                         /*scalingSubArrays=*/{{vec3Bytes({1, 1, 1})}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto transforms = husk::m2::parseTextureTransforms(blob, array);

    REQUIRE(transforms.size() == 1);
    CHECK(transforms[0].translation.has_value());
    CHECK_FALSE(transforms[0].rotation.has_value());
    CHECK(transforms[0].scaling.has_value());
    CHECK_FALSE(transforms[0].translationAnimated);
    CHECK(transforms[0].rotationAnimated);
    CHECK_FALSE(transforms[0].scalingAnimated);
}

TEST_CASE("parseTextureTransforms: a track with no sub-arrays at all has no value and is not "
          "flagged animated (genuinely empty, not dropped animation)") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putTextureTransform(blob, off, {}, {}, {});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto transforms = husk::m2::parseTextureTransforms(blob, array);

    REQUIRE(transforms.size() == 1);
    CHECK_FALSE(transforms[0].translation.has_value());
    CHECK_FALSE(transforms[0].translationAnimated);
    CHECK_FALSE(transforms[0].rotation.has_value());
    CHECK_FALSE(transforms[0].rotationAnimated);
    CHECK_FALSE(transforms[0].scaling.has_value());
    CHECK_FALSE(transforms[0].scalingAnimated);
}

TEST_CASE("parseTextureTransforms: empty array returns an empty vector without touching the "
          "blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 54321;
    CHECK(husk::m2::parseTextureTransforms(blob, array).empty());
}

TEST_CASE("parseTextureTransforms: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(10, 0);  // 0x3C bytes needed for one entry
    husk::m2::Array array;
    array.count = 1;
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseTextureTransforms(blob, array), husk::m2::ParseError);
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

// Regression test for FAILURES2.md #8 (AFID's 8-byte-record version of the
// TXID test above).
TEST_CASE("parseHeader: AFID chunk with a byte length not a multiple of 8 throws, rather than "
          "silently dropping the trailing partial entry") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    // 10 bytes: 1 whole {anim_id, sub_anim_id, file_id} entry (8 bytes) + 2
    // stray bytes.
    appendChunk(file, "AFID", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
    CHECK_THROWS_WITH_AS(husk::m2::parseHeader(file), doctest::Contains("AFID"),
                          husk::m2::ParseError);
}

// M2Sequence (wowdev.wiki M2#Animation_sequences), 0x40 bytes: 0x00 id
// (u16), 0x02 variationIndex (u16), 0x04 duration (u32), 0x0C flags (u32)
// -- movespeed/frequency/replay/blendTime/bounds/variationNext/aliasNext
// (M2_GAPS_TODO.md's former Item 1) left zeroed here since this helper is
// only used for the 4-field happy-path/throw tests below; putSequenceFull
// (below) covers every field. The 0x40 stride itself (not just these 4
// fields) is the part worth getting right -- see Sequence's doc comment in
// m2.hpp for the real-data story of why it's 64 bytes, not the 36 a naive
// reading of the wiki struct listing suggests.
void putSequence(std::vector<uint8_t>& buf, size_t off, uint16_t id, uint16_t variationIndex,
                  uint32_t duration, uint32_t flags) {
    if (buf.size() < off + 0x40) buf.resize(off + 0x40, 0);
    putU16(buf, off + 0x00, id);
    putU16(buf, off + 0x02, variationIndex);
    putU32(buf, off + 0x04, duration);
    putU32(buf, off + 0x0C, flags);
}

TEST_CASE("parseSequences: reads id/variationIndex/duration/flags for every entry") {
    std::vector<uint8_t> blob(200, 0);
    putSequence(blob, 200, 100, 0, 5000, 0x20);
    putSequence(blob, 200 + 0x40, 101, 1, 3200, 0);

    husk::m2::Array array;
    array.count = 2;
    array.offset = 200;
    auto sequences = husk::m2::parseSequences(blob, array);

    REQUIRE(sequences.size() == 2);
    CHECK(sequences[0].id == 100);
    CHECK(sequences[0].variationIndex == 0);
    CHECK(sequences[0].duration == 5000);
    CHECK(sequences[0].flags == 0x20);
    CHECK(sequences[1].id == 101);
    CHECK(sequences[1].variationIndex == 1);
    CHECK(sequences[1].duration == 3200);
    CHECK(sequences[1].flags == 0);
}

// Writes every field of one M2Sequence record (M2_GAPS_TODO.md's former
// Item 1), at the exact offsets m2.hpp's Sequence doc comment gives:
// /*0x08*/ movespeed, /*0x10*/ frequency, /*0x14*/ replay (2x u32),
// /*0x1C*/ blendTimeIn/Out, /*0x20*/ bounds (2x Vec3), /*0x38*/
// boundsRadius, /*0x3C*/ variationNext, /*0x3E*/ aliasNext.
void putSequenceFull(std::vector<uint8_t>& buf, size_t off, uint16_t id, uint16_t variationIndex,
                      uint32_t duration, uint32_t flags, float movespeed, int16_t frequency,
                      uint32_t replayMin, uint32_t replayMax, uint16_t blendTimeIn,
                      uint16_t blendTimeOut, float boundsRadius, int16_t variationNext,
                      uint16_t aliasNext) {
    if (buf.size() < off + 0x40) buf.resize(off + 0x40, 0);
    putU16(buf, off + 0x00, id);
    putU16(buf, off + 0x02, variationIndex);
    putU32(buf, off + 0x04, duration);
    std::memcpy(buf.data() + off + 0x08, &movespeed, 4);
    putU32(buf, off + 0x0C, flags);
    std::memcpy(buf.data() + off + 0x10, &frequency, 2);
    putU32(buf, off + 0x14, replayMin);
    putU32(buf, off + 0x18, replayMax);
    putU16(buf, off + 0x1C, blendTimeIn);
    putU16(buf, off + 0x1E, blendTimeOut);
    float boundsVals[6] = {1, 2, 3, 4, 5, 6};  // min (1,2,3), max (4,5,6)
    std::memcpy(buf.data() + off + 0x20, boundsVals, sizeof(boundsVals));
    std::memcpy(buf.data() + off + 0x38, &boundsRadius, 4);
    std::memcpy(buf.data() + off + 0x3C, &variationNext, 2);
    putU16(buf, off + 0x3E, aliasNext);
}

TEST_CASE("parseSequences: reads every M2_GAPS_TODO.md-former-Item-1 field at its real offset") {
    std::vector<uint8_t> blob(200, 0);
    putSequenceFull(blob, 200, 100, 0, 5000, 0x20, 4.5f, -3, 10, 20, 30, 40, 7.5f, -1, 0);

    husk::m2::Array array;
    array.count = 1;
    array.offset = 200;
    auto sequences = husk::m2::parseSequences(blob, array);

    REQUIRE(sequences.size() == 1);
    const auto& s = sequences[0];
    CHECK(s.movespeed == doctest::Approx(4.5f));
    CHECK(s.frequency == -3);
    CHECK(s.replay.minimum == 10);
    CHECK(s.replay.maximum == 20);
    CHECK(s.blendTimeIn == 30);
    CHECK(s.blendTimeOut == 40);
    CHECK(s.bounds.min.x == doctest::Approx(1));
    CHECK(s.bounds.min.y == doctest::Approx(2));
    CHECK(s.bounds.min.z == doctest::Approx(3));
    CHECK(s.bounds.max.x == doctest::Approx(4));
    CHECK(s.bounds.max.y == doctest::Approx(5));
    CHECK(s.bounds.max.z == doctest::Approx(6));
    CHECK(s.boundsRadius == doctest::Approx(7.5f));
    CHECK(s.variationNext == -1);
    CHECK(s.aliasNext == 0);
}

TEST_CASE("parseSequences: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 54321;
    CHECK(husk::m2::parseSequences(blob, array).empty());
}

TEST_CASE("parseSequences: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(20, 0);
    husk::m2::Array array;
    array.count = 1;   // 0x40 bytes needed
    array.offset = 0;  // but the blob is only 20 bytes
    CHECK_THROWS_AS(husk::m2::parseSequences(blob, array), husk::m2::ParseError);
}

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

// Builds a *full* M2Track<T> at `trackOff` -- both the `timestamps`
// (trackOff+0x04) and `values` (trackOff+0x0C) nested M2Array<M2Array<>>
// fields, in lockstep -- from per-sequence keyframe lists: sequences[i] is
// the (timestamp ms, raw value bytes) list for animation sub-array i (empty
// means that sequence's own M2Arrays both have count 0). Unlike
// putTrackValues (above, used for the parseColors/parseTextureWeights
// "unambiguously constant" tests, which never read timestamps at all),
// this is for resolveVec3TrackSequence/resolveQuatTrackSequence, which do.
void putFullTrack(std::vector<uint8_t>& buf, size_t trackOff,
                   const std::vector<std::vector<std::pair<uint32_t, std::vector<uint8_t>>>>& sequences) {
    if (buf.size() < trackOff + 0x14) buf.resize(trackOff + 0x14, 0);
    // M2TrackBase's own header (wowdev.wiki "Standard animation block"):
    // interpolation_type=1 (linear, the ordinary case) and
    // global_sequence=0xFFFF ("none" -- see m2::TrackMeta::kNoGlobalSequence).
    // A real per-sequence-indexed track (what every fixture below models)
    // always has the latter -- leaving this zero-filled would make it look
    // like a global-sequence track instead, which resolveVec3TrackSequence/
    // resolveQuatTrackSequence now (correctly) refuse to resolve by
    // sequence index at all.
    putU16(buf, trackOff + 0x00, 1);
    putU16(buf, trackOff + 0x02, 0xFFFF);

    size_t tsOuterOff = buf.size();
    buf.resize(tsOuterOff + sequences.size() * 8, 0);
    size_t valOuterOff = buf.size();
    buf.resize(valOuterOff + sequences.size() * 8, 0);

    for (size_t i = 0; i < sequences.size(); ++i) {
        const auto& kfs = sequences[i];
        if (kfs.empty()) {
            putArray(buf, tsOuterOff + i * 8, 0, 0);
            putArray(buf, valOuterOff + i * 8, 0, 0);
            continue;
        }
        size_t tsOff = buf.size();
        for (const auto& kf : kfs) putU32(buf, buf.size(), kf.first);
        size_t valOff = buf.size();
        for (const auto& kf : kfs) buf.insert(buf.end(), kf.second.begin(), kf.second.end());

        putArray(buf, tsOuterOff + i * 8, static_cast<uint32_t>(kfs.size()),
                 static_cast<uint32_t>(tsOff));
        putArray(buf, valOuterOff + i * 8, static_cast<uint32_t>(kfs.size()),
                 static_cast<uint32_t>(valOff));
    }

    putArray(buf, trackOff + 0x04, static_cast<uint32_t>(sequences.size()),
             static_cast<uint32_t>(tsOuterOff));
    putArray(buf, trackOff + 0x0C, static_cast<uint32_t>(sequences.size()),
             static_cast<uint32_t>(valOuterOff));
}

TEST_CASE("resolveVec3TrackSequence: reads timestamp/value keyframe pairs for one sequence index") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    // Sequence 0: 2 keyframes. Sequence 1: 1 keyframe.
    putFullTrack(blob, trackOff,
                 {
                     {{0, vec3Bytes({1, 2, 3})}, {1000, vec3Bytes({4, 5, 6})}},
                     {{500, vec3Bytes({7, 8, 9})}},
                 });

    auto seq0 = husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0);
    REQUIRE(seq0.size() == 2);
    CHECK(seq0[0].first == 0);
    CHECK(seq0[0].second.x == doctest::Approx(1));
    CHECK(seq0[0].second.z == doctest::Approx(3));
    CHECK(seq0[1].first == 1000);
    CHECK(seq0[1].second.y == doctest::Approx(5));

    auto seq1 = husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 1);
    REQUIRE(seq1.size() == 1);
    CHECK(seq1[0].first == 500);
    CHECK(seq1[0].second.x == doctest::Approx(7));
}

TEST_CASE("resolveVec3TrackSequence: a sequence index with no inline data (count 0) is empty, not "
          "an error") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    // Sequence 0 has real data; sequence 1 has none inline (e.g. its real
    // data lives in an external .anim file husk doesn't parse).
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 1, 1})}}, {}});

    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 1).empty());
}

TEST_CASE("readTrackMeta: reads interpolation_type/global_sequence, per wowdev.wiki's "
          "M2TrackBase") {
    std::vector<uint8_t> blob(20, 0);
    uint16_t interp = 3;
    uint16_t globalSeq = 42;
    std::memcpy(blob.data() + 0x00, &interp, 2);
    std::memcpy(blob.data() + 0x02, &globalSeq, 2);

    auto meta = husk::m2::readTrackMeta(blob, 0);
    CHECK(meta.interpolationType == 3);
    CHECK(meta.globalSequence == 42);
}

TEST_CASE("resolveVec3TrackSequence: a track with a real global_sequence (not the 0xFFFF \"none\" "
          "sentinel) resolves to empty for every sequence index -- it must not be silently "
          "attributed to whichever M2Sequence occupies outer-array position 0") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 2, 3})}}});
    // Override putFullTrack's default "none" sentinel with a real global
    // sequence index (7) -- same shape a genuine glow-pulse/idle-loop track
    // would have, per wowdev.wiki's "Global Sequences" section.
    uint16_t globalSeq = 7;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0).empty());
    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 1).empty());
}

TEST_CASE("resolveQuatTrackSequence: a track with a real global_sequence also resolves to empty, "
          "not misattributed") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, quatWireBytes(0, 32767, static_cast<int16_t>(65535u), 0)}}});
    uint16_t globalSeq = 3;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    CHECK(husk::m2::resolveQuatTrackSequence(blob, static_cast<uint32_t>(trackOff), 0).empty());
}

// resolveVec3GlobalSequenceTrack/resolveQuatGlobalSequenceTrack (FAILURES2.md
// #7): the real-data counterpart to the two "resolves to empty, not
// misattributed" tests just above -- resolveVec3TrackSequence/
// resolveQuatTrackSequence correctly refuse to resolve a global-sequence
// track *by M2Sequence index*, but that means such a track needs its *own*
// resolution path to ever produce real keyframes at all. `putFullTrack`'s
// single sub-array (index 0) is exactly the shape wowdev.wiki describes for
// a global-sequence track ("blocks that use global sequences also only have
// one track").
TEST_CASE("resolveVec3GlobalSequenceTrack: reads real keyframes for a global-sequence-driven "
          "track") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff,
                 {{{0, vec3Bytes({1, 2, 3})}, {1000, vec3Bytes({4, 5, 6})}}});
    uint16_t globalSeq = 7;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    auto keyframes = husk::m2::resolveVec3GlobalSequenceTrack(blob, static_cast<uint32_t>(trackOff));
    REQUIRE(keyframes.size() == 2);
    CHECK(keyframes[0].first == 0);
    CHECK(keyframes[0].second.x == doctest::Approx(1));
    CHECK(keyframes[1].first == 1000);
    CHECK(keyframes[1].second.y == doctest::Approx(5));
}

TEST_CASE("resolveVec3GlobalSequenceTrack: a track that's NOT global-sequence-driven (the 0xFFFF "
          "\"none\" sentinel) resolves to empty") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    // putFullTrack's own default is "none" -- no override needed.
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 2, 3})}}});

    CHECK(husk::m2::resolveVec3GlobalSequenceTrack(blob, static_cast<uint32_t>(trackOff)).empty());
}

TEST_CASE("resolveQuatGlobalSequenceTrack: reads real keyframes for a global-sequence-driven "
          "track") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    // Identity quaternion (0,0,0,1) is wire value (32767,32767,32767,65535)
    // -- see husk::m2::Quat's doc comment.
    putFullTrack(blob, trackOff,
                 {{{0, quatWireBytes(32767, 32767, 32767, static_cast<int16_t>(65535u))}}});
    uint16_t globalSeq = 3;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    auto keyframes = husk::m2::resolveQuatGlobalSequenceTrack(blob, static_cast<uint32_t>(trackOff));
    REQUIRE(keyframes.size() == 1);
    CHECK(keyframes[0].first == 0);
    CHECK(keyframes[0].second.w == doctest::Approx(1));
}

TEST_CASE("resolveVec3GlobalSequenceTrack: interpolation_type 2 or 3 throws, same as "
          "resolveVec3TrackSequence") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 2, 3})}}});
    uint16_t globalSeq = 1;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);
    uint16_t interp = 2;
    std::memcpy(blob.data() + trackOff + 0x00, &interp, 2);

    CHECK_THROWS_AS(husk::m2::resolveVec3GlobalSequenceTrack(blob, static_cast<uint32_t>(trackOff)),
                     husk::m2::ParseError);
}

TEST_CASE("resolveFloatTrackSequence: reads timestamp/value keyframe pairs for one sequence index") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff,
                 {
                     {{0, floatBytes(0.5f)}, {1000, floatBytes(1.5f)}},
                     {{500, floatBytes(2.5f)}},
                 });

    auto seq0 = husk::m2::resolveFloatTrackSequence(blob, static_cast<uint32_t>(trackOff), 0);
    REQUIRE(seq0.size() == 2);
    CHECK(seq0[0].first == 0);
    CHECK(seq0[0].second == doctest::Approx(0.5f));
    CHECK(seq0[1].first == 1000);
    CHECK(seq0[1].second == doctest::Approx(1.5f));

    auto seq1 = husk::m2::resolveFloatTrackSequence(blob, static_cast<uint32_t>(trackOff), 1);
    REQUIRE(seq1.size() == 1);
    CHECK(seq1[0].second == doctest::Approx(2.5f));
}

TEST_CASE("resolveFloatTrackSequence: a track with a real global_sequence resolves to empty for "
          "every sequence index, same non-misattribution guarantee as resolveVec3TrackSequence") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, floatBytes(1.0f)}}});
    uint16_t globalSeq = 5;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    CHECK(husk::m2::resolveFloatTrackSequence(blob, static_cast<uint32_t>(trackOff), 0).empty());
}

TEST_CASE("resolveFloatGlobalSequenceTrack: reads real keyframes for a global-sequence-driven "
          "track") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, floatBytes(3.25f)}, {200, floatBytes(4.5f)}}});
    uint16_t globalSeq = 2;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    auto kfs = husk::m2::resolveFloatGlobalSequenceTrack(blob, static_cast<uint32_t>(trackOff));
    REQUIRE(kfs.size() == 2);
    CHECK(kfs[0].second == doctest::Approx(3.25f));
    CHECK(kfs[1].first == 200);
    CHECK(kfs[1].second == doctest::Approx(4.5f));
}

TEST_CASE("resolveRawIntTrackSequence: reads raw uint8_t and uint16_t keyframe values, zero-"
          "extended, for one sequence index") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff16 = 100;
    putFullTrack(blob, trackOff16, {{{0, rawIntBytes(0xBEEFu, 2)}, {10, rawIntBytes(1, 2)}}});
    auto kfs16 = husk::m2::resolveRawIntTrackSequence(blob, static_cast<uint32_t>(trackOff16), 0, 2);
    REQUIRE(kfs16.size() == 2);
    CHECK(kfs16[0].second == 0xBEEFu);
    CHECK(kfs16[1].second == 1u);

    size_t trackOff8 = 200;
    putFullTrack(blob, trackOff8, {{{0, rawIntBytes(0xAB, 1)}}});
    auto kfs8 = husk::m2::resolveRawIntTrackSequence(blob, static_cast<uint32_t>(trackOff8), 0, 1);
    REQUIRE(kfs8.size() == 1);
    CHECK(kfs8[0].second == 0xABu);
}

TEST_CASE("resolveRawIntTrackSequence: an unsupported element size throws") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, rawIntBytes(1, 2)}}});
    CHECK_THROWS_AS(husk::m2::resolveRawIntTrackSequence(blob, static_cast<uint32_t>(trackOff), 0, 4),
                     husk::m2::ParseError);
}

// parseColors' stored colorTrackOffset/
// alphaTrackOffset are real, directly-usable M2Track byte offsets --
// exactly what cmd_export.cpp's resolveAnimatedColorCurve/
// resolveAnimatedFixed16Curve feed straight into resolveVec3TrackSequence/
// resolveRawIntTrackSequence. Builds a full M2Color record's worth of real
// per-sequence keyframe data (both tracks genuinely animated, 2 sub-arrays
// each) and confirms the round trip through parseColors -> stored offset ->
// resolver reproduces the exact keyframes, for both sequence indices.
TEST_CASE("parseColors: colorTrackOffset/alphaTrackOffset resolve real per-sequence keyframes via "
          "resolveVec3TrackSequence/resolveRawIntTrackSequence") {
    size_t off = 1000;
    // Slack past the M2Color record's own 0x28 bytes so the first track's
    // own appended inner-array data (which putFullTrack places starting at
    // the buffer's current end) never grows back into the second track's
    // still-unwritten header/outer-array region at off+0x14 -- both tracks
    // share one contiguous record, unlike putFullTrack's other test cases
    // above, which each use a fresh, far-apart trackOff.
    std::vector<uint8_t> blob(off + 0x28 + 256, 0);
    putFullTrack(blob, off + 0x00,
                 {{{0, vec3Bytes({1, 0, 0})}}, {{500, vec3Bytes({0, 1, 0})}}});
    putFullTrack(blob, off + 0x14,
                 {{{0, fixed16Bytes(32767)}}, {{500, fixed16Bytes(0)}}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto colors = husk::m2::parseColors(blob, array);

    REQUIRE(colors.size() == 1);
    CHECK(colors[0].colorAnimated);
    CHECK(colors[0].alphaAnimated);
    CHECK(colors[0].colorTrackOffset == off + 0x00);
    CHECK(colors[0].alphaTrackOffset == off + 0x14);

    auto colorSeq0 = husk::m2::resolveVec3TrackSequence(blob, colors[0].colorTrackOffset, 0);
    REQUIRE(colorSeq0.size() == 1);
    CHECK(colorSeq0[0].first == 0);
    CHECK(colorSeq0[0].second.x == doctest::Approx(1));

    auto colorSeq1 = husk::m2::resolveVec3TrackSequence(blob, colors[0].colorTrackOffset, 1);
    REQUIRE(colorSeq1.size() == 1);
    CHECK(colorSeq1[0].first == 500);
    CHECK(colorSeq1[0].second.y == doctest::Approx(1));

    auto alphaSeq0 =
        husk::m2::resolveRawIntTrackSequence(blob, colors[0].alphaTrackOffset, 0, 2);
    REQUIRE(alphaSeq0.size() == 1);
    CHECK(alphaSeq0[0].second == 32767u);

    auto alphaSeq1 =
        husk::m2::resolveRawIntTrackSequence(blob, colors[0].alphaTrackOffset, 1, 2);
    REQUIRE(alphaSeq1.size() == 1);
    CHECK(alphaSeq1[0].second == 0u);
}

TEST_CASE("resolveRawIntGlobalSequenceTrack: reads real keyframes for a global-sequence-driven "
          "track") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, rawIntBytes(7, 1)}}});
    uint16_t globalSeq = 9;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    auto kfs = husk::m2::resolveRawIntGlobalSequenceTrack(blob, static_cast<uint32_t>(trackOff), 1);
    REQUIRE(kfs.size() == 1);
    CHECK(kfs[0].second == 7u);
}

// Builds one FBlock (wowdev.wiki's "Fake-AnimationBlock": flat
// {nTimestamps, ofsTimestamps, nKeys, ofsKeys}, no per-sequence outer/inner
// indirection -- see husk::m2::FBlockMeta's doc comment) from parallel
// (uint16_t timestamp, raw value bytes) pairs.
void putFBlock(std::vector<uint8_t>& buf, size_t blockOff,
               const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& keyframes) {
    if (buf.size() < blockOff + 0x10) buf.resize(blockOff + 0x10, 0);
    size_t tsOff = buf.size();
    for (const auto& kf : keyframes) putU16(buf, buf.size(), kf.first);
    size_t valOff = buf.size();
    for (const auto& kf : keyframes) buf.insert(buf.end(), kf.second.begin(), kf.second.end());

    putArray(buf, blockOff + 0x00, static_cast<uint32_t>(keyframes.size()), static_cast<uint32_t>(tsOff));
    putArray(buf, blockOff + 0x08, static_cast<uint32_t>(keyframes.size()),
             static_cast<uint32_t>(valOff));
}

TEST_CASE("readFBlockMeta: reads the flat nTimestamps/ofsTimestamps/nKeys/ofsKeys header") {
    std::vector<uint8_t> blob(100, 0);
    putFBlock(blob, 50, {{0, floatBytes(1.0f)}, {32767, floatBytes(2.0f)}});

    auto meta = husk::m2::readFBlockMeta(blob, 50);
    CHECK(meta.timestamps.count == 2);
    CHECK(meta.keys.count == 2);
}

TEST_CASE("resolveFBlockVec3: reads timestamp/value keyframe pairs directly, no sequence "
          "indirection") {
    std::vector<uint8_t> blob(100, 0);
    putFBlock(blob, 50, {{0, vec3Bytes({255, 119, 0})}, {32767, vec3Bytes({151, 11, 11})}});

    auto kfs = husk::m2::resolveFBlockVec3(blob, 50);
    REQUIRE(kfs.size() == 2);
    CHECK(kfs[0].first == 0);
    CHECK(kfs[0].second.x == doctest::Approx(255));
    CHECK(kfs[1].first == 32767);
    CHECK(kfs[1].second.z == doctest::Approx(11));
}

TEST_CASE("resolveFBlockVec2: reads C2Vector keyframes") {
    std::vector<uint8_t> blob(100, 0);
    putFBlock(blob, 50, {{0, vec2Bytes({0.1f, 0.2f})}});

    auto kfs = husk::m2::resolveFBlockVec2(blob, 50);
    REQUIRE(kfs.size() == 1);
    CHECK(kfs[0].second.x == doctest::Approx(0.1f));
    CHECK(kfs[0].second.y == doctest::Approx(0.2f));
}

TEST_CASE("resolveFBlockFixed16: decodes fixed16 keyframes to 0.0..1.0 floats, same scale as "
          "readFixed16TrackValue") {
    std::vector<uint8_t> blob(100, 0);
    putFBlock(blob, 50, {{0, fixed16Bytes(0)}, {16000, fixed16Bytes(32767)}});

    auto kfs = husk::m2::resolveFBlockFixed16(blob, 50);
    REQUIRE(kfs.size() == 2);
    CHECK(kfs[0].second == doctest::Approx(0.0f));
    CHECK(kfs[1].second == doctest::Approx(1.0f));
}

TEST_CASE("resolveFBlockUint16: reads raw uint16_t keyframes (flipbook cell indices)") {
    std::vector<uint8_t> blob(100, 0);
    putFBlock(blob, 50, {{0, rawIntBytes(3, 2)}, {100, rawIntBytes(7, 2)}});

    auto kfs = husk::m2::resolveFBlockUint16(blob, 50);
    REQUIRE(kfs.size() == 2);
    CHECK(kfs[0].second == 3);
    CHECK(kfs[1].second == 7);
}

TEST_CASE("resolveFBlockVec3: empty (zero keys) returns an empty vector, not an error") {
    std::vector<uint8_t> blob(100, 0);
    putFBlock(blob, 50, {});
    CHECK(husk::m2::resolveFBlockVec3(blob, 50).empty());
}

TEST_CASE("resolveFBlockVec3: claimed key range past the end of the blob throws") {
    std::vector<uint8_t> blob(20, 0);
    // Claims 5 C3Vector keys (60 bytes) at offset 0, but the blob is only 20
    // bytes -- same "trust the claimed count, bounds-check before reading"
    // discipline as checkInnerArrayFits.
    putArray(blob, 0, 5, 0);
    putArray(blob, 8, 5, 0);
    CHECK_THROWS_AS(husk::m2::resolveFBlockVec3(blob, 0), husk::m2::ParseError);
}

TEST_CASE("parseGlobalLoops: reads count M2Loop (bare uint32 timestamp) records in order") {
    std::vector<uint8_t> blob(100, 0);
    size_t off = 40;
    putU32(blob, off + 0, 2000);
    putU32(blob, off + 4, 5000);

    husk::m2::Array array;
    array.count = 2;
    array.offset = static_cast<uint32_t>(off);
    auto loops = husk::m2::parseGlobalLoops(blob, array);
    REQUIRE(loops.size() == 2);
    CHECK(loops[0] == 2000);
    CHECK(loops[1] == 5000);
}

TEST_CASE("parseGlobalLoops: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(10, 0);
    husk::m2::Array array;
    array.count = 5;  // 20 bytes needed
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseGlobalLoops(blob, array), husk::m2::ParseError);
}

TEST_CASE("resolveVec3TrackSequence: interpolation_type 2 or 3 (bezier/hermite, M2SplineKey-only "
          "per wowdev.wiki) throws rather than silently misreading the values array at the wrong "
          "stride") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 2, 3})}}});

    uint16_t bezier = 2;
    std::memcpy(blob.data() + trackOff + 0x00, &bezier, 2);
    CHECK_THROWS_AS(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0),
                     husk::m2::ParseError);

    uint16_t hermite = 3;
    std::memcpy(blob.data() + trackOff + 0x00, &hermite, 2);
    CHECK_THROWS_AS(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0),
                     husk::m2::ParseError);
}

TEST_CASE("resolveVec3TrackSequence: interpolation_type 0 (step) or 1 (linear) are both accepted, "
          "the only two values a plain M2Track<T> should ever carry") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 2, 3})}}});

    uint16_t step = 0;
    std::memcpy(blob.data() + trackOff + 0x00, &step, 2);
    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0).size() == 1);

    uint16_t linear = 1;
    std::memcpy(blob.data() + trackOff + 0x00, &linear, 2);
    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0).size() == 1);
}

TEST_CASE("resolveVec3TrackSequence: a sequence index past the outer array's own count is empty, "
          "not an error") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 1, 1})}}});

    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 5).empty());
}

TEST_CASE("resolveVec3TrackSequence: no sub-arrays at all is empty, not an error") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {});
    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0).empty());
}

TEST_CASE("resolveQuatTrackSequence: decompresses the wiki's own worked identity-quaternion "
          "example (32767,32767,32767,65535) -> (0,0,0,1)") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    // 65535 as a wire uint16 is -1 when reinterpreted as int16 -- that's
    // the actual on-disk encoding wowdev.wiki's example uses.
    putFullTrack(blob, trackOff,
                 {{{0, quatWireBytes(32767, 32767, 32767, static_cast<int16_t>(65535u))}}});

    auto seq0 = husk::m2::resolveQuatTrackSequence(blob, static_cast<uint32_t>(trackOff), 0);
    REQUIRE(seq0.size() == 1);
    CHECK(seq0[0].first == 0);
    CHECK(seq0[0].second.x == doctest::Approx(0.0f));
    CHECK(seq0[0].second.y == doctest::Approx(0.0f));
    CHECK(seq0[0].second.z == doctest::Approx(0.0f));
    CHECK(seq0[0].second.w == doctest::Approx(1.0f));
}

TEST_CASE("resolveQuatTrackSequence: decompresses a non-identity quaternion, proving per-component "
          "decode and x/y/z/w wire order") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    // Per src/m2.cpp's readCompQuat: decode(raw) = (raw<0 ? raw+32768 :
    // raw-32767) / 32767.0. Wire x=0 -> (0-32767)/32767 = -1.0; wire
    // y=32767 -> (32767-32767)/32767 = 0.0; wire z=65535 (-1 signed) ->
    // (-1+32768)/32767 = 1.0; wire w=0 -> same as x, -1.0. Four different
    // wire values, four different decoded results -- proves each component
    // is read from its own wire slot in x/y/z/w order, not e.g. all reading
    // the same offset.
    putFullTrack(blob, trackOff, {{{0, quatWireBytes(0, 32767, static_cast<int16_t>(65535u), 0)}}});

    auto seq0 = husk::m2::resolveQuatTrackSequence(blob, static_cast<uint32_t>(trackOff), 0);
    REQUIRE(seq0.size() == 1);
    CHECK(seq0[0].second.x == doctest::Approx(-1.0f));
    CHECK(seq0[0].second.y == doctest::Approx(0.0f));
    CHECK(seq0[0].second.z == doctest::Approx(1.0f));
    CHECK(seq0[0].second.w == doctest::Approx(-1.0f));
}

TEST_CASE("resolveVec3TrackSequence: keyframe data running past the end of the blob throws") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    if (blob.size() < trackOff + 0x14) blob.resize(trackOff + 0x14, 0);
    putU16(blob, trackOff + 0x02, 0xFFFF);  // global_sequence: none (see putFullTrack)
    // Outer arrays both claim 1 sub-array; that sub-array claims 1000
    // keyframes at an offset that doesn't remotely fit in a 100-byte blob.
    size_t outerOff = blob.size();
    blob.resize(outerOff + 8, 0);
    putArray(blob, outerOff, 1000, 0);
    putArray(blob, trackOff + 0x04, 1, static_cast<uint32_t>(outerOff));
    putArray(blob, trackOff + 0x0C, 1, static_cast<uint32_t>(outerOff));

    CHECK_THROWS_AS(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0),
                     husk::m2::ParseError);
}

TEST_CASE("resolveVec3TrackSequence: an externalDataBlob reads keyframe payload from that blob, "
          "descriptors still from the M2 blob") {
    // The M2 blob holds the track's descriptors (outer + one inner
    // M2Array) exactly as usual -- but the inner array's `offset` field is
    // deliberately small/plausible-looking *for the M2 blob* while
    // actually only being valid against a separate, unrelated "anim" blob
    // -- proving the payload really comes from externalDataBlob, not by
    // coincidence sharing layout with descriptorBlob.
    std::vector<uint8_t> m2Blob(100, 0);
    size_t trackOff = 100;
    m2Blob.resize(trackOff + 0x14, 0);
    putU16(m2Blob, trackOff + 0x02, 0xFFFF);  // global_sequence: none (see putFullTrack)
    // Two separate inner-array descriptors -- timestamps and values are
    // unrelated arrays, at unrelated offsets, even though both are
    // (deliberately) meaningless against m2Blob's own small buffer and
    // only valid against animBlob.
    size_t tsOuterOff = m2Blob.size();
    m2Blob.resize(tsOuterOff + 8, 0);
    putArray(m2Blob, tsOuterOff, 1, 40);  // 1 timestamp at animBlob offset 40
    size_t valOuterOff = m2Blob.size();
    m2Blob.resize(valOuterOff + 8, 0);
    putArray(m2Blob, valOuterOff, 1, 60);  // 1 C3Vector at animBlob offset 60
    putArray(m2Blob, trackOff + 0x04, 1, static_cast<uint32_t>(tsOuterOff));
    putArray(m2Blob, trackOff + 0x0C, 1, static_cast<uint32_t>(valOuterOff));

    std::vector<uint8_t> animBlob(100, 0);
    putU32(animBlob, 40, 12345);  // timestamp
    auto posBytes = vec3Bytes({7, 8, 9});
    std::memcpy(animBlob.data() + 60, posBytes.data(), posBytes.size());

    auto result = husk::m2::resolveVec3TrackSequence(m2Blob, static_cast<uint32_t>(trackOff), 0,
                                                       &animBlob);
    REQUIRE(result.size() == 1);
    CHECK(result[0].first == 12345);
    CHECK(result[0].second.x == doctest::Approx(7));
    CHECK(result[0].second.z == doctest::Approx(9));
}

TEST_CASE("resolveVec3TrackSequence: externalDataBlob too small for the claimed keyframe data "
          "throws, not a wild read") {
    std::vector<uint8_t> m2Blob(100, 0);
    size_t trackOff = 100;
    m2Blob.resize(trackOff + 0x14, 0);
    putU16(m2Blob, trackOff + 0x02, 0xFFFF);  // global_sequence: none (see putFullTrack)
    size_t tsOuterOff = m2Blob.size();
    m2Blob.resize(tsOuterOff + 8, 0);
    putArray(m2Blob, tsOuterOff, 1, 0);  // 1 timestamp at offset 0 -- fits tinyAnimBlob
    size_t valOuterOff = m2Blob.size();
    m2Blob.resize(valOuterOff + 8, 0);
    putArray(m2Blob, valOuterOff, 1, 40);  // 1 C3Vector at offset 40 -- doesn't fit
    putArray(m2Blob, trackOff + 0x04, 1, static_cast<uint32_t>(tsOuterOff));
    putArray(m2Blob, trackOff + 0x0C, 1, static_cast<uint32_t>(valOuterOff));

    std::vector<uint8_t> tinyAnimBlob(10, 0);  // far too small for offset 40 + 12 bytes

    CHECK_THROWS_AS(husk::m2::resolveVec3TrackSequence(m2Blob, static_cast<uint32_t>(trackOff), 0,
                                                         &tinyAnimBlob),
                     husk::m2::ParseError);
}

TEST_CASE("extractAnimBlob: chunked=false returns the file bytes verbatim") {
    std::vector<uint8_t> flat = {1, 2, 3, 4, 5};
    auto blob = husk::m2::extractAnimBlob(flat, /*chunked=*/false);
    CHECK(blob == flat);
}

TEST_CASE("extractAnimBlob: chunked=true finds and returns the AFM2 chunk's payload") {
    std::vector<uint8_t> file;
    appendChunk(file, "AFSA", {0xAA, 0xAA});  // unrelated chunk before AFM2
    appendChunk(file, "AFM2", {1, 2, 3, 4, 5, 6});
    appendChunk(file, "AFSB", {0xBB});  // unrelated chunk after AFM2

    auto blob = husk::m2::extractAnimBlob(file, /*chunked=*/true);
    std::vector<uint8_t> expected = {1, 2, 3, 4, 5, 6};
    CHECK(blob == expected);
}

TEST_CASE("extractAnimBlob: chunked=true with no AFM2 chunk throws, naming what it found") {
    std::vector<uint8_t> file;
    appendChunk(file, "AFSA", {0xAA});
    appendChunk(file, "AFSB", {0xBB});

    CHECK_THROWS_WITH_AS(husk::m2::extractAnimBlob(file, /*chunked=*/true),
                          doctest::Contains("AFSA"), husk::m2::ParseError);
}
