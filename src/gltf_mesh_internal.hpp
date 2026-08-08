#pragma once

#include <string>
#include <unordered_map>
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
// materials sets Material::unlit. `usedTextureTransformExtension` is set
// (never cleared) if any material got a real KHR_texture_transform on its
// baseColorTexture (gltf_mesh.cpp's emitMaterial/textureTransformToKhr --
// the constant-case, planar-rotation subset of Material::textureTransform).
// `alternateTextureCache` (filename -> already-created texture index) is
// shared and accumulated across every
// call in one writeGlbMulti invocation -- a real character model can have
// many ambiguous hardcoded-texture-slot materials all drawing candidates
// from the *same* shared basename pool (gltf_mesh.hpp's
// AlternateTextureCandidate doc comment), and without this cache each one
// would re-embed the same file's full bytes as its own separate glTF
// image/texture, ballooning file size by however many materials share
// that candidate (confirmed: 19x on one real export). See emitMaterial's
// own alternate-textures block (gltf_mesh.cpp) for where this is used.
struct MeshEmission {
    tinygltf::Mesh mesh;
    tinygltf::Node node;
};
MeshEmission emitMeshNode(const NamedMesh& nm, bool hasSkeleton, int skinIdx, tinygltf::Buffer& buffer,
                           std::vector<tinygltf::BufferView>& views,
                           std::vector<tinygltf::Accessor>& accessors, std::vector<tinygltf::Image>& images,
                           std::vector<tinygltf::Texture>& textures,
                           std::vector<tinygltf::Material>& tinyMaterials, bool& usedUnlitExtension,
                           bool& usedTextureTransformExtension,
                           std::unordered_map<std::string, int>& alternateTextureCache);

}  // namespace husk::gltf
