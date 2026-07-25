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

// Best-effort expansion label(s) for a raw header version number, per the
// wiki's own version table -- which the wiki itself calls "rough estimates"
// with overlapping ranges, so this can return multiple labels joined by
// " or ", or "unknown" for anything outside the documented ranges.
std::string expansionForVersion(uint32_t version);

}  // namespace husk::m2
