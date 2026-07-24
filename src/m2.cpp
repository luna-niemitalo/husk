#include "m2.hpp"

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

// End of the last field this parser reads (collisionSphereRadius, 4 bytes).
// The real header continues further (attachments, events, lights, cameras,
// ribbons, particles, ...) -- this is the minimum a header must be for
// every field above to be safely readable, not the full struct size.
constexpr size_t minHeaderSize = collisionSphereRadius + 4;
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
    return h;
}

struct ResolvedBlob {
    std::vector<uint8_t> bytes;
    bool chunked;
    // Set only for chunked files that carry an SKID chunk (wowdev.wiki
    // M2#SKID) -- see Header::skeletonFileId.
    std::optional<uint32_t> skeletonFileId;
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

    std::optional<uint32_t> skeletonFileId;
    if (auto skid = findChunk(chunks, "SKID")) {
        skeletonFileId = readU32(skid->data, skid->size, 0);
    }

    return {std::vector<uint8_t>(md21->data, md21->data + md21->size), /*chunked=*/true,
            skeletonFileId};
}

}  // namespace

Header parseHeader(const std::vector<uint8_t>& fileBytes) {
    auto resolved = resolveBlob(fileBytes);
    Header h = parseBlob(resolved.bytes.data(), resolved.bytes.size(), resolved.chunked);
    h.skeletonFileId = resolved.skeletonFileId;
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
    vertices.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kVertexSize;
        if (off + kVertexSize > blobSize) {
            throw ParseError("vertex " + std::to_string(i) + " at offset " + std::to_string(off) +
                              " needs " + std::to_string(kVertexSize) +
                              " bytes but the blob is only " + std::to_string(blobSize) +
                              " bytes");
        }

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
    constexpr size_t kPivotOffset = 0x4C;

    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();
    bones.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kBoneSize;
        if (off + kBoneSize > blobSize) {
            throw ParseError("bone " + std::to_string(i) + " at offset " + std::to_string(off) +
                              " needs " + std::to_string(kBoneSize) +
                              " bytes but the blob is only " + std::to_string(blobSize) +
                              " bytes");
        }

        Bone b;
        b.keyBoneId = static_cast<int32_t>(readU32(data, blobSize, off + kKeyBoneIdOffset));
        b.flags = readU32(data, blobSize, off + kFlagsOffset);
        uint16_t parentBoneBits;
        std::memcpy(&parentBoneBits, data + off + kParentBoneOffset, sizeof(parentBoneBits));
        b.parentBone = static_cast<int16_t>(parentBoneBits);
        b.pivot = readVec3(data, blobSize, off + kPivotOffset);
        bones.push_back(b);
    }

    return bones;
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
