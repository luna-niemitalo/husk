#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "gltf.hpp"
#include "m2.hpp"

// Animation-clip resolution (per-M2Sequence and global-sequence alike, for
// both inline and external-.anim-sourced keyframe data) -- split out of
// cmd_export.cpp per FILE_SPLIT_TODO.md's Item 1.
namespace husk::commands {

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

// Resolves an alias sequence (M2Sequence::flags & kSequenceAliasFlag) to
// its terminal non-alias sequence's own index into `sequences`, by
// repeatedly following aliasNext until a sequence without the alias flag
// is reached. `startIndex` need not itself be an alias (returns
// `startIndex` unchanged in that case). Real data never cycles, but this
// is foreign file data -- bounded to `sequences.size()` hops (an acyclic
// chain can visit at most that many distinct sequences before it would
// have to repeat one), throwing rather than looping forever if a real
// cycle, or an out-of-range aliasNext, ever shows up.
size_t resolveAliasChain(const std::vector<m2::Sequence>& sequences, size_t startIndex);

// Lifts an m2::Sequence's own per-sequence metadata (movespeed/frequency/
// replay/blendTime/bounds/variationNext/aliasNext) into
// gltf::Animation::SequenceMetadata -- see that struct's doc comment for
// why these are exposed as inert clip `extras` rather than applied to
// anything.
// `animationDataName`: real AnimationData.db2 Name for `seq.id`, or empty
// when unresolved (no --db2-dir/--dbd-dir given, or no matching row) --
// see gltf::Animation::SequenceMetadata::animationDataName's own doc
// comment.
gltf::Animation::SequenceMetadata buildSequenceMetadata(const m2::Sequence& seq,
                                                          const std::string& animationDataName = {});

// Real wow.export-style extractions name external .anim files
// <model-basename><animId:04d>-<subAnimId:02d>.anim next to the model, not
// by FileDataID. Direct filename construction, not a directory scan like
// findSameBasenameSkins -- (animId, subAnimId) fully determines the name,
// so there's no ambiguity to resolve. Returns the constructed path
// unconditionally; existence is checked by the caller's own ifstream-open
// attempt, same as the FileDataID path.
std::filesystem::path findAnimFileByBasename(const std::string& modelPath, const std::string& animDir,
                                              uint16_t animId, uint16_t subAnimId);

// Builds one JointAnimation for bone `bi` from already-resolved translation/
// rotation/scale keyframes -- shared by buildAnimations (per-M2Sequence
// resolution) and buildGlobalSequenceAnimations (global-sequence, single-
// track resolution), which resolve *which* keyframes apply differently but
// build the resulting glTF channel data identically once resolved. Returns
// nullopt if all three are empty (nothing to animate for this bone in this
// clip) -- an empty JointAnimation isn't useful output. Validates every
// keyframe first via repairDuplicateTimestampsAndValidate (finiteness +
// strictly-increasing timestamps).
std::optional<gltf::JointAnimation> buildJointAnimation(
    const std::vector<uint8_t>& blob, const m2::Bone& bone, size_t bi, const gltf::Skeleton& skeleton,
    std::vector<std::pair<uint32_t, m2::Vec3>>& translation,
    std::vector<std::pair<uint32_t, m2::Quat>>& rotation,
    std::vector<std::pair<uint32_t, m2::Vec3>>& scale);

// Builds one glTF animation clip per distinct global_sequence index actually
// used by any of `bones`' translation/rotation/scale tracks -- a
// continuously-looping animation independent of any M2Sequence (eye glow
// pulses, torch flicker, idle sway; wowdev.wiki "Global Sequences": "always
// loops"). `blob`/`bones`/`skeleton` are the same triple buildAnimations
// takes (inline M2 or .skel-sourced) -- global-sequence tracks have no
// external-.anim mechanism of their own, so there's no `animInputs`/
// external-blob parameter here. Named "global_seq_<index>" per clip.
std::vector<gltf::Animation> buildGlobalSequenceAnimations(const std::vector<uint8_t>& blob,
                                                             const std::vector<m2::Bone>& bones,
                                                             const gltf::Skeleton& skeleton);

// Builds one glTF animation clip per M2Sequence that has resolvable
// keyframe data -- either inline (flags & 0x20) or, when `animInputs`
// resolves one, in an external .anim file (see M2AnimInputs, findAnimFileId,
// m2::extractAnimBlob) -- covering every bone that has real (non-empty)
// translation/rotation/scale keyframes for that specific sequence. Works
// equally for a model's own inline bones+sequences (`blob` = the MD20 blob,
// `sequences` from m2::parseSequences) and a .skel-sourced skeleton (`blob`
// = skel::boneTrackBlob's SKB1 payload, `sequences` from
// skel::parseSequences, `animInputs.animFileIds` from skel::findAnimFileIds
// -- a .skel's own AFID table, not the owning M2's, see skel.hpp).
// `skeleton` must be the already-built bind-pose Skeleton for these same
// `bones`, in the same order, since each keyframe's translation channel
// value is bind-pose-relative-to-parent plus the animated delta. Sequences
// with no bone actually carrying resolvable data for them are skipped -- an
// empty animation clip isn't useful output. A malformed .anim file throws
// rather than being silently skipped; only a genuinely *missing* file, or a
// chunked .anim with neither an `AFM2` nor an `AFSB` chunk, is treated as
// "husk doesn't have this one."
//
// An alias sequence (flags & kSequenceAliasFlag) is resolved via
// resolveAliasChain to its terminal non-alias sequence -- but only when
// this sequence doesn't *also* carry kSequenceStoredInlineFlag: a sequence
// with both flags set already carries its own real inline M2Track data and
// must not have it overwritten by the resolved terminal sequence's data.
// Either way, the resulting clip's *name* and `sequenceMetadata` extras
// always come from this sequence's own M2Sequence record (`originalSeq`),
// so it's registered under its own id/index even when reusing borrowed
// data.
//
// see DESIGN.md#Key-design-decisions for the AFSB byte-layout investigation
// and the aliasNext chain-resolution investigation behind the two
// paragraphs above.
// `animationNames`: optional real AnimationData.db2 id -> Name lookup
// (animationdata::toNameMap) -- when given, a sequence whose id has a real
// row gets that name attached as sequenceMetadata.animationDataName (see
// its own doc comment for why this doesn't rename the clip itself). Null
// (the default) when --db2-dir/--dbd-dir weren't given at export time.
std::vector<gltf::Animation> buildAnimations(
    const std::vector<uint8_t>& blob, const std::vector<m2::Bone>& bones, const gltf::Skeleton& skeleton,
    const std::vector<m2::Sequence>& sequences, const M2AnimInputs& animInputs,
    const std::unordered_map<uint32_t, std::string>* animationNames = nullptr);

}  // namespace husk::commands
