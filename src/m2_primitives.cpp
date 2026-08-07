#include "m2_primitives.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <optional>

#include "chunk.hpp"
#include "m2_header.hpp"

namespace husk::m2 {

uint32_t readU32(const uint8_t* blob, size_t blobSize, size_t off) {
    if (off + 4 > blobSize) {
        throw ParseError("header field at offset 0x" + std::to_string(off) +
                          " needs 4 bytes but the blob is only " + std::to_string(blobSize) +
                          " bytes");
    }
    uint32_t v;
    std::memcpy(&v, blob + off, sizeof(v));
    return v;
}

uint16_t readU16(const uint8_t* blob, size_t blobSize, size_t off) {
    if (off + 2 > blobSize) {
        throw ParseError("header field at offset 0x" + std::to_string(off) +
                          " needs 2 bytes but the blob is only " + std::to_string(blobSize) +
                          " bytes");
    }
    uint16_t v;
    std::memcpy(&v, blob + off, sizeof(v));
    return v;
}

uint8_t readU8(const uint8_t* blob, size_t blobSize, size_t off) {
    if (off + 1 > blobSize) {
        throw ParseError("header field at offset 0x" + std::to_string(off) +
                          " needs 1 byte but the blob is only " + std::to_string(blobSize) +
                          " bytes");
    }
    return blob[off];
}

float readF32(const uint8_t* blob, size_t blobSize, size_t off) {
    uint32_t bits = readU32(blob, blobSize, off);
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

Array readArray(const uint8_t* blob, size_t blobSize, size_t off) {
    Array a;
    a.count = readU32(blob, blobSize, off);
    a.offset = readU32(blob, blobSize, off + 4);
    return a;
}

Vec3 readVec3(const uint8_t* blob, size_t blobSize, size_t off) {
    Vec3 v;
    v.x = readF32(blob, blobSize, off);
    v.y = readF32(blob, blobSize, off + 4);
    v.z = readF32(blob, blobSize, off + 8);
    return v;
}

BoundingBox readBoundingBox(const uint8_t* blob, size_t blobSize, size_t off) {
    BoundingBox b;
    b.min = readVec3(blob, blobSize, off);
    b.max = readVec3(blob, blobSize, off + 12);
    return b;
}

std::string readName(const uint8_t* blob, size_t blobSize, Array nameArray) {
    if (nameArray.count == 0) {
        return "";
    }
    size_t start = nameArray.offset;
    size_t len = nameArray.count;
    if (start > blobSize || len > blobSize - start) {
        throw ParseError("name field claims " + std::to_string(len) + " bytes at offset " +
                          std::to_string(start) + ", which runs past the end of the blob (" +
                          std::to_string(blobSize) + " bytes)");
    }
    std::string s(reinterpret_cast<const char*>(blob + start), len);
    while (!s.empty() && s.back() == '\0') {
        s.pop_back();
    }
    return s;
}

namespace {

// Byte offsets into the MD20 blob, transcribed from wowdev.wiki's M2 header
// table (offsets as documented for expansion level >= 3). Kept as named
// offsets rather than a packed C struct so field presence/absence never
// depends on compiler struct-layout behavior -- every read below is an
// explicit, bounds-checked memcpy at a fixed offset.
namespace offset {
constexpr size_t magic = 0x000;
constexpr size_t version = 0x004;
constexpr size_t name = 0x008;
constexpr size_t globalFlags = 0x010;
constexpr size_t globalLoops = 0x014;
constexpr size_t sequences = 0x01C;
constexpr size_t sequenceLookup = 0x024;
constexpr size_t bones = 0x02C;
constexpr size_t boneLookup = 0x034;
constexpr size_t vertices = 0x03C;
constexpr size_t numSkinProfiles = 0x044;
constexpr size_t colors = 0x048;
constexpr size_t textures = 0x050;
constexpr size_t textureWeights = 0x058;
constexpr size_t textureTransforms = 0x060;
constexpr size_t textureLookup = 0x068;
constexpr size_t materials = 0x070;
constexpr size_t boneCombos = 0x078;
constexpr size_t textureCombos = 0x080;
constexpr size_t textureCoordCombos = 0x088;
constexpr size_t textureWeightCombos = 0x090;
constexpr size_t textureTransformCombos = 0x098;
constexpr size_t boundingBox = 0x0A0;   // CAaBox: 2x C3Vector, 24 bytes
constexpr size_t boundingSphereRadius = 0x0B8;
constexpr size_t collisionBox = 0x0BC;  // 24 bytes
constexpr size_t collisionSphereRadius = 0x0D4;
constexpr size_t collisionIndices = 0x0D8;
constexpr size_t collisionPositions = 0x0E0;
constexpr size_t collisionFaceNormals = 0x0E8;
constexpr size_t attachments = 0x0F0;
constexpr size_t attachmentLookup = 0x0F8;
constexpr size_t events = 0x100;
constexpr size_t lights = 0x108;
constexpr size_t cameras = 0x110;
constexpr size_t cameraLookup = 0x118;
constexpr size_t ribbonEmitters = 0x120;
constexpr size_t particleEmitters = 0x128;

// Only present in the wire header when GlobalFlag::kUseTextureCombinerCombos
// is set (wowdev.wiki M2#Header) -- read conditionally in parseBlob, not
// part of the unconditional field walk above.
constexpr size_t textureCombinerCombos = 0x130;

// End of the last field this parser unconditionally reads (particleEmitters,
// an 8-byte Array). The real header can continue further --
// textureCombinerCombos, read conditionally above -- this is the minimum a
// header must be for every unconditional field to be safely readable, not
// the full struct size.
constexpr size_t minHeaderSize = particleEmitters + 8;
}  // namespace offset

Header parseBlob(const uint8_t* blob, size_t blobSize, bool chunked) {
    uint32_t magic = readU32(blob, blobSize, offset::magic);
    if (magic != 0x3032444D /* "MD20" little-endian */) {
        char bytes[5] = {};
        std::memcpy(bytes, &magic, 4);
        throw ParseError(std::string("expected MD20 magic in the M2 data blob, got '") + bytes +
                          "'");
    }

    if (blobSize < offset::minHeaderSize) {
        throw ParseError("M2 header blob is " + std::to_string(blobSize) +
                          " bytes, need at least " + std::to_string(offset::minHeaderSize) +
                          " for the fields this parser reads");
    }

    Header h;
    h.magic = magic;
    h.chunked = chunked;
    h.version = readU32(blob, blobSize, offset::version);
    h.name = readName(blob, blobSize, readArray(blob, blobSize, offset::name));
    h.globalFlags = readU32(blob, blobSize, offset::globalFlags);
    h.globalLoops = readArray(blob, blobSize, offset::globalLoops);
    h.sequences = readArray(blob, blobSize, offset::sequences);
    h.sequenceLookup = readArray(blob, blobSize, offset::sequenceLookup);
    h.bones = readArray(blob, blobSize, offset::bones);
    h.boneLookup = readArray(blob, blobSize, offset::boneLookup);
    h.vertices = readArray(blob, blobSize, offset::vertices);
    h.numSkinProfiles = readU32(blob, blobSize, offset::numSkinProfiles);
    h.colors = readArray(blob, blobSize, offset::colors);
    h.textures = readArray(blob, blobSize, offset::textures);
    h.textureWeights = readArray(blob, blobSize, offset::textureWeights);
    h.textureTransforms = readArray(blob, blobSize, offset::textureTransforms);
    h.textureLookup = readArray(blob, blobSize, offset::textureLookup);
    h.materials = readArray(blob, blobSize, offset::materials);
    h.boneCombos = readArray(blob, blobSize, offset::boneCombos);
    h.textureCombos = readArray(blob, blobSize, offset::textureCombos);
    h.textureCoordCombos = readArray(blob, blobSize, offset::textureCoordCombos);
    h.textureWeightCombos = readArray(blob, blobSize, offset::textureWeightCombos);
    h.textureTransformCombos = readArray(blob, blobSize, offset::textureTransformCombos);
    h.boundingBox = readBoundingBox(blob, blobSize, offset::boundingBox);
    h.boundingSphereRadius = readF32(blob, blobSize, offset::boundingSphereRadius);
    h.collisionBox = readBoundingBox(blob, blobSize, offset::collisionBox);
    h.collisionSphereRadius = readF32(blob, blobSize, offset::collisionSphereRadius);
    h.collisionIndices = readArray(blob, blobSize, offset::collisionIndices);
    h.collisionPositions = readArray(blob, blobSize, offset::collisionPositions);
    h.collisionFaceNormals = readArray(blob, blobSize, offset::collisionFaceNormals);
    h.attachments = readArray(blob, blobSize, offset::attachments);
    h.attachmentLookup = readArray(blob, blobSize, offset::attachmentLookup);
    h.events = readArray(blob, blobSize, offset::events);
    h.lights = readArray(blob, blobSize, offset::lights);
    h.cameras = readArray(blob, blobSize, offset::cameras);
    h.cameraLookup = readArray(blob, blobSize, offset::cameraLookup);
    h.ribbonEmitters = readArray(blob, blobSize, offset::ribbonEmitters);
    h.particleEmitters = readArray(blob, blobSize, offset::particleEmitters);

    // textureCombinerCombos only exists in the wire header at all when this
    // flag bit is set (wowdev.wiki M2#Header) -- a real file with the flag
    // set but a blob too short for the extra 8 bytes is foreign data that
    // doesn't fit its own claim (same ParseError-on-truncation discipline
    // every array/record read in this file already uses), not something to
    // silently leave as an empty Array.
    if (h.globalFlags & GlobalFlag::kUseTextureCombinerCombos) {
        if (blobSize < offset::textureCombinerCombos + 8) {
            throw ParseError(
                "header sets flag_use_texture_combiner_combos but the blob (" +
                std::to_string(blobSize) + " bytes) is too short to hold the textureCombinerCombos "
                "array at offset 0x130");
        }
        h.textureCombinerCombos = readArray(blob, blobSize, offset::textureCombinerCombos);
    }
    return h;
}

// Many Legion+ chunks are just "one FileDataID" (e.g. SKID, wowdev.wiki
// M2#SKID) or "a flat array of FileDataIDs, one per some other array's
// entry" (e.g. TXID, M2#TXID) replacing what used to be a computed
// filename string -- SFID/AFID/BFID/PFID/SKID/TXID/RPID/GPID all follow one
// of these two shapes (see README.md's Design notes for why this keeps
// happening). Centralized here so wiring up the next one is a one-line
// call at the bottom of resolveBlob(), not a second hand-copied loop.
std::optional<uint32_t> findFileDataIdChunk(const std::vector<Chunk>& chunks, std::string_view tag) {
    auto chunk = findChunk(chunks, tag);
    if (!chunk) {
        return std::nullopt;
    }
    return readU32(chunk->data, chunk->size, 0);
}

std::optional<std::vector<uint32_t>> findFileDataIdArrayChunk(const std::vector<Chunk>& chunks,
                                                                std::string_view tag) {
    auto chunk = findChunk(chunks, tag);
    if (!chunk) {
        return std::nullopt;
    }
    // `chunk->size` isn't itself an M2Array-style count -- it's just the
    // chunk's raw byte length, so this is entries-of-4-bytes, not a count
    // field to trust/validate against some other array's count (that
    // cross-check, if any, is the consumer's job -- e.g. src/cmd_export.cpp
    // for TXID vs `textures.count`). A byte length that isn't itself a
    // multiple of 4 means a truncated/corrupted chunk -- every other
    // fixed-record-array parser in this file errors loudly on foreign data
    // that doesn't fit its own claims; silently dropping the last partial
    // entry here would be the one place that convention broke down.
    // TODO: Remove: FAILURES.md #2, FAILURES2.md #8.
    if (chunk->size % 4 != 0) {
        throw ParseError(std::string(tag) + " chunk is " + std::to_string(chunk->size) +
                          " bytes, not a multiple of 4 (one uint32 FileDataID per entry) -- "
                          "truncated or corrupted file?");
    }
    size_t count = chunk->size / 4;
    std::vector<uint32_t> ids;
    ids.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        ids.push_back(readU32(chunk->data, chunk->size, i * 4));
    }
    return ids;
}

// AFID (wowdev.wiki M2#AFID): a struct array, not a flat FileDataID array
// -- { uint16_t anim_id; uint16_t sub_anim_id; uint32_t file_id; }[], 8
// bytes per entry. Doesn't fit findFileDataIdArrayChunk's shape, but is
// just as small a helper.
std::optional<std::vector<Header::AnimFileEntry>> findAnimFileIdChunk(
    const std::vector<Chunk>& chunks, std::string_view tag) {
    auto chunk = findChunk(chunks, tag);
    if (!chunk) {
        return std::nullopt;
    }
    constexpr size_t kEntrySize = 8;
    // See findFileDataIdArrayChunk's identical check above -- same
    // reasoning, applied to AFID's 8-byte record instead of a flat 4-byte
    // FileDataID.
    // TODO: Remove: FAILURES2.md #8.
    if (chunk->size % kEntrySize != 0) {
        throw ParseError(std::string(tag) + " chunk is " + std::to_string(chunk->size) +
                          " bytes, not a multiple of " + std::to_string(kEntrySize) +
                          " (one {anim_id, sub_anim_id, file_id} entry per record) -- truncated "
                          "or corrupted file?");
    }
    size_t count = chunk->size / kEntrySize;
    std::vector<Header::AnimFileEntry> entries;
    entries.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        size_t off = i * kEntrySize;
        Header::AnimFileEntry e;
        e.animId = readU16(chunk->data, chunk->size, off + 0x00);
        e.subAnimId = readU16(chunk->data, chunk->size, off + 0x02);
        e.fileId = readU32(chunk->data, chunk->size, off + 0x04);
        entries.push_back(e);
    }
    return entries;
}

struct ResolvedBlob {
    std::vector<uint8_t> bytes;
    bool chunked;
    // Set only for chunked files that carry an SKID chunk (wowdev.wiki
    // M2#SKID) -- see Header::skeletonFileId.
    std::optional<uint32_t> skeletonFileId;
    // Set only for chunked files that carry a PFID chunk (wowdev.wiki
    // M2#PFID) -- see Header::physFileId.
    std::optional<uint32_t> physFileId;
    // Set only for chunked files that carry a TXID chunk (wowdev.wiki
    // M2#TXID) -- see Header::textureFileDataIds.
    std::optional<std::vector<uint32_t>> textureFileDataIds;
    // Set only for chunked files that carry an SFID chunk (wowdev.wiki
    // M2#SFID) -- see Header::skinFileDataIds.
    std::optional<std::vector<uint32_t>> skinFileDataIds;
    // Set only for chunked files that carry an LDV1 chunk (wowdev.wiki
    // M2#LDV1) -- see Header::lodCount.
    std::optional<uint16_t> lodCount;
    // Set only for chunked files that carry a BFID chunk (wowdev.wiki
    // M2#BFID) -- see Header::boneFileDataIds.
    std::optional<std::vector<uint32_t>> boneFileDataIds;
    // Set only for chunked files that carry an AFID chunk (wowdev.wiki
    // M2#AFID) -- see Header::animFileIds.
    std::optional<std::vector<Header::AnimFileEntry>> animFileIds;
    // Every top-level chunk tag found, in file order -- see
    // Header::chunkTags.
    std::vector<std::string> chunkTags;
};

// Resolves the flat-MD20-vs-Legion+-chunked shape shared by parseHeader and
// extractBlob: either the file's bytes are already the MD20 blob, or the
// MD21 chunk's payload is (wowdev.wiki M2#MD21). Copies the bytes out so
// callers own a self-contained blob regardless of which case applied.
ResolvedBlob resolveBlob(const std::vector<uint8_t>& fileBytes) {
    if (fileBytes.size() < 4) {
        throw ParseError("file is only " + std::to_string(fileBytes.size()) +
                          " bytes, too short to even contain a magic value");
    }

    uint32_t firstMagic;
    std::memcpy(&firstMagic, fileBytes.data(), 4);

    // Pre-Legion: the file itself starts with MD20, no chunk wrapper.
    if (firstMagic == 0x3032444D /* "MD20" */) {
        return {fileBytes, /*chunked=*/false, std::nullopt};
    }

    // Legion+: the file is chunks; MD21's payload is the MD20 blob, offsets
    // relative to that chunk's own start (wowdev.wiki M2#MD21).
    auto chunks = readChunks(fileBytes.data(), fileBytes.size());
    auto md21 = findChunk(chunks, "MD21");
    if (!md21) {
        std::string found;
        for (const auto& c : chunks) {
            if (!found.empty()) found += ", ";
            found += c.tag;
        }
        throw ParseError("file doesn't start with MD20 and has no MD21 chunk; chunks found: [" +
                          found + "]");
    }

    std::optional<uint32_t> skeletonFileId = findFileDataIdChunk(chunks, "SKID");
    std::optional<uint32_t> physFileId = findFileDataIdChunk(chunks, "PFID");
    std::optional<std::vector<uint32_t>> textureFileDataIds =
        findFileDataIdArrayChunk(chunks, "TXID");
    std::optional<std::vector<uint32_t>> skinFileDataIds = findFileDataIdArrayChunk(chunks, "SFID");

    // LDV1 (wowdev.wiki M2#LDV1): struct LodData { uint16 unk0; uint16
    // lodCount; ... } -- only lodCount (offset 0x02) is surfaced, see
    // Header::lodCount.
    std::optional<uint16_t> lodCount;
    if (auto ldv1 = findChunk(chunks, "LDV1")) {
        lodCount = readU16(ldv1->data, ldv1->size, 0x02);
    }

    std::optional<std::vector<uint32_t>> boneFileDataIds = findFileDataIdArrayChunk(chunks, "BFID");
    std::optional<std::vector<Header::AnimFileEntry>> animFileIds =
        findAnimFileIdChunk(chunks, "AFID");

    std::vector<std::string> chunkTags;
    chunkTags.reserve(chunks.size());
    for (const auto& c : chunks) {
        chunkTags.push_back(c.tag);
    }

    return {std::vector<uint8_t>(md21->data, md21->data + md21->size),
            /*chunked=*/true,
            skeletonFileId,
            physFileId,
            textureFileDataIds,
            skinFileDataIds,
            lodCount,
            boneFileDataIds,
            animFileIds,
            chunkTags};
}

}  // namespace

