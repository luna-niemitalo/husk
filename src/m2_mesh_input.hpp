#pragma once

#include <cstddef>
#include <vector>

#include "canon_mesh.hpp"
#include "m2_skeleton.hpp"  // m2::Vertex
#include "skin.hpp"

// husk::m2input: the M2 input module -- see m2_canon_input.hpp's own doc
// comment for the split this belongs to. Turns a real .skin's batch/submesh
// tables and an M2's own vertex array into canon::Mesh/canon::PrimitiveGeoset
// -- the adjacent twin of buildMaterialsAndPrimitives's batch loop
// (export_materials.cpp) and commands::buildSkinning (export_skeleton.cpp),
// built to prove structural convergence (REFACTOR/README.md stage 3's gate)
// without touching the existing pipeline.
namespace husk::m2input {

// Mirrors buildMaterialsAndPrimitives's batch loop (export_materials.cpp):
// for each batch, resolves its submesh, skips a zero-indexCount submesh (a
// real, documented case -- an empty geoset alongside siblings with real
// geometry), and otherwise emits one PrimitiveGeoset per batch. Throws
// std::runtime_error on the same two corruption cases that function
// guards against: an out-of-range skinSectionIndex, or a submesh index
// range running past triangleIndexCount.
std::vector<canon::PrimitiveGeoset> assemblePrimitiveGeosets(const std::vector<skin::Batch>& batches,
                                                               const std::vector<skin::Submesh>& submeshes,
                                                               size_t triangleIndexCount);

// Populates positions/normals/uv0/uv1/skinning directly from `vertices` (no
// axis conversion, no bind-pose adjustment -- literally v.pos/v.normal/
// v.texCoords[0]/v.texCoords[1]), and `indices`/`primitives` via
// assemblePrimitiveGeosets (not re-derived -- that function's own
// convergence proof already covers this half).
//
// Skinning mirrors commands::buildSkinning (export_skeleton.cpp) exactly:
// each of a vertex's 4 boneIndices is bounds-checked against `boneCount`
// (same throw message shape), each boneWeight is normalized uint8 -> float
// via /255.0f. When EVERY vertex's boneWeights are all zero -- a genuinely
// unskinned/static model, since M2 has no separate "this model has no
// skinning" header flag to check instead (confirmed by reading m2_header.hpp
// and m2::Model in full: no such bit exists) -- `skinning` is left empty
// rather than filled with boneCount-many meaningless zero-weight entries.
//
// Throws std::runtime_error on the same out-of-range-boneIndices case
// buildSkinning does.
canon::Mesh assembleMesh(const std::vector<m2::Vertex>& vertices, size_t boneCount,
                          const std::vector<skin::Batch>& batches, const std::vector<skin::Submesh>& submeshes,
                          const std::vector<uint32_t>& triangleIndices);

}  // namespace husk::m2input
