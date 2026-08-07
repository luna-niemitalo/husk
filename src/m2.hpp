#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// M2 header parsing, per https://wowdev.wiki/M2 (fetched 2026-07-24).
//
// Two on-disk shapes:
//  - Pre-Legion: the file *is* the MD20 header + data, starting with magic
//    "MD20" at byte 0.
//  - Legion+ (expansion level >= 7, build >= 7.0.1.20740): the file is
//    husk::readChunks()-style chunks in arbitrary order; the MD21 chunk's
//    payload is byte-for-byte the old MD20 blob, with every offset in it
//    relative to the *chunk's* start, not the file's.
//
// Either way, once you have the MD20 blob, the header layout is identical.
// This module resolves the outer shape first, then parses that one blob.
namespace husk::m2 {

// M2Array<T>: a (count, offset) pair. offset is relative to the start of
// the MD20 blob (see above), and points at `count` contiguous T records --
// this parser only resolves the pair itself, not the pointed-to records,
// except for `name` (a char array), which is small and worth surfacing.
struct Array {
    uint32_t count = 0;
    uint32_t offset = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Vec2 {
    float x = 0, y = 0;
};

// A compressed bone-rotation quaternion, already decompressed to floats --
// see wowdev.wiki "Quaternion values and 2.x" for the packed-int16 wire
// format (src/m2.cpp's readCompQuat does the actual unpacking). Field order
// matches Blizzard's own C4Quaternion: w last, not first (wowdev.wiki
// Common_Types#C4Quaternion's explicit warning about this).
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

// M2Vertex, per wowdev.wiki M2#Vertices -- 48 bytes on disk, field order
// below matches the wire layout exactly (see tests/test_m2.cpp for the byte
// offsets). Read verbatim: no coordinate-system conversion happens here --
// WoW's Z-up-to-glTF's-Y-up flip is a concern for whatever writes glTF, not
// for this reader.
struct Vertex {
    Vec3 pos;
    uint8_t boneWeights[4] = {};
    uint8_t boneIndices[4] = {};
    Vec3 normal;
    Vec2 texCoords[2];
};

struct BoundingBox {
    Vec3 min;
    Vec3 max;
};

// M2Texture, per wowdev.wiki M2#Textures -- 16 bytes on disk. `filename`
// only means something when `type == 0` ("NONE" -- a real, embedded path);
// every other type is resolved at runtime from DBC tables husk doesn't read
// (character customization, item textures, ...), and `filename` is empty
// (or a placeholder zero byte) for those. Modern (Legion+) content usually
// leaves even type-0 filenames empty and points at a FileDataID via the
// TXID chunk instead -- see Header::textureFileDataIds.
struct Texture {
    uint32_t type = 0;
    uint32_t flags = 0;
    std::string filename;
};

// M2Material, per wowdev.wiki M2#Render_flags_and_blending_modes -- 4 bytes
// on disk. `blendMode` is WoW's M2BLEND_* enum (see wowdev.wiki
// M2/Rendering#M2BLEND), not a glTF alphaMode directly -- translating that
// is roadmap stage 5's job (src/cmd_export.cpp), not this parser's.
struct Material {
    uint16_t flags = 0;
    uint16_t blendMode = 0;
};

// Field offsets and order below are transcribed directly from the wowdev.wiki
// M2 "Header" section (offsets given there for expansion level >= 3, which
// covers every currently-shipping model). Deliberately stops at the last
// field this MVP actually surfaces (particle_emitters) rather than
// transcribing the full struct -- extend as later commands need more of it.
struct Header {
    uint32_t magic = 0;    // "MD20" once resolved to the MD20 blob, either way
    uint32_t version = 0;
    std::string name;      // may be empty in files from 9.2.0.41462+ (see wiki)
    uint32_t globalFlags = 0;

    Array globalLoops;
    Array sequences;
    Array sequenceLookup;
    Array bones;
    Array boneLookup;
    Array vertices;
    uint32_t numSkinProfiles = 0;  // LOD views now live in .skin files, not here
    Array colors;
    Array textures;
    Array textureWeights;
    Array textureTransforms;
    Array textureLookup;
    Array materials;
    Array boneCombos;
    Array textureCombos;
    Array textureCoordCombos;
    Array textureWeightCombos;
    Array textureTransformCombos;

