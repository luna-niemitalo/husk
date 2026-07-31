#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

#include <CLI/CLI.hpp>

#include "bone.hpp"
#include "chunk.hpp"
#include "commands.hpp"
#include "gltf.hpp"
#include "m2.hpp"
#include "phys.hpp"
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
// keyframe data actually resolves: inline, or via --anim for an external
// .anim file, AFM2- or AFSB-shaped alike (see buildAnimations's doc comment
// below, and WIKI_FINDINGS.md §2 for how AFSB's undocumented byte layout
// was cracked).
namespace husk::commands {

namespace {

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
gltf::Vec3 toGltf(const phys::Vec3& v) { return gltf::zUpToYUp({v.x, v.y, v.z}); }

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

bool isFinite(const m2::Quat& q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

// Validates one bone property's resolved keyframe sequence before it's
// trusted as real animation data: every value finite (FAILURES.md #4 fixed
// this for vertex positions/normals; the identical exposure existed,
// unfixed, for animation keyframes -- see FAILURES2.md #9), and timestamps
// strictly increasing (gltf::JointAnimation's own doc comment already
// documents this as a precondition its caller -- this function -- must
// guarantee; glTF requires an animation sampler's input accessor `min`/
// `max` to be its true bounds, and gltf.cpp's addChannel takes a shortcut
// of `times.front()`/`times.back()` that's only correct if the data is
// actually sorted ascending). A corrupted or truncated .anim/.skel/M2 file
// -- the exact "extraction went wrong" scenario this project is built to
// survive gracefully -- that flips a bit in a keyframe throws here, with
// the offending bone/property/keyframe index named, rather than silently
// producing a spec-non-compliant .glb only a downstream tool (Blender, the
// Khronos validator) would ever notice.
//
// A *duplicate* timestamp (keyframes[i].first ==
// keyframes[i-1].first) is real, shipped Blizzard data, not corruption --
// found on 5 real files (world bosses, base character rigs, one world
// doodad), always exactly one pair, always on `rotation`, consistent with a
// genuinely-authored "hard cut" pose (two values meant to apply at the same
// instant). Repaired in place rather than rejected: nudges the later
// duplicate's timestamp forward by 1ms (cascading, so a run of 3+
// duplicates spreads out 1ms apart each) instead of dropping either
// keyframe -- collapsing would silently discard one of the two real
// authored values (whichever the drop picks), while nudging keeps both,
// turning an authored instantaneous cut into a 1ms transition that's
// visually indistinguishable and correct under both glTF LINEAR and STEP
// sampler interpolation (JointAnimation::translationStep etc. -- STEP just
// delays the jump by 1ms, LINEAR ramps over 1ms instead of 0). A timestamp
// that's *less than* the previous one in the original data (not just equal)
// stays a hard error -- that's genuine disorder, not the observed
// duplicate-hard-cut shape, and repairing it would require guessing which
// of the two is "right." The disorder check classifies each keyframe
// against the *original* (pre-repair) previous timestamp, captured up
// front, not the already-nudged one -- comparing against a nudged value
// would misclassify a legitimate cascading duplicate run (T, T, T) as
// disorder once the first T became T+1. A final pass re-checks the fully
// repaired sequence is actually strictly increasing and throws (rather than
// silently emitting a spec-non-compliant .glb) in the one shape this repair
// doesn't attempt to handle and has no real-data evidence for: a duplicate
// immediately followed by a distinct timestamp too close for the nudge to
// clear.
template <typename T>
void repairDuplicateTimestampsAndValidate(std::vector<std::pair<uint32_t, T>>& keyframes,
                                           size_t boneIndex, const char* property) {
    for (size_t i = 0; i < keyframes.size(); ++i) {
        if (!isFinite(keyframes[i].second)) {
            throw std::runtime_error("bone " + std::to_string(boneIndex) + "'s " + property +
                                      " keyframe " + std::to_string(i) +
                                      " has a non-finite (NaN/Inf) value -- corrupted read or "
                                      "truncated file?");
        }
    }

    std::vector<uint32_t> originalTimes;
    originalTimes.reserve(keyframes.size());
    for (const auto& kf : keyframes) originalTimes.push_back(kf.first);

    for (size_t i = 1; i < keyframes.size(); ++i) {
        if (originalTimes[i] < originalTimes[i - 1]) {
            throw std::runtime_error(
                "bone " + std::to_string(boneIndex) + "'s " + property + " keyframe " +
                std::to_string(i) + "'s timestamp (" + std::to_string(originalTimes[i]) +
                "ms) isn't strictly greater than keyframe " + std::to_string(i - 1) + "'s (" +
                std::to_string(originalTimes[i - 1]) + "ms) -- corrupted read or truncated file?");
        }
        if (originalTimes[i] == originalTimes[i - 1]) {
            keyframes[i].first = keyframes[i - 1].first + 1;
        }
    }

    for (size_t i = 1; i < keyframes.size(); ++i) {
        if (keyframes[i].first <= keyframes[i - 1].first) {
            throw std::runtime_error(
                "bone " + std::to_string(boneIndex) + "'s " + property + " keyframe " +
                std::to_string(i) + "'s timestamp couldn't be repaired into strictly-increasing "
                "order (duplicate-timestamp nudging collided with a following keyframe) -- "
                "corrupted read or truncated file?");
        }
    }
}

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
// alias") used to be treated as unresolvable (the wiki's own "stored...
// somewhere. I have no clue." bullet) -- WIKI_FINDINGS.md §12 resolved it:
// m2::Sequence::aliasNext is a plain local index into this same file's own
// `sequences` array, and following the wiki's own documented chain-walk
// (repeatedly jumping to sequences[aliasNext] until a non-alias record is
// reached) always terminates cleanly against real data (157/157 real
// aliases checked, zero cycles). See resolveAliasChain below, which
// buildAnimations now uses to reuse the terminal sequence's own keyframe
// data for an alias sequence, registered under the alias's own id/index.
constexpr uint32_t kSequenceStoredInlineFlag = 0x20;
constexpr uint32_t kSequenceAliasFlag = 0x40;

// Resolves an alias sequence (M2Sequence::flags & kSequenceAliasFlag) to
// its terminal non-alias sequence's own index into `sequences`, by
// repeatedly following aliasNext (WIKI_FINDINGS.md §12) until a sequence
// without the alias flag is reached. `startIndex` need not itself be an
// alias (returns `startIndex` unchanged in that case). Real data never
// cycles, but this is foreign file data -- bounded to `sequences.size()`
// hops (an acyclic chain can visit at most that many distinct sequences
// before it would have to repeat one), throwing rather than looping
// forever if a real cycle, or an out-of-range aliasNext, ever shows up.
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

// Lifts an m2::Sequence's own per-sequence metadata (movespeed/frequency/
// replay/blendTime/bounds/variationNext/aliasNext -- M2_GAPS_TODO.md's
// former Item 1) into gltf::Animation::SequenceMetadata, see that struct's
// doc comment for why these are exposed as inert clip `extras` rather than
// applied to anything. `bounds` is remapped Z-up -> Y-up the same way
// every other spatial value in this pipeline is (toGltf) -- it's a real
// bounding volume a downstream consumer might actually want to use
// spatially, unlike e.g. TextureTransform's raw model-space fields.
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

// Everything buildAnimations needs to resolve a sequence's keyframes from
// an external .anim file, when its data isn't inline -- bundled for the
// same reason M2MaterialInputs is (a handful of related inputs, one call
// site). `animDir` is the same local-directory-by-FileDataID convention
// `--skin-dir`/`--textures` already use, with a same-basename fallback (see
// findAnimFileByBasename) for the real wow.export-shaped naming convention --
// husk doesn't resolve a FileDataID to a CASC path itself, so a file missing
// under both conventions is treated as "skip this sequence" (see
// buildAnimations), not an error.
struct M2AnimInputs {
    std::optional<std::vector<m2::Header::AnimFileEntry>> animFileIds;
    // header.globalFlags & 0x200000 -- wowdev.wiki's flag_unk_0x200000,
    // "apparently: use 24500 upgraded model format: chunked .anim files".
    // See m2::extractAnimBlob's doc comment for the caveat: this is
    // implemented from that description, not yet verified against a real
    // chunked .anim file.
    bool animChunked = false;
    std::string animDir;
    std::string modelPath;  // for findAnimFileByBasename's fallback below
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

// Zero-pads `value` to at least `width` digits (e.g. zeroPad(69, 4) == "0069").
std::string zeroPad(unsigned value, size_t width) {
    std::string s = std::to_string(value);
    if (s.size() < width) s.insert(0, width - s.size(), '0');
    return s;
}

// Real wow.export-style extractions name external .anim files
// <model-basename><animId:04d>-<subAnimId:02d>.anim next to the model, not by
// FileDataID (confirmed against the committed bloodelffemale_hd0069-00.anim/
// -01.anim fixtures, which match this exactly -- no bare <FileDataID>.anim
// file exists anywhere in the real corpus sample; see WIKI_FINDINGS.md §2).
// Direct filename construction, not a directory scan like
// findSameBasenameSkins -- (animId, subAnimId) fully determines the name, so
// there's no ambiguity to resolve the way .skin's open-ended LOD-suffix scan
// has. Returns the constructed path unconditionally; existence is checked by
// the caller's own ifstream-open attempt, same as the FileDataID path.
std::filesystem::path findAnimFileByBasename(const std::string& modelPath, const std::string& animDir,
                                              uint16_t animId, uint16_t subAnimId) {
    std::string baseName = std::filesystem::path(modelPath).stem().string();
    std::string fileName = baseName + zeroPad(animId, 4) + "-" + zeroPad(subAnimId, 2) + ".anim";
    return std::filesystem::path(animDir) / fileName;
}

// Builds one JointAnimation for bone `bi` from already-resolved translation/
// rotation/scale keyframes -- shared by buildAnimations (per-M2Sequence
// resolution) and buildGlobalSequenceAnimations (global-sequence, single-
// track resolution) below, which resolve *which* keyframes apply
// differently but build the resulting glTF channel data identically once
// resolved. Returns nullopt if all three are empty (nothing to animate for
// this bone in this clip) -- an empty JointAnimation isn't useful output.
// Validates every keyframe first (finiteness + strictly-increasing
// timestamps, see checkKeyframesWellFormed, FAILURES2.md #9).
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

// Builds one glTF animation clip per distinct global_sequence index actually
// used by any of `bones`' translation/rotation/scale tracks -- a
// continuously-looping animation independent of any M2Sequence (eye glow
// pulses, torch flicker, idle sway; wowdev.wiki "Global Sequences": "always
// loops"). Fixes FAILURES2.md #7: these tracks used to correctly resolve to
// nothing at all (avoiding misattributing them to whichever M2Sequence
// happened to occupy outer-array position 0, see TrackMeta's doc comment --
// a real bug this exact type was introduced to fix) but were never resolved
// any other way either, so a real, intentional animation feature (glowing
// eyes, idle sway) silently never appeared in any exported clip. `blob`/
// `bones`/`skeleton` are the same triple buildAnimations takes (inline M2 or
// .skel-sourced) -- global-sequence tracks have no external-.anim mechanism
// of their own (they aren't tied to an AFID-resolvable M2Sequence at all),
// so there's no `animInputs`/external-blob parameter here. Named
// "global_seq_<index>" per clip. A clip that ends up with no bone actually
// carrying data for it (shouldn't happen given how the candidate index set
// is built, but keeps the same "no empty clips" policy as buildAnimations)
// is skipped.
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
// real bloodelffemale_hd.m2/.skel pair (see skel.hpp's doc comment). A
// .skel sequence whose external data lives in an AFSB-chunked .anim file
// (the norm for modern character models, see WIKI_FINDINGS.md §2) resolves
// the exact same way: SKB1's own per-bone M2Track (count, offset)
// descriptors -- the ones trackSequenceInnerArrays already reads for the
// inline/AFM2 cases above -- turn out to point directly into the AFSB
// payload's own byte range, not into SKB1 itself (verified against the
// entire real bloodelffemale_hd.m2/.skel/.anim corpus: every bone/sequence
// combination's timestamps decode monotonic-and-in-bounds and every
// resulting value is finite, rotation quaternions included, which decode
// to unit length). `skeleton` must be the already-built bind-pose Skeleton
// for these same `bones`, in the same order, since each keyframe's
// translation channel value is bind-pose-relative-to-parent
// (`skeleton.joints[i].localTranslation`) plus the animated delta -- glTF's
// animated translation *replaces* the node's translation at sampled times
// rather than adding to it, so the bind offset has to be baked into every
// keyframe value, not left implicit. Sequences with no bone actually
// carrying resolvable data for them (a zero-length primary sequence, one
// this model just doesn't animate any bone in, or an external sequence
// whose .anim file isn't available in `animInputs.animDir`) are skipped --
// an empty animation clip isn't useful output. A malformed .anim file (bad
// chunk framing, or keyframe data claiming more than the file actually
// holds) throws rather than being silently skipped -- same "foreign data
// that doesn't fit its own claims is an error, not a best-effort" policy as
// every other parser in this codebase; only a genuinely *missing* file, or
// a chunked .anim with neither an `AFM2` nor an `AFSB` chunk (a future
// .anim variant this parser doesn't recognize yet), is treated as "husk
// doesn't have this one," consistent with --textures/--skin-dir.
//
// An alias sequence (flags & kSequenceAliasFlag) is resolved via
// resolveAliasChain to its terminal non-alias sequence -- but only when
// this sequence doesn't *also* carry kSequenceStoredInlineFlag: real data
// (bloodelffemale_hd.skel) shows 31 of its 38 real alias sequences also
// have 0x20 set, meaning they already carry their own real inline M2Track
// data and don't need (or want) another sequence's data substituted --
// 0x20 already winning that priority is exactly what the pre-alias-
// resolution code did (by checking it first), and real per-clip content
// for those 31 would silently change if alias resolution pre-empted it
// instead. Only a sequence that is *purely* an alias (0x40 set, 0x20 not)
// gets its keyframe source redirected to the resolved terminal sequence
// (`sourceSeq`/`sourceIndex`) -- every inline-vs-external decision below,
// and the M2Track outer-array index passed to resolveVec3TrackSequence/
// resolveQuatTrackSequence, use that terminal. Either way, the resulting
// clip's *name* and `sequenceMetadata` extras always come from this
// sequence's own M2Sequence record (`originalSeq`), so it's registered
// under its own id/index even when reusing borrowed data.
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
                // model's .anim files were found, against real data, to
                // carry an AFSB chunk -- either alongside a small (real
                // files: 16-1344 bytes, always a multiple of 16) AFM2 chunk,
                // or alone. That AFM2 "stub" does NOT hold the real per-bone
                // track data (confirmed the hard way early on: resolving
                // against it throws a real "claims more keyframes than this
                // blob holds" bounds error) -- the real data is AFSB's own
                // payload, used directly as the external blob below, same
                // mechanism as an AFM2-shaped external file (WIKI_FINDINGS.md
                // §2's follow-up has the full byte-level receipts). AFSB
                // takes priority whenever both are present.
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
    // Every distinct Submesh::skinSectionId (the "geoset ID", wowdev.wiki
    // M2/.skin#Submeshes) actually referenced by a batch that became a
    // primitive, sorted ascending -- see skin.hpp's Submesh doc comment.
    // husk doesn't filter geosets yet (FAILURES2.md #1): every submesh in
    // the .skin file gets exported, unconditionally, even when several are
    // mutually-exclusive character-customization options (different
    // hairstyles, etc.) that a real client would only ever draw one of at a
    // time. Surfaced here so exportGlb can at least warn loudly when more
    // than one distinct geoset actually ended up in the output, rather than
    // silently merging them with no indication anything unusual happened.
    std::vector<uint16_t> distinctSkinSectionIds;
    // Number of batches whose textureCount is > 1 -- per wowdev.wiki
    // M2/.skin#Texture_units, textureCount (1-4) means the real per-unit
    // texture/UV-mapping/transparency lookups are `textureCount` consecutive
    // entries starting at textureComboIndex, used for real, visually
    // meaningful multi-texture effects (a second env-mapped "shine" layer on
    // armor/weapons, genuinely independent two-texture blends) -- husk only
    // ever resolves the first one (see FAILURES2.md #6), silently dropping
    // any additional layer. Surfaced so exportGlb can at least say so.
    size_t multiTextureBatchCount = 0;
    // Number of batches whose color (M2Color) or transparency-fade
    // (M2TextureWeight) reference is real per-sequence or global-sequence
    // keyframe animation, not a single constant value (see
    // m2::Color::colorAnimated/alphaAnimated, m2::TextureWeight::
    // weightAnimated) -- e.g. an eye-glow or enchant-glow pulse. Unlike a
    // bone's translation/rotation/scale, core glTF has no way to *play back*
    // an animated material property, so there's no real translation to
    // build -- but the actual keyframe data is resolved (same per-sequence/
    // global-sequence machinery buildAnimations already uses for bones,
    // resolveAnimatedColorCurve/resolveAnimatedFixed16Curve below) and
    // attached as inert `tint_animation`/`fade_animation` material extras
    // (see gltf::Material's doc comments), same treatment as the multi-
    // texture-layer/textureTransform extras below. This still exists so
    // exportGlb can note the static baseColorFactor default is a lossy
    // approximation of the real animation, not silently pretend the export
    // is complete.
    size_t animatedTintOrFadeBatchCount = 0;
    // Number of batches whose textureTransformComboIndex resolved to a real
    // M2TextureTransform (UV scroll/rotate/scale animation) -- see
    // gltf::Material::TextureTransform's doc comment for why this is
    // exposed as inert extras rather than a real KHR_texture_transform.
    size_t textureTransformBatchCount = 0;
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
    // but unused in Cataclysm") -- empty in almost every modern file (a
    // full ~130k-file real-data scan found only 3 exceptions, see
    // WIKI_FINDINGS.md §7); when empty, batch.textureCoordComboIndex is
    // never dereferenced at all and every material just uses UV set 0.
    // Real, nonzero data has been seen in those 3 files, but its values
    // (e.g. 33/34) don't match the wiki's documented -1/0/1 range --
    // consistent with the "present but unused" wording, likely leftover/
    // vestigial rather than actually consulted, but not confirmed against
    // an authoritative source. mapping==1 below still only special-cases
    // the one documented value, so this data (real or vestigial) safely
    // falls back to UV set 0 either way.
    std::vector<uint16_t> textureCoordCombos;
    std::vector<m2::Color> colors;
    std::vector<m2::TextureWeight> textureWeights;
    std::vector<uint16_t> textureWeightCombos;
    std::vector<m2::TextureTransform> textureTransforms;
    std::vector<uint16_t> textureTransformCombos;
    std::optional<std::vector<uint32_t>> textureFileDataIds;
    // For resolving a genuinely-animated M2Color/M2TextureWeight curve
    // (colorAnimated/alphaAnimated/weightAnimated) into real keyframe data --
    // see resolveAnimatedColorCurve/resolveAnimatedFixed16Curve below.
    // `blob` is the same MD20 bytes `colors`/`textureWeights` above were
    // parsed from (M2Track offsets are relative to it, not the .skin file);
    // `sequenceCount` is the model's own header.sequences.count, the
    // M2Track outer-array bound buildAnimations already iterates the same
    // way for bone tracks.
    const std::vector<uint8_t>* blob = nullptr;
    size_t sequenceCount = 0;
};

// Decodes one raw fixed16 wire value (as resolveRawIntTrackSequence/
// resolveRawIntGlobalSequenceTrack return it, zero-extended into a
// uint32_t) into a 0.0..1.0 float -- the same conversion m2.cpp's
// readFixed16TrackValue uses for the constant-value case
// (wowdev.wiki M2#Colors_and_transparency's own "0 - transparent, 0x7FFF -
// opaque" scale). Duplicated here rather than shared across the m2/
// cmd_export module boundary since resolveRawIntTrackSequence's own doc
// comment already assigns this scaling step to the caller.
float decodeFixed16(uint32_t bits) {
    uint16_t b = static_cast<uint16_t>(bits);
    int16_t raw;
    std::memcpy(&raw, &b, sizeof(raw));
    return std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);
}

// Resolves a genuinely-animated M2Color::color track (colorAnimated) into
// real (seconds, rgb) keyframes -- one gltf::Material::AnimatedColorCurve
// per M2Sequence that has real inline data for this track (the model's own
// sequence-array order, matching buildAnimations' own per-sequence loop for
// bone tracks), plus a synthetic global-sequence entry (sequenceIndex left
// at -1) when the track loops independently of any M2Sequence instead (see
// resolveVec3GlobalSequenceTrack's doc comment). `color`'s x/y/z are
// already 0..1 RGB, NOT a spatial vector -- deliberately NOT run through
// toGltf()'s Z-up -> Y-up remap, which only applies to real positions/
// directions.
std::vector<gltf::Material::AnimatedColorCurve> resolveAnimatedColorCurve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    std::vector<gltf::Material::AnimatedColorCurve> curves;
    for (size_t si = 0; si < sequenceCount; ++si) {
        auto raw = m2::resolveVec3TrackSequence(blob, trackOffset, static_cast<uint32_t>(si));
        if (raw.empty()) continue;
        gltf::Material::AnimatedColorCurve curve;
        curve.sequenceIndex = static_cast<int>(si);
        curve.keyframes.reserve(raw.size());
        for (const auto& [ts, v] : raw) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, gltf::Vec3{v.x, v.y, v.z});
        }
        curves.push_back(std::move(curve));
    }
    auto global = m2::resolveVec3GlobalSequenceTrack(blob, trackOffset);
    if (!global.empty()) {
        gltf::Material::AnimatedColorCurve curve;
        curve.keyframes.reserve(global.size());
        for (const auto& [ts, v] : global) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, gltf::Vec3{v.x, v.y, v.z});
        }
        curves.push_back(std::move(curve));
    }
    return curves;
}

