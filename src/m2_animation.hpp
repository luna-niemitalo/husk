#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "m2_primitives.hpp"

// M2 animation sequences, per-property track resolution (bone translation/
// rotation/scale, material color/alpha/weight/texture-transform curves,
// particle-emitter FBlocks), and global-sequence loops -- split out of the
// former monolithic m2.hpp/m2.cpp (see FILE_SPLIT_TODO.md Item 2). See
// m2_primitives.hpp for the module overview (on-disk shapes, blob-
// resolution entry points). This is the biggest of the five split files by
// design: every M2Track<T>/FBlock<T>/M2PartTrack<T> resolver lives here,
// regardless of which struct's field it services, since they all share the
// same handful of byte-layout shapes.
namespace husk::m2 {

// M2Range, per wowdev.wiki Common_Types -- a plain {minimum, maximum}
// uint32_t pair. Only user so far is M2Sequence::replay below.
struct Range {
    uint32_t minimum = 0;
    uint32_t maximum = 0;
};

// M2Sequence, per wowdev.wiki M2#Animation_sequences -- 0x40 (64) bytes for
// every version this parser targets (WotLK+, matching Bone's own minimum;
// the pre-6.0.1-vs-later blendTimeIn/blendTimeOut-vs-blendTime split
// doesn't move any of the offsets below, so it doesn't matter that this
// parser doesn't read that field at all). The wiki's own struct listing
// shows an "M2Bounds bounds;" field with no offset comment, right where a
// stale-looking "/*0x20*/ int16_t variationNext" annotation immediately
// follows it -- easy to misread as a documentation artifact (36-byte
// stride, bounds omitted) rather than a real 28-byte field the wiki just
// forgot to re-number after inserting. It's real -- every field below
// fits cleanly into the confirmed 64-byte stride with no gap/overlap:
// /*0x00*/ id, /*0x02*/ variationIndex, /*0x04*/ duration, /*0x08*/
// movespeed, /*0x0C*/ flags, /*0x10*/ frequency (+2 bytes padding),
// /*0x14*/ replay, /*0x1C*/ blendTimeIn, /*0x1E*/ blendTimeOut, /*0x20*/
// bounds (Vec3 min + Vec3 max, 24 bytes), /*0x38*/ boundsRadius, /*0x3C*/
// variationNext, /*0x3E*/ aliasNext.
// TODO: Remove: verified against bloodelffemale.m2 (a 36-byte stride
// decodes garbage, 64 bytes decodes all 339 sequences sanely); former
// M2_GAPS_TODO.md Item 1.
struct Sequence {
    uint16_t id = 0;             // AnimationData.dbc id -- husk has no DBC access, so this is
                                  // surfaced as a raw number, never resolved to a human name
    uint16_t variationIndex = 0; // which sub-animation in a row of same-id animations
    uint32_t duration = 0;       // milliseconds
    float movespeed = 0;         // used to sync e.g. run-cycle animation speed to actual movement speed
    // M2Sequence flags (wowdev.wiki M2#Animation_sequences's Flags table).
    // The two bits husk's animation export actually checks are 0x20
    // ("primary bone sequence": if set, this sequence's M2Track keyframe
    // data lives inline in this M2; if not, it's in an external .anim file
    // -- see Header::animFileIds) and 0x40 ("is alias" -- see aliasNext's
    // doc comment below) -- exposing the whole field rather than just
    // bools in case a future caller needs another bit.
    uint32_t flags = 0;
    int16_t frequency = 0;  // how often this sequence is played, relative to sibling variations
    Range replay;            // wowdev.wiki: "may be overridden by sound data"
    uint16_t blendTimeIn = 0;   // milliseconds
    uint16_t blendTimeOut = 0;  // milliseconds
    BoundingBox bounds;    // this sequence's own animated bounding box, model space
    float boundsRadius = 0;  // bounding sphere radius, same M2Bounds record as `bounds`
    int16_t variationNext = -1;  // id of the following variation, or -1 if this is the last
    // Local index into this same file's own `sequences` array (not an
    // AnimationData.dbc id, despite the wiki's own "id in the list of
    // animations" doc comment). Only meaningful when `flags & 0x40` is set
    // ("is alias") -- per the wiki, "the client skips these by following
    // aliasNext until an animation without 0x40 is found," which is
    // exactly what cmd_export.cpp's resolveAliasChain does to reuse the
    // terminal sequence's own keyframe data for this (otherwise-dataless)
    // alias sequence.
    // TODO: Remove: `WIKI_FINDINGS/M2.md` -- confirmed against 157 real
    // alias sequences across 4 real files, 100% valid in-range indices, zero cycles.
    uint16_t aliasNext = 0;
};

// M2Color / M2TextureWeight, per wowdev.wiki M2#Colors_and_transparency --
// referenced from a .skin Batch's colorIndex/textureWeightComboIndex (see
// husk::skin::Batch) to tint/fade a texture-unit's material. Both are
// M2Track<T>-backed (real keyframe animation, roadmap stage 6, not parsed
// here) -- what's surfaced instead is a value *only when the track is
// unambiguously constant* (exactly one animation sub-array with exactly
// one keyframe in it -- see src/m2_animation.cpp's constantTrackValueOffset
// for why anything looser is a real correctness trap, not just an
// approximation worth avoiding). `nullopt` covers both "genuinely no data"
// and "this track is actually animated, which is out of scope here" --
// callers fall back to that field's natural default (opaque white / full
// weight) either way, since a wrong guess (e.g. reading an animated alpha
// track's unrelated first keyframe) can be worse than no value at all.
//
// The `*Animated` flags below distinguish those two nullopt cases for a
// caller that wants to *say something* about the second one: core glTF
// has no way to animate a material's baseColorFactor at all (unlike a
// bone's translation/rotation/scale, which are real animatable node
// properties), so there's no real *playback* to build -- but the full
// curve is still real, useful data for a custom renderer or Blender
// script, resolved via `*TrackOffset` below the same way buildAnimations
// resolves bone tracks (resolveVec3TrackSequence/resolveRawIntTrackSequence,
// see cmd_export.cpp's buildMaterialsAndPrimitives) and attached as inert
// glTF material `extras` (`tint_animation`/`fade_animation`).
// TODO: Remove: FAILURES2.md #7 citation (the global-sequence bone-track
// fix this parenthetical referenced).
struct Color {
    std::optional<Vec3> color;   // 0..1 per channel, rgb order
    std::optional<float> alpha;  // 0 (transparent) .. 1 (opaque)
    bool colorAnimated = false;  // true iff `color` is nullopt *because* the track has real keyframe data
    bool alphaAnimated = false;  // true iff `alpha` is nullopt *because* the track has real keyframe data
    // Raw M2Track<C3Vector>/M2Track<fixed16> byte offsets for `color`/
    // `alpha` -- always set (regardless of *Animated), so a caller can
    // resolve the real curve itself when the flag is true.
    uint32_t colorTrackOffset = 0;
    uint32_t alphaTrackOffset = 0;
};

struct TextureWeight {
    std::optional<float> weight;  // 0..1; wiki: "I assume these are multiplied together" with Color::alpha
    bool weightAnimated = false;  // true iff `weight` is nullopt *because* the track has real keyframe data
    uint32_t weightTrackOffset = 0;  // raw M2Track<fixed16> byte offset for `weight`, see Color's doc comment
};

// M2TextureTransform, per wowdev.wiki M2#Texture_Transforms -- UV
// scroll/rotate/scale animation (flowing lava/water, some portal/aura
// effects). Referenced from a .skin Batch's textureTransformComboIndex
// (via Header::textureTransformCombos) -- see husk::skin::Batch. Like
// Color/TextureWeight above, all three fields are M2Track<T>-backed and
// only resolved here when unambiguously constant; the `*Animated` flags
// mirror Color's own for the non-constant case.
//
// Unlike Color/TextureWeight, the constant case *is* applied to the
// rendered material as a real KHR_texture_transform, pivot-corrected from
// the wiki's texture-center (0.5, 0.5) rotation -- see
// gltf::Material::TextureTransform's doc comment for the derivation and
// why the animated case still can't be.
struct TextureTransform {
    std::optional<Vec3> translation;
    std::optional<Quat> rotation;  // C4Quaternion -- 4 raw floats (x,y,z,w), NOT the compressed M2CompQuat bones use
    std::optional<Vec3> scaling;
    bool translationAnimated = false;
    bool rotationAnimated = false;
    bool scalingAnimated = false;
    // Raw M2Track<C3Vector>/M2Track<C4Quaternion>/M2Track<C3Vector> byte
    // offsets for translation/rotation/scaling -- always set (regardless of
    // *Animated), same "let a caller resolve the real curve itself" purpose
    // Color::colorTrackOffset/alphaTrackOffset already serve.
    uint32_t translationTrackOffset = 0;
    uint32_t rotationTrackOffset = 0;
    uint32_t scalingTrackOffset = 0;
};

// M2TrackBase's first two fields (wowdev.wiki "Standard animation block"),
// constant for the whole track regardless of which sequence's sub-array is
// being read -- interpolation_type governs how a *consumer* should
// interpolate between keyframes (0: step/none, 1: linear, 2/3: cubic
// bezier/hermite spline -- "only valid for M2SplineKey tracks", per the
// wiki), and global_sequence, when not the "none" sentinel, means this
// track ignores M2Sequence entirely and instead loops continuously against
// a duration in the model's own global_sequences table (wowdev.wiki
// "Global Sequences": "completely unrelated to animations... always
// loops", `Header::globalLoops`/parseGlobalLoops below). See
// resolveVec3TrackSequence/resolveQuatTrackSequence below for how a
// global-sequence track is refused *by sequence index* without silently
// mis-attributing the data to whichever M2Sequence happens to occupy index
// 0 of the track's outer array (a real bug this type existed to fix, not a
// hypothetical one), and resolveVec3GlobalSequenceTrack/
// resolveQuatGlobalSequenceTrack below for how it's actually resolved into
// a real, independently-looping glTF clip instead -- for bone tracks; the
// same track shape on a material's M2Color/M2TextureWeight has no glTF
// property to animate at all, see Color::colorAnimated/alphaAnimated's doc
// comment.
// TODO: Remove: WIKI_FINDINGS.md/TODO_correctness.md, FAILURES2.md #7.
struct TrackMeta {
    uint16_t interpolationType = 1;
    // 0xFFFF ("none"), the same -1-as-unsigned sentinel convention this
    // format uses elsewhere (Bone::parentBone, Attachment::bone, ...), not
    // an explicit wowdev.wiki quote for this specific field -- inferred by
    // convention, flagged here so a future correction is easy to find.
    static constexpr uint16_t kNoGlobalSequence = 0xFFFF;
    uint16_t globalSequence = kNoGlobalSequence;
};

// Reads interpolation_type/global_sequence directly, without touching the
// timestamps/values arrays that follow -- the two fields resolveVec3/
// QuatTrackSequence below check before deciding whether/how to resolve a
// given sequence index. Exposed separately so a caller building one glTF
// animation sampler per property (see cmd_export.cpp's buildAnimations)
// can pick STEP vs. LINEAR once per track, not per keyframe.
TrackMeta readTrackMeta(const std::vector<uint8_t>& blob, uint32_t trackOffset);

// Reads `array.count` M2Color records out of `blob` starting at
// `array.offset`, resolving each one's color/alpha M2Track to a static
// value (see Color's doc comment -- not real keyframe playback). Throws
// ParseError if the fixed-size record range, or either track's own
// (foreign-data) array descriptors, run past the end of the blob. An empty
// array (count 0) returns an empty vector without touching `array.offset`.
std::vector<Color> parseColors(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` M2TextureWeight records out of `blob` starting at
// `array.offset`, same static-value approximation as parseColors. Throws
// ParseError under the same conditions. An empty array (count 0) returns
// an empty vector without touching `array.offset`.
std::vector<TextureWeight> parseTextureWeights(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` M2TextureTransform records out of `blob` starting
// at `array.offset`, resolving each of the 3 tracks (translation/
// rotation/scaling) to a static value only when unambiguously constant
// (see TextureTransform's doc comment). Throws ParseError if the
// fixed-size record range, or any track's own array descriptors, run
// past the end of the blob. An empty array (count 0) returns an empty
// vector without touching `array.offset`.
std::vector<TextureTransform> parseTextureTransforms(const std::vector<uint8_t>& blob,
                                                       const Array& array);

// Reads `array.count` M2Sequence records out of `blob` starting at
// `array.offset`. Throws ParseError if that range runs past the end of the
// blob. An empty array (count 0) returns an empty vector without touching
// `array.offset` at all.
std::vector<Sequence> parseSequences(const std::vector<uint8_t>& blob, const Array& array);

// Resolves one M2Track<C3Vector>'s keyframe data for exactly one M2Sequence
// index (an index into the `sequences` array `parseSequences` returned,
// *not* an M2Sequence::id) -- see wowdev.wiki M2#Interpolation's "outer
// array indexed by animation" model, and Bone::translationTrackOffset's doc
// comment (m2_skeleton.hpp) for what `trackOffset` (and which `blob`)
// needs to be. Returns (timestamp in milliseconds, value) pairs in file
// order, or an empty vector -- not an error -- when `sequenceIndex` is out
// of range for this particular track's own outer array, that entry has
// zero keyframes, *or* the track's global_sequence isn't the "none"
// sentinel (see TrackMeta) -- husk doesn't resolve global-sequence tracks
// into a real looping clip yet, and the old behavior of indexing straight
// into the outer array by `sequenceIndex` anyway silently attributed a
// global-sequence track's data to whichever M2Sequence happened to occupy
// outer-array position `sequenceIndex` (usually 0), a real correctness
// bug, not just a missing feature -- returning empty here is strictly more
// correct than that, even though it means such a track currently produces
// no animation at all. Throws ParseError if interpolation_type is 2 or 3:
// per wowdev.wiki, cubic bezier/hermite interpolation is "only valid for
// M2SplineKey tracks", and every M2Track this function is used for (bone
// translation/scale, M2Color, M2TextureWeight) is declared as a plain
// M2Track<T>, not M2Track<M2SplineKey<T>> -- a real file reporting 2/3
// here would mean either this parser's assumption is wrong for some
// version, or the file is corrupted, and guessing at M2SplineKey's 3x-
// stride layout for a track that per spec shouldn't have it risks exactly
// the kind of silent misread this project's tests exist to catch.
// TODO: Remove: see the M2Sequence-stride investigation in WIKI_FINDINGS.md
// for the same shape of mistake.
//
// `externalDataBlob`, when non-null, is where the *actual keyframe bytes*
// are read from instead of `blob` -- `blob` still supplies every array
// descriptor (the outer M2Array<M2Array<T>> and the per-sequence inner
// M2Array<T> it points at), only the final timestamp/value reads move to
// the other blob. This is for a sequence whose real data lives in an
// external .anim file (Sequence::flags without 0x20): per wowdev.wiki,
// "these files are just a blob of data which may as well be in the main
// model file, that is pointed to by the first array_ref layer" -- the
// M2's own per-sequence inner M2Array descriptor is real (not zeroed),
// its `offset` field is just relative to the .anim file's blob instead of
// the M2's own (see extractAnimBlob). Throws ParseError if the track's own
// array descriptors (always read from `blob`), or the claimed keyframe
// data itself (read from whichever blob is actually in play), run past
// the end of that blob.
std::vector<std::pair<uint32_t, Vec3>> resolveVec3TrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    const std::vector<uint8_t>* externalDataBlob = nullptr);

// Same as resolveVec3TrackSequence, but for an M2Track<M2CompQuat> (i.e.
// Bone::rotationTrackOffset) -- each raw wire value is decompressed to a
// Quat (see Quat's doc comment) before being returned.
std::vector<std::pair<uint32_t, Quat>> resolveQuatTrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    const std::vector<uint8_t>* externalDataBlob = nullptr);