    BoundingBox boundingBox;
    float boundingSphereRadius = 0;
    BoundingBox collisionBox;
    float collisionSphereRadius = 0;

    // Collision mesh (physics/hit-testing, distinct from the render mesh in
    // `vertices`) -- wowdev.wiki M2#Header. Dereferenced by parseCollisionMesh
    // (see CollisionMesh's doc comment). The object-placement/gameplay
    // arrays that follow it are not: surfacing their Array descriptors
    // themselves is what `husk info` needs to report counts, and is the
    // minimum needed to keep this struct's field layout complete and in
    // wire order (see parseVertices/parseBones/parseTextures for the
    // dereferencing pattern, if one of these needs it later).
    Array collisionIndices;   // uint16 triangle indices into collisionPositions
    Array collisionPositions; // C3Vector
    Array collisionFaceNormals; // C3Vector
    Array attachments;        // M2Attachment -- equip/effect points (hands, head, ...)
    Array attachmentLookup;   // uint16, alt. name attachment_lookup_table
    Array events;             // M2Event -- e.g. the "$DTH" death-sound event
    Array lights;              // M2Light
    Array cameras;             // M2Camera
    Array cameraLookup;        // uint16, alt. name camera_lookup_table
    Array ribbonEmitters;      // M2Ribbon
    Array particleEmitters;    // M2Particle

    // M2Array<uint16_t>, wowdev.wiki M2#Header ("Second Texture Material
    // Override Combos") -- only present in the wire header at all when
    // GlobalFlag::kUseTextureCombinerCombos is set in globalFlags (offset
    // 0x130, right after particleEmitters); left as a default-constructed
    // empty Array (count 0) otherwise, indistinguishable from "flag set but
    // genuinely empty" -- callers that care about the distinction should
    // check the flag bit directly rather than infer it from count == 0.
    // Per the wiki: when set, multitexture blending uses *this* table's
    // material index instead of "current index material + 1" for combining
    // with the first texture -- surfaced here (see globalFlagNames, `husk
    // info`) but not wired into cmd_export.cpp's material resolution, since
    // the wiki gives no indexing key at all (indexed by what -- batch
    // order? materialIndex? something else?) and this project's own
    // "verify against real bytes before implementing, don't guess at
    // semantics" discipline means an unverified index scheme doesn't ship.
    // TODO: Remove: a full 130,576-file local-corpus scan found zero real
    // files with this flag set, so the layout itself is unverified against real bytes.
    Array textureCombinerCombos;

    bool chunked = false;  // true if this file was Legion+ MD21-wrapped

    // FileDataID of an external .skel file (wowdev.wiki M2#SKID) that this
    // model's `bones` array actually lives in, when `bones.count == 0`
    // doesn't mean "no skeleton" -- see husk::skel. Only ever set for
    // chunked files that happen to carry an SKID chunk; husk doesn't
    // resolve this ID to a path itself (no CASC/listfile access), so
    // callers that want the actual bones still need a .skel path from
    // elsewhere (see `husk export`'s optional 4th argument).
    std::optional<uint32_t> skeletonFileId;

    // FileDataID of an external .phys file (wowdev.wiki M2#PFID) --
    // physics/collision data (cloth, ragdoll-style secondary motion), a
    // format husk doesn't parse yet. Same non-goal as skeletonFileId: not
    // resolved to a path (no CASC/listfile access), surfaced purely so
    // `husk info` doesn't leave this sidecar invisible the way every other
    // Legion+ sidecar chunk already is (SFID/AFID/BFID/SKID/TXID).
    std::optional<uint32_t> physFileId;

    // FileDataIDs parallel to `textures` (same index, same count), from the
    // Legion+ TXID chunk (wowdev.wiki M2#TXID). Entry i is textures[i]'s
    // FileDataID, or 0 for a texture that isn't file-based at all (type != 0
    // -- see husk::m2::Texture). Only set for chunked files that carry a
    // TXID chunk; husk doesn't resolve these IDs to paths itself (no
    // CASC/listfile access -- same non-goal as skeletonFileId above).
    std::optional<std::vector<uint32_t>> textureFileDataIds;

