#include "m2.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "chunk.hpp"

namespace husk::m2 {

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

// End of the last field this parser reads (particleEmitters, an 8-byte
// Array). The real header continues further, version-gated (e.g.
// textureCombinerCombos, only present when a global flag bit is set) --
// this is the minimum a header must be for every field above to be safely
// readable, not the full struct size.
constexpr size_t minHeaderSize = particleEmitters + 8;
}  // namespace offset

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

// Reads the `name` M2Array<char> as a string, trimming a trailing NUL if
// present. Bounds-checked against the blob independently of the fixed
// header fields above, since this offset/count pair is foreign data (it
// came from inside the file, not from our own offset table).
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
    // that doesn't fit its own claims (FAILURES.md #2); silently dropping
    // the last partial entry here would be the one place that convention
    // broke down (FAILURES2.md #8).
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
    // See findFileDataIdArrayChunk's identical check above (FAILURES2.md #8)
    // -- same reasoning, applied to AFID's 8-byte record instead of a flat
    // 4-byte FileDataID.
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

std::vector<Vertex> parseVertices(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Vertex> vertices;
    if (array.count == 0) {
        return vertices;
    }

    constexpr size_t kVertexSize = 0x30;  // M2Vertex, wowdev.wiki M2#Vertices
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    // Validate the whole claimed range up front, before reserve() -- a
    // corrupted or misread count (see FAILURES.md #2: this is what a
    // version this parser doesn't know about yet, shifting this offset,
    // looks like) must not be able to make this allocate hundreds of GB
    // before the very first per-element check below ever runs.
    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kVertexSize) {
        throw ParseError("vertices array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kVertexSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    vertices.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kVertexSize;
        Vertex v;
        v.pos = readVec3(data, blobSize, off + 0x00);
        for (int j = 0; j < 4; ++j) v.boneWeights[j] = data[off + 0x0C + j];
        for (int j = 0; j < 4; ++j) v.boneIndices[j] = data[off + 0x10 + j];
        v.normal = readVec3(data, blobSize, off + 0x14);
        v.texCoords[0].x = readF32(data, blobSize, off + 0x20);
        v.texCoords[0].y = readF32(data, blobSize, off + 0x24);
        v.texCoords[1].x = readF32(data, blobSize, off + 0x28);
        v.texCoords[1].y = readF32(data, blobSize, off + 0x2C);
        vertices.push_back(v);
    }

    return vertices;
}

std::vector<Bone> parseBones(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Bone> bones;
    if (array.count == 0) {
        return bones;
    }

    // M2CompBone, >= Wrath shape (wowdev.wiki M2#Bones -- see the offset
    // table transcribed independently in tests/test_m2.cpp).
    constexpr size_t kBoneSize = 0x58;
    constexpr size_t kKeyBoneIdOffset = 0x00;
    constexpr size_t kFlagsOffset = 0x04;
    constexpr size_t kParentBoneOffset = 0x08;
    constexpr size_t kTranslationTrackOffset = 0x10;
    constexpr size_t kRotationTrackOffset = 0x24;
    constexpr size_t kScaleTrackOffset = 0x38;
    constexpr size_t kPivotOffset = 0x4C;

    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    // Same up-front validation as parseVertices above, and for the same
    // reason (FAILURES.md #2) -- a corrupted/misread count must fail with
    // a real message here, not a bare std::bad_alloc from reserve().
    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kBoneSize) {
        throw ParseError("bones array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kBoneSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    bones.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kBoneSize;
        Bone b;
        b.keyBoneId = static_cast<int32_t>(readU32(data, blobSize, off + kKeyBoneIdOffset));
        b.flags = readU32(data, blobSize, off + kFlagsOffset);
        uint16_t parentBoneBits;
        std::memcpy(&parentBoneBits, data + off + kParentBoneOffset, sizeof(parentBoneBits));
        b.parentBone = static_cast<int16_t>(parentBoneBits);
        b.translationTrackOffset = static_cast<uint32_t>(off + kTranslationTrackOffset);
        b.rotationTrackOffset = static_cast<uint32_t>(off + kRotationTrackOffset);
        b.scaleTrackOffset = static_cast<uint32_t>(off + kScaleTrackOffset);
        b.pivot = readVec3(data, blobSize, off + kPivotOffset);
        bones.push_back(b);
    }

    return bones;
}

const char* billboardModeName(uint32_t flags) {
    if (flags & BoneFlag::kSphericalBillboard) return "spherical";
    if (flags & BoneFlag::kCylindricalBillboardLockX) return "cylindrical_lock_x";
    if (flags & BoneFlag::kCylindricalBillboardLockY) return "cylindrical_lock_y";
    if (flags & BoneFlag::kCylindricalBillboardLockZ) return "cylindrical_lock_z";
    return nullptr;
}

std::vector<Texture> parseTextures(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Texture> textures;
    if (array.count == 0) {
        return textures;
    }

    // M2Texture, wowdev.wiki M2#Textures: type(u32) + flags(u32) +
    // filename(M2Array<char>, 8 bytes) = 16 bytes.
    constexpr size_t kTextureSize = 0x10;
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    // Same up-front validation as parseVertices/parseBones, same reason
    // (FAILURES.md #2).
    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kTextureSize) {
        throw ParseError("textures array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kTextureSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    textures.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kTextureSize;
        Texture t;
        t.type = readU32(data, blobSize, off + 0x00);
        t.flags = readU32(data, blobSize, off + 0x04);
        t.filename = readName(data, blobSize, readArray(data, blobSize, off + 0x08));
        textures.push_back(t);
    }

    return textures;
}

