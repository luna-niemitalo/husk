#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// WMO (world map object: buildings, caves, dungeons) root and group files,
// per documentation/wowdev-wiki/md/WMO.md, checked against real 12.1 files.
// Layout facts and wiki corrections: WIKI_FINDINGS/WORLD.md. Like ADT, chunk
// tags are byte-reversed on disk.
//
// Positions and normals are read as stored: (X, Y, Z), Z up -- the same frame
// as M2. (WMO.md's "(X,Z,-Y)" note does not describe the bytes: a guard tower's
// groups are tallest on the third component, and doodad rotations turn about it.)
namespace husk::wmo {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;

struct Box {
    Vec3 min{};
    Vec3 max{};
};

// MOHD, 64 bytes.
struct RootHeader {
    uint32_t textureCount = 0;
    uint32_t groupCount = 0;
    uint32_t portalCount = 0;
    uint32_t lightCount = 0;
    uint32_t doodadNameCount = 0;  // not MODI's real length -- see RootFile::doodadFileDataIds
    uint32_t doodadDefCount = 0;
    uint32_t doodadSetCount = 0;
    uint32_t ambientColor = 0;  // CArgb, raw
    uint32_t wmoAreaTableId = 0;
    Box bounds;
    uint16_t flags = 0;
    uint16_t lodCount = 0;
};

// MOMT, 64 bytes.
struct Material {
    uint32_t flags = 0;
    uint32_t shader = 0;
    uint32_t blendMode = 0;  // EGxBlend
    std::array<uint32_t, 3> textures{};  // FileDataIDs, or MOTX byte offsets when the root has MOTX; 0 = none
    uint32_t sidnColor = 0;  // CImVector (BGRA), raw
    uint32_t diffuseColor = 0;
    uint32_t groundType = 0;  // TerrainType row
    uint32_t color2 = 0;
    uint32_t flags2 = 0;
};

// MOGI, 32 bytes.
struct GroupInfo {
    uint32_t flags = 0;
    Box bounds;
    int32_t nameOffset = -1;  // into RootFile::groupNames, -1 = unnamed
};

// MODS, 32 bytes.
struct DoodadSet {
    std::string name;
    uint32_t startIndex = 0;  // into RootFile::doodads
    uint32_t count = 0;
};

// MODD, 40 bytes.
struct Doodad {
    uint32_t nameIndex = 0;  // MODI index (or MODN byte offset on pre-8.1 files)
    uint8_t flags = 0;
    Vec3 position{};
    std::array<float, 4> rotation{};  // quaternion x, y, z, w
    float scale = 1.0f;
    uint32_t color = 0;  // CImVector (BGRA), raw
};

struct RootFile {
    uint32_t version = 0;
    RootHeader header;
    std::vector<Material> materials;
    std::string textureNames;  // raw MOTX blob; empty: material textures are FileDataIDs
    std::string groupNames;    // raw MOGN blob
    std::vector<GroupInfo> groups;
    // GFID: row-major [lodTier][group], 0 = no file for that cell. Empty when
    // the root has no GFID chunk.
    std::vector<uint32_t> groupFileDataIds;
    std::vector<DoodadSet> doodadSets;
    std::vector<uint32_t> doodadFileDataIds;  // MODI, sized off the chunk, never header.doodadNameCount
    std::string doodadNames;                  // raw MODN blob (pre-8.1)
    std::vector<Doodad> doodads;
    std::vector<float> doodadColorMultipliers;  // MDDI, parallel to doodads; empty when absent
    // MOUV: per material, two layers' UV scroll speed. Empty when absent (all zero).
    std::vector<std::array<Vec2, 2>> uvScrollSpeeds;
    uint32_t skyboxFileDataId = 0;  // MOSI; 0 = none
    std::string skyboxName;         // MOSB; empty = none
};

// MOBA, 24 bytes (Legion+ layout).
struct Batch {
    uint32_t startIndex = 0;  // into GroupFile::indices
    uint32_t indexCount = 0;
    uint16_t minVertex = 0;
    uint16_t maxVertex = 0;
    uint32_t material = 0;  // index into RootFile::materials (material_id or material_id_large, per the flag)
};

// MOPY (2 bytes) or MPY2 (4 bytes) per triangle.
struct TriangleInfo {
    uint16_t flags = 0;
    uint16_t material = 0;  // 0xFF (MOPY) / 0xFFFF (MPY2): collision-only, not rendered
};

// One group file: the MOGP header plus its sub-chunks.
struct GroupFile {
    uint32_t version = 0;
    uint32_t nameOffset = 0;
    uint32_t descriptiveNameOffset = 0;
    uint32_t flags = 0;
    Box bounds;
    uint16_t portalStart = 0;
    uint16_t portalCount = 0;
    uint16_t transparentBatchCount = 0;
    uint16_t interiorBatchCount = 0;
    uint16_t exteriorBatchCount = 0;
    std::array<uint8_t, 4> fogIds{};
    uint32_t liquidType = 0;
    uint32_t wmoAreaTableId = 0;
    uint32_t flags2 = 0;

    std::vector<TriangleInfo> triangles;
    std::vector<uint32_t> indices;  // MOVI (u16) or MOVX (u32), widened
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::vector<Vec2>> uvSets;            // every MOTV, in file order
    std::vector<std::vector<uint32_t>> colorSets;     // every MOCV (BGRA, raw), in file order
    std::vector<Batch> batches;
};

// Throws ParseError when a required chunk (MVER, MOHD) is missing, a chunk's
// size doesn't fit its record size, or a record runs past its chunk.
RootFile parseRoot(const std::vector<uint8_t>& fileBytes);

// Throws ParseError when MVER or MOGP is missing, MOGP is shorter than its
// 68-byte header, a per-vertex chunk's count differs from MOVT's, or a batch
// runs past the index buffer.
GroupFile parseGroup(const std::vector<uint8_t>& fileBytes);

// GFID cell for (lodTier, group); 0 when the cell is empty or out of range.
uint32_t groupFileDataId(const RootFile& root, uint32_t lodTier, uint32_t group);

// The zero-terminated string starting at `offset` in a MOGN/MOTX/MODN blob.
// Throws ParseError when `offset` is past the blob.
std::string stringAt(const std::string& blob, uint32_t offset);

}  // namespace husk::wmo
