#pragma once

// Synthetic ADT/WDT byte builders shared by test_adt.cpp, test_adt_canon_input.cpp
// and test_cli_export_terrain.cpp. Built from documentation/wowdev-wiki/md/ADT/v18.md
// and WDT.md, with the real-data corrections in WIKI_FINDINGS/WORLD.md applied
// (MCNR is X, Y, Z on disk). Tags are given in the wiki's spelling and
// byte-reversed on write, matching the real on-disk ADT/WDT convention.
//
// Same anonymous-namespace fixture-header convention as test_cli_fixtures.hpp
// (see CMakeLists.txt's -Wno-unused-function note).

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace adtfx {

namespace {

constexpr float kTileSize = 1600.0f / 3.0f;
constexpr float kChunkSize = kTileSize / 16.0f;
constexpr float kMapHalfExtent = 32.0f * kTileSize;

void putU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }

void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

void putF32(std::vector<uint8_t>& b, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    putU32(b, bits);
}

void putVec3(std::vector<uint8_t>& b, float x, float y, float z) {
    putF32(b, x);
    putF32(b, y);
    putF32(b, z);
}

void setU32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[off + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (i * 8));
}

void setF32(std::vector<uint8_t>& b, size_t off, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    setU32(b, off, bits);
}

// `tag` in the wiki's spelling ("MCNK"); written reversed ("KNCM").
void appendChunk(std::vector<uint8_t>& out, const char* tag, const std::vector<uint8_t>& payload) {
    for (int i = 3; i >= 0; --i) out.push_back(static_cast<uint8_t>(tag[i]));
    putU32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> mverChunk() {
    std::vector<uint8_t> out;
    std::vector<uint8_t> p;
    putU32(p, 18);
    appendChunk(out, "MVER", p);
    return out;
}

// World (X, Y) of chunk (ix, iy)'s corner vertex (0, 0) on tile (tileX, tileY):
// IndexX steps -Y, IndexY steps -X (WIKI_FINDINGS/WORLD.md).
std::array<float, 2> chunkOrigin(uint32_t tileX, uint32_t tileY, uint32_t ix, uint32_t iy) {
    return {kMapHalfExtent - static_cast<float>(tileY) * kTileSize - static_cast<float>(iy) * kChunkSize,
            kMapHalfExtent - static_cast<float>(tileX) * kTileSize - static_cast<float>(ix) * kChunkSize};
}

// One root-file MCNK's knobs. Defaults give a flat, hole-free, upward-facing
// chunk on the 64-bit (high-res) hole path.
struct ChunkSpec {
    uint32_t flags = 0x10000;  // high_res_holes, universal in the real corpus
    float baseZ = 100.0f;
    std::function<float(int)> height = [](int) { return 0.0f; };  // MCVT, relative to baseZ
    std::array<int8_t, 3> normal = {0, 0, 127};                   // MCNR, X Y Z
    std::array<uint8_t, 8> holesHighRes{};
    uint16_t holesLowRes = 0;
    uint32_t areaId = 12;
    std::array<uint8_t, 16> predominantTexture{};
    std::array<uint8_t, 8> noEffectDoodad{};
    bool includeMcvt = true;
    bool includeMcnr = true;
    std::array<float, 2> positionOverride{};  // used only when overridePosition
    bool overridePosition = false;
};

std::vector<uint8_t> mcnkRootPayload(uint32_t tileX, uint32_t tileY, uint32_t ix, uint32_t iy,
                                     const ChunkSpec& spec) {
    std::vector<uint8_t> p(128, 0);
    setU32(p, 0x00, spec.flags);
    setU32(p, 0x04, ix);
    setU32(p, 0x08, iy);
    std::memcpy(p.data() + 0x14, spec.holesHighRes.data(), 8);
    setU32(p, 0x34, spec.areaId);
    p[0x3C] = static_cast<uint8_t>(spec.holesLowRes);
    p[0x3D] = static_cast<uint8_t>(spec.holesLowRes >> 8);
    std::memcpy(p.data() + 0x40, spec.predominantTexture.data(), 16);
    std::memcpy(p.data() + 0x50, spec.noEffectDoodad.data(), 8);
    std::array<float, 2> origin = spec.overridePosition ? spec.positionOverride : chunkOrigin(tileX, tileY, ix, iy);
    setF32(p, 0x68, origin[0]);
    setF32(p, 0x6C, origin[1]);
    setF32(p, 0x70, spec.baseZ);

    if (spec.includeMcvt) {
        std::vector<uint8_t> mcvt;
        for (int i = 0; i < 145; ++i) putF32(mcvt, spec.height(i));
        appendChunk(p, "MCVT", mcvt);
    }
    if (spec.includeMcnr) {
        std::vector<uint8_t> mcnr;
        for (int i = 0; i < 145; ++i) {
            for (int8_t c : spec.normal) mcnr.push_back(static_cast<uint8_t>(c));
        }
        mcnr.resize(mcnr.size() + 13, 0);
        appendChunk(p, "MCNR", mcnr);
    }
    return p;
}

// One MH2O instance. Empty `exists` / `vertexData` write offset 0.
struct LiquidSpec {
    uint32_t chunk = 0;
    uint16_t liquidType = 1;
    uint16_t liquidObjectOrLvf = 0;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t width = 8;
    uint8_t height = 8;
    std::vector<uint8_t> exists;
    std::vector<uint8_t> vertexData;
};

// MH2O payload: 256 header entries, then every chunk's instances in chunk
// order, then each instance's exists bitmap and vertex data back to back --
// the layout the bytes-per-vertex inference relies on.
std::vector<uint8_t> mh2oPayload(const std::vector<LiquidSpec>& liquids) {
    std::array<std::vector<const LiquidSpec*>, 256> byChunk;
    for (const LiquidSpec& l : liquids) byChunk[l.chunk].push_back(&l);

    std::vector<uint8_t> p(256 * 12, 0);
    std::vector<std::pair<size_t, const LiquidSpec*>> instanceAt;
    for (size_t c = 0; c < 256; ++c) {
        if (byChunk[c].empty()) continue;
        setU32(p, c * 12, static_cast<uint32_t>(p.size()));
        setU32(p, c * 12 + 4, static_cast<uint32_t>(byChunk[c].size()));
        for (const LiquidSpec* l : byChunk[c]) {
            instanceAt.emplace_back(p.size(), l);
            putU16(p, l->liquidType);
            putU16(p, l->liquidObjectOrLvf);
            putF32(p, l->minHeight);
            putF32(p, l->maxHeight);
            putU8(p, l->x);
            putU8(p, l->y);
            putU8(p, l->width);
            putU8(p, l->height);
            putU32(p, 0);  // offset_exists_bitmap, patched below
            putU32(p, 0);  // offset_vertex_data, patched below
        }
    }
    for (auto& [at, l] : instanceAt) {
        if (!l->exists.empty()) {
            setU32(p, at + 16, static_cast<uint32_t>(p.size()));
            p.insert(p.end(), l->exists.begin(), l->exists.end());
        }
        if (!l->vertexData.empty()) {
            setU32(p, at + 20, static_cast<uint32_t>(p.size()));
            p.insert(p.end(), l->vertexData.begin(), l->vertexData.end());
        }
    }
    return p;
}

struct RootSpec {
    uint32_t tileX = 32;
    uint32_t tileY = 48;
    std::function<ChunkSpec(uint32_t ix, uint32_t iy)> chunk = [](uint32_t, uint32_t) { return ChunkSpec{}; };
    std::vector<LiquidSpec> liquids;  // MH2O written only when non-empty
    bool writeMh2o = false;           // force an MH2O chunk even with no instances
    uint32_t chunkCount = 256;
    bool swapFirstTwoChunks = false;
};

std::vector<uint8_t> rootFile(const RootSpec& spec) {
    std::vector<uint8_t> out = mverChunk();
    std::vector<uint8_t> mhdr(64, 0);
    appendChunk(out, "MHDR", mhdr);
    if (!spec.liquids.empty() || spec.writeMh2o) appendChunk(out, "MH2O", mh2oPayload(spec.liquids));
    std::vector<std::vector<uint8_t>> chunks;
    for (uint32_t i = 0; i < spec.chunkCount; ++i) {
        uint32_t ix = i % 16;
        uint32_t iy = i / 16;
        chunks.push_back(mcnkRootPayload(spec.tileX, spec.tileY, ix, iy, spec.chunk(ix, iy)));
    }
    if (spec.swapFirstTwoChunks) std::swap(chunks[0], chunks[1]);
    for (const auto& c : chunks) appendChunk(out, "MCNK", c);
    return out;
}

// MDDF entry (36 bytes). `position`/`rotation` are the raw on-disk values.
struct DoodadSpec {
    uint32_t nameId = 0;
    uint32_t uniqueId = 0;
    std::array<float, 3> position{};
    std::array<float, 3> rotation{};
    uint16_t scale = 1024;
    uint16_t flags = 0x40;  // nameId is a FileDataID
};

// MODF entry (64 bytes).
struct MapObjectSpec {
    uint32_t nameId = 0;
    uint32_t uniqueId = 0;
    std::array<float, 3> position{};
    std::array<float, 3> rotation{};
    uint16_t flags = 0x8;  // nameId is a FileDataID
    uint16_t doodadSet = 0;
    uint16_t nameSet = 0;
    uint16_t scale = 1024;
};

// Name tables for the MMDX/MMID + MWMO/MWID (pre-FileDataID) placement path.
std::vector<uint8_t> nameTable(const std::vector<std::string>& names, std::vector<uint8_t>& offsetsOut) {
    std::vector<uint8_t> table;
    offsetsOut.clear();
    for (const std::string& n : names) {
        putU32(offsetsOut, static_cast<uint32_t>(table.size()));
        table.insert(table.end(), n.begin(), n.end());
        table.push_back(0);
    }
    return table;
}

struct ObjSpec {
    std::vector<DoodadSpec> doodads;
    std::vector<MapObjectSpec> mapObjects;
    std::vector<std::string> doodadNames;    // MMDX/MMID, written only when non-empty
    std::vector<std::string> mapObjectNames; // MWMO/MWID, written only when non-empty
};

std::vector<uint8_t> objFile(const ObjSpec& spec) {
    std::vector<uint8_t> out = mverChunk();
    if (!spec.doodadNames.empty()) {
        std::vector<uint8_t> ids;
        appendChunk(out, "MMDX", nameTable(spec.doodadNames, ids));
        appendChunk(out, "MMID", ids);
    }
    if (!spec.mapObjectNames.empty()) {
        std::vector<uint8_t> ids;
        appendChunk(out, "MWMO", nameTable(spec.mapObjectNames, ids));
        appendChunk(out, "MWID", ids);
    }
    std::vector<uint8_t> mddf;
    for (const DoodadSpec& d : spec.doodads) {
        putU32(mddf, d.nameId);
        putU32(mddf, d.uniqueId);
        putVec3(mddf, d.position[0], d.position[1], d.position[2]);
        putVec3(mddf, d.rotation[0], d.rotation[1], d.rotation[2]);
        putU16(mddf, d.scale);
        putU16(mddf, d.flags);
    }
    appendChunk(out, "MDDF", mddf);
    std::vector<uint8_t> modf;
    for (const MapObjectSpec& m : spec.mapObjects) {
        putU32(modf, m.nameId);
        putU32(modf, m.uniqueId);
        putVec3(modf, m.position[0], m.position[1], m.position[2]);
        putVec3(modf, m.rotation[0], m.rotation[1], m.rotation[2]);
        putVec3(modf, -1, -1, -1);
        putVec3(modf, 1, 1, 1);
        putU16(modf, m.flags);
        putU16(modf, m.doodadSet);
        putU16(modf, m.nameSet);
        putU16(modf, m.scale);
    }
    appendChunk(out, "MODF", modf);
    return out;
}

// One _tex0 MCLY entry plus the MCAL bytes it points at (empty for layer 0).
struct LayerSpec {
    uint32_t textureId = 0;
    uint32_t flags = 0;
    uint32_t effectId = 0xFFFFFFFF;
    std::vector<uint8_t> alpha;  // raw MCAL bytes for this layer
};

struct TexSpec {
    std::vector<uint32_t> diffuseIds = {1001};
    std::vector<uint32_t> heightIds;        // MHID, written only when non-empty
    std::vector<uint32_t> textureParams;    // MTXP flags, one per diffuse; written only when non-empty
    std::vector<std::string> textureNames;  // MTEX; written instead of MDID when non-empty
    bool emptyMtex = false;                 // untextured tile: MTEX holding one empty string, no MDID
    std::function<std::vector<LayerSpec>(uint32_t chunk)> layers = [](uint32_t) {
        return std::vector<LayerSpec>{LayerSpec{}};
    };
};

std::vector<uint8_t> texFile(const TexSpec& spec) {
    std::vector<uint8_t> out = mverChunk();
    if (spec.emptyMtex) {
        appendChunk(out, "MTEX", std::vector<uint8_t>{0});
    } else if (!spec.textureNames.empty()) {
        std::vector<uint8_t> ignored;
        appendChunk(out, "MTEX", nameTable(spec.textureNames, ignored));
    } else {
        std::vector<uint8_t> mdid;
        for (uint32_t id : spec.diffuseIds) putU32(mdid, id);
        appendChunk(out, "MDID", mdid);
    }
    if (!spec.heightIds.empty()) {
        std::vector<uint8_t> mhid;
        for (uint32_t id : spec.heightIds) putU32(mhid, id);
        appendChunk(out, "MHID", mhid);
    }
    if (!spec.textureParams.empty()) {
        std::vector<uint8_t> mtxp;
        for (uint32_t flags : spec.textureParams) {
            putU32(mtxp, flags);
            putF32(mtxp, 0.5f);
            putF32(mtxp, 2.0f);
            putU32(mtxp, 0);
        }
        appendChunk(out, "MTXP", mtxp);
    }
    for (uint32_t c = 0; c < 256; ++c) {
        std::vector<uint8_t> mcly;
        std::vector<uint8_t> mcal;
        for (const LayerSpec& l : spec.layers(c)) {
            putU32(mcly, l.textureId);
            putU32(mcly, l.flags);
            putU32(mcly, static_cast<uint32_t>(l.alpha.empty() ? 0 : mcal.size()));
            putU32(mcly, l.effectId);
            mcal.insert(mcal.end(), l.alpha.begin(), l.alpha.end());
        }
        std::vector<uint8_t> mcnk;
        appendChunk(mcnk, "MCLY", mcly);
        appendChunk(mcnk, "MCAL", mcal);
        appendChunk(out, "MCNK", mcnk);
    }
    return out;
}

std::vector<uint8_t> wdtFile(uint32_t mphdFlags) {
    std::vector<uint8_t> out = mverChunk();
    std::vector<uint8_t> mphd(32, 0);
    setU32(mphd, 0, mphdFlags);
    appendChunk(out, "MPHD", mphd);
    return out;
}

// Minimal valid BLP2: DXT1, 4x4, one solid-red block (same fixture as
// test_cli_textures.cpp's) -- enough for the DDS-rehousing payload path.
std::vector<uint8_t> oneBlockBlp() {
    std::vector<uint8_t> f(1172, 0);
    std::memcpy(f.data(), "BLP2", 4);
    f[0x04] = 1;  // version
    f[0x08] = 2;  // colorEncoding = DXT
    f[0x09] = 8;  // alphaBitDepth
    f[0x0A] = 0;  // preferredFormat = DXT1
    f[0x0B] = 1;  // hasMipmaps
    f[0x0C] = 4;  // width
    f[0x10] = 4;  // height
    uint32_t mipOffset = static_cast<uint32_t>(f.size());
    std::memcpy(f.data() + 0x14, &mipOffset, 4);
    uint32_t mipSize = 8;
    std::memcpy(f.data() + 0x54, &mipSize, 4);
    uint16_t red565 = 31 << 11;
    for (int i = 0; i < 2; ++i) {
        f.push_back(static_cast<uint8_t>(red565));
        f.push_back(static_cast<uint8_t>(red565 >> 8));
    }
    f.insert(f.end(), {0, 0, 0, 0});
    return f;
}

void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

}  // namespace adtfx
