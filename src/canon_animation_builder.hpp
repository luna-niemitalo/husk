#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "canon_curve.hpp"
#include "m2_skeleton.hpp"  // m2::Bone

// husk::canon: assembly of a bone's raw translation/rotation/scale curves
// from an inline M2Sequence's own keyframe data -- the adjacent twin of
// commands::buildJointAnimation (export_animation.hpp), built to prove
// structural convergence (REFACTOR/README.md stage 3's gate) without
// touching the existing pipeline. assembleBoneAnimation covers the
// per-M2Sequence case (SequenceRef::sequence); assembleBoneAnimationGlobal
// (below) covers the Header::globalLoops-scoped case (SequenceRef::
// globalSequence), the adjacent twin of commands::buildGlobalSequenceAnimations.
// Alias-sequence resolution reuses an already-assembled AnimationClip rather
// than calling either of these a second time (canon_model.cpp's own doc
// comment). External .anim files are handled by this same
// assembleBoneAnimation, via its own `externalBlob` parameter below --
// canon_model.cpp's assembleModel takes already-resolved external blob
// bytes, keyed by sequence, as an input parameter; deciding WHICH
// FileDataID to fetch and reading it off disk stays entirely outside
// canon:: (REFACTOR/AUDIT.md §7.2, invariant I1: "no paths in canon::") --
// see commands::buildAnimations for that filesystem-touching logic, not
// mirrored here.
namespace husk::canon {

// One bone's worth of assembled curves for one M2Sequence. Any of the
// three may hold zero keyframes (that property simply has no data for
// this sequence) -- assembleBoneAnimation only returns nullopt when all
// three are empty, mirroring buildJointAnimation's own "nothing to
// animate" early return.
struct BoneAnimationCurves {
    VecCurve translation;
    QuatCurve rotation;
    VecCurve scale;
};

// `blob`/`bone`/`sequenceIndex`/`externalBlob` feed straight into
// m2::resolveVec3TrackSequence/resolveQuatTrackSequence -- the same real
// track-resolution functions buildAnimations calls, not a re-derivation
// of that logic (TOOLS.md documents the cost of a hand-mirrored copy
// silently drifting from the real thing).
//
// Deliberately does NOT: convert Z-up to Y-up (toGltf/toGltfScale -- a
// writer's own axis-space choice), add bind-pose translation (glTF's
// local-transform representation, not a fact about the M2 track's own
// delta-from-bind-pose data), or fix quaternion hemisphere continuity.
// That last one was a real judgment call, not an oversight: raw
// M2CompQuat keyframes are a direct linear int16 decode (m2_animation.cpp's
// readCompQuat) with no per-keyframe sign choice, and the game client
// itself must interpolate these natively, so authored data is already
// hemisphere-continuous in M2 space. gltf::enforceHemisphereContinuity
// compensates for a discontinuity introduced by
// gltf::rotationZUpToYUp's own matrix round-trip (quatToMat3 ->
// mat3ToQuat), whose sign "isn't normalized against any convention" per
// its own doc comment -- an artifact of that specific axis-conversion
// technique, not a property of the underlying rotation data. A different
// writer converting axes a different way might not reintroduce the
// discontinuity at all, so the fix stays writer-side.
//
// `boneIndex` is carried only for repairDuplicateTimestampsAndValidate's
// diagnostic messages (export_transform.hpp) -- reused as-is (foreign-
// data validation, not a writer convention) rather than re-implemented,
// same one-source-of-truth reasoning as the resolve*TrackSequence calls
// above.
std::optional<BoneAnimationCurves> assembleBoneAnimation(const std::vector<uint8_t>& blob,
                                                            const m2::Bone& bone, size_t boneIndex,
                                                            uint32_t sequenceIndex,
                                                            const std::vector<uint8_t>* externalBlob = nullptr);

// Global-sequence counterpart to assembleBoneAnimation -- the adjacent twin
// of commands::buildGlobalSequenceAnimations's own per-bone resolution
// (export_animation.cpp): resolves `bone`'s translation/rotation/scale
// tracks against `globalSequenceIndex` (an index into
// m2::Header::globalLoops) via m2::resolveVec3GlobalSequenceTrack/
// resolveQuatGlobalSequenceTrack instead of the *TrackSequence family,
// gated per-property the same way legacy is: a track only contributes when
// its own m2::TrackMeta::globalSequence equals `globalSequenceIndex`
// (a bone's three tracks need not all share one global sequence, or be
// global-sequence-driven at all). No `externalBlob` parameter -- global
// sequences have no external-.anim mechanism of their own (see
// buildGlobalSequenceAnimations's own doc comment). Returns nullopt under
// the same "nothing to animate" condition as assembleBoneAnimation.
std::optional<BoneAnimationCurves> assembleBoneAnimationGlobal(const std::vector<uint8_t>& blob,
                                                                  const m2::Bone& bone, size_t boneIndex,
                                                                  uint16_t globalSequenceIndex);

}  // namespace husk::canon
