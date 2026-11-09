#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "canon_model.hpp"
#include "json_writer.hpp"
#include "writers/bundle_common.hpp"  // BufferSlice

// The model document's sections whose arrays live in .bin files: mesh,
// levels of detail, liquids, collision, skeleton and animation
// (bundle_writer.cpp assembles them into the manifest).
namespace husk::writers {

// A mesh's (or mesh level's) vertex streams, once appended to mesh.bin; a
// level that shares them writes the same slices.
struct VertexSlices {
    BufferSlice positions;
    BufferSlice normals;
    std::vector<BufferSlice> uvSets;
    std::vector<BufferSlice> colorSets;
    std::optional<BufferSlice> joints;
    std::optional<BufferSlice> weights;
};

struct MeshSlices {
    VertexSlices vertices;
    BufferSlice indices;
    std::vector<BufferSlice> attributes;
};

// resources.mesh -- appends every mesh BufferSlice to `meshBin` and writes
// the section to `w`. Returns the slices, which mesh level 0 repeats.
MeshSlices writeMeshSection(json::Writer& w, const canon::Model& model, std::vector<uint8_t>& meshBin);

// resources.geometry.lod: level 0 repeats the mesh's own slices and
// primitives (`mesh0`), so every level reads the same way; a level sharing
// the mesh's vertices repeats its vertex slices.
void writeLodSection(json::Writer& w, const canon::Model& model, const MeshSlices& mesh0, std::vector<uint8_t>& meshBin);

void writeSkeletonSection(json::Writer& w, const canon::Skeleton& skeleton, std::vector<uint8_t>& skeletonBin);

void writeAnimationSection(json::Writer& w, const canon::Skeleton& skeleton,
                            const std::vector<canon::AnimationClip>& clips, std::vector<uint8_t>& animBin);

// resources.liquids -- appends every liquid's arrays to `liquidBin`.
void writeLiquids(json::Writer& w, const canon::Model& model, std::vector<uint8_t>& liquidBin);

void writeCollision(json::Writer& w, const canon::CollisionMesh& c, std::vector<uint8_t>& collisionBin);

}  // namespace husk::writers