// Resolves one M2Track<C3Vector>'s keyframe data when it's global-sequence-
// driven (TrackMeta::globalSequence != TrackMeta::kNoGlobalSequence) --
// per wowdev.wiki M2#Interpolation, "blocks that use global sequences also
// only have one track": such a track's outer M2Array<M2Array<T>> holds
// exactly one sub-array (index 0), not one per M2Sequence, since the
// keyframes loop continuously against the model's own global_sequences
// duration table (see parseGlobalLoops) instead of being tied to any
// specific M2Sequence's timeline. This is the real-data counterpart to
// resolveVec3TrackSequence's own global-sequence check, which correctly
// refuses to resolve such a track *by sequence index* (see TrackMeta's doc
// comment) but as a result never resolved it any other way either.
// TODO: Remove: FAILURES2.md #7 was that gap.
// Returns the same (timestamp, value) pairs
// resolveVec3TrackSequence does, or empty (not an error) if this track is
// *not* global-sequence-driven, or if its single outer sub-array is itself
// empty. Throws ParseError under the same conditions as
// resolveVec3TrackSequence (interpolation_type 2/3, or a claimed range
// running past the end of `blob`/`externalDataBlob`).
std::vector<std::pair<uint32_t, Vec3>> resolveVec3GlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset,
    const std::vector<uint8_t>* externalDataBlob = nullptr);