// Same shape as resolveAnimatedColorCurve, but for a genuinely-animated
// M2Color::alpha or M2TextureWeight::weight track (both M2Track<fixed16>) --
// shared by both since only the source field differs, see
// gltf::Material::alphaFadeAnimation/weightFadeAnimation's doc comment.
std::vector<gltf::Material::AnimatedScalarCurve> resolveAnimatedFixed16Curve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    std::vector<gltf::Material::AnimatedScalarCurve> curves;
    for (size_t si = 0; si < sequenceCount; ++si) {
        auto raw = m2::resolveRawIntTrackSequence(blob, trackOffset, static_cast<uint32_t>(si), 2);
        if (raw.empty()) continue;
        gltf::Material::AnimatedScalarCurve curve;
        curve.sequenceIndex = static_cast<int>(si);
        curve.keyframes.reserve(raw.size());
        for (const auto& [ts, bits] : raw) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, decodeFixed16(bits));
        }
        curves.push_back(std::move(curve));
    }
    auto global = m2::resolveRawIntGlobalSequenceTrack(blob, trackOffset, 2);
    if (!global.empty()) {
        gltf::Material::AnimatedScalarCurve curve;
        curve.keyframes.reserve(global.size());
        for (const auto& [ts, bits] : global) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, decodeFixed16(bits));
        }
        curves.push_back(std::move(curve));
    }
    return curves;
}

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
        // A genuinely geometry-less .skin (real corpus
        // shape -- pure particle/ribbon VFX models, zero vertices at the M2
        // level, not just an empty batch table) has no triangles to put in
        // a primitive at all. Leave `result.primitives` empty rather than
        // manufacturing one with empty `indices` -- glTF has no valid
        // "primitive with zero indices" representation, so the caller
        // (cmd_export.cpp) skips adding a mesh node for this LOD tier
        // entirely when `result.primitives` comes back empty.
        if (triangleIndices.empty()) {
            return result;
        }
        gltf::Primitive prim;
        prim.indices = triangleIndices;
        result.primitives.push_back(std::move(prim));
        return result;
    }

    std::set<uint16_t> skinSectionIds;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.skinSectionIndex >= submeshes.size()) {
            throw std::runtime_error("batch " + std::to_string(bi) + "'s skinSectionIndex (" +
                                      std::to_string(b.skinSectionIndex) +
                                      ") is out of range for " + std::to_string(submeshes.size()) +
                                      " submeshes");
        }
        const auto& sm = submeshes[b.skinSectionIndex];
        skinSectionIds.insert(sm.skinSectionId);
        if (b.textureCount > 1) {
            ++result.multiTextureBatchCount;
        }
        if (static_cast<size_t>(sm.indexStart) + sm.indexCount > triangleIndices.size()) {
            throw std::runtime_error(
                "submesh " + std::to_string(b.skinSectionIndex) +
                "'s index range runs past the end of the resolved triangle-index buffer -- "
                "corrupted .skin?");
        }

        // The minority case: a submesh with zero indices
        // alongside sibling submeshes that have real geometry (mixed real+
        // empty geosets in one .skin) -- not the dominant "whole model is
        // geometry-less" shape (see the batches.empty() branch's caller-
        // side handling in cmd_export.cpp), but the same "don't manufacture
        // a primitive glTF can't represent" rule applies per-primitive:
        // skip just this batch (no primitive, no material) rather than
        // emitting a zero-indices primitive that would fail writeGlbMulti's
        // hard check.
        if (sm.indexCount == 0) {
            continue;
        }

        gltf::Primitive prim;
        prim.indices.assign(triangleIndices.begin() + sm.indexStart,
                             triangleIndices.begin() + sm.indexStart + sm.indexCount);
        prim.skinSectionId = sm.skinSectionId;

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
            if (color.colorAnimated || color.alphaAnimated) {
                ++result.animatedTintOrFadeBatchCount;
                // Full curve dump -- diagnostic-only
                // extras, see gltf::Material::tintAnimation/
                // alphaFadeAnimation's doc comments. `m2.blob` is only
                // unset for a hypothetical caller that never populated it
                // (none exists today, see M2MaterialInputs's doc comment) --
                // best-effort like additionalTextureLayers/textureTransform
                // above, not required for a usable export.
                if (m2.blob) {
                    if (color.colorAnimated) {
                        gm.tintAnimation = resolveAnimatedColorCurve(*m2.blob, color.colorTrackOffset,
                                                                       m2.sequenceCount);
                    }
                    if (color.alphaAnimated) {
                        gm.alphaFadeAnimation = resolveAnimatedFixed16Curve(
                            *m2.blob, color.alphaTrackOffset, m2.sequenceCount);
                    }
                }
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
            const auto& weight = m2.textureWeights[weightIndex];
            if (weight.weight) {
                gm.baseColorFactor[3] *= *weight.weight;
            }
            if (weight.weightAnimated) {
                ++result.animatedTintOrFadeBatchCount;
                if (m2.blob) {
                    gm.weightFadeAnimation = resolveAnimatedFixed16Curve(
                        *m2.blob, weight.weightTrackOffset, m2.sequenceCount);
                }
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

            // M2Texture::type -- see
            // gltf::Material::textureType's doc comment for why this is a
            // real "husk can't resolve this locally" signal for any nonzero
            // value, not just missing PNG data.
            gm.textureType = m2.textures[textureIndex].type;

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

            // Additional texture layers (textureCount > 1, FAILURES2.md #6):
            // per wowdev.wiki M2/.skin#Texture_units, textureComboIndex is a
            // *base* index -- layer i's real combo index is
            // textureComboIndex + i ("If the textureCount is e.g. 3 and the
            // texunit's uv anim lookup is 2, then the 3 uv animation lookups
            // are 2, 3, and 4"). Resolved best-effort, not with the same
            // "foreign data must fit its own claims" strictness as the
            // primary texture above: this is supplementary metadata (see
            // gltf::Material::AdditionalTextureLayer), not required for a
            // usable export, so an out-of-range layer is skipped rather than
            // failing the whole batch. The base-index arithmetic itself
            // *is* now independently verified against a real multi-texture
            // batch (TODO_correctness.md's former #3, resolved -- see
            // WIKI_FINDINGS.md §7 and tests/test_integration.cpp's
            // checkMultiTextureLayerArithmetic): a real 6-layer guild-
            // pennant batch's resolved FileDataIDs match a from-scratch
            // independent parse exactly, and a real file with a nonzero
            // textureCoordCombos table (values outside the documented
            // -1/0/1 range -- likely vestigial, see WIKI_FINDINGS.md)
            // still resolves safely via the mapping==1 fallback below.
            for (uint16_t layer = 1; layer < b.textureCount; ++layer) {
                size_t comboIdx = static_cast<size_t>(b.textureComboIndex) + layer;
                if (comboIdx >= m2.textureCombos.size()) break;
                uint16_t layerTextureIndex = m2.textureCombos[comboIdx];
                if (layerTextureIndex >= m2.textures.size()) continue;

                gltf::Material::AdditionalTextureLayer al;
                if (!m2.textureCoordCombos.empty()) {
                    size_t coordComboIdx = static_cast<size_t>(b.textureCoordComboIndex) + layer;
                    if (coordComboIdx < m2.textureCoordCombos.size() &&
                        m2.textureCoordCombos[coordComboIdx] == 1) {
                        al.texCoord = 1;
                    }
                }
                if (m2.textureFileDataIds && layerTextureIndex < m2.textureFileDataIds->size()) {
                    al.fileDataId = (*m2.textureFileDataIds)[layerTextureIndex];
                    if (al.fileDataId != 0 && !texturesDir.empty()) {
                        auto pngPath = std::filesystem::path(texturesDir) /
                                       (std::to_string(al.fileDataId) + ".png");
                        std::ifstream f(pngPath, std::ios::binary);
                        if (f) {
                            al.imagePng.assign(std::istreambuf_iterator<char>(f),
                                                std::istreambuf_iterator<char>());
                        }
                    }
                }
                gm.additionalTextureLayers.push_back(std::move(al));
            }
        }

        // UV scroll/rotate/scale animation (FINDINGS.md §3.1): resolved
        // the same "sentinel means none" way colorIndex is, then exposed as
        // inert extras -- see m2::TextureTransform's doc comment for why
        // this never becomes a real KHR_texture_transform on the render.
        // Best-effort like the additional-texture-layers loop just above
        // (an out-of-range index is skipped, not a failure) -- this is
        // supplementary metadata, not required for a usable export.
        if (b.textureTransformComboIndex != 0xFFFF &&
            b.textureTransformComboIndex < m2.textureTransformCombos.size()) {
            uint16_t transformIndex = m2.textureTransformCombos[b.textureTransformComboIndex];
            if (transformIndex < m2.textureTransforms.size()) {
                const auto& xf = m2.textureTransforms[transformIndex];
                gltf::Material::TextureTransform gxf;
                gxf.constant =
                    !xf.translationAnimated && !xf.rotationAnimated && !xf.scalingAnimated;
                if (xf.translation) {
                    gxf.translation = {xf.translation->x, xf.translation->y, xf.translation->z};
                }
                if (xf.rotation) {
                    gxf.rotation[0] = xf.rotation->x;
                    gxf.rotation[1] = xf.rotation->y;
                    gxf.rotation[2] = xf.rotation->z;
                    gxf.rotation[3] = xf.rotation->w;
                }
                if (xf.scaling) {
                    gxf.scaling = {xf.scaling->x, xf.scaling->y, xf.scaling->z};
                }
                gm.textureTransform = gxf;
                ++result.textureTransformBatchCount;
            }
        }

        prim.materialIndex = static_cast<int>(result.materials.size());
        result.materials.push_back(std::move(gm));
        result.primitives.push_back(std::move(prim));
    }

    result.distinctSkinSectionIds.assign(skinSectionIds.begin(), skinSectionIds.end());
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

