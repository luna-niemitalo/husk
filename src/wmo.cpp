#include "wmo.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "chunk.hpp"

namespace husk::wmo {

namespace {

constexpr size_t kMohdSize = 64;
constexpr size_t kMomtSize = 64;
constexpr size_t kMogiSize = 32;
constexpr size_t kModsSize = 32;
constexpr size_t kModdSize = 40;
constexpr size_t kMouvSize = 16;
constexpr size_t kMobaSize = 24;
constexpr size_t kMoltSize = 48;
constexpr size_t kMnldSize = 184;
constexpr size_t kMavdSize = 48;
constexpr size_t kMbvdSize = 128;
constexpr size_t kMfogSize = 48;
constexpr size_t kMfedSize = 16;
constexpr size_t kMoptSize = 20;
constexpr size_t kMoprSize = 8;
constexpr size_t kMpvdEmitterOffset = 0x2D0;
constexpr size_t kMpvdEmitterSize = 844;
constexpr size_t kMpvdEmitterSlots = 4;
constexpr uint32_t kMpvdEnvelopeSlots = 40;
constexpr size_t kMogpHeaderSize = 0x44;
constexpr uint8_t kBatchUsesLargeMaterialId = 0x02;

template <typename T>
T read(const uint8_t* data, size_t size, size_t off, const char* what) {
    if (off > size || size - off < sizeof(T)) {
        throw ParseError(std::string(what) + ": read of " + std::to_string(sizeof(T)) + " bytes at offset " +
                         std::to_string(off) + " runs past end (size " + std::to_string(size) + ")");
    }
    T v;
    std::memcpy(&v, data + off, sizeof(T));
    return v;
}

Vec3 readVec3(const uint8_t* data, size_t size, size_t off, const char* what) {
    return {read<float>(data, size, off, what), read<float>(data, size, off + 4, what),
            read<float>(data, size, off + 8, what)};
}

// IEEE 754 binary16.
float readHalf(const uint8_t* data, size_t size, size_t off, const char* what) {
    auto bits = read<uint16_t>(data, size, off, what);
    int exponent = (bits >> 10) & 0x1F;
    int mantissa = bits & 0x3FF;
    float sign = (bits & 0x8000) ? -1.0f : 1.0f;
    if (exponent == 0) return sign * std::ldexp(static_cast<float>(mantissa), -24);
    if (exponent == 31) return mantissa ? std::nanf("") : sign * INFINITY;
    return sign * std::ldexp(static_cast<float>(mantissa | 0x400), exponent - 25);
}

Box readBox(const uint8_t* data, size_t size, size_t off, const char* what) {
    return {readVec3(data, size, off, what), readVec3(data, size, off + 12, what)};
}

std::vector<Chunk> readChunksReversed(const uint8_t* data, size_t size) {
    std::vector<Chunk> chunks = readChunks(data, size);
    for (Chunk& c : chunks) std::reverse(c.tag.begin(), c.tag.end());
    return chunks;
}

// Number of `recordSize`-byte records in `c`; throws when the size isn't a multiple.
size_t recordCount(const Chunk& c, size_t recordSize) {
    if (c.size % recordSize != 0) {
        throw ParseError(c.tag + ": expected a multiple of " + std::to_string(recordSize) + " bytes, got " +
                         std::to_string(c.size));
    }
    return c.size / recordSize;
}

const Chunk& requireChunk(const std::vector<Chunk>& chunks, const char* tag, const char* file) {
    auto it = std::find_if(chunks.begin(), chunks.end(), [&](const Chunk& c) { return c.tag == tag; });
    if (it == chunks.end()) throw ParseError(std::string(file) + ": no " + tag + " chunk");
    return *it;
}

uint32_t readVersion(const std::vector<Chunk>& chunks, const char* file) {
    const Chunk& mver = requireChunk(chunks, "MVER", file);
    return read<uint32_t>(mver.data, mver.size, 0, "MVER");
}

template <typename T>
std::vector<T> readArray(const Chunk& c) {
    std::vector<T> out(recordCount(c, sizeof(T)));
    if (!out.empty()) std::memcpy(out.data(), c.data, c.size);
    return out;
}

RootHeader parseHeader(const Chunk& c) {
    if (c.size != kMohdSize) {
        throw ParseError("MOHD: expected " + std::to_string(kMohdSize) + " bytes, got " + std::to_string(c.size));
    }
    const uint8_t* d = c.data;
    RootHeader h;
    h.textureCount = read<uint32_t>(d, c.size, 0x00, "MOHD");
    h.groupCount = read<uint32_t>(d, c.size, 0x04, "MOHD");
    h.portalCount = read<uint32_t>(d, c.size, 0x08, "MOHD");
    h.lightCount = read<uint32_t>(d, c.size, 0x0C, "MOHD");
    h.doodadNameCount = read<uint32_t>(d, c.size, 0x10, "MOHD");
    h.doodadDefCount = read<uint32_t>(d, c.size, 0x14, "MOHD");
    h.doodadSetCount = read<uint32_t>(d, c.size, 0x18, "MOHD");
    h.ambientColor = read<uint32_t>(d, c.size, 0x1C, "MOHD");
    h.wmoAreaTableId = read<uint32_t>(d, c.size, 0x20, "MOHD");
    h.bounds = readBox(d, c.size, 0x24, "MOHD");
    h.flags = read<uint16_t>(d, c.size, 0x3C, "MOHD");
    h.lodCount = read<uint16_t>(d, c.size, 0x3E, "MOHD");
    return h;
}

std::vector<Material> parseMaterials(const Chunk& c) {
    std::vector<Material> out(recordCount(c, kMomtSize));
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kMomtSize;
        Material& m = out[i];
        m.flags = read<uint32_t>(c.data, c.size, o + 0x00, "MOMT");
        m.shader = read<uint32_t>(c.data, c.size, o + 0x04, "MOMT");
        m.blendMode = read<uint32_t>(c.data, c.size, o + 0x08, "MOMT");
        m.textures[0] = read<uint32_t>(c.data, c.size, o + 0x0C, "MOMT");
        m.sidnColor = read<uint32_t>(c.data, c.size, o + 0x10, "MOMT");
        m.textures[1] = read<uint32_t>(c.data, c.size, o + 0x18, "MOMT");
        m.diffuseColor = read<uint32_t>(c.data, c.size, o + 0x1C, "MOMT");
        m.groundType = read<uint32_t>(c.data, c.size, o + 0x20, "MOMT");
        m.textures[2] = read<uint32_t>(c.data, c.size, o + 0x24, "MOMT");
        m.color2 = read<uint32_t>(c.data, c.size, o + 0x28, "MOMT");
        m.flags2 = read<uint32_t>(c.data, c.size, o + 0x2C, "MOMT");
    }
    return out;
}

std::vector<GroupInfo> parseGroupInfos(const Chunk& c) {
    std::vector<GroupInfo> out(recordCount(c, kMogiSize));
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kMogiSize;
        out[i].flags = read<uint32_t>(c.data, c.size, o, "MOGI");
        out[i].bounds = readBox(c.data, c.size, o + 0x04, "MOGI");
        out[i].nameOffset = read<int32_t>(c.data, c.size, o + 0x1C, "MOGI");
    }
    return out;
}