// Same as resolveVec3GlobalSequenceTrack, but for an M2Track<M2CompQuat>
// (i.e. Bone::rotationTrackOffset) -- each raw wire value is decompressed
// to a Quat (see Quat's doc comment) before being returned.
std::vector<std::pair<uint32_t, Quat>> resolveQuatGlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset,
    const std::vector<uint8_t>* externalDataBlob = nullptr);

// Same as resolveQuatTrackSequence, but for an M2Track<C4Quaternion> (i.e.
// TextureTransform::rotationTrackOffset) -- 4 *raw* floats per keyframe, no
// M2CompQuat decompression (see TextureTransform::rotation's own doc
// comment for why this is a genuinely different wire format from a bone's
// rotation track, not just a naming difference).
std::vector<std::pair<uint32_t, Quat>> resolveRawQuatTrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    const std::vector<uint8_t>* externalDataBlob = nullptr);

// Global-sequence counterpart to resolveRawQuatTrackSequence, same
// relationship resolveQuatGlobalSequenceTrack has to resolveQuatTrackSequence.
std::vector<std::pair<uint32_t, Quat>> resolveRawQuatGlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset,
    const std::vector<uint8_t>* externalDataBlob = nullptr);

// Same shape as resolveVec3TrackSequence, but for an M2Track<float> --
// M2Particle's ~10 simulation-parameter tracks (emissionSpeed, gravity,
// lifespan, ...) and M2Ribbon's heightAboveTrack/heightBelowTrack are all
// this type; unlike Vec3/Quat (2 real call sites total), this one has over
// a dozen real occurrences once particles are parsed, past this codebase's
// own "third occurrence earns an abstraction" bar (see CLAUDE.md), so it's
// a real named function rather than another hand-duplicated copy.
std::vector<std::pair<uint32_t, float>> resolveFloatTrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    const std::vector<uint8_t>* externalDataBlob = nullptr);

