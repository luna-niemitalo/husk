#pragma once

#include <cstdint>
#include <vector>

#include "m2_primitives.hpp"

// M2 bind-pose skeleton, render-mesh vertices, and the collision mesh --
// split out of the former monolithic m2.hpp/m2.cpp (see
// FILE_SPLIT_TODO.md Item 2). See m2_primitives.hpp for the module
// overview (on-disk shapes, blob-resolution entry points).
namespace husk::m2 {

// M2Vertex, per wowdev.wiki M2#Vertices -- 48 bytes on disk, field order
// below matches the wire layout exactly (see tests/test_m2.cpp for the byte
// offsets). Read verbatim: no coordinate-system conversion happens here --
// WoW's Z-up-to-glTF's-Y-up flip is a concern for whatever writes glTF, not
// for this reader.
struct Vertex {
    Vec3 pos;
    uint8_t boneWeights[4] = {};
    uint8_t boneIndices[4] = {};
    Vec3 normal;
    Vec2 texCoords[2];
};

// M2CompBone, per wowdev.wiki M2#Bones -- 88 bytes on disk (>= Wrath shape,
// which is every version this parser targets). Only the fields stage 2 of
// the roadmap (skeleton + skinning, see README.md) needs for a bind-pose
// joint hierarchy are surfaced; the three embedded M2Track<T> animation
// blocks (translation/rotation/scale, 60 bytes together) are skipped over,
// not parsed -- see tests/test_m2.cpp for why their size is fixed and
// T-independent. Animation playback (roadmap stage 6) will need to revisit
// this and actually resolve them.
struct Bone {
    int32_t keyBoneId = -1;  // back-reference into the key-bone lookup table, -1 if none
    uint32_t flags = 0;
    int16_t parentBone = -1;  // index into this same bones array, -1 if none
    Vec3 pivot;                // bind-pose position, in model space

    // Byte offsets (relative to the *same blob `parseBones` was given* --
    // for inline M2 bones that's the MD20 blob; for a .skel file's SKB1
    // bones, per husk::skel::parseBones, it's that chunk's own payload
    // instead, which matters below) of this bone's three M2Track<T>
    // animation blocks -- translation/scale are M2Track<C3Vector>, rotation
    // is M2Track<Quat> (wire format M2CompQuat, see Quat's doc comment).
    // Not resolved into keyframe data by parseBones itself, same "just the
    // descriptor" policy Header's own M2Array fields follow -- see
    // resolveVec3TrackSequence/resolveQuatTrackSequence (m2_animation.hpp)
    // below, called with one specific M2Sequence index (Sequence, below) at
    // a time.
    //
    // Roadmap stage 6 (animation, see README.md) only actually wires these
    // up for a model's *inline* bones: per wowdev.wiki's .skel article,
    // once an M2 has moved its bones out to a .skel file, per-bone keyframe
    // data moves out too (into a .anim file's AFSB chunk, a format husk
    // doesn't parse yet) -- these three offsets are still populated for
    // .skel bones by parseBones (it can't tell the difference), but every
    // M2Track they point at is expected to be genuinely empty for that
    // case, not silently wrong.
    uint32_t translationTrackOffset = 0;
    uint32_t rotationTrackOffset = 0;
    uint32_t scaleTrackOffset = 0;
};

// Collision mesh (physics/hit-testing), dereferenced from the header's
// `collisionPositions`/`collisionIndices`/`collisionFaceNormals` Array
// descriptors -- wowdev.wiki M2#Header. A plain indexed triangle mesh, no
// M2Track/lookup-table indirection unlike Attachment/Event/Light
// (m2_scene.hpp): `indices` is flat (3 entries per triangle, indexing into
// `positions`), and `faceNormals` has one entry per triangle
// (indices.size() / 3), not per vertex -- distinct from the render mesh's
// per-vertex normals in m2::Vertex.
struct CollisionMesh {
    std::vector<Vec3> positions;
    std::vector<uint16_t> indices;
    std::vector<Vec3> faceNormals;
};

// Reads `array.count` M2Vertex records out of `blob` starting at
// `array.offset`. Throws ParseError if that range runs past the end of the
// blob. An empty array (count 0) returns an empty vector without touching
// `array.offset` at all.
std::vector<Vertex> parseVertices(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` M2CompBone records out of `blob` starting at
// `array.offset`. Throws ParseError if that range runs past the end of the
// blob. An empty array (count 0) returns an empty vector without touching
// `array.offset` at all.
std::vector<Bone> parseBones(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` little-endian uint16 values out of `blob` at
// `array.offset`. Throws ParseError if that range runs past the end of the
// blob. Used for the header's various uint16 "combo"/lookup arrays (e.g.
// `textureCombos`, wowdev.wiki's "Texture lookup table" -- see
// src/cmd_export.cpp for how batches resolve through it to an actual
// texture).
std::vector<uint16_t> parseUint16Array(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` raw C3Vector (12-byte float triples) out of `blob`
// at `array.offset`. Throws ParseError if that range runs past the end of
// the blob. Used for the header's plain position/normal arrays that carry
// no other per-record fields -- currently just the collision mesh's
// `collisionPositions`/`collisionFaceNormals` (see parseCollisionMesh).
std::vector<Vec3> parseVec3Array(const std::vector<uint8_t>& blob, const Array& array);

// Dereferences a header's collision Array descriptors into a real
// CollisionMesh: `positions`/`faceNormals` via parseVec3Array,
// `indices` via parseUint16Array. Throws ParseError under the same
// byte-range conditions as those two functions -- this does NOT
// cross-check `indices` against `positions.size()` or `faceNormals.size()`
// against `indices.size() / 3` (that's export-time consumption logic, same
// "cross-array validation lives at the point of use" split cmd_export.cpp
// already follows for e.g. vertex bone_indices vs. bone count). Every
// array empty (count 0) returns an all-empty CollisionMesh.
CollisionMesh parseCollisionMesh(const std::vector<uint8_t>& blob, const Array& positionsArray,
                                  const Array& indicesArray, const Array& faceNormalsArray);

}  // namespace husk::m2