std::vector<DoodadSet> parseDoodadSets(const Chunk& c) {
    std::vector<DoodadSet> out(recordCount(c, kModsSize));
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kModsSize;
        const char* name = reinterpret_cast<const char*>(c.data + o);
        out[i].name.assign(name, strnlen(name, 0x14));
        out[i].startIndex = read<uint32_t>(c.data, c.size, o + 0x14, "MODS");
        out[i].count = read<uint32_t>(c.data, c.size, o + 0x18, "MODS");
    }
    return out;
}

std::vector<Doodad> parseDoodads(const Chunk& c) {
    std::vector<Doodad> out(recordCount(c, kModdSize));
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kModdSize;
        Doodad& d = out[i];
        uint32_t nameAndFlags = read<uint32_t>(c.data, c.size, o, "MODD");
        d.nameIndex = nameAndFlags & 0x00FFFFFF;
        d.flags = static_cast<uint8_t>(nameAndFlags >> 24);
        d.position = readVec3(c.data, c.size, o + 0x04, "MODD");
        for (size_t k = 0; k < 4; ++k) d.rotation[k] = read<float>(c.data, c.size, o + 0x10 + 4 * k, "MODD");
        d.scale = read<float>(c.data, c.size, o + 0x20, "MODD");
        d.color = read<uint32_t>(c.data, c.size, o + 0x24, "MODD");
    }
    return out;
}

std::vector<LegacyLight> parseLegacyLights(const Chunk& c) {
    std::vector<LegacyLight> out(recordCount(c, kMoltSize));
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kMoltSize;
        LegacyLight& l = out[i];
        l.type = read<uint8_t>(c.data, c.size, o + 0x00, "MOLT");
        l.useAttenuation = read<uint8_t>(c.data, c.size, o + 0x01, "MOLT");
        l.unknown = {read<uint8_t>(c.data, c.size, o + 0x02, "MOLT"), read<uint8_t>(c.data, c.size, o + 0x03, "MOLT")};
        l.color = read<uint32_t>(c.data, c.size, o + 0x04, "MOLT");
        l.position = readVec3(c.data, c.size, o + 0x08, "MOLT");
        l.intensity = read<float>(c.data, c.size, o + 0x14, "MOLT");
        for (size_t k = 0; k < 4; ++k) l.rotation[k] = read<float>(c.data, c.size, o + 0x18 + 4 * k, "MOLT");
        l.attenuationStart = read<float>(c.data, c.size, o + 0x28, "MOLT");
        l.attenuationEnd = read<float>(c.data, c.size, o + 0x2C, "MOLT");
    }
    return out;
}