// Global-sequence counterpart to resolveFloatTrackSequence, same relationship
// resolveVec3GlobalSequenceTrack has to resolveVec3TrackSequence.
std::vector<std::pair<uint32_t, float>> resolveFloatGlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset,
    const std::vector<uint8_t>* externalDataBlob = nullptr);

// Same shape as resolveVec3TrackSequence, but for an M2Track<T> whose
// keyframe values are a small raw integer (uint8_t/uint16_t/int16_t) rather
// than a float/Vec3/Quat -- M2Ribbon's texSlotTrack (uint16_t)/
// visibilityTrack (uint8_t) and M2Particle's enabledIn (uint8_t) all need
// this, but each individually has too few real occurrences (1-2) to justify
// its own named function the way resolveFloatTrackSequence's dozen+ uses
// do -- `elementSize` (1 or 2 bytes) plays the same runtime-parameter role
// here that checkInnerArrayFits already uses instead of a template, matching
// this codebase's existing style. Each keyframe's raw little-endian bytes
// are returned zero-extended into a uint32_t; scaling (e.g. alphaTrack's
// fixed16 0..0x7FFF -> 0.0..1.0) is the caller's job, same split
// readFixed16TrackValue already draws from its own raw bit read. Throws
// ParseError if `elementSize` isn't 1 or 2, or under the same conditions as
// resolveVec3TrackSequence otherwise.
std::vector<std::pair<uint32_t, uint32_t>> resolveRawIntTrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    size_t elementSize, const std::vector<uint8_t>* externalDataBlob = nullptr);

