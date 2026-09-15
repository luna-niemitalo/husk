#include "canon_animation_builder.hpp"

#include "export_transform.hpp"  // repairDuplicateTimestampsAndValidate
#include "m2_animation.hpp"      // readTrackMeta, resolveVec3TrackSequence, resolveQuatTrackSequence

namespace husk::canon {

namespace {

Interpolation toInterpolation(const std::vector<uint8_t>& blob, uint32_t trackOffset) {
    return m2::readTrackMeta(blob, trackOffset).interpolationType == 0 ? Interpolation::Step
                                                                        : Interpolation::Linear;
}

VecCurve toVecCurve(std::vector<std::pair<uint32_t, m2::Vec3>> raw, Interpolation interpolation,
                    SequenceRef sequence) {
    VecCurve curve;
    curve.sequence = sequence;
    curve.interpolation = interpolation;
    curve.keyframes.reserve(raw.size());
    for (const auto& [ts, v] : raw) {
        curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, v);
    }
    return curve;
}

QuatCurve toQuatCurve(std::vector<std::pair<uint32_t, m2::Quat>> raw, Interpolation interpolation,
                     SequenceRef sequence) {
    QuatCurve curve;
    curve.sequence = sequence;
    curve.interpolation = interpolation;
    curve.keyframes.reserve(raw.size());
    for (const auto& [ts, v] : raw) {
        curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, v);
    }
    return curve;
}

}  // namespace

std::optional<BoneAnimationCurves> assembleBoneAnimation(const std::vector<uint8_t>& blob,
                                                            const m2::Bone& bone, size_t boneIndex,
                                                            uint32_t sequenceIndex,
                                                            const std::vector<uint8_t>* externalBlob) {
    auto translation =
        m2::resolveVec3TrackSequence(blob, bone.translationTrackOffset, sequenceIndex, externalBlob);
    auto rotation = m2::resolveQuatTrackSequence(blob, bone.rotationTrackOffset, sequenceIndex, externalBlob);
    auto scale = m2::resolveVec3TrackSequence(blob, bone.scaleTrackOffset, sequenceIndex, externalBlob);

    if (translation.empty() && rotation.empty() && scale.empty()) {
        return std::nullopt;
    }

    commands::repairDuplicateTimestampsAndValidate(translation, boneIndex, "translation");
    commands::repairDuplicateTimestampsAndValidate(rotation, boneIndex, "rotation");
    commands::repairDuplicateTimestampsAndValidate(scale, boneIndex, "scale");

    BoneAnimationCurves curves;
    curves.translation = toVecCurve(std::move(translation), toInterpolation(blob, bone.translationTrackOffset),
                                     SequenceRef::sequence(sequenceIndex));
    curves.rotation = toQuatCurve(std::move(rotation), toInterpolation(blob, bone.rotationTrackOffset),
                                   SequenceRef::sequence(sequenceIndex));
    curves.scale = toVecCurve(std::move(scale), toInterpolation(blob, bone.scaleTrackOffset),
                               SequenceRef::sequence(sequenceIndex));
    return curves;
}

std::optional<BoneAnimationCurves> assembleBoneAnimationGlobal(const std::vector<uint8_t>& blob,
                                                                  const m2::Bone& bone, size_t boneIndex,
                                                                  uint16_t globalSequenceIndex) {
    std::vector<std::pair<uint32_t, m2::Vec3>> translation;
    if (m2::readTrackMeta(blob, bone.translationTrackOffset).globalSequence == globalSequenceIndex) {
        translation = m2::resolveVec3GlobalSequenceTrack(blob, bone.translationTrackOffset);
    }
    std::vector<std::pair<uint32_t, m2::Quat>> rotation;
    if (m2::readTrackMeta(blob, bone.rotationTrackOffset).globalSequence == globalSequenceIndex) {
        rotation = m2::resolveQuatGlobalSequenceTrack(blob, bone.rotationTrackOffset);
    }
    std::vector<std::pair<uint32_t, m2::Vec3>> scale;
    if (m2::readTrackMeta(blob, bone.scaleTrackOffset).globalSequence == globalSequenceIndex) {
        scale = m2::resolveVec3GlobalSequenceTrack(blob, bone.scaleTrackOffset);
    }

    if (translation.empty() && rotation.empty() && scale.empty()) {
        return std::nullopt;
    }

    commands::repairDuplicateTimestampsAndValidate(translation, boneIndex, "translation");
    commands::repairDuplicateTimestampsAndValidate(rotation, boneIndex, "rotation");
    commands::repairDuplicateTimestampsAndValidate(scale, boneIndex, "scale");

    BoneAnimationCurves curves;
    curves.translation = toVecCurve(std::move(translation), toInterpolation(blob, bone.translationTrackOffset),
                                     SequenceRef::globalSequence(globalSequenceIndex));
    curves.rotation = toQuatCurve(std::move(rotation), toInterpolation(blob, bone.rotationTrackOffset),
                                   SequenceRef::globalSequence(globalSequenceIndex));
    curves.scale = toVecCurve(std::move(scale), toInterpolation(blob, bone.scaleTrackOffset),
                               SequenceRef::globalSequence(globalSequenceIndex));
    return curves;
}

}  // namespace husk::canon