std::vector<NewLight> parseNewLights(const Chunk& c) {
    std::vector<NewLight> out(recordCount(c, kMnldSize));
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kMnldSize;
        NewLight& l = out[i];
        l.type = read<int32_t>(c.data, c.size, o + 0x00, "MNLD");
        l.lightIndex = read<int32_t>(c.data, c.size, o + 0x04, "MNLD");
        l.flags = read<int32_t>(c.data, c.size, o + 0x08, "MNLD");
        l.doodadSet = read<int32_t>(c.data, c.size, o + 0x0C, "MNLD");
        l.innerColor = read<uint32_t>(c.data, c.size, o + 0x10, "MNLD");
        l.position = readVec3(c.data, c.size, o + 0x14, "MNLD");
        l.rotation = readVec3(c.data, c.size, o + 0x20, "MNLD");
        l.attenuationStart = read<float>(c.data, c.size, o + 0x2C, "MNLD");
        l.attenuationEnd = read<float>(c.data, c.size, o + 0x30, "MNLD");
        l.intensity = read<float>(c.data, c.size, o + 0x34, "MNLD");
        l.outerColor = read<uint32_t>(c.data, c.size, o + 0x38, "MNLD");
        l.blendStart = read<float>(c.data, c.size, o + 0x3C, "MNLD");
        l.blendEnd = read<float>(c.data, c.size, o + 0x40, "MNLD");
        l.flickerIntensity = read<float>(c.data, c.size, o + 0x48, "MNLD");
        l.flickerSpeed = read<float>(c.data, c.size, o + 0x4C, "MNLD");
        l.flickerMode = read<int32_t>(c.data, c.size, o + 0x50, "MNLD");
        l.cookieFileDataId = read<uint32_t>(c.data, c.size, o + 0x64, "MNLD");
        l.falloff = read<float>(c.data, c.size, o + 0x7C, "MNLD");
        l.innerAngle = read<float>(c.data, c.size, o + 0x80, "MNLD");
        l.outerAngle = read<float>(c.data, c.size, o + 0x84, "MNLD");
        l.scale = readHalf(c.data, c.size, o + 0x88, "MNLD");
        l.intensityMultiplier = readHalf(c.data, c.size, o + 0x8A, "MNLD");
    }
    return out;
}

Liquid parseLiquid(const Chunk& c) {
    constexpr size_t kHeaderSize = 30;
    constexpr size_t kVertexSize = 8;
    auto vertsX = read<int32_t>(c.data, c.size, 0x00, "MLIQ");
    auto vertsY = read<int32_t>(c.data, c.size, 0x04, "MLIQ");
    auto tilesX = read<int32_t>(c.data, c.size, 0x08, "MLIQ");
    auto tilesY = read<int32_t>(c.data, c.size, 0x0C, "MLIQ");
    if (vertsX < 1 || vertsY < 1 || tilesX != vertsX - 1 || tilesY != vertsY - 1) {
        throw ParseError("MLIQ: expected tiles = verts - 1 >= 0 per axis, got verts (" + std::to_string(vertsX) +
                         ", " + std::to_string(vertsY) + ") tiles (" + std::to_string(tilesX) + ", " +
                         std::to_string(tilesY) + ")");
    }
    size_t vertexCount = static_cast<size_t>(vertsX) * static_cast<size_t>(vertsY);
    size_t tileCount = static_cast<size_t>(tilesX) * static_cast<size_t>(tilesY);
    size_t expected = kHeaderSize + vertexCount * kVertexSize + tileCount;
    if (c.size != expected) {
        throw ParseError("MLIQ: expected " + std::to_string(expected) + " bytes for " + std::to_string(vertsX) + "x" +
                         std::to_string(vertsY) + " vertices, got " + std::to_string(c.size));
    }
    Liquid out;
    out.vertsX = static_cast<uint32_t>(vertsX);
    out.vertsY = static_cast<uint32_t>(vertsY);
    out.corner = readVec3(c.data, c.size, 0x10, "MLIQ");
    out.material = read<uint16_t>(c.data, c.size, 0x1C, "MLIQ");
    for (size_t v = 0; v < vertexCount; ++v) {
        size_t o = kHeaderSize + v * kVertexSize;
        out.vertexData.push_back(read<std::array<uint8_t, 4>>(c.data, c.size, o, "MLIQ"));
        out.heights.push_back(read<float>(c.data, c.size, o + 4, "MLIQ"));
    }
    const uint8_t* tiles = c.data + kHeaderSize + vertexCount * kVertexSize;
    out.tiles.assign(tiles, tiles + tileCount);
    return out;
}

std::vector<ParticulateVolume> parseParticulateVolumes(const Chunk& c) {
    std::vector<ParticulateVolume> out;
    size_t count = recordCount(c, kParticulateVolumeSize);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(parseParticulateVolume(c.data, c.size, i * kParticulateVolumeSize,
                                             "MPVD[" + std::to_string(i) + "]"));
    }
    return out;
}