// Global-sequence counterpart to resolveRawIntTrackSequence.
std::vector<std::pair<uint32_t, uint32_t>> resolveRawIntGlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t elementSize,
    const std::vector<uint8_t>* externalDataBlob = nullptr);

// M2Particle's Wrath+ colorTrack/alphaTrack/scaleTrack/headUVAnim/
// tailUVAnim use a different, simpler shape than M2Track -- wowdev.wiki's
// "Fake-AnimationBlock": a flat `{nTimestamps, ofsTimestamps, nKeys,
// ofsKeys}` (16 bytes) pointing directly at real keyframe data, no
// per-M2Sequence outer/inner indirection at all ("they're unable to change
// between different animations, so they directly point to the data").
// The timestamp values themselves are `uint16_t`, *not* the `uint32_t`
// milliseconds a real M2Track uses -- confirmed both by the wiki text ("the
// timestamps are shorts") and by real files, where they run 0..0x7FFF
// monotonically per key set. That range strongly suggests a normalized
// lifetime fraction (0 = particle spawn, 0x7FFF = particle death) rather
// than an absolute time, consistent with these curves having no
// M2Sequence/global-sequence to be absolute against -- exposed as raw
// `uint16_t` here (not rescaled to 0.0..1.0), since husk hasn't found an
// authoritative source confirming that interpretation, only strong
// circumstantial real-data evidence.
// TODO: Remove: WIKI_FINDINGS.md citation backing the real-data evidence above.
struct FBlockMeta {
    Array timestamps;
    Array keys;
};
FBlockMeta readFBlockMeta(const std::vector<uint8_t>& blob, uint32_t blockOffset);

