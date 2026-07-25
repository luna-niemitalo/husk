#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "commands.hpp"
#include "gltf.hpp"
#include "m2.hpp"
#include "skel.hpp"
#include "skin.hpp"

// Roadmap stages 1-5 (see README.md): stage 1 resolves an M2's vertex array
// plus a .skin file's two-level triangle-index lookup (see src/skin.hpp)
// into a static mesh; stage 2 additionally resolves the `bones` array into
// a bind-pose glTF skin, wiring M2Vertex's bone_weights/bone_indices into
// JOINTS_0/WEIGHTS_0; stage 3 covers the case where those bones live in an
// external .skel file instead (see src/skel.hpp); stage 5 resolves the
// .skin file's per-submesh batches (M2's materials/textures arrays, see
// src/skin.hpp's Batch) into one glTF material + primitive per batch, with
// WoW's blend mode translated to glTF's alphaMode and (optionally, via
// --textures) a real baseColorTexture image embedded. All of the above
// convert WoW's Z-up coordinates to glTF's Y-up. No animation playback yet
// -- that's roadmap stage 6.
namespace husk::commands {

namespace {

void printUsage() {
    std::cerr
        << "usage: husk export <file.m2> <file.skin> <output.glb> [file.skel]\n"
           "                    [--textures <dir>]\n"
           "\n"
           "Exports a mesh: resolves the M2's vertex array and the .skin\n"
           "file's triangle-index lookup tables, converts WoW's Z-up\n"
           "coordinates to glTF's Y-up, and writes a glTF binary (.glb) --\n"
           "one primitive per .skin batch, with WoW's blend mode/render\n"
           "flags translated to glTF's alphaMode/doubleSided. If the M2\n"
           "has bones, they're exported as a bind-pose glTF skin (no\n"
           "animation playback yet). Some models (see `husk info`'s\n"
           "output) keep their bones in a separate .skel file instead of\n"
           "inline -- pass its path as the optional 4th argument to use\n"
           "those. husk doesn't resolve texture FileDataIDs to actual BLP\n"
           "files itself (no CASC/listfile access, see README.md) -- pass\n"
           "--textures <dir> pointing at a directory of already-converted\n"
           "(via husk-blp, see blp/) PNGs named '<FileDataID>.png' to\n"
           "embed real baseColorTexture images; without it, materials\n"
           "still get the right blend mode/culling, just no image.\n";
}

std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("couldn't open '" + path + "' for reading");
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw std::runtime_error("error reading '" + path + "'");
    }
    return bytes;
}

gltf::Vec3 toGltf(const m2::Vec3& v) { return gltf::zUpToYUp({v.x, v.y, v.z}); }

bool isFinite(const m2::Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Detects a cycle in the bones' parent chains (bone A's parent is B, B's
// parent is A, or any longer loop). No single parentBone bounds check can
// catch this -- every individual index in a cycle is perfectly in-range
// (see FAILURES.md #3) -- so this walks each joint's parent chain
// separately, memoizing finished (acyclic) nodes so the whole pass stays
// O(joints) instead of O(joints^2). A real (not just hand-crafted) way to
// hit this: a .skel file that doesn't actually belong to the M2 it's
// passed alongside -- the same mismatch category the model/.skin
// vertex-count cross-check exists to catch, just for bones instead of
// vertices. Assumes every joint's `parent` is already bounds-checked
// (either -1 or a valid index) -- callers must validate that first.
void checkNoBoneCycles(const std::vector<gltf::Skeleton::Joint>& joints) {
    enum class State { kUnvisited, kInProgress, kDone };
    std::vector<State> state(joints.size(), State::kUnvisited);

    for (size_t start = 0; start < joints.size(); ++start) {
        if (state[start] == State::kDone) continue;

        std::vector<size_t> path;
        size_t cur = start;
        while (true) {
            if (state[cur] == State::kDone) break;
            if (state[cur] == State::kInProgress) {
                throw std::runtime_error(
                    "bone " + std::to_string(cur) +
                    "'s parent chain loops back on itself -- not a valid bind-pose skeleton "
                    "(wrong .skel paired with this model?)");
            }
            state[cur] = State::kInProgress;
            path.push_back(cur);
            int parent = joints[cur].parent;
            if (parent == -1) break;
            cur = static_cast<size_t>(parent);
        }
        for (size_t idx : path) state[idx] = State::kDone;
    }
}

// Builds a bind-pose Skeleton from M2's bones array: `bone.parentBone` is a
// direct index into the same bones array (-1 for a root), and each joint's
// local (parent-relative) translation is just the difference of the two
// bones' absolute pivots -- valid because M2's bind pose has no baked
// rotation/scale (see gltf::Skeleton's doc comment). Throws
// std::runtime_error if any bone's parentBone is out of range.
gltf::Skeleton buildSkeleton(const std::vector<m2::Bone>& bones) {
    gltf::Skeleton skeleton;
    skeleton.joints.reserve(bones.size());
    for (const auto& b : bones) {
        gltf::Skeleton::Joint j;
        j.parent = b.parentBone;
        j.globalPosition = toGltf(b.pivot);
        skeleton.joints.push_back(j);
    }

    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        auto& j = skeleton.joints[i];
        if (j.parent == -1) {
            j.localTranslation = j.globalPosition;
            continue;
        }
        if (j.parent < 0 || static_cast<size_t>(j.parent) >= skeleton.joints.size()) {
            throw std::runtime_error("bone " + std::to_string(i) + "'s parent (" +
                                      std::to_string(j.parent) + ") is out of range for " +
                                      std::to_string(skeleton.joints.size()) + " bones");
        }
        const gltf::Vec3& parentPos = skeleton.joints[static_cast<size_t>(j.parent)].globalPosition;
        j.localTranslation = {j.globalPosition.x - parentPos.x, j.globalPosition.y - parentPos.y,
                               j.globalPosition.z - parentPos.z};
    }
    checkNoBoneCycles(skeleton.joints);
    return skeleton;
}