std::vector<AmbientVolume> parseAmbientVolumes(const Chunk& c) {
    std::vector<AmbientVolume> out(recordCount(c, kMavdSize));
    const char* tag = c.tag.c_str();
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kMavdSize;
        AmbientVolume& v = out[i];
        v.position = readVec3(c.data, c.size, o + 0x00, tag);
        v.start = read<float>(c.data, c.size, o + 0x0C, tag);
        v.end = read<float>(c.data, c.size, o + 0x10, tag);
        for (size_t k = 0; k < 3; ++k) v.colors[k] = read<uint32_t>(c.data, c.size, o + 0x14 + 4 * k, tag);
        v.flags = read<uint32_t>(c.data, c.size, o + 0x20, tag);
        v.doodadSet = read<uint16_t>(c.data, c.size, o + 0x24, tag);
    }
    return out;
}

std::vector<AmbientBox> parseAmbientBoxes(const Chunk& c) {
    std::vector<AmbientBox> out(recordCount(c, kMbvdSize));
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kMbvdSize;
        AmbientBox& b = out[i];
        for (size_t k = 0; k < 6; ++k) b.planes[k] = read<std::array<float, 4>>(c.data, c.size, o + 16 * k, "MBVD");
        b.end = read<float>(c.data, c.size, o + 0x60, "MBVD");
        for (size_t k = 0; k < 3; ++k) b.colors[k] = read<uint32_t>(c.data, c.size, o + 0x64 + 4 * k, "MBVD");
        b.flags = read<uint32_t>(c.data, c.size, o + 0x70, "MBVD");
        b.doodadSet = read<uint16_t>(c.data, c.size, o + 0x74, "MBVD");
    }
    return out;
}

std::vector<Fog> parseFogs(const Chunk& c) {
    std::vector<Fog> out(recordCount(c, kMfogSize));
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kMfogSize;
        Fog& f = out[i];
        f.flags = read<uint32_t>(c.data, c.size, o + 0x00, "MFOG");
        f.position = readVec3(c.data, c.size, o + 0x04, "MFOG");
        f.smallerRadius = read<float>(c.data, c.size, o + 0x10, "MFOG");
        f.largerRadius = read<float>(c.data, c.size, o + 0x14, "MFOG");
        for (auto [layer, at] : {std::pair{&f.fog, o + 0x18}, std::pair{&f.underwater, o + 0x24}}) {
            layer->end = read<float>(c.data, c.size, at + 0x0, "MFOG");
            layer->startScalar = read<float>(c.data, c.size, at + 0x4, "MFOG");
            layer->color = read<uint32_t>(c.data, c.size, at + 0x8, "MFOG");
        }
    }
    return out;
}

struct FogDoodadSet {
    uint16_t doodadSet = 0;
    std::optional<std::vector<uint8_t>> reserved;  // set only when not all zero
};

std::vector<FogDoodadSet> parseFogDoodadSets(const Chunk& c) {
    std::vector<FogDoodadSet> out(recordCount(c, kMfedSize));
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kMfedSize;
        out[i].doodadSet = read<uint16_t>(c.data, c.size, o, "MFED");
        std::vector<uint8_t> rest(c.data + o + 2, c.data + o + kMfedSize);
        if (std::any_of(rest.begin(), rest.end(), [](uint8_t b) { return b != 0; })) out[i].reserved = std::move(rest);
    }
    return out;
}

std::vector<Portal> parsePortals(const Chunk& c) {
    std::vector<Portal> out(recordCount(c, kMoptSize));
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kMoptSize;
        out[i].startVertex = read<uint16_t>(c.data, c.size, o + 0x00, "MOPT");
        out[i].count = read<uint16_t>(c.data, c.size, o + 0x02, "MOPT");
        out[i].normal = readVec3(c.data, c.size, o + 0x04, "MOPT");
        out[i].distance = read<float>(c.data, c.size, o + 0x10, "MOPT");
    }
    return out;
}

std::vector<PortalRef> parsePortalRefs(const Chunk& c) {
    std::vector<PortalRef> out(recordCount(c, kMoprSize));
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kMoprSize;
        out[i].portal = read<uint16_t>(c.data, c.size, o + 0x00, "MOPR");
        out[i].group = read<uint16_t>(c.data, c.size, o + 0x02, "MOPR");
        out[i].side = read<int16_t>(c.data, c.size, o + 0x04, "MOPR");
    }
    return out;
}

