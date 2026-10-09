#include "adt.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <string>

#include "chunk.hpp"

namespace husk::adt {

namespace {

constexpr size_t kMcnkHeaderSize = 128;
constexpr size_t kMh2oHeaderEntrySize = 12;
constexpr size_t kMh2oInstanceSize = 24;
constexpr size_t kMddfSize = 36;
constexpr size_t kModfSize = 64;
constexpr size_t kMcvtSize = kHeightCount * 4;
// 145 normals + 13 trailing bytes (Cata+); the pre-Cata 435-byte form is not
// accepted -- ADT_TERRAIN_TODO.md found zero of it in this corpus.
constexpr size_t kMcnrSize = kHeightCount * 3 + 13;

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

std::array<float, 3> readVec3(const uint8_t* data, size_t size, size_t off, const char* what) {
    return {read<float>(data, size, off, what), read<float>(data, size, off + 4, what),
            read<float>(data, size, off + 8, what)};
}

std::vector<Chunk> readChunksReversed(const uint8_t* data, size_t size) {
    std::vector<Chunk> chunks = readChunks(data, size);
    for (Chunk& c : chunks) std::reverse(c.tag.begin(), c.tag.end());
    return chunks;
}

void requireSize(const Chunk& c, size_t expected, const char* what) {
    if (c.size != expected) {
        throw ParseError(std::string(what) + ": expected " + std::to_string(expected) + " bytes, got " +
                         std::to_string(c.size));
    }
}

MapChunk parseMapChunk(const Chunk& mcnk) {
    const uint8_t* d = mcnk.data;
    size_t n = mcnk.size;
    if (n < kMcnkHeaderSize) {
        throw ParseError("MCNK: expected >= 128-byte header, got " + std::to_string(n) + " bytes");
    }
    MapChunk out;
    out.flags = read<uint32_t>(d, n, 0x00, "MCNK.flags");
    out.indexX = read<uint32_t>(d, n, 0x04, "MCNK.IndexX");
    out.indexY = read<uint32_t>(d, n, 0x08, "MCNK.IndexY");
    std::memcpy(out.holesHighRes.data(), d + 0x14, 8);
    out.areaId = read<uint32_t>(d, n, 0x34, "MCNK.areaid");
    out.holesLowRes = read<uint16_t>(d, n, 0x3C, "MCNK.holes_low_res");
    std::memcpy(out.predominantTexture.data(), d + 0x40, 16);
    std::memcpy(out.noEffectDoodad.data(), d + 0x50, 8);
    out.position = readVec3(d, n, 0x68, "MCNK.position");

    std::vector<Chunk> subs = readChunksReversed(d + kMcnkHeaderSize, n - kMcnkHeaderSize);
    std::optional<Chunk> mcvt = findChunk(subs, "MCVT");
    std::optional<Chunk> mcnr = findChunk(subs, "MCNR");
    if (!mcvt || !mcnr) {
        throw ParseError("MCNK (" + std::to_string(out.indexX) + "," + std::to_string(out.indexY) +
                         "): expected MCVT and MCNR sub-chunks, got MCVT=" + (mcvt ? "yes" : "no") +
                         " MCNR=" + (mcnr ? "yes" : "no"));
    }
    requireSize(*mcvt, kMcvtSize, "MCVT");
    requireSize(*mcnr, kMcnrSize, "MCNR");
    std::memcpy(out.heights.data(), mcvt->data, kMcvtSize);
    std::memcpy(out.normals.data(), mcnr->data, kHeightCount * 3);
    return out;
}

// MH2O has no per-instance vertex-format field that is resolvable without
// LiquidObject/LiquidType/LiquidMaterial DB2s. ADT/v18.md's "Alternate case
// determination" instead infers it: every data block is written back to
// back in enumeration order, so the gap to the next referenced offset over
// the vertex count gives bytes-per-vertex (5/8/1/9 -> case 0/1/2/3).
LiquidVertexFormat inferVertexFormat(size_t gapBytes, size_t vertexCount, uint32_t offset) {
    if (gapBytes % vertexCount != 0) {
        throw ParseError("MH2O instance vertex data at " + std::to_string(offset) + ": expected gap divisible by " +
                         std::to_string(vertexCount) + " vertices, got " + std::to_string(gapBytes) + " bytes");
    }
    switch (gapBytes / vertexCount) {
        case 5: return LiquidVertexFormat::HeightDepth;
        case 8: return LiquidVertexFormat::HeightUv;
        case 1: return LiquidVertexFormat::DepthOnly;
        case 9: return LiquidVertexFormat::HeightUvDepth;
    }
    throw ParseError("MH2O instance vertex data at " + std::to_string(offset) +
                     ": expected 5/8/1/9 bytes per vertex, got " + std::to_string(gapBytes / vertexCount));
}

void readVertexData(const uint8_t* d, size_t n, uint32_t off, LiquidInstance& inst) {
    size_t count = static_cast<size_t>(inst.width + 1) * (inst.height + 1);
    bool hasHeight = *inst.vertexFormat != LiquidVertexFormat::DepthOnly;
    bool hasUv = *inst.vertexFormat == LiquidVertexFormat::HeightUv ||
                 *inst.vertexFormat == LiquidVertexFormat::HeightUvDepth;
    bool hasDepth = *inst.vertexFormat != LiquidVertexFormat::HeightUv;
    size_t cursor = off;
    if (hasHeight) {
        for (size_t i = 0; i < count; ++i) inst.vertexHeights.push_back(read<float>(d, n, cursor + i * 4, "MH2O heights"));
        cursor += count * 4;
    }
    if (hasUv) {
        for (size_t i = 0; i < count; ++i) {
            inst.vertexUvs.push_back({read<uint16_t>(d, n, cursor + i * 4, "MH2O uv"),
                                      read<uint16_t>(d, n, cursor + i * 4 + 2, "MH2O uv")});
        }
        cursor += count * 4;
    }
    if (hasDepth) {
        for (size_t i = 0; i < count; ++i) inst.vertexDepths.push_back(read<uint8_t>(d, n, cursor + i, "MH2O depth"));
    }
}

std::array<LiquidChunk, kChunkCount> parseMh2o(const Chunk& mh2o) {
    const uint8_t* d = mh2o.data;
    size_t n = mh2o.size;
    std::array<LiquidChunk, kChunkCount> out;
    std::set<uint32_t> dataOffsets;
    std::vector<std::pair<LiquidInstance*, uint32_t>> pendingVertexData;

    for (size_t ci = 0; ci < kChunkCount; ++ci) {
        size_t h = ci * kMh2oHeaderEntrySize;
        uint32_t offInstances = read<uint32_t>(d, n, h, "MH2O.offset_instances");
        uint32_t layerCount = read<uint32_t>(d, n, h + 4, "MH2O.layer_count");
        uint32_t offAttributes = read<uint32_t>(d, n, h + 8, "MH2O.offset_attributes");
        if (offAttributes != 0) dataOffsets.insert(offAttributes);
        out[ci].instances.resize(layerCount);
        for (uint32_t k = 0; k < layerCount; ++k) {
            size_t b = offInstances + k * kMh2oInstanceSize;
            LiquidInstance& inst = out[ci].instances[k];
            inst.liquidType = read<uint16_t>(d, n, b, "MH2O.liquid_type");
            inst.liquidObjectOrLvf = read<uint16_t>(d, n, b + 2, "MH2O.liquid_object_or_lvf");
            inst.minHeight = read<float>(d, n, b + 4, "MH2O.min_height_level");
            inst.maxHeight = read<float>(d, n, b + 8, "MH2O.max_height_level");
            inst.xOffset = read<uint8_t>(d, n, b + 12, "MH2O.x_offset");
            inst.yOffset = read<uint8_t>(d, n, b + 13, "MH2O.y_offset");
            inst.width = read<uint8_t>(d, n, b + 14, "MH2O.width");
            inst.height = read<uint8_t>(d, n, b + 15, "MH2O.height");
            uint32_t offExists = read<uint32_t>(d, n, b + 16, "MH2O.offset_exists_bitmap");
            uint32_t offVertex = read<uint32_t>(d, n, b + 20, "MH2O.offset_vertex_data");
            if (inst.xOffset + inst.width > 8 || inst.yOffset + inst.height > 8 || inst.width == 0 ||
                inst.height == 0) {
                throw ParseError("MH2O chunk " + std::to_string(ci) + " instance " + std::to_string(k) +
                                 ": expected rect inside 8x8 with nonzero size, got offset (" +
                                 std::to_string(inst.xOffset) + "," + std::to_string(inst.yOffset) + ") size (" +
                                 std::to_string(inst.width) + "," + std::to_string(inst.height) + ")");
            }
            if (offExists != 0) {
                dataOffsets.insert(offExists);
                size_t bytes = (static_cast<size_t>(inst.width) * inst.height + 7) / 8;
                for (size_t i = 0; i < bytes; ++i) inst.existsBitmap.push_back(read<uint8_t>(d, n, offExists + i, "MH2O exists"));
            }
            if (offVertex != 0) {
                dataOffsets.insert(offVertex);
                pendingVertexData.emplace_back(&inst, offVertex);
            }
        }
    }

    for (auto& [inst, off] : pendingVertexData) {
        auto next = dataOffsets.upper_bound(off);
        size_t end = next == dataOffsets.end() ? n : *next;
        size_t count = static_cast<size_t>(inst->width + 1) * (inst->height + 1);
        inst->vertexFormat = inferVertexFormat(end - off, count, off);
        readVertexData(d, n, off, *inst);
    }
    return out;
}

}  // namespace

RootFile parseRoot(const std::vector<uint8_t>& bytes) {
    std::vector<Chunk> chunks = readChunksReversed(bytes.data(), bytes.size());
    RootFile out;
    for (const Chunk& c : chunks) {
        if (c.tag == "MCNK") out.chunks.push_back(parseMapChunk(c));
    }
    if (out.chunks.size() != kChunkCount) {
        throw ParseError("ADT root: expected 256 MCNK chunks, got " + std::to_string(out.chunks.size()));
    }
    for (size_t i = 0; i < out.chunks.size(); ++i) {
        const MapChunk& mc = out.chunks[i];
        if (mc.indexX != i % kChunksPerSide || mc.indexY != i / kChunksPerSide) {
            throw ParseError("ADT root: MCNK #" + std::to_string(i) + " expected index (" +
                             std::to_string(i % kChunksPerSide) + "," + std::to_string(i / kChunksPerSide) +
                             "), got (" + std::to_string(mc.indexX) + "," + std::to_string(mc.indexY) + ")");
        }
    }
    if (std::optional<Chunk> mh2o = findChunk(chunks, "MH2O")) out.liquid = parseMh2o(*mh2o);
    return out;
}

ObjFile parseObj(const std::vector<uint8_t>& bytes) {
    std::vector<Chunk> chunks = readChunksReversed(bytes.data(), bytes.size());
    ObjFile out;
    if (std::optional<Chunk> mddf = findChunk(chunks, "MDDF")) {
        if (mddf->size % kMddfSize != 0) {
            throw ParseError("MDDF: expected size multiple of 36, got " + std::to_string(mddf->size));
        }
        for (size_t b = 0; b < mddf->size; b += kMddfSize) {
            const uint8_t* d = mddf->data;
            size_t n = mddf->size;
            DoodadPlacement p;
            p.nameId = read<uint32_t>(d, n, b, "MDDF.nameId");
            p.uniqueId = read<uint32_t>(d, n, b + 4, "MDDF.uniqueId");
            p.position = readVec3(d, n, b + 8, "MDDF.position");
            p.rotation = readVec3(d, n, b + 20, "MDDF.rotation");
            p.scale = read<uint16_t>(d, n, b + 32, "MDDF.scale");
            p.flags = read<uint16_t>(d, n, b + 34, "MDDF.flags");
            out.doodads.push_back(p);
        }
    }
    if (std::optional<Chunk> modf = findChunk(chunks, "MODF")) {
        if (modf->size % kModfSize != 0) {
            throw ParseError("MODF: expected size multiple of 64, got " + std::to_string(modf->size));
        }
        for (size_t b = 0; b < modf->size; b += kModfSize) {
            const uint8_t* d = modf->data;
            size_t n = modf->size;
            MapObjectPlacement p;
            p.nameId = read<uint32_t>(d, n, b, "MODF.nameId");
            p.uniqueId = read<uint32_t>(d, n, b + 4, "MODF.uniqueId");
            p.position = readVec3(d, n, b + 8, "MODF.position");
            p.rotation = readVec3(d, n, b + 20, "MODF.rotation");
            p.extentsMin = readVec3(d, n, b + 32, "MODF.extents");
            p.extentsMax = readVec3(d, n, b + 44, "MODF.extents");
            p.flags = read<uint16_t>(d, n, b + 56, "MODF.flags");
            p.doodadSet = read<uint16_t>(d, n, b + 58, "MODF.doodadSet");
            p.nameSet = read<uint16_t>(d, n, b + 60, "MODF.nameSet");
            p.scale = read<uint16_t>(d, n, b + 62, "MODF.scale");
            out.mapObjects.push_back(p);
        }
    }
    return out;
}

namespace {

std::vector<uint32_t> readU32Array(const Chunk& c, const char* what) {
    if (c.size % 4 != 0) {
        throw ParseError(std::string(what) + ": expected size multiple of 4, got " + std::to_string(c.size));
    }
    std::vector<uint32_t> out(c.size / 4);
    std::memcpy(out.data(), c.data, c.size);
    return out;
}

}  // namespace

TexFile parseTex(const std::vector<uint8_t>& bytes) {
    std::vector<Chunk> chunks = readChunksReversed(bytes.data(), bytes.size());
    TexFile out;
    std::optional<Chunk> mdid = findChunk(chunks, "MDID");
    std::optional<Chunk> mtex = findChunk(chunks, "MTEX");
    // Untextured tiles still ship an MTEX holding only an empty string (findings doc).
    bool emptyMtex = mtex && std::all_of(mtex->data, mtex->data + mtex->size, [](uint8_t b) { return b == 0; });
    if (!mdid && !emptyMtex) {
        throw ParseError("_tex0: expected an MDID chunk (FileDataID texture list) or an empty MTEX; "
                         "MTEX filename tables are not implemented");
    }
    if (mdid) out.diffuseIds = readU32Array(*mdid, "MDID");
    if (std::optional<Chunk> mhid = findChunk(chunks, "MHID")) {
        out.heightIds = readU32Array(*mhid, "MHID");
        if (out.heightIds.size() != out.diffuseIds.size()) {
            throw ParseError("MHID: expected " + std::to_string(out.diffuseIds.size()) + " entries (one per MDID), got " +
                             std::to_string(out.heightIds.size()));
        }
    }
    if (std::optional<Chunk> mtxp = findChunk(chunks, "MTXP")) {
        if (mtxp->size % 16 != 0) {
            throw ParseError("MTXP: expected size multiple of 16, got " + std::to_string(mtxp->size));
        }
        std::vector<TextureParams> params;
        for (size_t b = 0; b < mtxp->size; b += 16) {
            params.push_back({read<uint32_t>(mtxp->data, mtxp->size, b, "MTXP.flags"),
                              read<float>(mtxp->data, mtxp->size, b + 4, "MTXP.heightScale"),
                              read<float>(mtxp->data, mtxp->size, b + 8, "MTXP.heightOffset")});
        }
        out.params = std::move(params);
    }

    size_t ci = 0;
    for (const Chunk& c : chunks) {
        if (c.tag != "MCNK") continue;
        if (ci == kChunkCount) throw ParseError("_tex0: expected 256 MCNK chunks, got more");
        std::vector<Chunk> subs = readChunksReversed(c.data, c.size);
        TexChunk& tc = out.chunks[ci++];
        if (std::optional<Chunk> mcly = findChunk(subs, "MCLY")) {
            if (mcly->size % 16 != 0) {
                throw ParseError("MCLY: expected size multiple of 16, got " + std::to_string(mcly->size));
            }
            for (size_t b = 0; b < mcly->size; b += 16) {
                TextureLayer layer{read<uint32_t>(mcly->data, mcly->size, b, "MCLY.textureId"),
                                   read<uint32_t>(mcly->data, mcly->size, b + 4, "MCLY.flags"),
                                   read<uint32_t>(mcly->data, mcly->size, b + 8, "MCLY.offsetInMCAL"),
                                   read<uint32_t>(mcly->data, mcly->size, b + 12, "MCLY.effectId")};
                if (layer.textureId >= out.diffuseIds.size()) {
                    throw ParseError("MCLY in MCNK #" + std::to_string(ci - 1) + ": expected textureId < " +
                                     std::to_string(out.diffuseIds.size()) + ", got " + std::to_string(layer.textureId));
                }
                tc.layers.push_back(layer);
            }
        }
        if (std::optional<Chunk> mcal = findChunk(subs, "MCAL")) tc.mcal.assign(mcal->data, mcal->data + mcal->size);
    }
    if (ci != kChunkCount) throw ParseError("_tex0: expected 256 MCNK chunks, got " + std::to_string(ci));
    return out;
}

uint32_t parseWdtFlags(const std::vector<uint8_t>& bytes) {
    std::vector<Chunk> chunks = readChunksReversed(bytes.data(), bytes.size());
    std::optional<Chunk> mphd = findChunk(chunks, "MPHD");
    if (!mphd) throw ParseError("WDT: expected an MPHD chunk");
    return read<uint32_t>(mphd->data, mphd->size, 0, "MPHD.flags");
}

std::array<uint8_t, 64 * 64> decodeAlphaMap(const std::vector<uint8_t>& mcal, const TextureLayer& layer,
                                            bool eightBitAlpha, bool doNotFix) {
    std::array<uint8_t, 64 * 64> out{};
    const uint8_t* d = mcal.data();
    size_t n = mcal.size();
    size_t in = layer.offsetInMcal;
    if (layer.flags & kLayerAlphaCompressed) {
        if (!eightBitAlpha) {
            throw ParseError("MCAL: compressed alpha flag set but WDT MPHD selects 4-bit alpha (expected 0x4 or 0x80)");
        }
        // Some Blizzard maps decompress past 4096 (ADT/v18.md); stop at the map's end like Noggit does.
        size_t o = 0;
        while (o < out.size()) {
            uint8_t control = read<uint8_t>(d, n, in++, "MCAL rle control");
            bool fill = control & 0x80;
            size_t count = control & 0x7F;
            for (size_t k = 0; k < count && o < out.size(); ++k) {
                out[o++] = read<uint8_t>(d, n, fill ? in : in + k, "MCAL rle value");
            }
            in += fill ? 1 : count;
        }
        return out;
    }
    if (eightBitAlpha) {
        for (size_t i = 0; i < out.size(); ++i) out[i] = read<uint8_t>(d, n, in + i, "MCAL 4096");
        return out;
    }
    for (size_t i = 0; i < out.size(); i += 2) {
        uint8_t packed = read<uint8_t>(d, n, in + i / 2, "MCAL 2048");
        uint8_t lo = packed & 0x0F;
        uint8_t hi = packed >> 4;
        out[i] = static_cast<uint8_t>(lo | lo << 4);
        out[i + 1] = static_cast<uint8_t>(hi | hi << 4);
    }
    if (!doNotFix) {
        for (size_t r = 0; r < 64; ++r) out[r * 64 + 63] = out[r * 64 + 62];
        for (size_t c = 0; c < 64; ++c) out[63 * 64 + c] = out[62 * 64 + c];
    }
    return out;
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ParseError("cannot open " + path.string());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace husk::adt