// Lifts M2Vertex's raw bone_weights[4]/bone_indices[4] into glTF's
// JOINTS_0/WEIGHTS_0 shape: weights normalized from 0-255 to 0.0-1.0,
// joint indices copied verbatim (M2Vertex.bone_indices are direct indices
// into the M2's `bones` array, confirmed against pywowlib's M2 writer --
// NOT indices into the .skin file's own, differently-indirected `bones`
// lookup table). Throws std::runtime_error if any index is out of range
// for `boneCount`.
std::vector<gltf::JointWeights> buildSkinning(const std::vector<m2::Vertex>& vertices,
                                               size_t boneCount) {
    std::vector<gltf::JointWeights> skinning;
    skinning.reserve(vertices.size());
    for (size_t vi = 0; vi < vertices.size(); ++vi) {
        const auto& v = vertices[vi];
        gltf::JointWeights jw;
        for (int j = 0; j < 4; ++j) {
            if (v.boneIndices[j] >= boneCount) {
                throw std::runtime_error("vertex " + std::to_string(vi) + "'s bone_indices[" +
                                          std::to_string(j) + "] (" +
                                          std::to_string(v.boneIndices[j]) +
                                          ") is out of range for " + std::to_string(boneCount) +
                                          " bones");
            }
            jw.joints[j] = v.boneIndices[j];
            jw.weights[j] = v.boneWeights[j] / 255.0f;
        }
        skinning.push_back(jw);
    }
    return skinning;
}

// WoW's M2BLEND_* blend modes (wowdev.wiki M2/Rendering#M2BLEND) collapsed
// to glTF's three-way alphaMode: 0 (OPAQUE) maps directly, 1 (ALPHA_KEY,
// alpha-tested) maps to MASK, and everything else -- 2 (a real alpha
// blend), plus the additive/multiply modes 3+ that glTF's core material
// model has no equivalent for -- maps to BLEND as the closest
// approximation. Not an attempt at faithfully reproducing additive
// rendering, see README.md roadmap stage 5.
gltf::Material::AlphaMode alphaModeForBlend(uint16_t blendMode) {
    switch (blendMode) {
        case 0: return gltf::Material::AlphaMode::Opaque;
        case 1: return gltf::Material::AlphaMode::Mask;
        default: return gltf::Material::AlphaMode::Blend;
    }
}

struct BuiltMaterials {
    std::vector<gltf::Material> materials;
    std::vector<gltf::Primitive> primitives;
};