std::vector<Batch> parseBatches(const Chunk& c) {
    std::vector<Batch> out(recordCount(c, kMobaSize));
    for (size_t i = 0; i < out.size(); ++i) {
        size_t o = i * kMobaSize;
        Batch& b = out[i];
        uint16_t largeMaterial = read<uint16_t>(c.data, c.size, o + 0x0A, "MOBA");
        b.startIndex = read<uint32_t>(c.data, c.size, o + 0x0C, "MOBA");
        b.indexCount = read<uint16_t>(c.data, c.size, o + 0x10, "MOBA");
        b.minVertex = read<uint16_t>(c.data, c.size, o + 0x12, "MOBA");
        b.maxVertex = read<uint16_t>(c.data, c.size, o + 0x14, "MOBA");
        uint8_t flags = read<uint8_t>(c.data, c.size, o + 0x16, "MOBA");
        uint8_t smallMaterial = read<uint8_t>(c.data, c.size, o + 0x17, "MOBA");
        // Real files set the large-id flag and leave material_id 0 (every batch
        // of 9du_torghast_modular_chambercap01_000.wmo), so the flag decides.
        b.material = (flags & kBatchUsesLargeMaterialId) ? largeMaterial : smallMaterial;
    }
    return out;
}

void requireVertexCount(const char* tag, size_t actual, size_t vertexCount) {
    if (actual != vertexCount) {
        throw ParseError(std::string(tag) + ": expected " + std::to_string(vertexCount) +
                         " entries (one per MOVT vertex), got " + std::to_string(actual));
    }
}

void keepIfSet(const uint8_t* data, size_t size, size_t o, size_t begin, size_t end, const char* tag,
               std::vector<UnknownRange>& out) {
    if (o + end > size) {
        throw ParseError(std::string(tag) + ": range [" + std::to_string(begin) + ", " + std::to_string(end) +
                         ") runs past end (size " + std::to_string(size - o) + ")");
    }
    keepIfNonzero(data + o, begin, end, out);
}

}  // namespace

void keepIfNonzero(const uint8_t* record, size_t begin, size_t end, std::vector<UnknownRange>& out) {
    UnknownRange range{static_cast<uint32_t>(begin), {}};
    bool set = false;
    for (size_t at = begin; at < end; at += 4) {
        uint32_t word = 0;
        std::memcpy(&word, record + at, 4);
        range.words.push_back(word);
        set = set || word != 0;
    }
    if (set) out.push_back(std::move(range));
}

ParticulateVolume parseParticulateVolume(const uint8_t* data, size_t size, size_t offset, const std::string& what) {
    if (offset > size || size - offset < kParticulateVolumeSize) {
        throw ParseError(what + ": expected " + std::to_string(kParticulateVolumeSize) + " bytes at offset " +
                         std::to_string(offset) + ", chunk is " + std::to_string(size));
    }
    const char* tag = what.c_str();
    size_t o = offset;
    ParticulateVolume v;
    v.flags = read<uint32_t>(data, size, o + 0x00, tag);
    v.position = readVec3(data, size, o + 0x08, tag);
    v.extent = readVec3(data, size, o + 0x14, tag);
    v.rotation = readVec3(data, size, o + 0x20, tag);
    v.unknown04 = read<uint32_t>(data, size, o + 0x04, tag);
    v.unknown2C = read<float>(data, size, o + 0x2C, tag);
    v.presetId = read<uint32_t>(data, size, o + 0x30, tag);
    v.particleCount = read<uint32_t>(data, size, o + 0x34, tag);
    v.unknown38 = read<uint32_t>(data, size, o + 0x38, tag);
    v.opacity = read<float>(data, size, o + 0x3C, tag);
    v.unknown220 = read<std::array<float, 20>>(data, size, o + 0x220, tag);
    v.unknown29C = read<float>(data, size, o + 0x29C, tag);
    auto keyCount = read<uint32_t>(data, size, o + 0x298, tag);
    if (keyCount > kMpvdEnvelopeSlots) {
        throw ParseError(what + " keyframe count: expected <= " + std::to_string(kMpvdEnvelopeSlots) + ", got " +
                         std::to_string(keyCount));
    }
    for (uint32_t k = 0; k < keyCount; ++k) {
        v.envelope.push_back(read<std::array<float, 3>>(data, size, o + 0x40 + 12 * k, tag));
    }
    keepIfSet(data, size, o, 0x40 + 12 * keyCount, 0x220, tag, v.unknownRanges);
    keepIfSet(data, size, o, 0x270, 0x298, tag, v.unknownRanges);
    keepIfSet(data, size, o, 0x2A0, kMpvdEmitterOffset, tag, v.unknownRanges);
    for (uint32_t slot = 0; slot < kMpvdEmitterSlots; ++slot) {
        size_t start = kMpvdEmitterOffset + slot * kMpvdEmitterSize;
        size_t e = o + start;
        ParticulateEmitter emitter;
        emitter.slot = slot;
        emitter.textureFileDataId = read<uint32_t>(data, size, e + 0x94, tag);
        // Corpus: an entry is all zero exactly when its texture is 0.
        if (emitter.textureFileDataId == 0) {
            keepIfSet(data, size, o, start, start + kMpvdEmitterSize, tag, v.unknownRanges);
            continue;
        }
        emitter.parameters = read<std::array<float, 32>>(data, size, e, tag);
        emitter.color = read<std::array<float, 4>>(data, size, e + 0x80, tag);
        emitter.unknown90 = read<float>(data, size, e + 0x90, tag);
        emitter.blendMode = read<uint32_t>(data, size, e + 0x98, tag);
        emitter.tilesX = read<uint32_t>(data, size, e + 0x9C, tag);
        emitter.tilesY = read<uint32_t>(data, size, e + 0xA0, tag);
        emitter.frames = read<uint32_t>(data, size, e + 0xA4, tag);
        emitter.unknownA8 = read<float>(data, size, e + 0xA8, tag);
        keepIfSet(data, size, o, start + 0xAC, start + kMpvdEmitterSize, tag, v.unknownRanges);
        v.emitters.push_back(emitter);
    }
    keepIfSet(data, size, o, 0x1000, 0x10AC, tag, v.unknownRanges);
    // The record's last dword counts its emitter entries (812/812 real records).
    auto declared = read<uint32_t>(data, size, o + 0x10AC, tag);
    if (declared != v.emitters.size()) {
        throw ParseError(what + " emitter count: expected the +0x10AC count to match the " +
                         std::to_string(v.emitters.size()) + " slots with a texture, got " + std::to_string(declared));
    }
    return v;
}