// Resolves one FBlock's keyframes as raw Vec3 (colorTrack: C3Vector RGB
// multiplier, wire values observed 0..255-ish, not normalized 0..1 -- see
// FBlockMeta's doc comment, exposed raw rather than guessing a scale).
// Throws ParseError if the claimed timestamp/key ranges run past the end of
// the blob. Empty (zero keys) returns an empty vector, not an error.
std::vector<std::pair<uint16_t, Vec3>> resolveFBlockVec3(const std::vector<uint8_t>& blob,
                                                           uint32_t blockOffset);

// Same as resolveFBlockVec3, but for a C2Vector (scaleTrack).
std::vector<std::pair<uint16_t, Vec2>> resolveFBlockVec2(const std::vector<uint8_t>& blob,
                                                           uint32_t blockOffset);

// Same as resolveFBlockVec3, but for a fixed16 scalar (alphaTrack) --
// decoded to a 0.0..1.0 float the same way readFixed16TrackValue does.
std::vector<std::pair<uint16_t, float>> resolveFBlockFixed16(const std::vector<uint8_t>& blob,
                                                               uint32_t blockOffset);

// Same as resolveFBlockVec3, but for a raw uint16_t (headUVAnim/tailUVAnim
// flipbook-cell indices).
std::vector<std::pair<uint16_t, uint16_t>> resolveFBlockUint16(const std::vector<uint8_t>& blob,
                                                                 uint32_t blockOffset);

