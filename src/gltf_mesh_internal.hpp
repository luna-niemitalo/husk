#pragma once

#include <vector>

#include <tiny_gltf.h>

#include "gltf_mesh.hpp"

// writeGlbMulti's (gltf.cpp) mesh-side phase-functions: input validation and
// per-NamedMesh tinygltf emission -- not part of the public husk::gltf API
// (see gltf_mesh.hpp for that), promoted out of gltf.cpp's own former
// anonymous namespace so gltf.cpp (the orchestrator) can call into
// gltf_mesh.cpp, same "cross-file helper gets promoted" pattern as
// gltf_buffer_utils.hpp / m2_primitives.hpp (FILE_SPLIT_TODO.md Item 2).
namespace husk::gltf {

// Throws Error (gltf.hpp) on any of writeGlbMulti's per-mesh/per-primitive
// contract violations -- see gltf.hpp's writeGlb doc comment for the exact
// conditions. `hasSkeleton` gates the skinning-data mismatch checks (a mesh
// may only carry skinning data when a skeleton was given).
void validateMeshes(const std::vector<NamedMesh>& meshes, bool hasSkeleton);

// One NamedMesh's worth of tinygltf output: its glTF mesh (primitives, each
// referencing accessors this call appends) and its glTF node (name,
// optional skin reference, optional collision-mesh extras). Appends to
// `buffer`/`views`/`accessors` (vertex/index/skinning data) and to
// `images`/`textures`/`tinyMaterials` (this mesh's own material list,
// remapped from NamedMesh::materials' local numbering into the shared,
// model-wide indices the returned mesh's primitives reference) -- mirrors
// writeGlbMulti's former per-mesh loop body 1:1, no behavior change.
// `usedUnlitExtension` is set (never cleared) if any of this mesh's
// materials sets Material::unlit.
struct MeshEmission {
    tinygltf::Mesh mesh;
    tinygltf::Node node;
};
MeshEmission emitMeshNode(const NamedMesh& nm, bool hasSkeleton, int skinIdx, tinygltf::Buffer& buffer,
                           std::vector<tinygltf::BufferView>& views,
                           std::vector<tinygltf::Accessor>& accessors, std::vector<tinygltf::Image>& images,
                           std::vector<tinygltf::Texture>& textures,
                           std::vector<tinygltf::Material>& tinyMaterials, bool& usedUnlitExtension);

}  // namespace husk::gltf