// Resolves the literal "auto" .skin path via the M2's own SFID chunk,
// honoring an optional --lod selection (`lodArg`, "" if not given). "":
// the roadmap-stage-7 policy this always followed before --lod
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
        // Only reachable via --lod/an indexed 'auto' resolution with
        // --skin-dir explicitly 'none' -- exportGlb's caller already
        // rejects this combination before parsing the model at all (see
        // its own "--lod ... --skin-dir 'none'" check), so this is a
        // belt-and-suspenders guard, not the primary error path.
        throw std::runtime_error(
            "'auto' needs a --skin-dir to search (got 'none') -- pass --skin-dir <dir> pointing at "
            "a directory of already-extracted '<FileDataID>.skin' files, or drop --lod so 'auto' "
            "can fall back to the same-basename numbered scan instead");
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
//
// A digit-suffix match of *any* length is ambiguous
// when one model's basename is itself a numeric-suffix prefix of another
// model's basename in the same directory (e.g. "mogu_library_crate_10" is
// a prefix of "mogu_library_crate_100" and "mogu_library_crate_1000") --
// a real corpus scan found this silently pairs a model with a *different*
// model's skin, since the wrong-basename candidate happens to parse as a
// valid (if shorter/longer) digit suffix too. WoW's own convention is
// always exactly 2 digits (`00`-`0N`); real corpus directories checked
// here (world/nodxt/detail's vebgrs*/vebbsh* families, 1-17 LOD-numbered
// siblings each) confirm every genuine skin resolves cleanly under "prefer
// a 2-digit suffix match when one exists" -- so 1-digit/3+-digit matches
// are only ever collisions with a sibling model's own real 2-digit suffix,
// never a real model's only skin. Kept as a fallback (not an outright
// reject) for the unconfirmed case of a directory with no 2-digit match at
// all, rather than turning a hypothetical 1-or-3-digit-only model into a
// new "no .skin found" regression.
std::vector<std::pair<int, std::string>> findSameBasenameSkins(const std::string& modelPath) {
    std::filesystem::path model(modelPath);
    std::string baseName = model.stem().string();  // e.g. "bloodelffemale"
    std::filesystem::path dir = model.parent_path();
    if (dir.empty()) dir = ".";

    // (lod, path, digit-suffix length) -- the length is only used to filter
    // for the "prefer 2 digits" rule below, then discarded.
    std::vector<std::tuple<int, std::string, size_t>> found;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return {};
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
        found.emplace_back(lod, entry.path().string(), pos - digitsStart);
    }

    bool hasTwoDigitMatch =
        std::any_of(found.begin(), found.end(), [](const auto& t) { return std::get<2>(t) == 2; });

    std::vector<std::pair<int, std::string>> result;
    for (const auto& [lod, path, digitLen] : found) {
        if (hasTwoDigitMatch && digitLen != 2) continue;
        result.emplace_back(lod, path);
    }
    std::sort(result.begin(), result.end());
    return result;
}

