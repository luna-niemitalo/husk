#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "canon_material.hpp"  // TextureRef
#include "canon_primitives.hpp"
#include "canon_ref.hpp"

// husk::canon: terrain tile -- WIP (TODO/WORLD/ADT_EXPORT_FINDINGS.md), not
// yet reviewed the way canon::Model's pieces were. Pure value types.
//
// Frame: WoW world space, yards. +X north, +Y west, +Z up -- the frame
// MCNK.position is already in. Every on-disk frame variant (MDDF/MODF's
// rotated frame, MCNR's int8 encoding, the low-res/high-res hole maps) is
// collapsed into this one by the input module; nothing here records which
// variant a value came from.
namespace husk::canon {

inline constexpr float kTerrainTileSize = 1600.0f / 3.0f;
inline constexpr int kTerrainChunksPerSide = 16;
inline constexpr float kTerrainChunkSize = kTerrainTileSize / kTerrainChunksPerSide;
inline constexpr int kTerrainQuadsPerSide = 8;
inline constexpr float kTerrainQuadSize = kTerrainChunkSize / kTerrainQuadsPerSide;
inline constexpr int kTerrainVertexCount = 9 * 9 + 8 * 8;
inline constexpr int kTerrainAlphaSize = 64;

// A ground texture the tile's layers draw from. Repeats `repeatsPerChunk`
// times across one chunk along each axis.
struct TerrainTexture {
    TextureRef diffuse;
    std::optional<TextureRef> height;  // the _h texture for height-based blending
    float repeatsPerChunk = 8.0f;
    float heightScale = 0.0f;   // 0 / 1 (the defaults) mean height blending is off
    float heightOffset = 1.0f;
};

struct TerrainLayerAnimation {
    float directionDegrees = 0.0f;  // UV scroll direction
    uint32_t speed = 0;             // 0..7, unit not established
};

// One texture layer of one chunk. Layer 0 is opaque; each later layer has a
// 64x64 alpha map over the chunk, rows stepping like TerrainChunk rows.
struct TerrainLayer {
    uint32_t textureIndex = 0;  // into Terrain::textures
    std::optional<uint32_t> groundEffectIndex;  // into Terrain::groundEffects
    bool overbright = false;
    std::optional<TerrainLayerAnimation> animation;
    std::vector<uint8_t> alpha;  // kTerrainAlphaSize^2, empty for layer 0
};

// One detail doodad (grass, flowers, pebbles) a ground effect scatters.
struct GroundEffectDoodad {
    Ref doodad;  // Db2Row GroundEffectDoodad
    Ref model;   // FileDataId of the M2
    uint32_t weight = 0;
    uint32_t flags = 0;
};

// The client scatters detail doodads at runtime; what the data records is
// the rule (density + weighted doodad set), not instances. Empty `doodads`
// and no `density` when the GroundEffect DB2s were not available.
struct GroundEffect {
    Ref ref;  // Db2Row GroundEffectTexture
    std::optional<uint32_t> density;
    std::vector<GroundEffectDoodad> doodads;
};

// One 33.33-yard cell: a regular heightfield of 9x9 corner vertices plus
// 8x8 quad-centre vertices, stored interleaved (17 per row pair: 9 corners
// then 8 centres). Corner (r, c) sits at origin - (r, c) * kTerrainQuadSize
// along (X, Y); centre (r, c) at origin - (r + 0.5, c + 0.5) * kTerrainQuadSize.
// Rows step south (-X), columns step east (-Y).
//
// Topology is implied, not stored: each quad is 4 triangles fanned around
// its centre vertex, and a quad whose hole bit is set contributes none.
struct TerrainChunk {
    uint32_t gridX = 0;  // column within tile, steps -Y
    uint32_t gridY = 0;  // row within tile, steps -X
    Vec2 origin;         // world (X, Y) of corner (0, 0)
    std::array<float, kTerrainVertexCount> heights{};  // absolute world Z
    std::array<Vec3, kTerrainVertexCount> normals{};
    std::array<uint8_t, kTerrainQuadsPerSide> holeRows{};  // bit c of [r]: quad (r, c) absent
    Ref area;  // Db2Row AreaTable
    std::vector<TerrainLayer> layers;
    // Per quad: which layer's ground effect applies there, and whether
    // ground effects are suppressed (bit c of [r], same shape as holeRows).
    std::array<uint8_t, kTerrainQuadsPerSide * kTerrainQuadsPerSide> dominantLayer{};
    std::array<uint8_t, kTerrainQuadsPerSide> groundEffectSuppressedRows{};
};

enum class LiquidHeightSource {
    Heightmap,       // per-vertex heights were stored
    MinHeightLevel,  // no vertex data: flat at the instance's min height
    Zero,            // depth-only vertex format: the client renders at 0.0
};

// One liquid instance (MH2O layer) covering a rectangle of one chunk's 8x8
// quad grid. Vertex arrays are (width + 1) * (height + 1), row-major; rows
// step along the chunk's gridY direction, matching TerrainChunk.
struct LiquidSurface {
    uint32_t chunkGridX = 0;
    uint32_t chunkGridY = 0;
    Ref liquidType;    // Db2Row LiquidType
    Ref liquidObject;  // Db2Row LiquidObject, or None when the source field held a vertex format instead
    uint8_t quadX = 0;
    uint8_t quadY = 0;
    uint8_t width = 0;
    uint8_t height = 0;
    std::vector<uint8_t> quadExists;  // width * height, row-major, 0/1
    LiquidHeightSource heightSource = LiquidHeightSource::Heightmap;
    std::vector<float> heights;                // absolute world Z
    std::optional<std::vector<float>> depth;   // 0..1
    std::optional<std::vector<Vec2>> uv;
};

// A reference to another asset placed on this tile. The asset itself is
// not exported here; `asset` carries identity only (BUNDLE_FORMAT.md's
// "a reference may be unresolved").
struct Placement {
    enum class Kind { Model, MapObject };
    Kind kind = Kind::Model;
    Ref asset;
    uint32_t uniqueId = 0;
    Vec3 position;  // world frame
    Quat rotation;  // world-from-model; the asset's own space is its native M2/WMO space
    float scale = 1.0f;
    uint16_t flags = 0;
    std::optional<uint16_t> doodadSet;  // Kind::MapObject only
};

struct Terrain {
    Ref map;  // the map's directory name; no Map.db2 row is resolved
    uint32_t tileX = 0;
    uint32_t tileY = 0;
    std::vector<TerrainTexture> textures;
    std::vector<GroundEffect> groundEffects;
    std::vector<TerrainChunk> chunks;  // index gridY * 16 + gridX
    std::vector<LiquidSurface> liquids;
    std::vector<Placement> placements;
};

}  // namespace husk::canon
