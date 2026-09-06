#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "canon_geoset.hpp"
#include "m2_primitives.hpp"  // m2::Vec2/Vec3
#include "m2_skeleton.hpp"    // m2::Vertex
#include "skin.hpp"

// husk::canon: assembly of per-primitive geoset identity from a .skin's
// batch/submesh tables -- the adjacent twin of buildMaterialsAndPrimitives's
// batch loop (export_materials.cpp), built to prove structural convergence
// (REFACTOR/README.md stage 3's gate) without touching the existing
// pipeline. Geoset-id/index-range extraction only -- material/texture
// resolution is a separate, much larger canon concern not built yet.
namespace husk::canon {

// indexStart/indexCount are carried here rather than left for the caller to
// re-slice from skin::Submesh, since a caller only has this type's own
// batch-filtered result to work from (skipped/invalid submeshes never
// appear here at all) -- re-deriving the range from the original submesh
// list would mean re-doing this function's own filtering.
struct PrimitiveGeoset {
    Geoset geoset;
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
};

// Mirrors buildMaterialsAndPrimitives's batch loop (export_materials.cpp):
// for each batch, resolves its submesh, skips a zero-indexCount submesh (a
// real, documented case -- an empty geoset alongside siblings with real
// geometry), and otherwise emits one PrimitiveGeoset per batch. Throws
// std::runtime_error on the same two corruption cases that function
// guards against: an out-of-range skinSectionIndex, or a submesh index
// range running past triangleIndexCount.
std::vector<PrimitiveGeoset> assemblePrimitiveGeosets(const std::vector<skin::Batch>& batches,
                                                       const std::vector<skin::Submesh>& submeshes,
                                                       size_t triangleIndexCount);

// One model's worth of real per-vertex geometry + skinning, raw M2 space
// (see gltf_mesh.hpp's Mesh doc comment for the writer-side Y-up conversion
// this deliberately does NOT perform -- I1, same "no glTF-shaped convenience
// baked in" rule canon_skeleton.hpp's Joint::globalPosition already states).
// positions/normals/uv0/skinning share one index space -- every M2 global
// vertex, not a per-primitive-local list -- and `indices`/`primitives` slice
// into that shared space, mirroring gltf::Mesh's own flat-array-plus-index-
// buffer shape (gltf_mesh.hpp) exactly.
struct Mesh {
    std::vector<m2::Vec3> positions;
    std::vector<m2::Vec3> normals;
    std::vector<m2::Vec2> uv0;

    // M2's second UV set (m2::Vertex::texCoords[1]). No M2-level flag
    // distinguishes "this model actually samples a second UV set" from
    // "the field is present but unused" -- wowdev.wiki M2.md's own
    // tex_coords[2] comment ("two textures, depending on shader used") says
    // this is a per-batch/shader fact, not a header-level one, and the real
    // production pipeline (cmd_export.cpp's buildBaseMesh) already emits
    // texCoords2 unconditionally for every model regardless of actual use.
    // Rather than mirror that (a real "fabricated all-zero array" case) or
    // invent a new per-model flag M2 doesn't have, this applies the same
    // technique the task spec asks for `skinning` below: absent (nullopt)
    // exactly when every vertex's texCoords[1] is the literal origin --
    // indistinguishable from "never written," so nothing downstream could
    // read it as a real second coordinate anyway.
    std::optional<std::vector<m2::Vec2>> uv1;

    struct Skinning {
        std::array<uint8_t, 4> joints{};
        std::array<float, 4> weights{};
    };
    // One entry per position when the model is skinned; empty for a
    // genuinely unskinned/static model (a real, valid state -- a prop with
    // no bones at all). Mirrors buildSkinning's own bounds check/
    // normalization exactly (export_skeleton.cpp) -- see assembleMesh's own
    // doc comment for the all-zero-weights detection this struct doesn't
    // itself perform.
    std::vector<Skinning> skinning;

    std::vector<uint32_t> indices;  // shared triangle-index buffer; PrimitiveGeoset::indexStart/indexCount slice into this
    std::vector<PrimitiveGeoset> primitives;
};

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
Mesh assembleMesh(const std::vector<m2::Vertex>& vertices, size_t boneCount,
                   const std::vector<skin::Batch>& batches, const std::vector<skin::Submesh>& submeshes,
                   const std::vector<uint32_t>& triangleIndices);

}  // namespace husk::canon
