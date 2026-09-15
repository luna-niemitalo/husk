#include "canon_model.hpp"

#include <set>
#include <stdexcept>
#include <unordered_map>

#include "canon_animation_builder.hpp"
#include "canon_skeleton_builder.hpp"
#include "m2_animation.hpp"  // readTrackMeta, TrackMeta::kNoGlobalSequence

namespace husk::canon {

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
// called since that function lives in commands:: and canon_model.cpp
// already reimplements the one bit test (kSequenceAliasFlag) it depends on.
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

}  // namespace

Model assembleModel(const m2::Model& model, const std::vector<skin::Batch>& batches,
                     const std::vector<skin::Submesh>& submeshes,
                     const std::vector<uint32_t>& triangleIndices,
                     const ExternalAnimBlobs& externalAnimBlobs,
                     const TextureResolutions& textureResolutions) {
    Model result;

    result.skeleton = assembleSkeleton(model.bones);
    result.mesh = assembleMesh(model.vertices, model.bones.size(), batches, submeshes, triangleIndices);

    // Animations, pass 1: one AnimationClip per sequence that is either
    // real inline OR has a caller-supplied external blob (externalAnimBlobs,
    // ExternalAnimBlobs' own doc comment in canon_model.hpp) -- see
    // canon_model.hpp's own doc comment for the full three-pass breakdown.
    // A pure-alias sequence is skipped here unconditionally, even if it has
    // its own externalAnimBlobs entry -- aliases resolve exclusively
    // through pass 3 below, against their terminal's index (the convention
    // ExternalAnimBlobs itself documents). resolvedClipIndexBySequence lets
    // pass 3 (aliases) find and reuse an already-assembled clip's
    // boneCurves by the ORIGINAL model.sequences index, not
    // result.animations's own position -- it doesn't distinguish an
    // inline-sourced clip from an external-blob-sourced one, so an alias
    // whose terminal is external-.anim-resolved reuses it exactly the same
    // way an alias-to-real-inline terminal already did.
    //
    // A sequence where NO bone has any real keyframe data (every
    // assembleBoneAnimation call below returns nullopt) produces no clip --
    // mirrors buildAnimations's own `if (!anim.joints.empty())` gate
    // (export_animation.cpp) exactly. Confirmed as a real, previously-
    // unhandled case, not a hypothetical: `husk export --compare-canon`
    // against bloodelffemale.m2 found 26 real sequences (flags carry the
    // inline bit, genuinely not aliases) that canon emitted an empty clip
    // for and legacy correctly omitted -- see REFACTOR/AUDIT.md §7.2's
    // history for the full trace. Same "absent, not a zero-content
    // placeholder" rule pass 2/3 below already apply for their own empty
    // cases -- this closes the one place that rule wasn't applied yet.
    std::unordered_map<size_t, size_t> resolvedClipIndexBySequence;
    for (size_t si = 0; si < model.sequences.size(); ++si) {
        const auto& seq = model.sequences[si];
        const std::vector<uint8_t>* externalBlob = nullptr;
        if (!isRealInlineSequence(seq)) {
            if (isPureAliasSequence(seq)) continue;  // resolved via its terminal in pass 3
            auto blobIt = externalAnimBlobs.find(static_cast<uint32_t>(si));
            if (blobIt == externalAnimBlobs.end()) continue;  // no inline data, no external blob supplied
            externalBlob = &blobIt->second;
        }

        AnimationClip clip;
        clip.sequence = SequenceRef::sequence(static_cast<uint32_t>(si));
        clip.boneCurves.reserve(result.skeleton.joints.size());
        bool anyBoneAnimated = false;
        for (size_t bi = 0; bi < model.bones.size(); ++bi) {
            auto curves = assembleBoneAnimation(model.blob, model.bones[bi], bi, static_cast<uint32_t>(si),
                                                 externalBlob);
            anyBoneAnimated |= curves.has_value();
            clip.boneCurves.push_back(std::move(curves));
        }
        if (!anyBoneAnimated) continue;
        resolvedClipIndexBySequence.emplace(si, result.animations.size());
        result.animations.push_back(std::move(clip));
    }
    // Captured here, before passes 2/3 append global-sequence/alias clips
    // to `result.animations` -- see materialSequenceIndex's own comment
    // below for why reading result.animations.front() after those passes
    // run would be wrong. May be a real-inline OR an external-blob-sourced
    // sequence's index -- pass 1 above no longer distinguishes the two, and
    // neither does this pick.
    std::optional<uint32_t> firstResolvedSequenceIndex =
        result.animations.empty() ? std::nullopt
                                   : std::make_optional(result.animations.front().sequence.index);

    // Animations, pass 2: one AnimationClip per distinct global sequence
    // actually referenced by any bone track (globalSequenceIndices above),
    // independent of any M2Sequence -- mirrors
    // buildGlobalSequenceAnimations's own per-bone gating exactly
    // (assembleBoneAnimationGlobal re-checks each property's own
    // TrackMeta::globalSequence, since a bone's three tracks need not share
    // one global sequence). A global sequence with no bone actually
    // carrying resolvable data for it (assembleBoneAnimationGlobal
    // returning nullopt for every bone) produces no clip -- an empty clip
    // isn't useful output, same "skip it" rule buildGlobalSequenceAnimations
    // itself applies.
    for (uint16_t gs : globalSequenceIndices(model.blob, model.bones)) {
        AnimationClip clip;
        clip.sequence = SequenceRef::globalSequence(gs);
        clip.boneCurves.reserve(result.skeleton.joints.size());
        bool anyBoneAnimated = false;
        for (size_t bi = 0; bi < model.bones.size(); ++bi) {
            auto curves = assembleBoneAnimationGlobal(model.blob, model.bones[bi], bi, gs);
            anyBoneAnimated |= curves.has_value();
            clip.boneCurves.push_back(std::move(curves));
        }
        if (anyBoneAnimated) {
            result.animations.push_back(std::move(clip));
        }
    }

    // Animations, pass 3: a pure-alias M2Sequence (flags & kSequenceAliasFlag,
    // without the inline bit) that resolves, via resolveAliasChain, to a
    // terminal already assembled in pass 1 (real-inline OR external-blob-
    // sourced -- resolvedClipIndexBySequence doesn't distinguish the two)
    // reuses that terminal's already-assembled boneCurves rather than
    // re-deriving them -- registered under the ALIAS's own sequence-array
    // index, matching buildAnimations's own "clip identity always comes
    // from the original alias sequence, not the terminal it borrows data
    // from" convention. A terminal with no assembled clip at all (neither
    // real-inline nor a supplied external blob) produces no clip for the
    // alias either -- same "absent, not approximated" handling a
    // non-inline, non-alias, non-external sequence already gets in pass 1.
    for (size_t si = 0; si < model.sequences.size(); ++si) {
        if (!isPureAliasSequence(model.sequences[si])) continue;

        size_t terminal = resolveAliasChain(model.sequences, si);
        auto it = resolvedClipIndexBySequence.find(terminal);
        if (it == resolvedClipIndexBySequence.end()) continue;

        AnimationClip clip;
        clip.sequence = SequenceRef::sequence(static_cast<uint32_t>(si));
        clip.boneCurves = result.animations[it->second].boneCurves;
        result.animations.push_back(std::move(clip));
    }

    // Materials: one per surviving batch (mirroring assemblePrimitiveGeosets's
    // own zero-indexCount skip so materials[i] stays aligned with
    // mesh.primitives[i] -- see Model::materials' own doc comment), all
    // resolved against a single fixed sequence (canon_model.hpp's own doc
    // comment explains why: canon::Material is scoped to one sequence, and
    // multiplying materials by sequence count has no consumer yet).
    //
    // Uses `firstResolvedSequenceIndex`, captured right after pass 1 above,
    // NOT `result.animations.front()` here -- that vector now also holds
    // global-sequence/alias clips appended by passes 2/3, whose own
    // `sequence.index` means something different (a globalLoops index, or
    // an alias's own sequence index) and would silently corrupt this pick
    // if read at this point instead. 0 (the pre-existing fallback) when
    // there were no real inline/external-resolved sequences at all, same as
    // before this change.
    uint32_t materialSequenceIndex = firstResolvedSequenceIndex.value_or(0);
    M2MaterialInputs m2in = toMaterialInputs(model);
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.skinSectionIndex >= submeshes.size()) continue;  // assembleMesh already threw on this; unreachable in practice
        if (submeshes[b.skinSectionIndex].indexCount == 0) continue;
        result.materials.push_back(assembleMaterial(b, bi, m2in, materialSequenceIndex, textureResolutions));
    }

    return result;
}

}  // namespace husk::canon
