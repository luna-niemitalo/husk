#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "chunk.hpp"
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
// keyframes into real glTF animation clips, for a model's own inline bones
// and (via skel::parseSequences/skel::boneTrackBlob/skel::findAnimFileIds)
// a .skel-sourced skeleton alike -- either way, only for sequences whose
// keyframe data actually resolves: inline, via --anim-dir for an AFM2-shape
// external .anim file, or not at all for a .skel sequence whose external
// data is in the AFSB shape husk doesn't parse yet (see buildAnimations's
// doc comment below).
namespace husk::commands {

namespace {

void printUsage() {
    std::cerr
        << "usage: husk export <file.m2> [<file.skin>|auto [<output.glb> [file.skel]]]\n"
           "                    [--textures <dir>] [--skin-dir <dir>] [--anim-dir <dir>]\n"
           "                    [--lod <n>|all]\n"
           "\n"
           "Only <file.m2> is required -- everything else defaults from what's\n"
           "sitting next to it: an omitted .skin path looks for a same-named\n"
           "'<basename><N>.skin' file in the model's own directory (picking the\n"
           "lowest-numbered/highest-detail one if more than one matches); an\n"
           "omitted output path defaults to '<basename>.glb'; an omitted .skel\n"
           "path (only relevant when the model has 0 inline bones) checks for a\n"
           "same-named '<basename>.skel' next to the model; and --textures/\n"
           "--skin-dir/--anim-dir all default to the model's own directory\n"
           "instead of requiring three separate flags for what's normally the\n"
           "same one directory a real extraction already drops everything\n"
           "into. Every default is overridable by just giving that argument\n"
           "explicitly, and every one of these positionals is trailing-\n"
           "optional (you can stop early, but can't skip one in the middle).\n"
           "\n"
           "Exports a mesh: resolves the M2's vertex array and the .skin\n"
           "file's triangle-index lookup tables, converts WoW's Z-up\n"
           "coordinates to glTF's Y-up, and writes a glTF binary (.glb) --\n"
           "one primitive per .skin batch, with WoW's blend mode/render\n"
           "flags translated to glTF's alphaMode/doubleSided. If the M2\n"
           "has bones, they're exported as a glTF skin, with real\n"
           "animation clips, one per M2Sequence whose keyframe data\n"
           "resolves -- either inline, or via --anim-dir <dir> pointing at\n"
           "a directory of already-extracted '<FileDataID>.anim' files\n"
           "(same local-directory-by-FileDataID convention as\n"
           "--skin-dir/--textures) for sequences whose data lives in an\n"
           "external .anim file instead (husk reads the FileDataID off the\n"
           "model's AFID chunk, it just doesn't fetch the file itself).\n"
           "Some models (see `husk info`'s output) keep their bones in a\n"
           "separate .skel file instead of inline -- pass its path as the\n"
           "optional 4th argument to use those; animation clips still work\n"
           "for that case (own SKS1 sequences/AFID table), except for\n"
           "sequences whose external data lives in a .skel-only 'AFSB'\n"
           ".anim shape husk doesn't parse yet (see README.md). husk doesn't\n"
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
           "naming a .skin file explicitly. --lod only does anything\n"
           "alongside 'auto': --lod <n> picks SFID entry n instead of\n"
           "always 0 (see husk info's SFID listing for how many entries a\n"
           "model has); --lod all resolves every entry and exports one\n"
           "named node per LOD tier ('lod0', 'lod1', ...) in the same\n"
           ".glb, all sharing one skeleton/animation set (every LOD of\n"
           "one M2 draws from the same bones array).\n";
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
        if (const char* mode = m2::billboardModeName(b.flags)) {
            j.billboardMode = mode;
        }
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

// M2Sequence flags bits (wowdev.wiki M2#Animation_sequences's Flags
// table). 0x20, historically named "looped animation" by a wiki
// contributor without a cited source, actually means "the animation data
// is in the .m2 file" -- unset means external. 0x40 ("has next / is
// alias") layered on top of an unset 0x20 is a case the wiki itself
// admits it doesn't understand ("stored... somewhere. I have no clue.") --
// sequences in that state are skipped entirely, not guessed at.
constexpr uint32_t kSequenceStoredInlineFlag = 0x20;
constexpr uint32_t kSequenceAliasFlag = 0x40;

// Everything buildAnimations needs to resolve a sequence's keyframes from
// an external .anim file, when its data isn't inline -- bundled for the
// same reason M2MaterialInputs is (a handful of related inputs, one call
// site). `animDir` is the same local-directory-by-FileDataID convention
// `--skin-dir`/`--textures` already use -- husk doesn't resolve a FileDataID
// to a CASC path itself, so a missing file here is treated as "skip this
// sequence" (see buildAnimations), not an error.
struct M2AnimInputs {
    std::optional<std::vector<m2::Header::AnimFileEntry>> animFileIds;
    // header.globalFlags & 0x200000 -- wowdev.wiki's flag_unk_0x200000,
    // "apparently: use 24500 upgraded model format: chunked .anim files".
    // See m2::extractAnimBlob's doc comment for the caveat: this is
    // implemented from that description, not yet verified against a real
    // chunked .anim file.
    bool animChunked = false;
    std::string animDir;
};

// Finds the FileDataID for sequence (animId, subAnimId) in `animFileIds`,
// or 0 ("none", same sentinel AnimFileEntry::fileId itself uses) if there's
// no matching entry, or the matching entry's own fileId is 0.
uint32_t findAnimFileId(const std::vector<m2::Header::AnimFileEntry>& animFileIds, uint16_t animId,
                         uint16_t subAnimId) {
    for (const auto& e : animFileIds) {
        if (e.animId == animId && e.subAnimId == subAnimId && e.fileId != 0) {
            return e.fileId;
        }
    }
    return 0;
}

// Builds one glTF animation clip per M2Sequence that has resolvable
// keyframe data -- either inline (flags & 0x20) or, when `animInputs`
// resolves one, in an external .anim file (see M2AnimInputs, findAnimFileId,
// m2::extractAnimBlob) -- covering every bone that has real (non-empty)
// translation/rotation/scale keyframes for that specific sequence. Works
// equally for a model's own inline bones+sequences (`blob` = the MD20 blob,
// `sequences` from m2::parseSequences) and a .skel-sourced skeleton (`blob`
// = skel::boneTrackBlob's SKB1 payload, `sequences` from
// skel::parseSequences, `animInputs.animFileIds` from skel::findAnimFileIds
// -- a .skel's own AFID table, not the owning M2's, see skel.hpp) -- the
// M2Track outer-array-indexed-by-sequence-position convention (and the
// external-.anim-file mechanism) is the same either way, verified against a
// real bloodelffemale_hd.m2/.skel pair (see skel.hpp's doc comment). The
// only case this can't resolve is a .skel sequence whose external data
// lives in an AFSB-chunked .anim file, a format wowdev.wiki documents no
// byte layout for at all (unlike AFM2's "identical to the flat format"
// one-liner) -- skipped below, same policy as a missing file. `skeleton`
// must be the already-built bind-pose Skeleton for these same `bones`, in
// the same order, since each keyframe's translation channel value is
// bind-pose-relative-to-parent (`skeleton.joints[i].localTranslation`) plus
// the animated delta -- glTF's animated translation *replaces* the node's
// translation at sampled times rather than adding to it, so the bind offset
// has to be baked into every keyframe value, not left implicit. Sequences
// with no bone actually carrying resolvable data for them (a zero-length
// primary sequence, one this model just doesn't animate any bone in, an
// external sequence whose .anim file isn't available in
// `animInputs.animDir`, or one in the unhandled AFSB shape above) are
// skipped -- an empty animation clip isn't useful output. A malformed .anim
// file (bad chunk framing, or keyframe data claiming more than the file
// actually holds) throws rather than being silently skipped -- same
// "foreign data that doesn't fit its own claims is an error, not a
// best-effort" policy as every other parser in this codebase; only a
// *missing* file, or the AFSB case, is treated as "husk doesn't have this
// one," consistent with --textures/--skin-dir.
std::vector<gltf::Animation> buildAnimations(const std::vector<uint8_t>& blob,
                                              const std::vector<m2::Bone>& bones,
                                              const gltf::Skeleton& skeleton,
                                              const std::vector<m2::Sequence>& sequences,
                                              const M2AnimInputs& animInputs) {
    std::vector<gltf::Animation> animations;

    for (size_t si = 0; si < sequences.size(); ++si) {
        const auto& seq = sequences[si];

        // Keeps a loaded external .anim blob alive for this sequence's
        // iteration -- externalBlob, when set, points into this.
        std::vector<uint8_t> loadedAnimBlob;
        const std::vector<uint8_t>* externalBlob = nullptr;

        if ((seq.flags & kSequenceStoredInlineFlag) != 0) {
            // Inline -- externalBlob stays null, resolves against `blob`.
        } else if ((seq.flags & kSequenceAliasFlag) != 0) {
            continue;  // wowdev.wiki: "I have no clue" where this lives.
        } else {
            if (animInputs.animDir.empty() || !animInputs.animFileIds) {
                continue;
            }
            uint32_t fileId = findAnimFileId(*animInputs.animFileIds, seq.id, seq.variationIndex);
            if (fileId == 0) {
                continue;
            }
            auto animPath =
                std::filesystem::path(animInputs.animDir) / (std::to_string(fileId) + ".anim");
            std::ifstream f(animPath, std::ios::binary);
            if (!f) {
                continue;  // not available locally -- same skip policy as --textures
            }
            std::vector<uint8_t> animFileBytes((std::istreambuf_iterator<char>(f)),
                                                std::istreambuf_iterator<char>());
            if (animInputs.animChunked) {
                // Peek at the top-level chunks before handing off to
                // extractAnimBlob (which only knows AFM2): a .skel-sourced
                // model's .anim files were found, against real data, to
                // carry an AFSB chunk (wowdev.wiki has no documented byte
                // layout for this at all, unlike AFM2's "identical to the
                // flat format" one-liner) either alongside a small
                // (64-byte, real files) AFM2 chunk or alone -- and that
                // small AFM2 "stub" does NOT hold the real per-bone track
                // data (confirmed the hard way: resolving against it throws
                // a real "claims more keyframes than this blob holds"
                // bounds error, not silently wrong data, but still not
                // useful output) -- an AFSB chunk being present at all
                // means husk can't use this file, regardless of whether
                // AFM2 also is. Skip it, same "husk doesn't have this one"
                // policy as a missing --anim-dir file, rather than letting
                // a downstream bounds check throw over a shape it was
                // never told to expect.
                auto topChunks = readChunks(animFileBytes.data(), animFileBytes.size());
                if (findChunk(topChunks, "AFSB") || !findChunk(topChunks, "AFM2")) {
                    continue;
                }
            }
            loadedAnimBlob = m2::extractAnimBlob(animFileBytes, animInputs.animChunked);
            externalBlob = &loadedAnimBlob;
        }

        gltf::Animation anim;
        anim.name = "anim_" + std::to_string(seq.id) + "_" + std::to_string(seq.variationIndex);

        for (size_t bi = 0; bi < bones.size(); ++bi) {
            const auto& bone = bones[bi];
            auto translation = m2::resolveVec3TrackSequence(blob, bone.translationTrackOffset,
                                                              static_cast<uint32_t>(si), externalBlob);
            auto rotation = m2::resolveQuatTrackSequence(blob, bone.rotationTrackOffset,
                                                          static_cast<uint32_t>(si), externalBlob);
            auto scale = m2::resolveVec3TrackSequence(blob, bone.scaleTrackOffset,
                                                       static_cast<uint32_t>(si), externalBlob);
            if (translation.empty() && rotation.empty() && scale.empty()) {
                continue;
            }

            gltf::JointAnimation ja;
            ja.joint = static_cast<int>(bi);
            const gltf::Vec3& bindTranslation = skeleton.joints[bi].localTranslation;
            // interpolation_type 0 ("step: values change instantly at the
            // timestamp, with no interpolation whatsoever" -- wowdev.wiki
            // M2#Interpolation) needs glTF's STEP sampler, not its default
            // LINEAR -- each of translation/rotation/scale is a separate
            // M2Track with its own independent type. Reading this doesn't
            // touch the timestamps/values arrays resolveVec3TrackSequence/
            // resolveQuatTrackSequence already validated above, just the
            // two fields before them (see m2::TrackMeta).
            ja.translationStep = m2::readTrackMeta(blob, bone.translationTrackOffset)
                                      .interpolationType == 0;
            ja.rotationStep =
                m2::readTrackMeta(blob, bone.rotationTrackOffset).interpolationType == 0;
            ja.scaleStep = m2::readTrackMeta(blob, bone.scaleTrackOffset).interpolationType == 0;

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

// M2Material flags (wowdev.wiki M2#Render_flags_and_blending_modes). Only
// 0x04 (two-sided) was translated before; 0x01 (unlit) is the other bit
// with a real glTF equivalent (KHR_materials_unlit) -- a material rendered
// without directional lighting in the real client (glow/eye-effect layers,
// some UI-attached models) would otherwise come out looking normally-lit in
// any glTF consumer, a real visible mismatch, not just missing metadata.
// depthTest/depthWrite (0x08/0x10) have no core-glTF equivalent at all (no
// per-material depth-state override in the spec) so aren't translated --
// surfaced as raw `flags` on m2::Material for a consumer that wants them.
constexpr uint16_t kMaterialUnlitFlag = 0x01;
constexpr uint16_t kMaterialTwoSidedFlag = 0x04;

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
        gm.doubleSided = (mat.flags & kMaterialTwoSidedFlag) != 0;
        gm.unlit = (mat.flags & kMaterialUnlitFlag) != 0;
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

// Shared by every resolveAutoSkinPaths mode below: husk's own non-goal (no
// CASC/listfile access) means a model with no SFID chunk at all has no
// FileDataIDs to auto-select from, 'all'/--lod alike.
const std::vector<uint32_t>& requireSkinFileDataIds(const m2::Header& header,
                                                      const std::string& modelPath) {
    if (!header.skinFileDataIds || header.skinFileDataIds->empty()) {
        throw std::runtime_error("'" + modelPath +
                                  "' has no SFID chunk (or it's empty) -- this M2 doesn't carry "
                                  "skin FileDataIDs to auto-select from (pre-Legion M2s never do) "
                                  "-- pass an explicit .skin path instead of 'auto'");
    }
    return *header.skinFileDataIds;
}

// Resolves the literal "auto" .skin path (see printUsage) via the M2's own
// SFID chunk, honoring an optional --lod selection (`lodArg`, "" if not
// given). "": the roadmap-stage-7 policy this always followed before --lod
// existed -- SFID entry 0, "the main skin aka lod0" (wowdev.wiki M2#SFID),
// the highest-detail LOD. "<n>": SFID entry n instead (0-based), letting a
// caller deliberately ask for a lower-detail tier LDV1's lodCount says
// exists but husk never picked before. "all": every entry, so
// husk export can emit one glTF node per LOD tier in a single .glb (see
// exportGlb) instead of just one. Each result pairs the entry's own index
// (for node naming -- "" for the "" case, since that's still one unnamed
// mesh, same as before --lod existed) with its resolved local path. husk
// doesn't resolve any of these FileDataIDs to a WoW/CASC path itself (no
// CASC/listfile access, same non-goal as `--textures`) -- this only ever
// looks for `<skinDir>/<FileDataID>.skin` on the local filesystem, the same
// convention `--textures` already uses for PNGs.
std::vector<std::pair<std::string, std::string>> resolveAutoSkinPaths(const m2::Header& header,
                                                                        const std::string& skinDir,
                                                                        const std::string& modelPath,
                                                                        const std::string& lodArg) {
    if (skinDir.empty()) {
        throw std::runtime_error(
            "'auto' was given for the .skin path but --skin-dir wasn't -- pass --skin-dir <dir> "
            "pointing at a directory of already-extracted '<FileDataID>.skin' files");
    }
    const auto& ids = requireSkinFileDataIds(header, modelPath);
    auto pathFor = [&](size_t index) {
        return (std::filesystem::path(skinDir) / (std::to_string(ids[index]) + ".skin")).string();
    };

    if (lodArg == "all") {
        std::vector<std::pair<std::string, std::string>> result;
        result.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            result.emplace_back("lod" + std::to_string(i), pathFor(i));
        }
        return result;
    }

    size_t index = 0;
    if (!lodArg.empty()) {
        // Deliberately strict: std::stoul silently accepts leading
        // whitespace/a leading sign and stops at the first non-digit rather
        // than requiring the whole argument to be numeric -- reject
        // anything it would otherwise quietly half-parse (e.g. "3abc"),
        // same "foreign data that doesn't fit its own claims is an error"
        // policy as every parser in this codebase (--lod counts as one,
        // even though it's a CLI argument rather than file bytes).
        if (!std::all_of(lodArg.begin(), lodArg.end(), [](unsigned char c) { return std::isdigit(c); })) {
            throw std::runtime_error("--lod '" + lodArg + "' isn't 'all' or a non-negative integer");
        }
        index = static_cast<size_t>(std::stoul(lodArg));
    }
    if (index >= ids.size()) {
        std::string lodCountNote;
        if (header.lodCount) {
            lodCountNote = " (LDV1 lod_count: " + std::to_string(*header.lodCount) + ")";
        }
        throw std::runtime_error("--lod " + std::to_string(index) + " is out of range -- '" +
                                  modelPath + "'s SFID chunk only has " + std::to_string(ids.size()) +
                                  " skin FileDataID(s)" + lodCountNote);
    }
    return {{lodArg.empty() ? "" : "lod" + std::to_string(index), pathFor(index)}};
}

// Scans `modelPath`'s own directory for files named exactly
// `<model-basename><digits>.skin` (e.g. "bloodelffemale00.skin" for
// "bloodelffemale.m2") -- the naming convention a real casc-tool-style
// extraction actually produces (see README.md's Usage section), as opposed
// to `resolveAutoSkinPaths`'s FileDataID-renamed-directory convention.
// Deliberately stricter than a plain string-prefix match: the character
// immediately after the basename must be a digit, which is what excludes a
// real, dangerous false match this project's own test data contains --
// "bloodelffemale_hd00.skin" belongs to a *separate*, much-higher-poly
// model (bloodelffemale_hd.m2), not this one, even though it does start
// with "bloodelffemale" as a plain string. Returns every match found,
// sorted by that numeric suffix ascending -- 0 is always "the main skin
// aka lod0" (wowdev.wiki M2#SFID), the highest-detail LOD, the same
// "pick the most-detailed one" policy `--skin-dir`'s auto-select already
// follows. Empty if `modelPath`'s directory doesn't exist or has no match.
std::vector<std::pair<int, std::string>> findSameBasenameSkins(const std::string& modelPath) {
    std::filesystem::path model(modelPath);
    std::string baseName = model.stem().string();  // e.g. "bloodelffemale"
    std::filesystem::path dir = model.parent_path();
    if (dir.empty()) dir = ".";

    std::vector<std::pair<int, std::string>> found;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return found;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.size() <= baseName.size() || name.compare(0, baseName.size(), baseName) != 0) {
            continue;
        }
        size_t digitsStart = baseName.size();
        size_t pos = digitsStart;
        while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos]))) ++pos;
        if (pos == digitsStart) continue;  // no digit right after the basename
        if (name.compare(pos, name.size() - pos, ".skin") != 0) continue;
        int lod = std::stoi(name.substr(digitsStart, pos - digitsStart));
        found.emplace_back(lod, entry.path().string());
    }
    std::sort(found.begin(), found.end());
    return found;
}

}  // namespace