std::vector<Material> parseMaterials(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Material> materials;
    if (array.count == 0) {
        return materials;
    }

    constexpr size_t kMaterialSize = 0x04;  // M2Material: flags(u16) + blendMode(u16)
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kMaterialSize) {
        throw ParseError("materials array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kMaterialSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    materials.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kMaterialSize;
        Material m;
        m.flags = readU16(data, blobSize, off + 0x00);
        m.blendMode = readU16(data, blobSize, off + 0x02);
        materials.push_back(m);
    }

    return materials;
}

std::vector<uint16_t> parseUint16Array(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<uint16_t> values;
    if (array.count == 0) {
        return values;
    }

    constexpr size_t kElementSize = 2;
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kElementSize) {
        throw ParseError("array claims " + std::to_string(array.count) +
                          " uint16 entries at offset " + std::to_string(array.offset) +
                          ", which needs more room than the blob's " + std::to_string(blobSize) +
                          " bytes");
    }
    values.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kElementSize;
        uint16_t v;
        std::memcpy(&v, data + off, sizeof(v));
        values.push_back(v);
    }

    return values;
}

std::vector<Vec3> parseVec3Array(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Vec3> values;
    if (array.count == 0) {
        return values;
    }

    constexpr size_t kElementSize = 12;  // C3Vector: 3x float32
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kElementSize) {
        throw ParseError("array claims " + std::to_string(array.count) +
                          " C3Vector entries at offset " + std::to_string(array.offset) +
                          ", which needs more room than the blob's " + std::to_string(blobSize) +
                          " bytes");
    }
    values.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kElementSize;
        values.push_back(readVec3(data, blobSize, off));
    }

    return values;
}

CollisionMesh parseCollisionMesh(const std::vector<uint8_t>& blob, const Array& positionsArray,
                                  const Array& indicesArray, const Array& faceNormalsArray) {
    CollisionMesh mesh;
    mesh.positions = parseVec3Array(blob, positionsArray);
    mesh.indices = parseUint16Array(blob, indicesArray);
    mesh.faceNormals = parseVec3Array(blob, faceNormalsArray);
    return mesh;
}

// Locates an M2Track<T>'s single value, *only* when the track is
// unambiguously constant -- exactly one animation sub-array (outer.count
// == 1) with exactly one keyframe in it (inner.count == 1). Anything else
// is real per-sequence or globally-looped keyframe animation (roadmap
// stage 6, not this parser's job), and returns nullopt rather than
// guessing. This distinction mattered in practice, not just in theory: an
// earlier version of this code took element [0][0] unconditionally, which
// for a real bloodelffemale.m2 batch meant reading sequence 0's *first*
// alpha keyframe (0 -- fully transparent) as if it were a sensible default,
// which would have silently rendered the entire model invisible. Requiring
// both counts to be exactly 1 is what `interpolation_ranges`/
// "Blocks that use global sequences also only have one track" (wowdev.wiki
// M2#Interpolation) actually describes as a non-animated block; anything
// looser risks repeating that exact mistake. Returns the byte offset of
// the single value on success, checked the same bounds-checked way any
// other M2Array element access in this file is.
std::optional<size_t> constantTrackValueOffset(const uint8_t* data, size_t blobSize,
                                                size_t trackOffset) {
    Array outer = readArray(data, blobSize, trackOffset + 0x0C);
    if (outer.count != 1) {
        return std::nullopt;
    }
    Array inner = readArray(data, blobSize, outer.offset);
    if (inner.count != 1) {
        return std::nullopt;
    }
    return inner.offset;
}

// True when an M2Track<T> carries real keyframe data beyond the
// single-constant-value case constantTrackValueOffset resolves --
// per-M2Sequence animation (outer.count > 1), or a global-sequence-driven
// loop (outer.count == 1 but with more than one keyframe in its single
// sub-array). Both constantTrackValueOffset and this function agree on
// "empty" (outer.count == 0): neither an animated nor a constant track,
// just genuinely no data. This is what lets a caller distinguish "this
// M2Color/M2TextureWeight track is nullopt because it's genuinely empty"
// from "...because it's animated and husk has no way to represent that in
// a static baseColorFactor" -- see Color::colorAnimated/alphaAnimated and
// TextureWeight::weightAnimated.
bool trackHasAnimatedData(const uint8_t* data, size_t blobSize, size_t trackOffset) {
    Array outer = readArray(data, blobSize, trackOffset + 0x0C);
    if (outer.count == 0) {
        return false;
    }
    if (outer.count > 1) {
        return true;
    }
    Array inner = readArray(data, blobSize, outer.offset);
    return inner.count > 1;
}

std::optional<float> readFixed16TrackValue(const uint8_t* data, size_t blobSize, size_t trackOffset) {
    auto valueOffset = constantTrackValueOffset(data, blobSize, trackOffset);
    if (!valueOffset) {
        return std::nullopt;
    }
    // fixed16: a 16-bit fixed-point fraction, 0 (0.0) .. 0x7FFF (1.0) --
    // wowdev.wiki M2#Colors_and_transparency's own "0 - transparent,
    // 0x7FFF - opaque" note for M2Color::alpha; M2TextureWeight::weight
    // uses the same scale.
    int16_t raw;
    uint16_t bits = readU16(data, blobSize, *valueOffset);
    std::memcpy(&raw, &bits, sizeof(raw));
    return std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);
}

std::optional<Vec3> readVec3TrackValue(const uint8_t* data, size_t blobSize, size_t trackOffset) {
    auto valueOffset = constantTrackValueOffset(data, blobSize, trackOffset);
    if (!valueOffset) {
        return std::nullopt;
    }
    return readVec3(data, blobSize, *valueOffset);
}

