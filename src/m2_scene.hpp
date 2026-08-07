#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "m2_primitives.hpp"

// M2 world-placement/gameplay records -- attachment points, animation
// events, lights, ribbon trails, and particle emitters -- split out of the
// former monolithic m2.hpp/m2.cpp (see FILE_SPLIT_TODO.md Item 2). See
// m2_primitives.hpp for the module overview (on-disk shapes, blob-
// resolution entry points).
namespace husk::m2 {

// M2Attachment, per wowdev.wiki M2#Attachments -- 40 (0x28) bytes on disk.
// Only the static fields are surfaced; `animate_attached` (an
// M2Track<uchar>, whether the attached model animates with this one) is
// skipped, same "M2Track fields are stage-6-or-later" policy as
// Bone/Color/TextureWeight elsewhere -- it's a bool-ish on/off switch, not
// something husk's glTF export has a slot for yet.
struct Attachment {
    uint32_t id = 0;      // meaning depends on model type; see wowdev.wiki's attachment-point table
    int16_t bone = -1;    // attachment base
    Vec3 position;         // relative to `bone`
};

// M2Event, per wowdev.wiki M2#Events -- 36 (0x24) bytes on disk.
// `identifier` is 4 raw bytes read as ASCII (typically a '$'-prefixed
// 3-character code, e.g. "$DTH" for death) -- read in file order, same as
// every other 4-byte field in this codebase (M2 doesn't reverse these,
// unlike WMO/ADT chunk tags, see chunk.hpp). `enabled` (an
// M2TrackBase-only "timestamp-only animation block", every timestamp an
// implicit "fire now") is skipped -- resolving *when* an event fires
// during playback is a real-animation-clip concern, out of scope for a
// static record dump.
struct Event {
    std::string identifier;
    uint32_t data = 0;
    uint32_t bone = 0;
    Vec3 position;
};

// M2Light, per wowdev.wiki M2#Lights -- 156 (0x9C) bytes on disk. Only the
// 3 static fields (of 8 total) are surfaced; the other 5 -- ambient/
// diffuse color and intensity, attenuation start/end, visibility -- are
// all M2Track-animated, same skip-for-now policy as Attachment/Event above.
struct Light {
    uint16_t type = 0;    // 0 = directional, 1 = point (wowdev.wiki M2#Lights)
    int16_t bone = -1;    // -1 if not attached to a bone
    Vec3 position;         // relative to `bone`, if given
};

// M2Ribbon, per wowdev.wiki M2#Ribbon_emitters -- 176 (0xB0) bytes for the
// >= Wrath shape (matching the same minimum-targeted version as Bone/
// Sequence elsewhere; the wiki itself marks the trailing
// priorityPlane/RibbonColorIndex/textureTransformLookupIndex fields "TODO:
// verify version", but they're present in every version this parser
// targets either way). `textureIndices`/`materialIndices` (M2Array<uint16_t>
// lookup tables) are surfaced directly; the six M2Track<T> fields
// (colorTrack/alphaTrack/heightAboveTrack/heightBelowTrack/texSlotTrack/
// visibilityTrack) are stored as raw track offsets only -- same
// "descriptor now, real keyframe resolution happens downstream" policy as
// Bone's own three tracks (see Bone::translationTrackOffset's doc comment,
// m2_skeleton.hpp) -- full curve resolution for these lives in
// `husk dump-chunks` (src/cmd_dump.cpp), not here.
struct Ribbon {
    uint32_t ribbonId = 0;  // "Always (as I have seen): -1" per the wiki
    uint32_t boneIndex = 0;  // bone to attach to
    Vec3 position;            // relative to `boneIndex`
    std::vector<uint16_t> textureIndices;   // into the model's own `textures` array
    std::vector<uint16_t> materialIndices;  // into the model's own `materials` array
    uint32_t colorTrackOffset = 0;        // M2Track<C3Vector>
    uint32_t alphaTrackOffset = 0;        // M2Track<fixed16>
    uint32_t heightAboveTrackOffset = 0;  // M2Track<float>
    uint32_t heightBelowTrackOffset = 0;  // M2Track<float>
    float edgesPerSecond = 0;  // ribbon smoothness -- quads generated per second
    float edgeLifetime = 0;    // seconds a generated quad stays around
    float gravity = 0;         // use arcsin(val) for the emission angle, per the wiki
    uint16_t textureRows = 0;  // tiles in the ribbon's texture
    uint16_t textureCols = 0;
    uint32_t texSlotTrackOffset = 0;    // M2Track<uint16_t>
    uint32_t visibilityTrackOffset = 0;  // M2Track<uint8_t>
    int16_t priorityPlane = 0;
    int8_t ribbonColorIndex = 0;
    int8_t textureTransformLookupIndex = 0;
};

// M2Particle, per wowdev.wiki M2#Particle_emitters -- the Cata+ shape only
// (M2ParticleOld's late-BC blendingType/emitterType width + Cata's
// multiTexScale + Wrath's FBlock-based color/alpha/scale/UV curves +
// Cata's multiTexScrollMid/Range wrapper), 492 (0x1EC) bytes on disk. Per
// the wiki's own note ("if 0x200 is set or if version is bigger than 271,
// length of M2ParticleOld is 492"), every real file this parser targets
// (version >= kMinVerifiedParticleVersion) always uses the 492-byte shape
// unconditionally, regardless of the per-particle flag -- so there's no
// version OR per-record branching inside parseParticles, only the
// file-level kMinVerifiedParticleVersion gate. Older (pre-BC/pre-Wrath/
// pre-Cata) shapes are real but unverified against any file this project
// has access to -- not implemented, same "kMinVerified*"-gated policy as
// Bone/Sequence/Ribbon (see kMinVerifiedRecordStrideVersion,
// m2_primitives.hpp). M2Track/FBlock fields are stored as raw offsets;
// full curve resolution lives downstream in `husk dump-chunks`
// (src/cmd_dump.cpp), same split as Ribbon's tracks.
// TODO: Remove: verified against mace_2h_bolvar_d_01.m2 (WIKI_FINDINGS.md)
// -- real fire/ember color gradient, clean fade/grow curves, MultiTexture
// flag correlates with non-zero multiTexScale.
struct ParticleEmitter {
    uint32_t particleId = 0;  // "Always (as I have seen): -1" per the wiki
    uint32_t flags = 0;       // see wowdev.wiki M2#Particle_Flags
    Vec3 position;             // relative to `boneId`
    uint16_t boneId = 0;
    // Cata+: 3x 5-bit sub-fields (textureId1/textureId2/textureId3) + 1 pad
    // bit, for multi-textured particles -- stored raw, un-decoded, same
    // "give the raw bits, let a consumer interpret" policy as elsewhere.
    uint16_t textureId = 0;
    std::string particleModelFilename;       // non-empty: this emitter spawns model particles
    std::string childEmittersModelFilename;  // non-empty: child emitters come from this model
    uint8_t blendingType = 0;  // see wowdev.wiki M2#Particle_Blendings
    uint8_t emitterType = 0;   // 1=Plane, 2=Sphere, 3=Spline, 4=Bone
    uint16_t particleColorIndex = 0;  // ParticleColor.dbc row selector, 0 = unmodified
    // fixed_point<int8_t,2,5>[2] decoded to float (raw / 32.0f) -- per-
    // texture-layer scale for multi-textured particles.
    float multiTexScale[2] = {0, 0};
    int16_t priorityPlane = 0;
    uint16_t rows = 0;  // tiles in texture
    uint16_t columns = 0;
    uint32_t emissionSpeedTrackOffset = 0;      // M2Track<float>
    uint32_t speedVariationTrackOffset = 0;     // M2Track<float>
    uint32_t verticalRangeTrackOffset = 0;      // M2Track<float>
    uint32_t horizontalRangeTrackOffset = 0;    // M2Track<float>
    uint32_t gravityTrackOffset = 0;            // M2Track<float>
    uint32_t lifespanTrackOffset = 0;           // M2Track<float>
    float lifespanVariation = 0;
    uint32_t emissionRateTrackOffset = 0;       // M2Track<float>
    float emissionRateVariation = 0;
    uint32_t emissionAreaWidthTrackOffset = 0;  // M2Track<float>
    uint32_t emissionAreaLengthTrackOffset = 0; // M2Track<float>
    uint32_t zSourceTrackOffset = 0;            // M2Track<float>
    uint32_t colorTrackBlockOffset = 0;  // FBlock<C3Vector>
    uint32_t alphaTrackBlockOffset = 0;  // FBlock<fixed16>
    uint32_t scaleTrackBlockOffset = 0;  // FBlock<C2Vector>
    Vec2 scaleVary;  // percentage amount to randomly vary each particle's scale
    uint32_t headUVAnimBlockOffset = 0;  // FBlock<uint16_t>, flipbook cell indices
    uint32_t tailUVAnimBlockOffset = 0;  // FBlock<uint16_t>
    float tailLength = 0;
    float twinkleSpeed = 0;
    float twinklePercent = 0;
    float twinkleScaleMin = 0;  // CRange
    float twinkleScaleMax = 0;
    float inheritVelocityScale = 0;
    float drag = 0;
    float baseSpin = 0;
    float baseSpinVariation = 0;
    float spinSpeed = 0;
    float spinSpeedVariation = 0;
    Vec3 tumbleMin;  // M2Box: angular velocity min/max, ModelParticles only
    Vec3 tumbleMax;
    Vec3 windVector;  // static wind, ignored if the DynamicWind flag is set
    float windTime = 0;
    float followSpeed1 = 0;
    float followScale1 = 0;
    float followSpeed2 = 0;
    float followScale2 = 0;
    std::vector<Vec3> splinePoints;  // spline emitter control points (emitterType == 3)
    uint32_t enabledInTrackOffset = 0;  // M2Track<uint8_t>
    // 2x vector_2fp_6_9 (fixed_point<uint16_t,6,9>), decoded to float
    // (raw / 512.0f), as {x0, y0, x1, y1}.
    float multiTexScrollMid[4] = {0, 0, 0, 0};
    float multiTexScrollRange[4] = {0, 0, 0, 0};
};

// M2ExtendedParticle (wowdev.wiki M2#EXP2, >= 7.3.0), 28 (0x1C) bytes on
// disk -- effectively supersedes M2Particle's own zSource/colorMult/
// alphaMult (see EXPT, dumpExpt in cmd_dump.cpp): the wiki's own text says
// "if EXP2 doesn't exist, the client tries to reconstruct it with data from
// the EXPT chunk," i.e. EXP2 is EXPT's superset, not an unrelated record --
// the same three floats are duplicated here rather than only living in
// EXPT, plus one field EXPT has no room for at all: `alphaCutoff`, an
// M2PartTrack<fixed16> (a flat {times, values} pair, no per-sequence/
// global-sequence indirection at all -- simpler than a real M2Track, see
// resolveFixed16PartTrack's doc comment, m2_animation.hpp) mapping the
// particle's own 0.0..1.0 normalized lifetime to an alpha-test cutoff
// threshold. One entry expected per `particle_emitters` array entry (same
// indexing convention TXAC/EXPT/RPID/GPID/PGD1 already use) -- not
// cross-checked here, same "trust this chunk's own byte length" policy
// dumpTxac already uses.
// TODO: Remove: verification history -- a live-CASC corpus scan found
// 17,065 real EXP2-bearing files (local extraction had none); 2 pulled
// files decoded cleanly with no parser changes needed. See
// WIKI_FINDINGS.md/M2_COMPLETENESS.md.
struct ExtendedParticle {
    float zSource = 0;
    float colorMult = 0;
    float alphaMult = 0;
    uint32_t alphaCutoffOffset = 0;  // M2PartTrack<fixed16>, see resolveFixed16PartTrack
};

// Reads `array.count` M2Attachment records out of `blob` starting at
// `array.offset`, surfacing only `id`/`bone`/`position` (see Attachment's
// doc comment). Throws ParseError if that range runs past the end of the
// blob. An empty array (count 0) returns an empty vector without touching
// `array.offset` at all.
std::vector<Attachment> parseAttachments(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` M2Event records out of `blob` starting at
// `array.offset`, surfacing only `identifier`/`data`/`bone`/`position`.
// Throws ParseError under the same conditions as parseAttachments. An
// empty array (count 0) returns an empty vector without touching
// `array.offset`.
std::vector<Event> parseEvents(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` M2Light records out of `blob` starting at
// `array.offset`, surfacing only `type`/`bone`/`position`. Throws
// ParseError under the same conditions as parseAttachments. An empty
// array (count 0) returns an empty vector without touching `array.offset`.
std::vector<Light> parseLights(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` M2Ribbon records out of `blob` starting at
// `array.offset`, surfacing only Ribbon's static fields (see its doc
// comment). Throws ParseError under the same conditions as
// parseAttachments. An empty array (count 0) returns an empty vector
// without touching `array.offset`.
std::vector<Ribbon> parseRibbons(const std::vector<uint8_t>& blob, const Array& array);

// The lowest header version parseParticles's 0x1EC-byte record shape is
// documented and real-data-verified for: Cataclysm, per expansionForVersion's
// own table (see ParticleEmitter's doc comment for the real-file cross-check
// this was confirmed against). Callers check `header.version <
// kMinVerifiedParticleVersion` and warn/report count-only rather than
// silently trusting output this parser was never confirmed to read
// correctly for an older shape -- same policy kMinVerifiedRecordStrideVersion
// already uses for Bone/Sequence/Ribbon, just a newer floor since
// M2Particle's own byte layout genuinely changed at Cataclysm (unlike those
// three, which are stride-stable back to Wrath).
constexpr uint32_t kMinVerifiedParticleVersion = 272;

// Reads `array.count` M2Particle records out of `blob` starting at
// `array.offset`, at the fixed 0x1EC-byte Cata+ stride (see
// ParticleEmitter's doc comment) -- callers must check
// `version >= kMinVerifiedParticleVersion` themselves before calling this;
// it does not check `version` on its own (mirroring parseRibbons/parseBones,
// which likewise never branch on version internally). Throws ParseError if
// the fixed-size record range, or any nested M2Array/M2Track/FBlock
// descriptor read while resolving `particleModelFilename`/
// `childEmittersModelFilename`/`splinePoints`/`textureIndices`-shaped
// fields, runs past the end of the blob. An empty array (count 0) returns
// an empty vector without touching `array.offset`.
std::vector<ParticleEmitter> parseParticles(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` M2ExtendedParticle records out of `blob` starting at
// `array.offset`, at a fixed 28 (0x1C)-byte stride (see ExtendedParticle's
// doc comment) -- `alphaCutoffOffset` is stored as a raw blob offset only
// (resolve via resolveFixed16PartTrack), same "descriptor now, real
// resolution happens downstream in husk dump-chunks" split every other
// M2Track/FBlock-bearing field in this file already uses. Throws
// ParseError if that range runs past the end of the blob. An empty array
// (count 0) returns an empty vector without touching `array.offset` at
// all. Note: unlike parseParticles, EXP2's own containing chunk carries
// its own local M2Array<M2ExtendedParticle> header (see cmd_dump.cpp's
// dumpExp2) -- `blob`/`array` here are typically that chunk's own payload
// bytes and its own local array descriptor, not necessarily the model's
// full MD20 blob, since this function (like parseTextureWeights/
// parseUint16Array when cmd_dump.cpp reuses them for PADC/PABC) only cares
// about byte offsets relative to whatever buffer it's given.
std::vector<ExtendedParticle> parseExtendedParticles(const std::vector<uint8_t>& blob,
                                                       const Array& array);

}  // namespace husk::m2
