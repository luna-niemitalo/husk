#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <vector>

// ADT v18 (Cata+ split-file) parsing -- WIP, terrain-export testing ground
// (TODO/WORLD/ADT_EXPORT_FINDINGS.md). Raw on-disk shapes only; meaning
// (world coordinates, decoded normals, unified hole masks) is assigned in
// adt_canon_input.cpp.
//
// Chunk tags are byte-reversed on disk, like .phys (WIKI_FINDINGS/WORLD.md).
namespace husk::adt {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline constexpr int kChunksPerSide = 16;
inline constexpr int kChunkCount = kChunksPerSide * kChunksPerSide;
inline constexpr int kHeightCount = 9 * 9 + 8 * 8;

// One root-file MCNK: header fields this exporter reads, plus MCVT/MCNR.
struct MapChunk {
    uint32_t flags = 0;
    uint32_t indexX = 0;
    uint32_t indexY = 0;
    uint32_t areaId = 0;
    std::array<uint8_t, 8> holesHighRes{};  // header bytes 0x14..0x1B, row-major, bit c of byte r
    uint16_t holesLowRes = 0;
    std::array<uint8_t, 16> predominantTexture{};  // uint2_t[8][8], header 0x40
    std::array<uint8_t, 8> noEffectDoodad{};       // uint1_t[8][8], header 0x50
    std::array<float, 3> position{};  // MCNK.position as stored
    std::array<float, kHeightCount> heights{};
    std::array<std::array<int8_t, 3>, kHeightCount> normals{};
};

// The 4 on-disk MH2O vertex layouts (ADT/v18.md "instance vertex data").
enum class LiquidVertexFormat { HeightDepth = 0, HeightUv = 1, DepthOnly = 2, HeightUvDepth = 3 };

struct LiquidInstance {
    uint16_t liquidType = 0;
    uint16_t liquidObjectOrLvf = 0;
    float minHeight = 0;
    float maxHeight = 0;
    uint8_t xOffset = 0;
    uint8_t yOffset = 0;
    uint8_t width = 0;
    uint8_t height = 0;
    std::vector<uint8_t> existsBitmap;  // empty: offset was 0, every quad exists
    std::optional<LiquidVertexFormat> vertexFormat;  // nullopt: offset_vertex_data was 0
    std::vector<float> vertexHeights;
    std::vector<uint8_t> vertexDepths;
    std::vector<std::array<uint16_t, 2>> vertexUvs;
};

struct LiquidChunk {
    std::vector<LiquidInstance> instances;
};

struct RootFile {
    std::vector<MapChunk> chunks;  // kChunkCount, index = indexY * 16 + indexX (checked)
    std::optional<std::array<LiquidChunk, kChunkCount>> liquid;  // nullopt: no MH2O chunk
};

// MDDF, 36 bytes.
struct DoodadPlacement {
    uint32_t nameId = 0;
    uint32_t uniqueId = 0;
    std::array<float, 3> position{};
    std::array<float, 3> rotation{};
    uint16_t scale = 0;
    uint16_t flags = 0;
};

// MODF, 64 bytes.
struct MapObjectPlacement {
    uint32_t nameId = 0;
    uint32_t uniqueId = 0;
    std::array<float, 3> position{};
    std::array<float, 3> rotation{};
    std::array<float, 3> extentsMin{};
    std::array<float, 3> extentsMax{};
    uint16_t flags = 0;
    uint16_t doodadSet = 0;
    uint16_t nameSet = 0;
    uint16_t scale = 0;
};

inline constexpr uint16_t kMddfNameIsFileDataId = 0x40;
inline constexpr uint16_t kModfNameIsFileDataId = 0x8;

struct ObjFile {
    std::vector<DoodadPlacement> doodads;
    std::vector<MapObjectPlacement> mapObjects;
};

// MCLY, 16 bytes.
struct TextureLayer {
    uint32_t textureId = 0;  // index into TexFile::diffuseIds
    uint32_t flags = 0;
    uint32_t offsetInMcal = 0;
    uint32_t effectId = 0;  // GroundEffectTexture row, kNoGroundEffect for none
};

inline constexpr uint32_t kNoGroundEffect = 0xFFFFFFFF;
inline constexpr uint32_t kLayerAnimationRotationMask = 0x7;
inline constexpr uint32_t kLayerAnimationSpeedShift = 3;
inline constexpr uint32_t kLayerAnimationSpeedMask = 0x7;
inline constexpr uint32_t kLayerAnimationEnabled = 0x40;
inline constexpr uint32_t kLayerOverbright = 0x80;
inline constexpr uint32_t kLayerUseAlphaMap = 0x100;
inline constexpr uint32_t kLayerAlphaCompressed = 0x200;

struct TexChunk {
    std::vector<TextureLayer> layers;
    std::vector<uint8_t> mcal;  // raw, decoded by decodeAlphaMap
};

// MTXP, 16 bytes.
struct TextureParams {
    uint32_t flags = 0;
    float heightScale = 0.0f;
    float heightOffset = 1.0f;
};

struct TexFile {
    std::vector<uint32_t> diffuseIds;  // MDID
    std::vector<uint32_t> heightIds;   // MHID, 0 = none
    std::optional<std::vector<TextureParams>> params;  // MTXP, nullopt when absent
    std::array<TexChunk, kChunkCount> chunks;
};

// WDT MPHD flags this exporter reads.
inline constexpr uint32_t kMphdBigAlpha = 0x4;
inline constexpr uint32_t kMphdHeightTexturing = 0x80;
inline constexpr uint32_t kMcnkDoNotFixAlphaMap = 0x8000;

RootFile parseRoot(const std::vector<uint8_t>& bytes);
ObjFile parseObj(const std::vector<uint8_t>& bytes);
TexFile parseTex(const std::vector<uint8_t>& bytes);
uint32_t parseWdtFlags(const std::vector<uint8_t>& bytes);

// One layer's alpha map as 64x64 8-bit, row-major. Collapses the three
// on-disk forms (ADT/v18.md MCAL): RLE-compressed and uncompressed 4096
// (8-bit, when `eightBitAlpha`), and uncompressed 2048 (4-bit, expanded by
// the client's own `v | v << 4`; 63x63 with a duplicated last row/column
// unless `doNotFix`).
std::array<uint8_t, 64 * 64> decodeAlphaMap(const std::vector<uint8_t>& mcal, const TextureLayer& layer,
                                            bool eightBitAlpha, bool doNotFix);

std::vector<uint8_t> readFile(const std::filesystem::path& path);

}  // namespace husk::adt