RootFile parseRoot(const std::vector<uint8_t>& fileBytes) {
    std::vector<Chunk> chunks = readChunksReversed(fileBytes.data(), fileBytes.size());
    RootFile root;
    std::vector<FogDoodadSet> fogDoodadSets;
    root.version = readVersion(chunks, "WMO root");
    root.header = parseHeader(requireChunk(chunks, "MOHD", "WMO root"));
    for (const Chunk& c : chunks) {
        if (c.tag == "MOMT") root.materials = parseMaterials(c);
        else if (c.tag == "MOTX") root.textureNames.assign(reinterpret_cast<const char*>(c.data), c.size);
        else if (c.tag == "MOGN") root.groupNames.assign(reinterpret_cast<const char*>(c.data), c.size);
        else if (c.tag == "MOGI") root.groups = parseGroupInfos(c);
        else if (c.tag == "GFID") root.groupFileDataIds = readArray<uint32_t>(c);
        else if (c.tag == "MODS") root.doodadSets = parseDoodadSets(c);
        else if (c.tag == "MODI") root.doodadFileDataIds = readArray<uint32_t>(c);
        else if (c.tag == "MODN") root.doodadNames.assign(reinterpret_cast<const char*>(c.data), c.size);
        else if (c.tag == "MODD") root.doodads = parseDoodads(c);
        else if (c.tag == "MDDI") root.doodadColorMultipliers = readArray<float>(c);
        else if (c.tag == "MOUV") root.uvScrollSpeeds = readArray<std::array<Vec2, 2>>(c);
        else if (c.tag == "MOLT") root.legacyLights = parseLegacyLights(c);
        else if (c.tag == "MNLD") root.newLights = parseNewLights(c);
        else if (c.tag == "MPVD") root.particulateVolumes = parseParticulateVolumes(c);
        else if (c.tag == "MAVG") root.globalAmbientVolumes = parseAmbientVolumes(c);
        else if (c.tag == "MAVD") root.ambientVolumes = parseAmbientVolumes(c);
        else if (c.tag == "MBVD") root.ambientBoxes = parseAmbientBoxes(c);
        else if (c.tag == "MFOG") root.fogs = parseFogs(c);
        else if (c.tag == "MFED") fogDoodadSets = parseFogDoodadSets(c);
        else if (c.tag == "MOPV") root.portalVertices = readArray<Vec3>(c);
        else if (c.tag == "MOPT") root.portals = parsePortals(c);
        else if (c.tag == "MOPR") root.portalRefs = parsePortalRefs(c);
        else if (c.tag == "MOPE") root.portalExtras = readArray<std::array<uint8_t, 16>>(c);
        else if (c.tag == "MOSI") root.skyboxFileDataId = read<uint32_t>(c.data, c.size, 0, "MOSI");
        else if (c.tag == "MOSB" && c.size > 0) {
            root.skyboxName = stringAt(std::string(reinterpret_cast<const char*>(c.data), c.size), 0);
        }
    }

    if (!root.uvScrollSpeeds.empty() && root.uvScrollSpeeds.size() != root.materials.size()) {
        throw ParseError("MOUV: expected one entry per MOMT material (" + std::to_string(root.materials.size()) +
                         "), got " + std::to_string(root.uvScrollSpeeds.size()));
    }
    if (!root.doodadColorMultipliers.empty() && root.doodadColorMultipliers.size() != root.doodads.size()) {
        throw ParseError("MDDI: expected one entry per MODD doodad (" + std::to_string(root.doodads.size()) +
                         "), got " + std::to_string(root.doodadColorMultipliers.size()));
    }
    auto requireSet = [&](const char* tag, size_t i, uint32_t set) {
        if (set >= root.doodadSets.size()) {
            throw ParseError(std::string(tag) + "[" + std::to_string(i) + "].doodadSet: expected a MODS index < " +
                             std::to_string(root.doodadSets.size()) + ", got " + std::to_string(set));
        }
    };
    for (size_t i = 0; i < root.globalAmbientVolumes.size(); ++i) requireSet("MAVG", i, root.globalAmbientVolumes[i].doodadSet);
    for (size_t i = 0; i < root.ambientVolumes.size(); ++i) requireSet("MAVD", i, root.ambientVolumes[i].doodadSet);
    for (size_t i = 0; i < root.ambientBoxes.size(); ++i) requireSet("MBVD", i, root.ambientBoxes[i].doodadSet);
    for (size_t i = 0; i < root.portals.size(); ++i) {
        const Portal& p = root.portals[i];
        if (p.startVertex + p.count > root.portalVertices.size()) {
            throw ParseError("MOPT[" + std::to_string(i) + "]: expected vertices [" + std::to_string(p.startVertex) +
                             ", " + std::to_string(p.startVertex + p.count) + ") within " +
                             std::to_string(root.portalVertices.size()) + " MOPV vertices");
        }
    }
    for (size_t i = 0; i < root.portalRefs.size(); ++i) {
        const PortalRef& r = root.portalRefs[i];
        if (r.portal >= root.portals.size() || r.group >= root.groups.size()) {
            throw ParseError("MOPR[" + std::to_string(i) + "]: expected portal < " + std::to_string(root.portals.size()) +
                             " and group < " + std::to_string(root.groups.size()) + ", got portal " +
                             std::to_string(r.portal) + ", group " + std::to_string(r.group));
        }
    }
    if (!root.portalExtras.empty() && root.portalExtras.size() != root.portals.size()) {
        throw ParseError("MOPE: expected one entry per MOPT portal (" + std::to_string(root.portals.size()) + "), got " +
                         std::to_string(root.portalExtras.size()));
    }
    // Real MFED chunks are either empty (1,699/1,802) or one per MFOG.
    if (!fogDoodadSets.empty()) {
        if (fogDoodadSets.size() != root.fogs.size()) {
            throw ParseError("MFED: expected one entry per MFOG fog (" + std::to_string(root.fogs.size()) + "), got " +
                             std::to_string(fogDoodadSets.size()));
        }
        for (size_t i = 0; i < root.fogs.size(); ++i) {
            requireSet("MFED", i, fogDoodadSets[i].doodadSet);
            root.fogs[i].doodadSet = fogDoodadSets[i].doodadSet;
            root.fogs[i].doodadSetReserved = std::move(fogDoodadSets[i].reserved);
        }
    }
    for (size_t i = 0; i < root.newLights.size(); ++i) {
        int32_t set = root.newLights[i].doodadSet;
        if (set < 0 || static_cast<size_t>(set) >= root.doodadSets.size()) {
            throw ParseError("MNLD[" + std::to_string(i) + "].doodadSet: expected a MODS index < " +
                             std::to_string(root.doodadSets.size()) + ", got " + std::to_string(set));
        }
    }
    for (const DoodadSet& set : root.doodadSets) {
        if (set.startIndex > root.doodads.size() || set.count > root.doodads.size() - set.startIndex) {
            throw ParseError("MODS '" + set.name + "': expected doodads [" + std::to_string(set.startIndex) + ", " +
                             std::to_string(set.startIndex + set.count) + ") within " +
                             std::to_string(root.doodads.size()) + " MODD entries");
        }
    }
    return root;
}

