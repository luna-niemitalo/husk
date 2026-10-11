#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "canon_liquid.hpp"    // LiquidType, LiquidObject
#include "canon_material.hpp"  // TextureRef
#include "canon_placement.hpp"
#include "canon_primitives.hpp"
#include "canon_ref.hpp"

// husk::canon: terrain tile -- one ADT tile's heightfield, texture layers,
// ground-cover rules, liquid and placements. Pure value types; built by
// adt_canon_input.hpp, written by writers/terrain_bundle_writer.hpp.
//
// Frame: the tile's own, yards. Axes are WoW world space's (+X north, +Y
// west, +Z up), the origin is the tile's corner vertex (chunk 0, corner
// (0, 0)) at world height 0, so `terrainTileCorner` is all a world needs to
// place the tile. Heights stay absolute. Every on-disk frame variant
// (MDDF/MODF's rotated frame, MCNR's int8 encoding, the low-res/high-res
// hole maps) is collapsed into this one by the input module; nothing here
// records which variant a value came from.
namespace husk::canon {

inline constexpr float kTerrainTileSize = 1600.0f / 3.0f;
inline constexpr int kTerrainChunksPerSide = 16;
inline constexpr float kTerrainChunkSize = kTerrainTileSize / kTerrainChunksPerSide;
inline constexpr int kTerrainQuadsPerSide = 8;
inline constexpr float kTerrainQuadSize = kTerrainChunkSize / kTerrainQuadsPerSide;
inline constexpr int kTerrainVertexCount = 9 * 9 + 8 * 8;
inline constexpr int kTerrainAlphaSize = 64;
inline constexpr int kTerrainTilesPerSide = 64;

// World (X, Y) of tile (tileX, tileY)'s frame origin. The map's origin is
// its centre, so tile (0, 0)'s corner is +32 tiles on both axes; tileX
// steps -Y, tileY steps -X.
inline Vec2 terrainTileCorner(uint32_t tileX, uint32_t tileY) {
    constexpr float kHalfExtent = kTerrainTilesPerSide / 2 * kTerrainTileSize;
    return {kHalfExtent - static_cast<float>(tileY) * kTerrainTileSize,
            kHalfExtent - static_cast<float>(tileX) * kTerrainTileSize};
}

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
    Vec2 origin;         // tile-frame (X, Y) of corner (0, 0)
    std::array<float, kTerrainVertexCount> heights{};  // absolute: world Z
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


struct Terrain {
    Ref subject;  // the tile itself: its root ADT; set by the caller, like Model::subject
    Ref map;  // the map's directory name; no Map.db2 row is resolved
    uint32_t tileX = 0;
    uint32_t tileY = 0;
    std::vector<TerrainTexture> textures;
    std::vector<GroundEffect> groundEffects;
    std::vector<TerrainChunk> chunks;  // index gridY * 16 + gridX
    std::vector<LiquidSurface> liquids;
    // Each LiquidType and LiquidObject the liquids name, once, in first-seen
    // order; a liquid's refs match one entry each.
    std::vector<LiquidType> liquidTypes;
    std::vector<LiquidObject> liquidObjects;

    // What the tile places (REFACTOR/PLACEMENT_SETS.md), in the tile frame:
    // MDDF models and MODF map objects (WMOs). Two sets because their unique
    // ids are separate number spaces. Instance ids are ADT uniqueIds, which
    // are map-wide: the same id in two tiles' sets is one object spanning
    // both, to be loaded once. A map object's activeSets name the WMO doodad
    // sets (by MODS index) it turns on beyond the always-on set 0.
    PlacementSet doodads;
    PlacementSet mapObjects;
};

}  // namespace husk::canon