// Resolves `--skin auto` (CLI11 rejects the literal 'none' at parse time,
// and an explicit .skin path never reaches this function at all -- see
// addExportOptions/exportGlb) for the common case of no explicit --lod:
// folds what used to be two independent code paths (an omitted .skin
// positional -> findSameBasenameSkins only; the literal word "auto" ->
// resolveAutoSkinPaths only) into the single 'auto' state DESIGN.md
// decided on. Order matters: the SFID-declared FileDataID (the model's own
// self-description) is tried first, in `skinDir` (unless `skinDirNone`,
// which skips this stage entirely); the same-basename numbered scan next
// to the model is the fallback, tried only when the SFID stage didn't
// resolve to a file that actually exists. An explicit --lod is handled
// separately by exportGlb (via resolveAutoSkinPaths directly) -- there's
// no equivalent of an indexed/'all' selection in the same-basename scan,
// so --lod always commits to the SFID stage with no fallback, same as
// before this migration.
std::vector<std::pair<std::string, std::string>> resolveSkin(const m2::Header& header,
                                                               const std::string& modelPath,
                                                               const std::string& skinDir,
                                                               bool skinDirNone) {
    bool sfidPresent = header.skinFileDataIds && !header.skinFileDataIds->empty();

    if (!skinDirNone && sfidPresent) {
        std::string candidate =
            (std::filesystem::path(skinDir) / (std::to_string((*header.skinFileDataIds)[0]) + ".skin"))
                .string();
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            std::cerr << "husk: note: 'auto' resolved '" << candidate
                      << "' (SFID entry 0, highest-detail LOD)\n";
            return {{"", candidate}};
        }
    }

    auto candidates = findSameBasenameSkins(modelPath);
    if (!candidates.empty()) {
        std::cerr << "husk: note: 'auto' resolved '" << candidates.front().second
                  << "' via the same-basename numbered scan";
        if (candidates.size() > 1) {
            std::cerr << " (lowest-numbered of " << candidates.size()
                      << " same-basename .skin files found next to the model)";
        }
        std::cerr << "\n";
        return {{"", candidates.front().second}};
    }

    std::string reason;
    if (skinDirNone) {
        reason = "--skin-dir is 'none', so the SFID stage was skipped entirely";
    } else if (!sfidPresent) {
        reason = "'" + modelPath + "' has no SFID chunk (or it's empty) -- this M2 doesn't carry "
                                    "skin FileDataIDs to auto-select from (pre-Legion M2s never do)";
    } else {
        reason = "the SFID-declared FileDataID's .skin wasn't found in '" + skinDir + "'";
    }
    throw std::runtime_error("'auto' couldn't resolve a .skin file for '" + modelPath + "': " + reason +
                              ", and no same-named '<model-basename><N>.skin' file exists next to it "
                              "either -- pass an explicit .skin path instead of 'auto'");
}

}  // namespace

