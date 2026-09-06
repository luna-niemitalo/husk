#include "canon_model.hpp"

#include "canon_animation_builder.hpp"
#include "canon_skeleton_builder.hpp"

namespace husk::canon {

namespace {

// Private to export_animation.cpp's anonymous namespace (see that file's
// own comment on the wowdev.wiki M2#Animation_sequences Flags table) --
// reimplemented here rather than exported, same one-bit-test-not-worth-a-
// shared-header tradeoff every other canon_*_builder.cpp in this codebase
// already accepts for a private production constant.
constexpr uint32_t kSequenceStoredInlineFlag = 0x20;

bool isRealInlineSequence(const m2::Sequence& seq) { return (seq.flags & kSequenceStoredInlineFlag) != 0; }

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
                     const std::vector<uint32_t>& triangleIndices) {
    Model result;

    result.skeleton = assembleSkeleton(model.bones);
    result.mesh = assembleMesh(model.vertices, model.bones.size(), batches, submeshes, triangleIndices);

    // Animations: one AnimationClip per real inline M2Sequence -- see
    // canon_model.hpp's own doc comment for why pure-alias sequences are
    // excluded rather than resolved.
    for (size_t si = 0; si < model.sequences.size(); ++si) {
        if (!isRealInlineSequence(model.sequences[si])) continue;

        AnimationClip clip;
        clip.sequence = SequenceRef::sequence(static_cast<uint32_t>(si));
        clip.boneCurves.reserve(result.skeleton.joints.size());
        for (size_t bi = 0; bi < model.bones.size(); ++bi) {
            clip.boneCurves.push_back(
                assembleBoneAnimation(model.blob, model.bones[bi], bi, static_cast<uint32_t>(si)));
        }
        result.animations.push_back(std::move(clip));
    }

    // Materials: one per surviving batch (mirroring assemblePrimitiveGeosets's
    // own zero-indexCount skip so materials[i] stays aligned with
    // mesh.primitives[i] -- see Model::materials' own doc comment), all
    // resolved against a single fixed sequence (canon_model.hpp's own doc
    // comment explains why: canon::Material is scoped to one sequence, and
    // multiplying materials by sequence count has no consumer yet).
    uint32_t materialSequenceIndex = result.animations.empty() ? 0 : result.animations.front().sequence.index;
    M2MaterialInputs m2in = toMaterialInputs(model);
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.skinSectionIndex >= submeshes.size()) continue;  // assembleMesh already threw on this; unreachable in practice
        if (submeshes[b.skinSectionIndex].indexCount == 0) continue;
        result.materials.push_back(assembleMaterial(b, bi, m2in, materialSequenceIndex));
    }

    return result;
}

}  // namespace husk::canon
