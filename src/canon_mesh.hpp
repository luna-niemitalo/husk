#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "canon_geoset.hpp"
#include "m2_primitives.hpp"  // m2::Vec2/Vec3

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// CANONICAL_MODEL.md's Resources layer -- pure per-vertex geometry +
// skinning value types, no assembly logic. Assembling one of these from a
// real M2/.skin pair is `husk::m2input`'s job (m2_mesh_input.hpp), not
// canon::'s -- see CANONICAL_MODEL.md's extended I1 section for why the
// struct and the M2-consuming assembler that builds it live in different
// files/namespaces now.
namespace husk::canon {

// indexStart/indexCount are carried here rather than left for the caller to
// re-slice from skin::Submesh, since a caller only has this type's own
// batch-filtered result to work from (skipped/invalid submeshes never
// appear here at all) -- re-deriving the range from the original submesh
// list would mean re-doing the assembler's own filtering.
struct PrimitiveGeoset {
    Geoset geoset;
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
};

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
    // normalization exactly (export_skeleton.cpp) -- see m2_mesh_input.hpp's
    // assembleMesh doc comment for the all-zero-weights detection this
    // struct doesn't itself perform.
    std::vector<Skinning> skinning;

    std::vector<uint32_t> indices;  // shared triangle-index buffer; PrimitiveGeoset::indexStart/indexCount slice into this
    std::vector<PrimitiveGeoset> primitives;
};

}  // namespace husk::canon
