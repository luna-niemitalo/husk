#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "canon_attribute.hpp"
#include "canon_effects.hpp"  // InstanceRef
#include "canon_geoset.hpp"
#include "canon_primitives.hpp"  // Vec2/Vec3

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
    std::optional<uint32_t> part;  // index into Mesh::parts; nullopt for a model with no parts (every M2)
};

// A part's opening onto another part through a Mesh::portals polygon
// (WMO MOPR): the cell/portal graph a renderer culls with.
struct PortalLink {
    uint32_t portal = 0;  // index into Mesh::portals
    uint32_t toPart = 0;  // the part on the other side
    // Source sign, raw (+1/-1). In 8,685/10,103 real links it is the sign
    // of dot(normal, c) + distance for this part's vertex centroid c.
    int32_t side = 0;
};

// A polygon two parts see each other through (WMO MOPT/MOPV).
struct Portal {
    std::vector<Vec3> vertices;  // in source order, on the plane
    // dot(normal, p) + distance == 0 for its vertices (5,069/5,069 real portals).
    Vec3 normal;
    float distance = 0;
    std::vector<uint8_t> sourceExtra;  // WMO MOPE record, raw; empty when absent
};

// A named, separately addressable piece of a model -- a WMO group (a room,
// a wing, an outside shell). Its primitives point back at it through
// PrimitiveGeoset::part.
struct MeshPart {
    Ref ref;             // id = RecordIndex{part index}; name from the source when it has one
    uint32_t flags = 0;  // source-format group flags, raw (WMO MOGP flags)
    Vec3 boundsMin;
    Vec3 boundsMax;
    // Fog instances in the model's owned placement sets: WMO MFVR when the
    // group has one (its fogIds are then all 0), else MOGP fogIds' nonzero
    // slots. A 0 slot names the model's default fog (Model::defaultFog).
    std::vector<InstanceRef> fogs;
    std::vector<PortalLink> portals;
    std::vector<InstanceRef> particulateVolumes;  // particle effect instances (WMO MPVR)
    std::vector<InstanceRef> ambientVolumes;   // ambient effect instances (WMO MAVR, then MBVR)
};

// One model's worth of real per-vertex geometry + skinning, raw M2 space
// (see gltf_mesh.hpp's Mesh doc comment for the writer-side Y-up conversion
// this deliberately does NOT perform -- I1, same "no glTF-shaped convenience
// baked in" rule canon_skeleton.hpp's Joint::globalPosition already states).
// positions/normals/uvSets/skinning share one index space -- every M2 global
// vertex, not a per-primitive-local list -- and `indices`/`primitives` slice
// into that shared space, mirroring gltf::Mesh's own flat-array-plus-index-
// buffer shape (gltf_mesh.hpp) exactly.
struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    // Texture coordinate sets, each one entry per vertex; set 0 is always
    // present (empty only with no vertices). M2's second set is left out
    // when every coordinate is the origin: M2 carries no flag saying a model
    // samples it, and an all-origin set can't be told from one never
    // written. WMO groups have up to three (MOTV).
    std::vector<std::vector<Vec2>> uvSets{1};

    // Per-vertex colour sets, RGBA bytes as stored (WMO MOCV: baked vertex
    // lighting; the client's FixColorVertexAlpha adjustment is not applied).
    std::vector<std::vector<std::array<uint8_t, 4>>> colorSets;

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
    std::vector<MeshPart> parts;
    std::vector<Portal> portals;
    std::vector<AttributeChannel> attributes;
};

}  // namespace husk::canon