    // Set only for chunked files that carry an SFID chunk (wowdev.wiki
    // M2#SFID) -- the .skin files this model actually has, as FileDataIDs,
    // replacing what used to be filename templating
    // (`${basename}${view}.skin`). Deliberately undifferentiated: the
    // wiki's own `skinFileDataIDs[nViews]` + `lod_skinFileDataIDs[lodBands]`
    // split isn't reconstructed here (nViews == Header::numSkinProfiles,
    // known by the time this is populated, but the split isn't needed for
    // what husk does with it) -- entry 0 is always "the main skin aka
    // lod0" per the wiki, which is all `husk export --skin-dir`'s LOD
    // auto-selection actually uses. husk doesn't resolve these IDs to
    // paths itself (no CASC/listfile access, same non-goal as
    // skeletonFileId/textureFileDataIds above) -- `--skin-dir` only ever
    // checks a local, user-populated directory for `<FileDataID>.skin`.
    std::optional<std::vector<uint32_t>> skinFileDataIds;

    // Set only for chunked files that carry an LDV1 chunk (wowdev.wiki
    // M2#LDV1) -- the number of `_lod%0d.skin` files this model has beyond
    // its ordinary per-view skins (see skinFileDataIds). Not consumed by
    // `--skin-dir`'s "always pick lod0" policy (roadmap stage 7 already
    // settled on that); surfaced for `husk info` since it's otherwise
    // invisible metadata about how many LOD tiers a model actually has.
    std::optional<uint16_t> lodCount;

    // Set only for chunked files that carry a BFID chunk (wowdev.wiki
    // M2#BFID) -- `.bone` files, replacing what used to be filename
    // templating (`${basename}_${i}.bone`). Per the wiki these hold
    // per-bone animation track data (the same category as `.anim`, roadmap
    // stage 6), so -- like skeletonFileId before .skel support existed --
    // this is surfaced now but not yet resolved to actual bone-track
    // content; husk doesn't resolve these IDs to paths itself (no
    // CASC/listfile access, same non-goal as the other *FileDataIds
    // above).
    std::optional<std::vector<uint32_t>> boneFileDataIds;

    // M2's AFID entry (wowdev.wiki M2#AFID) -- one per `.anim` file this
    // model has, replacing what used to be filename templating
    // (`"%s%04d-%02d.anim"`). `animId`/`subAnimId` identify which
    // M2Sequence this file holds data for; `fileId` is 0 for "none" per
    // the wiki (not sparse, just possibly unset).
    struct AnimFileEntry {
        uint16_t animId = 0;
        uint16_t subAnimId = 0;
        uint32_t fileId = 0;
    };

    // Set only for chunked files that carry an AFID chunk. Same
    // surfaced-but-not-yet-resolved status as boneFileDataIds -- real
    // `.anim` keyframe parsing is roadmap stage 6.
    std::optional<std::vector<AnimFileEntry>> animFileIds;