GroupFile parseGroup(const std::vector<uint8_t>& fileBytes) {
    std::vector<Chunk> top = readChunksReversed(fileBytes.data(), fileBytes.size());
    GroupFile g;
    g.version = readVersion(top, "WMO group");
    const Chunk& mogp = requireChunk(top, "MOGP", "WMO group");
    if (mogp.size < kMogpHeaderSize) {
        throw ParseError("MOGP: expected >= " + std::to_string(kMogpHeaderSize) + "-byte header, got " +
                         std::to_string(mogp.size) + " bytes");
    }
    const uint8_t* d = mogp.data;
    g.nameOffset = read<uint32_t>(d, mogp.size, 0x00, "MOGP");
    g.descriptiveNameOffset = read<uint32_t>(d, mogp.size, 0x04, "MOGP");
    g.flags = read<uint32_t>(d, mogp.size, 0x08, "MOGP");
    g.bounds = readBox(d, mogp.size, 0x0C, "MOGP");
    g.portalStart = read<uint16_t>(d, mogp.size, 0x24, "MOGP");
    g.portalCount = read<uint16_t>(d, mogp.size, 0x26, "MOGP");
    g.transparentBatchCount = read<uint16_t>(d, mogp.size, 0x28, "MOGP");
    g.interiorBatchCount = read<uint16_t>(d, mogp.size, 0x2A, "MOGP");
    g.exteriorBatchCount = read<uint16_t>(d, mogp.size, 0x2C, "MOGP");
    for (size_t k = 0; k < 4; ++k) g.fogIds[k] = read<uint8_t>(d, mogp.size, 0x30 + k, "MOGP");
    g.liquidType = read<uint32_t>(d, mogp.size, 0x34, "MOGP");
    g.wmoAreaTableId = read<uint32_t>(d, mogp.size, 0x38, "MOGP");
    g.flags2 = read<uint32_t>(d, mogp.size, 0x3C, "MOGP");

    // Every other group chunk lives inside MOGP's payload, after its header.
    std::vector<Chunk> sub = readChunksReversed(d + kMogpHeaderSize, mogp.size - kMogpHeaderSize);
    for (const Chunk& c : sub) {
        if (c.tag == "MOPY") {
            for (const auto& [flags, material] : readArray<std::array<uint8_t, 2>>(c)) {
                g.triangles.push_back({flags, material});
            }
        } else if (c.tag == "MPY2") {
            for (const auto& [flags, material] : readArray<std::array<uint16_t, 2>>(c)) {
                g.triangles.push_back({flags, material});
            }
        } else if (c.tag == "MOVI") {
            for (uint16_t i : readArray<uint16_t>(c)) g.indices.push_back(i);
        } else if (c.tag == "MOVX") {
            g.indices = readArray<uint32_t>(c);
        } else if (c.tag == "MOVT") {
            g.positions = readArray<Vec3>(c);
        } else if (c.tag == "MONR") {
            g.normals = readArray<Vec3>(c);
        } else if (c.tag == "MOTV") {
            g.uvSets.push_back(readArray<Vec2>(c));
        } else if (c.tag == "MOCV") {
            g.colorSets.push_back(readArray<uint32_t>(c));
        } else if (c.tag == "MOBA") {
            g.batches = parseBatches(c);
        } else if (c.tag == "MLIQ") {
            g.liquid = parseLiquid(c);
        } else if (c.tag == "MPVR") {
            g.particulateVolumeRefs = readArray<uint16_t>(c);
        } else if (c.tag == "MAVR") {
            g.ambientVolumeRefs = readArray<uint16_t>(c);
        } else if (c.tag == "MBVR") {
            g.ambientBoxRefs = readArray<uint16_t>(c);
        } else if (c.tag == "MFVR") {
            g.fogRefs = readArray<uint16_t>(c);
        }
    }

    if (g.indices.size() % 3 != 0) {
        throw ParseError("MOVI: expected a multiple of 3 indices, got " + std::to_string(g.indices.size()));
    }
    if (!g.triangles.empty() && g.triangles.size() != g.indices.size() / 3) {
        throw ParseError("MOPY/MPY2: expected one entry per MOVI triangle (" + std::to_string(g.indices.size() / 3) +
                         "), got " + std::to_string(g.triangles.size()));
    }
    size_t vertexCount = g.positions.size();
    if (!g.normals.empty()) requireVertexCount("MONR", g.normals.size(), vertexCount);
    for (const auto& uv : g.uvSets) requireVertexCount("MOTV", uv.size(), vertexCount);
    for (const auto& colors : g.colorSets) requireVertexCount("MOCV", colors.size(), vertexCount);
    for (uint32_t i : g.indices) {
        if (i >= vertexCount) {
            throw ParseError("MOVI: expected indices < " + std::to_string(vertexCount) + " vertices, got " +
                             std::to_string(i));
        }
    }
    for (size_t i = 0; i < g.batches.size(); ++i) {
        const Batch& b = g.batches[i];
        if (b.startIndex > g.indices.size() || b.indexCount > g.indices.size() - b.startIndex) {
            throw ParseError("MOBA batch " + std::to_string(i) + ": expected indices [" +
                             std::to_string(b.startIndex) + ", " + std::to_string(b.startIndex + b.indexCount) +
                             ") within " + std::to_string(g.indices.size()) + " MOVI indices");
        }
    }
    return g;
}

uint32_t groupFileDataId(const RootFile& root, uint32_t lodTier, uint32_t group) {
    size_t index = static_cast<size_t>(lodTier) * root.header.groupCount + group;
    if (group >= root.header.groupCount || index >= root.groupFileDataIds.size()) return 0;
    return root.groupFileDataIds[index];
}

std::string stringAt(const std::string& blob, uint32_t offset) {
    if (offset >= blob.size()) {
        throw ParseError("string offset: expected < " + std::to_string(blob.size()) + " (blob size), got " +
                         std::to_string(offset));
    }
    return std::string(blob.c_str() + offset);
}

}  // namespace husk::wmo