// See commands.hpp's doc comment on ExportOptions: this is the one place
// export's flag surface is declared, shared by exportGlb's own real parse
// and main.cpp's `--print-completion` introspection.
void addExportOptions(CLI::App& app, ExportOptions& opts) {
    app.add_option("-i,--input,input", opts.modelPath, "the .m2 file to export")->required();
    app.add_option("-o,--output,output", opts.outputPath,
                    "output .glb path (default: '<model-basename>.glb')");
    app.add_option("-s,--skin", opts.skinArg,
                    "a .skin path, or 'auto' to resolve via the model's own SFID chunk, falling "
                    "back to a same-basename numbered scan next to the model if that doesn't "
                    "resolve")
        ->capture_default_str()
        ->check(
            [](const std::string& v) -> std::string {
                if (v == "none") {
                    return "--skin takes a .skin path or 'auto', never 'none' -- a .skin file is "
                           "the sole source of triangle/submesh/batch data, not optional "
                           "enrichment (pass an explicit path instead)";
                }
                return "";
            },
            "SKIN");
    app.add_option("-t,--textures", opts.texturesArg,
                    "directory of already-converted '<FileDataID>.png' files, or 'none' to skip "
                    "embedding images (default: the model's own directory)");
    app.add_option("--skin-dir", opts.skinDirArg,
                    "directory 'auto' searches for the SFID-declared '<FileDataID>.skin' file, or "
                    "'none' to skip that stage (default: the model's own directory)");
    app.add_option("-a,--anim", opts.animArg,
                    "'auto': inline + global-sequence + best-effort external directory search; "
                    "'inline': inline + global-sequence only, no external search; 'none': no "
                    "animation clips at all (bind pose only); or a directory of "
                    "'<FileDataID>.anim' files, falling back to "
                    "'<model-basename><animId>-<subId>.anim' when a FileDataID-named file isn't found")
        ->capture_default_str();
    app.add_option("--skel", opts.skelArg,
                    "external .skel path (only relevant for a model with 0 inline bones), or "
                    "'none' to never look for one (default: a same-basename '.skel' next to the "
                    "model, if any)");
    app.add_option("--lod", opts.lodArg,
                    "'<n>' or 'all' -- only meaningful when --skin resolves via 'auto' (default: "
                    "entry 0)");
    app.add_option("--bones-dir", opts.bonesDirArg,
                    "directory of already-extracted '<FileDataID>.bone' files (per the model's/"
                    "'.skel's BFID array), or 'none' to skip (default: the model's own directory) "
                    "-- attached as inert glTF extras only, never applied to the render (see "
                    "TODO_correctness.md #3)");
    app.add_option("--phys", opts.physArg,
                    "external .phys path (per the model's own PFID scalar), or 'none' to never "
                    "look for one (default: a same-basename '.phys' next to the model, if any) -- "
                    "a minimal per-body placement anchor is attached as inert glTF extras; the "
                    "full body/shape/joint record set is available via 'husk dump-chunks' instead");
}

