// Shared byte-buffer-building helpers for tests/test_m2_*.cpp -- factored
// out here because buildMd20Blob/appendChunk/putU32-family are each used by
// 3+ of the split files (see FILE_SPLIT_TODO.md Item 5). Anonymous
// namespace: each including TU gets its own private copy, same as when
// this lived inline in the pre-split tests/test_m2.cpp.
//
// See TEST_DESIGN.md#Independent-transcription-convention -- offsets below
// are typed out fresh from https://wowdev.wiki/M2 "Header" and "Chunks"
// sections (fetched 2026-07-24), not copied from src/m2.cpp.
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

#pragma once

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
