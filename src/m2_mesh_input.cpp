#include "m2_mesh_input.hpp"

#include <stdexcept>
#include <string>

namespace husk::m2input {

std::vector<canon::PrimitiveGeoset> assemblePrimitiveGeosets(const std::vector<skin::Batch>& batches,
                                                               const std::vector<skin::Submesh>& submeshes,
                                                               size_t triangleIndexCount) {
    std::vector<canon::PrimitiveGeoset> result;
    result.reserve(batches.size());

    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.skinSectionIndex >= submeshes.size()) {
            throw std::runtime_error("batch " + std::to_string(bi) + "'s skinSectionIndex (" +
                                      std::to_string(b.skinSectionIndex) +
                                      ") is out of range for " + std::to_string(submeshes.size()) +
                                      " submeshes");
        }
        const auto& sm = submeshes[b.skinSectionIndex];
        if (static_cast<size_t>(sm.indexStart) + sm.indexCount > triangleIndexCount) {
            throw std::runtime_error(
                "submesh " + std::to_string(b.skinSectionIndex) +
                "'s index range runs past the end of the resolved triangle-index buffer -- "
                "corrupted .skin?");
        }

        // A submesh with zero indices alongside siblings that have real
        // geometry -- no primitive to build, same skip export_materials.cpp
        // applies.
        if (sm.indexCount == 0) {
            continue;
        }

        canon::PrimitiveGeoset pg;
        pg.geoset = canon::Geoset::fromRawId(sm.skinSectionId);
        pg.indexStart = sm.indexStart;
        pg.indexCount = sm.indexCount;
        result.push_back(pg);
    }

    return result;
}

namespace {

bool isZeroVec2(const m2::Vec2& v) { return v.x == 0.0f && v.y == 0.0f; }

bool allZeroWeights(const uint8_t weights[4]) {
    for (int j = 0; j < 4; ++j) {
        if (weights[j] != 0) return false;
    }
    return true;
}

}  // namespace

canon::Mesh assembleMesh(const std::vector<m2::Vertex>& vertices, size_t boneCount,
                          const std::vector<skin::Batch>& batches, const std::vector<skin::Submesh>& submeshes,
                          const std::vector<uint32_t>& triangleIndices) {
    canon::Mesh mesh;
    mesh.positions.reserve(vertices.size());
    mesh.normals.reserve(vertices.size());
    mesh.uv0.reserve(vertices.size());

    std::vector<m2::Vec2> uv1;
    uv1.reserve(vertices.size());
    bool uv1AllZero = true;

    bool skinned = false;
    for (const auto& v : vertices) {
        if (!allZeroWeights(v.boneWeights)) {
            skinned = true;
            break;
        }
    }

    for (size_t vi = 0; vi < vertices.size(); ++vi) {
        const auto& v = vertices[vi];
        mesh.positions.push_back(v.pos);
        mesh.normals.push_back(v.normal);
        mesh.uv0.push_back(v.texCoords[0]);
        uv1.push_back(v.texCoords[1]);
        if (!isZeroVec2(v.texCoords[1])) uv1AllZero = false;

        if (skinned) {
            canon::Mesh::Skinning sk;
            for (int j = 0; j < 4; ++j) {
                if (v.boneIndices[j] >= boneCount) {
                    throw std::runtime_error("vertex " + std::to_string(vi) + "'s bone_indices[" +
                                              std::to_string(j) + "] (" +
                                              std::to_string(v.boneIndices[j]) +
                                              ") is out of range for " + std::to_string(boneCount) +
                                              " bones");
                }
                sk.joints[static_cast<size_t>(j)] = v.boneIndices[j];
                sk.weights[static_cast<size_t>(j)] = static_cast<float>(v.boneWeights[j]) / 255.0f;
            }
            mesh.skinning.push_back(sk);
        }
    }

    if (!uv1AllZero) mesh.uv1 = std::move(uv1);

    mesh.indices = triangleIndices;
    mesh.primitives = assemblePrimitiveGeosets(batches, submeshes, triangleIndices.size());
    return mesh;
}

}  // namespace husk::m2input
