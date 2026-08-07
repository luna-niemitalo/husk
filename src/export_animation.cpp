#include "export_animation.hpp"

#include <fstream>
#include <set>
#include <stdexcept>

#include "chunk.hpp"
#include "export_transform.hpp"

namespace husk::commands {

namespace {

// M2Sequence flags bits (wowdev.wiki M2#Animation_sequences's Flags
// table). 0x20 means "the animation data is in the .m2 file" -- unset
// means external. 0x40 ("is alias") means m2::Sequence::aliasNext is a
// plain local index into this same file's own `sequences` array; see
// resolveAliasChain, which buildAnimations uses to reuse the terminal
// sequence's own keyframe data for an alias sequence, registered under the
// alias's own id/index.
constexpr uint32_t kSequenceStoredInlineFlag = 0x20;
constexpr uint32_t kSequenceAliasFlag = 0x40;

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

// Zero-pads `value` to at least `width` digits (e.g. zeroPad(69, 4) == "0069").
std::string zeroPad(unsigned value, size_t width) {
    std::string s = std::to_string(value);
    if (s.size() < width) s.insert(0, width - s.size(), '0');
    return s;
}

}  // namespace

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

gltf::Animation::SequenceMetadata buildSequenceMetadata(const m2::Sequence& seq) {
    gltf::Animation::SequenceMetadata sm;
    sm.movespeed = seq.movespeed;
    sm.frequency = seq.frequency;
    sm.replayMin = seq.replay.minimum;
    sm.replayMax = seq.replay.maximum;
    sm.blendTimeIn = seq.blendTimeIn;
    sm.blendTimeOut = seq.blendTimeOut;
    sm.boundsMin = toGltf(seq.bounds.min);
    sm.boundsMax = toGltf(seq.bounds.max);
    sm.boundsRadius = seq.boundsRadius;
    sm.variationNext = seq.variationNext;
    sm.aliasNext = seq.aliasNext;
    sm.isAlias = (seq.flags & kSequenceAliasFlag) != 0;
    return sm;
}

std::filesystem::path findAnimFileByBasename(const std::string& modelPath, const std::string& animDir,
                                              uint16_t animId, uint16_t subAnimId) {
    std::string baseName = std::filesystem::path(modelPath).stem().string();
    std::string fileName = baseName + zeroPad(animId, 4) + "-" + zeroPad(subAnimId, 2) + ".anim";
    return std::filesystem::path(animDir) / fileName;
}

std::optional<gltf::JointAnimation> buildJointAnimation(
    const std::vector<uint8_t>& blob, const m2::Bone& bone, size_t bi, const gltf::Skeleton& skeleton,
    std::vector<std::pair<uint32_t, m2::Vec3>>& translation,
    std::vector<std::pair<uint32_t, m2::Quat>>& rotation,
    std::vector<std::pair<uint32_t, m2::Vec3>>& scale) {
    if (translation.empty() && rotation.empty() && scale.empty()) {
        return std::nullopt;
    }
    repairDuplicateTimestampsAndValidate(translation, bi, "translation");
    repairDuplicateTimestampsAndValidate(rotation, bi, "rotation");
    repairDuplicateTimestampsAndValidate(scale, bi, "scale");

    gltf::JointAnimation ja;
    ja.joint = static_cast<int>(bi);
    const gltf::Vec3& bindTranslation = skeleton.joints[bi].localTranslation;
    // interpolation_type 0 ("step: values change instantly at the
    // timestamp, with no interpolation whatsoever" -- wowdev.wiki
    // M2#Interpolation) needs glTF's STEP sampler, not its default LINEAR
    // -- each of translation/rotation/scale is a separate M2Track with its
    // own independent type.
    ja.translationStep = m2::readTrackMeta(blob, bone.translationTrackOffset).interpolationType == 0;
    ja.rotationStep = m2::readTrackMeta(blob, bone.rotationTrackOffset).interpolationType == 0;
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
    return ja;
}

std::vector<gltf::Animation> buildGlobalSequenceAnimations(const std::vector<uint8_t>& blob,
                                                             const std::vector<m2::Bone>& bones,
                                                             const gltf::Skeleton& skeleton) {
    std::set<uint16_t> globalSequenceIndices;
    for (const auto& bone : bones) {
        for (uint32_t off : {bone.translationTrackOffset, bone.rotationTrackOffset, bone.scaleTrackOffset}) {
            uint16_t gs = m2::readTrackMeta(blob, off).globalSequence;
            if (gs != m2::TrackMeta::kNoGlobalSequence) {
                globalSequenceIndices.insert(gs);
            }
        }
    }

    std::vector<gltf::Animation> animations;
    for (uint16_t gs : globalSequenceIndices) {
        gltf::Animation anim;
        anim.name = "global_seq_" + std::to_string(gs);

        for (size_t bi = 0; bi < bones.size(); ++bi) {
            const auto& bone = bones[bi];
            std::vector<std::pair<uint32_t, m2::Vec3>> translation;
            if (m2::readTrackMeta(blob, bone.translationTrackOffset).globalSequence == gs) {
                translation = m2::resolveVec3GlobalSequenceTrack(blob, bone.translationTrackOffset);
            }
            std::vector<std::pair<uint32_t, m2::Quat>> rotation;
            if (m2::readTrackMeta(blob, bone.rotationTrackOffset).globalSequence == gs) {
                rotation = m2::resolveQuatGlobalSequenceTrack(blob, bone.rotationTrackOffset);
            }
            std::vector<std::pair<uint32_t, m2::Vec3>> scale;
            if (m2::readTrackMeta(blob, bone.scaleTrackOffset).globalSequence == gs) {
                scale = m2::resolveVec3GlobalSequenceTrack(blob, bone.scaleTrackOffset);
            }

            if (auto ja = buildJointAnimation(blob, bone, bi, skeleton, translation, rotation, scale)) {
                anim.joints.push_back(std::move(*ja));
            }
        }

        if (!anim.joints.empty()) {
            animations.push_back(std::move(anim));
        }
    }
    return animations;
}

std::vector<gltf::Animation> buildAnimations(const std::vector<uint8_t>& blob,
                                              const std::vector<m2::Bone>& bones,
                                              const gltf::Skeleton& skeleton,
                                              const std::vector<m2::Sequence>& sequences,
                                              const M2AnimInputs& animInputs) {
    std::vector<gltf::Animation> animations;

    for (size_t si = 0; si < sequences.size(); ++si) {
        const auto& originalSeq = sequences[si];
        size_t sourceIndex = si;
        bool isPureAlias = (originalSeq.flags & kSequenceStoredInlineFlag) == 0 &&
                            (originalSeq.flags & kSequenceAliasFlag) != 0;
        if (isPureAlias) {
            sourceIndex = resolveAliasChain(sequences, si);
        }
        const auto& sourceSeq = sequences[sourceIndex];

        // Keeps a loaded external .anim blob alive for this sequence's
        // iteration -- externalBlob, when set, points into this.
        std::vector<uint8_t> loadedAnimBlob;
        const std::vector<uint8_t>* externalBlob = nullptr;

        if ((sourceSeq.flags & kSequenceStoredInlineFlag) != 0) {
            // Inline -- externalBlob stays null, resolves against `blob`.
        } else {
            if (animInputs.animDir.empty()) {
                continue;
            }
            // FileDataID-named file first (primary -- some extraction tools
            // do use this convention); same-basename convention second (the
            // real wow.export-shaped fallback, see findAnimFileByBasename).
            // Neither requires the other: an AFID-less model/.skel
            // (animFileIds == std::nullopt) skips straight to the basename
            // attempt below, rather than skipping external resolution
            // outright.
            std::filesystem::path animPath;
            if (animInputs.animFileIds) {
                uint32_t fileId =
                    findAnimFileId(*animInputs.animFileIds, sourceSeq.id, sourceSeq.variationIndex);
                if (fileId != 0) {
                    animPath =
                        std::filesystem::path(animInputs.animDir) / (std::to_string(fileId) + ".anim");
                }
            }
            // is_open(), not the stream's own bool conversion: a default-
            // constructed ifstream that never had open() called on it (the
            // animFileIds-absent/fileId==0 case above) reports goodbit, not
            // failbit, so `!f` would be false and silently fall through to
            // reading an unopened stream (empty bytes) instead of trying the
            // basename fallback.
            std::ifstream f;
            if (!animPath.empty()) {
                f.open(animPath, std::ios::binary);
            }
            if (!f.is_open()) {
                animPath = findAnimFileByBasename(animInputs.modelPath, animInputs.animDir, sourceSeq.id,
                                                   sourceSeq.variationIndex);
                f.open(animPath, std::ios::binary);
            }
            if (!f.is_open()) {
                continue;  // not available locally under either naming convention
            }
            std::vector<uint8_t> animFileBytes((std::istreambuf_iterator<char>(f)),
                                                std::istreambuf_iterator<char>());
            if (animInputs.animChunked) {
                // Peek at the top-level chunks before handing off to
                // extractAnimBlob (which only knows AFM2): a .skel-sourced
                // model's .anim files may carry an AFSB chunk, either
                // alongside a small AFM2 stub or alone. The AFM2 stub does
                // NOT hold real per-bone track data (resolving against it
                // throws a "claims more keyframes than this blob holds"
                // bounds error) -- the real data is AFSB's own payload, used
                // directly as the external blob below. AFSB takes priority
                // whenever both are present.
                auto topChunks = readChunks(animFileBytes.data(), animFileBytes.size());
                if (auto afsb = findChunk(topChunks, "AFSB")) {
                    loadedAnimBlob.assign(afsb->data, afsb->data + afsb->size);
                    externalBlob = &loadedAnimBlob;
                } else if (findChunk(topChunks, "AFM2")) {
                    loadedAnimBlob = m2::extractAnimBlob(animFileBytes, animInputs.animChunked);
                    externalBlob = &loadedAnimBlob;
                } else {
                    continue;  // neither AFM2 nor AFSB -- an unrecognized future shape
                }
            } else {
                loadedAnimBlob = m2::extractAnimBlob(animFileBytes, animInputs.animChunked);
                externalBlob = &loadedAnimBlob;
            }
        }

        gltf::Animation anim;
        anim.name = "anim_" + std::to_string(originalSeq.id) + "_" + std::to_string(originalSeq.variationIndex);
        anim.sequenceMetadata = buildSequenceMetadata(originalSeq);

        for (size_t bi = 0; bi < bones.size(); ++bi) {
            const auto& bone = bones[bi];
            auto translation = m2::resolveVec3TrackSequence(
                blob, bone.translationTrackOffset, static_cast<uint32_t>(sourceIndex), externalBlob);
            auto rotation = m2::resolveQuatTrackSequence(
                blob, bone.rotationTrackOffset, static_cast<uint32_t>(sourceIndex), externalBlob);
            auto scale = m2::resolveVec3TrackSequence(
                blob, bone.scaleTrackOffset, static_cast<uint32_t>(sourceIndex), externalBlob);
            if (auto ja = buildJointAnimation(blob, bone, bi, skeleton, translation, rotation, scale)) {
                anim.joints.push_back(std::move(*ja));
            }
        }

        if (!anim.joints.empty()) {
            animations.push_back(std::move(anim));
        }
    }

    return animations;
}

}  // namespace husk::commands