    // Every top-level chunk tag found in this file, in file order -- only
    // populated for chunked (Legion+) files; empty for flat MD20. This
    // format has added a new top-level chunk at a steady clip since Legion
    // (wowdev.wiki M2#Chunks currently documents 30 of them, spanning
    // 7.0 through an unreleased 12.0 build at the time this was written --
    // see README.md's Design notes for the recurring shapes they take).
    // husk::readChunks itself is already tag-agnostic (an unrecognized tag
    // is just skipped over, never an error) -- surfacing the raw tag list
    // here is what lets a caller (see `husk info`, cmd_info.cpp) turn
    // "silently fine" into an actual diagnostic when a real file contains
    // a chunk tag nobody's taught husk about yet, which is exactly what a
    // future client update looks like from here.
    std::vector<std::string> chunkTags;
};

// Header::globalFlags named bits (wowdev.wiki M2#Header), transcribed
// directly from the wiki's own bitfield struct -- bit positions derived by
// counting `uint32_t : 1` reserved slots alongside the named ones, not
// guessed from the hex comments alone (cross-checked: every constant below
// matches the wiki's own inline hex annotation exactly, e.g.
// kUseTextureCombinerCombos at bit 3 == 0x8). Three single-bit gaps (bits
// 2, 4, 6) and a large unnamed gap (bits 22-29, between
// kFlagUnk0x200000 and kFlagUnk0x40000000) are real -- the wiki gives no
// name for those bits at all, not an omission here. Version-gating
// (BC+/Mists+/WoD+/Legion+ per the wiki) isn't enforced when decoding: a
// bit being set on an older file than the wiki claims it's valid for would
// be new information worth seeing, not a case to silently mask.
namespace GlobalFlag {
constexpr uint32_t kTiltX = 0x1;
constexpr uint32_t kTiltY = 0x2;
constexpr uint32_t kUseTextureCombinerCombos = 0x8;  // >= BC
constexpr uint32_t kLoadPhysData = 0x20;              // >= Mists
constexpr uint32_t kUnk0x80 = 0x80;  // >= WoD; unset: demon hunter tattoos stop glowing
constexpr uint32_t kCameraRelated = 0x100;
constexpr uint32_t kNewParticleRecord = 0x200;  // >= Legion; Cata+ 492-byte M2Particle shape
constexpr uint32_t kUnk0x400 = 0x400;
constexpr uint32_t kTextureTransformsUseBoneSequences = 0x800;  // >= WoD
constexpr uint32_t kUnk0x1000 = 0x1000;
constexpr uint32_t kChunkedAnimFiles = 0x2000;
constexpr uint32_t kUnk0x4000 = 0x4000;
constexpr uint32_t kUnk0x8000 = 0x8000;
constexpr uint32_t kUnk0x10000 = 0x10000;
constexpr uint32_t kUnk0x20000 = 0x20000;
constexpr uint32_t kUnk0x40000 = 0x40000;
constexpr uint32_t kUnk0x80000 = 0x80000;
constexpr uint32_t kUnk0x100000 = 0x100000;
constexpr uint32_t kUnk0x200000 = 0x200000;  // "use 24500 upgraded model format" per the wiki
constexpr uint32_t kUnk0x40000000 = 0x40000000;  // seen on 11.1.7+ player-housing furniture
}  // namespace GlobalFlag

// Names every set bit in `flags` using GlobalFlag's constants (wiki-given
// names where documented, "unk_0x<hex>" for the reserved-but-unnamed ones),
// in bit order low to high. A bit not covered by any GlobalFlag constant at
// all (the true reserved gaps -- bits 2, 4, 6, 22-29, or anything past bit
// 30) is silently omitted, same as an M2CompBone flag billboardModeName
// doesn't recognize -- this is a diagnostic aid (`husk info`), not a
// bounds-checked parse, so an unrecognized bit isn't an error. Shared
// helper rather than inlined into cmd_info.cpp so a future caller (e.g.
// cmd_export.cpp, if a specific bit ever needs to gate real behavior) has
// one place to check a name against, mirroring billboardModeName's own
// precedent.
std::vector<std::string> globalFlagNames(uint32_t flags);

// M2CompBone::flags bits (wowdev.wiki M2#Bones), the ones relevant to
// rendering rather than animation-blending internals. Billboarding is a
// *renderer-camera-relative* behavior, not baked model data: these bits
// only say which joints need their orientation overridden at render time
// to face whatever camera the (custom) engine actually has -- nothing
// about M2Camera (a completely separate, unrelated concept: WoW's own
// baked cinematic camera paths) is needed to act on them. Spherical and
// cylindrical-lock bits are mutually exclusive per the wiki (only one
// applies to a given bone); husk doesn't enforce that here, just surfaces
// whichever bits are actually set (see cmd_info.cpp/cmd_export.cpp for
// where these get read).
namespace BoneFlag {
constexpr uint32_t kSphericalBillboard = 0x8;
constexpr uint32_t kCylindricalBillboardLockX = 0x10;
constexpr uint32_t kCylindricalBillboardLockY = 0x20;
constexpr uint32_t kCylindricalBillboardLockZ = 0x40;
constexpr uint32_t kBillboardMask = kSphericalBillboard | kCylindricalBillboardLockX |
                                     kCylindricalBillboardLockY | kCylindricalBillboardLockZ;
}  // namespace BoneFlag

// Names a bone's billboard mode ("spherical"/"cylindrical_lock_x"/"_y"/"_z"),
// or nullptr if none of BoneFlag's bits are set -- the ordinary case for
// most bones. Spherical and cylindrical-lock bits are documented as
// mutually exclusive; if a real file somehow sets more than one, this
// reports whichever is checked first rather than inventing a combined
// meaning nothing defines. Shared by `husk info` (cmd_info.cpp, printed
// per-bone) and `husk export` (cmd_export.cpp, into the exported glTF
// joint's `extras` -- see gltf::Skeleton::Joint::billboardMode) so the two
// don't carry two copies of the same bit-to-name mapping.
const char* billboardModeName(uint32_t flags);

// Names a bone's `keyBoneId` (Bone::keyBoneId, a back-reference into
// wowdev.wiki M2#Key-Bone_Lookup's "Key Bone Names" table -- ArmL, Head,
// FootL, Root, etc.), or nullptr if `keyBoneId` is -1 (no key bone) or a
// value the table doesn't cover (the table itself has real gaps -- e.g.
// IDs 46/47/90-189/191-289/294/295 are undocumented on the wiki, not an
// omission here). Transcribed directly from the wiki's own 193-row table,
// IDs and names both -- not guessed at or reordered. Used for glTF joint
// node names (`gltf::writeGlbMulti`) so a real bone at least sometimes
// gets a real semantic name instead of Blender's own generic "Bone"/"Node"
// fallback.
// TODO: Remove: BLENDER_EXPORT_TODO.md §6.
const char* keyBoneName(int32_t keyBoneId);

// Names a texture's `type` (Texture::type, wowdev.wiki M2#Textures'
// "Texture types" table) -- "skin", "char_hair", "guild_emblem", etc., or
// nullptr for type 0 (a real embedded filename, no name needed here) or a
// value the table doesn't cover (IDs 24-26 are real but the wiki gives no
// name for them, just "seen in DracthyrDragon.m2" -- not an omission
// here). Used to give a hardcoded/runtime-resolved texture slot (one husk
// can't embed a real image for -- no CASC/DB2 access) a real semantic
// material name instead of a bare "_tex<N>" -- see cmd_export.cpp's
// material-naming code.
// TODO: Remove: BLENDER_EXPORT_TODO.md §4.
const char* textureTypeName(uint32_t type);

// M2CompBone, per wowdev.wiki M2#Bones -- 88 bytes on disk (>= Wrath shape,
// which is every version this parser targets). Only the fields stage 2 of
// the roadmap (skeleton + skinning, see README.md) needs for a bind-pose
// joint hierarchy are surfaced; the three embedded M2Track<T> animation
// blocks (translation/rotation/scale, 60 bytes together) are skipped over,
// not parsed -- see tests/test_m2.cpp for why their size is fixed and
// T-independent. Animation playback (roadmap stage 6) will need to revisit
// this and actually resolve them.
struct Bone {
    int32_t keyBoneId = -1;  // back-reference into the key-bone lookup table, -1 if none
    uint32_t flags = 0;
    int16_t parentBone = -1;  // index into this same bones array, -1 if none
    Vec3 pivot;                // bind-pose position, in model space