Header parseHeader(const std::vector<uint8_t>& fileBytes) {
    auto resolved = resolveBlob(fileBytes);
    Header h = parseBlob(resolved.bytes.data(), resolved.bytes.size(), resolved.chunked);
    h.skeletonFileId = resolved.skeletonFileId;
    h.physFileId = resolved.physFileId;
    h.textureFileDataIds = resolved.textureFileDataIds;
    h.skinFileDataIds = resolved.skinFileDataIds;
    h.lodCount = resolved.lodCount;
    h.boneFileDataIds = resolved.boneFileDataIds;
    h.animFileIds = resolved.animFileIds;
    h.chunkTags = resolved.chunkTags;
    return h;
}

std::vector<uint8_t> extractBlob(const std::vector<uint8_t>& fileBytes) {
    return resolveBlob(fileBytes).bytes;
}

Header loadFile(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw ParseError("couldn't open '" + path + "' for reading: " + std::strerror(errno));
    }
    errno = 0;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw ParseError("error reading '" + path + "': " + std::strerror(errno));
    }
    return parseHeader(bytes);
}

std::string expansionForVersion(uint32_t version) {
    // Transcribed verbatim from the wowdev.wiki M2#Versions table, including
    // its overlaps -- the wiki itself says these are rough estimates, not a
    // clean partition, so a version landing in more than one row is
    // expected, not a bug in this table.
    struct Row {
        uint32_t lo, hi;
        const char* label;
    };
    static const Row rows[] = {
        {256, 256, "Pre-Release"},
        {256, 257, "Classic"},
        {260, 263, "The Burning Crusade"},
        {264, 264, "Wrath of the Lich King"},
        {265, 272, "Cataclysm"},
        {272, 272, "Mists of Pandaria / Warlords of Draenor"},
        {272, 274, "Legion / Battle for Azeroth / Shadowlands"},
    };

    std::string result;
    for (const auto& row : rows) {
        if (version >= row.lo && version <= row.hi) {
            if (!result.empty()) result += " or ";
            result += row.label;
        }
    }
    return result.empty() ? "unknown" : result;
}

}  // namespace husk::m2
