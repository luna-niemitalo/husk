#include "gltf_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

// Real KHR_texture_transform equivalent (offset/rotation/scale, pivoting
// around (0,0)) for a *constant* M2TextureTransform, whose rotation pivots
// around the texture's own center (0.5, 0.5) instead (wowdev.wiki
// M2#Texture_Transforms). Full derivation, real-fixture hand verification,
// and the 20,000-trial randomized cross-check against the real client's own
// matrix composition: DESIGN.md's Key design decisions, "A batch's
// M2TextureTransform...". Load-bearing summary for reading the code below:
// pivot = (0.5, 0.5); M = R*S; offset = R*S*translation + R*t_S + t_R,
// where t_S = pivot - S*pivot and t_R = pivot - R*pivot.
struct KhrTextureTransform {
    double offsetU = 0, offsetV = 0;
    double rotation = 0;
    double scaleU = 1, scaleV = 1;
};

// Same epsilon find_texture_transform_files.py's is_identity_rotation uses.
constexpr double kPlanarRotationEpsilon = 1e-4;

// Returns nullopt when the quaternion isn't a pure rotation about the UV
// plane's normal (x/y components beyond floating-point noise) --
// KHR_texture_transform only has a single scalar (Z) rotation, so a
// genuinely 3-axis rotation here (never seen in real corpus data so far)
// has no honest equivalent and stays extras-only rather than being
// silently misrepresented.
std::optional<KhrTextureTransform> textureTransformToKhr(const Material::TextureTransform& xf) {
    double qx = xf.rotation[0], qy = xf.rotation[1], qz = xf.rotation[2], qw = xf.rotation[3];
    if (std::abs(qx) > kPlanarRotationEpsilon || std::abs(qy) > kPlanarRotationEpsilon) {
        return std::nullopt;
    }
    double theta = 2.0 * std::atan2(qz, qw);
    double c = std::cos(theta), s = std::sin(theta);
    double sx = xf.scaling.x, sy = xf.scaling.y;
    double tx = xf.translation.x, ty = xf.translation.y;

    // t_S, t_R (see the struct's own doc comment for the formula legend).
    double tSx = 0.5 * (1 - sx), tSy = 0.5 * (1 - sy);
    double rcx = 0.5 * c - 0.5 * s, rcy = 0.5 * s + 0.5 * c;
    double tRx = 0.5 - rcx, tRy = 0.5 - rcy;
    // R*S*translation
    double rsx = c * sx * tx - s * sy * ty;
    double rsy = s * sx * tx + c * sy * ty;
    // R*t_S
    double rtsx = c * tSx - s * tSy;
    double rtsy = s * tSx + c * tSy;

    KhrTextureTransform out;
    out.offsetU = rsx + rtsx + tRx;
    out.offsetV = rsy + rtsy + tRy;
    out.rotation = theta;
    out.scaleU = sx;
    out.scaleV = sy;
    return out;
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
                                 bool& usedUnlitExtension, bool& usedTextureTransformExtension,
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

        // Real KHR_texture_transform: only for the constant case (the
        // animated case has no honest equivalent -- see
        // gltf_mesh.hpp's TextureTransform doc comment) and only once
        // there's a real baseColorTexture to attach the extension to.
        // The raw resolved values stay in materialExtras below regardless
        // (diagnostic / animated-case fallback), so this is additive, not
        // a replacement.
        if (mat.textureTransform && mat.textureTransform->constant) {
            if (auto khr = textureTransformToKhr(*mat.textureTransform)) {
                tinygltf::Value::Object khrObj;
                khrObj["offset"] = tinygltf::Value(
                    tinygltf::Value::Array{tinygltf::Value(khr->offsetU), tinygltf::Value(khr->offsetV)});
                khrObj["rotation"] = tinygltf::Value(khr->rotation);
                khrObj["scale"] = tinygltf::Value(
                    tinygltf::Value::Array{tinygltf::Value(khr->scaleU), tinygltf::Value(khr->scaleV)});
                tm.pbrMetallicRoughness.baseColorTexture.extensions["KHR_texture_transform"] =
                    tinygltf::Value(khrObj);
                usedTextureTransformExtension = true;
            }
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

    // UV scroll/rotate/scale animation (M2TextureTransform) -- raw resolved
    // values, always present when a batch references one, regardless of
    // whether the constant-case real KHR_texture_transform above applied
    // (planarity check failed, or no baseColorTexture to attach it to) --
    // a diagnostic fallback and the only representation at all for the
    // animated case, see gltf_mesh.hpp's TextureTransform doc comment.
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
                           bool& usedTextureTransformExtension,
                           std::unordered_map<std::string, int>& alternateTextureCache,
                           const std::unordered_map<int, uint32_t>& geosetTagJointIndex) {
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
    int jAccIdx = -1, wAccIdx = -1, j1AccIdx = -1, w1AccIdx = -1;
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

        // Geoset tag joints (Skeleton::GeosetTag / GEOSET_MASK_TODO.md): a
        // second JOINTS_1/WEIGHTS_1 set weaving each vertex into every
        // distinct geoset tag a primitive referencing it maps to (via
        // Primitive::skinSectionId -> geosetTagJointIndex) -- a vertex
        // shared at a geoset seam can genuinely belong to more than one,
        // capped at 4 (glTF's own per-set influence limit; real M2 submesh
        // splits don't produce 5-way vertex sharing in practice, so a 5th
        // distinct tag on one vertex is silently dropped rather than
        // plumbing a JOINTS_2/WEIGHTS_2 set for a case never seen).
        // UNSIGNED_SHORT (not UNSIGNED_BYTE, unlike JOINTS_0): a tag
        // joint's skin-relative index is realJointCount + tagIndex, which
        // exceeds 255 on real models with 200+ bones and 50+ geosets (e.g.
        // bloodelffemale_hd.m2: 245 + 114 = 359).
        //
        // glTF requires a vertex's *combined* weight across every joint
        // set to sum to 1.0, not each set independently (a real
        // gltf-validator run caught this: WEIGHTS_0 alone already sums to
        // 1.0, so naively adding a full second 1.0 in WEIGHTS_1 produces a
        // combined 2.0 -- "non-normalized sum" errors on both accessors).
        // Fixed by rescaling *both* sets down by the same factor for any
        // tagged vertex so the combined total is exactly 1.0 again --
        // provably a no-op on the actual Blender deformation, not just a
        // paperwork fix: Blender's Armature modifier was verified
        // (GEOSET_MASK_TODO.md) to renormalize total influence weight
        // across every joint set at evaluation time regardless of what's
        // stored, so 0.5/0.5 deforms identically to an unscaled 1.0/1.0 --
        // this only changes what's written to the file, not what Blender
        // ever computes from it.
        std::vector<std::array<uint16_t, 4>> joints1Flat;
        std::vector<std::array<float, 4>> weights1Flat;
        if (!geosetTagJointIndex.empty()) {
            std::vector<std::vector<uint32_t>> vertexTags(n);
            for (const auto& p : mesh.primitives) {
                if (p.skinSectionId < 0) continue;
                auto it = geosetTagJointIndex.find(p.skinSectionId);
                if (it == geosetTagJointIndex.end()) continue;
                uint32_t tagJoint = it->second;
                for (uint32_t idx : p.indices) {
                    auto& tags = vertexTags[idx];
                    if (std::find(tags.begin(), tags.end(), tagJoint) == tags.end() && tags.size() < 4) {
                        tags.push_back(tagJoint);
                    }
                }
            }
            bool anyTagged = std::any_of(vertexTags.begin(), vertexTags.end(),
                                          [](const auto& t) { return !t.empty(); });
            if (anyTagged) {
                joints1Flat.assign(n, {0, 0, 0, 0});
                weights1Flat.assign(n, {0, 0, 0, 0});
                for (size_t vi = 0; vi < n; ++vi) {
                    const auto& tags = vertexTags[vi];
                    if (tags.empty()) continue;
                    float tagWeight = 1.0f / static_cast<float>(tags.size());
                    for (size_t k = 0; k < tags.size(); ++k) {
                        joints1Flat[vi][k] = static_cast<uint16_t>(tags[k]);
                        weights1Flat[vi][k] = tagWeight;
                    }
                    float total = tagWeight * static_cast<float>(tags.size());
                    for (float w : weightsFlat[vi]) total += w;
                    if (total <= 0.0f) continue;
                    for (float& w : weightsFlat[vi]) w /= total;
                    for (float& w : weights1Flat[vi]) w /= total;
                }
            }
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

        if (!joints1Flat.empty()) {
            int j1View = appendBufferView(buffer, views, joints1Flat, TINYGLTF_TARGET_ARRAY_BUFFER);
            int w1View = appendBufferView(buffer, views, weights1Flat, TINYGLTF_TARGET_ARRAY_BUFFER);

            tinygltf::Accessor j1Acc;
            j1Acc.bufferView = j1View;
            j1Acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
            j1Acc.count = n;
            j1Acc.type = TINYGLTF_TYPE_VEC4;
            j1AccIdx = static_cast<int>(accessors.size());
            accessors.push_back(j1Acc);

            tinygltf::Accessor w1Acc;
            w1Acc.bufferView = w1View;
            w1Acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
            w1Acc.count = n;
            w1Acc.type = TINYGLTF_TYPE_VEC4;
            w1AccIdx = static_cast<int>(accessors.size());
            accessors.push_back(w1Acc);
        }
    }

    // Materials/images/textures: appended into the shared, model-wide
    // lists, so a primitive's tinygltf material index has to be offset by
    // however many other meshes' materials came before this one's --
    // `nm.materials`/`mesh.primitives[*].materialIndex` are numbered
    // locally to this one NamedMesh (see gltf_mesh.hpp's doc comment).
    int materialBase = static_cast<int>(tinyMaterials.size());
    for (const auto& mat : nm.materials) {
        tinyMaterials.push_back(emitMaterial(mat, buffer, views, images, textures, uv2AccIdx,
                                              usedUnlitExtension, usedTextureTransformExtension,
                                              alternateTextureCache));
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
        if (j1AccIdx >= 0) {
            tp.attributes["JOINTS_1"] = j1AccIdx;
            tp.attributes["WEIGHTS_1"] = w1AccIdx;
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
