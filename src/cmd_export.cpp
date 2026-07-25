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
// convert WoW's Z-up coordinates to glTF's Y-up. Stage 6 (animation, see
// README.md) additionally resolves M2Sequence + each bone's M2Track<T>
// keyframes into real glTF animation clips -- but only for a model's
// *inline* bones (an external .skel file moves per-bone keyframe data out
// to .anim files husk doesn't parse the content of yet, see
// buildAnimations's doc comment below) and only for sequences whose data
// actually lives in this M2 rather than an external .anim file.
namespace husk::commands {

namespace {

void printUsage() {
    std::cerr
        << "usage: husk export <file.m2> <file.skin>|auto <output.glb> [file.skel]\n"
           "                    [--textures <dir>] [--skin-dir <dir>]\n"
           "\n"
           "Exports a mesh: resolves the M2's vertex array and the .skin\n"
           "file's triangle-index lookup tables, converts WoW's Z-up\n"
           "coordinates to glTF's Y-up, and writes a glTF binary (.glb) --\n"
           "one primitive per .skin batch, with WoW's blend mode/render\n"
           "flags translated to glTF's alphaMode/doubleSided. If the M2\n"
           "has bones, they're exported as a glTF skin; inline bones (not\n"
           "from a separate .skel file) additionally get real animation\n"
           "clips, one per M2Sequence whose keyframe data lives in this M2\n"
           "rather than an external .anim file (which husk doesn't parse\n"
           "the content of yet). Some models (see `husk info`'s output)\n"
           "keep their bones in a separate .skel file instead of inline --\n"
           "pass its path as the optional 4th argument to use those (bind\n"
           "pose only, no animation clips, for that case). husk doesn't\n"
           "resolve texture/skin FileDataIDs to actual\n"
           "BLP/.skin files itself (no CASC/listfile access, see\n"
           "README.md) -- pass --textures <dir> pointing at a directory of\n"
           "already-converted (via husk-blp, see blp/) PNGs named\n"
           "'<FileDataID>.png' to embed real baseColorTexture images;\n"
           "without it, materials still get the right blend mode/culling,\n"
           "just no image. Pass the literal word 'auto' instead of a\n"
           ".skin path, plus --skin-dir <dir> pointing at a directory of\n"
           "already-extracted '<FileDataID>.skin' files, to auto-select\n"
           "the model's highest-detail LOD (M2's SFID chunk) instead of\n"
           "naming a .skin file explicitly.\n";
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

// Converts an M2 bone-rotation quaternion (already decompressed, see
// m2::Quat) from WoW's Z-up space to glTF's Y-up space. Derived (and
// numerically checked against several test rotations, not just asserted)
// from the general rule for re-expressing a rotation under a change of
// basis that is itself a proper rotation (det +1, which zUpToYUp's (X, -Z,
// Y) permutation is): apply the same permutation to the quaternion's
// vector part and leave the scalar part untouched. No wowdev.wiki page
// spells this out explicitly for M2 bone tracks specifically -- this
// wasn't visually verified against a real animated model in a 3D viewer,
// only mathematically (see tests/test_cmd_export.cpp), so treat a first
// real animated .glb as still worth a sanity look in Blender.
gltf::Quat toGltf(const m2::Quat& q) { return {q.x, -q.z, q.y, q.w}; }

// Converts an M2 bone scale vector from Z-up to Y-up. Deliberately *not*
// gltf::zUpToYUp -- that function's sign flip on the (former) Z component
// is correct for a position/direction, but scale is a set of per-axis
// magnitudes (the diagonal of a scale matrix), and conjugating a diagonal
// matrix by a signed-permutation change-of-basis just permutes the
// diagonal entries -- the signs cancel out (checked numerically the same
// way as the quaternion conversion above). Swapping Y and Z, unsigned, is
// the whole conversion.
gltf::Vec3 toGltfScale(const m2::Vec3& s) { return {s.x, s.z, s.y}; }

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

// M2Sequence flags bit meaning "this sequence's keyframe data lives inline
// in the M2 (this bit set) vs. in an external .anim file (unset)" --
// wowdev.wiki M2#Animation_sequences's Flags table, historically named
// "looped animation" by a wiki contributor without a cited source, but its
// actual documented meaning ("If set, the animation data is in the .m2
// file") is what matters here.
constexpr uint32_t kSequenceStoredInlineFlag = 0x20;

// Builds one glTF animation clip per M2Sequence that has its keyframe data
// inline (flags & 0x20 -- everything else's real data is in an external
// .anim file, wowdev.wiki M2#AFID, which husk doesn't parse the content of
// yet), covering every bone that has real (non-empty) translation/
// rotation/scale keyframes for that specific sequence. Deliberately only
// called for a model's *inline* bones (see m2::Bone::translationTrackOffset
// et al.'s doc comment for why a .skel-sourced skeleton's tracks are
// expected to be empty, not wrong, here) -- `skeleton` must be the
// already-built bind-pose Skeleton for these same `bones`, in the same
// order, since each keyframe's translation channel value is bind-pose-
// relative-to-parent (`skeleton.joints[i].localTranslation`) plus the
// animated delta -- glTF's animated translation *replaces* the node's
// translation at sampled times rather than adding to it, so the bind
// offset has to be baked into every keyframe value, not left implicit.
// Sequences with no bone actually carrying inline data for them (a
// zero-length primary sequence, or one this model just doesn't animate any
// bone in) are skipped -- an empty animation clip isn't useful output.
std::vector<gltf::Animation> buildAnimations(const std::vector<uint8_t>& blob,
                                              const std::vector<m2::Bone>& bones,
                                              const gltf::Skeleton& skeleton,
                                              const std::vector<m2::Sequence>& sequences) {
    std::vector<gltf::Animation> animations;

    for (size_t si = 0; si < sequences.size(); ++si) {
        const auto& seq = sequences[si];
        if ((seq.flags & kSequenceStoredInlineFlag) == 0) {
            continue;
        }

        gltf::Animation anim;
        anim.name = "anim_" + std::to_string(seq.id) + "_" + std::to_string(seq.variationIndex);

        for (size_t bi = 0; bi < bones.size(); ++bi) {
            const auto& bone = bones[bi];
            auto translation =
                m2::resolveVec3TrackSequence(blob, bone.translationTrackOffset, static_cast<uint32_t>(si));
            auto rotation =
                m2::resolveQuatTrackSequence(blob, bone.rotationTrackOffset, static_cast<uint32_t>(si));
            auto scale =
                m2::resolveVec3TrackSequence(blob, bone.scaleTrackOffset, static_cast<uint32_t>(si));
            if (translation.empty() && rotation.empty() && scale.empty()) {
                continue;
            }

            gltf::JointAnimation ja;
            ja.joint = static_cast<int>(bi);
            const gltf::Vec3& bindTranslation = skeleton.joints[bi].localTranslation;

            ja.translationTimes.reserve(translation.size());
            ja.translationValues.reserve(translation.size());
            for (const auto& [ts, v] : translation) {
                gltf::Vec3 delta = toGltf(v);
                ja.translationTimes.push_back(static_cast<float>(ts) / 1000.0f);
                ja.translationValues.push_back({bindTranslation.x + delta.x, bindTranslation.y + delta.y,
                                                 bindTranslation.z + delta.z});
            }
            ja.rotationTimes.reserve(rotation.size());
            ja.rotationValues.reserve(rotation.size());
            for (const auto& [ts, q] : rotation) {
                ja.rotationTimes.push_back(static_cast<float>(ts) / 1000.0f);
                ja.rotationValues.push_back(toGltf(q));
            }
            ja.scaleTimes.reserve(scale.size());
            ja.scaleValues.reserve(scale.size());
            for (const auto& [ts, s] : scale) {
                ja.scaleTimes.push_back(static_cast<float>(ts) / 1000.0f);
                ja.scaleValues.push_back(toGltfScale(s));
            }
            anim.joints.push_back(std::move(ja));
        }

        if (!anim.joints.empty()) {
            animations.push_back(std::move(anim));
        }
    }

    return animations;
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

// Everything buildMaterialsAndPrimitives needs out of the M2 itself (as
// opposed to the .skin file's own submeshes/batches) to resolve one batch
// into a real glTF material -- bundled since the M2 side alone is seven
// distinct arrays/optionals by this point (materials/textures/
// textureCombos/textureCoordCombos/colors/textureWeights/
// textureWeightCombos/textureFileDataIds), one call site, no real
// abstraction cost.
struct M2MaterialInputs {
    std::vector<m2::Material> materials;
    std::vector<m2::Texture> textures;
    std::vector<uint16_t> textureCombos;
    // Pre-Cataclysm only (wowdev.wiki M2/.skin#geosetIndex: "Still present
    // but unused in Cataclysm") -- empty in every modern file this was
    // tested against, in which case batch.textureCoordComboIndex is never
    // dereferenced at all and every material just uses UV set 0.
    std::vector<uint16_t> textureCoordCombos;
    std::vector<m2::Color> colors;
    std::vector<m2::TextureWeight> textureWeights;
    std::vector<uint16_t> textureWeightCombos;
    std::optional<std::vector<uint32_t>> textureFileDataIds;
};

// Resolves the .skin file's batches (M2's actual material/texture
// linkage, see src/skin.hpp's Batch doc comment) into one glTF material +
// primitive per batch: batch -> submesh (a slice of `triangleIndices`) ->
// material (blend mode/render flags -> alphaMode/doubleSided, plus a
// static color tint/alpha and texture-weight fade multiplied into
// baseColorFactor -- see m2::Color/TextureWeight's doc comments for why
// this is a bind-time approximation, not real animation) -> texture (via
// the textureCombos lookup table -> M2's textures array -> optionally a
// FileDataID, via TXID, resolved to a real PNG if `texturesDir` is given,
// and which of the mesh's two UV sets to sample it with, via
// textureCoordCombos). If the .skin has no batches at all (e.g. a
// minimal/synthetic fixture, or in principle a genuinely material-less
// model), falls back to stage-1-through-4 behavior: one primitive covering
// every triangle, no material.
BuiltMaterials buildMaterialsAndPrimitives(const std::vector<uint32_t>& triangleIndices,
                                            const std::vector<skin::Submesh>& submeshes,
                                            const std::vector<skin::Batch>& batches,
                                            const M2MaterialInputs& m2,
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

        if (b.materialIndex >= m2.materials.size()) {
            throw std::runtime_error("batch " + std::to_string(bi) + "'s materialIndex (" +
                                      std::to_string(b.materialIndex) + ") is out of range for " +
                                      std::to_string(m2.materials.size()) + " materials");
        }
        const auto& mat = m2.materials[b.materialIndex];

        gltf::Material gm;
        gm.alphaMode = alphaModeForBlend(mat.blendMode);
        gm.doubleSided = (mat.flags & 0x04) != 0;
        gm.name = "batch" + std::to_string(bi) + "_mat" + std::to_string(b.materialIndex);

        // Vertex-color tint + combined alpha/texture-weight fade (static
        // approximation -- see m2::Color/TextureWeight). colorIndex is
        // genuinely optional (0xFFFF/"none" is common and expected);
        // textureWeightComboIndex is not documented as nullable and every
        // real batch this was tested against has a valid one, so it's
        // resolved unconditionally and bounds-checked like any other index.
        if (b.colorIndex != 0xFFFF) {
            if (b.colorIndex >= m2.colors.size()) {
                throw std::runtime_error("batch " + std::to_string(bi) + "'s colorIndex (" +
                                          std::to_string(b.colorIndex) + ") is out of range for " +
                                          std::to_string(m2.colors.size()) + " colors");
            }
            const auto& color = m2.colors[b.colorIndex];
            if (color.color) {
                gm.baseColorFactor[0] = color.color->x;
                gm.baseColorFactor[1] = color.color->y;
                gm.baseColorFactor[2] = color.color->z;
            }
            if (color.alpha) {
                gm.baseColorFactor[3] *= *color.alpha;
            }
        }
        // Like textureCoordCombos above, treat a completely empty table as
        // "this model doesn't use this feature" rather than an error --
        // unlike textureCoordCombos there's no documented version cutoff
        // for this one, but the same defensive reasoning applies: a
        // model-wide absence of transparency-weight data shouldn't turn
        // into every single batch failing to export.
        if (!m2.textureWeightCombos.empty()) {
            if (b.textureWeightComboIndex >= m2.textureWeightCombos.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s textureWeightComboIndex (" +
                    std::to_string(b.textureWeightComboIndex) + ") is out of range for " +
                    std::to_string(m2.textureWeightCombos.size()) + " textureWeightCombos entries");
            }
            uint16_t weightIndex = m2.textureWeightCombos[b.textureWeightComboIndex];
            if (weightIndex >= m2.textureWeights.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s texture weight (index " +
                    std::to_string(weightIndex) + " via textureWeightCombos[" +
                    std::to_string(b.textureWeightComboIndex) + "]) is out of range for " +
                    std::to_string(m2.textureWeights.size()) + " textureWeights entries");
            }
            if (auto weight = m2.textureWeights[weightIndex].weight) {
                gm.baseColorFactor[3] *= *weight;
            }
        }

        if (b.textureCount > 0) {
            if (b.textureComboIndex >= m2.textureCombos.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s textureComboIndex (" +
                    std::to_string(b.textureComboIndex) + ") is out of range for " +
                    std::to_string(m2.textureCombos.size()) + " textureCombos entries");
            }
            uint16_t textureIndex = m2.textureCombos[b.textureComboIndex];
            if (textureIndex >= m2.textures.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s texture (index " +
                    std::to_string(textureIndex) + " via textureCombos[" +
                    std::to_string(b.textureComboIndex) + "]) is out of range for " +
                    std::to_string(m2.textures.size()) + " textures");
            }
            gm.name += "_tex" + std::to_string(textureIndex);

            // Second UV set (roadmap "Second UV set" feature): only
            // meaningful pre-Cataclysm, see M2MaterialInputs::
            // textureCoordCombos's doc comment -- an empty table (every
            // modern file) always means UV set 0.
            if (!m2.textureCoordCombos.empty()) {
                if (b.textureCoordComboIndex >= m2.textureCoordCombos.size()) {
                    throw std::runtime_error(
                        "batch " + std::to_string(bi) + "'s textureCoordComboIndex (" +
                        std::to_string(b.textureCoordComboIndex) + ") is out of range for " +
                        std::to_string(m2.textureCoordCombos.size()) +
                        " textureCoordCombos entries");
                }
                uint16_t mapping = m2.textureCoordCombos[b.textureCoordComboIndex];
                // 0xFFFF (-1) is environment mapping, which has no glTF
                // equivalent -- fall back to UV set 0 rather than guessing.
                if (mapping == 1) {
                    gm.baseColorTexCoord = 1;
                }
            }

            if (m2.textureFileDataIds && textureIndex < m2.textureFileDataIds->size()) {
                uint32_t fdid = (*m2.textureFileDataIds)[textureIndex];
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

// Resolves the literal "auto" .skin path (see printUsage) via the M2's own
// SFID chunk: entry 0 is always "the main skin aka lod0" (wowdev.wiki
// M2#SFID), which is the highest-detail LOD -- the policy roadmap stage 7
// already settled on ("always emit the highest-detail skin profile,
// ignore the rest"). husk doesn't resolve that FileDataID to a WoW/CASC
// path itself (no CASC/listfile access, same non-goal as `--textures`) --
// this only ever looks for `<skinDir>/<FileDataID>.skin` on the local
// filesystem, the same convention `--textures` already uses for PNGs.
std::string resolveAutoSkinPath(const m2::Header& header, const std::string& skinDir,
                                 const std::string& modelPath) {
    if (skinDir.empty()) {
        throw std::runtime_error(
            "'auto' was given for the .skin path but --skin-dir wasn't -- pass --skin-dir <dir> "
            "pointing at a directory of already-extracted '<FileDataID>.skin' files");
    }
    if (!header.skinFileDataIds || header.skinFileDataIds->empty()) {
        throw std::runtime_error("'" + modelPath +
                                  "' has no SFID chunk (or it's empty) -- this M2 doesn't carry "
                                  "skin FileDataIDs to auto-select from (pre-Legion M2s never do) "
                                  "-- pass an explicit .skin path instead of 'auto'");
    }
    uint32_t fileDataId = header.skinFileDataIds->at(0);
    auto path = std::filesystem::path(skinDir) / (std::to_string(fileDataId) + ".skin");
    return path.string();
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
    std::string skinDir;

    // Remaining args: at most one bare positional (.skel path) plus
    // optional --textures/--skin-dir <dir> flags, in any order.
    for (int i = 3; i < argc; ++i) {
        std::string arg = args[i];
        if (arg == "--textures") {
            if (i + 1 >= argc) {
                printUsage();
                return 1;
            }
            texturesDir = args[++i];
        } else if (arg == "--skin-dir") {
            if (i + 1 >= argc) {
                printUsage();
                return 1;
            }
            skinDir = args[++i];
        } else if (skelPath.empty()) {
            skelPath = arg;
        } else {
            printUsage();
            return 1;
        }
    }
    if (!skinDir.empty() && skinPath != "auto") {
        std::cerr << "husk: --skin-dir only does anything when the .skin path is 'auto'\n";
        return 1;
    }

    try {
        auto modelBytes = readFileBytes(modelPath);
        auto header = m2::parseHeader(modelBytes);
        auto blob = m2::extractBlob(modelBytes);
        auto vertices = m2::parseVertices(blob, header.vertices);

        M2MaterialInputs m2Inputs;
        m2Inputs.materials = m2::parseMaterials(blob, header.materials);
        m2Inputs.textures = m2::parseTextures(blob, header.textures);
        m2Inputs.textureCombos = m2::parseUint16Array(blob, header.textureCombos);
        m2Inputs.textureCoordCombos = m2::parseUint16Array(blob, header.textureCoordCombos);
        m2Inputs.colors = m2::parseColors(blob, header.colors);
        m2Inputs.textureWeights = m2::parseTextureWeights(blob, header.textureWeights);
        m2Inputs.textureWeightCombos = m2::parseUint16Array(blob, header.textureWeightCombos);
        m2Inputs.textureFileDataIds = header.textureFileDataIds;

        if (skinPath == "auto") {
            skinPath = resolveAutoSkinPath(header, skinDir, modelPath);
            std::cerr << "husk: note: resolved 'auto' -> '" << skinPath << "' (SFID entry 0, "
                      << "highest-detail LOD)\n";
        }
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
        mesh.texCoords2.reserve(vertices.size());
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
            mesh.texCoords2.push_back({v.texCoords[1].x, v.texCoords[1].y});
        }
        auto built =
            buildMaterialsAndPrimitives(triangleIndices, submeshes, batches, m2Inputs, texturesDir);
        mesh.primitives = built.primitives;

        auto bones = m2::parseBones(blob, header.bones);
        // Only *inline* bones (this same flag's condition) have track
        // offsets relative to `blob` -- a .skel-sourced skeleton's tracks
        // are expected to be empty anyway (see buildAnimations's doc
        // comment), but capturing this before the skel fallback below
        // might overwrite `bones` keeps the intent explicit rather than
        // relying on that emptiness as an implicit signal.
        bool bonesAreInline = !bones.empty();
        if (bonesAreInline && !skelPath.empty()) {
            std::cerr << "husk: note: '" << modelPath << "' has its own inline bones; ignoring '"
                      << skelPath << "'\n";
        } else if (!bonesAreInline && !skelPath.empty()) {
            auto skelBytes = readFileBytes(skelPath);
            bones = skel::parseBones(skelBytes);
        }

        gltf::Skeleton skeleton;
        std::vector<gltf::Animation> animations;
        if (!bones.empty()) {
            skeleton = buildSkeleton(bones);
            mesh.skinning = buildSkinning(vertices, bones.size());
            if (bonesAreInline) {
                auto sequences = m2::parseSequences(blob, header.sequences);
                animations = buildAnimations(blob, bones, skeleton, sequences);
            }
        }

        auto glb =
            gltf::writeGlb(mesh, built.materials, bones.empty() ? nullptr : &skeleton, animations);

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
            std::cout << ", " << bones.size() << " bones";
            if (!animations.empty()) {
                std::cout << ", " << animations.size() << " animation(s)";
            } else {
                std::cout << " (bind pose only, no animation)";
            }
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
