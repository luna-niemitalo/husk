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

    bool chunked = false;  // true if this file was Legion+ MD21-wrapped

    // FileDataID of an external .skel file (wowdev.wiki M2#SKID) that this
    // model's `bones` array actually lives in, when `bones.count == 0`
    // doesn't mean "no skeleton" -- see husk::skel. Only ever set for
    // chunked files that happen to carry an SKID chunk; husk doesn't
    // resolve this ID to a path itself (no CASC/listfile access), so
    // callers that want the actual bones still need a .skel path from
    // elsewhere (see `husk export`'s optional 4th argument).
    std::optional<uint32_t> skeletonFileId;

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
struct Color {
    std::optional<Vec3> color;   // 0..1 per channel, rgb order
    std::optional<float> alpha;  // 0 (transparent) .. 1 (opaque)
};

struct TextureWeight {
    std::optional<float> weight;  // 0..1; wiki: "I assume these are multiplied together" with Color::alpha
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

// Best-effort expansion label(s) for a raw header version number, per the
// wiki's own version table -- which the wiki itself calls "rough estimates"
// with overlapping ranges, so this can return multiple labels joined by
// " or ", or "unknown" for anything outside the documented ranges.
std::string expansionForVersion(uint32_t version);

}  // namespace husk::m2