// Same shape as readVec3TrackValue, but for an M2Track<C4Quaternion> --
// M2TextureTransform::rotation's own type (wowdev.wiki Common_Types
// C4Quaternion: 4 raw floats x/y/z/w, *not* the compressed M2CompQuat
// bones use -- see readCompQuat below for that unrelated format).
std::optional<Quat> readQuatFloatTrackValue(const uint8_t* data, size_t blobSize, size_t trackOffset) {
    auto valueOffset = constantTrackValueOffset(data, blobSize, trackOffset);
    if (!valueOffset) {
        return std::nullopt;
    }
    Quat q;
    q.x = readF32(data, blobSize, *valueOffset + 0);
    q.y = readF32(data, blobSize, *valueOffset + 4);
    q.z = readF32(data, blobSize, *valueOffset + 8);
    q.w = readF32(data, blobSize, *valueOffset + 12);
    return q;
}

// Unpacks one M2CompQuat (4x int16, x/y/z/w order) into a Quat, per
// wowdev.wiki "Quaternion values and 2.x" -- fetched 2026-07-25, since the
// main M2 page only links to this formula rather than inlining it:
//   float(v < 0 ? v + 32768 : v - 32767) / 32767.0
// applied independently to each component. The identity quaternion
// (0,0,0,1) is wire value (32767,32767,32767,65535) -- 65535 read as a
// signed int16 is -1, and (-1 + 32768)/32767 == 1.0, matching the wiki's
// own worked example exactly (checked in tests/test_m2.cpp).
Quat readCompQuat(const uint8_t* data, size_t blobSize, size_t off) {
    auto decode = [](int16_t raw) {
        int v = raw < 0 ? raw + 32768 : raw - 32767;
        return static_cast<float>(v) / 32767.0f;
    };
    auto readI16 = [&](size_t o) {
        uint16_t bits = readU16(data, blobSize, o);
        int16_t v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    };
    Quat q;
    q.x = decode(readI16(off + 0));
    q.y = decode(readI16(off + 2));
    q.z = decode(readI16(off + 4));
    q.w = decode(readI16(off + 6));
    return q;
}

TrackMeta readTrackMeta(const std::vector<uint8_t>& blob, uint32_t trackOffset) {
    TrackMeta meta;
    meta.interpolationType = readU16(blob.data(), blob.size(), trackOffset + 0x00);
    meta.globalSequence = readU16(blob.data(), blob.size(), trackOffset + 0x02);
    return meta;
}

// Resolves one M2Track<T>'s keyframe sub-array for a specific sequence
// index -- the general case constantTrackValueOffset (above) deliberately
// refuses to handle. Returns the (outer-array-relative) timestamps/values
// inner-array descriptors, or nullopt if `sequenceIndex` is out of range
// for either of the track's own outer arrays (never an error -- a sequence
// with no inline data for this track is the ordinary case for anything
// husk doesn't have the .anim keyframes for, see Sequence::flags).
std::optional<std::pair<Array, Array>> trackSequenceInnerArrays(const uint8_t* data,
                                                                  size_t blobSize,
                                                                  size_t trackOffset,
                                                                  uint32_t sequenceIndex) {
    Array timestampsOuter = readArray(data, blobSize, trackOffset + 0x04);
    Array valuesOuter = readArray(data, blobSize, trackOffset + 0x0C);
    if (sequenceIndex >= timestampsOuter.count || sequenceIndex >= valuesOuter.count) {
        return std::nullopt;
    }
    Array timestampsInner =
        readArray(data, blobSize, timestampsOuter.offset + static_cast<size_t>(sequenceIndex) * 8);
    Array valuesInner =
        readArray(data, blobSize, valuesOuter.offset + static_cast<size_t>(sequenceIndex) * 8);
    return std::make_pair(timestampsInner, valuesInner);
}

std::vector<Color> parseColors(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Color> colors;
    if (array.count == 0) {
        return colors;
    }

    // M2Color: M2Track<C3Vector> color (0x14) + M2Track<fixed16> alpha
    // (0x14), 0x28 = 40 bytes total.
    constexpr size_t kColorSize = 0x28;
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kColorSize) {
        throw ParseError("colors array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kColorSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    colors.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kColorSize;
        Color c;
        c.color = readVec3TrackValue(data, blobSize, off + 0x00);
        c.alpha = readFixed16TrackValue(data, blobSize, off + 0x14);
        c.colorAnimated = !c.color && trackHasAnimatedData(data, blobSize, off + 0x00);
        c.alphaAnimated = !c.alpha && trackHasAnimatedData(data, blobSize, off + 0x14);
        colors.push_back(c);
    }

    return colors;
}

