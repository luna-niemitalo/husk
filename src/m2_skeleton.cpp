#include "m2_skeleton.hpp"

#include <cstring>

namespace husk::m2 {

std::vector<Vertex> parseVertices(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Vertex> vertices;
    if (array.count == 0) {
        return vertices;
    }

    constexpr size_t kVertexSize = 0x30;  // M2Vertex, wowdev.wiki M2#Vertices
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    // Validate the whole claimed range up front, before reserve() -- a
    // corrupted or misread count (this is what a version this parser
    // doesn't know about yet, shifting this offset, looks like) must not
    // be able to make this allocate hundreds of GB before the very first
    // per-element check below ever runs.
    // TODO: Remove: FAILURES.md #2.
    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kVertexSize) {
        throw ParseError("vertices array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kVertexSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    vertices.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kVertexSize;
        Vertex v;
        v.pos = readVec3(data, blobSize, off + 0x00);
        for (int j = 0; j < 4; ++j) v.boneWeights[j] = data[off + 0x0C + j];
        for (int j = 0; j < 4; ++j) v.boneIndices[j] = data[off + 0x10 + j];
        v.normal = readVec3(data, blobSize, off + 0x14);
        v.texCoords[0].x = readF32(data, blobSize, off + 0x20);
        v.texCoords[0].y = readF32(data, blobSize, off + 0x24);
        v.texCoords[1].x = readF32(data, blobSize, off + 0x28);
        v.texCoords[1].y = readF32(data, blobSize, off + 0x2C);
        vertices.push_back(v);
    }

    return vertices;
}

std::vector<Bone> parseBones(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Bone> bones;
    if (array.count == 0) {
        return bones;
    }

    // M2CompBone, >= Wrath shape (wowdev.wiki M2#Bones -- see the offset
    // table transcribed independently in tests/test_m2.cpp).
    constexpr size_t kBoneSize = 0x58;
    constexpr size_t kKeyBoneIdOffset = 0x00;
    constexpr size_t kFlagsOffset = 0x04;
    constexpr size_t kParentBoneOffset = 0x08;
    constexpr size_t kTranslationTrackOffset = 0x10;
    constexpr size_t kRotationTrackOffset = 0x24;
    constexpr size_t kScaleTrackOffset = 0x38;
    constexpr size_t kPivotOffset = 0x4C;

    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    // Same up-front validation as parseVertices above, and for the same
    // reason -- a corrupted/misread count must fail with a real message
    // here, not a bare std::bad_alloc from reserve().
    // TODO: Remove: FAILURES.md #2.
    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kBoneSize) {
        throw ParseError("bones array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kBoneSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    bones.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kBoneSize;
        Bone b;
        b.keyBoneId = static_cast<int32_t>(readU32(data, blobSize, off + kKeyBoneIdOffset));
        b.flags = readU32(data, blobSize, off + kFlagsOffset);
        uint16_t parentBoneBits;
        std::memcpy(&parentBoneBits, data + off + kParentBoneOffset, sizeof(parentBoneBits));
        b.parentBone = static_cast<int16_t>(parentBoneBits);
        b.translationTrackOffset = static_cast<uint32_t>(off + kTranslationTrackOffset);
        b.rotationTrackOffset = static_cast<uint32_t>(off + kRotationTrackOffset);
        b.scaleTrackOffset = static_cast<uint32_t>(off + kScaleTrackOffset);
        b.pivot = readVec3(data, blobSize, off + kPivotOffset);
        bones.push_back(b);
    }

    return bones;
}

std::vector<uint16_t> parseUint16Array(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<uint16_t> values;
    if (array.count == 0) {
        return values;
    }

    constexpr size_t kElementSize = 2;
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kElementSize) {
        throw ParseError("array claims " + std::to_string(array.count) +
                          " uint16 entries at offset " + std::to_string(array.offset) +
                          ", which needs more room than the blob's " + std::to_string(blobSize) +
                          " bytes");
    }
    values.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kElementSize;
        uint16_t v;
        std::memcpy(&v, data + off, sizeof(v));
        values.push_back(v);
    }

    return values;
}

std::vector<Vec3> parseVec3Array(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Vec3> values;
    if (array.count == 0) {
        return values;
    }

    constexpr size_t kElementSize = 12;  // C3Vector: 3x float32
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kElementSize) {
        throw ParseError("array claims " + std::to_string(array.count) +
                          " C3Vector entries at offset " + std::to_string(array.offset) +
                          ", which needs more room than the blob's " + std::to_string(blobSize) +
                          " bytes");
    }
    values.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kElementSize;
        values.push_back(readVec3(data, blobSize, off));
    }

    return values;
}

CollisionMesh parseCollisionMesh(const std::vector<uint8_t>& blob, const Array& positionsArray,
                                  const Array& indicesArray, const Array& faceNormalsArray) {
    CollisionMesh mesh;
    mesh.positions = parseVec3Array(blob, positionsArray);
    mesh.indices = parseUint16Array(blob, indicesArray);
    mesh.faceNormals = parseVec3Array(blob, faceNormalsArray);
    return mesh;
}

}  // namespace husk::m2
