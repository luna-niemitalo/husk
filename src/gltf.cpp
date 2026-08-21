#include "gltf.hpp"

#include <sstream>
#include <string>
#include <unordered_map>

#include <tiny_gltf.h>

#include "gltf_mesh_internal.hpp"
#include "gltf_skeleton_internal.hpp"

// This file is the orchestrator (per FILE_SPLIT_TODO.md's Item 3):
// writeGlbMulti's own pipeline wiring, each stage of which is a named
// phase-function in gltf_mesh.cpp (validateMeshes/emitMeshNode) or
// gltf_skeleton.cpp (validateSkeletonTopology/validateSkeletonAnchors/
// validateAnimations/emitSkeletonAndSkin/buildAnimationClips) rather than
// inline code, so the pipeline's shape (validate -> skeleton/skin -> per-
// mesh nodes -> node/scene layout -> animations -> serialize) is visible
// from function signatures. Only document-level bookkeeping with no
// natural type-file home (buffer/model setup, node/scene assembly, the
// final tinygltf write) stays here.
//
// A zero-image export (real, common -- husk::m2::Texture slots with no
// local candidate file at all) omits the "images" key entirely, per
// tinygltf's own WriteGltfSceneToStream (gates on model->images.size()).
// This is the ONLY spec-valid shape: the glTF 2.0 schema requires
// "images" to have minItems:1 *when present* -- confirmed directly via
// gltf_validator, which reports "/images: Entity cannot be empty" for an
// explicit `"images":[]`. A real self-built Blender dev branch (glTF
// importer, io_scene_gltf2/blender/imp/blender_gltf.py:
// `len(gltf.data.images)` with no None-guard) crashes on the *correct*,
// spec-valid absent-key case with `TypeError: object of type 'NoneType'
// has no len()` -- a real upstream Blender bug (missing null-check on an
// always-optional glTF property), not fixable from husk's side without
// producing an invalid file that gltf_validator itself rejects (this
// project's own conformance suite, tests/test_conformance.cpp, gates on
// zero gltf_validator errors for every real export). The project's own
// pinned flake Blender (5.1.1) handles the absent-key case correctly, as
// does gltf_validator and every other consumer checked -- if this shows
// up again, it's that specific Blender build/version, not husk's output.
namespace husk::gltf {

std::vector<uint8_t> writeGlbMulti(const std::vector<NamedMesh>& meshes, const Skeleton* skeleton,
                                    const std::vector<Animation>& animations) {
    bool hasSkeleton = skeleton != nullptr && !skeleton->joints.empty();
    if (meshes.empty() && !hasSkeleton) {
        throw Error("writeGlbMulti: meshes must not be empty without a skeleton to fall back to "
                    "(nothing to export)");
    }
    validateSkeletonTopology(skeleton, hasSkeleton);
    validateSkeletonAnchors(skeleton);
    validateAnimations(animations, skeleton, hasSkeleton);
    validateMeshes(meshes, hasSkeleton);

    tinygltf::Model model;
    model.asset.version = "2.0";
    model.asset.generator = "husk";

    tinygltf::Buffer buffer;
    std::vector<tinygltf::BufferView> views;
    std::vector<tinygltf::Accessor> accessors;
    // Set if any material sets Material::unlit -- KHR_materials_unlit needs
    // a document-level extensionsUsed entry, added once after the mesh/
    // material emission loop below. usedTextureTransformExtension is the
    // same shape for KHR_texture_transform (gltf_mesh.cpp's emitMaterial).
    bool usedUnlitExtension = false;
    bool usedTextureTransformExtension = false;

    // Skeleton/skin: built once, shared by every mesh node below. Joint
    // nodes are appended after every mesh node (see the node-index layout
    // comment near the bottom of this function), so their indices are
    // offset by meshes.size() -- known up front, unlike the single-mesh
    // writeGlb's hardcoded "1 +". See gltf_skeleton_internal.hpp's
    // emitSkeletonAndSkin doc comment for the full shape this builds
    // (joint nodes, inverse bind matrices, skin + its extras, the
    // synthesized multi-root parent node, Attachment/Event/Light anchor
    // nodes).
    size_t meshCount = meshes.size();
    SkeletonEmission skelEm =
        emitSkeletonAndSkin(skeleton, hasSkeleton, meshCount, buffer, views, accessors);
    int skinIdx = skelEm.skinIndex;
    if (skelEm.skin) {
        model.skins = {*skelEm.skin};
    }

    std::vector<tinygltf::Image> images;
    std::vector<tinygltf::Texture> textures;
    std::vector<tinygltf::Material> tinyMaterials;
    std::vector<tinygltf::Mesh> tinyMeshes;
    std::vector<tinygltf::Node> meshNodes;
    // filename -> already-created texture index, shared across every mesh/
    // material this call emits -- see gltf_mesh_internal.hpp's
    // emitMeshNode doc comment for why this matters (many ambiguous
    // hardcoded-texture-slot materials can share the same candidate pool).
    std::unordered_map<std::string, int> alternateTextureCache;

    // Per-mesh emission (gltf_mesh_internal.hpp's emitMeshNode): appends
    // this mesh's own vertex/index/skinning accessors and materials/images/
    // textures into the shared, model-wide lists above. `node.mesh` is
    // filled in here, not by emitMeshNode itself, since it depends on this
    // mesh's final index in the shared `tinyMeshes` list.
    for (const auto& nm : meshes) {
        MeshEmission em = emitMeshNode(nm, hasSkeleton, skinIdx, buffer, views, accessors, images,
                                        textures, tinyMaterials, usedUnlitExtension,
                                        usedTextureTransformExtension, alternateTextureCache,
                                        skelEm.geosetTagJointIndex);
        tinyMeshes.push_back(em.mesh);
        em.node.mesh = static_cast<int>(tinyMeshes.size()) - 1;
        meshNodes.push_back(em.node);
    }

    model.images = images;
    model.textures = textures;
    model.materials = tinyMaterials;
    if (usedUnlitExtension) {
        model.extensionsUsed.emplace_back("KHR_materials_unlit");
    }
    if (usedTextureTransformExtension) {
        model.extensionsUsed.emplace_back("KHR_texture_transform");
    }
    model.meshes = tinyMeshes;

    // Node layout: mesh nodes first (indices 0..meshCount-1, in `meshes`
    // order), then the shared skeleton's joint nodes (meshCount..meshCount+
    // jointCount-1) -- see emitSkeletonAndSkin, which already computed
    // every joint/skin reference against this same offset -- then, only
    // for a multi-root skeleton, the one synthesized parent node at index
    // meshCount+jointCount (see skelEm.hasSyntheticRoot above), then any
    // attachment/event/light placement nodes (not scene roots themselves --
    // each is reached only via its owning joint node's `.children`).
    model.nodes = meshNodes;
    for (auto& jointNode : skelEm.jointNodes) {
        model.nodes.push_back(jointNode);
    }
    for (auto& tagNode : skelEm.geosetTagNodes) {
        model.nodes.push_back(tagNode);
    }
    if (skelEm.hasSyntheticRoot) {
        model.nodes.push_back(skelEm.syntheticRootNode);
    }
    for (auto& anchorNode : skelEm.anchorNodes) {
        model.nodes.push_back(anchorNode);
    }

    tinygltf::Scene scene;
    for (size_t mi = 0; mi < meshCount; ++mi) {
        scene.nodes.push_back(static_cast<int>(mi));
    }
    if (skelEm.hasSyntheticRoot) {
        // One scene root standing in for every real root joint -- reached
        // via the synthetic node's own .children, not listed individually.
        scene.nodes.push_back(skelEm.syntheticRootNodeIndex);
    } else {
        for (int rootIdx : skelEm.rootJointNodeIndices) {
            scene.nodes.push_back(rootIdx);
        }
    }
    model.scenes = {scene};
    model.defaultScene = 0;

    // Animations: one glTF animation per husk::gltf::Animation (see
    // gltf_skeleton_internal.hpp's buildAnimationClips doc comment).
    model.animations = buildAnimationClips(animations, meshCount, buffer, views, accessors);

    // Deferred until every appendBufferView/accessors.push_back above (mesh
    // data, skinning, materials' embedded images, and now animations) has
    // run -- `views`/`accessors` are plain local vectors, not references
    // into the model, so copying them into `model` any earlier would miss
    // whatever got appended after that point.
    model.bufferViews = views;
    model.accessors = accessors;

    model.buffers = {buffer};

    tinygltf::TinyGLTF writer;
    std::ostringstream out(std::ios::binary);
    if (!writer.WriteGltfSceneToStream(&model, out, /*prettyPrint=*/false, /*writeBinary=*/true)) {
        throw Error(
            "writeGlbMulti: tinygltf failed to write the model to its output stream, for an "
            "unknown reason -- WriteGltfSceneToStream() only returns bool, with no error-string "
            "API to report why (unlike tinygltf's own LoadBinaryFromFile/LoadASCIIFromFile, which "
            "take err/warn out-params); this is an upstream tinygltf limitation, not a diagnostic "
            "husk is withholding");
    }

    std::string bytes = out.str();
    return {bytes.begin(), bytes.end()};
}

std::vector<uint8_t> writeGlb(const Mesh& mesh, const std::vector<Material>& materials,
                               const Skeleton* skeleton, const std::vector<Animation>& animations) {
    return writeGlbMulti({NamedMesh{"", mesh, materials}}, skeleton, animations);
}

}  // namespace husk::gltf
