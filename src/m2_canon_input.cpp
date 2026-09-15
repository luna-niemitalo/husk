#include "m2_canon_input.hpp"

#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

#include "m2_animation.hpp"        // readTrackMeta, TrackMeta::kNoGlobalSequence
#include "m2_animation_input.hpp"
#include "m2_material_input.hpp"
#include "m2_mesh_input.hpp"
#include "m2_skeleton_input.hpp"

namespace husk::m2input {

namespace {

// Private to export_animation.cpp's anonymous namespace (see that file's
// own comment on the wowdev.wiki M2#Animation_sequences Flags table) --
// reimplemented here rather than exported, same one-bit-test-not-worth-a-
// shared-header tradeoff every other canon_*_builder.cpp in this codebase
// already accepts for a private production constant.
constexpr uint32_t kSequenceStoredInlineFlag = 0x20;
constexpr uint32_t kSequenceAliasFlag = 0x40;

bool isRealInlineSequence(const m2::Sequence& seq) { return (seq.flags & kSequenceStoredInlineFlag) != 0; }

bool isPureAliasSequence(const m2::Sequence& seq) {
    return (seq.flags & kSequenceStoredInlineFlag) == 0 && (seq.flags & kSequenceAliasFlag) != 0;
}

// Mirrors commands::resolveAliasChain (export_animation.cpp) exactly --
// same bounded-hop-count defensiveness against a cyclic or out-of-range
// aliasNext chain in real (foreign) file data, reimplemented rather than
// called since that function lives in commands:: and this file already
// reimplements the one bit test (kSequenceAliasFlag) it depends on.
size_t resolveAliasChain(const std::vector<m2::Sequence>& sequences, size_t startIndex) {
    size_t cur = startIndex;
    for (size_t hop = 0; hop <= sequences.size(); ++hop) {
        if ((sequences[cur].flags & kSequenceAliasFlag) == 0) {
            return cur;
        }
        uint16_t next = sequences[cur].aliasNext;
        if (next >= sequences.size()) {
            throw std::runtime_error("sequence " + std::to_string(cur) + "'s aliasNext (" +
                                      std::to_string(next) + ") is out of range for " +
                                      std::to_string(sequences.size()) + " sequences");
        }
        cur = next;
    }
    throw std::runtime_error("sequence " + std::to_string(startIndex) +
                              "'s aliasNext chain didn't reach a non-alias sequence within " +
                              std::to_string(sequences.size()) + " hops (cycle?)");
}

// Every distinct global-sequence index actually referenced by any of
// `bones`' translation/rotation/scale tracks -- mirrors
// buildGlobalSequenceAnimations's own discovery exactly (export_animation.cpp):
// does NOT iterate Header::globalLoops itself, since an entry with no bone
// track pointing at it would produce an empty, useless clip either way.
std::set<uint16_t> globalSequenceIndices(const std::vector<uint8_t>& blob, const std::vector<m2::Bone>& bones) {
    std::set<uint16_t> indices;
    for (const auto& bone : bones) {
        for (uint32_t off : {bone.translationTrackOffset, bone.rotationTrackOffset, bone.scaleTrackOffset}) {
            uint16_t gs = m2::readTrackMeta(blob, off).globalSequence;
            if (gs != m2::TrackMeta::kNoGlobalSequence) {
                indices.insert(gs);
            }
        }
    }
    return indices;
}

M2MaterialInputs toMaterialInputs(const m2::Model& model) {
    M2MaterialInputs m2in;
    m2in.materials = model.materials;
    m2in.textures = model.textures;
    m2in.textureCombos = model.textureCombos;
    m2in.textureCoordCombos = model.textureCoordCombos;
    m2in.colors = model.colors;
    m2in.textureWeights = model.textureWeights;
    m2in.textureWeightCombos = model.textureWeightCombos;
    m2in.textureTransforms = model.textureTransforms;
    m2in.textureTransformCombos = model.textureTransformCombos;
    m2in.blob = &model.blob;
    return m2in;
}

// Every real M2 field a batch's own material CONTENT actually depends on
// (given a model-wide-constant sequenceIndex/textureResolutions, which
// don't vary per batch) -- see buildCanonModel's own doc comment for why
// this tuple, not a post-hoc hash of the built canon::Material, is the
// dedup key. skinSectionIndex/shaderId are deliberately excluded: the
// former only selects which submesh a batch draws (mesh concern, not
// material content), and the latter isn't read by assembleMaterial at all
// today (ShadingFunction decoding is separate, later work per
// m2_material_input.hpp's own doc comment).
using MaterialKey = std::tuple<uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t>;

MaterialKey materialKeyFor(const skin::Batch& b) {
    return {b.materialIndex,          b.textureCount,           b.textureComboIndex, b.textureCoordComboIndex,
            b.colorIndex,             b.textureWeightComboIndex, b.textureTransformComboIndex};
}

}  // namespace

canon::Model buildCanonModel(const m2::Model& model, const std::vector<skin::Batch>& batches,
                              const std::vector<skin::Submesh>& submeshes,
                              const std::vector<uint32_t>& triangleIndices,
                              const ExternalAnimBlobs& externalAnimBlobs,
                              const TextureResolutions& textureResolutions,
                              const ExternalSkeletonSource* externalSkeleton) {
    // Skeleton + bone-animation source: model's own inline data, or the
    // caller-supplied .skel triple -- see ExternalSkeletonSource's own doc
    // comment for why mesh geometry (model.vertices) and material tables
    // (m2in.blob, below) stay model-sourced regardless.
    const std::vector<m2::Bone>& bones = externalSkeleton ? externalSkeleton->bones : model.bones;
    const std::vector<m2::Sequence>& sequences = externalSkeleton ? externalSkeleton->sequences : model.sequences;
    const std::vector<uint8_t>& blob = externalSkeleton ? externalSkeleton->blob : model.blob;

    canon::Skeleton skeleton = assembleSkeleton(bones);
    canon::Mesh mesh = assembleMesh(model.vertices, bones.size(), batches, submeshes, triangleIndices);

    // Animations, pass 1: one AnimationClip per sequence that is either
    // real inline OR has a caller-supplied external blob (externalAnimBlobs,
    // ExternalAnimBlobs' own doc comment in this file's header) -- see
    // ExternalAnimBlobs' own doc comment for the full three-pass breakdown
    // this mirrors from buildAnimations/buildGlobalSequenceAnimations
    // (export_animation.cpp). A pure-alias sequence is skipped here
    // unconditionally, even if it has its own externalAnimBlobs entry --
    // aliases resolve exclusively through pass 3 below, against their
    // terminal's index. resolvedClipIndexBySequence lets pass 3 (aliases)
    // find and reuse an already-assembled clip's boneCurves by the
    // ORIGINAL model.sequences index, not the result animations vector's
    // own position.
    //
    // A sequence where NO bone has any real keyframe data (every
    // assembleBoneAnimation call below returns nullopt) produces no clip --
    // mirrors buildAnimations's own `if (!anim.joints.empty())` gate
    // (export_animation.cpp) exactly.
    std::vector<canon::AnimationClip> animations;
    std::unordered_map<size_t, size_t> resolvedClipIndexBySequence;
    for (size_t si = 0; si < sequences.size(); ++si) {
        const auto& seq = sequences[si];
        const std::vector<uint8_t>* externalBlob = nullptr;
        if (!isRealInlineSequence(seq)) {
            if (isPureAliasSequence(seq)) continue;  // resolved via its terminal in pass 3
            auto blobIt = externalAnimBlobs.find(static_cast<uint32_t>(si));
            if (blobIt == externalAnimBlobs.end()) continue;  // no inline data, no external blob supplied
            externalBlob = &blobIt->second;
        }

        canon::AnimationClip clip;
        clip.sequence = canon::SequenceRef::sequence(static_cast<uint32_t>(si));
        clip.boneCurves.reserve(skeleton.joints.size());
        bool anyBoneAnimated = false;
        for (size_t bi = 0; bi < bones.size(); ++bi) {
            auto curves = assembleBoneAnimation(blob, bones[bi], bi, static_cast<uint32_t>(si), externalBlob);
            anyBoneAnimated |= curves.has_value();
            clip.boneCurves.push_back(std::move(curves));
        }
        if (!anyBoneAnimated) continue;
        resolvedClipIndexBySequence.emplace(si, animations.size());
        animations.push_back(std::move(clip));
    }
    // Captured here, before passes 2/3 append global-sequence/alias clips
    // -- see materialSequenceIndex's own comment below for why reading
    // animations.front() after those passes run would be wrong. May be a
    // real-inline OR an external-blob-sourced sequence's index -- pass 1
    // above no longer distinguishes the two, and neither does this pick.
    std::optional<uint32_t> firstResolvedSequenceIndex =
        animations.empty() ? std::nullopt : std::make_optional(animations.front().sequence.index);

    // Animations, pass 2: one AnimationClip per distinct global sequence
    // actually referenced by any bone track (globalSequenceIndices above),
    // independent of any M2Sequence -- mirrors
    // buildGlobalSequenceAnimations's own per-bone gating exactly.
    for (uint16_t gs : globalSequenceIndices(blob, bones)) {
        canon::AnimationClip clip;
        clip.sequence = canon::SequenceRef::globalSequence(gs);
        clip.boneCurves.reserve(skeleton.joints.size());
        bool anyBoneAnimated = false;
        for (size_t bi = 0; bi < bones.size(); ++bi) {
            auto curves = assembleBoneAnimationGlobal(blob, bones[bi], bi, gs);
            anyBoneAnimated |= curves.has_value();
            clip.boneCurves.push_back(std::move(curves));
        }
        if (anyBoneAnimated) {
            animations.push_back(std::move(clip));
        }
    }

    // Animations, pass 3: a pure-alias M2Sequence resolves, via
    // resolveAliasChain, to a terminal already assembled in pass 1
    // (real-inline OR external-blob-sourced) and reuses that terminal's
    // already-assembled boneCurves rather than re-deriving them --
    // registered under the ALIAS's own sequence-array index. A terminal
    // with no assembled clip at all produces no clip for the alias either.
    for (size_t si = 0; si < sequences.size(); ++si) {
        if (!isPureAliasSequence(sequences[si])) continue;

        size_t terminal = resolveAliasChain(sequences, si);
        auto it = resolvedClipIndexBySequence.find(terminal);
        if (it == resolvedClipIndexBySequence.end()) continue;

        canon::AnimationClip clip;
        clip.sequence = canon::SequenceRef::sequence(static_cast<uint32_t>(si));
        clip.boneCurves = animations[it->second].boneCurves;
        animations.push_back(std::move(clip));
    }

    // Materials: one per DISTINCT batch identity (materialKeyFor, above),
    // deduped -- see this file's own doc comment for why. All resolved
    // against a single fixed sequence (m2_material_input.hpp's own
    // doc comment explains why: canon::Material is scoped to one sequence,
    // and multiplying materials by sequence count has no consumer yet).
    //
    // Uses `firstResolvedSequenceIndex`, captured right after pass 1 above,
    // NOT `animations.front()` here -- that vector now also holds
    // global-sequence/alias clips appended by passes 2/3. 0 (the
    // pre-existing fallback) when there were no real inline/external-
    // resolved sequences at all, same as before this change.
    uint32_t materialSequenceIndex = firstResolvedSequenceIndex.value_or(0);
    M2MaterialInputs m2in = toMaterialInputs(model);

    std::vector<canon::Material> materials;
    std::vector<canon::Identity> primitiveMaterials;
    std::map<MaterialKey, size_t> materialIndexByKey;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.skinSectionIndex >= submeshes.size()) continue;  // assembleMesh already threw on this; unreachable in practice
        if (submeshes[b.skinSectionIndex].indexCount == 0) continue;

        MaterialKey key = materialKeyFor(b);
        auto it = materialIndexByKey.find(key);
        size_t materialIndex;
        if (it != materialIndexByKey.end()) {
            materialIndex = it->second;
        } else {
            canon::Material mat = assembleMaterial(b, bi, m2in, materialSequenceIndex, textureResolutions);
            materialIndex = materials.size();
            mat.ref.id = canon::RecordIndex{static_cast<uint32_t>(materialIndex)};
            materials.push_back(std::move(mat));
            materialIndexByKey.emplace(key, materialIndex);
        }
        primitiveMaterials.push_back(canon::Identity{canon::RecordIndex{static_cast<uint32_t>(materialIndex)}});
    }

    return canon::assembleModel(std::move(skeleton), std::move(mesh), std::move(materials),
                                 std::move(primitiveMaterials), std::move(animations));
}

}  // namespace husk::m2input