std::vector<TextureWeight> parseTextureWeights(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<TextureWeight> weights;
    if (array.count == 0) {
        return weights;
    }

    constexpr size_t kWeightSize = 0x14;  // M2Track<fixed16>, 20 bytes
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kWeightSize) {
        throw ParseError("textureWeights array claims " + std::to_string(array.count) +
                          " records (" + std::to_string(kWeightSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    weights.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kWeightSize;
        TextureWeight w;
        w.weight = readFixed16TrackValue(data, blobSize, off + 0x00);
        w.weightAnimated = !w.weight && trackHasAnimatedData(data, blobSize, off + 0x00);
        weights.push_back(w);
    }

    return weights;
}

std::vector<TextureTransform> parseTextureTransforms(const std::vector<uint8_t>& blob,
                                                       const Array& array) {
    std::vector<TextureTransform> transforms;
    if (array.count == 0) {
        return transforms;
    }

    // M2TextureTransform: M2Track<C3Vector> translation (0x14) +
    // M2Track<C4Quaternion> rotation (0x14) + M2Track<C3Vector> scaling
    // (0x14), 0x3C = 60 bytes total (wowdev.wiki M2#Texture_Transforms).
    constexpr size_t kTransformSize = 0x3C;
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kTransformSize) {
        throw ParseError("textureTransforms array claims " + std::to_string(array.count) +
                          " records (" + std::to_string(kTransformSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    transforms.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kTransformSize;
        TextureTransform t;
        t.translation = readVec3TrackValue(data, blobSize, off + 0x00);
        t.rotation = readQuatFloatTrackValue(data, blobSize, off + 0x14);
        t.scaling = readVec3TrackValue(data, blobSize, off + 0x28);
        t.translationAnimated = !t.translation && trackHasAnimatedData(data, blobSize, off + 0x00);
        t.rotationAnimated = !t.rotation && trackHasAnimatedData(data, blobSize, off + 0x14);
        t.scalingAnimated = !t.scaling && trackHasAnimatedData(data, blobSize, off + 0x28);
        transforms.push_back(t);
    }

    return transforms;
}

std::vector<Sequence> parseSequences(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Sequence> sequences;
    if (array.count == 0) {
        return sequences;
    }

    // M2Sequence, wowdev.wiki M2#Animation_sequences -- 0x40 (64) bytes,
    // see Sequence's doc comment in m2.hpp for why (an easy-to-miss 28-byte
    // M2Bounds field, real-data-verified).
    constexpr size_t kSequenceSize = 0x40;
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kSequenceSize) {
        throw ParseError("sequences array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kSequenceSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    sequences.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kSequenceSize;
        Sequence s;
        s.id = readU16(data, blobSize, off + 0x00);
        s.variationIndex = readU16(data, blobSize, off + 0x02);
        s.duration = readU32(data, blobSize, off + 0x04);
        s.flags = readU32(data, blobSize, off + 0x0C);
        sequences.push_back(s);
    }

    return sequences;
}

namespace {

// Shared bounds-checked "read n fixed-size records at a foreign-data
// offset" guard -- same up-front-validate-before-reserve discipline as
// every other parse* function in this file (FAILURES.md #2), factored out
// here since resolveVec3TrackSequence/resolveQuatTrackSequence both need it
// for data that isn't a top-level M2Array (it's an inner M2Track array
// instead), which is otherwise the one shape parseVertices/parseBones/etc.
// don't already cover.
void checkInnerArrayFits(const Array& inner, size_t elementSize, size_t blobSize,
                          const char* what) {
    if (inner.offset > blobSize || inner.count > (blobSize - inner.offset) / elementSize) {
        throw ParseError(std::string("track claims ") + std::to_string(inner.count) + " " + what +
                          " (" + std::to_string(elementSize) + " bytes each) at offset " +
                          std::to_string(inner.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
}

}  // namespace

std::vector<std::pair<uint32_t, Vec3>> resolveVec3TrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    // A global-sequence track loops continuously, independent of any
    // M2Sequence -- resolving it by `sequenceIndex` at all would silently
    // attribute its data to whichever M2Sequence happens to occupy the
    // track's outer-array position `sequenceIndex` (see TrackMeta's doc
    // comment). husk doesn't have global_sequences durations to build a
    // real independent clip yet, so this returns empty rather than guessing
    // -- strictly more correct than the old behavior, even though it means
    // no animation comes out for this track today.
    if (meta.globalSequence != TrackMeta::kNoGlobalSequence) {
        return {};
    }
    // interpolation_type 2/3 (cubic bezier/hermite) is only valid for
    // M2SplineKey<T> tracks per wowdev.wiki -- bone translation/scale,
    // M2Color, and M2TextureWeight are all plain M2Track<T>, so a real file
    // reporting 2/3 here means either a version this parser doesn't
    // understand or a corrupted read, not a case husk can silently keep
    // reading at the wrong (3x) stride (see TrackMeta's doc comment).
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, sequenceIndex);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 12, dataSize, "C3Vector values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, Vec3>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        Vec3 v = readVec3(data, dataSize, valuesInner.offset + i * 12);
        out.emplace_back(ts, v);
    }
    return out;
}

std::vector<std::pair<uint32_t, Quat>> resolveQuatTrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    // See resolveVec3TrackSequence's identical checks for why these two
    // conditions are handled before touching the timestamps/values arrays
    // at all.
    if (meta.globalSequence != TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, sequenceIndex);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 8, dataSize, "M2CompQuat values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, Quat>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        Quat q = readCompQuat(data, dataSize, valuesInner.offset + i * 8);
        out.emplace_back(ts, q);
    }
    return out;
}

std::vector<std::pair<uint32_t, Vec3>> resolveVec3GlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence == TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    // A global-sequence track's outer M2Array<M2Array<T>> holds exactly one
    // sub-array (see this function's doc comment) -- index 0 is always the
    // right (and only) one to resolve, the same way trackSequenceInnerArrays
    // already resolves a specific M2Sequence's sub-array by index.
    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, /*sequenceIndex=*/0);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 12, dataSize, "C3Vector values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, Vec3>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        Vec3 v = readVec3(data, dataSize, valuesInner.offset + i * 12);
        out.emplace_back(ts, v);
    }
    return out;
}

std::vector<std::pair<uint32_t, Quat>> resolveQuatGlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence == TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, /*sequenceIndex=*/0);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 8, dataSize, "M2CompQuat values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, Quat>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        Quat q = readCompQuat(data, dataSize, valuesInner.offset + i * 8);
        out.emplace_back(ts, q);
    }
    return out;
}

std::vector<std::pair<uint32_t, float>> resolveFloatTrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence != TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, sequenceIndex);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 4, dataSize, "float values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, float>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        float v = readF32(data, dataSize, valuesInner.offset + i * 4);
        out.emplace_back(ts, v);
    }
    return out;
}

std::vector<std::pair<uint32_t, float>> resolveFloatGlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence == TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, /*sequenceIndex=*/0);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 4, dataSize, "float values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, float>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        float v = readF32(data, dataSize, valuesInner.offset + i * 4);
        out.emplace_back(ts, v);
    }
    return out;
}

namespace {
uint32_t readRawIntElement(const uint8_t* data, size_t dataSize, size_t off, size_t elementSize) {
    if (elementSize == 1) {
        return readU8(data, dataSize, off);
    }
    if (elementSize == 2) {
        return readU16(data, dataSize, off);
    }
    throw ParseError("resolveRawIntTrackSequence: unsupported element size " +
                      std::to_string(elementSize) + " (only 1 or 2 are valid)");
}
}  // namespace

