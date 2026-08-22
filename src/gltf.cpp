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
                                    const std::vector<Animation>& animations,
                                    const std::string& slimTexturesOutputDir) {
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
                                        skelEm.geosetTagJointIndex, slimTexturesOutputDir);
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

    // Real AnimationData.db2 names, mirrored onto the skin's own root
    // joint extras (see emitSkeletonAndSkin's own doc comment on why
    // that's where Blender-survivable extras live) -- confirmed directly,
    // not assumed, that Blender's glTF importer drops animation-level
    // extras too (an Action's own custom properties come back empty after
    // a round trip even when the source file's `animations[].extras` was
    // set), so a Blender-side consumer needs this reachable from the
    // imported armature the same way chr_texture_layout etc. already are.
    // Each animation's own `sequence_metadata.animation_data_name` extras
    // (built above, in buildAnimationClips) stays too, for a raw-JSON
    // consumer that isn't going through Blender's importer at all -- the
    // one deliberate duplicate in this file, since the "natural" glTF
    // location and the one Blender's own importer will actually preserve
    // aren't the same place.
    if (!model.skins.empty() && !skelEm.rootJointNodeIndices.empty()) {
        tinygltf::Value::Object names;
        for (const auto& anim : animations) {
            if (anim.sequenceMetadata && !anim.sequenceMetadata->animationDataName.empty()) {
                names[anim.name] = tinygltf::Value(anim.sequenceMetadata->animationDataName);
            }
        }
        if (!names.empty()) {
            tinygltf::Node& rootNode = model.nodes[static_cast<size_t>(skelEm.rootJointNodeIndices.front())];
            tinygltf::Value::Object merged =
                rootNode.extras.IsObject() ? rootNode.extras.Get<tinygltf::Value::Object>() : tinygltf::Value::Object{};
            merged["animation_data_names"] = tinygltf::Value(names);
            rootNode.extras = tinygltf::Value(merged);
        }
    }

    // Deferred until every appendBufferView/accessors.push_back above (mesh
    // data, skinning, materials' embedded images, and now animations) has
    // run -- `views`/`accessors` are plain local vectors, not references
    // into the model, so copying them into `model` any earlier would miss
    // whatever got appended after that point.
    model.bufferViews = views;
    model.accessors = accessors;

    model.buffers = {buffer};

    tinygltf::TinyGLTF writer;
    // WriteGltfSceneToStream's own image-serialization step (tinygltf's
    // UpdateImageObject, tiny_gltf.h) unconditionally reduces any `img.uri`
    // we set down to GetBaseFilename(uri) -- e.g. our own real
    // 'textures/<name>.png' (--slim-textures, gltf_mesh.cpp's
    // writeSlimTextureFile) collapses to bare '<name>.png', silently
    // breaking the very file already written to disk at that relative
    // path -- whenever `img.image` (tinygltf's own decoded-pixel buffer,
    // which husk never populates; it hands tinygltf pre-encoded PNG bytes
    // instead) is empty, regardless of the `embedImages` argument
    // (WriteGltfSceneToStream always passes true, since it can't write a
    // separate file to a stream target anyway -- confirmed by reading
    // tiny_gltf.h directly, not assumed). A custom writer callback is the
    // only hook tinygltf exposes to intervene: this one ignores the
    // basename tinygltf already computed and hands back `image->uri`
    // (the real, full relative path we set) unchanged. A no-op for every
    // embedded (bufferView-based, uri empty) image -- UpdateImageObject
    // never even calls this callback for those (filename stays empty).
    writer.SetImageWriter(
        [](const std::string*, const std::string*, const tinygltf::Image* image, bool,
           const tinygltf::FsCallbacks*, const tinygltf::URICallbacks*, std::string* outUri,
           void*) -> bool {
            *outUri = image->uri;
            return true;
        },
        nullptr);
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
                               const Skeleton* skeleton, const std::vector<Animation>& animations,
                               const std::string& slimTexturesOutputDir) {
    return writeGlbMulti({NamedMesh{"", mesh, materials}}, skeleton, animations, slimTexturesOutputDir);
}

}  // namespace husk::gltf