// Resolves the .skin file's batches (M2's actual material/texture
// linkage, see src/skin.hpp's Batch doc comment) into one glTF material +
// primitive per batch: batch -> submesh (a slice of `triangleIndices`) ->
// material (blend mode/render flags -> alphaMode/doubleSided) -> texture
// (via the textureCombos lookup table -> M2's textures array -> optionally
// a FileDataID, via TXID, resolved to a real PNG if `texturesDir` is
// given). If the .skin has no batches at all (e.g. a minimal/synthetic
// fixture, or in principle a genuinely material-less model), falls back to
// stage-1-through-4 behavior: one primitive covering every triangle, no
// material.
BuiltMaterials buildMaterialsAndPrimitives(const std::vector<uint32_t>& triangleIndices,
                                            const std::vector<skin::Submesh>& submeshes,
                                            const std::vector<skin::Batch>& batches,
                                            const std::vector<m2::Material>& m2Materials,
                                            const std::vector<m2::Texture>& m2Textures,
                                            const std::vector<uint16_t>& textureCombos,
                                            const std::optional<std::vector<uint32_t>>& textureFileDataIds,
                                            const std::string& texturesDir) {
    BuiltMaterials result;

    if (batches.empty()) {
        gltf::Primitive prim;
        prim.indices = triangleIndices;
        result.primitives.push_back(std::move(prim));
        return result;
    }

    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.skinSectionIndex >= submeshes.size()) {
            throw std::runtime_error("batch " + std::to_string(bi) + "'s skinSectionIndex (" +
                                      std::to_string(b.skinSectionIndex) +
                                      ") is out of range for " + std::to_string(submeshes.size()) +
                                      " submeshes");
        }
        const auto& sm = submeshes[b.skinSectionIndex];
        if (static_cast<size_t>(sm.indexStart) + sm.indexCount > triangleIndices.size()) {
            throw std::runtime_error(
                "submesh " + std::to_string(b.skinSectionIndex) +
                "'s index range runs past the end of the resolved triangle-index buffer -- "
                "corrupted .skin?");
        }

        gltf::Primitive prim;
        prim.indices.assign(triangleIndices.begin() + sm.indexStart,
                             triangleIndices.begin() + sm.indexStart + sm.indexCount);

        if (b.materialIndex >= m2Materials.size()) {
            throw std::runtime_error("batch " + std::to_string(bi) + "'s materialIndex (" +
                                      std::to_string(b.materialIndex) + ") is out of range for " +
                                      std::to_string(m2Materials.size()) + " materials");
        }
        const auto& mat = m2Materials[b.materialIndex];

        gltf::Material gm;
        gm.alphaMode = alphaModeForBlend(mat.blendMode);
        gm.doubleSided = (mat.flags & 0x04) != 0;
        gm.name = "batch" + std::to_string(bi) + "_mat" + std::to_string(b.materialIndex);

        if (b.textureCount > 0) {
            if (b.textureComboIndex >= textureCombos.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s textureComboIndex (" +
                    std::to_string(b.textureComboIndex) + ") is out of range for " +
                    std::to_string(textureCombos.size()) + " textureCombos entries");
            }
            uint16_t textureIndex = textureCombos[b.textureComboIndex];
            if (textureIndex >= m2Textures.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s texture (index " +
                    std::to_string(textureIndex) + " via textureCombos[" +
                    std::to_string(b.textureComboIndex) + "]) is out of range for " +
                    std::to_string(m2Textures.size()) + " textures");
            }
            gm.name += "_tex" + std::to_string(textureIndex);

            if (textureFileDataIds && textureIndex < textureFileDataIds->size()) {
                uint32_t fdid = (*textureFileDataIds)[textureIndex];
                if (fdid != 0) {
                    gm.name += "_fdid" + std::to_string(fdid);
                    if (!texturesDir.empty()) {
                        auto pngPath =
                            std::filesystem::path(texturesDir) / (std::to_string(fdid) + ".png");
                        std::ifstream f(pngPath, std::ios::binary);
                        if (f) {
                            gm.baseColorImagePng.assign(std::istreambuf_iterator<char>(f),
                                                         std::istreambuf_iterator<char>());
                        }
                    }
                }
            }
        }

        prim.materialIndex = static_cast<int>(result.materials.size());
        result.materials.push_back(std::move(gm));
        result.primitives.push_back(std::move(prim));
    }

    return result;
}

}  // namespace

