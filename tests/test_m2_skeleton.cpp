// Tests for husk::m2's skeleton module (src/m2_skeleton.hpp/.cpp): parseVertices/parseBones/parseUint16Array/parseVec3Array/parseCollisionMesh.
// Split out of the former tests/test_m2.cpp -- see FILE_SPLIT_TODO.md Item 5.

#include "test_m2_fixtures.hpp"

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


