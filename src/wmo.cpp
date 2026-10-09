#include "wmo.hpp"

#include <algorithm>
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

}  // namespace

RootFile parseRoot(const std::vector<uint8_t>& fileBytes) {
    std::vector<Chunk> chunks = readChunksReversed(fileBytes.data(), fileBytes.size());
    RootFile root;
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
        }
    }

    if (g.indices.size() % 3 != 0) {
        throw ParseError("MOVI: expected a multiple of 3 indices, got " + std::to_string(g.indices.size()));
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