std::vector<std::pair<uint32_t, uint32_t>> resolveRawIntTrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    size_t elementSize, const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence != TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, sequenceIndex);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, elementSize, dataSize, "raw int values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, uint32_t>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        uint32_t v = readRawIntElement(data, dataSize, valuesInner.offset + i * elementSize, elementSize);
        out.emplace_back(ts, v);
    }
    return out;
}

std::vector<std::pair<uint32_t, uint32_t>> resolveRawIntGlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t elementSize,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence == TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, /*sequenceIndex=*/0);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, elementSize, dataSize, "raw int values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, uint32_t>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        uint32_t v = readRawIntElement(data, dataSize, valuesInner.offset + i * elementSize, elementSize);
        out.emplace_back(ts, v);
    }
    return out;
}

namespace {
void checkFBlockArrayFits(const Array& a, size_t elementSize, size_t blobSize, const char* what) {
    if (a.offset > blobSize || a.count > (blobSize - a.offset) / elementSize) {
        throw ParseError(std::string("FBlock claims ") + std::to_string(a.count) + " " + what + " (" +
                          std::to_string(elementSize) + " bytes each) at offset " +
                          std::to_string(a.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
}
}  // namespace

FBlockMeta readFBlockMeta(const std::vector<uint8_t>& blob, uint32_t blockOffset) {
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();
    FBlockMeta meta;
    meta.timestamps = readArray(data, blobSize, blockOffset + 0x00);
    meta.keys = readArray(data, blobSize, blockOffset + 0x08);
    return meta;
}

namespace {
// Shared "FBlock timestamps (uint16_t each) + N-byte keys" walk -- every
// resolveFBlockXxx below differs only in the key element size and decode,
// same elementSize-as-parameter style resolveRawIntTrackSequence uses
// instead of a template.
template <typename T, typename Decode>
std::vector<std::pair<uint16_t, T>> resolveFBlockGeneric(const std::vector<uint8_t>& blob,
                                                           uint32_t blockOffset, size_t keyElementSize,
                                                           Decode decode) {
    FBlockMeta meta = readFBlockMeta(blob, blockOffset);
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();
    checkFBlockArrayFits(meta.timestamps, 2, blobSize, "timestamps");
    checkFBlockArrayFits(meta.keys, keyElementSize, blobSize, "keys");
    size_t n = std::min<size_t>(meta.timestamps.count, meta.keys.count);

    std::vector<std::pair<uint16_t, T>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint16_t ts = readU16(data, blobSize, meta.timestamps.offset + i * 2);
        T v = decode(data, blobSize, meta.keys.offset + i * keyElementSize);
        out.emplace_back(ts, v);
    }
    return out;
}
}  // namespace

std::vector<std::pair<uint16_t, Vec3>> resolveFBlockVec3(const std::vector<uint8_t>& blob,
                                                           uint32_t blockOffset) {
    return resolveFBlockGeneric<Vec3>(blob, blockOffset, 12,
                                       [](const uint8_t* d, size_t s, size_t o) { return readVec3(d, s, o); });
}

std::vector<std::pair<uint16_t, Vec2>> resolveFBlockVec2(const std::vector<uint8_t>& blob,
                                                           uint32_t blockOffset) {
    return resolveFBlockGeneric<Vec2>(blob, blockOffset, 8, [](const uint8_t* d, size_t s, size_t o) {
        Vec2 v;
        v.x = readF32(d, s, o);
        v.y = readF32(d, s, o + 4);
        return v;
    });
}

std::vector<std::pair<uint16_t, float>> resolveFBlockFixed16(const std::vector<uint8_t>& blob,
                                                               uint32_t blockOffset) {
    return resolveFBlockGeneric<float>(blob, blockOffset, 2, [](const uint8_t* d, size_t s, size_t o) {
        int16_t raw;
        uint16_t bits = readU16(d, s, o);
        std::memcpy(&raw, &bits, sizeof(raw));
        return std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);
    });
}

std::vector<std::pair<uint16_t, uint16_t>> resolveFBlockUint16(const std::vector<uint8_t>& blob,
                                                                 uint32_t blockOffset) {
    return resolveFBlockGeneric<uint16_t>(blob, blockOffset, 2,
                                           [](const uint8_t* d, size_t s, size_t o) { return readU16(d, s, o); });
}

std::vector<uint32_t> parseGlobalLoops(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<uint32_t> loops;
    if (array.count == 0) {
        return loops;
    }

    constexpr size_t kLoopSize = 4;  // M2Loop: a bare uint32_t timestamp
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kLoopSize) {
        throw ParseError("globalLoops array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kLoopSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    loops.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        loops.push_back(readU32(data, blobSize, array.offset + static_cast<size_t>(i) * kLoopSize));
    }

    return loops;
}

std::vector<uint8_t> extractAnimBlob(const std::vector<uint8_t>& animFileBytes, bool chunked) {
    if (!chunked) {
        return animFileBytes;
    }
    auto chunks = readChunks(animFileBytes.data(), animFileBytes.size());
    auto afm2 = findChunk(chunks, "AFM2");
    if (!afm2) {
        std::string found;
        for (const auto& c : chunks) {
            if (!found.empty()) found += ", ";
            found += c.tag;
        }
        throw ParseError("chunked .anim file has no AFM2 chunk; chunks found: [" + found + "]");
    }
    return std::vector<uint8_t>(afm2->data, afm2->data + afm2->size);
}

std::vector<Attachment> parseAttachments(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Attachment> attachments;
    if (array.count == 0) {
        return attachments;
    }

    constexpr size_t kAttachmentSize = 0x28;  // M2Attachment, wowdev.wiki M2#Attachments
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kAttachmentSize) {
        throw ParseError("attachments array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kAttachmentSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    attachments.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kAttachmentSize;
        Attachment a;
        a.id = readU32(data, blobSize, off + 0x00);
        uint16_t boneBits = readU16(data, blobSize, off + 0x04);
        std::memcpy(&a.bone, &boneBits, sizeof(a.bone));
        a.position = readVec3(data, blobSize, off + 0x08);
        attachments.push_back(a);
    }

    return attachments;
}

std::vector<Event> parseEvents(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Event> events;
    if (array.count == 0) {
        return events;
    }

    constexpr size_t kEventSize = 0x24;  // M2Event, wowdev.wiki M2#Events
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kEventSize) {
        throw ParseError("events array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kEventSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    events.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kEventSize;
        Event e;
        // identifier is 4 raw ASCII bytes, file order (not reversed, same
        // as chunk tags -- see chunk.hpp), trimmed of a trailing NUL the
        // same way readName handles the `name` field.
        char idBytes[5] = {};
        std::memcpy(idBytes, data + off + 0x00, 4);
        e.identifier = std::string(idBytes, 4);
        while (!e.identifier.empty() && e.identifier.back() == '\0') e.identifier.pop_back();
        e.data = readU32(data, blobSize, off + 0x04);
        e.bone = readU32(data, blobSize, off + 0x08);
        e.position = readVec3(data, blobSize, off + 0x0C);
        events.push_back(e);
    }

    return events;
}

std::vector<Light> parseLights(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Light> lights;
    if (array.count == 0) {
        return lights;
    }

    constexpr size_t kLightSize = 0x9C;  // M2Light, wowdev.wiki M2#Lights
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kLightSize) {
        throw ParseError("lights array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kLightSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    lights.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kLightSize;
        Light l;
        l.type = readU16(data, blobSize, off + 0x00);
        uint16_t boneBits = readU16(data, blobSize, off + 0x02);
        std::memcpy(&l.bone, &boneBits, sizeof(l.bone));
        l.position = readVec3(data, blobSize, off + 0x04);
        lights.push_back(l);
    }

    return lights;
}

std::vector<Ribbon> parseRibbons(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Ribbon> ribbons;
    if (array.count == 0) {
        return ribbons;
    }

    // M2Ribbon, >= Wrath shape (wowdev.wiki M2#Ribbon_emitters -- see
    // Ribbon's doc comment in m2.hpp for the full offset derivation, and
    // WIKI_FINDINGS.md for the real-file cross-check that confirmed it).
    constexpr size_t kRibbonSize = 0xB0;
    constexpr size_t kRibbonIdOffset = 0x00;
    constexpr size_t kBoneIndexOffset = 0x04;
    constexpr size_t kPositionOffset = 0x08;
    constexpr size_t kTextureIndicesOffset = 0x14;
    constexpr size_t kMaterialIndicesOffset = 0x1C;
    constexpr size_t kColorTrackOffset = 0x24;
    constexpr size_t kAlphaTrackOffset = 0x38;
    constexpr size_t kHeightAboveTrackOffset = 0x4C;
    constexpr size_t kHeightBelowTrackOffset = 0x60;
    constexpr size_t kEdgesPerSecondOffset = 0x74;
    constexpr size_t kEdgeLifetimeOffset = 0x78;
    constexpr size_t kGravityOffset = 0x7C;
    constexpr size_t kTextureRowsOffset = 0x80;
    constexpr size_t kTextureColsOffset = 0x82;
    constexpr size_t kTexSlotTrackOffset = 0x84;
    constexpr size_t kVisibilityTrackOffset = 0x98;
    constexpr size_t kPriorityPlaneOffset = 0xAC;
    constexpr size_t kRibbonColorIndexOffset = 0xAE;
    constexpr size_t kTextureTransformLookupIndexOffset = 0xAF;

    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kRibbonSize) {
        throw ParseError("ribbonEmitters array claims " + std::to_string(array.count) +
                          " records (" + std::to_string(kRibbonSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    ribbons.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kRibbonSize;
        Ribbon r;
        r.ribbonId = readU32(data, blobSize, off + kRibbonIdOffset);
        r.boneIndex = readU32(data, blobSize, off + kBoneIndexOffset);
        r.position = readVec3(data, blobSize, off + kPositionOffset);
        r.textureIndices =
            parseUint16Array(blob, readArray(data, blobSize, off + kTextureIndicesOffset));
        r.materialIndices =
            parseUint16Array(blob, readArray(data, blobSize, off + kMaterialIndicesOffset));
        r.colorTrackOffset = static_cast<uint32_t>(off + kColorTrackOffset);
        r.alphaTrackOffset = static_cast<uint32_t>(off + kAlphaTrackOffset);
        r.heightAboveTrackOffset = static_cast<uint32_t>(off + kHeightAboveTrackOffset);
        r.heightBelowTrackOffset = static_cast<uint32_t>(off + kHeightBelowTrackOffset);
        r.edgesPerSecond = readF32(data, blobSize, off + kEdgesPerSecondOffset);
        r.edgeLifetime = readF32(data, blobSize, off + kEdgeLifetimeOffset);
        r.gravity = readF32(data, blobSize, off + kGravityOffset);
        r.textureRows = readU16(data, blobSize, off + kTextureRowsOffset);
        r.textureCols = readU16(data, blobSize, off + kTextureColsOffset);
        r.texSlotTrackOffset = static_cast<uint32_t>(off + kTexSlotTrackOffset);
        r.visibilityTrackOffset = static_cast<uint32_t>(off + kVisibilityTrackOffset);
        r.priorityPlane = static_cast<int16_t>(readU16(data, blobSize, off + kPriorityPlaneOffset));
        r.ribbonColorIndex = static_cast<int8_t>(readU8(data, blobSize, off + kRibbonColorIndexOffset));
        r.textureTransformLookupIndex =
            static_cast<int8_t>(readU8(data, blobSize, off + kTextureTransformLookupIndexOffset));
        ribbons.push_back(r);
    }

    return ribbons;
}

std::vector<ParticleEmitter> parseParticles(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<ParticleEmitter> particles;
    if (array.count == 0) {
        return particles;
    }

    // M2Particle, Cata+ shape (wowdev.wiki M2#Particle_emitters -- see
    // ParticleEmitter's doc comment in m2.hpp for the full offset
    // derivation and its real-file cross-check).
    constexpr size_t kParticleSize = 0x1EC;
    constexpr size_t kParticleIdOffset = 0x00;
    constexpr size_t kFlagsOffset = 0x04;
    constexpr size_t kPositionOffset = 0x08;
    constexpr size_t kBoneIdOffset = 0x14;
    constexpr size_t kTextureIdOffset = 0x16;
    constexpr size_t kParticleModelFilenameOffset = 0x18;
    constexpr size_t kChildEmittersModelFilenameOffset = 0x20;
    constexpr size_t kBlendingTypeOffset = 0x28;
    constexpr size_t kEmitterTypeOffset = 0x29;
    constexpr size_t kParticleColorIndexOffset = 0x2A;
    constexpr size_t kMultiTexScaleOffset = 0x2C;
    constexpr size_t kPriorityPlaneOffset = 0x2E;
    constexpr size_t kRowsOffset = 0x30;
    constexpr size_t kColumnsOffset = 0x32;
    constexpr size_t kEmissionSpeedTrackOffset = 0x34;
    constexpr size_t kSpeedVariationTrackOffset = 0x48;
    constexpr size_t kVerticalRangeTrackOffset = 0x5C;
    constexpr size_t kHorizontalRangeTrackOffset = 0x70;
    constexpr size_t kGravityTrackOffset = 0x84;
    constexpr size_t kLifespanTrackOffset = 0x98;
    constexpr size_t kLifespanVariationOffset = 0xAC;
    constexpr size_t kEmissionRateTrackOffset = 0xB0;
    constexpr size_t kEmissionRateVariationOffset = 0xC4;
    constexpr size_t kEmissionAreaWidthTrackOffset = 0xC8;
    constexpr size_t kEmissionAreaLengthTrackOffset = 0xDC;
    constexpr size_t kZSourceTrackOffset = 0xF0;
    constexpr size_t kColorTrackBlockOffset = 0x104;
    constexpr size_t kAlphaTrackBlockOffset = 0x114;
    constexpr size_t kScaleTrackBlockOffset = 0x124;
    constexpr size_t kScaleVaryOffset = 0x134;
    constexpr size_t kHeadUVAnimBlockOffset = 0x13C;
    constexpr size_t kTailUVAnimBlockOffset = 0x14C;
    constexpr size_t kTailLengthOffset = 0x15C;
    constexpr size_t kTwinkleSpeedOffset = 0x160;
    constexpr size_t kTwinklePercentOffset = 0x164;
    constexpr size_t kTwinkleScaleOffset = 0x168;
    constexpr size_t kInheritVelocityScaleOffset = 0x170;
    constexpr size_t kDragOffset = 0x174;
    constexpr size_t kBaseSpinOffset = 0x178;
    constexpr size_t kBaseSpinVariationOffset = 0x17C;
    constexpr size_t kSpinSpeedOffset = 0x180;
    constexpr size_t kSpinSpeedVariationOffset = 0x184;
    constexpr size_t kTumbleOffset = 0x188;
    constexpr size_t kWindVectorOffset = 0x1A0;
    constexpr size_t kWindTimeOffset = 0x1AC;
    constexpr size_t kFollowSpeed1Offset = 0x1B0;
    constexpr size_t kFollowScale1Offset = 0x1B4;
    constexpr size_t kFollowSpeed2Offset = 0x1B8;
    constexpr size_t kFollowScale2Offset = 0x1BC;
    constexpr size_t kSplinePointsOffset = 0x1C0;
    constexpr size_t kEnabledInTrackOffset = 0x1C8;
    constexpr size_t kMultiTexScrollMidOffset = 0x1DC;
    constexpr size_t kMultiTexScrollRangeOffset = 0x1E4;

    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kParticleSize) {
        throw ParseError("particleEmitters array claims " + std::to_string(array.count) +
                          " records (" + std::to_string(kParticleSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    particles.reserve(array.count);

    // fixed_point<int8_t,2,5>: 2 integer bits + 5 fraction bits -> raw/32.
    auto readFixed8_2_5 = [&](size_t o) {
        int8_t raw;
        uint8_t bits = readU8(data, blobSize, o);
        std::memcpy(&raw, &bits, sizeof(raw));
        return static_cast<float>(raw) / 32.0f;
    };
    // fixed_point<uint16_t,6,9>: 6 integer bits + 9 fraction bits -> raw/512.
    auto readFixed16_6_9 = [&](size_t o) {
        return static_cast<float>(readU16(data, blobSize, o)) / 512.0f;
    };

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kParticleSize;
        ParticleEmitter p;
        p.particleId = readU32(data, blobSize, off + kParticleIdOffset);
        p.flags = readU32(data, blobSize, off + kFlagsOffset);
        p.position = readVec3(data, blobSize, off + kPositionOffset);
        p.boneId = readU16(data, blobSize, off + kBoneIdOffset);
        p.textureId = readU16(data, blobSize, off + kTextureIdOffset);
        p.particleModelFilename =
            readName(data, blobSize, readArray(data, blobSize, off + kParticleModelFilenameOffset));
        p.childEmittersModelFilename = readName(
            data, blobSize, readArray(data, blobSize, off + kChildEmittersModelFilenameOffset));
        p.blendingType = readU8(data, blobSize, off + kBlendingTypeOffset);
        p.emitterType = readU8(data, blobSize, off + kEmitterTypeOffset);
        p.particleColorIndex = readU16(data, blobSize, off + kParticleColorIndexOffset);
        p.multiTexScale[0] = readFixed8_2_5(off + kMultiTexScaleOffset);
        p.multiTexScale[1] = readFixed8_2_5(off + kMultiTexScaleOffset + 1);
        p.priorityPlane = static_cast<int16_t>(readU16(data, blobSize, off + kPriorityPlaneOffset));
        p.rows = readU16(data, blobSize, off + kRowsOffset);
        p.columns = readU16(data, blobSize, off + kColumnsOffset);
        p.emissionSpeedTrackOffset = static_cast<uint32_t>(off + kEmissionSpeedTrackOffset);
        p.speedVariationTrackOffset = static_cast<uint32_t>(off + kSpeedVariationTrackOffset);
        p.verticalRangeTrackOffset = static_cast<uint32_t>(off + kVerticalRangeTrackOffset);
        p.horizontalRangeTrackOffset = static_cast<uint32_t>(off + kHorizontalRangeTrackOffset);
        p.gravityTrackOffset = static_cast<uint32_t>(off + kGravityTrackOffset);
        p.lifespanTrackOffset = static_cast<uint32_t>(off + kLifespanTrackOffset);
        p.lifespanVariation = readF32(data, blobSize, off + kLifespanVariationOffset);
        p.emissionRateTrackOffset = static_cast<uint32_t>(off + kEmissionRateTrackOffset);
        p.emissionRateVariation = readF32(data, blobSize, off + kEmissionRateVariationOffset);
        p.emissionAreaWidthTrackOffset = static_cast<uint32_t>(off + kEmissionAreaWidthTrackOffset);
        p.emissionAreaLengthTrackOffset = static_cast<uint32_t>(off + kEmissionAreaLengthTrackOffset);
        p.zSourceTrackOffset = static_cast<uint32_t>(off + kZSourceTrackOffset);
        p.colorTrackBlockOffset = static_cast<uint32_t>(off + kColorTrackBlockOffset);
        p.alphaTrackBlockOffset = static_cast<uint32_t>(off + kAlphaTrackBlockOffset);
        p.scaleTrackBlockOffset = static_cast<uint32_t>(off + kScaleTrackBlockOffset);
        p.scaleVary.x = readF32(data, blobSize, off + kScaleVaryOffset);
        p.scaleVary.y = readF32(data, blobSize, off + kScaleVaryOffset + 4);
        p.headUVAnimBlockOffset = static_cast<uint32_t>(off + kHeadUVAnimBlockOffset);
        p.tailUVAnimBlockOffset = static_cast<uint32_t>(off + kTailUVAnimBlockOffset);
        p.tailLength = readF32(data, blobSize, off + kTailLengthOffset);
        p.twinkleSpeed = readF32(data, blobSize, off + kTwinkleSpeedOffset);
        p.twinklePercent = readF32(data, blobSize, off + kTwinklePercentOffset);
        p.twinkleScaleMin = readF32(data, blobSize, off + kTwinkleScaleOffset);
        p.twinkleScaleMax = readF32(data, blobSize, off + kTwinkleScaleOffset + 4);
        p.inheritVelocityScale = readF32(data, blobSize, off + kInheritVelocityScaleOffset);
        p.drag = readF32(data, blobSize, off + kDragOffset);
        p.baseSpin = readF32(data, blobSize, off + kBaseSpinOffset);
        p.baseSpinVariation = readF32(data, blobSize, off + kBaseSpinVariationOffset);
        p.spinSpeed = readF32(data, blobSize, off + kSpinSpeedOffset);
        p.spinSpeedVariation = readF32(data, blobSize, off + kSpinSpeedVariationOffset);
        p.tumbleMin = readVec3(data, blobSize, off + kTumbleOffset);
        p.tumbleMax = readVec3(data, blobSize, off + kTumbleOffset + 12);
        p.windVector = readVec3(data, blobSize, off + kWindVectorOffset);
        p.windTime = readF32(data, blobSize, off + kWindTimeOffset);
        p.followSpeed1 = readF32(data, blobSize, off + kFollowSpeed1Offset);
        p.followScale1 = readF32(data, blobSize, off + kFollowScale1Offset);
        p.followSpeed2 = readF32(data, blobSize, off + kFollowSpeed2Offset);
        p.followScale2 = readF32(data, blobSize, off + kFollowScale2Offset);
        p.splinePoints =
            parseVec3Array(blob, readArray(data, blobSize, off + kSplinePointsOffset));
        p.enabledInTrackOffset = static_cast<uint32_t>(off + kEnabledInTrackOffset);
        p.multiTexScrollMid[0] = readFixed16_6_9(off + kMultiTexScrollMidOffset + 0);
        p.multiTexScrollMid[1] = readFixed16_6_9(off + kMultiTexScrollMidOffset + 2);
        p.multiTexScrollMid[2] = readFixed16_6_9(off + kMultiTexScrollMidOffset + 4);
        p.multiTexScrollMid[3] = readFixed16_6_9(off + kMultiTexScrollMidOffset + 6);
        p.multiTexScrollRange[0] = readFixed16_6_9(off + kMultiTexScrollRangeOffset + 0);
        p.multiTexScrollRange[1] = readFixed16_6_9(off + kMultiTexScrollRangeOffset + 2);
        p.multiTexScrollRange[2] = readFixed16_6_9(off + kMultiTexScrollRangeOffset + 4);
        p.multiTexScrollRange[3] = readFixed16_6_9(off + kMultiTexScrollRangeOffset + 6);
        particles.push_back(p);
    }

    return particles;
}

Header loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw ParseError("couldn't open '" + path + "' for reading");
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw ParseError("error reading '" + path + "'");
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