int exportGlb(int argc, char** args) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    std::string modelPath = args[0];
    std::string skinPath = args[1];
    std::string outputPath = args[2];
    std::string skelPath;
    std::string texturesDir;

    // Remaining args: at most one bare positional (.skel path) plus an
    // optional --textures <dir> flag, in either order.
    for (int i = 3; i < argc; ++i) {
        std::string arg = args[i];
        if (arg == "--textures") {
            if (i + 1 >= argc) {
                printUsage();
                return 1;
            }
            texturesDir = args[++i];
        } else if (skelPath.empty()) {
            skelPath = arg;
        } else {
            printUsage();
            return 1;
        }
    }

    try {
        auto modelBytes = readFileBytes(modelPath);
        auto header = m2::parseHeader(modelBytes);
        auto blob = m2::extractBlob(modelBytes);
        auto vertices = m2::parseVertices(blob, header.vertices);

        auto textures = m2::parseTextures(blob, header.textures);
        auto materials = m2::parseMaterials(blob, header.materials);
        auto textureCombos = m2::parseUint16Array(blob, header.textureCombos);

        auto skinBytes = readFileBytes(skinPath);
        auto skinHeader = skin::parseHeader(skinBytes);
        auto triangleIndices = skin::resolveTriangleIndices(skinBytes, skinHeader);
        auto submeshes = skin::parseSubmeshes(skinBytes, skinHeader.submeshes);
        auto batches = skin::parseBatches(skinBytes, skinHeader.batches);

        // Cross-module boundary check: skin::resolveTriangleIndices only
        // validates indices against the skin file's own `vertices` array --
        // it has no idea how many vertices the M2 actually has. A skin file
        // that doesn't belong to this M2 (wrong LOD, wrong model) shows up
        // here as an out-of-range global vertex index.
        for (uint32_t idx : triangleIndices) {
            if (idx >= vertices.size()) {
                throw std::runtime_error(
                    "'" + skinPath + "' references M2 vertex " + std::to_string(idx) + " but '" +
                    modelPath + "' only has " + std::to_string(vertices.size()) +
                    " vertices -- model/.skin mismatch?");
            }
        }

        gltf::Mesh mesh;
        mesh.positions.reserve(vertices.size());
        mesh.normals.reserve(vertices.size());
        mesh.texCoords.reserve(vertices.size());
        for (size_t vi = 0; vi < vertices.size(); ++vi) {
            const auto& v = vertices[vi];
            // glTF requires finite POSITION/NORMAL values (and their
            // accessor min/max); a NaN/Inf here is a real symptom of a
            // corrupted read or truncated file, not valid mesh data (see
            // FAILURES.md #4) -- catch it here, where the offending
            // vertex index is still known, rather than downstream.
            if (!isFinite(v.pos) || !isFinite(v.normal)) {
                throw std::runtime_error("vertex " + std::to_string(vi) +
                                          " has a non-finite (NaN/Inf) position or normal -- "
                                          "corrupted read or truncated file?");
            }
            mesh.positions.push_back(toGltf(v.pos));
            mesh.normals.push_back(toGltf(v.normal));
            mesh.texCoords.push_back({v.texCoords[0].x, v.texCoords[0].y});
        }
        auto built = buildMaterialsAndPrimitives(triangleIndices, submeshes, batches, materials,
                                                  textures, textureCombos,
                                                  header.textureFileDataIds, texturesDir);
        mesh.primitives = built.primitives;

        auto bones = m2::parseBones(blob, header.bones);
        if (!bones.empty() && !skelPath.empty()) {
            std::cerr << "husk: note: '" << modelPath << "' has its own inline bones; ignoring '"
                      << skelPath << "'\n";
        } else if (bones.empty() && !skelPath.empty()) {
            auto skelBytes = readFileBytes(skelPath);
            bones = skel::parseBones(skelBytes);
        }

        gltf::Skeleton skeleton;
        if (!bones.empty()) {
            skeleton = buildSkeleton(bones);
            mesh.skinning = buildSkinning(vertices, bones.size());
        }

        auto glb = gltf::writeGlb(mesh, built.materials, bones.empty() ? nullptr : &skeleton);

        std::ofstream out(outputPath, std::ios::binary);
        if (!out) {
            throw std::runtime_error("couldn't open '" + outputPath + "' for writing");
        }
        out.write(reinterpret_cast<const char*>(glb.data()),
                  static_cast<std::streamsize>(glb.size()));
        if (!out) {
            throw std::runtime_error("error writing '" + outputPath + "'");
        }

        std::cout << outputPath << ": " << vertices.size() << " vertices, "
                  << (triangleIndices.size() / 3) << " triangles";
        if (!bones.empty()) {
            std::cout << ", " << bones.size() << " bones (bind pose only, no animation)";
        }
        if (!built.materials.empty()) {
            size_t withImage = 0;
            for (const auto& m : built.materials) {
                if (!m.baseColorImagePng.empty()) ++withImage;
            }
            std::cout << ", " << built.materials.size() << " materials (" << withImage
                      << " with an embedded texture)";
        }
        std::cout << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "husk: export failed: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace husk::commands