int exportGlb(int argc, char** args) {
    ExportOptions opts;
    CLI::App app{"husk export: WoW M2 (+ .skin/.skel/.anim sidecars) -> glTF 2.0 (.glb)",
                 "husk export"};
    addExportOptions(app, opts);

    try {
        // App::parse(vector<string>&) processes tokens back-to-front
        // (mirroring how its (argc, argv) sibling reverses argv before
        // handing off to the same internal _parse) -- reverse `args` into
        // that expected order, or every flag's value binds to the wrong
        // neighbor.
        std::vector<std::string> argVec(args, args + argc);
        std::reverse(argVec.begin(), argVec.end());
        app.parse(argVec);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    const std::string& modelPath = opts.modelPath;
    std::string outputPath = opts.outputPath;
    bool skinDirGiven = app.count("--skin-dir") > 0;
    bool lodGiven = !opts.lodArg.empty();

    // --skin-dir/--lod only mean anything alongside --skin auto (the
    // default) -- an explicit .skin path has no SFID-resolution stage for
    // either of them to modify.
    if (opts.skinArg != "auto") {
        if (skinDirGiven) {
            std::cerr << "husk: --skin-dir only does anything when --skin is 'auto'\n";
            return 1;
        }
        if (lodGiven) {
            std::cerr << "husk: --lod only does anything when --skin is 'auto'\n";
            return 1;
        }
    }
    if (lodGiven && skinDirGiven && opts.skinDirArg == "none") {
        std::cerr << "husk: --lod needs the SFID-based resolution --skin-dir 'none' disables\n";
        return 1;
    }

    if (app.count("--output") == 0) {
        outputPath = std::filesystem::path(modelPath).replace_extension(".glb").string();
        std::cerr << "husk: note: no output path given -- writing to '" << outputPath << "'\n";
    }

    std::filesystem::path modelDir = std::filesystem::path(modelPath).parent_path();
    std::string modelDirStr = modelDir.empty() ? "." : modelDir.string();

    // --textures/--skin-dir: three-state resolution (~/docs/CLI.md §2.11)
    // -- unset defaults to the model's own directory (a real casc-tool-
    // style extraction drops the .m2 and every sidecar it needs into one
    // directory together, see README.md's Usage section); an explicit
    // 'none' means deliberately skip, never attempted; anything else
    // overrides. (--skel gets the identical three states below, but its
    // "unset" default -- a same-basename '.skel' next to the model -- is a
    // filesystem *check*, not a fixed directory, so it's resolved later,
    // once bones are known to actually be needed.)
    std::string texturesDir = app.count("--textures") ? opts.texturesArg : modelDirStr;
    if (texturesDir == "none") texturesDir.clear();

    std::string bonesDir = app.count("--bones-dir") ? opts.bonesDirArg : modelDirStr;
    if (bonesDir == "none") bonesDir.clear();

    bool skinDirNone = skinDirGiven && opts.skinDirArg == "none";
    std::string skinDir = skinDirNone ? "" : (skinDirGiven ? opts.skinDirArg : modelDirStr);

    // --anim: four states, not three (see DESIGN.md's "Does inline
    // generalize past --anim?") -- inline M2Sequence/global-sequence bone
    // tracks and external-.anim-directory resolution are independent axes.
    // 'inline' deliberately leaves `animDir` empty rather than setting a
    // separate flag: buildAnimations already treats an empty animDir as
    // "skip external resolution" (its own doc comment), which is exactly
    // what 'inline' means -- inline/global-sequence tracks aren't gated on
    // animDir at all, so they still resolve.
    std::string animDir;
    bool animNone = false;
    if (opts.animArg == "auto") {
        animDir = modelDirStr;
    } else if (opts.animArg == "none") {
        animNone = true;
    } else if (opts.animArg != "inline") {
        animDir = opts.animArg;
    }

    bool skelGiven = app.count("--skel") > 0;
    bool skelNone = skelGiven && opts.skelArg == "none";
    std::string skelPath = (skelGiven && !skelNone) ? opts.skelArg : "";

    bool physGiven = app.count("--phys") > 0;
    bool physNone = physGiven && opts.physArg == "none";
    std::string physPath = (physGiven && !physNone) ? opts.physArg : "";

    try {
        auto modelBytes = readFileBytes(modelPath);
        auto header = m2::parseHeader(modelBytes);
        auto blob = m2::extractBlob(modelBytes);
        auto vertices = m2::parseVertices(blob, header.vertices);

        if (header.version < m2::kMinVerifiedRecordStrideVersion) {
            std::cerr << "husk: warning: '" << modelPath << "' is version " << header.version
                      << ", below Wrath (264) -- this parser's bones/sequences/ribbon_emitters "
                         "record sizes are only documented and verified for Wrath+; the "
                         "exported bind pose/animation/ribbon data may be silently wrong rather "
                         "than failing loudly (see FAILURES2.md #3)\n";
        }

        M2MaterialInputs m2Inputs;
        m2Inputs.materials = m2::parseMaterials(blob, header.materials);
        m2Inputs.textures = m2::parseTextures(blob, header.textures);
        m2Inputs.textureCombos = m2::parseUint16Array(blob, header.textureCombos);
        m2Inputs.textureCoordCombos = m2::parseUint16Array(blob, header.textureCoordCombos);
        m2Inputs.colors = m2::parseColors(blob, header.colors);
        m2Inputs.textureWeights = m2::parseTextureWeights(blob, header.textureWeights);
        m2Inputs.textureWeightCombos = m2::parseUint16Array(blob, header.textureWeightCombos);
        m2Inputs.textureTransforms = m2::parseTextureTransforms(blob, header.textureTransforms);
        m2Inputs.textureTransformCombos = m2::parseUint16Array(blob, header.textureTransformCombos);
        m2Inputs.textureFileDataIds = header.textureFileDataIds;
        m2Inputs.blob = &blob;
        m2Inputs.sequenceCount = header.sequences.count;

        // One (node name, .skin path) pair per LOD tier to export -- just
        // one, unnamed ("lod" only appears in a node name once --lod
        // resolves more than a single entry, see resolveAutoSkinPaths), for
        // every case except 'auto' + --lod all. --skin auto without an
        // explicit --lod folds the SFID-first/same-basename-scan-fallback
        // resolution (resolveSkin); an explicit --lod always commits to
        // the SFID stage (resolveAutoSkinPaths) since the same-basename
        // scan has no indexed/'all' equivalent to fall back to (see
        // DESIGN.md).
        std::vector<std::pair<std::string, std::string>> skinsToExport;
        if (opts.skinArg == "auto") {
            if (lodGiven) {
                skinsToExport = resolveAutoSkinPaths(header, skinDir, modelPath, opts.lodArg);
                for (const auto& [name, path] : skinsToExport) {
                    std::cerr << "husk: note: 'auto' resolved '" << path << "'"
                              << (name.empty() ? " (SFID entry 0, highest-detail LOD)\n"
                                                : " (SFID, " + name + ")\n");
                }
            } else {
                skinsToExport = resolveSkin(header, modelPath, skinDir, skinDirNone);
            }
        } else {
            skinsToExport.emplace_back("", opts.skinArg);
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
        // --skel: three-state resolution (~/docs/CLI.md §2.11) -- 'none'
        // means never look, even if a same-basename .skel exists (checked
        // first, regardless of bonesAreInline, since it's a deliberate
        // skip); an explicit path overrides; unset means auto-detect a
        // same-basename '.skel' next to the model (the README's own
        // worked example is exactly this shape: bloodelffemale_hd.m2 +
        // bloodelffemale_hd.skel). Inline bones already present makes any
        // of this moot -- only the "ignore it, and say so" note differs
        // between an explicit override and 'none' (nothing to ignore for
        // 'none', since the user already said not to look).
        if (bonesAreInline) {
            if (skelGiven && !skelNone) {
                std::cerr << "husk: note: '" << modelPath << "' has its own inline bones; ignoring '"
                          << skelPath << "'\n";
            }
        } else if (skelNone) {
            // Deliberately skip -- forces an unskinned mesh regardless of
            // whether a same-basename '.skel' actually exists.
        } else if (skelGiven) {
            skelBytes = readFileBytes(skelPath);
            haveSkel = true;
            bones = skel::parseBones(skelBytes);
        } else {
            // Not finding one isn't an error: plenty of 0-bone models
            // genuinely have no skeleton at all, and this model falls back
            // to the same unskinned-mesh output it always did.
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
        }

        gltf::Skeleton skeleton;
        std::vector<gltf::Animation> animations;
        if (!bones.empty()) {
            skeleton = buildSkeleton(bones);
            baseMesh.skinning = buildSkinning(vertices, bones.size());
            // --anim none: skip animation-clip resolution entirely (both
            // per-sequence and global-sequence), inline or external --
            // the bind pose (skinning, just built above) is unaffected,
            // only `animations` stays empty.
            if (!animNone && bonesAreInline) {
                auto sequences = m2::parseSequences(blob, header.sequences);
                M2AnimInputs animInputs;
                animInputs.animFileIds = header.animFileIds;
                animInputs.animChunked = (header.globalFlags & 0x200000) != 0;
                animInputs.animDir = animDir;
                animInputs.modelPath = modelPath;
                animations = buildAnimations(blob, bones, skeleton, sequences, animInputs);
                // Global-sequence-driven tracks (continuous looping
                // animation independent of any M2Sequence -- see
                // buildGlobalSequenceAnimations's doc comment,
                // FAILURES2.md #7) aren't tied to `sequences` at all, so
                // this runs unconditionally alongside the per-sequence
                // clips above, not gated on any of them existing.
                auto globalSeqAnims = buildGlobalSequenceAnimations(blob, bones, skeleton);
                animations.insert(animations.end(), std::make_move_iterator(globalSeqAnims.begin()),
                                   std::make_move_iterator(globalSeqAnims.end()));
            } else if (!animNone && haveSkel) {
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
                // Global-sequence-driven bone tracks are a property of the
                // bones themselves, not of SKS1's sequences, so those are
                // resolved even when a .skel has no SKS1 chunk at all.
                auto skelTrackBlob = skel::boneTrackBlob(skelBytes);
                if (findChunk(readChunks(skelBytes.data(), skelBytes.size()), "SKS1")) {
                    auto sequences = skel::parseSequences(skelBytes);
                    M2AnimInputs animInputs;
                    animInputs.animFileIds = skel::findAnimFileIds(skelBytes);
                    animInputs.animChunked = (header.globalFlags & 0x200000) != 0;
                    animInputs.animDir = animDir;
                    animInputs.modelPath = modelPath;
                    animations = buildAnimations(skelTrackBlob, bones, skeleton, sequences, animInputs);
                }
                auto globalSeqAnims = buildGlobalSequenceAnimations(skelTrackBlob, bones, skeleton);
                animations.insert(animations.end(), std::make_move_iterator(globalSeqAnims.begin()),
                                   std::make_move_iterator(globalSeqAnims.end()));
            }

            // --bones-dir: resolves each of the model's/.skel's BFID-
            // declared FileDataIDs to a real '<bonesDir>/<id>.bone' file,
            // if present (silently skipped otherwise, same "optional,
            // resolve what's there" policy --textures already uses for a
            // missing PNG) -- attached as inert gltf::Skeleton::
            // CorrectionSet extras, never applied to the bind pose/
            // animation above (see gltf.hpp's CorrectionSet doc comment,
            // TODO_correctness.md #3).
            if (!bonesDir.empty()) {
                std::optional<std::vector<uint32_t>> boneFileDataIds =
                    bonesAreInline ? header.boneFileDataIds
                                    : (haveSkel ? skel::findBoneFileDataIds(skelBytes)
                                                : std::nullopt);
                if (boneFileDataIds) {
                    size_t found = 0;
                    for (uint32_t fdid : *boneFileDataIds) {
                        auto bonePath =
                            std::filesystem::path(bonesDir) / (std::to_string(fdid) + ".bone");
                        std::error_code ec;
                        if (!std::filesystem::exists(bonePath, ec) || ec) {
                            continue;
                        }
                        auto boneBytes = readFileBytes(bonePath.string());
                        auto corrections = bone::parse(boneBytes);
                        gltf::Skeleton::CorrectionSet set;
                        set.fileDataId = fdid;
                        set.corrections.reserve(corrections.size());
                        for (const auto& c : corrections) {
                            if (c.boneIndex >= skeleton.joints.size()) {
                                throw std::runtime_error(
                                    "'" + bonePath.string() + "' corrects bone " +
                                    std::to_string(c.boneIndex) + ", out of range for " +
                                    std::to_string(skeleton.joints.size()) + " bones");
                            }
                            set.corrections.push_back(
                                {static_cast<int>(c.boneIndex), c.matrix});
                        }
                        skeleton.correctionSets.push_back(std::move(set));
                        ++found;
                    }
                    if (found > 0) {
                        std::cerr << "husk: note: attached " << found << "/"
                                  << boneFileDataIds->size()
                                  << " '.bone' correction set(s) from '" << bonesDir
                                  << "' as inert glTF extras (not applied to the render -- "
                                     "which slot is 'correct' for a given character depends "
                                     "on client-side customization-choice data husk doesn't "
                                     "have; see TODO_correctness.md #3)\n";
                    }
                }
            }

            // Ribbon/particle placement anchors (gltf::Skeleton::
            // EmitterAnchor's doc comment): unconditional, no CLI flag --
            // unlike --bones-dir's optional sidecar directory, this data
            // comes straight from the model's own already-parsed header
            // arrays, same "always attached" treatment as the geoset/
            // texture-transform extras. Full field/curve data lives in
            // `husk dump-chunks`, not here (see cmd_dump.cpp's doc
            // comment) -- this is placement only.
            for (const auto& r : m2::parseRibbons(blob, header.ribbonEmitters)) {
                if (r.boneIndex >= skeleton.joints.size()) {
                    throw std::runtime_error("ribbon emitter references bone " +
                                              std::to_string(r.boneIndex) + ", out of range for " +
                                              std::to_string(skeleton.joints.size()) + " bones");
                }
                skeleton.ribbonAnchors.push_back(
                    {r.ribbonId, static_cast<int>(r.boneIndex), toGltf(r.position)});
            }
            if (header.particleEmitters.count == 0 ||
                header.version >= m2::kMinVerifiedParticleVersion) {
                for (const auto& p : m2::parseParticles(blob, header.particleEmitters)) {
                    if (p.boneId >= skeleton.joints.size()) {
                        throw std::runtime_error("particle emitter references bone " +
                                                  std::to_string(p.boneId) + ", out of range for " +
                                                  std::to_string(skeleton.joints.size()) + " bones");
                    }
                    skeleton.particleAnchors.push_back(
                        {p.particleId, static_cast<int>(p.boneId), toGltf(p.position)});
                }
            }
            if (!skeleton.ribbonAnchors.empty() || !skeleton.particleAnchors.empty()) {
                std::cerr << "husk: note: attached " << skeleton.ribbonAnchors.size()
                          << " ribbon and " << skeleton.particleAnchors.size()
                          << " particle emitter placement anchor(s) as inert glTF extras (id/"
                             "bone/position only -- full field/curve data via `husk "
                             "dump-chunks`)\n";
            }

            // Attachment/Event/Light placement nodes (gltf::Skeleton::
            // Attachment/Event/Light's doc comments): unconditional, no CLI
            // flag, same "always attached" treatment as ribbon/particle
            // anchors above -- but unlike those, these become real child
            // glTF nodes, not skin extras, since a bone-relative position
            // marker is all M2Attachment/M2Event/M2Light static data ever
            // is (see M2_GAPS_TODO.md's former Item 6). `bone == -1`
            // ("not attached to any bone," real for M2Light and possibly
            // M2Attachment) is treated as out of range and throws -- husk
            // has no established "unparented placement node" concept yet.
            for (const auto& a : m2::parseAttachments(blob, header.attachments)) {
                if (a.bone < 0 || static_cast<size_t>(a.bone) >= skeleton.joints.size()) {
                    throw std::runtime_error("attachment " + std::to_string(a.id) +
                                              " references bone " + std::to_string(a.bone) +
                                              ", out of range for " +
                                              std::to_string(skeleton.joints.size()) + " bones");
                }
                skeleton.attachments.push_back({a.id, static_cast<int>(a.bone), toGltf(a.position)});
            }
            for (const auto& e : m2::parseEvents(blob, header.events)) {
                if (e.bone >= skeleton.joints.size()) {
                    throw std::runtime_error("event '" + e.identifier + "' references bone " +
                                              std::to_string(e.bone) + ", out of range for " +
                                              std::to_string(skeleton.joints.size()) + " bones");
                }
                skeleton.events.push_back({e.identifier, static_cast<int>(e.bone), toGltf(e.position)});
            }
            for (const auto& l : m2::parseLights(blob, header.lights)) {
                if (l.bone < 0 || static_cast<size_t>(l.bone) >= skeleton.joints.size()) {
                    throw std::runtime_error("light references bone " + std::to_string(l.bone) +
                                              ", out of range for " +
                                              std::to_string(skeleton.joints.size()) + " bones");
                }
                skeleton.lights.push_back({static_cast<int>(l.bone), toGltf(l.position)});
            }
            if (!skeleton.attachments.empty() || !skeleton.events.empty() ||
                !skeleton.lights.empty()) {
                std::cerr << "husk: note: attached " << skeleton.attachments.size()
                          << " attachment, " << skeleton.events.size() << " event, and "
                          << skeleton.lights.size()
                          << " light placement node(s) to the exported skeleton\n";
            }

            // --phys: three-state resolution mirroring --skel (DESIGN.md's
            // Key design decisions -- PFID is a single scalar FileDataID,
            // like SKID, not an array like BFID/AFID/SFID, so a directory
            // flag doesn't apply here the way it does for --bones-dir).
            // 'none' means never look, even if a same-basename .phys
            // exists; an explicit path overrides; unset auto-detects a
            // same-basename '.phys' next to the model. Not finding one isn't
            // an error -- most models have no physics data at all. Only the
            // minimal per-body placement anchor (gltf::Skeleton::
            // PhysicsBody's doc comment) is attached here; the full body/
            // shape/joint/PHYV record set is available via `husk
            // dump-chunks` instead (same split as ribbon/particle above).
            std::string resolvedPhysPath;
            if (physNone) {
                // Deliberately skip -- forces no physics_bodies extras
                // regardless of whether a same-basename '.phys' exists.
            } else if (physGiven) {
                resolvedPhysPath = physPath;
            } else {
                auto defaultPhys = std::filesystem::path(modelPath).replace_extension(".phys");
                std::error_code ec;
                if (std::filesystem::exists(defaultPhys, ec) && !ec) {
                    resolvedPhysPath = defaultPhys.string();
                }
            }
            if (!resolvedPhysPath.empty()) {
                auto physBytes = readFileBytes(resolvedPhysPath);
                auto physFile = phys::parse(physBytes);
                skeleton.physicsBodies.reserve(physFile.bodies.size());
                for (size_t bi = 0; bi < physFile.bodies.size(); ++bi) {
                    const auto& b = physFile.bodies[bi];
                    if (b.boneIndex >= skeleton.joints.size()) {
                        throw std::runtime_error(
                            "'" + resolvedPhysPath + "' body " + std::to_string(bi) +
                            " references bone " + std::to_string(b.boneIndex) +
                            ", out of range for " + std::to_string(skeleton.joints.size()) +
                            " bones");
                    }
                    skeleton.physicsBodies.push_back({static_cast<uint32_t>(bi),
                                                        static_cast<int>(b.boneIndex),
                                                        toGltf(b.position), b.type});
                }
                if (!skeleton.physicsBodies.empty()) {
                    std::cerr << "husk: note: attached " << skeleton.physicsBodies.size()
                              << " physics body placement anchor(s) from '" << resolvedPhysPath
                              << "' as inert glTF extras (id/bone/position/type only -- full "
                                 "body/shape/joint record data via `husk dump-chunks`)\n";
                }
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
            //
            // Report every out-of-range index found (
            // count + the worst offender), not just the first -- a real
            // wrong-.skin pairing references hundreds of out-of-range
            // indices, not one, and the first-hit index alone made two
            // genuinely identical bugs look like different shapes (a small
            // "off by one" vs. a "gap of dozens") purely as an artifact of
            // where in `triangleIndices` iteration happened to first fail.
            uint32_t maxOutOfRange = 0;
            size_t outOfRangeCount = 0;
            for (uint32_t idx : triangleIndices) {
                if (idx >= vertices.size()) {
                    maxOutOfRange = std::max(maxOutOfRange, idx);
                    ++outOfRangeCount;
                }
            }
            if (outOfRangeCount > 0) {
                throw std::runtime_error(
                    "'" + path + "' references " + std::to_string(outOfRangeCount) +
                    " out-of-range M2 vertex index(es) (up to " + std::to_string(maxOutOfRange) +
                    ") but '" + modelPath + "' only has " + std::to_string(vertices.size()) +
                    " vertices -- model/.skin mismatch?");
            }

            auto built = buildMaterialsAndPrimitives(triangleIndices, submeshes, batches, m2Inputs,
                                                       texturesDir);

            // FAILURES2.md #1: husk doesn't filter geosets (skinSectionId) --
            // every submesh in the .skin file is exported unconditionally,
            // even when several are mutually-exclusive character-
            // customization options (different hairstyles, facial hair,
            // gear slots, ...) a real client would only ever draw one of at
            // a time. Loudly note it rather than silently merging them with
            // no indication multiple geosets ended up in the output.
            if (built.distinctSkinSectionIds.size() > 1) {
                std::cerr << "husk: note: '" << path << "'"
                          << (name.empty() ? "" : " (" + name + ")")
                          << "'s batches span " << built.distinctSkinSectionIds.size()
                          << " distinct geoset IDs (skinSectionId: ";
                for (size_t i = 0; i < built.distinctSkinSectionIds.size(); ++i) {
                    if (i) std::cerr << ", ";
                    std::cerr << built.distinctSkinSectionIds[i];
                }
                std::cerr << ") -- husk doesn't filter geosets yet, so every one of them is "
                             "exported unfiltered into this mesh (see FAILURES2.md #1)\n";
            }

            // FAILURES2.md #6: a batch with textureCount > 1 has real,
            // additional texture layers (a second env-mapped "shine" pass,
            // a genuine two-texture blend) that husk still only wires the
            // *first* of into the actual glTF material (WoW's fixed-
            // function combiner math has no core-glTF equivalent to
            // translate it into) -- the rest are captured as inert
            // `extras` metadata (gltf::Material::additionalTextureLayers)
            // for a custom renderer or Blender script to use, not silently
            // dropped, but still worth noting since the *rendered* result
            // remains single-texture.
            if (built.multiTextureBatchCount > 0) {
                std::cerr << "husk: note: '" << path << "'"
                          << (name.empty() ? "" : " (" + name + ")") << "' has "
                          << built.multiTextureBatchCount
                          << " batch(es) with more than one texture (textureCount > 1) -- husk "
                             "only wires the first texture per batch into the rendered "
                             "material; additional layers are exported as inert 'extras' "
                             "metadata (see FAILURES2.md #6), not applied to the render\n";
            }

            // A batch's M2Color/M2TextureWeight can be
            // real per-sequence or global-sequence keyframe animation (an
            // eye-glow or enchant-glow pulse, say), not the single constant
            // value gltf::Material::baseColorFactor can actually hold --
            // unlike a bone's translation/rotation/scale (a real, animatable
            // glTF node property, see resolveVec3GlobalSequenceTrack/
            // FAILURES2.md #7), core glTF has no way to *play back* a
            // material property's animation, so the rendered material still
            // uses each batch's static default -- but the real curve is now
            // resolved and attached as inert `tint_animation`/
            // `fade_animation` extras (see gltf::Material's doc comments),
            // not just dropped.
            if (built.animatedTintOrFadeBatchCount > 0) {
                std::cerr << "husk: note: '" << path << "'"
                          << (name.empty() ? "" : " (" + name + ")") << "' has "
                          << built.animatedTintOrFadeBatchCount
                          << " batch(es) whose color tint (M2Color) or transparency fade "
                             "(M2TextureWeight) is animated (per-sequence or global-sequence "
                             "keyframes, not a single constant value) -- core glTF has no way to "
                             "animate a material's baseColorFactor, so each batch's static "
                             "default is used on the render, but the real curve is attached as "
                             "'tint_animation'/'fade_animation' extras\n";
            }

            // FINDINGS.md §3.1: a batch's textureTransformComboIndex
            // resolved to a real M2TextureTransform (UV scroll/rotate/scale
            // animation, e.g. flowing lava/water) -- exposed as inert
            // extras (gltf::Material::textureTransform), not applied to the
            // render, see m2::TextureTransform's doc comment for why.
            if (built.textureTransformBatchCount > 0) {
                std::cerr << "husk: note: '" << path << "'"
                          << (name.empty() ? "" : " (" + name + ")") << "' has "
                          << built.textureTransformBatchCount
                          << " batch(es) with a UV transform (M2TextureTransform) -- exported as "
                             "inert 'extras' metadata on the material, not applied to the "
                             "render (see FINDINGS.md §3.1)\n";
            }

            // No primitives came out of this LOD tier's
            // .skin (genuinely geometry-less M2, or every batch's submesh
            // had zero indices) -- glTF requires a mesh's own primitives
            // list to be non-empty, so there's no valid mesh to emit here.
            // Skip the NamedMesh entirely rather than manufacturing an
            // empty one; the model's skeleton and ribbon/particle emitter
            // anchors (built unconditionally above) still export.
            if (built.primitives.empty()) {
                std::cerr << "husk: note: '" << path << "'"
                          << (name.empty() ? "" : " (" + name + ")")
                          << "' has no renderable geometry -- skipping mesh output for this LOD "
                             "tier (skeleton and ribbon/particle emitter anchors, if any, are "
                             "still exported)\n";
                continue;
            }

            gltf::NamedMesh nm;
            nm.name = name;
            nm.mesh = baseMesh;
            nm.mesh.primitives = built.primitives;
            nm.materials = std::move(built.materials);
            namedMeshes.push_back(std::move(nm));
        }

        // Captured before the collision mesh (if any) is appended below --
        // the final summary needs to know how many of `namedMeshes` are
        // real render/LOD entries versus the trailing collision entry, so
        // it doesn't mislabel the collision mesh as another LOD tier.
        size_t renderMeshCount = namedMeshes.size();

        // The collision mesh (physics/hit-testing, m2::CollisionMesh) is a
        // plain triangle mesh with an unambiguous glTF translation --
        // unlike geoset selection/multi-
        // texture-layers (data with no unambiguous glTF shape, hence inert
        // extras only), so it's exported as real geometry: one more
        // NamedMesh, unskinned (a collision mesh is static, not deformed by
        // the armature -- see writeGlbMulti's doc comment for sharing a
        // skeleton without being skinned by it), tagged `isCollision` so
        // writeGlbMulti marks its node `{"collision": true}` in extras.
        // Silently skipped when the model has no collision data at all
        // (count 0 -- not every M2 necessarily has some), same "quiet when
        // nothing applies" policy as --textures.
        if (header.collisionPositions.count > 0 && header.collisionIndices.count > 0) {
            auto collisionMesh = m2::parseCollisionMesh(blob, header.collisionPositions,
                                                          header.collisionIndices,
                                                          header.collisionFaceNormals);

            if (collisionMesh.indices.size() % 3 != 0) {
                throw std::runtime_error(
                    "'" + modelPath + "'s collision mesh has " +
                    std::to_string(collisionMesh.indices.size()) +
                    " indices, not a multiple of 3 (one triangle per 3 entries)");
            }
            for (uint16_t idx : collisionMesh.indices) {
                if (idx >= collisionMesh.positions.size()) {
                    throw std::runtime_error(
                        "'" + modelPath + "'s collision mesh index " + std::to_string(idx) +
                        " is out of range for " + std::to_string(collisionMesh.positions.size()) +
                        " collision positions");
                }
            }
            size_t triCount = collisionMesh.indices.size() / 3;
            if (!collisionMesh.faceNormals.empty() && collisionMesh.faceNormals.size() != triCount) {
                throw std::runtime_error(
                    "'" + modelPath + "'s collision mesh has " +
                    std::to_string(collisionMesh.faceNormals.size()) + " face normals for " +
                    std::to_string(triCount) + " triangles");
            }

            gltf::Mesh collisionGltfMesh;
            collisionGltfMesh.positions.reserve(collisionMesh.positions.size());
            for (const auto& p : collisionMesh.positions) {
                collisionGltfMesh.positions.push_back(toGltf(p));
            }

            // No per-vertex normals exist in the source data (only one
            // face normal per triangle, m2::CollisionMesh's doc comment) --
            // approximate them by averaging every adjacent triangle's face
            // normal at each shared vertex, the standard flat-to-smooth
            // normal derivation. A collision mesh isn't shaded/rendered in
            // practice, so this is only to satisfy gltf::Mesh's own
            // positions/normals/texCoords-all-same-length invariant with
            // real, finite, unit-length data rather than a placeholder.
            std::vector<gltf::Vec3> normalSums(collisionMesh.positions.size(), gltf::Vec3{0, 0, 0});
            if (!collisionMesh.faceNormals.empty()) {
                for (size_t t = 0; t < triCount; ++t) {
                    gltf::Vec3 faceNormal = toGltf(collisionMesh.faceNormals[t]);
                    for (int c = 0; c < 3; ++c) {
                        uint16_t vi = collisionMesh.indices[t * 3 + static_cast<size_t>(c)];
                        normalSums[vi].x += faceNormal.x;
                        normalSums[vi].y += faceNormal.y;
                        normalSums[vi].z += faceNormal.z;
                    }
                }
            }
            collisionGltfMesh.normals.reserve(normalSums.size());
            for (const auto& sum : normalSums) {
                float len = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);
                collisionGltfMesh.normals.push_back(
                    len > 1e-8f ? gltf::Vec3{sum.x / len, sum.y / len, sum.z / len}
                                : gltf::Vec3{0, 1, 0});
            }
            collisionGltfMesh.texCoords.assign(collisionMesh.positions.size(), gltf::Vec2{0, 0});

            gltf::Primitive collisionPrim;
            collisionPrim.indices.assign(collisionMesh.indices.begin(), collisionMesh.indices.end());
            collisionGltfMesh.primitives = {collisionPrim};

            gltf::NamedMesh collisionNamedMesh;
            collisionNamedMesh.name = "collision";
            collisionNamedMesh.mesh = std::move(collisionGltfMesh);
            collisionNamedMesh.isCollision = true;
            namedMeshes.push_back(std::move(collisionNamedMesh));

            std::cout << "husk: note: attached a " << collisionMesh.positions.size()
                      << "-position/" << triCount
                      << "-triangle collision mesh (unskinned, tagged 'collision' in glTF "
                         "extras, not applied to the render)\n";
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

        if (renderMeshCount == 0) {
            // Every LOD tier's .skin was geometry-less
            // (or there was only ever one tier and it was empty) -- no mesh
            // node exists in this .glb at all, only the skeleton and
            // whatever ribbon/particle emitter anchors were attached above.
            std::cout << outputPath << ": no renderable geometry (particle/ribbon-effect model?)";
            if (!bones.empty()) {
                std::cout << ", " << bones.size() << " bones";
                if (!animations.empty()) {
                    std::cout << ", " << animations.size() << " animation(s)";
                } else {
                    std::cout << " (bind pose only, no animation)";
                }
            }
            std::cout << "\n";
        } else if (renderMeshCount == 1) {
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
                      << renderMeshCount << " LOD tier(s) as separate nodes:\n";
            for (size_t mi = 0; mi < renderMeshCount; ++mi) {
                const auto& nm = namedMeshes[mi];
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