int exportGlb(int argc, char** args) {
    if (argc < 1) {
        printUsage();
        return 1;
    }

    std::string modelPath = args[0];
    // Up to 3 bare positionals after the model, in order: .skin path,
    // output .glb path, .skel path -- each one trailing-optional (you can
    // stop early, but can't skip one in the middle without a placeholder),
    // same convention as e.g. `cp src [dst]`. Omitted ones default once the
    // model's own header/directory are known (see below): an empty
    // `skinPath` triggers findSameBasenameSkins; an empty `outputPath`
    // defaults to the model's own basename + ".glb"; an empty `skelPath`
    // (only relevant when the model's own inline bones are empty) checks
    // for a same-basename ".skel" next to the model. Every existing
    // 2/3/4-bare-positional invocation is completely unaffected -- this
    // only adds new, shorter ones.
    std::vector<std::string> positionals;
    std::string texturesDir;
    std::string skinDir;
    std::string animDir;
    std::string lodArg;

    for (int i = 1; i < argc; ++i) {
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
        } else if (arg == "--anim-dir") {
            if (i + 1 >= argc) {
                printUsage();
                return 1;
            }
            animDir = args[++i];
        } else if (arg == "--lod") {
            if (i + 1 >= argc) {
                printUsage();
                return 1;
            }
            lodArg = args[++i];
        } else if (positionals.size() < 3) {
            positionals.push_back(arg);
        } else {
            printUsage();
            return 1;
        }
    }
    std::string skinPath = positionals.size() > 0 ? positionals[0] : "";
    std::string outputPath = positionals.size() > 1 ? positionals[1] : "";
    std::string skelPath = positionals.size() > 2 ? positionals[2] : "";

    if (!skinDir.empty() && skinPath != "auto") {
        std::cerr << "husk: --skin-dir only does anything when the .skin path is 'auto'\n";
        return 1;
    }
    if (!lodArg.empty() && skinPath != "auto") {
        std::cerr << "husk: --lod only does anything when the .skin path is 'auto'\n";
        return 1;
    }

    if (outputPath.empty()) {
        outputPath = std::filesystem::path(modelPath).replace_extension(".glb").string();
        std::cerr << "husk: note: no output path given -- writing to '" << outputPath << "'\n";
    }
    // --textures/--skin-dir/--anim-dir all default to the model's own
    // directory when not given explicitly -- a real casc-tool-style
    // extraction drops the .m2 and every sidecar it needs (BLP-converted
    // PNGs, .skin/.anim files) into one directory together (see
    // README.md's Usage section), so requiring three separate flags for
    // what's normally the same one directory was pure friction. An explicit
    // flag still overrides this for the FileDataID-renamed-directory
    // workflow (see resolveAutoSkinPaths). This runs after the --skin-dir/
    // 'auto' mismatch checks above so defaulting skinDir here can't mask a
    // real usage error.
    {
        std::filesystem::path modelDir = std::filesystem::path(modelPath).parent_path();
        std::string modelDirStr = modelDir.empty() ? "." : modelDir.string();
        if (texturesDir.empty()) texturesDir = modelDirStr;
        if (skinDir.empty()) skinDir = modelDirStr;
        if (animDir.empty()) animDir = modelDirStr;
    }

    try {
        auto modelBytes = readFileBytes(modelPath);
        auto header = m2::parseHeader(modelBytes);
        auto blob = m2::extractBlob(modelBytes);
        auto vertices = m2::parseVertices(blob, header.vertices);

        if (skinPath.empty()) {
            auto candidates = findSameBasenameSkins(modelPath);
            if (candidates.empty()) {
                throw std::runtime_error(
                    "no .skin path given, and no same-named '<model-basename><N>.skin' file found "
                    "next to '" +
                    modelPath +
                    "' -- pass one explicitly, or 'auto' + --skin-dir <dir> for the "
                    "FileDataID-renamed-directory convention");
            }
            skinPath = candidates.front().second;
            std::cerr << "husk: note: no .skin path given -- resolved '" << skinPath << "'";
            if (candidates.size() > 1) {
                std::cerr << " (lowest-numbered of " << candidates.size()
                          << " same-basename .skin files found next to the model)";
            }
            std::cerr << "\n";
        }

        M2MaterialInputs m2Inputs;
        m2Inputs.materials = m2::parseMaterials(blob, header.materials);
        m2Inputs.textures = m2::parseTextures(blob, header.textures);
        m2Inputs.textureCombos = m2::parseUint16Array(blob, header.textureCombos);
        m2Inputs.textureCoordCombos = m2::parseUint16Array(blob, header.textureCoordCombos);
        m2Inputs.colors = m2::parseColors(blob, header.colors);
        m2Inputs.textureWeights = m2::parseTextureWeights(blob, header.textureWeights);
        m2Inputs.textureWeightCombos = m2::parseUint16Array(blob, header.textureWeightCombos);
        m2Inputs.textureFileDataIds = header.textureFileDataIds;

        // One (node name, .skin path) pair per LOD tier to export -- just
        // one, unnamed ("lod" only appears in a node name once --lod
        // resolves more than a single entry, see resolveAutoSkinPaths), for
        // every case except 'auto' + --lod all.
        std::vector<std::pair<std::string, std::string>> skinsToExport;
        if (skinPath == "auto") {
            skinsToExport = resolveAutoSkinPaths(header, skinDir, modelPath, lodArg);
            for (const auto& [name, path] : skinsToExport) {
                std::cerr << "husk: note: resolved 'auto' -> '" << path << "'"
                          << (name.empty() ? " (SFID entry 0, highest-detail LOD)\n"
                                            : " (SFID, " + name + ")\n");
            }
        } else {
            skinsToExport.emplace_back("", skinPath);
        }

        // Base mesh geometry -- the M2's own global vertex list, shared by
        // every LOD tier below (a .skin file only ever selects a *subset* of
        // it via its own vertices/indices two-level lookup, see
        // src/skin.hpp; it never adds vertices of its own), built once
        // rather than once per LOD.
        gltf::Mesh baseMesh;
        baseMesh.positions.reserve(vertices.size());
        baseMesh.normals.reserve(vertices.size());
        baseMesh.texCoords.reserve(vertices.size());
        baseMesh.texCoords2.reserve(vertices.size());
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
            baseMesh.positions.push_back(toGltf(v.pos));
            baseMesh.normals.push_back(toGltf(v.normal));
            baseMesh.texCoords.push_back({v.texCoords[0].x, v.texCoords[0].y});
            baseMesh.texCoords2.push_back({v.texCoords[1].x, v.texCoords[1].y});
        }

        auto bones = m2::parseBones(blob, header.bones);
        // Only *inline* bones (this same flag's condition) have track
        // offsets relative to `blob` -- a .skel-sourced skeleton's bone
        // track offsets are relative to the .skel's own SKB1 payload
        // instead (skelBytes/skelTrackBlob below), so capturing this before
        // the skel fallback might overwrite `bones` keeps the intent
        // explicit rather than relying on which blob's in play as an
        // implicit signal.
        bool bonesAreInline = !bones.empty();
        std::vector<uint8_t> skelBytes;
        bool haveSkel = false;
        if (bonesAreInline && !skelPath.empty()) {
            std::cerr << "husk: note: '" << modelPath << "' has its own inline bones; ignoring '"
                      << skelPath << "'\n";
        } else if (!bonesAreInline && skelPath.empty()) {
            // No .skel path given, and this model's own inline bones are
            // empty -- check for a same-basename .skel next to the model
            // (the README's own worked example is exactly this shape:
            // bloodelffemale_hd.m2 + bloodelffemale_hd.skel, same
            // directory). Not finding one isn't an error: plenty of 0-bone
            // models genuinely have no skeleton at all, and this model
            // falls back to the same unskinned-mesh output it always did
            // when no .skel was given.
            auto defaultSkel = std::filesystem::path(modelPath).replace_extension(".skel");
            std::error_code ec;
            if (std::filesystem::exists(defaultSkel, ec) && !ec) {
                skelPath = defaultSkel.string();
                std::cerr << "husk: note: '" << modelPath
                          << "' has 0 inline bones -- found and using '" << skelPath
                          << "' next to it\n";
                skelBytes = readFileBytes(skelPath);
                haveSkel = true;
                bones = skel::parseBones(skelBytes);
            }
        } else if (!bonesAreInline && !skelPath.empty()) {
            skelBytes = readFileBytes(skelPath);
            haveSkel = true;
            bones = skel::parseBones(skelBytes);
        }

        gltf::Skeleton skeleton;
        std::vector<gltf::Animation> animations;
        if (!bones.empty()) {
            skeleton = buildSkeleton(bones);
            baseMesh.skinning = buildSkinning(vertices, bones.size());
            if (bonesAreInline) {
                auto sequences = m2::parseSequences(blob, header.sequences);
                M2AnimInputs animInputs;
                animInputs.animFileIds = header.animFileIds;
                animInputs.animChunked = (header.globalFlags & 0x200000) != 0;
                animInputs.animDir = animDir;
                animations = buildAnimations(blob, bones, skeleton, sequences, animInputs);
            } else if (haveSkel && findChunk(readChunks(skelBytes.data(), skelBytes.size()), "SKS1")) {
                // Same buildAnimations, pointed at the .skel's own blob
                // (SKB1's payload, which is what `bones`'s track offsets
                // are relative to -- see skel::boneTrackBlob) and its own
                // sequences/AFID table instead of the M2's (see
                // buildAnimations's doc comment). Peeking for SKS1 first,
                // rather than just calling skel::parseSequences and letting
                // its ParseError propagate, treats "this .skel has no
                // sequences at all" (plausible for a non-animated model,
                // and skel::findAnimFileIds already returns nullopt rather
                // than throwing for the symmetric "no AFID" case) as
                // "no animation clips available," not export failure.
                auto skelTrackBlob = skel::boneTrackBlob(skelBytes);
                auto sequences = skel::parseSequences(skelBytes);
                M2AnimInputs animInputs;
                animInputs.animFileIds = skel::findAnimFileIds(skelBytes);
                animInputs.animChunked = (header.globalFlags & 0x200000) != 0;
                animInputs.animDir = animDir;
                animations = buildAnimations(skelTrackBlob, bones, skeleton, sequences, animInputs);
            }
        }

        // One NamedMesh per LOD tier: each resolves its own .skin file's
        // triangle-index lookup/submeshes/batches (see src/skin.hpp) into
        // its own primitives/materials, but reuses baseMesh's shared
        // positions/normals/texCoords/texCoords2/skinning as-is -- see the
        // comment above baseMesh's construction for why that's valid.
        std::vector<gltf::NamedMesh> namedMeshes;
        namedMeshes.reserve(skinsToExport.size());
        for (const auto& [name, path] : skinsToExport) {
            auto skinBytes = readFileBytes(path);
            auto skinHeader = skin::parseHeader(skinBytes);
            auto triangleIndices = skin::resolveTriangleIndices(skinBytes, skinHeader);
            auto submeshes = skin::parseSubmeshes(skinBytes, skinHeader.submeshes);
            auto batches = skin::parseBatches(skinBytes, skinHeader.batches);

            // Cross-module boundary check: skin::resolveTriangleIndices only
            // validates indices against the skin file's own `vertices`
            // array -- it has no idea how many vertices the M2 actually
            // has. A skin file that doesn't belong to this M2 (wrong LOD,
            // wrong model) shows up here as an out-of-range global vertex
            // index.
            for (uint32_t idx : triangleIndices) {
                if (idx >= vertices.size()) {
                    throw std::runtime_error("'" + path + "' references M2 vertex " +
                                              std::to_string(idx) + " but '" + modelPath +
                                              "' only has " + std::to_string(vertices.size()) +
                                              " vertices -- model/.skin mismatch?");
                }
            }

            auto built = buildMaterialsAndPrimitives(triangleIndices, submeshes, batches, m2Inputs,
                                                       texturesDir);

            gltf::NamedMesh nm;
            nm.name = name;
            nm.mesh = baseMesh;
            nm.mesh.primitives = built.primitives;
            nm.materials = std::move(built.materials);
            namedMeshes.push_back(std::move(nm));
        }

        auto glb = gltf::writeGlbMulti(namedMeshes, bones.empty() ? nullptr : &skeleton, animations);

        std::ofstream out(outputPath, std::ios::binary);
        if (!out) {
            throw std::runtime_error("couldn't open '" + outputPath + "' for writing");
        }
        out.write(reinterpret_cast<const char*>(glb.data()),
                  static_cast<std::streamsize>(glb.size()));
        if (!out) {
            throw std::runtime_error("error writing '" + outputPath + "'");
        }

        if (namedMeshes.size() == 1) {
            size_t triCount = 0;
            for (const auto& p : namedMeshes[0].mesh.primitives) triCount += p.indices.size() / 3;
            std::cout << outputPath << ": " << vertices.size() << " vertices, " << triCount
                      << " triangles";
            if (!bones.empty()) {
                std::cout << ", " << bones.size() << " bones";
                if (!animations.empty()) {
                    std::cout << ", " << animations.size() << " animation(s)";
                } else {
                    std::cout << " (bind pose only, no animation)";
                }
            }
            if (!namedMeshes[0].materials.empty()) {
                size_t withImage = 0;
                for (const auto& m : namedMeshes[0].materials) {
                    if (!m.baseColorImagePng.empty()) ++withImage;
                }
                std::cout << ", " << namedMeshes[0].materials.size() << " materials (" << withImage
                          << " with an embedded texture)";
            }
            std::cout << "\n";
        } else {
            std::cout << outputPath << ": " << vertices.size() << " vertices (shared), "
                      << namedMeshes.size() << " LOD tier(s) as separate nodes:\n";
            for (const auto& nm : namedMeshes) {
                size_t triCount = 0;
                for (const auto& p : nm.mesh.primitives) triCount += p.indices.size() / 3;
                std::cout << "  " << nm.name << ": " << triCount << " triangles, "
                          << nm.materials.size() << " materials\n";
            }
            if (!bones.empty()) {
                std::cout << "  " << bones.size() << " bones (shared)";
                if (!animations.empty()) {
                    std::cout << ", " << animations.size() << " animation(s) (shared)";
                } else {
                    std::cout << " (bind pose only, no animation)";
                }
                std::cout << "\n";
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "husk: export failed: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace husk::commands
