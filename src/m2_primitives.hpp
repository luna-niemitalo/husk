#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// M2 primitive types and blob-parsing entry points, per
// https://wowdev.wiki/M2 (fetched 2026-07-24) -- split out of the former
// monolithic m2.hpp/m2.cpp (see FILE_SPLIT_TODO.md Item 2). This is the
// base module every other m2_*.hpp builds on: the plain (count, offset)/
// vector/quaternion primitives every struct elsewhere is made of, the
// bounds-checked blob-read helpers every parse* function elsewhere calls,
// and the top-level entry points (parseHeader/loadFile/extractBlob) that
// resolve the file's outer shape before any struct-specific parsing begins.
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

// Forward declaration only -- avoids a circular #include with
// m2_header.hpp (which needs Array/BoundingBox from *this* header for
// Header's own fields). A function *declaration* returning Header doesn't
// need the complete type, only a definition or a call site does -- every
// real caller reaches this via m2.hpp, which includes both headers, so
// Header is always complete by the time parseHeader/loadFile are actually
// invoked.
struct Header;

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
// format (src/m2_animation.cpp's readCompQuat does the actual unpacking).
// Field order matches Blizzard's own C4Quaternion: w last, not first
// (wowdev.wiki Common_Types#C4Quaternion's explicit warning about this).
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

struct BoundingBox {
    Vec3 min;
    Vec3 max;
};

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Bounds-checked blob-read primitives shared across every m2_*.cpp file --
// were file-local (anonymous namespace) helpers in the pre-split m2.cpp;
// promoted to real declarations here so m2_header.cpp/m2_skeleton.cpp/
// m2_animation.cpp/m2_scene.cpp can all call them across the TU boundary
// (same pattern used for export_transform.hpp's scanDirOrWarn precedent,
// see FILE_SPLIT_TODO.md).
uint32_t readU32(const uint8_t* blob, size_t blobSize, size_t off);
uint16_t readU16(const uint8_t* blob, size_t blobSize, size_t off);
uint8_t readU8(const uint8_t* blob, size_t blobSize, size_t off);
float readF32(const uint8_t* blob, size_t blobSize, size_t off);
Array readArray(const uint8_t* blob, size_t blobSize, size_t off);
Vec3 readVec3(const uint8_t* blob, size_t blobSize, size_t off);
BoundingBox readBoundingBox(const uint8_t* blob, size_t blobSize, size_t off);

// Reads the `name` M2Array<char> as a string, trimming a trailing NUL if
// present. Bounds-checked against the blob independently of the fixed
// header fields above, since this offset/count pair is foreign data (it
// came from inside the file, not from our own offset table).
std::string readName(const uint8_t* blob, size_t blobSize, Array nameArray);

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

// Best-effort expansion label(s) for a raw header version number, per the
// wiki's own version table -- which the wiki itself calls "rough estimates"
// with overlapping ranges, so this can return multiple labels joined by
// " or ", or "unknown" for anything outside the documented ranges.
std::string expansionForVersion(uint32_t version);

// The lowest header version Bone/Sequence/Ribbon's fixed record strides
// (0x58/0x40/0xB0 bytes -- see their own doc comments) are documented and
// real-data-verified for: Wrath of the Lich King, per expansionForVersion's
// own table. parseBones/parseSequences/parseRibbons don't branch on version
// at all -- there's no code path that reads a different stride for an older
// file -- so a genuine Classic/TBC M2 (a completely normal thing to have
// from an older-client extraction) would be silently decoded at the wrong
// byte stride: not necessarily a bounds error, just quiet wrong data, the
// same failure shape a wrong stride guess always risks (see Sequence's doc
// comment in m2_animation.hpp for one such case this parser already had to
// correct). Callers that resolve these three arrays (cmd_info.cpp,
// cmd_export.cpp) check `header.version < kMinVerifiedRecordStrideVersion`
// and warn loudly rather than silently trusting output this parser was
// never confirmed to read correctly for that version.
// TODO: Remove: WIKI_FINDINGS.md / FAILURES2.md #3 citations for this
// policy's origin.
constexpr uint32_t kMinVerifiedRecordStrideVersion = 264;

}  // namespace husk::m2
