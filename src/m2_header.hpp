#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "m2_primitives.hpp"

// M2 top-level header, texture/material records, and their named-flag
// tables -- split out of the former monolithic m2.hpp/m2.cpp (see
// FILE_SPLIT_TODO.md Item 2). See m2_primitives.hpp for the module
// overview (on-disk shapes, blob-resolution entry points).
namespace husk::m2 {

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
    // (see CollisionMesh's doc comment, m2_skeleton.hpp). The
    // object-placement/gameplay arrays that follow it are not: surfacing
    // their Array descriptors themselves is what `husk info` needs to
    // report counts, and is the minimum needed to keep this struct's field
    // layout complete and in wire order (see parseVertices/parseBones/
    // parseTextures for the dereferencing pattern, if one of these needs it
    // later).
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
const char* keyBoneName(int32_t keyBoneId);

// Names an attachment's `id` (Attachment::id, wowdev.wiki M2#Attachments'
// "Attachment Lookup" table -- Shield, HandRight, Helm, Head, Chest,
// Breath, etc., 61 real entries), or nullptr for a value the table doesn't
// cover (IDs 58/59 are real gaps in the wiki's own table, not an omission
// here). Same authority level as `keyBoneName` -- real per-file data, not a
// guess -- used to name a bone that has an attachment but no `keyBoneId` of
// its own.
const char* attachmentTypeName(uint32_t id);

// Names an event's `identifier` (Event::identifier, wowdev.wiki M2#Events'
// "Possible Events" table -- $BTH=Breath, $HIT=PlayWoundAnimKit, etc.), or
// nullptr for an identifier the table doesn't document at all, or
// documents as real but with no known meaning (e.g. $CHD -- the wiki's own
// "probably does not exist?!" -- or $CVS/$KVS/$WWG/DEST/POIN/WHEE/BOTT/TOP,
// all genuinely undocumented; see BONE_NAME_DEDUCTION_TODO.md for what
// real corpus investigation filling these in would take). Weaker signal
// than `attachmentTypeName`: the wiki table itself has real gaps, not just
// omissions here. Bracket-ranged identifiers (e.g. "$AH[0-3]" documenting
// "$AH0".."$AH3" at once) are matched by stripping a trailing digit and
// checking a prefix table -- every real range in the source table is a
// single trailing digit (0-9), never multi-digit.
const char* eventName(const std::string& identifier);

// Names a texture's `type` (Texture::type, wowdev.wiki M2#Textures'
// "Texture types" table) -- "skin", "char_hair", "guild_emblem", etc., or
// nullptr for type 0 (a real embedded filename, no name needed here) or a
// value the table doesn't cover (IDs 24-26 are real but the wiki gives no
// name for them, just "seen in DracthyrDragon.m2" -- not an omission
// here). Used to give a hardcoded/runtime-resolved texture slot (one husk
// can't embed a real image for -- no CASC/DB2 access) a real semantic
// material name instead of a bare "_tex<N>" -- see cmd_export.cpp's
// material-naming code.
const char* textureTypeName(uint32_t type);

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

}  // namespace husk::m2