    // Byte offsets (relative to the *same blob `parseBones` was given* --
    // for inline M2 bones that's the MD20 blob; for a .skel file's SKB1
    // bones, per husk::skel::parseBones, it's that chunk's own payload
    // instead, which matters below) of this bone's three M2Track<T>
    // animation blocks -- translation/scale are M2Track<C3Vector>, rotation
    // is M2Track<Quat> (wire format M2CompQuat, see Quat's doc comment).
    // Not resolved into keyframe data by parseBones itself, same "just the
    // descriptor" policy Header's own M2Array fields follow -- see
    // resolveVec3TrackSequence/resolveQuatTrackSequence below, called with
    // one specific M2Sequence index (Sequence, below) at a time.
    //
    // Roadmap stage 6 (animation, see README.md) only actually wires these
    // up for a model's *inline* bones: per wowdev.wiki's .skel article,
    // once an M2 has moved its bones out to a .skel file, per-bone keyframe
    // data moves out too (into a .anim file's AFSB chunk, a format husk
    // doesn't parse yet) -- these three offsets are still populated for
    // .skel bones by parseBones (it can't tell the difference), but every
    // M2Track they point at is expected to be genuinely empty for that
    // case, not silently wrong.
    uint32_t translationTrackOffset = 0;
    uint32_t rotationTrackOffset = 0;
    uint32_t scaleTrackOffset = 0;
};

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
    // TODO: Remove: WIKI_FINDINGS.md §12 -- confirmed against 157 real
    // alias sequences across 4 real files, 100% valid in-range indices, zero cycles.
    uint16_t aliasNext = 0;
};

// M2Color / M2TextureWeight, per wowdev.wiki M2#Colors_and_transparency --
// referenced from a .skin Batch's colorIndex/textureWeightComboIndex (see
// husk::skin::Batch) to tint/fade a texture-unit's material. Both are
// M2Track<T>-backed (real keyframe animation, roadmap stage 6, not parsed
// here) -- what's surfaced instead is a value *only when the track is
// unambiguously constant* (exactly one animation sub-array with exactly
// one keyframe in it -- see src/m2.cpp's constantTrackValueOffset for why
// anything looser is a real correctness trap, not just an approximation
// worth avoiding). `nullopt` covers both "genuinely no data" and "this
// track is actually animated, which is out of scope here" -- callers fall
// back to that field's natural default (opaque white / full weight)
// either way, since a wrong guess (e.g. reading an animated alpha track's
// unrelated first keyframe) can be worse than no value at all.
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
// Unlike Color/TextureWeight, husk doesn't even attempt to apply this to
// the rendered material (see cmd_export.cpp) -- core glTF's
// KHR_texture_transform extension is a *static* offset/rotation/scale
// with no animation-channel target of its own, so the animated case
// (almost certainly the common one for a real scrolling-UV model)
// couldn't be represented as real playback either way, and per the wiki,
// rotation here is around the texture's own center (0.5, 0.5) -- a
// different pivot than KHR_texture_transform's own (0,0), and correctly
// folding that pivot difference into the extension's offset field isn't
// attempted without a real animated test file to check it against, which
// isn't available yet. Surfaced as raw resolved values instead
// (gltf::Material::textureTransform, inert `extras`), for a downstream
// renderer or Blender script to apply correctly itself.
// TODO: Remove: process-methodology aside (WIKI_FINDINGS.md: "decode real
// records, don't guess from text alone") -- belongs in DESIGN.md#Coding-Policy, not here.
struct TextureTransform {
    std::optional<Vec3> translation;
    std::optional<Quat> rotation;  // C4Quaternion -- 4 raw floats (x,y,z,w), NOT the compressed M2CompQuat bones use
    std::optional<Vec3> scaling;
    bool translationAnimated = false;
    bool rotationAnimated = false;
    bool scalingAnimated = false;
};

// M2Attachment, per wowdev.wiki M2#Attachments -- 40 (0x28) bytes on disk.
// Only the static fields are surfaced; `animate_attached` (an
// M2Track<uchar>, whether the attached model animates with this one) is
// skipped, same "M2Track fields are stage-6-or-later" policy as
// Bone/Color/TextureWeight elsewhere in this file -- it's a bool-ish
// on/off switch, not something husk's glTF export has a slot for yet.
struct Attachment {
    uint32_t id = 0;      // meaning depends on model type; see wowdev.wiki's attachment-point table
    int16_t bone = -1;    // attachment base
    Vec3 position;         // relative to `bone`
};

// Collision mesh (physics/hit-testing), dereferenced from the header's
// `collisionPositions`/`collisionIndices`/`collisionFaceNormals` Array
// descriptors -- wowdev.wiki M2#Header. A plain indexed triangle mesh, no
// M2Track/lookup-table indirection unlike Attachment/Event/Light below:
// `indices` is flat (3 entries per triangle, indexing into `positions`),
// and `faceNormals` has one entry per triangle (indices.size() / 3), not
// per vertex -- distinct from the render mesh's per-vertex normals in
// m2::Vertex.
struct CollisionMesh {
    std::vector<Vec3> positions;
    std::vector<uint16_t> indices;
    std::vector<Vec3> faceNormals;
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
// Sequence elsewhere in this file; the wiki itself marks the trailing
// priorityPlane/RibbonColorIndex/textureTransformLookupIndex fields "TODO:
// verify version", but they're present in every version this parser
// targets either way). `textureIndices`/`materialIndices` (M2Array<uint16_t>
// lookup tables) are surfaced directly; the six M2Track<T> fields
// (colorTrack/alphaTrack/heightAboveTrack/heightBelowTrack/texSlotTrack/
// visibilityTrack) are stored as raw track offsets only -- same
// "descriptor now, real keyframe resolution happens downstream" policy as
// Bone's own three tracks (see Bone::translationTrackOffset's doc comment)
// -- full curve resolution for these lives in `husk dump-chunks`
// (src/cmd_dump.cpp), not here.
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
// Bone/Sequence/Ribbon (see kMinVerifiedRecordStrideVersion). M2Track/
// FBlock fields are stored as raw offsets; full curve resolution lives
// downstream in `husk dump-chunks` (src/cmd_dump.cpp), same split as
// Ribbon's tracks.
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
// resolveFixed16PartTrack's doc comment) mapping the particle's own
// 0.0..1.0 normalized lifetime to an alpha-test cutoff threshold. One entry
// expected per `particle_emitters` array entry (same indexing convention
// TXAC/EXPT/RPID/GPID/PGD1 already use) -- not cross-checked here, same
// "trust this chunk's own byte length" policy dumpTxac already uses.
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

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Parses the header out of a complete M2 file already read into memory.
// Throws ParseError on anything structurally wrong: too short, bad magic,
// a chunked file with no MD21 chunk, or a header shorter than the fixed
// portion this parser reads.
Header parseHeader(const std::vector<uint8_t>& fileBytes);

// Reads `path` fully into memory and calls parseHeader. Throws ParseError
// (I/O failure wrapped in the same error type) or lets parseHeader's own
// ParseError propagate.
Header loadFile(const std::string& path);

// Returns the raw MD20 blob bytes for `fileBytes`, resolving the same
// flat-vs-Legion+-chunked shape parseHeader does. Every M2Array offset --
// including ones parseHeader itself doesn't resolve, like `vertices` -- is
// relative to this blob, not to `fileBytes`. Throws ParseError under the
// same conditions parseHeader does.
std::vector<uint8_t> extractBlob(const std::vector<uint8_t>& fileBytes);

// Reads `array.count` M2Vertex records out of `blob` starting at
// `array.offset`. Throws ParseError if that range runs past the end of the
// blob. An empty array (count 0) returns an empty vector without touching
// `array.offset` at all.
std::vector<Vertex> parseVertices(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` M2CompBone records out of `blob` starting at
// `array.offset`. Throws ParseError if that range runs past the end of the
// blob. An empty array (count 0) returns an empty vector without touching
// `array.offset` at all.
std::vector<Bone> parseBones(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` M2Texture records out of `blob` starting at
// `array.offset`. Throws ParseError if that range runs past the end of the
// blob. An empty array (count 0) returns an empty vector without touching
// `array.offset` at all.
std::vector<Texture> parseTextures(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` M2Material records out of `blob` starting at
// `array.offset`. Throws ParseError if that range runs past the end of the
// blob. An empty array (count 0) returns an empty vector without touching
// `array.offset` at all.
std::vector<Material> parseMaterials(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` little-endian uint16 values out of `blob` at
// `array.offset`. Throws ParseError if that range runs past the end of the
// blob. Used for the header's various uint16 "combo"/lookup arrays (e.g.
// `textureCombos`, wowdev.wiki's "Texture lookup table" -- see
// src/cmd_export.cpp for how batches resolve through it to an actual
// texture).
std::vector<uint16_t> parseUint16Array(const std::vector<uint8_t>& blob, const Array& array);

// Reads `array.count` raw C3Vector (12-byte float triples) out of `blob`
// at `array.offset`. Throws ParseError if that range runs past the end of
// the blob. Used for the header's plain position/normal arrays that carry
// no other per-record fields -- currently just the collision mesh's
// `collisionPositions`/`collisionFaceNormals` (see parseCollisionMesh).
std::vector<Vec3> parseVec3Array(const std::vector<uint8_t>& blob, const Array& array);

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

// Resolves one M2Track<C3Vector>'s keyframe data for exactly one M2Sequence
// index (an index into the `sequences` array `parseSequences` returned,
// *not* an M2Sequence::id) -- see wowdev.wiki M2#Interpolation's "outer
// array indexed by animation" model, and Bone::translationTrackOffset's doc
// comment for what `trackOffset` (and which `blob`) needs to be. Returns
// (timestamp in milliseconds, value) pairs in file order, or an empty
// vector -- not an error -- when `sequenceIndex` is out of range for this
// particular track's own outer array, that entry has zero keyframes, *or*
// the track's global_sequence isn't the "none" sentinel (see TrackMeta) --
// husk doesn't resolve global-sequence tracks into a real looping clip yet,
// and the old behavior of indexing straight into the outer array by
// `sequenceIndex` anyway silently attributed a global-sequence track's data
// to whichever M2Sequence happened to occupy outer-array position
// `sequenceIndex` (usually 0), a real correctness bug, not just a missing
// feature -- returning empty here is strictly more correct than that, even
// though it means such a track currently produces no animation at all.
// Throws ParseError if interpolation_type is 2 or 3: per wowdev.wiki, cubic
// bezier/hermite interpolation is "only valid for M2SplineKey tracks", and
// every M2Track this function is used for (bone translation/scale, M2Color,
// M2TextureWeight) is declared as a plain M2Track<T>, not
// M2Track<M2SplineKey<T>> -- a real file reporting 2/3 here would mean
// either this parser's assumption is wrong for some version, or the file is
// corrupted, and guessing at M2SplineKey's 3x-stride layout for a track
// that per spec shouldn't have it risks exactly the kind of silent
// misread this project's tests exist to catch.
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

// Reads `array.count` M2Attachment records out of `blob` starting at
// `array.offset`, surfacing only `id`/`bone`/`position` (see Attachment's
// doc comment). Throws ParseError if that range runs past the end of the
// blob. An empty array (count 0) returns an empty vector without touching
// `array.offset` at all.
std::vector<Attachment> parseAttachments(const std::vector<uint8_t>& blob, const Array& array);

// Dereferences a header's collision Array descriptors into a real
// CollisionMesh: `positions`/`faceNormals` via parseVec3Array,
// `indices` via parseUint16Array. Throws ParseError under the same
// byte-range conditions as those two functions -- this does NOT
// cross-check `indices` against `positions.size()` or `faceNormals.size()`
// against `indices.size() / 3` (that's export-time consumption logic, same
// "cross-array validation lives at the point of use" split cmd_export.cpp
// already follows for e.g. vertex bone_indices vs. bone count). Every
// array empty (count 0) returns an all-empty CollisionMesh.
CollisionMesh parseCollisionMesh(const std::vector<uint8_t>& blob, const Array& positionsArray,
                                  const Array& indicesArray, const Array& faceNormalsArray);

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

// Best-effort expansion label(s) for a raw header version number, per the
// wiki's own version table -- which the wiki itself calls "rough estimates"
// with overlapping ranges, so this can return multiple labels joined by
// " or ", or "unknown" for anything outside the documented ranges.
std::string expansionForVersion(uint32_t version);

// The lowest header version Bone/Sequence/Ribbon's fixed record strides
// (0x58/0x40/0xB0 bytes -- see their own doc comments above) are documented
// and real-data-verified for: Wrath of the Lich King, per
// expansionForVersion's own table. parseBones/parseSequences/parseRibbons
// don't branch on version at all -- there's no code path that reads a
// different stride for an older file -- so a genuine Classic/TBC M2 (a
// completely normal thing to have from an older-client extraction) would be
// silently decoded at the wrong byte stride: not necessarily a bounds error,
// just quiet wrong data, the same failure shape a wrong stride guess always
// risks (see Sequence's doc comment for one such case this parser already
// had to correct). Callers that resolve these three arrays (cmd_info.cpp,
// cmd_export.cpp) check `header.version < kMinVerifiedRecordStrideVersion`
// and warn loudly rather than silently trusting output this parser was
// never confirmed to read correctly for that version.
// TODO: Remove: WIKI_FINDINGS.md / FAILURES2.md #3 citations for this
// policy's origin.
constexpr uint32_t kMinVerifiedRecordStrideVersion = 264;

}  // namespace husk::m2
