#include "gltf_mesh.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <unordered_map>

#include "gltf_buffer_utils.hpp"
#include "gltf_mesh_internal.hpp"
#include "gltf.hpp"

namespace husk::gltf {

namespace {

const char* alphaModeString(Material::AlphaMode mode) {
    switch (mode) {
        case Material::AlphaMode::Opaque: return "OPAQUE";
        case Material::AlphaMode::Mask: return "MASK";
        case Material::AlphaMode::Blend: return "BLEND";
    }
    return "OPAQUE";
}

// One Material -> one tinygltf::Material, embedding its base-color image (if
// any) and building `materialExtras` from every inert-metadata field
// (additional texture layers, texture transform, texture type/FileDataID,
// animated tint/fade curves) -- see gltf_mesh.hpp's Material doc comment for
// why each of those is extras-only rather than a real glTF/PBR slot. Mirrors
// writeGlbMulti's former per-material loop body 1:1.
tinygltf::Material emitMaterial(const Material& mat, tinygltf::Buffer& buffer,
                                 std::vector<tinygltf::BufferView>& views,
                                 std::vector<tinygltf::Image>& images,
                                 std::vector<tinygltf::Texture>& textures, int uv2AccIdx,
                                 bool& usedUnlitExtension,
                                 std::unordered_map<std::string, int>& alternateTextureCache) {
    tinygltf::Material tm;
    tm.name = mat.name;
    tm.alphaMode = alphaModeString(mat.alphaMode);
    tm.doubleSided = mat.doubleSided;
    tm.pbrMetallicRoughness.baseColorFactor = {mat.baseColorFactor[0], mat.baseColorFactor[1],
                                                 mat.baseColorFactor[2], mat.baseColorFactor[3]};
    if (!mat.baseColorImagePng.empty()) {
        // Two genuinely different materials (different textureType, e.g.
        // char_hair vs. object_skin) can still legitimately resolve to the
        // exact same source file -- an unrecognized-category fuzzy
        // candidate stays a wildcard for every type (candidateAllowedForType's
        // doc comment), so nothing stops two unrelated ambiguous slots from
        // picking the same one. materialDedupKey (export_materials.cpp)
        // already merges materials that are otherwise identical, but these
        // two aren't (different textureType) -- so without this cache each
        // would still embed its own separate copy of the same image, which
        // is exactly the "same filename, incrementing Blender suffix"
        // report this cache is here to close. Keyed by the same real source
        // filename `alternateTextureCache` already uses for
        // alternateTextureCandidates -- one shared cache, one shared
        // meaning, not two independently-behaving lookups.
        auto cached = mat.baseColorImageName.empty()
                          ? alternateTextureCache.end()
                          : alternateTextureCache.find(mat.baseColorImageName);
        int texIdx;
        if (cached != alternateTextureCache.end()) {
            texIdx = cached->second;
        } else {
            int imgView = appendBufferView(buffer, views, mat.baseColorImagePng, /*target=*/0);
            tinygltf::Image img;
            img.mimeType = "image/png";
            img.bufferView = imgView;
            // Real source filename (Material::baseColorImageName's own doc
            // comment) -- without this, Blender's glTF importer falls back
            // to an auto-generated "Image_<N>" name, which is what
            // prompted this.
            img.name = mat.baseColorImageName;
            int imgIdx = static_cast<int>(images.size());
            images.push_back(img);

            tinygltf::Texture tex;
            tex.source = imgIdx;
            tex.name = mat.baseColorImageName;
            texIdx = static_cast<int>(textures.size());
            textures.push_back(tex);

            if (!mat.baseColorImageName.empty()) {
                alternateTextureCache.emplace(mat.baseColorImageName, texIdx);
            }
        }

        tm.pbrMetallicRoughness.baseColorTexture.index = texIdx;
        if (mat.baseColorTexCoord == 1 && uv2AccIdx >= 0) {
            tm.pbrMetallicRoughness.baseColorTexture.texCoord = 1;
        }
    }
    if (mat.unlit) {
        tm.extensions["KHR_materials_unlit"] = tinygltf::Value(tinygltf::Value::Object());
        usedUnlitExtension = true;
    }
    // Both blocks below accumulate into the same `materialExtras` object
    // (assigned to tm.extras once, at the end) rather than each setting
    // tm.extras independently -- a material can have both an extra texture
    // layer and a texture transform at once, and the second assignment
    // would otherwise silently clobber the first.
    tinygltf::Value::Object materialExtras;

    // Additional texture layers (textureCount > 1) -- inert glTF extras,
    // not wired into pbrMetallicRoughness (see gltf_mesh.hpp's
    // AdditionalTextureLayer doc comment for why): each layer's
    // FileDataID/UV-set is always listed, plus a real, separately embedded
    // (but core-material-unused) image/texture when `--textures` had a
    // match for it, the same "embed if available, metadata either way"
    // policy baseColorImagePng uses.
    // TODO: Remove: FAILURES2.md #6.
    if (!mat.additionalTextureLayers.empty()) {
        tinygltf::Value::Array layers;
        for (const auto& layer : mat.additionalTextureLayers) {
            tinygltf::Value::Object layerObj;
            layerObj["file_data_id"] = tinygltf::Value(static_cast<int>(layer.fileDataId));
            layerObj["tex_coord"] = tinygltf::Value(layer.texCoord);
            if (!layer.imagePng.empty()) {
                int imgView = appendBufferView(buffer, views, layer.imagePng, /*target=*/0);
                tinygltf::Image img;
                img.mimeType = "image/png";
                img.bufferView = imgView;
                // No source filename tracked for this layer, only a
                // FileDataID -- still better than an auto-generated
                // "Image_<N>" in Blender's importer.
                if (layer.fileDataId != 0) img.name = std::to_string(layer.fileDataId);
                int imgIdx = static_cast<int>(images.size());
                images.push_back(img);

                tinygltf::Texture tex;
                tex.source = imgIdx;
                tex.name = img.name;
                int texIdx = static_cast<int>(textures.size());
                textures.push_back(tex);

                layerObj["texture_index"] = tinygltf::Value(texIdx);
            }
            layers.push_back(tinygltf::Value(layerObj));
        }
        materialExtras["additional_textures"] = tinygltf::Value(layers);
    }

    // UV scroll/rotate/scale animation (M2TextureTransform) -- same inert-
    // extras treatment, see gltf_mesh.hpp's TextureTransform doc comment
    // for why this isn't a real KHR_texture_transform.
    if (mat.textureTransform) {
        const auto& xf = *mat.textureTransform;
        tinygltf::Value::Object xfObj;
        xfObj["constant"] = tinygltf::Value(xf.constant);
        xfObj["translation"] = tinygltf::Value(tinygltf::Value::Array{
            tinygltf::Value(static_cast<double>(xf.translation.x)),
            tinygltf::Value(static_cast<double>(xf.translation.y)),
            tinygltf::Value(static_cast<double>(xf.translation.z))});
        xfObj["rotation"] = tinygltf::Value(tinygltf::Value::Array{
            tinygltf::Value(static_cast<double>(xf.rotation[0])),
            tinygltf::Value(static_cast<double>(xf.rotation[1])),
            tinygltf::Value(static_cast<double>(xf.rotation[2])),
            tinygltf::Value(static_cast<double>(xf.rotation[3]))});
        xfObj["scaling"] = tinygltf::Value(tinygltf::Value::Array{
            tinygltf::Value(static_cast<double>(xf.scaling.x)),
            tinygltf::Value(static_cast<double>(xf.scaling.y)),
            tinygltf::Value(static_cast<double>(xf.scaling.z))});
        materialExtras["texture_transform"] = tinygltf::Value(xfObj);
    }

    // Texture.type marker -- see gltf_mesh.hpp's Material::textureType doc
    // comment.
    if (mat.textureType != 0) {
        materialExtras["texture_type"] = tinygltf::Value(static_cast<int>(mat.textureType));
    }

    // The primary texture's real FileDataID, when known -- see
    // gltf_mesh.hpp's Material::baseColorTextureFileDataId doc comment.
    if (mat.baseColorTextureFileDataId != 0) {
        materialExtras["texture_file_data_id"] =
            tinygltf::Value(static_cast<int>(mat.baseColorTextureFileDataId));
    }

    // Ambiguous hardcoded-slot candidates -- see gltf_mesh.hpp's
    // AlternateTextureCandidate doc comment. Each *distinct* candidate file
    // is a real, separately embedded image/texture (same pattern as
    // additionalTextureLayers above), named so a human or script can tell
    // which real file it came from and swap it into
    // pbrMetallicRoughness.baseColorTexture in place of the arbitrary
    // default husk already picked. `alternateTextureCache` (filename ->
    // texture index, shared across every material this whole export
    // emits) makes this a real dedup, not just a naming convenience: many
    // materials can share the exact same ambiguous candidate pool (a real
    // character model had 19 such materials sharing one 94-file pool), and
    // without this cache each one would re-embed every candidate's full
    // bytes as its own separate image, multiplying file size by however
    // many materials share the pool.
    if (!mat.alternateTextureCandidates.empty()) {
        tinygltf::Value::Array candidates;
        for (const auto& cand : mat.alternateTextureCandidates) {
            tinygltf::Value::Object candObj;
            candObj["filename"] = tinygltf::Value(cand.filename);
            if (!cand.category.empty()) {
                candObj["category"] = tinygltf::Value(cand.category);
            }
            if (cand.width != 0 && cand.height != 0) {
                candObj["width"] = tinygltf::Value(static_cast<int>(cand.width));
                candObj["height"] = tinygltf::Value(static_cast<int>(cand.height));
            }
            if (!cand.imagePng.empty()) {
                auto cached = alternateTextureCache.find(cand.filename);
                int texIdx;
                if (cached != alternateTextureCache.end()) {
                    texIdx = cached->second;
                } else {
                    int imgView = appendBufferView(buffer, views, cand.imagePng, /*target=*/0);
                    tinygltf::Image img;
                    img.mimeType = "image/png";
                    img.bufferView = imgView;
                    // Real source filename, same reasoning as the primary
                    // baseColorImagePng image above -- these are the exact
                    // candidates Luna asked to be able to tell apart in
                    // Blender, so this one matters most.
                    img.name = std::filesystem::path(cand.filename).stem().string();
                    int imgIdx = static_cast<int>(images.size());
                    images.push_back(img);

                    tinygltf::Texture tex;
                    tex.source = imgIdx;
                    tex.name = img.name;
                    texIdx = static_cast<int>(textures.size());
                    textures.push_back(tex);

                    alternateTextureCache.emplace(cand.filename, texIdx);
                }
                candObj["texture_index"] = tinygltf::Value(texIdx);
            }
            candidates.push_back(tinygltf::Value(candObj));
        }
        materialExtras["alternate_textures"] = tinygltf::Value(candidates);
    }

    // Animated tint/fade curves -- diagnostic-only dump, see gltf_mesh.hpp's
    // AnimatedColorCurve/AnimatedScalarCurve doc comments for why this can
    // never become real playback (core glTF has no animation-channel target
    // for a material property).
    if (!mat.tintAnimation.empty()) {
        tinygltf::Value::Array curves;
        for (const auto& c : mat.tintAnimation) {
            tinygltf::Value::Object co;
            if (c.sequenceIndex >= 0) {
                co["sequence_index"] = tinygltf::Value(c.sequenceIndex);
            }
            tinygltf::Value::Array kfs;
            for (const auto& [t, v] : c.keyframes) {
                tinygltf::Value::Object kf;
                kf["time"] = tinygltf::Value(static_cast<double>(t));
                kf["value"] = tinygltf::Value(tinygltf::Value::Array{
                    tinygltf::Value(static_cast<double>(v.x)), tinygltf::Value(static_cast<double>(v.y)),
                    tinygltf::Value(static_cast<double>(v.z))});
                kfs.push_back(tinygltf::Value(kf));
            }
            co["keyframes"] = tinygltf::Value(kfs);
            curves.push_back(tinygltf::Value(co));
        }
        materialExtras["tint_animation"] = tinygltf::Value(curves);
    }

    // Shared by alphaFadeAnimation/weightFadeAnimation just below -- both
    // are Material::AnimatedScalarCurve, only the source M2 field differs
    // (see gltf_mesh.hpp's doc comment).
    auto scalarCurvesToValue = [](const std::vector<Material::AnimatedScalarCurve>& curves) {
        tinygltf::Value::Array out;
        for (const auto& c : curves) {
            tinygltf::Value::Object co;
            if (c.sequenceIndex >= 0) {
                co["sequence_index"] = tinygltf::Value(c.sequenceIndex);
            }
            tinygltf::Value::Array kfs;
            for (const auto& [t, v] : c.keyframes) {
                tinygltf::Value::Object kf;
                kf["time"] = tinygltf::Value(static_cast<double>(t));
                kf["value"] = tinygltf::Value(static_cast<double>(v));
                kfs.push_back(tinygltf::Value(kf));
            }
            co["keyframes"] = tinygltf::Value(kfs);
            out.push_back(tinygltf::Value(co));
        }
        return out;
    };
    if (!mat.alphaFadeAnimation.empty() || !mat.weightFadeAnimation.empty()) {
        tinygltf::Value::Object fadeObj;
        if (!mat.alphaFadeAnimation.empty()) {
            fadeObj["alpha"] = tinygltf::Value(scalarCurvesToValue(mat.alphaFadeAnimation));
        }
        if (!mat.weightFadeAnimation.empty()) {
            fadeObj["weight"] = tinygltf::Value(scalarCurvesToValue(mat.weightFadeAnimation));
        }
        materialExtras["fade_animation"] = tinygltf::Value(fadeObj);
    }

    if (!materialExtras.empty()) {
        tm.extras = tinygltf::Value(materialExtras);
    }
    return tm;
}

}  // namespace

void validateMeshes(const std::vector<NamedMesh>& meshes, bool hasSkeleton) {
    // Per-entry validation, same checks (and same reasons) as writeGlb's
    // single mesh/materials pair -- see gltf.hpp's doc comments.
    for (size_t mi = 0; mi < meshes.size(); ++mi) {
        const Mesh& mesh = meshes[mi].mesh;
        const auto& materials = meshes[mi].materials;
        size_t n = mesh.positions.size();
        if (mesh.normals.size() != n || mesh.texCoords.size() != n) {
            throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s positions (" +
                        std::to_string(n) + "), normals (" + std::to_string(mesh.normals.size()) +
                        "), and texCoords (" + std::to_string(mesh.texCoords.size()) +
                        ") must all be the same length");
        }
        if (!mesh.texCoords2.empty() && mesh.texCoords2.size() != n) {
            throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s texCoords2 (" +
                        std::to_string(mesh.texCoords2.size()) + ") was given but doesn't match "
                        "positions (" + std::to_string(n) + ")");
        }
        if (mesh.primitives.empty()) {
            throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s primitives must not be "
                        "empty");
        }
        for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
            const auto& prim = mesh.primitives[pi];
            if (prim.indices.empty()) {
                throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s primitive " +
                            std::to_string(pi) + " indices must not be empty");
            }
            if (prim.indices.size() % 3 != 0) {
                throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s primitive " +
                            std::to_string(pi) + "'s indices count (" +
                            std::to_string(prim.indices.size()) +
                            ") must be a multiple of 3 (one triangle per 3 entries)");
            }
            for (uint32_t idx : prim.indices) {
                if (idx >= n) {
                    throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s primitive " +
                                std::to_string(pi) + "'s indices reference " + std::to_string(idx) +
                                " but there are only " + std::to_string(n) + " positions");
                }
            }
            if (prim.materialIndex != -1 &&
                (prim.materialIndex < 0 || static_cast<size_t>(prim.materialIndex) >= materials.size())) {
                throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s primitive " +
                            std::to_string(pi) + "'s materialIndex (" +
                            std::to_string(prim.materialIndex) + ") is out of range for " +
                            std::to_string(materials.size()) + " materials");
            }
        }
        if (hasSkeleton && !mesh.skinning.empty() && mesh.skinning.size() != n) {
            throw Error("writeGlbMulti: mesh " + std::to_string(mi) + " has a skeleton but its "
                        "skinning (" + std::to_string(mesh.skinning.size()) +
                        ") doesn't match positions (" + std::to_string(n) + ")");
        }
        if (!hasSkeleton && !mesh.skinning.empty()) {
            throw Error("writeGlbMulti: mesh " + std::to_string(mi) + " has skinning data without "
                        "a skeleton");
        }
    }
}