// Resolves an M2PartTrack<fixed16> (wowdev.wiki M2#EXP2's `alphaCutoff`) --
// a flat {M2Array<fixed16> times; M2Array<fixed16> values;} pair (16
// bytes), structurally identical to FBlockMeta's own two-Array layout
// (readFBlockMeta is reused directly for the header read) but with
// fixed16-decoded *timestamps* too, unlike FBlock's raw-uint16_t ones --
// M2PartTrack has no interpolation_type/global_sequence header at all,
// simpler even than an FBlock. Both `times` and `values` decode as
// 0.0..1.0 fixed16 fractions (the same 0x0000..0x7FFF -> 0.0..1.0 scaling
// readFixed16TrackValue/resolveFBlockFixed16 already use elsewhere) -- per
// the wiki, `times` indexes the particle's own normalized lifetime (0 =
// spawn, 1 = death) and `values` is the alpha-test cutoff at that point in
// its life. Throws ParseError if the claimed times/values ranges run past
// the end of the blob. Empty (zero values) returns an empty vector, not an
// error.
std::vector<std::pair<float, float>> resolveFixed16PartTrack(const std::vector<uint8_t>& blob,
                                                                uint32_t blockOffset);

// Reads `array.count` M2Loop records (wowdev.wiki M2#Global_sequences: a
// bare `uint32_t timestamp` each, the total duration in milliseconds a
// global sequence loops over) out of `blob` at `array.offset` -- the
// model's (or `.skel`'s own) `Header::globalLoops` array, indexed by a
// track's TrackMeta::globalSequence value. Throws ParseError if that range
// runs past the end of the blob. An empty array (count 0) returns an empty
// vector without touching `array.offset` at all.
std::vector<uint32_t> parseGlobalLoops(const std::vector<uint8_t>& blob, const Array& array);

// Resolves a .anim file's own possibly-chunked shape (wowdev.wiki
// M2#.anim_files' "Legion 24500" section) into the raw blob
// resolveVec3TrackSequence/resolveQuatTrackSequence's `externalDataBlob`
// expects. Unlike the M2/.skel container formats, a .anim file carries no
// magic value of its own to sniff -- whether a *specific* model's .anim
// files are chunked (single AFM2 chunk wrapping the same content a flat
// file would hold) or flat (the raw content directly) is determined by
// that model's own M2 header: `chunked` should be
// `(header.globalFlags & 0x200000) != 0` (wowdev.wiki's
// `flag_unk_0x200000`, "apparently: use 24500 upgraded model format:
// chunked .anim files"). Throws ParseError if `chunked` is true but no
// AFM2 chunk is found (a flat file passed in with the wrong flag would
// otherwise be silently misread as chunk headers). NOTE: this is
// implemented directly from the wiki's description, not yet cross-checked
// against a real chunked .anim file -- see README.md's Design notes.
std::vector<uint8_t> extractAnimBlob(const std::vector<uint8_t>& animFileBytes, bool chunked);

}  // namespace husk::m2