MeshEmission emitMeshNode(const NamedMesh& nm, bool hasSkeleton, int skinIdx, tinygltf::Buffer& buffer,
                           std::vector<tinygltf::BufferView>& views,
                           std::vector<tinygltf::Accessor>& accessors, std::vector<tinygltf::Image>& images,
                           std::vector<tinygltf::Texture>& textures,
                           std::vector<tinygltf::Material>& tinyMaterials, bool& usedUnlitExtension,
                           std::unordered_map<std::string, int>& alternateTextureCache) {
    const Mesh& mesh = nm.mesh;
    size_t n = mesh.positions.size();
    bool hasTexCoords2 = !mesh.texCoords2.empty();

    int posView = appendBufferView(buffer, views, mesh.positions, TINYGLTF_TARGET_ARRAY_BUFFER);
    int normView = appendBufferView(buffer, views, mesh.normals, TINYGLTF_TARGET_ARRAY_BUFFER);
    int uvView = appendBufferView(buffer, views, mesh.texCoords, TINYGLTF_TARGET_ARRAY_BUFFER);

    Vec3 posMin = mesh.positions[0], posMax = mesh.positions[0];
    for (const auto& p : mesh.positions) {
        posMin.x = std::min(posMin.x, p.x);
        posMin.y = std::min(posMin.y, p.y);
        posMin.z = std::min(posMin.z, p.z);
        posMax.x = std::max(posMax.x, p.x);
        posMax.y = std::max(posMax.y, p.y);
        posMax.z = std::max(posMax.z, p.z);
    }

    tinygltf::Accessor posAcc;
    posAcc.bufferView = posView;
    posAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    posAcc.count = n;
    posAcc.type = TINYGLTF_TYPE_VEC3;
    posAcc.minValues = {posMin.x, posMin.y, posMin.z};
    posAcc.maxValues = {posMax.x, posMax.y, posMax.z};
    int posAccIdx = static_cast<int>(accessors.size());
    accessors.push_back(posAcc);

    tinygltf::Accessor normAcc;
    normAcc.bufferView = normView;
    normAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    normAcc.count = n;
    normAcc.type = TINYGLTF_TYPE_VEC3;
    int normAccIdx = static_cast<int>(accessors.size());
    accessors.push_back(normAcc);

    tinygltf::Accessor uvAcc;
    uvAcc.bufferView = uvView;
    uvAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    uvAcc.count = n;
    uvAcc.type = TINYGLTF_TYPE_VEC2;
    int uvAccIdx = static_cast<int>(accessors.size());
    accessors.push_back(uvAcc);

    int uv2AccIdx = -1;
    if (hasTexCoords2) {
        int uv2View = appendBufferView(buffer, views, mesh.texCoords2, TINYGLTF_TARGET_ARRAY_BUFFER);
        tinygltf::Accessor uv2Acc;
        uv2Acc.bufferView = uv2View;
        uv2Acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        uv2Acc.count = n;
        uv2Acc.type = TINYGLTF_TYPE_VEC2;
        uv2AccIdx = static_cast<int>(accessors.size());
        accessors.push_back(uv2Acc);
    }

    // A NamedMesh with a shared skeleton in scope may still opt out of
    // skinning by leaving `mesh.skinning` empty -- e.g. a collision mesh or
    // other auxiliary geometry parented directly to the scene root, not
    // deformed by the armature (see cmd_export.cpp's collision-mesh
    // export). Validated by validateMeshes above: non-empty skinning must
    // match `n` exactly when a skeleton exists; empty is always allowed.
    bool meshIsSkinned = hasSkeleton && !mesh.skinning.empty();
    int jAccIdx = -1, wAccIdx = -1;
    if (meshIsSkinned) {
        std::vector<std::array<uint8_t, 4>> jointsFlat;
        std::vector<std::array<float, 4>> weightsFlat;
        jointsFlat.reserve(n);
        weightsFlat.reserve(n);
        for (const auto& jw : mesh.skinning) {
            std::array<uint8_t, 4> j;
            std::copy(std::begin(jw.joints), std::end(jw.joints), j.begin());
            jointsFlat.push_back(j);
            std::array<float, 4> w;
            std::copy(std::begin(jw.weights), std::end(jw.weights), w.begin());
            weightsFlat.push_back(w);
        }
        int jointsView = appendBufferView(buffer, views, jointsFlat, TINYGLTF_TARGET_ARRAY_BUFFER);
        int weightsView = appendBufferView(buffer, views, weightsFlat, TINYGLTF_TARGET_ARRAY_BUFFER);

        tinygltf::Accessor jAcc;
        jAcc.bufferView = jointsView;
        jAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        jAcc.count = n;
        jAcc.type = TINYGLTF_TYPE_VEC4;
        jAccIdx = static_cast<int>(accessors.size());
        accessors.push_back(jAcc);

        tinygltf::Accessor wAcc;
        wAcc.bufferView = weightsView;
        wAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        wAcc.count = n;
        wAcc.type = TINYGLTF_TYPE_VEC4;
        wAccIdx = static_cast<int>(accessors.size());
        accessors.push_back(wAcc);
    }

    // Materials/images/textures: appended into the shared, model-wide
    // lists, so a primitive's tinygltf material index has to be offset by
    // however many other meshes' materials came before this one's --
    // `nm.materials`/`mesh.primitives[*].materialIndex` are numbered
    // locally to this one NamedMesh (see gltf_mesh.hpp's doc comment).
    int materialBase = static_cast<int>(tinyMaterials.size());
    for (const auto& mat : nm.materials) {
        tinyMaterials.push_back(emitMaterial(mat, buffer, views, images, textures, uv2AccIdx,
                                              usedUnlitExtension, alternateTextureCache));
    }

    std::vector<tinygltf::Primitive> tinyPrims;
    for (const auto& p : mesh.primitives) {
        int idxView = appendBufferView(buffer, views, p.indices, TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
        tinygltf::Accessor idxAcc;
        idxAcc.bufferView = idxView;
        idxAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        idxAcc.count = p.indices.size();
        idxAcc.type = TINYGLTF_TYPE_SCALAR;
        int idxAccIdx = static_cast<int>(accessors.size());
        accessors.push_back(idxAcc);

        tinygltf::Primitive tp;
        tp.attributes["POSITION"] = posAccIdx;
        tp.attributes["NORMAL"] = normAccIdx;
        tp.attributes["TEXCOORD_0"] = uvAccIdx;
        if (uv2AccIdx >= 0) {
            tp.attributes["TEXCOORD_1"] = uv2AccIdx;
        }
        if (jAccIdx >= 0) {
            tp.attributes["JOINTS_0"] = jAccIdx;
            tp.attributes["WEIGHTS_0"] = wAccIdx;
        }
        tp.indices = idxAccIdx;
        tp.mode = TINYGLTF_MODE_TRIANGLES;
        if (p.materialIndex >= 0) {
            tp.material = materialBase + p.materialIndex;
        }
        // Geoset metadata -- inert glTF extras, see gltf_mesh.hpp's
        // Primitive::skinSectionId doc comment.
        if (p.skinSectionId >= 0) {
            tinygltf::Value::Object extras;
            extras["geoset_id"] = tinygltf::Value(p.skinSectionId);
            extras["geoset_group"] = tinygltf::Value(p.skinSectionId / 100);
            extras["geoset_variant"] = tinygltf::Value(p.skinSectionId % 100);
            tp.extras = tinygltf::Value(extras);
        }
        tinyPrims.push_back(tp);
    }

    MeshEmission out;
    out.mesh.primitives = tinyPrims;

    out.node.name = nm.name;
    // out.node.mesh is filled in by the caller once it knows this mesh's
    // final index in the shared tinyMeshes list (gltf.cpp's writeGlbMulti).
    if (skinIdx >= 0 && meshIsSkinned) {
        out.node.skin = skinIdx;
    }
    if (nm.isCollision) {
        tinygltf::Value::Object extras;
        extras["collision"] = tinygltf::Value(true);
        out.node.extras = tinygltf::Value(extras);
    }
    return out;
}

}  // namespace husk::gltf
